# Guard shield effect snapshot load drift — 2026-06-01

**Status:** FIX SHIPPED (soak pending)  
**Scope:** `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netsync.c`

## Symptom

Cross-ISA netplay (Android host + Linux guest, Saffron City): DK holds Z/L shield while Link flurries (`status=222` / `Attack100Loop`); Yamabuki Pokémon hits both. Shield bubble sticks on DK and transfers to Link after rollback load.

Log window **6204–6450**: shield effect spawns (`effect_count=0→1`, `eff=0x7C5B130F`); Link flurry + DK `GuardOn`/`GuardSetOff`; second effect at **6249**; **LOAD_HASH_DRIFT @6448** with `eff=0x3D7226A6/0x7C5B130F`.

## Root cause

Same class as [Fox reflector wrong-fighter coupling](netplay_fox_speciallw_snapshot_load_2026-05-27.md):

1. Shield respawn trusted stale `effect_blob->fighter_gobj_id` after GObj id recycle — bubble could parent to the wrong fighter (Link) while DK's blob still owned `guard_effect_gobj_id`.
2. No ensure-on-load when guard-scope fighter had valid `guard_effect_gobj_id` but missing live GObj after particle reset / reconcile.
3. No prune of shield effects on fighters outside guard scope or with mismatched coupled id / `effect_vars.shield.player`.
4. `guard_effect_gobj_id` not scoped to guard window on read paths (stale id when status left guard).
5. Effect hash did not fold shield `player` + `is_damage_shield`.
6. No pre–joint-anim rebind when any fighter in guard scope (Fox reflector already had this).

## Fix

| Change | Purpose |
|--------|---------|
| `syNetRbSnapBlobInGuardScope` / `syNetRbSnapFighterInGuardScope` | Guard scope = `is_shield` or status `GuardStart..GuardEnd` |
| `syNetRbSnapGuardEffectIdFromBlob` | Zero coupled id outside guard scope |
| `syNetRbSnapResolveShieldParentGobj` | Parent from fighter blob owner, not stale effect blob parent |
| `syNetRbSnapEnsureShieldEffectsFromSlot` | Respawn missing shield / Yoshi shield before effect reconcile |
| `syNetRbSnapPruneStaleShields` | Eject shields on wrong fighter, out-of-scope, wrong coupled id, or player mismatch |
| `syNetRbSnapFindLiveShieldEffectForFighter` | Rebind after respawn assigns new GObj id |
| Load finalize | Guard-scope early `RebindFighterEffectGobjs` before joint anim reapply |
| `syNetSyncHashActiveEffectsForRollback` | Fold shield `player` + `is_damage_shield` |
| Phase 11 (2026-06-05) | Removed synctest `guard_shield` defer — load path is blob-authoritative (see tap-churn doc) |

## Verify

DK vs Link on Saffron; DK shield + Link flurry through Pokémon hit; trim ticks **6180–6500**.

- `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1`: `effect_respawn kind=SHIELD resolved_parent=<DK gobj>` — not Link.
- No shield bubble on Link after restore; DK bubble clears when leaving guard scope.
- No `eff` mismatch at load @6448 with `SSB64_NETPLAY_ROLLBACK_VERIFY_EFFECT_HASH=1`.
