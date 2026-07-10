# Rollback map hash save/verify parity (Sector deck + hash-only quantize)

**Date:** 2026-06-03  
**Scope:** `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netsync.c`, `port/net/sys/netplay_sim_quantize.c`, `decomp/src/gr/grcommon/grsector.c`  
**Status:** FIX SHIPPED — follow-up kin instant + repair dedup in [netrollback_map_hash_save_verify_kin_2026-06-04.md](netrollback_map_hash_save_verify_kin_2026-06-04.md)

## Symptoms

Sector Z netplay synctest reported **`map_mismatch`** with **`figh=` matched** while live peers stayed `state_diverge=0`. Reproduced with `SSB64_NETPLAY_SIM_F32_QUANTIZE=0` (quantize-off bisect): self round-trip failed even when forward sim agreed.

## Root cause

1. **Save vs verify ground fold timing** — `slot->hash_map` folded `slot->ground` captured before the pre-hash Sector deck reconcile (`grSectorArwingUpdateCollisions` updates collision flags and yakumono line 1). Verify recomputed ground from live after reconcile → ground fold diverged.

2. **Dual authority for Arwing deck** — map apply restored `mp_yaku[1]` from blob, then Arwing flight-tree repair re-derived line 1; independent blob restore could poison `gMPCollisionSpeeds[1]` before reconcile (double `mpCollisionSetYakumonoPosID` with zero delta).

3. **Quantize in blobs vs hash** — map yaku and Sector ground scalars were quantized into snapshot bytes when sim quantize was on, while verify/save hash paths could disagree when quantize env was off.

## Fix

1. **`syNetRbSnapshotPrepareMapStateForHash`** — shared deck reconcile before map hash on save and verify.

2. **Re-capture ground at save hash time** — after prepare, refresh `slot->ground` so stored blob and hash fold see post-reconcile Sector collision flags.

3. **`syNetRbSnapshotComputeMapHashWithGround` + `syNetSyncHashMapCollisionKinematicsForRollback`** — rollback save/load/synctest use hash-only grid (`syNetplayQuantizeF32ForRollbackHash`) at hash boundaries; snapshot blobs stay raw.

4. **`syNetRbSnapFoldGroundPayloadHashForRollback`** — quantize known ground-payload F32 fields in a scratch copy for fold only (Sector/Hyrule/Jungle).

5. **Skip `mp_yaku[1]` blob apply** when Sector patrol is active; flight tree is sole authority until reconcile.

6. **`grSectorArwingUpdateCollisions`** — single `mpCollisionSetYakumonoPosID` per frame (removed duplicate call that zeroed deck speed).

## Test plan

1. Sector Z synctest soak (Linux↔Linux, quantize off): no `map_mismatch` with matched `figh=`.
2. Same with default quantize on: no regression on Yoshi deck jump / FC recovery.
3. Confirm snapshot blobs still carry raw F32 (no grid rounding in `mp_yaku` capture).
