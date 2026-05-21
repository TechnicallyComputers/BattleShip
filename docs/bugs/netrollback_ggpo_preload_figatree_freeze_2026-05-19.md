# GGPO initiator figatree mutation before snapshot load (2026-05-19)

## Symptoms

- Host-only `LOAD_HASH_DRIFT` anim-only @691 after GGPO @707: `slot_anim=0xED4F9D7C`, `live_anim=0x82396D19`; `figh`/world/rng match.
- Client loads same slot 691 with `live_anim == slot_anim`.
- Host log: `ftMainSetStatus motion=14` with new `figatree` pointer **before** `LOAD_TICK_ADJUST` / `item apply tick=691`.
- `item_count=0` in 691–707 window — not item/blob format; live graph poisoned before apply.

## Root cause

1. `syNetRbSnapApplyFighter` writes raw `status_id` / `motion_id` / joint anim scalars — **no** `ftMainSetStatus`, **no** figatree re-attach.
2. On the GGPO initiator, `scVSBattleFuncUpdate` ran `ifCommonBattleUpdateInterfaceAll()` (battle sim, including `ftMainSetStatus` / figatree attach) **before** `syNetPeerUpdate()` → `syNetRollbackTryBeginDeferredMismatch()` → `syNetRbSnapshotLoad`.
3. Live-sim cap at `mismatch_tick - 1` existed only for **symmetric** deferred corrections, not ordinary GGPO deferred queue.

Pre-load figatree attach left joint/AObj state that scalar apply could not fully reconcile; verify anim hash failed while CSI scalars matched blob.

## Fix

1. **`syNetRollbackDeferredCorrectionBlocksLiveAdvance`** — cap live sim at `deferred_mismatch_tick - 1` for **all** pending deferred input corrections (GGPO + symmetric).
2. **`syNetRollbackPumpCorrectionBeforeBattleSim`** — call `TryBeginDeferredMismatch` / `TryBeginDeferredStateMismatch` at the start of `scVSBattleFuncUpdate` before battle sim.
3. **Peer epoch cap** — include any deferred correction `target_tick` in `syNetRollbackComputePeerEpochLiveCap`.

## Verify

Re-run Dream Land soak with homerun-bat / stick-correction scenario:

- After `GGPO input correction queued`, host must **not** log `ftMainSetStatus` with a new figatree before snapshot load for that episode.
- `fighter_anim post-load` @ load tick: `live_anim == slot_anim`.
- No anim-only `LOAD_HASH_DRIFT` on initiator load @691 for this episode class.
