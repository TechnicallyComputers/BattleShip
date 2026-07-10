# Netplay — Ness PK Thunder jibaku resim self-hit + ephemeral tracking (2026-07-10)

**Date:** 2026-07-10  
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)  
**Follow-up to:** [`netplay_ness_pkthunder_jibaku_air_arc_quantize_2026-07-10.md`](netplay_ness_pkthunder_jibaku_air_arc_quantize_2026-07-10.md), [`netplay_ness_pkthunder_jibaku_resim_hold_drift_2026-07-10.md`](netplay_ness_pkthunder_jibaku_resim_hold_drift_2026-07-10.md)

## Symptom / audit

After hold-resim and air-arc quantize fixes, remaining jibaku resim gaps could still fork
cross-ISA:

1. **Self-hit collide** — `ftNessSpecialHiCheckCollidePKThunder` compared raw fighter/head
   positions against `FTNESS_PKTHUNDER_COLLIDE_*`. Sub-grid ULP can fire jibaku one tick
   early/late (`status_total_tics` 7 vs 8 at FC snap).
2. **Ephemeral gate RAM** — `HoldEntryTick` / `ThrowEntryTick` / `AirJibakuStartTick` survived
   rollback load when non-zero; reconstruct only filled zeros → stale forward-path tracking
   poisoned gravity sanitize / launch-guard during resim.
3. **Apply path** — snapshot apply ran jibaku catch-up without `JibakuSimState` canonicalize.
4. **Slide-off / bound / ground decel / ProcMap** — incomplete post-transition quantize hooks.

## Fix

| Layer | Change |
|-------|--------|
| **Self-hit** | Netplay path in `CheckCollidePKThunder` — quantize fighter/head + distances before box test; quantize stored `pkthunder_pos` |
| **Hold tracking** | `SyncHoldEntryTracking` always rebuilds entry tick + gravity from `status_total_tics`; `ReconstructThrowEntryTick` always overwrites |
| **Air jibaku start** | `ResimReplayHardeningAfterLoadStep` rebuilds `AirJibakuStartTick` from live tics |
| **Load harden** | Call hardening after **every** resim load (not only FC recovery) |
| **Apply** | `CanonicalizeNessPKJibakuSimState` after catch-up on snapshot apply |
| **Sanitize** | Slot-apply sanitize also covers jibaku status vars |
| **Slide-off** | `HardenPKJibakuAirVelFromAngle` after ArcTan2 |
| **Bound** | `JibakuSimState` after `BoundSetStatus` reflect/scale |
| **Ground decel** | Quantize `vel_ground.x` before air transfer |
| **ProcMap** | End-of-`AirHiJibakuProcMap` canonicalize |

## Regression (2026-07-10) — air hold fall pinned at −0.5

**Symptom:** Air PK Thunder Hold drifts at a constant −0.5 Y/tick instead of vanilla accelerating fall.

**Cause:** Always-overwriting `HoldEntryGravityDelay = live + hold_frames` after gravity hit 0 made
`throw_frames` reconstruct as exactly `GRAVITY_DELAY` every tick → `CanonicalPKThunderHoldFallVelY`
stuck at `gravity_steps=1`.

**Fix:** Keep entry gravity stable once countdown finishes; only force-rebuild `ThrowEntryTick` once
after snapshot load (`syNetplayNessResimHardeningAfterSnapshotLoad`), not every sanitize/replay tick.
