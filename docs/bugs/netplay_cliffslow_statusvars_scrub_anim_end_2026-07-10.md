# Netplay: CliffSlow hang after synctest emergency restore

**Date:** 2026-07-10  
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android soak2 session `1745589902`  
**Match:** Captain Falcon (P0) vs Ness — determinism PASS, presentation hang at soak end  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptom

Captain Falcon frozen mid slow ledge pull-up (`status=89` CliffSlow, `motion=77`) with iframes
active from tick 6001 through session end @6877 (~877 ticks). Both peers identical (not an FC
diverge). Offline slow ledge getup should always advance to `CliffClimbSlow1` (90) within ~30
sim ticks.

## Log evidence (soak2-linux.log)

| Observation | Detail |
|-------------|--------|
| Cliff chain | Dive (238) → CliffCatch (84) @5968 → CliffWait (85) @5969 → **CliffSlow (89)** @6001 |
| Stuck duration | Status 89 for ticks 6001–6877 |
| Synctest cadence | `SYNCTEST_OK` @6029, 6149, … every 120 ticks; `emergency_restore` on same boundaries |
| Figatree rebind | `figatree-bind … motion=77 status=89` oscillates `frame=0 → 29 → 30 → 0` on each restore |
| proc_update | `emergency_restore` shows `proc_upd` non-NULL for status 89 (CliffSlow handler live) |
| Position freeze | After first in-window restore @6150, `topn` stuck at `(2397.54, -356.48)` |

Vanilla transition (`ftCommonCliffSlowProcUpdate`): when `fighter_gobj->anim_frame <= 0.0F`, switch
on `ftStatusVarsCliffMotion(fp)->status_id` and call `ftCommonCliffClimbSlow1SetStatus`.

## Root cause (two-part)

### 1. Union stomp on snapshot save

`syNetRbSnapScrubInactiveStatusVarsInBlob` memset-scrubs `common.attackair`, `common.dead`,
`common.rebirth`, etc. while the fighter is in cliff statuses. Those overlays alias
`common.cliffmotion` / `common.cliffwait` at union offset 0. The scrub zeroes
`cliffmotion.status_id` (and `cliff_id`) in every ring blob and emergency slot.

`syNetRbSnapBlobInCliffMotionSynctestFragileScope` existed but was **not wired** into the scrub
path (dead helper). After synctest `CaptureLiveEmergency` → load → `RestoreLiveEmergency`, apply
memcpy'd the poisoned overlay back onto live state.

### 2. Quantized anim end never reaches `<= 0`

Even when anim visually completes (~frame 30 windup), `syNetplayQuantizeAnimScalar` can leave
`gobj->anim_frame` at a tiny positive grid value. `CliffSlowProcUpdate` checks `<= 0.0F` exactly
(same class as GuardOff / Link SpecialN charge end). Without a near-zero snap, the windup never
terminates after emergency restore re-pins mid-animation cursors.

Combined: synctest every 120 ticks during forward sim repeatedly restored a CliffSlow fighter whose
blob overlay and anim cursor could not complete the windup → infinite CliffSlow with iframes.

## Fix

1. **`port/net/sys/netrollbacksnapshot.c`** — early-return from
   `syNetRbSnapScrubInactiveStatusVarsInBlob` when
   `syNetRbSnapBlobInCliffMotionSynctestFragileScope` (status 84–`CliffEscapeSlow2`), preserving
   live `cliffmotion` / `cliffwait` bytes in the blob.

2. **`port/net/sys/netplay_sim_quantize.c`** — extend `syNetplayCanonicalizeAnimEndWaitThreshold`
   to call `syNetplaySnapAnimFrameToEndIfNearZero` for the same cliff status range before
   `ProcUpdate`.

## Test plan

- [ ] Re-soak Captain vs Ness (or any high-damage slow ledge getup) with synctest enabled; confirm
      CliffSlow exits to 90 within ~30 ticks and no hang through 120-tick probe boundaries.
- [ ] `netplay-scan-drift.py` PASS; grep logs for `status=89` runs ending before `VS_SESSION_END`.
- [ ] Offline control: slow ledge getup without netmenu still matches vanilla timing.
