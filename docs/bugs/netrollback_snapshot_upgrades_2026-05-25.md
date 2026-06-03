# Rollback snapshot upgrades (effects + ground) — 2026-05-25

**Status:** SHIPPED

Follow-up to [`netrollback_snapshot_closure_2026-05-25.md`](netrollback_snapshot_closure_2026-05-25.md) and the remaining-GObj audit.

## Shipped

### Effects (links 6/8)

- Enumerate/capture **no-struct** particle-shell effect GObjs (`user_data == NULL`, `nGCCommonKindEffect`).
- **Prune fighter-attached** effects not listed in the snapshot (or fighter blob id fields).
- **Respawn whitelist:** quake, shield, Yoshi shield, Fox reflector, Ness PK Thunder wave (when `fighter_gobj_id` valid).
- `is_effect_attach` on fighter blob; finalized after effect rebind.
- Effect hash includes no-struct shells.

### Stage ground (`gGRCommonStruct`, link 1)

Per-`gkind` sanitized payloads (pointer fields → GObj ids or omitted):

| Stage | Fields |
|-------|--------|
| Hyrule | Twister motion/waits/status + `twister_gobj_id` |
| Jungle | Tarucann status/wait/rotate + `tarucann_gobj_id` |
| Zebes | Acid level/status |
| Yamabuki | Gate/monster waits + gobj ids |
| Inishie | Splat + power block / pakkun ids |
| Pupupu | Whispy timers/status |
| Castle | Bumper pos + `bumper_gobj_id` |

Folded into `hash_map` at save (tag `0x47524F55`). Apply after map, before world. Load verify must use `syNetRbSnapshotComputeMapHashLive()` (same fold) — see [`netrollback_map_hash_verify_ground_fold_2026-05-25.md`](netrollback_map_hash_verify_ground_fold_2026-05-25.md).

### Audit

`SSB64_NETPLAY_GOBJ_LINK_AUDIT=1` now logs link **2** (`ItemActor`) count.

## Pass 2 (same day)

### Item hold / `lbCommonEjectTreeDObj`

- **PORT guard** in `lbCommonEjectTreeDObj` when `child == NULL` after eject.
- `itMainDetachOrphanHoldDisplay` / `itMainSetFighterRelease` skip eject without inner child.

### Ground extensions

- **Sector Z**, **Yoster** (3 clouds), **Inishie** (scales + player timers).

### Baseline wire v2

- Packet **72** bytes (+ `hash_effect`). Legacy **68** / **56** receive paths unchanged.

## Still open

- Inishie `pblock_pos_ids` (stage-static pointer table).

## Sector Z Arwing (2026-05-30)

- `SYNetRbSnapGroundSector` v2 scalars (`map_gobj_flags`, `unk_sector_*`).
- `SYNetRbSnapArwingBlob` slot partition (12× `SYNetRbSnapDObjAnimBlob`).
- `syNetRbSnapEnsureSectorArwingAfterParticleReset` + `grSectorReestablishArwingVisualTree`.
- See [`netplay_sector_arwing_rollback_2026-05-30.md`](netplay_sector_arwing_rollback_2026-05-30.md).
