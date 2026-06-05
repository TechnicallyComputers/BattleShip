# Yoshi neutral-B egg hatch animation — rollback lifecycle — 2026-06-05

**Status:** FIX SHIPPED (soak pending — phase 4: rebind-only live path, no per-tick spawn)  
**Scope:** `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netrollback.c`

## Symptom

Cross-ISA soak: when a victim escapes Yoshi neutral-B egg (`YoshiEgg` / status 178), hatch shell animation and shatter VFX fire inconsistently — sometimes sim escape (Fall) runs without presentation.

Logs showed `effect_apply kind=YOSHI_EGG_LAY fighter_gobj_id=0` (parent not resolved) and eff hash drift after load while fighter blob matched.

## Root cause

1. **Egg-lay effect parent resolution** — effect blobs saved with `fighter_gobj_id=0` when `ep->fighter_gobj` was cleared; apply/respawn did not reverse-lookup victim via `captureyoshi_effect_gobj_id` (unlike guard shield `ResolveShieldParentGobj`).
2. **Live reconcile gap** — `PruneStaleYoshiEggLayEffects` returned early when `slot==NULL`, so orphan egg-lay effects survived between rollback loads.
3. **Hatch particles** — escape-at-save (`Fall` + escape velocity) skips `ftCommonYoshiEggProcUpdate` on load; LBParticle hatch scripts were wiped by rollback particle reset with no replay.
4. **SIGSEGV in `ftCommonYoshiEggProcInterrupt`** — `no_fighter` prune ejected egg-lay GObj without clearing `captureyoshi.effect_gobj` on victim; next tick wiggled `->child == NULL` (`fault_addr=0x20`).

## Fix

| Change | Purpose |
|--------|---------|
| `FindYoshiEggLayOwnerGobjFromSlot` / `ResolveYoshiEggLayParentGobj` | Resolve victim on apply, respawn, and capture backfill |
| Apply path | Re-parent egg-lay DObj to victim TopN joint; re-couple `captureyoshi.effect_gobj` |
| `ReconcileYoshiEggLayEffectsCore/Live/AtTick` | Ensure + prune + sanitize on load verify and live forward |
| `EnsureLiveYoshiEggLayEffects` | Respawn egg-lay shell on live/resim path when YoshiEgg has no valid effect |
| `ClearAllFightersEffectPointerIfMatch` | Decouple captureyoshi/guard pointers on orphan eject |
| `SanitizeCaptureYoshiEffectGobj` | Validate gobj id + DObj `child` before YoshiEgg interrupt |
| `ftCommonYoshiEggProcInterrupt` netmenu guard | Skip wiggle when egg DObj tree incomplete (rollback safety net) |
| `ReplayYoshiEggLayHatchCosmeticsFromSlot` | Replay explode/break particles on load when blob is post-escape Fall |
| Effect diag | `resolved_parent=` on apply; `yoshi_egg_lay_hatch_replay` lines |

## Verify

Yoshi neutral B egg lay → mash escape under rollback. Both peers should show hatch shell anim + shatter VFX every escape.

- `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1`: `resolved_parent=<victim gobj>` (not 0); prune/ensure lines during egg window.
- Load at escape tick: `yoshi_egg_lay_hatch_replay` when Fall + escape `vel_y` signature.
- No eff-only `LOAD_HASH_DRIFT` soft-continues with frozen egg effect hash during status 178.

Related: [netplay_yoshi_egg_lay_2026-06-01.md](netplay_yoshi_egg_lay_2026-06-01.md), [netplay_yoshi_egg_explode_particles_2026-06-01.md](netplay_yoshi_egg_explode_particles_2026-06-01.md).
