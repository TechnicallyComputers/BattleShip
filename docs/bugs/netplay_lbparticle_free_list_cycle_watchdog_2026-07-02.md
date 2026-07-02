# Netplay LBParticle transform free-list watchdog

**Date:** 2026-07-02  
**Status:** Fix implemented (`PORT && SSB64_NETMENU`, soak pending)  
**Area:** Netmenu rollback / LBParticle transform pool / transient effect verification

## Symptoms

After removing the former `transient_effect_probe` skip, a Fox Firefox collision soak hit a Linux watchdog hang:

- `frame` and `yield_count` stayed frozen while watchdog samples repeated.
- Backtraces alternated inside `lbParticleTransformIsOnFreeList` and `lbParticleTransformIsAllocated`.
- The final `SIGABRT` was watchdog escalation, not the original fault.

The log sequence immediately before the hang showed an unmasked `SYNCTEST_FAIL`, rollback effect enforcement, and a freshly allocated effect GObj:

```text
SSB64 NetRbSnapshot: slot_effect_enforce ... ejected=3 canonical=1 slot_count=1
SSB64 NetRollback: SYNCTEST_FAIL tick=756
SSB64: gobj_alloc ... link=6 ...
SSB64: WATCHDOG HANG ... frame=2434 yield_count=118701
```

## Root Cause

The netmenu-only effect `xf` safety checks walk the LBParticle transform free list and live particle/generator lists to reject stale `LBTransform` pointers before effect procs write through them.

Those walks had no cycle bound. Rollback particle reset and verify-only effect churn can expose malformed refcount/free-list state; if an `LBTransform` is pushed onto the free list more than once, its `next` chain can become circular. The next effect update then spins forever in the validation helper instead of ejecting the stale effect.

The removed skip did not cause the underlying corruption. It allowed the previously skipped transient-effect tick to run the full verify/enforce path, exposing the latent particle-pool bug.

## Fix

- Track the netmenu transform pool in `lbparticle.c` with side metadata instead of widening `LBTransform`.
- Mark transforms as free/live on `lbParticleEjectTransform` and `lbParticleGetTransform`.
- Reject double-free attempts before `proc_dead` can re-enter and before the transform is pushed onto the free list again.
- Bound all netmenu `LBTransform` validation walks by known pool sizes and log `SSB64 LBParticle: pool_guard ...` instead of hanging on a cyclic list.

All changes are compiled only under `PORT && SSB64_NETMENU`; offline builds keep the original particle-pool behavior.

## Verification

- Build `ssb64`.
- Re-soak Fox Firefox collision with rollback synctest enabled and no `transient_effect_probe` skip.
- Healthy behavior: no watchdog; if pool corruption is still triggered, logs should contain `SSB64 LBParticle: pool_guard tag=...` and the stale effect should be ejected instead of pinning the game thread.
