# NetRollback snapshot baseline (pre–typed snapshot ring)

**Date:** 2026-05-16  
**Status:** Baseline captured before `netrollbacksnapshot` rollout.

## Prior behavior

- Ring size followed **`SYNETINPUT_HISTORY_LENGTH` (720)** via `SYNETROLLBACK_RING_LENGTH`.
- Per-tick save stored a **partial** fighter blob (~20 scalars + TopN translate) and up to **32** map yakumono kinematic entries.
- Load verification used **`syNetSyncHashBattleFighters`** and **`syNetSyncHashMapCollisionKinematics`** only; mismatches logged as **`LOAD_HASH_DRIFT`** when partial apply could not reproduce full fighter/map CSI.
- Resim ran **all** catch-up ticks inside a single **`syNetRollbackUpdate`** call (no per-frame budget).

## Diagnostic envs (unchanged)

| Variable | Role |
|----------|------|
| `SSB64_NETPLAY_ROLLBACK` | Master enable |
| `SSB64_NETPLAY_ROLLBACK_INJECT_TICK` / `FORCE_MISMATCH` | Forced mismatch harness |
| `SSB64_NETPLAY_ROLLBACK_LOAD_HASH_VERIFY` | Post-load hash check (default on) |
| `SSB64_NETPLAY_RESIM_TICK_TRACE` | Per resim tick figh/mph log |
| `SSB64_NETPLAY_ROLLBACK_VERIFY_STRICT` | Warn if figh unchanged after resim |

## Expected post-change improvements

- Ring decoupled: **`SSB64_NETPLAY_ROLLBACK_SNAPSHOT_FRAMES`** (default 32, max 64).
- Subsystem CSI hashes: fighter, world, item, weapon, map, RNG, camera, animation.
- Typed restore with stable **`GObj->id`** refs and cosmetic anim cleanup after load.
