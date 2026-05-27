# Android debug restart SIGSEGV at Port menu init

**Status:** FIX SHIPPED (device soak pending)  
**Package:** `com.jrickey.battleship.netplay.debug`  
**Symptom:** After **Restart in Debug Mode**, `ssb64-debug.log` stopped at Port menu attach with `SIGSEGV fault_addr=0x0` / `x0=0`, often with an empty FP backtrace. Logcat sometimes showed `Port menu attached` then process death ~120 ms later, before `PortInit complete` / `debug session ready`.

## Root causes (two contributors)

### 1. ImGui menu font merge on failed TTF load

`Menu::EnsureMenuFontsLoaded()` calls `AddMergedMenuFont()` with `assets/custom/fonts/Montserrat-Regular.ttf` when `FindMenuAssetPath()` finds any path. On Android the font is usually **not** shipped under `externalFilesDir`; a stray or corrupt file at that relative path can make `AddFontFromFileTTF` return **NULL**. ImGui Font Awesome merge (`MergeMode`) targets the **last added** font — merging into NULL **SIGSEGVs** during `PortMenu::Init()`.

**Fix:** [`port/gui/Menu.cpp`](../../port/gui/Menu.cpp) — fall back to `AddFontDefault` when TTF load fails; skip icon merge if no font.

### 2. Boot ordering (JNI, window lifetime, logcat)

Several ordering bugs showed up only on **debug-session fast restart** (toast OK, native dies before `FileDropMgr OK`):

- `SDL_INIT_GAMECONTROLLER` ran before `SDL_INIT_VIDEO` (controller warm-up before `InitWindow`).
- A scoped `Fast3dWindow` local was destroyed at block end while attaching the menu (use `sContext->GetWindow()` instead).
- Debug `port_log` mirrored every line to logcat during GL/window setup (extra JNI).

**Fix:** [`port/port.cpp`](../../port/port.cpp)

- `portAndroidJniWarmupEarly()` before `PortInit()` (display hint + `SDL_getenv` only).
- `portAndroidJniWarmupLate()` after `InitWindow` (gamecontroller + aaudio).
- `port_watchdog_init()` before `PortInit()` on Android so PortInit crashes are logged.
- Defer `PortMenu::Init()` (`Gui::SetMenu(menu, false)`); `portAndroidFinishPortMenuInit()` after `PortInit complete`.
- `port_log_set_android_logcat_mirror(1)` only after PortInit when debug is active ([`port/port_log.c`](../../port/port_log.c)).

### 3. Crash handler noise (diagnostics only)

`port_watchdog::CrashSignalHandler` called `Fast::DumpDLDiag()` (spdlog, not async-signal-safe). On Android this sometimes produced a **second** fault (`pc≈lr`, empty backtrace) and hid the real site in exported logs.

**Fix:** Skip `DumpDLDiag` in the Android crash handler ([`port/port_watchdog.cpp`](../../port/port_watchdog.cpp)).

### 4. Debug restart launched game before GLES surface (2026-05 follow-up)

`BootActivity` called `startGame()` synchronously from `onCreate` on `EXTRA_DEBUG_RESTART`. A **100 ms** `Handler` delay did not fix the crash: logcat showed `surfaceCreated` before `SDL_main` on the second launch.

### 5. Two SDL_main threads in one process (root cause for Fast3d SIGSEGV)

Logcat (same PID): first `SDL_main` thread still running after **Restart in Debug Mode**; second `nativeRunMain` starts; **`surfaceDestroyed` ~13 ms later** while the new `PortInit` runs `Fast3dWindow` → null EGL / `SIGSEGV`.

`SDL_main` is not stopped by `Activity.finish()`; `CLEAR_TASK` + new `BattleShipActivity` (`singleInstance`) did not tear down the native thread.

**Fix (cooperative, in-process):** Native `Window::Close()` + in-process `SDL_main` return. [`BattleShipActivity#onDestroy`](../../android/app/src/main/java/com/jrickey/battleship/BattleShipActivity.java) clears `mSDLThread` before [`SDLActivity#onDestroy`](../../android/app/src/main/java/org/libsdl/app/SDLActivity.java) (no main-thread join deadlock), then **`Handler.postDelayed` → Boot** (~150 ms) without gating on `Thread.join` (background join is telemetry-only). JNI + [`SDLMain.run`](../../android/app/src/main/java/org/libsdl/app/SDLActivity.java) set `sCooperativeMainExited`. No `killProcess`. [`port/android_debug_restart.c`](../../port/android_debug_restart.c) skips `_exit` on restart return.

**Native:** [`port/port.cpp`](../../port/port.cpp) — split log lines for `Fast3dWindow` construct vs `InitWindow`; removed early `port_watchdog_init()` before `PortInit` (watchdog remains in `PortGameInit`).

## Verification

1. Install netplay debug APK; open Port menu → **Restart in Debug Mode**.
2. Logcat (`ssb64.debug`, `ssb64.boot`, `SDL`) should show, in order:
   - `cooperative shutdown: finishing activity`
   - `SDL_main returned` (JNI and/or SDLMain)
   - `onDestroy begin` → `onDestroy end` (no main-thread `nativeSendQuit + join`)
   - `posting Boot in 150ms (not gated on join)`
   - `launching BootActivity after destroy`
   - optional `background join: …` (telemetry, may finish after Boot)
   - `ssb64.boot: debug session restart`
   - second `Running main function SDL_main`
3. `ssb64-debug.log` truncates with fresh `debug session start mode=log_only` and `debug session ready`.
4. No `!!!! CRASH SIGSEGV` before automatch / matchmaking lines.

## Related

- Android JNI coroutine note in `port/port.cpp` (`SDL_HINT_DISPLAY_USABLE_BOUNDS`).
- Debug logging plan: `docs/netplay_environment_variables.md` (Debug Mode defaults).
