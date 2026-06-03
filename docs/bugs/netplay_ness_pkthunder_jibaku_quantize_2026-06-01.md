# Netplay — Ness PK Thunder jibaku cross-ISA quantize (2026-06-01)

**Date:** 2026-06-01  
**Status:** Fix shipped (soak pending)  
**Area:** `port/net/sys/netplay_sim_quantize.c`, `decomp/src/ft/ftchar/ftness/ftnessspecialhi.c`, `port/net/sys/netrollbacksnapshot.c`

## Symptom

Cross-ISA (Android aarch64 vs Linux x86_64) upward air jibaku: ground snap fixed, but one peer completed jibaku normally while the other hit frame-commit **figh/rng** diverge @600, FC recovery storm, session stop. Android-only `jibaku_stall_tick` on status 236 treated as catch-up misfire; assumed **symptom** of prior sim drift (stale proc / frozen `anim_length`), not root cause.

## Root cause

Jibaku sim uses raw libm (`__cosf`/`__sinf`/`syUtilsArcTan2`) on `pkjibaku_angle`, `vel_air`, and model pitch joint rotation. Global fighter physics quantize (`syNetplayCanonicalizeFighterSimState`) does not cover Ness **specialhi** status vars (`pkjibaku_angle`, `pkthunder_pos`) or the per-tick jibaku deceleration path. Cross-ISA ULP drift accumulates through the jibaku arc → divergent fighter/rng hashes despite matched inputs.

## Fix

| Layer | Change |
|-------|--------|
| **Scope** | `syNetplayFighterInNessPKJibakuSimScope`: statuses 231 / 236 / 235 (bound) |
| **Live sim** | `syNetplayCanonicalizeNessPKJibakuSimState` after air/ground jibaku ProcPhysics, wall collide, UpdateModelPitch |
| **Launch** | `syNetplayCanonicalizeNessPKJibakuLaunchState` after air/ground SetStatus — quantize angle, re-derive launch `vel_air` from grid angle |
| **Rollback** | `syNetplayQuantizeNessPKJibakuStatusVars` on fighter blob capture/apply; wired into `syNetplayCanonicalizeFighterSimState` |

## Verification

1. Cross-ISA Ness upward jibaku: no `FRAME_COMMIT_STATE_DIVERGE` through validation 600+.
2. Both peers stay status **236** for full arc; no asymmetric early 234 exit unless vanilla timer expires on both.
3. Optional bisect: `SSB64_NETPLAY_SIM_F32_QUANTIZE=0` on both peers should reproduce drift (not for production).

## Related

- [`netplay_ness_pkthunder_orphan_weapons_crash_2026-06-01.md`](netplay_ness_pkthunder_orphan_weapons_crash_2026-06-01.md)
- [`netplay_link_dair_hit_detect_quantize_2026-05-30.md`](netplay_link_dair_hit_detect_quantize_2026-05-30.md)
- [`netplay_hyrule_twister_rollback_2026-05-29.md`](netplay_hyrule_twister_rollback_2026-05-29.md)
