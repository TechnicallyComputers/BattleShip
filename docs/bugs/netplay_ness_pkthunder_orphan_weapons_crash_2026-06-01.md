# Netplay — Ness PK Thunder orphan weapons + jibaku stall + SIGABRT

**Date:** 2026-06-01  
**Status:** Fix shipped (soak pending)  
**Log:** `netplay-session-trimmed-rollback.log` (cross-ISA Ness vs Pikachu, heavy PK Thunder)

## Symptom

After repeated PK Thunder use in rollback netplay:

- `weapon_count=5` sustained on ring save (orphan heads/trails not culled).
- Ground jibaku (status 231) soft-lock ~40 ticks with frozen anim hash despite prior `anim_length_zero` guard.
- Both peers **SIGABRT** (crash not in trimmed log tail; likely heap stress from weapon/effect accumulation).

Instant-jibaku (`pkjibaku_delay` scrub) was already fixed in a prior pass — Hold grace counted down correctly in this session.

## Root cause

1. **Orphan weapon lifecycle** — Rollback apply reacquired PK Thunder heads during jibaku/end statuses instead of culling. Snapshot save captured every live weapon on the link with no pre-save cull, so orphan heads persisted across `LOAD_HASH_DRIFT` loads (`weapon_count` climbed to 5).

2. **Jibaku timer stall** — Catch-up only forced exit when `pkjibaku_anim_length <= 0`. If jibaku procs did not advance the timer (stale proc after rollback), `anim_length` stayed positive indefinitely with no `anim_length_zero` diag.

3. **PK wave effect drift** — Wave userdata effects were identified only while fighter was in full PK Thunder scope; prune kept orphans during jibaku/end. Live sim never pruned between rollback loads.

## Fix

| Layer | Change |
|-------|--------|
| **Pre-save cull** | `syNetRbSnapCullAllOrphanPKThunderLive()` before `syNetRbSnapCaptureWeapons` — keep coupled head only in Start/Hold, cull all otherwise. |
| **Live maintenance** | Same cull + `syNetRbSnapPruneStaleNessPKWaveEffectsLive()` each sim tick in `syNetplayNessRunLiveJibakuCatchUpAll`. |
| **Apply post** | Start/Hold: reacquire + cull extras. Jibaku/end/bound: NULL coupling + cull all (no reacquire). |
| **Jibaku stall catch-up** | Per-player stall counter: if `pkjibaku_anim_length` unchanged for 2 ticks, force one decrement / exit. |
| **Effect prune** | Identify PK wave by joint attachment only; prune when fighter outside Start/Hold (`PKWaveScope`). |
| **Jibaku entry (deferred)** | `ftNessSpecialHiPortPrepareJibakuPKThunder()` NULLs coupling only; `syNetplayNessFinishJibakuTransition()` after SetStatus runs orphan cull + PK wave prune (avoids synchronous destroy-all mid transition). |

## Update — SIGABRT @ jibaku tick 3001 (same log, post-resim-sanitize soak)

Both peers crashed immediately after `jibaku_trigger` on the 7th jibaku. **`weapon_count=5` during Hold is normal** (1 head + 4 trails = `WPPKTHUNDER_PARTS_COUNT`); weapons drop to 0 after each jibaku (e.g. tick 2737→2738).

**Cause:** `ftNessSpecialHiPortCleanupPKThunder()` in `JibakuInitStatusVars` synchronously `wpMainDestroyWeapon`'d the Collide head and culled all trails during Hold→Jibaku transition, racing the Collide weapon proc teardown. Repeated cycles → SIGABRT.

**Fix:** Deferred jibaku teardown (prepare + finish transition hooks above).

## Update — SIGABRT @ jibaku tick 1628 (post-deferred-fix soak)

Jibaku #1 succeeded; **jibaku #2 @1628** crashed both peers with the same SIGABRT pattern (log ends on `jibaku_trigger`).

**Cause:** `LOAD_HASH_DRIFT` apply mid-Hold restored five PK Thunder weapons with stale `owner_gobj` IDs (`WEAPON_OWNER_RESOLVE` mismatch). `syNetplayNessFinishJibakuTransition()` still ran synchronous `CullAllOrphanPKThunderLive()` + `PruneStaleNessPKWaveEffectsLive()` immediately after SetStatus, racing Collide proc teardown on blob-restored weapons. End-of-frame `RunLiveJibakuCatchUpAll()` culls again → double-teardown.

**Fix:**

| Layer | Change |
|-------|--------|
| **Finish transition** | Only clear `is_effect_attach`; defer cull/prune to `syNetplayNessRunLiveJibakuCatchUpAll` (end of frame). |
| **Post weapon-apply reconcile** | `syNetplayNessReconcilePKThunderWeaponsAfterApply()` — patch PK head/trail `owner_gobj` + `parent_gobj`, reacquire head, cull extras; called from coupled rebind after weapon apply. |
| **NNess** | Coupled PK Thunder rebind includes `nFTKindNNess`. |
| **Phase diag** | `event=jibaku_phase phase=prepare\|setstatus\|anim\|finish` under `NESS_PKTHUNDER_GATE_DIAG`. |

## Update — SIGABRT @ jibaku tick 1646 (deferred finish still EoF cull same tick)

Log showed full `jibaku_phase` … `finish` on both peers, then abrupt end (no `sim_state_tick` 1646). Jibakus #1–#3 completed with `weapon_count=0` on the next save.

**Cause:** `syNetplayNessRunLiveJibakuCatchUpAll()` still ran `CullAllOrphanPKThunderLive()` + PK wave prune on the **same tick** as jibaku transition (fighter left Start/Hold → cull all five weapons while Collide/anim teardown still in flight).

**Fix:** `FinishJibakuTransition` sets `DeferPKCullUntilTick = now+1`. EoF catch-up skips global cull/prune when `tick < cull_at`; runs cull next tick with `event=jibaku_post_cull action=deferred|cull` under `NESS_PKTHUNDER_GATE_DIAG`. Trim script summarizes `jibaku_post_cull` and flags deferred-without-following-cull (crash signature).

## Update — SIGABRT @ jibaku tick 1124 (weapons=5 after finish, upward air jibaku)

Deferred global cull ran on jibakus #1–#2 (`deferred weapons=0`); #3 crashed after `finish` with **5 PK weapons still live** — weapon head `ProcUpdate`/`ProcMap` Collide and non-Hold destroy ran same tick after fighter left Hold.

**Fix:** Schedule defer at `jibaku_trigger` + `finish`; `syNetplayNessShouldDeferPKThunderHeadProcTeardown()` skips head Collide/non-Hold/ProcMap mass destroy until tick+1 (global cull handles teardown). Diag: `event=jibaku_weapon_state` at finish (`weapons`, `head_pkstatus`, `cull_at_tick`); trim script flags `weapons>0` at finish as SUSPECT.

## Update — SIGABRT @ jibaku tick 954 (2nd upward jibaku, no `deferred` after `finish`)

Jibaku #1 @596 completed (`finish` → `deferred weapons=5` → tick+1 `cull`). Jibaku #2 @954 logged full `jibaku_phase` … `finish` with `weapons=5` / `head_pkstatus=2` (Collide), then log ended — no `jibaku_post_cull`, no `sim_state_tick` 954.

**Cause:** Partial head defer left **trail destroy cascade** (`SetDestroyTrails`), **ProcHit**, **Destroy/expire** head branches, and **air jibaku ProcMap** (ceiling/wall) running same tick after `finish`, synchronously tearing down five live weapons + effects before EoF defer.

**Fix:**

| Layer | Change |
|-------|--------|
| **SetDestroyTrails** | No-op while defer active for that player |
| **Head ProcUpdate** | Defer Destroy, expire, owner NULL, ProcHit paths |
| **Trail ProcUpdate** | Defer trail Destroy status (normal + reflect) |
| **ProcDead / ProcReflector** | Defer mass trail destroy |
| **Air jibaku ProcMap** | One-tick grace: skip map collision while defer active |
| **Diag** | `event=jibaku_post_finish` (`mask_curr`, `vel_air`, `defer_teardown`); trim summarizes + SUSPECT when `defer_teardown=0` |

## Update — SIGABRT @ jibaku tick 1132 (deferred OK, crash before `cull@1133`, upward)

Jibaku #1 @586: `deferred weapons=5` → `air_jibaku_on_floor` → ring save `weapon_count=0`. Jibaku #2 @1132: full defer path (`jibaku_post_finish defer=1`, `vel_air` upward), `deferred weapons=5`, log ends — no `weapon save tick=1132`, no `cull@1133`.

**Cause:** `syNetRbSnapCullAllOrphanPKThunderLive()` in ring **pre-save capture** still ran on the defer tick (after EoF catch-up logged `deferred`), mass-destroying five Collide weapons while proc defer was active. Upward jibaku skipped same-tick `air_jibaku_on_floor` that had masked the race on #1.

**Fix:**

| Layer | Change |
|-------|--------|
| **Ring pre-save cull** | Skip `syNetRbSnapCullAllOrphanPKThunderLive()` when `syNetplayNessIsPKThunderGlobalDeferActive()` |
| **End status cleanup** | `ftNessSpecialHiPortCleanupPKThunder()` no-op (NULL coupling only) during defer |
| **Catch-up floor** | Skip `air_jibaku_on_floor` ground transition during defer |

## Files

- `port/net/sys/netrollbacksnapshot.c`
- `port/net/sys/netrollbacksnapshot.h`
- `port/net/sys/netplay_ness_pkthunder_gate.c`
- `decomp/src/ft/ftchar/ftness/ftnessspecialhi.c`
- `decomp/src/wp/wpness/wpnesspkthunder.c`
- `scripts/netplay-trim-logs.py`

## Verification

1. Cross-ISA Ness spam PK Thunder 15+ jibaku cycles with rollback enabled.
2. `weapon_count` may be 5 during active Hold (head + trails); must be 0 within 1 tick after jibaku.
3. Ness must not sit in status 231 > ~30 ticks; diag may show `jibaku_stall_tick` on **231 only** (not 236).
4. No SIGABRT / unbounded ef6 growth across rollback loads.

## Update — SIGABRT @ tick 851 (2nd upward jibaku, `air_jibaku_on_floor` after deferred cull)

Cross-ISA soak: jibaku #1 downward @568 completed; jibaku #2 upward @843 deferred cull @844, then **`air_jibaku_on_floor` @851** (`ga=1`, ascending `vel_y>0`) → SIGABRT (empty backtrace, heap abort).

**Cause:** Deferred weapon cull @844 ran same window as premature ground snap on ascending arc (`236→231` while `vel_y>0`). Catch-up / ProcMap still called `SwitchStatusGround` on shallow floor contact during upward travel, racing post-cull teardown.

**Fix:**

| Layer | Change |
|-------|--------|
| **Defer span** | `ScheduleDeferPKTeardown` now `now+2` ticks (was `+1`) before global cull |
| **Post-cull floor grace** | 2-tick grace after deferred cull blocks air→ground snap (`PostCullFloorGraceUntilTick`) |
| **Ground snap guards** | `ShouldBlockAirJibakuGroundSnap`: skip when defer active, grace active, `ga==Ground`, or `vel_air.y>0` |
| **Catch-up + ProcMap** | `ShouldSkipAirJibakuFloorTransition` / ProcMap pass-cliff + on-floor use guards before `SwitchStatusGround` |

## Update — instant upward jibaku ground snap @844 + FC resim SIGSEGV @840

Soak: upward jibaku @843 (`vel_air=+177,+91`) → **236→231 same tick @844** (Android); Linux peer kept 236 through @798 with defer+2. @851 `air_jibaku_on_floor ga=1`. Then **FC recovery resim** load 720→841 while live Ness in **SpecialAirHiEnd (234)** → SIGSEGV `ftMainProcUpdateInterrupt` fault_addr=0x38.

**Cause (ground snap):** Post-cull floor grace was armed **after** EoF cull on the defer-expiry tick. ProcMap ran that tick with defer expired but grace not yet set → premature `SwitchStatusGround` while still ascending.

**Cause (crash):** `FcResimDeferScope` omitted `SpecialAirHiEnd` / `SpecialHiEnd`; FC recovery at @840 was not deferred. Clamp used stale `PkScopeEarliestLoadTick` (721) instead of hold session start (~745) → resim loaded neutral @720 into live PK Thunder end state.

**Fix:**

| Layer | Change |
|-------|--------|
| **Grace at jibaku finish** | Arm `PostCullFloorGraceUntilTick` + `AirJibakuStartTick` in `FinishJibakuTransition` (defer+grace span from finish, not post-cull) |
| **Launch guard** | Block air→ground snap for first 4 ticks of air jibaku (`AIR_JIBAKU_LAUNCH_GUARD_TICKS`) |
| **FC defer scope** | Include HiEnd, AirHiEnd, HiStart in `FcResimDeferScope`; defer when post-cull grace or launch guard active |
| **Session earliest load** | Track `PkSessionEarliestLoadTick` from hold_enter through PK span; FC clamp uses effective min(scope, session) |

## Update — revert non-vanilla floor forcing + ground-only stall (2026-06-01)

Soak after grace/launch guards: upward jibaku @547 stayed **236** (no 231) but **`jibaku_stall_tick` fired on air jibaku (236) ticks 570–575**, shortening anim and exiting to 234 early — user still perceived instant ground jibaku. No `air_jibaku_on_floor` events in log.

**Cause:** Prior patches added **non-vanilla** solid-floor ProcMap transition and catch-up `air_jibaku_on_floor` (vanilla only uses pass-cliff for air→ground). Stall catch-up ran on both 231 and 236, forcing decrements on air jibaku when procs legitimately held `anim_length` steady.

**Fix:**

| Layer | Change |
|-------|--------|
| **ProcMap** | Remove `#ifdef PORT` solid-floor on-floor block; keep pass-cliff only (vanilla path) |
| **Catch-up** | Remove `air_jibaku_on_floor` synthetic ground transition |
| **Stall catch-up** | Restrict `jibaku_stall_tick` to **ground jibaku (231)** only |
| **Guards** | Keep `ShouldBlockAirJibakuGroundSnap` on pass-cliff only; remove `ShouldSkipAirJibakuFloorTransition` |
| **Diag** | `event=air_jibaku_ground_snap` when `SwitchStatusGround` actually runs (source tag) |

## Related

- [`netplay_ness_pkthunder_jibaku_ground_lock_2026-06-01.md`](netplay_ness_pkthunder_jibaku_ground_lock_2026-06-01.md)
- [`netplay_ness_pkthunder_instant_jibaku_2026-06-01.md`](netplay_ness_pkthunder_instant_jibaku_2026-06-01.md)
- [`netplay_ness_pkthunder_upb_segv_2026-05-22.md`](netplay_ness_pkthunder_upb_segv_2026-05-22.md)
