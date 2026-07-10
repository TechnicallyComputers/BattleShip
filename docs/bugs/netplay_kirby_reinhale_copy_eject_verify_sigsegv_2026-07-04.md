# Netplay Kirby re-inhale after copy eject verify SIGSEGV — 2026-07-04

**Date:** 2026-07-04  
**Sessions:** `1836363854` (probe 2429), **`139215096`** (probe 1709, post-round-1 fix)  
**Status:** FIX IMPLEMENTED (soak pending)

## Symptom

Both peers `SIGSEGV fault_addr=0xa` on synctest verify immediately after `prepare_verify`. Gameplay: Kirby inhale → copy Link → taunt eject → re-inhale → hold CopyLink (status 277).

| Session | Last `SYNCTEST_OK` | Crash probe | Kirby at crash |
|---------|-------------------|-------------|----------------|
| 1836363854 | tick 2309 | 2429 | status 277 |
| 139215096 | tick 1589 | **1709** | status 277 |

Last log lines (both peers, identical):

```
prepare_verify tick=<probe> player=1 fkind=8 status=277 motion=251
effect save … gobj_id=1011 (inhale wind bank=0 and/or shield bubble bank=10)
!!!! CRASH SIGSEGV fault_addr=0xa
```

No `LOAD_HASH_DRIFT` or FC failure before crash.

## Root cause

Repeated inhale/copy/eject cycles recycle **`gobj_id=1011`** between Kirby inhale-wind shells (hidden/excluded) and **residual shield bubbles** (`shield=55`, `bank=10`, folded as `effect_count=1`). Synctest verify calls `syNetRollbackVerifyLoadedSlot` → `syNetRbSnapshotFinalizeVerifyEffectState`.

Round-1 fix (`syNetRbSnapEjectHiddenCosmeticEffectShellsForVerify`) only swept **hidden/excluded** cosmetics and still routed shells with superficially valid `ep->xf` through bare `syNetRbSnapEjectGObj` → `lbParticleFindStructForEffectGobj`. That pool walk dereferenced **unrelated** stale `pc->xf` pointers (`fault_addr=0xa`). Authoritative bank=10 shield shells on recycled 1011 were never swept and hit the same path during enforce/eject.

## Fix

1. **`decomp/src/lb/lbparticle.c`** — `lbParticleFindStructForEffectGobj`: only dereference `pc->xf->effect_gobj` when `lbParticleTransformIsAllocated(pc->xf)`.

2. **`decomp/src/ef/efmanager.c`** + **`.h`** — export `efManagerNetplayEffectXfIsLive` for rollback eject guards.

3. **`port/net/sys/netrollbacksnapshot.c`**:
   - **`syNetRbSnapEjectGObj`** — Kirby inhale-wind delegate; when `ep->xf` is not live, `efManagerNetSafeFreeStruct` + `gcEjectGObj` instead of particle destroy; hardened find/destroy only on live coupling.
   - **`syNetRbSnapEjectStaleParticleCouplingEffectsForVerify`** — verify-only sweep of **all** link-6/8 shells with stale xf (including slot-authoritative bank=10 on 1011) before enforce.
   - **`syNetRbSnapEjectHiddenCosmeticEffectShellForVerify`** — route through hardened `syNetRbSnapEjectGObj` after fighter attach cleanup.
   - **`syNetRbSnapshotFinalizeVerifyEffectStateInternal`** — call stale-coupling sweep after hidden-cosmetic sweep.

Related: `docs/bugs/netplay_kirby_inhale_wind_synctest_verify_sigsegv_2026-07-04.md` (pre-apply inhale eject + orphan guard).

## Verify

- `cmake --build build-netmenu --target ssb64 -j 4`
- Re-soak session `139215096` path: inhale → copy → taunt eject → re-inhale → synctest probes past tick **1709** with zero `SIGSEGV` on both peers.
