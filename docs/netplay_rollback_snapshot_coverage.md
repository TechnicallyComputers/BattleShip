# Rollback snapshot coverage matrix

Tracks what the typed rollback ring captures vs what still risks crash or gameplay drift. Complements [`netplay_rollback_refactor_contracts.md`](netplay_rollback_refactor_contracts.md).

**Legend:** Y = captured/applied; P = partial; N = not in blob; H = hashed for verify; B = peer baseline wire (v2: figh/world/item/rng/weapon/map/camera/effect + anim when FSM requires).

| Subsystem | Blob / apply | Hash (`hash_*`) | LOAD verify | Baseline wire | Synctest probe | Phase |
|-----------|--------------|-----------------|-------------|---------------|----------------|-------|
| Fighters (`FTStruct` + joints) | Y | `figh` H | Y | B (slot + wire digests) | — | baseline |
| World (`SCBattleState`, RNG, item spawn tables) | Y | `world` H | Y | Y (`rng`, `world`) | — | baseline |
| Map yakumono (kin + anim) | Y (cap 64) | `map` H | Y | N | `SynctestProbeMapMismatch` | 2 |
| Map blast bounds (`gMPCollisionBounds`) | Y | folded in `map` H | Y | N | (via map probe) | 1 |
| Items | Y (cap 32) | `item` H | Y | Y | — | baseline |
| Weapons | Y (cap 32) | `weapon` H | Y | B | `SynctestProbeWeaponMismatch` | baseline |
| Camera | Y | `cam` H | Y | B | — | baseline |
| Fighter animation (AObj cap 16) | Y (joint blobs) | `anim` H | Y | N | — | baseline |
| Effects (`EFStruct` + no-struct shells, links 6/8) | Y (vars + coupled prune + respawn whitelist) | `effect` H | opt (`VERIFY_EFFECT_HASH`) | B (when peer sends v2 tail) | `SynctestProbeEffectMismatch` | 4 |
| Particles (`LBParticle`) | Y (rollback teardown + post-reset stage repair) | N | N | N | — | 3 |
| Fighter `effect_gobj` / coupled weapon ptrs in `status_vars` | Y (always scrubbed on capture/apply; id rebind after effects/weapons) | in `figh` | Y | N | — | 5 |
| Stage ground (`gGRCommonStruct` hazards) | Y (10 VS `gkind`s incl. Sector/Yoster/Inishie scales) | folded in `map` H | Y | v2 wire (`effect` + map) | `GObj link audit` env | 6 |
| Stage GObjs (link 13 presentation, …) | N (audit only) | `gcRunAll` diag | N | N | `GObj link audit` env | 6 |
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

## Coupled pointer hygiene (2026-05-25)

`memcpy` of `status_vars` / `passive_vars` never restores raw `GObj *` slots. Capture and apply always clear guard/capture/Fox effect pointers and all known coupled weapon slots; `syNetRbSnapRebindFighterEffectGobjs` and `syNetRbSnapRebindFighterCoupledGObjs` re-resolve from blob ids after effect/weapon apply.

## Post-particle stage repair (2026-05-29)

`syNetRbSnapRepairStageAfterParticleReset` runs after `lbParticleEjectStructAll` / `lbParticleEjectGeneratorAll` on snapshot apply, and again in finalize before effect reconcile:

| Stage | Repair |
|-------|--------|
| Hyrule | Twister gobj pose, xf recreate, obstacle clear/re-add |
| Pupupu | Whispy `leaves_xf` / `dust_xf` when blowing; `efDisplayEnsureParticleDrawInfrastructure` after particle reset / synctest verify (dl 10/15/18/25 particle draw hooks not snapshotted). See [`bugs/netplay_whispy_lbparticle_synctest_spawn_2026-06-09.md`](bugs/netplay_whispy_lbparticle_synctest_spawn_2026-06-09.md). |
| Yoster | Cloud `dobj[]` rebind + yakumono pos |
| Castle | Bumper DObj translate from blob |
| Yamabuki | Tower gate DObj repair/finalize after item apply; yakumono id 3 collision sync; Open-state minimum egress timer in `gate_wait` plus tracked-edge clearance hold |
| Sector Z | Arwing `map_dobjs[]` anim + pose partition, `map_gobj` flags, tree reestablish, `grSectorRepairArwingPresentation` |

Full LBParticle population snapshot remains deferred unless probes show gameplay hash drift.

## Open risks (explicit)

- Effect respawn whitelist — extend when soak finds missing banks (shield/reflect/wave/quake covered).
- `aobj->interpolate` on yakumono — already in `SYNetRbSnapAObjNodeBlob`; enable per-stage if map hash drifts.
- Full particle population snapshot — only if probes show gameplay hash drift.
