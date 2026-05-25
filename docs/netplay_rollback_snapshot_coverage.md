# Rollback snapshot coverage matrix

Tracks what the typed rollback ring captures vs what still risks crash or gameplay drift. Complements [`netplay_rollback_refactor_contracts.md`](netplay_rollback_refactor_contracts.md).

**Legend:** Y = captured/applied; P = partial; N = not in blob; H = hashed for verify; B = peer baseline wire (`ROLLBACK_BASELINE` figh/world/item/rng subset).

| Subsystem | Blob / apply | Hash (`hash_*`) | LOAD verify | Baseline wire | Synctest probe | Phase |
|-----------|--------------|-----------------|-------------|---------------|----------------|-------|
| Fighters (`FTStruct` + joints) | Y | `figh` H | Y | partial (slot digests) | — | baseline |
| World (`SCBattleState`, RNG, item spawn tables) | Y | `world` H | Y | Y (`rng`, `world`) | — | baseline |
| Map yakumono (kin + anim) | Y (cap 64) | `map` H | Y | N | `SynctestProbeMapMismatch` | 2 |
| Map blast bounds (`gMPCollisionBounds`) | Y | folded in `map` H | Y | N | (via map probe) | 1 |
| Items | Y (cap 32) | `item` H | Y | Y | — | baseline |
| Weapons | Y (cap 32) | `weapon` H | Y | N | `SynctestProbeWeaponMismatch` | baseline |
| Camera | Y | `cam` H | Y | N | — | baseline |
| Fighter animation (AObj cap 16) | Y (joint blobs) | `anim` H | Y | N | — | baseline |
| Effects (`EFStruct`, links 6/8) | Y (presence + vars + quake respawn) | `effect` H | opt (`VERIFY_EFFECT_HASH`) | N | `SynctestProbeEffectMismatch` | 4 |
| Particles (`LBParticle`) | Y (rollback teardown) | N | N | N | — | 3 |
| Fighter `effect_gobj` pointers | Y (id rebind) | in `figh` | Y | N | — | 5 |
| Stage GObjs (ground, link 13, …) | N (audit only) | `gcRunAll` diag | N | N | `GObj link audit` env | 6 |
| Particles RNG | cosmetic split | — | — | — | — | [`netrollback_rng_item_identity_drift_2026-05-17.md`](bugs/netrollback_rng_item_identity_drift_2026-05-17.md) |

## Caps (save fails if truncated)

| Partition | Max | Env diag |
|-----------|-----|----------|
| Items | `SYNETRB_SNAPSHOT_MAX_ITEMS` (32) | `SSB64_NETPLAY_SNAPSHOT_ITEM_DIAG` |
| Weapons | 32 | weapon diag |
| Effects | 48 | `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG` |
| Yakumono | 64 | map save logs |

Hash walks use the same caps as snapshot enumeration (`SYNET_SYNC_*_HASH_SORT_MAX` tied to snapshot caps where noted).

## Soak probes

- `syNetRbSnapshotSynctestProbeWeaponMismatch(tick)`
- `syNetRbSnapshotSynctestProbeEffectMismatch(tick)`
- `syNetRbSnapshotSynctestProbeMapMismatch(tick)` — yakumono count and bounds capture flag
- `SSB64_NETPLAY_GOBJ_LINK_AUDIT=1` — link census after snapshot apply
- `SSB64_NETPLAY_ROLLBACK_VERIFY_EFFECT_HASH=1` — include `hash_effect` in LOAD verify

## Open risks (explicit)

- Universal effect respawn from `bank_id` — whitelist growth only (quake magnitude in blob).
- `aobj->interpolate` (TraI) on yakumono — add per-stage if a stage needs it.
- Full particle population snapshot — only if probes show gameplay hash drift.
