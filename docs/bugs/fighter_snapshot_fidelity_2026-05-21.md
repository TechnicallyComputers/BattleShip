# Fighter Snapshot Fidelity — Phase 6 (2026-05-21)

**Date:** 2026-05-21  
**Status:** PHASE 6d APPLY PARITY SHIPPED — operator re-soak required for bisect + pass/fail

## Problem (unified @420 / @600 / @900)

- `inp_local == inp_peer`, `world`/`rng`/`item` match at frame-commit.
- **`figh`** (often **`anim`** + **`camera`**) diverges; live split may precede FC by several ticks.
- No `LOAD_HASH_DRIFT` on either peer → per-peer snapshot round-trip OK; poison is **cross-peer forward sim**.
- Recovery: `BASELINE_UNIVERSE_MISMATCH` @ `load_tick = validation - 1` when baseline digests disagree.

## Phase 6 code shipped

| Area | Change |
|------|--------|
| Slot hash log | `fighter_slot_hash` now logs `fhash_light`, **`fhash_full`**, **`anim_hash`**, **`camera_mode`** per player ([`netsync.c`](../../port/net/sys/netsync.c)) |
| Baseline diff | `BASELINE_UNIVERSE_DIFF` logs per-slot Full + anim + camera; triggers **`fighter_field_diff`** at load tick when `SNAPSHOT_FIGHTER_FIELD_DIFF=1` |
| Replay parity | `FIGHTER_PHASE_TRACE` wired in [`ftmain.c`](../../decomp/src/ft/ftmain.c); `syNetInputFuncRead` returns during resim (no double publish) |
| Anchor probe | `SSB64_NETPLAY_RESIM_ANCHOR_PROBE=1` — one sim step after load; `RESIM_ANCHOR_PROBE` compares ring vs live at `load+1` |
| Episode FSM | `EPISODE_FSM Abort(snapshot_fidelity)` when deeper load exhausted after matching sealed replay inputs |
| Blob extend (6d) | **Shipped** — field diff oracle at `baseline_universe` / `resim_anchor_probe` / `load_drift`; AObj chain cap 12 + truncation metadata; post-load presentation via `syNetRbSnapshotSyncFighterPresentation` |
| Presentation sync on load | **Shipped** — `syNetRbSnapshotSyncFighterPresentation` after load verify (pre-sync hashes); not on synctest emergency path; `ftMainRefreshFigatreeVisual` only; `SSB64_NETPLAY_SNAPSHOT_FIGHTER_CLEANUP=force` for legacy SetStatus bisect |

## Blob vs hash (audit gate)

| Surface | Notes |
|---------|--------|
| `SYNetRbSnapFighterBlob` | Full `physics`, MPColl, joints, anim AObj chain, status/passive vars, coupled weapon IDs |
| `syNetSyncHashBattleFightersFull` | Light + shield, jostle, all joint translates, hitstun/shield flags |
| `syNetSyncHashFighterAnimationStateForRollback` | Separate partition; slot verify checks `hash_animation` |
| `syNetSyncHashFighterSlotFull` / `SlotAnim` | Per-player slices for bisect (new) |

## Operator re-soak protocol

Source [`scripts/netplay-midmatch-fighter-soak.env.example`](../../scripts/netplay-midmatch-fighter-soak.env.example) on **both** peers (automatch host + client).

Suggested window for @420-class breaks:

```bash
export SSB64_NETPLAY_FIGHTER_SLOT_HASH_TICK_MIN=400
export SSB64_NETPLAY_FIGHTER_SLOT_HASH_TICK_MAX=920
# optional: load vs replay poison split
export SSB64_NETPLAY_RESIM_ANCHOR_PROBE=1
```

**Bisect steps:**

1. Grep both logs for first `fighter_slot_hash` where same `player`/`status`/`motion` but **`fhash_full`** differs.
2. Note whether **`anim_hash`** diverges on the same or earlier tick.
3. On recovery, grep `BASELINE_UNIVERSE_DIFF`, `fighter_field_diff`, `RESIM_ANCHOR_PROBE`, `EPISODE_REPLAY_DIVERGE`.
4. With `FIGHTER_PHASE_TRACE=2`, grep `ft_phase_assert` / `ctrl_hist_mismatch` at the fork tick.

## Pass criteria

- Matching **`fhash_full`** / **`anim_hash`** through FC windows (no hidden drift between commits), **or**
- First FC diverge recovers cleanly: `RESIM_POST_MATCH` / `baseline_matched=1`, no `BASELINE_UNIVERSE_STORM_CAP`.
- `BASELINE_UNIVERSE_MISMATCH` only when real input or RNG divergence (not matching sealed inputs).

## Fail criteria

- `FRAME_COMMIT_STATE_DIVERGE` with matching `inp_*`, divergent `figh`, `baseline_matched=0`.
- `EPISODE_FSM Abort(snapshot_fidelity)` or `FC_DEEPEN_STORM` without documented field-level root cause.

## Phase 6d — apply parity (2026-05-21)

1. **`syNetRbSnapshotLogFighterFieldDiffAtTick`** — named scalar diff vs ring blob; tags: `load_drift`, `baseline_universe`, `resim_anchor_probe`, `seal_authority_mismatch`.
2. **AObj chain** — `SYNETROLLBACK_SNAPSHOT_AOBJ_CHAIN_MAX` 6→8→**12** (soak @569: `joint4/6_aobj_trunc stored=8 total=9`); `aobj_chain_total` on capture; log `jointN_aobj_trunc` when truncated.
3. **Presentation sync** — `syNetRbSnapshotSyncFighterPresentation` after load verify in `syNetRollbackLoadPostTick` (apply → coupling rebind → verify → presentation → proc rebind). Synctest probe/restore: coupling only, no presentation. Legacy `ftMainSetStatus` only when `SNAPSHOT_FIGHTER_CLEANUP=force`.
4. **Synctest gating** — skip during `intro_wait`, `item_hold`, `fighter_throw`, `item_throw` (`syNetRbSnapshotSynctestShouldSkip`).
5. **Anim hash** — rollback fold includes `status_id`/`motion_id`, per-joint AObj chain total (detects cap truncation), shared with `syNetSyncHashFighterSlotAnim`.

## Re-soak result (fill after run)

| Field | Host | Client |
|-------|------|--------|
| First `fhash_full` fork tick | _TBD_ | _TBD_ |
| First `anim_hash` fork tick | _TBD_ | _TBD_ |
| FC break validation tick | _TBD_ | _TBD_ |
| `RESIM_ANCHOR_PROBE match_f` @ load | _TBD_ | _TBD_ |
| `fighter_field_diff` first named field | _TBD_ | _TBD_ |
| Outcome | _TBD_ | _TBD_ |

## Related

- [`netrollback_fighter_midmatch_drift_2026-05-20.md`](netrollback_fighter_midmatch_drift_2026-05-20.md)
- [`../netplay_rollback_test_matrix.md`](../netplay_rollback_test_matrix.md) — Mid-match fighter determinism row
