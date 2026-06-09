# Netplay guard shield + Whispy quake synctest — 2026-06-09

**Status:** FIX SHIPPED (soak pending)  
**Scope:** `port/net/sys/netrollbacksnapshot.c`

## Symptom

Cross-ISA synctest soak (Kirby vs Ness, Dream Land):

- **Tick 3602:** Ness `GuardOff` (154) during shield release (`release_lag=1`) with lingering Whispy camera quake. Only `eff` partition drifted; `guard_effect_gobj_id live=0 blob=1011`; `LOAD_HASH_DRIFT soft-continue blocked reason=guard_escape_eff_coupling`.
- **Tick 2144 (Phase 47 follow-up):** Kirby `GuardOff` + `release_lag=5` with dual quake+shield on gobj **1011**. Save `effect_count=2`, verify `count=1`; same eff-only drift and synctest fail after Whispy post-blow skip on probe 2143 only.

## Root cause

1. **Gobj-id collision:** Whispy quake (`respawn=1`) and shield bubble (`respawn=2`) shared pool id **1011**. Capture deduped to `effect_count=1` (shield won priority) while live fold still had **2** effects → frozen `ring_eff` vs advancing `live_eff`, repeated `effect_probe_mismatch` skips.

2. **Synctest skip gap:** After bubble ejected @3603, live count matched slot count (1) so probe **3602** ran. Slot still captured GuardOff release-lag + dual-effect state verify could not round-trip. @2144: `guard_release_boundary_probe` required `next_slot` while Whispy skip only covered probe 2143.

3. **Verify guard coupling:** Shield blob present but verify path did not respawn missing bubble before hash fold; `guard_escape_eff_coupling` blocked eff-only soft-continue. **`syNetRbSnapTryRespawnEffectFromBlob` blocked all shield mint during verify-only** (lines ~15414) so reconcile/enforce respawn hooks were no-ops; prune `no_fighter` ejected detached T+1 bubble before hash.

## Fix

| Area | Change |
|------|--------|
| `syNetRbSnapEffectGobjIdCollisionAllowsCoexist` | Allow `QUAKE` + `SHIELD`/`YOSHI_SHIELD` on same gobj_id (capture + sanitize) |
| `syNetRbSnapshotSynctestProbeGuardReleaseBoundaryFragile` | Skip probe when slot fighter is `GuardOff` with `release_lag > 0` and slot lists shield blob — **no `next_slot` required** |
| `syNetRbSnapEnsureShieldEffectsFromSlot` | Verify-only: `TryRespawnEffectFromBlob` when slot lists shield blob but live bubble missing |
| `syNetRbSnapPatchAllGuardShieldsFromSlot` | Verify-only: same respawn before patch |
| `syNetRbSnapTryRespawnEffectFromBlob` | Remove verify-only shield mint block; duplicate checks above suffice |
| `syNetRbSnapEnforceSlotAuthoritativeEffectSet` | Verify-only: `TryRespawnEffectFromBlob` when shield find/adopt still null |
| `syNetRbSnapResolveLiveEffectGobjForBlobApply` | Quake resolve before `gcFindGObjByID`; reject quake/shield gobj for wrong blob kind |
| `syNetRbSnapPruneStaleShields` | Verify-only: defer `no_fighter` eject when slot lists shield blob (player or gobj_id match) |
| `syNetRbSnapReconcileSnapshotEffectsBeforeItems` | Verify-only shield-only respawn pass (pass-3 equivalent) |

## Verify

Re-soak cross-ISA synctest with Whispy wind ending into shield hold/release (~2140–2150 and ~3595–3605):

- Expect `effect_count=2` in slot when quake + shield coexist; `ring_eff == live_eff` on save lines.
- Expect `eff_fold_diag tag=verify count=2` on GuardOff + release_lag probes (not `count=1`).
- Expect `SYNCTEST_SKIP reason=guard_release_boundary_probe` @2144/3602 (or verify OK) instead of `SYNCTEST_FAIL`.
- No `guard_escape_eff_coupling` soft-continue block on those ticks.
