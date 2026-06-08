# Netplay: dual-shield per-player identity (4p) — 2026-06-08

**Status:** FIX SHIPPED (Phase 48 soak pending)  
**Scope:** PORT netmenu rollback shield capture / reconcile / ensure

## Symptom

Dual-shield hold (Falcon vs Kirby, synctest ON): shields flicker like resim every ~120 ticks, stamina drain stalls at a plateau (~48 health), one player's bubble visually jumps to the other fighter. Sync-report stayed `MATCH: STABLE` — local effect identity bug, not peer desync.

Log signature (2110-tick soak post Phase 46):

- Both shield blobs saved with same `gobj_id=1011` but different `shield_player=0/1`
- Synctest verify: `effect_respawn kind=SHIELD` ×4 per probe → live `count=8` (2 blobs → 8 bubbles)
- Post-probe tick: `bubble=0`, `shield_decay_wait=0`, `is_shield=1`, `z_live=1` — drain frozen ~530 ticks
- `LOAD_HASH_DRIFT eff-only` soft-continue every ~120 ticks; `resim=0`

## Root cause

Shield effects share a fixed pool link id; ring `gobj_id` is **not** a unique per-player key when multiple bubbles are live. Reconcile/enforce resolved shields via `gcFindGObjByID(blob->gobj_id)` first, collapsing both slot blobs onto one live GObj. `guard_effect_gobj_id` backfill copied the same id to every fighter in guard scope.

## Fix — player-slot identity framework (`GMCOMMON_PLAYERS_MAX`)

| Area | Change |
|------|--------|
| `syNetRbSnapEffectBlobIsShieldKind` / `FindLiveShieldEffectForPlayer` | Shield lookup keyed by `shield.player` slot (0..3), not pool id |
| `syNetRbSnapLiveEffectMatchesBlob` | Shield branch requires matching `shield_player` |
| `syNetRbSnapResolveLiveEffectGobjForBlobApply` | Shields skip ring-id lookup; per-player reconcile mask + GObj pointer list |
| `syNetRbSnapTrackReconciledEffectGobjShieldAware` | Track reconciled player bits alongside ids (shared-id safe) |
| `syNetRbSnapNormalizeCapturedShieldIdentity` | Post-capture: one blob per player, canonical `fighter_gobj_id` from fighter blob |
| Capture | Stamp shield `fighter_gobj_id` from `slot->fighters[player].gobj_id` |
| `guard_shield_ensure` | Reject foreign `guard->effect_gobj` (another player's bubble) before spawn |
| `PruneDuplicateShieldEffects` | Prefer `FindLiveShieldEffectForPlayer` over `gcFindGObjByID` |

### Phase 47 — synctest respawn idempotency + post-probe bubble recovery

Phase 46 per-player identity reduced cross-player jump but synctest verify still stacked shields (2 slot blobs × multiple respawn passes → 8 live bubbles). Post-emergency-restore reconcile ran with `slot != NULL` (no live-forward bubble ensure), leaving `bubble=0` while `is_shield=1` and freezing stamina drain.

| Area | Change |
|------|--------|
| `syNetRbSnapAdoptLiveShieldForBlob` | Never spawn when player slot already owns a live shield |
| `TryRespawnEffectFromBlob` | Shield/Yoshi shield: adopt by `shield.player`; verify-only abort when count>0 |
| `ApplyEffectBlobToGObj` | Adopt live per-player shield before TryRespawn |
| Reconcile pass 3 | Shield adopt-by-player before respawn; skip respawn when count>0 |
| `ResolveShieldParentGobj` | Drop `FindShieldOwnerGobjFromSlot` shared-id fallback |
| `EnsureShieldEffectsFromSlot` verify | Patch via `FindLiveShieldEffectForPlayer` when fighter lookup misses |
| `syNetRbSnapshotRecoverGuardShieldBubblesAfterSynctest` | After emergency restore, run live-forward guard/shield reconcile |
| `syNetRbSnapshotSlotDualActiveShieldHold` | Synctest probe skip when ≥2 fighters in active dual shield hold |

### Phase 48 — single-shield synctest `shield_decay_wait` restore

Phase 47 fixed post-probe `bubble=0` but single-shield probes still ran synctest and left `shield_decay_wait=0` while `bubble=1` — vanilla `ftCommonGuardUpdateShieldVars` never drains when decay_wait is already 0. Dual-shield only worked because `dual_active_shield_probe` skipped probes entirely.

| Area | Change |
|------|--------|
| `syNetRbSnapStashEmergencyGuardShieldDecayWait` | At emergency capture, stash per-player live `shield_decay_wait` |
| `syNetRbSnapRestoreGuardShieldDecayWaitAfterSynctest` | After bubble reconcile, restore stashed wait (fallback: emergency blob, then `FTCOMMON_GUARD_DECAY_INT`) when active guard + `decay_wait==0` + `shield_health>0` |

## Verify

1. Re-soak **single-shield** hold with synctest ON — continuous drain past ~49 plateau; post-probe logs show `decay_wait>0` or cycling, not stuck at 0.
2. Re-soak dual-shield hold with synctest ON — verify pass shows `count=2` not 8, no post-probe `bubble=0` stall, continuous stamina drain toward low health.
3. A/B: `SSB64_NETPLAY_ROLLBACK_SYNCTEST=0` should match offline drain curve (confirms synctest as trigger).
4. 4p FFA: all guarding players keep distinct bubbles through rollback load + synctest probe.
5. Expect 0 eff-only `LOAD_HASH_DRIFT` on shield synctest ticks (or repair without 2→8 duplication).
