# Netplay intro entry facing — joint rotate quantize (2026-06-02)

**Date:** 2026-06-02  
**Scope:** `decomp/src/sys/objanim.c`, `port/net/sys/netplay_sim_quantize.c`, `port/net/sys/netrollbacksnapshot.c`  
**Status:** FIX SHIPPED — soak pending (cross-ISA intro / Wait shield pose)

## Symptoms

Cross-ISA netplay (Android aarch64 vs Linux x86_64): fighters face wrong directions during match intro (Entry animation through Wait / “3-2-1” shield pose). Aggregate `figh`/`anim` oracles can match while body yaw from anim joint **rotate** tracks diverges cross-ISA.

Soak signature (`netplay-session-trimmed-connection.log`): persistent `ring_save_player full_ok=0 anim_ok=0` during `status=5` Entry; `joint_translate_trigger` early; synctest LOAD_HASH presentational `anim`/`cam` drift at Wait.

## Root cause

1. **`gcPlayDObjAnimJoint`** quantized **translate** after track evaluation but not **rotate** or **scale** — entry/appear anims drive facing via `RotX/Y/Z` with cross-ISA libm/interp ULP noise.

2. **`syNetplayCanonicalizeFighterSimState`** quantized root rotate and joint translates but not child joint rotates during intro.

3. **Snapshot capture order** ran `syNetplayCanonicalizeFighterSimState` *after* joint/anim blob capture, so live full/anim hashes never matched slot blobs on ring save (diag noise, rollback load re-derives pose from divergent anim state).

## Fix

| Change | Purpose |
|--------|---------|
| `syNetplayQuantizeDObjAnimPose` (translate + rotate + scale) | Shared hook after anim-joint evaluation in `objanim.c` |
| `syNetplayCanonicalizeFighterIntroJointPose` gated on `game_status==Wait` or Entry/EntryNull/Wait status | Extra joint rotate/scale grid pass during intro |
| Move fighter canonicalize **before** joint/anim blob capture | Ring slot matches live full/anim at save time |

## Test plan

1. Cross-ISA soak (Kirby vs Yoshi or any pair): fighters face each other correctly at Wait; no sideways/backward entry poses.
2. `ring_save_player` during ticks 0–150: `full_ok=1` / `anim_ok=1` when quantize on (both peers).
3. Synctest load at first Wait tick: reduced presentational `anim`/`cam` LOAD_HASH drift vs pre-fix.

Related: [`netplay_cross_isa_determinism_2026-05-27.md`](netplay_cross_isa_determinism_2026-05-27.md).
