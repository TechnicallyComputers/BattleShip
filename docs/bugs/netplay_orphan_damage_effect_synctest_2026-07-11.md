# Orphan NULL-parent damage VFX — SYNCTEST_FAIL eff — 2026-07-11

**Status:** **FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)**

## Symptom

soak2 session `362259664` / seed `1994546352` (same pair as cliff-floor FC @600):

- `SYNCTEST_FAIL` / `LOAD_HASH_DRIFT diverged=eff` @749 on both peers.
- Capture: `effect_count=1`, `gobj_id=1011`, **`respawn=0`**, **`parent_id=0`**, rising `anim_frame` / incidental `quake_pri` union alias.
- Verify: `count=0` → empty hash `0x811C9DC5` vs capture `0xB635ED46`.
- Context: Kirby `AttackAirLw` (213) hit Captain into damage (`DamageN3` → `DamageFlyLw`); effect first folded @702.

## Root cause

Hit presentation shells (DamageFly orbs/sparks class and siblings) with **`fighter_gobj == NULL`** and **`respawn=NONE`** have no mint path. They were folded into the rollback eff hash while `anim_frame > 0`, then ejected as `verify_non_canonical` on synctest load — Link-spin / DefaultProc particle class.

Prior gate only hid **`efManagerHaveStructProcUpdate`** orphans (DeadExplode). This shell is a different proc with the same parent/respawn shape, so it still folded.

## Fix

In `syNetRbSnapEffectHiddenFromRollback` and `syNetRbSnapLiveEffectExcludedFromRollbackHash`: exclude any live effect with `fighter_gobj == NULL` and `syNetRbSnapEffectRespawnKindFromLive == NONE`. ImpactWave/Quake keep non-NONE respawn kinds and stay on their existing paths.

## Related

- [`netplay_cliff_floor_pass_harden_fc_drift_2026-07-11.md`](netplay_cliff_floor_pass_harden_fc_drift_2026-07-11.md) — FC `figh` @600 / second resim.
- [`netplay_dead_explode_stamped_quake_eff_rng_2026-07-11.md`](netplay_dead_explode_stamped_quake_eff_rng_2026-07-11.md) — HaveStruct-only predecessor.
- [`netplay_link_spin_attack_effect_synctest_2026-07-06.md`](netplay_link_spin_attack_effect_synctest_2026-07-06.md) — fold-without-mint pattern.
