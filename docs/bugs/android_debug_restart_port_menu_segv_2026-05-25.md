# Android debug restart SIGSEGV at Port menu init

**Status:** RESOLVED (manual launcher relaunch)  
**Package:** `com.jrickey.battleship.netplay.debug`  
**Symptom:** After **Restart in Debug Mode**, `ssb64-debug.log` stopped at Port menu attach with `SIGSEGV fault_addr=0x0` / `x0=0`, often with an empty FP backtrace. Logcat sometimes showed `Port menu attached` then process death ~120 ms later, before `PortInit complete` / `debug session ready`.

## Root causes (fixed)

### 1. ImGui menu font merge on failed TTF load

`Menu::EnsureMenuFontsLoaded()` could merge Font Awesome into a NULL font when Montserrat failed to load on Android.

**Fix:** [`port/gui/Menu.cpp`](../../port/gui/Menu.cpp) — fall back to `AddFontDefault`; skip icon merge if no font.

### 2. Boot ordering (JNI, window lifetime, logcat)

Controller warm-up before `InitWindow`, deferred `PortMenu::Init()`, logcat mirror only after `PortInit` when debug is active.

**Fix:** [`port/port.cpp`](../../port/port.cpp), [`port/port_log.c`](../../port/port_log.c).

### 3. Crash handler noise (diagnostics only)

**Fix:** Skip `DumpDLDiag` in the Android crash handler ([`port/port_watchdog.cpp`](../../port/port_watchdog.cpp)).

## In-process auto-relaunch (not supported)

Cooperative `SDL_main` return + BootActivity + second `BattleShipActivity` in the same process was attempted but unreliable (SDL thread / `nativeQuit` races, flash “SDL Error” dialogs, stuck teardown). **Supported Android flow:** arm debug mode from the Port menu, **close the app**, **open from the launcher** — [`DebugSessionHelper`](../../android/app/src/main/java/com/jrickey/battleship/DebugSessionHelper.java) writes `.battleship_debug_session`; the next `main()` consumes it and truncates `ssb64-debug.log`.

## Verification (manual relaunch)

1. Install netplay debug APK; open Port menu → **Restart in Debug Mode** (toast: close app, relaunch from launcher).
2. Leave the app (recents swipe or back).
3. Open from launcher → logcat tag `ssb64`: `debug session start mode=log_only`, `debug session ready`.
4. `ssb64-debug.log` under `Android/data/<package>/files/` has a fresh session banner.
5. No SIGSEGV before automatch / matchmaking lines.

```bash
adb logcat -d | grep -E 'ssb64\.debug|ssb64:|AndroidRuntime|FATAL'
adb shell ls -la /sdcard/Android/data/com.jrickey.battleship.netplay.debug/files/.battleship_debug_session
```

(Sentinel absent after step 3 — consumed on launcher start.)

## Related

- [`docs/netplay_environment_variables.md`](../netplay_environment_variables.md) — Debug Mode defaults and workflow.
