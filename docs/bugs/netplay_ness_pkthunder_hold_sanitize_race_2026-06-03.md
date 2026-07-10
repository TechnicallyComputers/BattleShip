# Netplay — Ness PK Thunder Hold sanitize / tracking race (2026-06-03)

**Date:** 2026-06-03  
**Status:** Fix shipped (soak pending)  
**Area:** `port/net/sys/netplay_ness_pkthunder_gate.c`

## Symptom

After hold timer carryover fix, cross-ISA soak showed Hold feel correct early, then degraded to “normal gravity” drop and harder jibaku landings. Gate diag:

- Mid-Hold rollback: `sanitize_delay` restores `now=9`, then same tick `sanitize_delay_skip reason=hold_grace_expired status_tics=21`.
- Later holds logged carried `hold_enter delay=` values but gravity transition still felt late.
- Soak follow-up (`netplay-trimmed.log`): **5/14** holds with `gravity_delay=0` at entry; **zero** `sanitize_gravity` events — rollback during Start scrubbed gravity with no restore path.

## Root cause

`syNetplayNessSanitizePKThunderThrowStatusVars` ran **sync tracking between delay and gravity sanitize**. `SyncHoldEntryTracking` copied **raw blob-zero** into `HoldEntryDelay` / `HoldEntryGravityDelay` and could reset `HoldEntryTick` from hold-local `status_total_tics`, poisoning the tracking used on the **next** sanitize pass in the same rollback apply epoch.

**Gravity-specific gap:** `sanitize_gravity` ran on Hold only, not Start/AirStart. Rollback mid-Start (status 232) restored `pkjibaku_delay` but left `pkthunder_gravity_delay=0` through Hold entry. Hold sync could further lower `entry_gravity` when reconstructing from scrubbed live zero.

Sequence:

1. Sanitize restores `pkjibaku_delay` from good tracking → `9`.
2. Sync overwrites tracking with blob `delay=0`, `entry_tick = tick - status_total_tics`.
3. Second sanitize / next apply: `HoldDelayZeroIsLegitimate` → grace expired → skip restore; gravity falls through with wrong tracking.
4. Start rollback: delay sanitize fires @ status 232; gravity never sanitized → `hold_enter gravity_delay=0`.

## Fix

| Layer | Change |
|-------|--------|
| **Apply order** | Hold sanitize: delay → gravity → sync (sync last). |
| **Sync reconstruct** | Preserve `HoldEntryTick` from `NotifyHoldEntered`; set `entry_delay = live_delay + hold_frames`, same for gravity — never copy scrubbed blob counters into tracking. |
| **Pass-floor snap** | Block all descending air jibaku (`vel_y <= 0`) on `MAP_VERTEX_COLL_PASS` floors, not shallow descent only (steep down-jibaku was still clipping). |
| **Cliff/ledge snap** | Removed — cliff edge ground-snap block trapped Ness at platform edges; anchor/cull parity fixes address launch snap instead. |
| **Gravity scrub** | Sanitize gravity on Start+Hold; `NotifyThrowStarted` records throw-scope tick; expected gravity from throw frames; Hold sync never lowers `entry_gravity`. |
| **Diag** | `hold_enter` logs `gravity_delay`; `netplay-trim-logs.py` summarizes `sanitize_gravity` + flags `hold_enter gravity_delay=0`. |

## Verification

1. Cross-ISA soak with gate diag: no `sanitize_delay` followed by `sanitize_delay_skip` on same Hold epoch after rollback.
2. Mid-Hold rollback: gravity transition timing stable for full session (not just first few holds).
3. Rollback during Start: `sanitize_gravity was=0 now=N expected=N` before Hold entry; no `hold_enter gravity_delay=0` except legitimate expiry.
4. Downward jibaku into pass-through platforms: `air_jibaku_ground_snap_blocked reason=pass_floor_descent` instead of clip-through `SwitchStatusGround`.

## Related

- [`netplay_ness_pkthunder_hold_timer_carryover_2026-06-03.md`](netplay_ness_pkthunder_hold_timer_carryover_2026-06-03.md)
- [`netplay_ness_pkthunder_hold_early_exit_pass_floor_2026-06-02.md`](netplay_ness_pkthunder_hold_early_exit_pass_floor_2026-06-02.md)
