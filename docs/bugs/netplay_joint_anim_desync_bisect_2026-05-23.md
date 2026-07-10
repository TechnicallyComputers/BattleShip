# Netplay joint-animation desync — function bisect (v10 auto3/4 class)

**Date:** 2026-05-23 (updated 2026-05-25: Tier 1 fix landed)  
**Status:** TIER 1 PATCHED — AObj chain rebuild on snapshot apply; awaiting WAN soak  
**Evidence:** `client-auto3`/`gangster3` (resim @1560, `match_f=0`, `joint_anim_frame` diffs), `client-auto4`/`gangster4` (fork @958, `LOAD_HASH_DRIFT` figh+anim @900)

## 2026-05-25 fix — Tier 1 AObj chain rebuild

**Root cause:** `syNetRbSnapApplyDObjAnim` patched node *values* in place on the existing live `dobj->aobj` linked list. The live chain topology (node count and per-slot track/kind ordering) was never restored. The chain is grown monotonically by `gcParseDObjAnimJoint` calling `gcAddAObjForDObj` (head-prepend) as the figatree event stream advances; if `anim_frame` drifted even one f32 ULP between peers, the parser reached "add new track" events at different ticks, leading to different chain lengths or orderings. The cross-peer animation hash (`syNetSyncFoldFighterAnimJointContribution`) folds the FULL live chain count and walks head→tail for the first 16 nodes — so a topology mismatch produced a silent `anim_hash` divergence even when every fighter scalar matched.

**Patch (`port/net/sys/netrollbacksnapshot.c`):**

1. Extended `SYNetRbSnapAObjNodeBlob` with `interpolate_ptr` (uintptr_t — code-segment pointer set by `nGCAnimEvent32SetInterp`; stable intra-process across rollback).
2. Extended `SYNetRbSnapDObjAnimBlob` with `is_anim_root` (u8), `dobj_flags` (u8), `event32_ptr` (uintptr_t — active figatree event cursor).
3. Capture now records all of the above and emits `aobj_chain_overflow` diagnostic if a captured chain exceeds the 16-node cap.
4. Apply now **drops the live chain entirely** via `syNetRbSnapDObjDrainAObjChain` (inlined `gcRemoveAObjFromDObj`-equivalent, without the `anim_wait = AOBJ_ANIM_NULL` side-effect we'd overwrite anyway), then reconstructs the snapshot order by allocating fresh AObj nodes via `gcAddAObjForDObj` walking the blob `aobj[N-1..0]` so head-prepend yields blob[0] at the live chain head — bit-for-bit identical to capture order.
5. Apply also restores `is_anim_root`, `dobj->flags`, and `dobj->anim_joint.event32` from the DObjAnimBlob, so the next `gcPlayDObjAnimJoint`/`gcParseDObjAnimJoint` cycle advances from the exact captured stream position.
6. A/B safety: `SSB64_NETPLAY_AOBJ_CHAIN_REBUILD=0` falls back to the legacy in-place patch path for bisect comparison. Default is **rebuild ON**.

**Architectural invariants now upheld by the snapshot pipeline:**

- *Topology determinism:* the AObj chain on every DObj is reproduced exactly (count + node order) after `syNetRbSnapApplyDObjAnim`. Any further parser advance proceeds from a known-equal starting topology.
- *Cursor determinism:* `anim_wait`, `anim_speed`, `anim_frame`, `is_anim_root`, `flags`, and `anim_joint.event32` are all part of the per-DObj snapshot footprint; `gcParseDObjAnimJoint` cannot start advancing from a stale cursor.
- *Hash equivalence:* every field the netsync animation fold reads (`syNetSyncFoldFighterAnimJointContribution`) is now part of the round-trip — including the `chain_total` count, which previously could drift without being restored.

**Coverage:** the fix applies uniformly to every `SYNetRbSnapDObjAnimBlob` site (fighter joints, yaku DObjs, weapon DObjs) since they share the same capture/apply helpers.

**Verification plan:** rerun the v10 auto3/auto4 soak with `SSB64_NETPLAY_RESIM_ANCHOR_PROBE=1` and confirm `match_f=1` on reanchor at tick 1560 (pair3) and that pair4-class load drifts at 900 either disappear or contain *only* topology-invariant scalars. If `anim_hash` still forks during pure-locomotion windows after this patch, the remaining divergence is Tier 2B (per-tick `gcPlayDObjAnimJoint` arithmetic), not Tier 1.

## Log verdict (what we already know)

| Observation | Implication |
|-------------|-------------|
| `inp_*` agrees at FC; `world`/`rng` often match | Not ingress / wire inputs |
| First cross-peer **`fhash_light`** split @1595 (pair3) / @958 (pair4) | Forward **fighter sim** fork, not recovery-only |
| `RESIM_ANCHOR_PROBE`: `match_f=0`, `match_cam=1` @1561 | One sim step after load: **fighter hash** diverges from ring; camera OK |
| `fighter_field_diff tag=resim_anchor_probe`: `gobj_anim_frame`, `joint_anim_frame`, `top_joint_y`, `coll_pos_prev_y` | **Joint / presentation layer** is the first named delta — aligns with joint-anim suspicion |
| Pair3: `LOAD_HASH_DRIFT anim-only` @1560 → resim runs; **post `figh` differs host vs client** | Replay does not reconverge — **runtime sim path**, not snapshot wire baseline (baseline slot fix already OK) |
| Pair4: `LOAD_HASH_DRIFT` **figh+anim** @900 → no `resim begin` | Load verify blocks recovery; live @960 far from ring@900 |

**Do not confuse hash pipelines:**

| Log field | Function | Notes |
|-----------|----------|--------|
| `sim_state_tick figh=` | `syNetSyncHashBattleFighters()` (light) | Periodic NetSync |
| FC `token_figh_*` | Ring `hash_fighter` @ `validation_tick - 1` | `syNetFrameCommitBuildToken` |
| `LOAD_HASH_DRIFT figh=` | `syNetSyncHashBattleFightersFull()` | Slot vs live after load |
| `fighter_slot_hash anim_hash=` | `syNetSyncHashFighterSlotAnim` | Per-player anim partition (same fold as one fighter in rollback anim hash) |
| `sim_state_tick anim=` (global) | `syNetSyncHashFighterAnimationStateForRollback()` | **Slot-major merge** (`slot_hash[player]` → fixed merge), same structural pattern as `syNetSyncHashBattleFighters`; **historical**: pre-2026-05-25 merged folds in **`link_next` order** (order-dependent). Trace: [netplay_anim_hash_code_trace_2026-05-25.md](netplay_anim_hash_code_trace_2026-05-25.md). |

---

## Bisect tiers (run in order)

### Tier 0 — Observability (before blaming game code)

1. **`SSB64_NETPLAY_FIGHTER_SLOT_HASH_LOG=1`** with `TICK_MIN`/`MAX` bracketing fork (e.g. 1550–1610 / 940–970).
2. **`SSB64_NETPLAY_SNAPSHOT_FIGHTER_FIELD_DIFF=1`** — grep `fighter_field_diff` + `jointN_aobj_trunc`.
3. **`SSB64_NETPLAY_RESIM_ANCHOR_PROBE=1`** — confirms load+1 step vs ring.
4. **`SSB64_NETPLAY_FIGHTER_PHASE_TRACE=2`** — requires hooks in `ftMainProcUpdateInterrupt` / `ftMainProcParams` and `ifCommonBattleGoUpdateInterface` (wired 2026-05-23). Grep `ft_phase` / `ft_phase_assert`.
5. If `gch` matches at fork tick: composition ruled out → stay in fighter/joint tier ([`netplay_frame_composition.md`](../netplay_frame_composition.md)).

**Pair3 soak had `FIGHTER_PHASE_TRACE=1` but no `ft_phase` lines** — hooks were not called from `ftmain.c` until this bisect wiring.

---

### Tier 1 — Snapshot capture / restore (load & resim)

**Hypothesis:** Ring stores joint state, but load path clobbers or only partially restores AObj chains.

| Step | Function | File | Bisect action |
|------|----------|------|----------------|
| 1.1 | `syNetRbSnapCaptureFighter` | `netrollbacksnapshot.c` | Copies `joint_translate[]`, `joint_anim[]`, `gobj_anim_frame` |
| 1.2 | `syNetRbSnapCaptureDObjAnim` | same | Caps AObj chain at **`SYNETROLLBACK_SNAPSHOT_AOBJ_CHAIN_MAX` (16)**; logs `jointN_aobj_trunc` if `aobj_chain_total > aobj_count` |
| 1.3 | `syNetRbSnapApplyFighter` → `syNetRbSnapApplyDObjAnim` | same | Restores only **existing** `dobj->aobj` nodes up to `aobj_count` — does not rebuild truncated tail |
| 1.4 | `syNetRbSnapshotFinalizeLoad` → `syNetRbSnapshotSyncFighterPresentation` | same | **`ftMainRefreshFigatreeVisual`** |
| 1.5 | `lbCommonAddFighterPartsFigatree` | `lbcommon.c` | **`gcAddDObjAnimJoint`** — resets AObj kinds, sets `anim_joint` from figatree |
| 1.6 | `syNetRbSnapReapplyFighterJointAnimFromSlot` | `netrollbacksnapshot.c` | Re-applies blob after figatree; **`syNetRbSnapshotReapplyJointAnimAtTick`** |
| 1.7 | `syNetRollbackVerifyLoadedSlot` | `netrollback.c` | Compares live **full** `figh` + **`syNetSyncHashFighterAnimationStateForRollback`** vs slot |

**A/B env:**

- Default presentation: unset `SSB64_NETPLAY_SNAPSHOT_FIGHTER_CLEANUP` → `ftMainRefreshFigatreeVisual` only.
- **`SSB64_NETPLAY_SNAPSHOT_FIGHTER_CLEANUP=force`** → legacy `ftMainSetStatus` on load (reproduces figatree + motion replay class; intro flicker).

**Expected if Tier 1 is root:** `jointN_aobj_trunc` at save tick; `anim_hash` fork ≤ `fhash_full` fork; anchor probe `match_f=0` with only anim/joint fields.

---

### Tier 2 — Per-tick forward sim (where pair3/4 actually fork)

**Hypothesis:** Peers agree on inputs but diverge while advancing joint anim during the same sim tick.

#### 2A — Interrupt / input → published `pl` (Phase A–B)

| Step | Function | File | Notes |
|------|----------|------|-------|
| 2A.1 | `ftMainProcUpdateInterrupt` | `ftmain.c` | Reads `SYController` → `fp->input.pl`; hitlag preserves tap masks |
| 2A.2 | `syNetInputPublishFrame` / history | `netinput.c` | Frozen `Inputs_t` for tick — must match `ft_phase` hist hash |
| 2A.3 | `ftMainPlayAnimEventsAll` (hitlag==0) | `ftmain.c` | Called from interrupt after input block |

**Bisect:** `FIGHTER_PHASE_TRACE=2` — first slot where `hmatch=0` or `plB`/`stB` diverges cross-peer (unlikely in v10 — `inp` agreed).

#### 2B — Motion scripts & anim keys (primary suspect)

| Step | Function | File | Notes |
|------|----------|------|-------|
| 2B.1 | `ftMainPlayAnimEventsAll` | `ftmain.c` | `ftMainPlayAnim` + `ftMainUpdateMotionEventsAll` |
| 2B.2 | `ftParamUpdateAnimKeys` | `ftparam.c` | Per joint: `gcParseDObjAnimJoint` **or** `ftAnimParseDObjFigatree`, then **`gcPlayDObjAnimJoint`** |
| 2B.3 | `ftParamsUpdateFighterPartsTransform` | `ftparam.c` | Walks skeleton; clears `transform_update_mode` |
| 2B.4 | `gcPlayDObjAnimJoint` / `gcParseDObjAnimJoint` | `objanim.c` / decomp | Runtime AObj advance |
| 2B.5 | `ftMainProcParams` | `ftmain.c` | Damage queue, shield, **`proc_hit`** → damage states |

**Bisect:** At fork tick, compare `fighter_slot_hash` **`anim_hash`** first; if split, log `status`/`motion` and enable `SNAPSHOT_FIGHTER_FIELD_DIFF` on a **local synctest** at that status.

#### 2C — Status transition / knockback (combat window)

| Step | Function | File | Notes |
|------|----------|------|-------|
| 2C.1 | `ftCommonDamageInitDamageVars` | `ftcommondamage.c` | Sets hitstun, **`vel_damage_*`**, calls **`ftMainSetStatus`** + **`ftMainPlayAnimEventsAll`** |
| 2C.2 | `ftMainSetStatus` | `ftmain.c` | Resets joint defaults from `dobjdesc`; **`lbCommonAddFighterPartsFigatree(..., frame_begin)`** |
| 2C.3 | `syUtilsRandFloat()` in fly-roll branch | `ftcommondamage.c` | **RNG branch** — only if `%/RNG` already diverged |
| 2C.4 | `ftParamsUpdateFighterPartsTransformAll` | `ftparam.c` | Called from damage fly paths |

**Bisect:** If fork tick coincides with hitstun enter/exit, bracket with `status`/`motion` from `fighter_slot_hash`. Match v10: last agreed FC @1560, fork @1595 → inspect ticks 1561–1595 for first `anim_hash` mismatch.

#### 2D — Physics / collision (feeds joint translate)

| Step | Function | File | Notes |
|------|----------|------|-------|
| 2D.1 | `ftMainProcPhysicsMap` | `ftmain.c` | Map collision integration |
| 2D.2 | `syNetRbSnapApplyMPColl` | `netrollbacksnapshot.c` | `coll_pos_prev` in anchor probe diffs |
| 2D.3 | `gmCollisionTransformMatrixAll` | decomp | When `transform_update_mode == 3` (anim locks) |

---

### Tier 3 — Cross-peer replay (pair3 resim post mismatch)

| Step | Function | Notes |
|------|----------|-------|
| 3.1 | `syNetRollbackRunResim` / forward sim | Same Tier 2B path per replay tick |
| 3.2 | `resim complete` baseline/post | Host `figh post=0xF234FFAB` vs client `0x004F922D` with same `load_tick` |

**Conclusion if 3.2 reproduces without load errors:** Determinism bug in **Tier 2B–2C** (not Tier 1 baseline wire). Fix belongs in sim/anim, not net transport.

---

## Recommended soak env (both peers)

```bash
export SSB64_NETPLAY_SIM_STATE_TICK_INTERVAL=1
export SSB64_NETPLAY_FIGHTER_SLOT_HASH_LOG=1
export SSB64_NETPLAY_FIGHTER_SLOT_HASH_TICK_MIN=1550   # pair3 class; use 940 for pair4
export SSB64_NETPLAY_FIGHTER_SLOT_HASH_TICK_MAX=1620
export SSB64_NETPLAY_SNAPSHOT_FIGHTER_FIELD_DIFF=1
export SSB64_NETPLAY_SNAPSHOT_FIGHTER_DIAG=1
export SSB64_NETPLAY_RESIM_ANCHOR_PROBE=1
export SSB64_NETPLAY_FIGHTER_PHASE_TRACE=2
export SSB64_NETPLAY_FHASH_LIGHT_MISMATCH_TRIGGER=1
```

Optional isolate Tier 1: `export SSB64_NETPLAY_SNAPSHOT_FIGHTER_CLEANUP=force` (one run only).

---

## Pass / fail per tier

| Tier | Pass | Fail |
|------|------|------|
| 0 | `ft_phase` lines appear; first `anim_hash` mismatch tick identified on both logs | No `fighter_slot_hash` / `ft_phase` at fork |
| 1 | No `jointN_aobj_trunc` at load tick; anchor `match_f=1` or only `anim` with anim-only continue acceptable | `aobj_trunc` + `match_f=0` + `joint_anim_frame` diffs |
| 2 | `anim_hash` and `fhash_full` match through fork window with agreed `inp` | First `anim_hash` or `fhash_full` split with same `status`/`motion` |
| 3 | After resim, host/client `resim complete` post `figh` match | Post `figh` still swapped (pair3 class) |

---

## Fix direction (after bisect confirms joint anim)

1. **If Tier 1:** Extend AObj capture / rebuild chain on apply; or defer `ftMainRefreshFigatreeVisual` until after verify; verify-after-coupling + reapply (see [`netrollback_weapon_load_finalize_order_2026-05-20.md`](netrollback_weapon_load_finalize_order_2026-05-20.md)). **— Landed 2026-05-25 (see top of this doc).**
2. **If Tier 2B:** Breakpoint bisect inside `ftParamUpdateAnimKeys` (`is_anim_joint` vs figatree path) at first `anim_hash` mismatch tick.
3. **If Tier 2C:** Audit `ftMainSetStatus` joint reset vs snapshot `joint_translate` at hitstun boundaries; ensure motion script pointer state is in blob (already `motion_scripts[][]`).
4. **Policy:** On inp-agree FC drift, allow **`ROLLBACK_LOAD_HASH_SOFT`** or anim-only continue for **figh** at reanchor load (pair4 @900) so resim can run.

---

## Related

- [`fighter_snapshot_fidelity_2026-05-21.md`](fighter_snapshot_fidelity_2026-05-21.md)
- [`netplay_resim_baseline_slot_digest_2026-05-23.md`](netplay_resim_baseline_slot_digest_2026-05-23.md)
- [`netrollback_fighter_midmatch_drift_2026-05-20.md`](netrollback_fighter_midmatch_drift_2026-05-20.md)
- [`../netplay_frame_composition.md`](../netplay_frame_composition.md)
