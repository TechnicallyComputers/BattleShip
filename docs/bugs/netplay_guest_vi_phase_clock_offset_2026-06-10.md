# Netplay guest VI phase latch used raw local wall clock

**Date:** 2026-06-10  
**Status:** Fix shipped — barrier=0 monotonic fallback added 2026-06-10 (soak pending)  
**Companion:** [`netplay_cross_os_pacing_symmetry_2026-05-27.md`](netplay_cross_os_pacing_symmetry_2026-05-27.md)

## Symptom

Android host ↔ Windows guest VS: hashes stay matched (`rb=0`), but Windows accumulates large `decouple_skips` (~14% of `PortPushFrame` calls) and both peers feel ~45 Hz despite `contract_hz=60`. At `both_sides_latched_startup` the guest logs:

```
decouple_vi_phase_bias_ns=-2699999892 phase_delta=-162 agreed_phase=... local_phase=...
```

while the host reports `phase_delta=+2`.

## Root cause

`syNetPeerCurrentViPhaseBucketNow()` used each peer's **raw** `unix_ms / gran_ms` bucket. The agreed phase on the guest comes from the **host** wire (`ExecSyncPeerViPhaseLatch`). Device wall clocks differ by seconds, so `local_phase - agreed_phase` was dominated by clock skew (~162 VI buckets ≈ 2.7 s), not real sim-grid misalignment.

The guest barrier path already converts host schedule to local time via `start_ms - offset_ms`; latch bias did not.

## Fix

**Path A (clock barrier):** On guest after `BATTLE_START_TIME` latch: map VI phase buckets with `host_unix ≈ guest_local - BattleStartOffsetMs` (same offset from clock-sync median).

**Path B (automatch `barrier=0`):** When guest receives host `BATTLE_EXEC_SYNC`, latch `host_vi_phase` as epoch and advance buckets by monotonic elapsed ms / `gran_ms`. Avoids comparing raw device `unix_ms` buckets when clock-sync never ran (LAN automatch soak).

**Path B2 (gameloop decouple):** At `both_sides_latched_startup`, pass exec-sync `syNetPeerOsMonotonicMs()` anchor to `port_set_vs_decouple_exec_sync_monotonic_anchor`. First `PortPushFrame` decouple init sets `sVsNextSimStepDeadline` to the next contract-VI grid line from that anchor (replacing raw `now` + wall-clock bias when anchor is present).

Additional pacing regressions from the VS push throttle patch (same milestone):

- Restore idle `PresentCurrentFramebuffer` on decouple sim-skip frames (display was freezing during skip spins).
- Remove push/sim deadline snap (long sleeps when sim deadline sat far ahead).
- Decoupled VS wall cap defaults to contract VI Hz on **all** platforms (cross-OS parity without env).
- **Unified decouple grid (2026-06-10):** `sVsNextSimStepDeadline` is the sole grid for sim stepping and wall-slot sleep; skip frames advance the same deadline at frame end so `push_wall` and `sim_adv` stay 1:1 when wall cap is active.
- **Sim-led decouple (2026-06-10):** When exec-sync monotonic anchor is latched, `PortPushFrame` uses `anchor + syNetInputGetTick() * gran_ms` (monotonic) for sim offers and wall-slot sleep instead of advancing a steady_clock deadline. Cross-OS VS enables contract-Hz wall cap automatically when the anchor is present (no per-platform env). Late sim slip reverted (skip-late only on legacy grid).

## Verify

Cross-OS soak (Android host + Windows guest): at latch expect `|phase_delta| ≤ few buckets` on both peers; Windows `decouple_skips` should stay near host (O(1)) through tick 720+; host `pct_R` should drop if guest ingress cadence improves.
