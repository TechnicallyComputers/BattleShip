# Rollback snapshot closure (multi-phase) — 2026-05-25

**Status:** SHIPPED (initial implementation)

Implements the roadmap in [`netplay_rollback_snapshot_coverage.md`](../netplay_rollback_snapshot_coverage.md).

## Shipped in this pass

| Phase | Summary |
|-------|---------|
| 0 | Coverage matrix doc + `SynctestProbeMapMismatch` |
| 1 | `gMPCollisionBounds` in snapshot slot; folded into `syNetSyncHashMapCollisionKinematics` |
| 2 | `SYNETSYNC_MAX_MP_YAKU` aligned to `SYNETRB_SNAPSHOT_MAX_YAKU` (64) |
| 3 | `syNetRbSnapResetParticlesForRollback` — `lbParticleEjectStructAll` / `lbParticleEjectGeneratorAll` before effect reconcile |
| 4 | Effect blob: sanitized `effect_vars`, `quake_magnitude`, apply + quake respawn via `efManagerQuakeMakeEffect` |
| 5 | Fighter blob: `guard` / `captureyoshi` / `fox_speciallw` effect GObj ids + rebind after effect apply |
| 6 | `SSB64_NETPLAY_GOBJ_LINK_AUDIT=1` link census log |
| 7 | `SSB64_NETPLAY_ROLLBACK_VERIFY_EFFECT_HASH=1` optional `hash_effect` in LOAD verify |

## Follow-ups

- TraI `interpolate` on yakumono if a stage requires it.
- Full particle population snapshot (only if probes show gameplay drift).
- Baseline wire extension for `hash_effect` / bounds (versioned packet bump).
- Per-stage GObj whitelist blobs from audit logs.
