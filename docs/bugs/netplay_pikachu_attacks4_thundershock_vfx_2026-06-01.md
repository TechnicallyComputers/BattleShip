# Netplay: Pikachu forward smash ThunderShock VFX missing after rollback

**Date:** 2026-06-01  
**Status:** FIX SHIPPED (soak pending)

## Symptom

During netplay, Pikachu forward smash (`nFTCommonStatusAttackS4*`) sometimes shows no extending electric shock VFX while hitboxes still connect.

## Root cause

Forward smash hitboxes come from motion-script `MakeAttackColl` and are independent of the ThunderShock effect spawned in `ftCommonAttackS4ProcUpdate` when motion flags fire.

Rollback/snapshot paths were missing ThunderShock coupling:

1. `syNetRbSnapEffectRespawnKindFromLive` returned `RESPAWN_NONE` for ThunderShock (unlike Ness PK wave / Fox reflector).
2. `syNetRbSnapPruneOrphanFighterAttachedEffects` ejected short-lived fighter-attached shocks not whitelisted in the effect ring.
3. `syNetRbSnapFinalizeFighterEffectAttachFlags` cleared `is_effect_attach` with no Pikachu AttackS4 keep path.
4. Periodic synctest could probe mid f-smash (`effect_count`/`eff` hash drift at status 204) without defer.

## Fix

In `port/net/sys/netrollbacksnapshot.c`:

- Added `SYNETRB_EFFECT_RESPAWN_PIKACHU_THUNDER_SHOCK` with live detection (`efManagerHaveStructProcUpdate` + TopN joint parent + child offset DObj).
- Capture `attack4.gfx_id` in effect blob `quake_magnitude` for variant-correct respawn.
- `syNetRbSnapEnsurePikachuThunderShockEffectsFromSlot` / `syNetRbSnapPruneStalePikachuThunderShockEffects` on apply and finalize.
- Extended `FinalizeFighterEffectAttachFlags` keep path for AttackS4 attach pending / live shock.
- Synctest defer reason `pikachu_attacks4` while any Pikachu is in AttackS4 status range.
- Synth respawn when attack colliders or motion shock flags are active but no live ThunderShock remains after load.

## Verification

- Build: `cmake --build build --target ssb64 -j 4` (pass).
- Manual: Pikachu vs CPU/netplay, spam forward smash through rollback/synctest windows; shock arc should track hitbox frames.
