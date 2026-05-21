# NetRollback — fighter snapshot apply order + post-verify rebind

**Date:** 2026-05-19  
**Status:** Fix shipped (soak verification pending)

## Symptom

Automatch host/client soak (~tick 161–186):

- `FRAME_COMMIT_STATE_DIVERGE` at validation 120 and 180 with **matching** `inp_local` / `inp_peer`, divergent `figh`.
- Client `LOAD_HASH_DRIFT` at tick **161**: **figh** and **anim** live ≠ slot after load (`load post tick 161 failed`).
- Host stopped on peer `VS_SESSION_END`; no local `LOAD_HASH_DRIFT`.

Related prior work: [`netrollback_fighter_joint_anim_event32_2026-05-19.md`](netrollback_fighter_joint_anim_event32_2026-05-19.md) (joint `event32` / flags in blob).

## Root cause

1. **Asymmetric apply order** — Save captured fighters, then map/world/items/weapons/camera. Load applied **map → world → camera → fighters**, so floor/MPColl state could change before fighter joint/coll restore, producing `figh` drift at verify even when the blob was faithful.

2. **`ftMainRebindStatusProcs` before verify** — `syNetRbSnapApplyFighter` rebinded status procs immediately after restore. Rebind can advance status/animation in ways that change `syNetSyncHashBattleFightersFull` / `syNetSyncHashFighterAnimationStateForRollback` before `syNetRollbackVerifyLoadedSlot` compared slot hashes captured at save time (post-tick, pre-rebind).

## Fix

| Change | Location |
|--------|----------|
| Load order mirrors save: **fighters → map → world → items → weapons → camera** | `syNetRbSnapApplySlotToLive` in [`port/net/sys/netrollbacksnapshot.c`](port/net/sys/netrollbacksnapshot.c) |
| Remove rebind from `syNetRbSnapApplyFighter`; add `syNetRbSnapshotRebindAllFighters()` | same |
| Call rebind only after successful load verify (and soft-continue paths that keep loaded state) | `syNetRollbackLoadPostTick` in [`port/net/sys/netrollback.c`](port/net/sys/netrollback.c) |
| Emergency restore rebinds after apply | `syNetRbSnapshotRestoreLiveEmergency` |
| **`SSB64_NETPLAY_SNAPSHOT_FIGHTER_DIAG=1`** — `fighter_load_verify` + per-player `fighter_slot` on drift | `syNetRbSnapshotLogFighterLoadVerifyDiag` |

`synctest` still loads without rebind (verify-only probe), then restores emergency — unchanged.

## Verification

Re-run automatch with:

```bash
export SSB64_NETPLAY_ROLLBACK_SYNCTEST=1
export SSB64_NETPLAY_SNAPSHOT_FIGHTER_DIAG=1
# optional bisect:
export SSB64_NETPLAY_JOINT_TRANSLATE_TRACE=1
```

Pass signals:

1. No `LOAD_HASH_DRIFT` on **figh** at ticks 120–200 (anim-only soft-continue may still appear rarely).
2. `SYNCTEST_OK` through tick 200+.
3. `fighter_load_verify`: if drift occurs, `fighter_slot` lines show which player/status/motion forked.
4. No early `VS_SESSION_END` from client snapshot abort during idle VS.

If `FRAME_COMMIT_STATE_DIVERGE` persists with matching inputs, next bisect is true sim divergence or frame-commit sampling vs rollback ring (not apply-order).
