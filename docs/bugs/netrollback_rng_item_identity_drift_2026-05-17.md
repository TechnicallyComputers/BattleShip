# NetRollback RNG and item identity drift

**Date:** 2026-05-17  
**Status:** DIAGNOSTIC FIX

## Symptoms

Forced rollback with `FORCE_MISMATCH` proved rollback could load a clean snapshot and run visually stable resim, but an identity rollback still changed post-resim hashes. Later in the same run, item spawns appeared to diverge even when fighter positions looked acceptable.

The logs did not show `LOAD_HASH_DRIFT`, which meant snapshot load matched the stored slot hashes. The gap was post-load execution: the periodic NetSync line only printed input, fighter, and map hashes, so RNG/item/world drift was hidden unless load verification failed.

## Root Cause

The snapshot covered the global RNG seed and the natural item appear actor, but did not capture the separate `gITManagerRandomWeights` table used by container/item selection. Also, visual effect and particle code consumed the same global RNG stream as gameplay, so unsnapshotted cosmetic execution during netplay could perturb later gameplay RNG consumers such as item spawns.

## Fix

Rollback diagnostics now print the full subsystem hash set during resim and periodic NetSync validation: fighter, world, active items, active weapons, map, RNG seed, camera, and animation. Forced identity rollback records pre-resim hashes and logs `ROLLBACK_IDENTITY_DRIFT` when unchanged confirmed input does not reproduce the same post-resim state.

Snapshots now save and restore `gITManagerRandomWeights` scalars plus its active `kinds` and `blocks` arrays. The rollback world hash covers those fields too, so item spawn-policy drift is visible in both load verification and NetSync logs.

Effect and particle systems now use PORT cosmetic RNG wrappers. Outside netplay they delegate to the original gameplay RNG for behavior compatibility; during netplay they advance a separate cosmetic seed, initialized from the gameplay seed at VS session start, so visual randomness does not alter gameplay RNG advancement.
