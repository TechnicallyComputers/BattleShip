# NetRollback typed snapshot ring

**Date:** 2026-05-16  
**Status:** RESOLVED (initial rollout)

## Change

Replaced the 720-slot input-history-sized partial fighter/map rollback ring with a dedicated typed snapshot module (`port/net/sys/netrollbacksnapshot.c`):

- Ring size from **`SSB64_NETPLAY_ROLLBACK_SNAPSHOT_FRAMES`** (default 32, max 64).
- Subsystems: fighters (gameplay closure), world (battle + RNG + item spawn), items, weapons, map yakumono (64), camera, animation CSI.
- Stable **`GObj->id`** remapping for cross-references; cosmetic **`gcRemoveAnimAll`** cleanup after load.
- Load verify compares eight subsystem hashes (fighter/world/item/weapon/map/rng/camera/animation).
- Catch-up pacing via **`SSB64_NETPLAY_ROLLBACK_RESIM_TICKS_PER_FRAME`**.

## Baseline

See [`netrollback_snapshot_baseline_2026-05-16.md`](netrollback_snapshot_baseline_2026-05-16.md).
