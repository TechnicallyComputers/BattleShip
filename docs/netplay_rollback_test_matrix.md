# Netplay rollback test matrix

Manual soak checklist after typed snapshot rollout (`netrollbacksnapshot.c`).

## Env presets

| Scenario | Suggested env |
|----------|----------------|
| Baseline 1v1 | `SSB64_NETPLAY_ROLLBACK=1`, `SSB64_NETPLAY_ROLLBACK_SNAPSHOT_FRAMES=32` |
| Deep rollback | `SSB64_NETPLAY_ROLLBACK_SNAPSHOT_FRAMES=64` |
| Forced resim | `SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH=1`, `SSB64_NETPLAY_ROLLBACK_INJECT_TICK=<T>` |
| Load verify | `SSB64_NETPLAY_ROLLBACK_LOAD_HASH_VERIFY=1` (default) |
| Catch-up budget | `SSB64_NETPLAY_ROLLBACK_RESIM_TICKS_PER_FRAME=4` |

## Cases

1. **No-items 1v1** — verify no `LOAD_HASH_DRIFT` after forced resim.
2. **Projectile-heavy** — Link/Samus/Pikachu; confirm weapon snapshot restore.
3. **Item-heavy** — high item rate; confirm item spawn + active item hashes stable.
4. **4-player / high delay** — ring depth 64; stress mismatch scan + paced resim.

## Pass criteria

- Resim completes to frontier without load failures.
- Subsystem `LOAD_HASH_DRIFT` logs absent for covered hashes (fighter/world/item/weapon/map/rng/camera/animation).
- Gameplay continues without ghost items/weapons after rollback.
