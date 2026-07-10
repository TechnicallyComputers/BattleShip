# macOS Clean Quit SIGABRT During `__cxa_finalize`

**Resolved:** 2026-05-24 (macOS / POSIX voluntary exit)
**Where:** `port/port.cpp` — `main()` return after `PortShutdown()`
**Symptom:** Opening the Port menu (ESC) and quitting (or otherwise closing the app
after ~2–3 s) produced `SIGABRT` in `ssb64.log` with a backtrace only in
`__cxa_finalize_ranges` → `std::terminate` → `abort` — no BattleShip frames on
the faulting thread. macOS Crash Reporter showed the same pattern (~2.7 s uptime).

## Root cause

`PortShutdown()` already drops `sContext`, audio bridge refs, and closes
`ssb64.log` before `main` returns. Returning from `main` still runs libc++
static destructors (`__cxa_finalize`). Other translation units (spdlog async pool,
`BS::thread_pool`, remaining `Ship::IResource` holders, etc.) can still call
into logging or Context during that phase and hit `std::terminate()` → `abort()`.
The port watchdog’s `SIGABRT` handler then logs the “!!!! CRASH SIGABRT” block even
though shutdown was intentional.

This is the same lifecycle class as the comment at `port/port.cpp` first-run
wizard cancel (IResource destructors after spdlog shutdown), not an ImGui/Metal
menu-draw fault.

## Fix

On POSIX (`__unix__` / `__APPLE__`), after intentional teardown call `_exit(code)`
via `port_exit_process()` instead of `return` from `main`, skipping static C++
destructors. Matches the existing `_exit(0)` pattern in `port/bridge/lbreloc_bridge.cpp`
for stale `BattleShip.o2r`.

Windows keeps `std::exit()` so CRT/SDL teardown order stays unchanged.

## Verification

1. macOS netplay build: launch → ESC → Quit (power icon). Process should exit
   without `!!!! CRASH SIGABRT` in `~/Library/Application Support/BattleShip/ssb64.log`.
2. Last log lines should include `clean shutdown — skipping static C++ destructors`.
