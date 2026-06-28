# Android BLASTBufferQueue FAILED_TRANSACTION abort — orientation not actually landscape-locked

**Date:** 2026-06-26
**Status:** Fix implemented (Android build/soak pending; landscape lock removes the rotation trigger)
**Area:** `port/port.cpp` (`portAndroidJniWarmupEarly`), SDL2 orientation handling, `android/app/.../AndroidManifest.xml`

## Symptom

Mid-session **SIGABRT** on the SDL render thread (empty in-process backtrace via the port
crash handler). logcat shows the real cause and the faulting thread's frames:

```
E BLASTBufferQueue: Transaction Failure Details: Status: -2147483646 (FAILED_TRANSACTION) ... Frame Number: 509, Buffer Size: 943x2231
F BLASTBufferQueue: acquireNextBufferLocked failed to apply transaction. status=-2147483646
F ... runtime.cc:761]  native: SDL_EGL_SwapBuffers -> Android_GLES_SwapWindow -> SDL_GL_SwapWindowWithResult
                                -> Fast::GfxWindowBackendSDL2::SwapBuffersBegin -> Fast::Interpreter::EndFrame
                                -> Fast::Fast3dWindow::DrawAndRunGraphicsCommands -> port_drain_pending_display_list
                                -> PortPushFrame -> SDL_main
```

Preceded by window geometry changes (`WindowInsets changed: 2231x1080 -> 2340x1080`) and a
portrait-shaped buffer (`943x2231`). The crash coincided with a netplay match but is **not**
a netplay bug — it was simply what was on screen during a surface geometry change.

## Root cause

`eglSwapBuffers` presented a stale-size buffer against a resized/rotated surface, so
SurfaceFlinger rejected the Binder transaction (`FAILED_TRANSACTION`) and `BLASTBufferQueue`
logged it at **Fatal** priority → process abort.

The geometry change happened because **the app was not actually landscape-locked at runtime**,
despite `android:screenOrientation="landscape"` in the manifest. SDL2 overrides the manifest
at window creation: in `SDLActivity.setOrientationBis()`, with **no `SDL_HINT_ORIENTATIONS`
set** and a **resizable SDL window** (`libultraship/src/fast/backends/gfx_sdl2.cpp` creates the
window with `SDL_WINDOW_RESIZABLE`), it calls
`setRequestedOrientation(SCREEN_ORIENTATION_FULL_USER)`. `FULL_USER` follows the OS auto-rotate
lock, so on a phone the app defaulted to the device's natural **portrait** orientation and the
user had to enable OS rotation to reach landscape — then every portrait↔landscape rotation
resized the surface and risked the BLAST abort.

## Fix

Set the SDL orientation hint **before LUS creates the window** so SDL locks to landscape
(`SENSOR_LANDSCAPE` — rotates only between the two landscape orientations, never portrait):

```c
/* port/port.cpp : portAndroidJniWarmupEarly() (runs before PortInit -> window creation) */
SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
```

With both landscape tokens present, `setOrientationBis()` takes the "landscape allowed" branch
and requests `SCREEN_ORIENTATION_SENSOR_LANDSCAPE`, overriding `FULL_USER` regardless of the
manifest or the user's auto-rotate setting. The manifest `screenOrientation="landscape"` alone
was insufficient because SDL clobbers it at runtime.

## Caveats / remaining exposure

Landscape lock removes the portrait↔landscape rotation (the primary trigger on a phone) but the
surface can still resize from:

- **Multi-window / split-screen / freeform** — `screenOrientation` is ignored there, and
  `android:resizeableActivity="true"`. Consider `resizeableActivity="false"` to close this.
- **Foldables** and **inset/cutout settling** (the `2231 -> 2340` width change observed).

The fully robust hardening is a **present-path surface guard**: react to
`SDL_WINDOWEVENT_SIZE_CHANGED` / surface destroyed-created by reconfiguring the EGL surface and
skipping `eglSwapBuffers` while the surface size does not match the window. That protects
against any geometry change, not just rotation. Tracked as follow-up.

## Verification

- Desktop `build-netmenu` rebuilds clean (the change is inside `#if defined(__ANDROID__)`;
  `SDL_HINT_ORIENTATIONS` confirmed present in vendored SDL 2.32.10). **Android NDK/Gradle build
  not runnable in the dev sandbox — build + on-device check required.**
- On device: app launches directly in landscape without enabling OS rotation; rotating the
  device flips only between the two landscape orientations; no portrait layout; no
  `BLASTBufferQueue FAILED_TRANSACTION` abort on orientation change.

## Audit hook

`BLASTBufferQueue FAILED_TRANSACTION` / fatal in `eglSwapBuffers` → surface geometry vs
swapchain mismatch (resize/rotation/multi-window), not a renderer logic bug. Check that SDL is
actually orientation-locked (`SDL_HINT_ORIENTATIONS`), not just the manifest — SDL overrides the
manifest at window creation.
