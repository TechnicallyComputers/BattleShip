# Netplay Windows timer sleep pacing (waitable timer + late-skip)

**Date:** 2026-07-11  
**Status:** Fix implemented — cross-OS soak pending  
**Companion:** [`netplay_cross_os_pacing_symmetry_2026-05-27.md`](netplay_cross_os_pacing_symmetry_2026-05-27.md), [`netplay_guest_vi_phase_clock_offset_2026-06-10.md`](netplay_guest_vi_phase_clock_offset_2026-06-10.md)

## Problem

Windows netplay cadence lagged Linux/Android peers even when hashes matched:

- `syNetPeerOsSleepMicros` used `Sleep()`, which is ~1 ms best-case and often ~15.6 ms without process timer resolution.
- VS decouple wall slots in `PortPushFrame` used `std::this_thread::sleep_for` (same coarse OS path).
- Sim-led finish path slept a **full** `gran_ms` (~17 ms) when a skip frame was already past its slot — compounding `decouple_skips` and feeling ~45 Hz.

Fast3D present pacing already used `CreateWaitableTimerExW(...HIGH_RESOLUTION)`; netplay bootstrap / wall-slot sleeps did not.

## Fix

1. **`port/net/sys/netpeer_socket_platform.c` (Win32):** lazy-init high-res waitable timer (fallback auto-reset waitable timer), then `SetWaitableTimer` + `WaitForSingleObject` for `syNetPeerOsSleepMicros`. Keep `Sleep()` only if the timer path fails.
2. **`port/gameloop.cpp`:** route VS wall-slot and sync-present-hold sleeps through `syNetPeerOsSleepMicros`.
3. **Late skip:** if already at/past the current sim-led slot, do **not** sleep another full period. Legacy grid catch-up sets deadline to `now` instead of `now + period`.

## Verify

Cross-OS soak (Windows guest ↔ Linux/Android host) with `SSB64_NETPLAY_CROSS_OS_PACING_DIAG=1`:

- Windows `decouple_skips` near host (O(1) through tick 720+), not ~14% of `PortPushFrame`.
- Felt cadence near `contract_hz` (typically 60).
- Bootstrap retry slices still yield via 1 ms chunks in `syNetPeerSleepBootstrapRetry`.
