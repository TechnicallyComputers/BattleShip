# Android Orientation, Landscape Lock & Surface Geometry

Reference for how screen orientation and window/surface geometry behave in the Android port,
why the game wasn't actually landscape-locked, and the surface-resize crash that exposed it.

Bug postmortem: [`docs/bugs/android_surface_resize_blast_abort_2026-06-26.md`](bugs/android_surface_resize_blast_abort_2026-06-26.md).

## TL;DR

- The manifest declares `android:screenOrientation="landscape"`, but **SDL2 overrides it at
  window creation**. With no orientation hint set and a resizable SDL window, SDL requested
  `SCREEN_ORIENTATION_FULL_USER`, so the app followed the OS auto-rotate lock and opened in the
  device's natural **portrait** orientation (users had to enable rotation to reach landscape).
- That made the surface rotate/resize at runtime. On a geometry change, `eglSwapBuffers`
  presented a stale-size buffer, SurfaceFlinger rejected the Binder transaction
  (`BLASTBufferQueue FAILED_TRANSACTION`), and the process **fatally aborted** on the SDL render
  thread.
- **Fix:** set `SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight")` before the
  window is created (`port/port.cpp` `portAndroidJniWarmupEarly`). SDL then requests
  `SCREEN_ORIENTATION_SENSOR_LANDSCAPE` — landscape-only, rotating between the two landscape
  orientations, never portrait.

## How orientation is actually decided in this port

There are two layers, and the second wins:

### 1. Manifest (intent, but overridden)

`android/app/src/main/AndroidManifest.xml` sets `android:screenOrientation="landscape"` and
`android:resizeableActivity="true"` on `BattleShipActivity`. The manifest value is the *initial*
request only; SDL changes it at runtime.

### 2. SDL2 runtime override (authoritative)

When native SDL creates the window, it calls `SDLActivity.setOrientation(w, h, resizable, hint)`
→ `setOrientationBis()`, which ends in `setRequestedOrientation(req)` — clobbering the manifest.
`req` is chosen from the `SDL_HINT_ORIENTATIONS` string + the window's `resizable` flag + w/h:

```
no hint + resizable      -> SCREEN_ORIENTATION_FULL_USER       (follows OS auto-rotate; natural = portrait)
no hint + non-resizable  -> SENSOR_LANDSCAPE or SENSOR_PORTRAIT (from w>h)
"LandscapeLeft LandscapeRight"        -> SCREEN_ORIENTATION_SENSOR_LANDSCAPE   (landscape both ways)
"LandscapeLeft" / "LandscapeRight"    -> fixed single landscape
"Portrait" (+/- "PortraitUpsideDown") -> portrait variants
hint with both portrait & landscape + resizable -> FULL_USER
```

(See `android/app/src/main/java/org/libsdl/app/SDLActivity.java` `setOrientationBis`.)

**Why we landed in `FULL_USER`:**
- `SDL_HINT_ORIENTATIONS` was **never set** anywhere in C/C++ (only `SDLActivity.java` reads it).
- The LUS window is created **resizable**: `libultraship/src/fast/backends/gfx_sdl2.cpp` uses
  `SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI`.

`FULL_USER` = "all orientations, obey the user's rotation lock." With auto-rotate off (the common
default) the device stays in its natural orientation — portrait on phones — so the game opened in
portrait despite the manifest.

## The geometry / surface crash

Because the app rotated/resized at runtime, the EGL swapchain could get out of sync with the
window. On a geometry change the render thread kept presenting a stale-size buffer:

```
E BLASTBufferQueue: Transaction Failure Details: Status: -2147483646 (FAILED_TRANSACTION) ... Buffer Size: 943x2231
F BLASTBufferQueue: acquireNextBufferLocked failed to apply transaction. status=-2147483646
F runtime.cc:761] native: SDL_EGL_SwapBuffers -> Android_GLES_SwapWindow -> SDL_GL_SwapWindowWithResult
                          -> Fast::GfxWindowBackendSDL2::SwapBuffersBegin -> Fast::Interpreter::EndFrame
                          -> Fast::Fast3dWindow::DrawAndRunGraphicsCommands -> port_drain_pending_display_list
                          -> PortPushFrame -> SDL_main
```

`BLASTBufferQueue` logs the failed SurfaceFlinger transaction at **Fatal** priority, aborting the
process (SIGABRT on the SDL render thread; the port crash handler then prints an empty backtrace
because the abort is the graphics path, not the game/main coroutine). It coincided with a netplay
match but is purely a graphics/surface lifecycle bug.

## The fix (landscape lock)

`port/port.cpp`, `portAndroidJniWarmupEarly()` (runs before `PortInit()` → LUS window creation):

```c
SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
```

This routes `setOrientationBis()` to `SCREEN_ORIENTATION_SENSOR_LANDSCAPE`, overriding
`FULL_USER` regardless of the user's auto-rotate setting. The game now opens in landscape and
only ever flips between the two landscape orientations — removing the portrait↔landscape resize
that triggered the BLAST abort.

The hint must be set **before** `SDL_CreateWindow`; `portAndroidJniWarmupEarly()` runs ahead of
`PortInit()`, so the ordering is correct. The manifest `screenOrientation="landscape"` is kept
(harmless, documents intent) but is not sufficient on its own — SDL overrides it.

## What's still exposed (and hardening options)

Landscape lock removes the dominant trigger (phone rotation) but the surface can still change
geometry from:

| Source | Covered by landscape lock? | Mitigation |
|--------|----------------------------|------------|
| Sensor rotation portrait↔landscape | Yes (eliminated) | — |
| Multi-window / split-screen / freeform | No (`screenOrientation` ignored there) | `android:resizeableActivity="false"` |
| Foldable posture change | No | resizeable=false + posture handling |
| Inset / cutout / gesture-nav settling | No (still resizes width) | present-path surface guard |

**Durable hardening (recommended follow-up):** a present-path guard in
`GfxWindowBackendSDL2::SwapBuffersBegin` / the port's SDL window-event handling that reacts to
`SDL_WINDOWEVENT_SIZE_CHANGED` and surface destroyed/created by reconfiguring the EGL surface and
skipping `eglSwapBuffers` while the surface size doesn't match the window. That protects against
*any* geometry change, not just rotation.

## Testing checklist

- App opens directly in **landscape** with OS auto-rotate **off** (no manual rotation needed).
- Rotating the device flips only between the two landscape orientations; no portrait layout.
- No `BLASTBufferQueue FAILED_TRANSACTION` / fatal in `eglSwapBuffers` on orientation change.
- (If `resizeableActivity` left true) sanity-check split-screen doesn't immediately abort.

## Build / verification notes

- The hint lives in `#if defined(__ANDROID__)` code; desktop builds exclude it.
  `SDL_HINT_ORIENTATIONS` is present in the vendored SDL 2.32.10.
- The Android NDK/Gradle build is not runnable in the dev sandbox — verify on-device.

## Key references

- `port/port.cpp` — `portAndroidJniWarmupEarly()` (the hint), `portAndroidJniWarmupLate()`.
- `android/app/src/main/java/org/libsdl/app/SDLActivity.java` — `setOrientationBis()`.
- `libultraship/src/fast/backends/gfx_sdl2.cpp` — window creation flags, `SwapBuffersBegin`.
- `android/app/src/main/AndroidManifest.xml` — `screenOrientation`, `resizeableActivity`.
- Postmortem: [`docs/bugs/android_surface_resize_blast_abort_2026-06-26.md`](bugs/android_surface_resize_blast_abort_2026-06-26.md).
