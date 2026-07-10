# Netplay: Captain Falcon kick flame / anim presentation after rollback

**Date:** 2026-07-09  
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android soak2 session `925949463` / seed `4291665786`  
**Match:** Captain Falcon vs Ness  
**Status:** FIX LANDED (entry reconcile) + extended trace — sim kick-in-place after SYNCTEST clobber

## Symptom

User reports Captain Falcon **Falcon Kick** sometimes ends abruptly or loses the flame VFX after rollback/resim, even when synctest passes.

**Ground kick in place (sim bug):** standing down-B kick plays animation but Falcon does not slide forward. Reproduced in soak2 session `717227637` at tick **630** when **SYNCTEST @629** overlaps fresh kick entry.

## Root cause (ground kick in place @ tick 630)

1. Tick 629 ends in standing ground state (`status=29 motion=23`).
2. Tick 630: vanilla `SpecialLwCheck -> PASS` → `ftMainSetStatus` ground kick @ frame 0.
3. SYNCTEST verify applies tick-629 standing blob, clobbering kick entry.
4. Emergency restore applies tick-630 kick blob @ `anim_frame≈1` **without** re-running `ftCaptainSpecialLwProcStatus` or frame-0 motion events.
5. Result: kick anim runs 84 frames but `vel_scale` / `vel_ground` never receive entry impulse → **kick in place**. Both peers match → `SYNCTEST_OK`.

## Fix (2026-07-09)

`syNetRbSnapReconcileCaptainGroundKickEntryAfterRollback` in `port/net/sys/netrollbacksnapshot.c`, wired from `syNetRbSnapApplyFighterNetplayPost`:

- Triggers on early ground kick (`status=230`, `motion=205`, `ga=0`, `anim_frame≤12`) when `vel_scale<0.95` (frames ≤4) or `|vel_ground.x|<8`.
- Replays vanilla entry: `ftCaptainSpecialLwProcStatus` + `ftMainSetStatus(SpecialLw, 0)` + `PlayAnimEventsAll`, then `PlayAnimEventsForward` to blob `gobj_anim_frame` + captain `proc_hit`/`proc_shield` restore.

**SYNCTEST mid-kick verify hardening (2026-07-09 follow-up):**

- `syNetRbSnapVerifyProtectCaptainFalconKickShell` — skip `hidden_cosmetic_verify` eject for live kick flame during verify.
- `syNetRbSnapFinalizeFighterEffectAttachFlags` — force `is_effect_attach=TRUE` on ground kick (`status=230`) during `verify_only` (tag `keep_verify_attach`).
- `syNetRbSnapReconcileCaptainGroundKickMomentumAfterRollback` — mid/late kick (`anim_frame` 13–83): restore `vel_ground` + `motion_scripts` from blob when live velocity drifts or verify stuck-vs-blob mismatch; one `ftCaptainSpecialLwProcPhysics` pulse (tags `reconcile_momentum`, `reconcile_exit_momentum`).
- `syNetRbSnapReconcileCaptainGroundKickAnimEndAfterRollback` — ground kick past vanilla window (`status_total_tics>84` or `anim_frame>=83.95` with status still 230): restore `motion_scripts`, run `ProcUpdate`, then force `ftCaptainSpecialLwDecideSetEndStatus` if still stuck (tag `reconcile_anim_end`).
- `syNetRbSnapReconcileCaptainGroundKickVerifyMotionAfterRollback` — SYNCTEST verify mid-kick (`anim_frame` 13–83, `flag3=1`): safe blob coll/vel restore + `is_coll_end` clear + `ProcPhysics` only (no `ftMainSetStatus` replay — avoids `floor_line_id=-1` crash).
- `syNetRbSnapCatchUpCaptainGroundKickForwardIfDue` — live forward stall (frozen `topn_x`): mid-platform unstick clears bogus `flag3`/`is_coll_end`, runs `ProcMap`+`ProcPhysics` (tag `reconcile_forward_stall`). **Does not** force `reconcile_anim_end` on live forward — vanilla `ftAnimEndCheckSetStatus` owns exit.

**Mid-platform stall unstick (soak2 end-kick, 2026-07-09):**

Last kick tick 735–818: position froze at `topn_x≈-653` (anim ~16) after Ness shield zeroed `vel_scale`; `flag3=1` by anim 73; momentum pulse ineffective; tick 818 `reconcile_anim_end` forced Wait on live forward.

Fix:
1. `syNetRbSnapReconcileCaptainGroundKickForwardStallUnstick` — when `dist_l`/`dist_r` > 48 from platform edges, clear `flag3` + `is_coll_end`, restore captain procs, pulse `ProcMap` then `ProcPhysics`.
2. Forward stall reconcile no longer requires `flag3=1` (catches early freeze before STOPEDGE flag sets).
3. Removed live-forward `reconcile_anim_end` from `CatchUpCaptainGroundKickForwardIfDue` (rollback apply path still uses anim-end reconcile).

## Mid-platform momentum freeze (soak2 post-fix, 2026-07-09)

Latest soak (`soak2-linux.log` / `soak2-android.log`, post anim-end reconcile): **12/12 full 84f ground kicks**, `state_diverge=0`, `SYNCTEST_OK` throughout. Sim agrees on both peers — not rollback divergence.

**User-visible issue:** momentum dies **mid-platform** while kick anim continues to frame 84 (not true ledge pin). Examples on Dream Land `floor_line=3`:

| Kick ticks | Freeze `topn_x` | Distance to platform edge |
|------------|-----------------|---------------------------|
| 1763–1846 | −1686.65 | ~628 short of left edge (−2314.53) |
| 1907–1990 | 1848.91 | ~273 short of right edge (2121.91) |

**Revised hypothesis:** `ftCaptainSpecialLwDecideMapCollide` routes through `mpCommonProcFighterOnEdge` when motion `flag3!=0` (`MAP_PROC_TYPE_STOPEDGE`). `mpCommonCheckSetFighterEdge` may fail mid-stage → `is_coll_end=TRUE` without edge snap, zeroing forward travel while anim continues. Deterministic on both peers; investigate vanilla STOPEDGE at these X positions offline.

**Edge diagnostics (2026-07-09):** `captain_kick_trace` now logs `flag3`, `mask_curr`, `mask_stat`, `flooredge`, `floor_line`, `floor_dist`, `is_coll_end`, and read-only `edge_l_x`/`edge_r_x`/`dist_l`/`dist_r` probes. Grep `forward_kick_stall` for mid-kick position freeze events.

**Offline control (2026-07-09):** User confirms offline does **not** inconsistently reproduce mid-platform kick freeze → netplay-only (SYNCTEST verify blob apply + forward stall). Fix: `reconcile_verify_motion` / `reconcile_forward_stall` safe momentum pulse (blob coll+vel restore, `is_coll_end` clear, `ProcPhysics` only).

**Crash fix (2026-07-09):** Mid-kick `ftMainSetStatus` motion replay caused `mpGetExistCollisionId() id=-1` debug halt @tick≈470. Replaced with safe pulse path (no status reset, no `ProcMap`).

**Crash fix #2 (2026-07-09 soak2 @tick 990):** After kick transitions to air (`status=231`, `floor_line=-1` legitimate), SYNCTEST verify `ensure_pre` trace called `syNetRbSnapProbeCaptainKickFloorEdge` without guarding `floor_line_id<0` → same `mpGetExistCollisionId(-1)` halt. Fix: probe skips invalid floor lines; entry motion replay skips when `floor_line_id<0`; `mpCollisionCheckExistLineID(-1)` returns `FALSE` on `PORT` instead of debug infinite loop.

**Platform-edge stall false positive (soak2 post mid-platform fix, 2026-07-09):**

User reports post-ledge physics wrong: momentum dies just over the ledge or air glide stops mid-flight. Mid-platform unstick works (`mid_platform=1` @ ticks 1973/2092); new failure is **true edge** (`dist_l=0` or `dist_r=0`).

| Tick | `topn_x` | `flag3` | `vel_gx` | Unstick effect |
|------|----------|---------|----------|----------------|
| 477 | −2318 (left lip) | 1→1 | 0.67 | `mid_platform=0`, ProcMap pulse, stays `status=230` |
| 1812 | 2318 (right lip) | 1→1 | 0.67 | same — no `230→231` edge-off |

Root cause: forward stall detector treats STOPEDGE lip clamp (frozen `topn_x` + high `vel_gx`) as mid-platform freeze. `reconcile_forward_stall` runs extra `ProcMap`+`ProcPhysics` outside normal tick order and blocks vanilla `mpCommonProcFighterOnEdge` → `ftCaptainSpecialLwAirSetStatus`.

Fix: `syNetRbSnapCaptainGroundKickAtPlatformEdge` — skip stall detect and unstick when `dist_l` or `dist_r` ≤ 48. Mid-platform path unchanged.

Successful edge-off still observed @ tick 780 (`230→231`, air glide `tr_x` advancing ~50 units/tick while `tr_y` flat) when dash leaves stage before lip clamp triggers false stall.

**vel_scale stomp + air freeze (soak2 @tick 810/1001, 2026-07-09):**

Mid-kick and post-ledge air kick carry `vel_scale=0.000` (impossible vanilla — floor is 1/64 after 6 `ProcHit` halvings). `ftCaptainSpecialLwProcPhysics` multiplies `vel_air` by `vel_scale` every tick; at 0 the air kick freezes in place (`tr_x`/`tr_y` identical for 1001–1003).

Fix:
1. `syNetRbSnapCaptainGroundKickVelScaleFromTimer` / `syNetRbSnapSanitizeCaptainGroundKickVelScale` — re-derive from `scale_apply_timer` when `vel_scale < 1/64`.
2. `syNetRbSnapApplyCaptainGroundKickSpeciallwFromBlob` — never trust stomped blob `vel_scale`; skip copying stomped blob `vel_ground` mid-kick.
3. `syNetRbSnapReconcileCaptainGroundKickAirMomentumAfterRollback` — status **231** scope: restore `vel_air` + sanitized `vel_scale`, physics pulse (tag `reconcile_air_momentum`).
4. Live forward: `reconcile_air_vel_scale` sanitizes status 231 on corrupt `vel_scale`; forward stall unstick also sanitizes before pulse.

**Post-kick recovery lock (soak2 ledge-off, 2026-07-09):**

After vel_scale fix, momentum is good but ledge-off kick recovery can lock: air kick exits correctly (`231→26`), player aerial-jumps (`25`), then **Falcon Dive** (`status=238`) hangs ~64 ticks with frozen `tr_x` — cannot turn/jump back.

Root cause family: `FTCaptainStatusVars` union aliases `speciallw.vel_scale` with `specialhi.vel.x`. Rollback blob `memcpy` can restore kick overlay bytes into SpecialAirHi physics; anim scalar restore can also block `ftCaptainSpecialHiProcUpdate` (`anim_frame <= 0` → `FallSpecial`).

Fix:
1. Scrub guard — skip common-overlay scrubs when blob is in captain kick **or** specialhi scope (mirror Fox Firefox guard).
2. `syNetRbSnapCanonicalizeCaptainStatusVarsOnApply` — kick scope re-applies sanitized `speciallw`; specialhi scope with stomped union runs `ftCaptainSpecialHiProcStatus`.
3. `syNetRbSnapReconcileCaptainGroundKickAirExitAfterRollback` — live status 231 when blob/advanced anim says exit: `ftCaptainSpecialLwDecideSetEndStatus` (tag `reconcile_air_exit`).
4. `syNetRbSnapReconcileCaptainSpecialAirHiAfterRollback` — union scrub + `ftCaptainSpecialHiProcUpdate` catch-up when `anim_frame <= 0` (tag `reconcile_specialhi`).
5. **eff SYNCTEST @2189 (soak2 @146978893):** `syNetRbSnapHashCanonicalSlotEffects` now delegates to `syNetSyncHashActiveEffectsForRollback()` — the old `gcFindGObjByID` blob-resolve path picked the wrong recycled `id=1011` survivor (`ring_eff=0xD837E95C` vs `live_eff=0xEB52DFA7`). Stale kick flame during Falcon Dive is excluded via `syNetRbSnapLiveEffectIsStaleCaptainFalconKickCosmetic`. Live-forward `catchup_specialhi` no longer union-scrubs on `vel_scale=0` false positives.
5. Live forward catch-up in `syNetRbSnapCatchUpCaptainGroundKickForwardIfDue` — `catchup_air_exit`, `catchup_specialhi`, `post_kick_recovery` trace.
6. Extended trace: `captain_post_kick_recovery` logs `jumps_used`, `vel_air`, `specialhi.vel`, `vel_scale` across `231→26→25→27→238`.

**SYNCTEST @3269 anim/figh LOAD_HASH_DRIFT (soak2 session `1540335317`, 2026-07-09):**

Captain mid-kick (`status=230`, `anim_frame=77`, `status_total_tics=85`, shielded vs Yoshi) — blob captured correctly, but SYNCTEST verify `reconcile_anim_end` fired on `status_total_tics > 84` alone and forced `230→Wait(10)`. Live then bound Wait figatree at kick `anim_frame=77` → corrupted joints + `LOAD_HASH_DRIFT` (`anim`, `figh`).

Fix:
1. Skip `syNetRbSnapReconcileCaptainGroundKickAnimEndAfterRollback` when `syNetRbSnapRepairStageIsVerifyOnly()`.
2. Tighten `syNetRbSnapCaptainGroundKickNeedsAnimEndReconcile`: `status_total_tics > 84` also requires `anim_frame >= 83.95`.

**Post-kick air glide / momentum drop (soak2 post-recovery-lock, 2026-07-09):**

User reports clean recovery but post-kick air glide feels wrong: instant neutral control, horizontal momentum dies vs offline flame carry.

Log analysis @ tick 803/1098: `catchup_air_exit` fired on live forward when status 231 entered at `anim_frame=0.00`. `syNetRbSnapCaptainAirKickNeedsExitReconcile` treated `anim_frame<=0.05` as anim-end, but frame 0 is also **entry** for countdown anims (`ftAnimEndCheckSetStatus`). Live catchup forced `ftCaptainSpecialLwDecideSetEndStatus` outside tick order, collapsing 231→26 same tick. Sim still showed `vel_ax≈±31` at Fall entry but kick-pose glide window was skipped.

Fix:
1. `syNetRbSnapCaptainAirKickNeedsExitReconcile` — **rollback apply only** (`blob` required). Anim-end requires `status_total_tics>0` with `anim_frame<=0`; removed `anim_frame>=29` (wrong semantics) and entry false-positive.
2. Removed live-forward `catchup_air_exit` from `syNetRbSnapCatchUpCaptainGroundKickForwardIfDue` — vanilla `ftCaptainSpecialLwProcUpdate` owns forward exit.
3. `syNetRbSnapReconcileCaptainGroundKickAirExitAfterRollback` — pin `blob->physics.vel_air` after forced Fall transition (`ftPhysicsClampAirVelXMax` bleed).

Session `925949463`: **22/22 `SYNCTEST_OK`**, zero `eff` drift on both peers. Kick windows are **bit-identical** on Linux guest and Android host (14 windows logged).

| Kick type | Observed duration | Notes |
|-----------|-------------------|-------|
| Ground (`status=230`) | 84 frames (6×) | Full vanilla length |
| Ground edge-off (`230→231`) | 31–40 frames (3×) | `DecideMapCollide` platform edge transition — vanilla |
| Aerial chain (`233→232`) | 39–47f air, 45f landing | Normal |

During kick windows: `eff=0x811C9DC5` (empty hash — kick flame excluded from rollback hash by design). After mid-kick rollback load: `figatree-bind ... frame=0.00` on fighter rebind — sim continues but animation may snap to frame 0.

## Working hypothesis

This is a **presentation-only** bug class, not rollback sim divergence:

1. Kick flame (`efManagerCaptainFalconKickMakeEffect`, joint 23, `NoEject`) is not in the eff hash fold.
2. Post-rollback figatree refresh can rebind the fighter model at `anim_frame=0` while sim state (status/motion/ga) remains in kick scope.
3. Ensure/mint/prune/repin paths may miss a window where `is_effect_attach` or joint userdata is stale after figatree walks.

Hardening already landed: `syNetRbSnapRepinCaptainFalconKickJoint`, relaxed `IsCaptainFalconKick`, `EnsureCaptainFalconKickEffectsFromSlot`, apply-path `is_effect_attach` restore.

## Diagnostic

Set on **both peers** during soak:

```bash
export SSB64_NETPLAY_CAPTAIN_KICK_TRACE=1
# or rely on the broader bundle:
export SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1
```

Grep logs for `captain_kick_trace`. Tags:

| Tag | When |
|-----|------|
| `ensure_pre` / `ensure_post` | Scope scan around kick effect ensure |
| `ensure` + `mint_ok` / `mint_fail` / `skip_*` | Per-fighter mint decisions |
| `prune` + `eject_orphan` / `eject_stale` | Stale kick effect shells removed |
| `apply_repin` / `apply_anim` | Effect blob rebind + anim_frame change |
| `finalize` + `keep_*` / `clear_*` | Post-reconcile attach flag |
| `reconcile_entry` / `reconcile_entry_detail` | Ground kick entry replay after blob apply |
| `reconcile_momentum` / `reconcile_momentum_detail` | Mid/late kick velocity restore after verify/reload |
| `reconcile_exit_momentum` | Momentum restore when `anim_frame≥78` |
| `reconcile_anim_end` / `reconcile_anim_end_detail` | Forced ground-kick exit when anim-end stalls past vanilla 84f (`anim_frame>=83.95` or `status_total_tics>84`) |
| `reconcile_verify_motion` / `reconcile_verify_motion_detail` | SYNCTEST verify mid-kick motion replay (preserve `topn_x`, restore blob vel) |
| `reconcile_forward_stall` / `reconcile_forward_stall_detail` | Forward-sim stall recovery (mid-platform `flag3`/`is_coll_end` clear + `ProcMap`/`ProcPhysics`) |
| `forward_entry_kick` | Forward-sim entry scan (`anim_frame<=6`) |
| `forward_mid_kick` | Forward-sim mid-kick scan (`7<=anim_frame<=78`, grounded `status=230`) |
| `forward_mid_kick_orphan` | Ground kick state airborne without `230→231` (`status=230`, `ga=1`) |
| `forward_mid_kick_ga_flip` | One-shot: `ga` went ground→air during `status=230` |
| `forward_mid_kick_flag1_clear` | One-shot: `flag1` went `2→0` while still grounded (`status=230`) — vanilla air-goto latch lost |
| `forward_late_kick` | Forward-sim late-kick scan (`anim_frame>=79`) |
| `forward_kick_stall` | Mid-kick momentum stall: anim advancing, `topn_x` frozen, `|vel_gx|<1` |
| `reconcile_air_momentum` / `reconcile_air_momentum_detail` | Status 231 air-kick velocity + vel_scale restore after rollback |
| `reconcile_air_exit` / `reconcile_air_exit_detail` | Rollback-only air kick (`231`) forced exit when blob/advanced past anim end; pins blob `vel_air` |
| `reconcile_specialhi` / `reconcile_specialhi_detail` | Falcon Dive union scrub + anim-end catch-up (`238`) |
| `catchup_specialhi` | Live-forward Falcon Dive catch-up only (no live `catchup_air_exit`) |
| `captain_post_kick_recovery` / `post_kick_recovery` | Extended recovery chain trace (`231→26→25→27→238`) |
| `reconcile_air_vel_scale` | Live forward vel_scale sanitize on status 231 |
| `keep_verify_attach` | Verify-only attach preserved on ground kick |
| `apply_post_finalize` / `verify_post_finalize` | End-state scope scan |

Each line includes: `status`, `motion`, `ga`, `attach`, `flag0`, `flag1`, `flag2`, `flag3`, `blob_attach`, `blob_flag2`, fighter `anim_frame`, `status_total_tics`, `vel_scale`, `vel_gx`, `vel_ax`, `topn_x`, `mask_curr`, `mask_stat`, `flooredge`, `floor_line`, `floor_dist`, `is_coll_end`, `edge_l_x`, `edge_r_x`, `dist_l`, `dist_r`, `live_kick_gobj`, `joint_ok`, `verify_only`.

Pair with always-on `figatree-bind` lines in `lbcommon.c` to correlate anim rebind (`frame=0.00`) with missing `joint_ok` or `mint_fail`.

## Re-soak pass criteria (presentation)

When user reproduces abrupt kick end:

1. Confirm `SYNCTEST_OK` and matching `sim_state_tick` for kick window on both peers.
2. At rollback load inside kick: expect `apply_repin` or `mint_ok`; flag `joint_ok=0`, `live_kick_gobj=0`, or `apply_anim anim_after=0.00` as bisect anchors.
3. Ground kick ending at 31–40f with `status 230→231` is **vanilla edge-off**, not a port bug.

## Orphan `230+ga=Air` ledge-off recovery hang (soak2 @568–652, 2026-07-09)

**Symptom:** Captain ground kick over right ledge plays recovery pose (`status=230`) while falling offstage for the full 84f window instead of transitioning to air kick (`231`). User-visible: stuck in post-kick recovery animation, falling too long.

**Log evidence (Linux + Android, deterministic):**

| Tick | State |
|------|-------|
| 568 | Kick entry grounded (`ga=0`, `topn_x≈119`) |
| 629 | `SYNCTEST_OK` — `status=230`, `ga=1`, `anim_frame=62`, `topn_x=3326`, `flag3=1`, `floor_line=-1`; all reconcile hooks `skip_no_pending` |
| 651 | Still `status=230` |
| 652 | `230→26` (Fall) at vanilla anim end |
| 960 (control) | Normal edge-off: `230` for 31 ticks then `231` |

No `reconcile_forward_stall`, `forward_kick_stall`, or `reconcile_anim_end` fired during ticks 568–651 in this soak.

### Netplay-only? **No — not for this incident**

| Check | Result |
|-------|--------|
| Captain kick decomp (`ftcaptainspeciallw.c`) | No `PORT` / `SSB64_NETMENU` patches |
| Netplay reconcile touched bad kick? | **No** — zero stall/momentum/anim-end reconcile logs in window |
| Rollback desync? | **No** — `SYNCTEST_OK @629`, `state_diverge=0` |
| Vanilla `230→231` gate | `ftCaptainSpecialLwAirCheckAirGoto`: requires `flag1==2` **and** `ga==Air`; clears `flag1` if still grounded when `flag1==2` (motion sets `flag1=2` ~frame 32, `flag3=1` ~frame 36) |
| Successful edge-off in same soak | Kick @929 → `231` @960 (frame ~31) — same vanilla path, different timing/position |

**Conclusion:** The soak2 @568 failure is **vanilla timing** (missed `flag1==2` + `ga==Air` coincidence for `ftCaptainSpecialLwAirSetStatus`), not a netplay-only fork divergence. Netplay **can** aggravate the same failure mode when `reconcile_forward_stall` runs extra `ProcMap` off-tick (may clear `flag1` while grounded — see `reconcile_forward_stall_detail flag1_before/after`), but that path did **not** run here.

**Trace extension (2026-07-09):** Mid-kick window now logs `forward_mid_kick` (frames 7–78), `forward_mid_kick_orphan` (`230`+`ga=1`), plus one-shot `forward_mid_kick_ga_flip` / `forward_mid_kick_flag1_clear`. Re-soak with `SSB64_NETPLAY_CAPTAIN_KICK_TRACE=1` and grep those tags on the next ledge-off miss to confirm `flag1_clear` precedes `ga_flip` without `231`.

**Offline confirmation still needed:** Run the same center→right ledge kick offline; if orphan `230+ga=Air` appears, treat as vanilla-quirk presentation (fix only in netmenu if we choose to paper over it for rollback parity).
