# Hit detect non-mutating quantize + Zebes acid grid (2026-05-30)

**Date:** 2026-05-30  
**Status:** Fix shipped (Zebes / Link dair cross-ISA soak pending)  
**Area:** `port/net/sys/netplay_sim_quantize.c`, `decomp/src/gm/gmcollision.c`, `decomp/src/gr/grcommon/grzebes.c`, `decomp/src/ft/ftmain.c`, `port/net/sys/netrollbacksnapshot.c`

## Symptoms (phase 2 regression)

Cross-ISA Zebes soak after phase 2 hit quantize:

- Fighters sometimes **knock back from acid before sprites touch the surface** (acid uses fighter root Y; phase 2 snapped all fighter roots in-place before `ftMainSearchGroundHit`).
- Link down-air **still intermittently misses** DK bounce; `figh` LOAD_HASH_DRIFT continues under `LOAD_HASH_SOFT=1`.

## Root cause

1. **`syNetplayCanonicalizeHitDetectSceneOnce`** mutated live `DObj->translate` for every fighter before ground hazard checks — physics had already run with unquantized roots; acid comparison then used **quantized fighter Y vs raw acid Y** → early acid triggers.
2. In-place quantize of **`attack_coll->size`**, **`parts->vec_scale`**, and persistent **damage coll** fields inflated/deflated boolean hit tests without matching offline geometry.
3. Zebes **`acid_level_curr` / `acid_level_step`** were not on the shared grid in live rise or snapshot ground blob.

## Fix

| Change | Purpose |
|--------|---------|
| Remove scene-wide in-place root/attack pass before hit search | Acid and sim pose stay aligned for the frame |
| `syNetplayQuantizeVec3fInto` + scratch compares in `gmCollisionCheckFighterInFighterRange` / `gmCollisionCheckFighterAttackDamageCollide` | Grid-aligned fighter–fighter tests without mutating sim state |
| Drop `size` / `vec_scale` quantize; damage offset/size only in scratch at test time | Avoid inflated hurtboxes |
| `grZebesAcidCheckGetDamageKind` + rise update: quantize fighter Y, acid surface Y, `acid_level_*` | Symmetric acid gate cross-ISA |
| Snapshot Zebes ground + attack coll **positions only** on grid | Rollback acid height matches peers |

## Verification

- Zebes cross-ISA: acid damage only when sprite roots cross surface; no early lava knockback from quantize snap.
- Link dair vs DK: bounce via `ftCommonAttackAirLwProcHit` more consistent; bisect `SSB64_NETPLAY_SIM_F32_QUANTIZE=0` if needed.
- Same-ISA / offline: unchanged when quantize inactive.

## Related

- [netplay_link_dair_hit_detect_quantize_2026-05-30.md](netplay_link_dair_hit_detect_quantize_2026-05-30.md)
- [netplay_cross_isa_determinism_2026-05-27.md](netplay_cross_isa_determinism_2026-05-27.md)
