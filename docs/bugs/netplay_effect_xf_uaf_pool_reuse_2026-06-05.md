# Netplay effect LBTransform UAF (pool reuse false negative)

**Date:** 2026-06-05  
**Status:** Fix shipped (soak pending)  
**Area:** Netmenu rollback / particle effects (`efManagerDefaultProcUpdate`)

## Symptoms

- SIGSEGV in `efManagerDefaultProcUpdate+0x2b` during netplay soak (Sector Z, shield spam, fireball testing).
- `fault_addr` in guest heap / Gfx DL range; crash handler may report STALE-DL diag.
- Prior xf guard logged `effect_xf_stale reason=owner_mismatch` in some sessions; latest soak had **zero** stale lines before crash → validation false negative.

## Root cause

1. **Transform pool reuse without clearing `effect_gobj`.** `lbParticleGetTransform()` recycled `LBTransform` structs from the free list but did not clear `effect_gobj`. A new particle could inherit a stale owner pointer while an old effect GObj still held the same `xf` address in `effect_vars.common.xf` and passed coupling checks (`xf->effect_gobj == gobj`, `pc->xf == xf`).

2. **Insufficient validity checks.** `efManagerNetplayEffectXfIsLive` only checked pointer coupling, not whether `xf` was still allocated (not on the transform free list, referenced by a live particle/generator, `users_num > 0`).

3. **Rollback particle reset gap.** `syNetRbSnapResetParticlesForRollback()` ejected all particles/transforms but live effect GObjs could retain stale `xf` pointers and `DefaultProcUpdate` procs until snapshot apply nulled them.

## Fix

| Layer | Change |
|-------|--------|
| `lbparticle.c` | Clear `effect_gobj` on transform alloc (`GetTransform`) and eject (`EjectTransform`); add `lbParticleTransformIsOnFreeList` / `lbParticleTransformIsAllocated`. |
| `efmanager.c` | Extend `efManagerNetplayEffectXfIsLive`; always-on rate-limited `effect_xf_stale` log; pre-write re-check in DefaultProcUpdate / dust procs. |
| `netrollbacksnapshot.c` | `syNetRbSnapStripEffectXfCouplingAfterParticleReset()` after particle wipe; orphan shield `no_fighter` ends proc + xf procs before eject; log uses `syNetInputGetTick()`. |

## Verification

- Rebuild netmenu binary / AppImage; soak with fireball + shield spam past tick ~1990.
- Trim log should show `effect_xf_stale` with reasons like `xf_on_free_list` / `xf_not_allocated` if guard fires, not SIGSEGV.
- `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1` still adds verbose fields (`free=`, `alloc=`, `users=`).
