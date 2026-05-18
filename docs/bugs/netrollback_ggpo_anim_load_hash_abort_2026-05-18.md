# GGPO resim hard-abort on anim-only LOAD_HASH_DRIFT (jump)

**Date:** 2026-05-18  
**Status:** RESOLVED

## Symptoms

After P2 (client) jumped, host ran one GGPO input correction (`predicted` stick vs confirmed jump). Session immediately ended with `LOAD_HASH_DRIFT — stopping VS session`. Client received `VS_SESSION_END`; P2 appeared frozen, P1 only moved locally.

## Root cause

`syNetRollbackVerifyLoadedSlot` compared live `syNetSyncHashFighterAnimationStateForRollback()` to the value stored at snapshot save. On load, figatree/status restore can advance animation one frame before verify (jump motions 14/16/20). Fighter/world/RNG/item/wpn/map/cam matched; only `anim` diverged.

`syNetRollbackLoadHashDriftIsSoft()` was false on the first rollback (`rollbacks=1` < threshold 8, env soft off), so `syNetRollbackLoadPostTick` hard-aborted and sent `VS_SESSION_END`.

## Fix

`syNetRollbackLoadHashDriftIsAnimOnly`: when all gameplay hashes match and only animation differs, verify returns success and resim continues (log `LOAD_HASH_DRIFT anim-only — continuing resim`). Gameplay hashes remain hard-fail.

## Files

- `port/net/sys/netrollback.c` — `syNetRollbackLoadHashDriftIsAnimOnly`, wired in `syNetRollbackVerifyLoadedSlot`
- `port/net/sys/netrollback.h` — header note
