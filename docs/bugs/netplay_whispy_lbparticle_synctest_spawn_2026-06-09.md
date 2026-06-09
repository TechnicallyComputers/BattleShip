# Dream Land Whispy wind VFX in netplay rollback + synctest (2026-06-09)

**Status:** **RESOLVED** — soak-verified on Android netmenu with `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1` (Kirby vs Yoshi, Dream Land).

## Symptom

On Dream Land (`nGRKindPupupu`), netplay with periodic rollback synctest:

- Whispy **tree mouth / flower GObj anims** worked (ground `map_gobj` anims).
- Whispy **wind leaves** (air LBParticles) and **ground dust** never appeared.
- Wind **gameplay push** could still apply (sim-side particles existed).
- With synctest off on the same build, VFX worked.

Cosmetic LBParticles are **not** in the rollback snapshot blob (`leaves_xf` / `dust_xf` handles are runtime-only). Tree anims use separate ground GObj paths.

## Architecture (vanilla + port)

| Piece | Role |
|-------|------|
| `grPupupuWhispyUpdateBlow` (`grpupupu.c`) | Blow state machine; spawns leaves/dust via `lbParticleMakeScriptID` on Pupupu bank |
| Leaves | `particle_bank_id` + genlink 0 → LBParticle **link 1** |
| Dust | genlink 1 → LBParticle **link 2** |
| `efDisplayInitAll` (`efdisplay.c`) | Permanent effect-link GObjs on display-list **15** and **18** with `efDisplayZPerspCLDProcDisplay` |
| `lbParticleDrawTextures` (`lbparticle.c`) | Emits particle quads when display procs run |
| `gmCamera` capture (`gmcamera.c`) | Captures dl 15 / 18 each frame; empty chains → nothing drawn |

Display GObj `camera_mask` selects which particle **links** to draw (not the dl index): dl 15 gobj draws link 1 (leaves); dl 18 gobj draws links 0 + 2 (dust on link 2).

## Root causes (two layers)

Synctest exposed **both** a sim-side particle lifecycle bug and a render-side infrastructure bug. Fixes for layer 1 alone left particles simulating but invisible.

### Layer 1 — Sim: stale xf handles after particle reset

Periodic synctest calls `syNetRbSnapResetParticlesForRollback()` (`lbParticleEjectStructAll` / `lbParticleEjectGeneratorAll`). `GRCommonGroundVarsPupupu.leaves_xf` / `dust_xf` were **not** snapshotted, so handles could stay non-NULL while LBTransforms were freed. Blow spawn then skipped respawn; structs were not warmed → `leaves_drawable` / `dust_drawable` stayed 0.

### Layer 2 — Render: efDisplay infrastructure ejected on verify

`syNetRbSnapEjectAllNonCanonicalEffectsForVerify` ejected **all** effect-link GObjs not in the canonical synctest set, including permanent `efDisplayInitAll` hooks. After the first `SYNCTEST_OK` (~tick 389):

- `gobj_link_audit` showed `ef6` (effect link count) **7 → 0**.
- `efDisplayInitAll` was never re-run → dl 15 / 18 chains empty.
- `render_draw` stopped after display frame ~390 even when `forward_tick` showed `leaves_drawable=51`.

## Working solution (shipped)

### Sim / spawn (`grpupupu.c`, `netrollbacksnapshot.c`)

- Netplay blow entry: eject + fresh spawn + 64-tick warmup; clear particle skip IDs 1 and 2 on blow entry.
- `grPupupuWhispyNullParticleXfHandles()` after every `syNetRbSnapResetParticlesForRollback()` on Dream Land.
- Eject helpers guard `lbParticleTransformIsAllocated()` (no UAF on stale xfs).
- `UpdateBlow`: respawn broken xfs; 30-tick drawable stall → fresh spawn + 128-tick warmup.
- `syNetRbSnapClearPupupuWhispyStaleParticleXf()` not gated on `whispy_status == Blow`.
- `syNetRbSnapRepairPupupuWhispyPresentationAfterLoad()` for post-load cosmetic repair + warmup on synctest restore.
- Synctest probe skip on Kirby guard→GuardOff boundary (`guard_release_boundary_probe`, tick ~1085 class).

### Render / display infrastructure (`efdisplay.c`, `netrollbacksnapshot.c`, `objman.c`)

- `efDisplayIsInfrastructureGObj()` — identifies permanent particle draw GObjs (dl 10 / 15 / 18 / 25).
- `efDisplayEnsureParticleDrawInfrastructure()` — recreates missing hooks without reloading particle banks.
- Exempt infrastructure from `syNetRbSnapEjectAllNonCanonicalEffectsForVerify`.
- Call `efDisplayEnsureParticleDrawInfrastructure()` after verify eject and after `syNetRbSnapResetParticlesForRollback()`.
- `gcInitGObjCommon` clears `proc_display` / `camera_mask` / `camera_tag` on GObj pool reuse (prevents stale display metadata on recycled slots).

## Key symbols

| Symbol | File |
|--------|------|
| `efDisplayEnsureParticleDrawInfrastructure` | `decomp/src/ef/efdisplay.c` |
| `efDisplayIsInfrastructureGObj` | `decomp/src/ef/efdisplay.c` |
| `grPupupuWhispyNullParticleXfHandles` | `decomp/src/gr/grcommon/grpupupu.c` |
| `syNetRbSnapResetParticlesForRollback` | `port/net/sys/netrollbacksnapshot.c` |
| `syNetRbSnapEjectAllNonCanonicalEffectsForVerify` | `port/net/sys/netrollbacksnapshot.c` |
| `syNetRbSnapRepairPupupuWhispyPresentationAfterLoad` | `port/net/sys/netrollbacksnapshot.c` |

## Verification

**Functional:** Dream Land netplay soak with `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1` — leaves and dust visible during Whispy blow; wind push matches offline.

**Logs** (optional `SSB64_NETPLAY_WHISPY_REPAIR_DIAG=1`):

| Signal | Healthy blow |
|--------|----------------|
| `forward_tick` | `leaves_drawable` / `dust_drawable` > 0, `struct_skip=0x0` |
| `render_draw` | Continues after first synctest; `size_pass>0`, `drawn>0` |
| `display_gobj` | `infra=1`, `dl_link=15` or `18` (not `65`) |
| `gobj_link_audit` | `ef6` stays ≥ 3 after synctest (not stuck at 0) |
| `SYNCTEST_OK` | No false fails at guard-release ticks |

Log trimmer: `./scripts/netplay-trim-logs.py` summarizes `forward_tick`, `render_draw`, `post_verify`.

## Related docs

- Snapshot coverage table: [`docs/netplay_rollback_snapshot_coverage.md`](netplay_rollback_snapshot_coverage.md) (Pupupu Whispy repair row).
- Debug env: [`docs/netplay_environment_variables.md`](netplay_environment_variables.md) (`SSB64_NETPLAY_WHISPY_REPAIR_DIAG`).
- Hyrule twister (same post-particle-reset repair dispatcher): [`netplay_hyrule_twister_rollback_2026-05-29.md`](netplay_hyrule_twister_rollback_2026-05-29.md).

## Offline vs netmenu

All Whispy rollback repair is behind `#if defined(PORT) && defined(SSB64_NETMENU)` and `syNetplayRollbackSemanticsActive()`. Offline JRickey builds do not compile or run this path.
