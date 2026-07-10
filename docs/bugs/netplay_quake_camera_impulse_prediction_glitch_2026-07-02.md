# Netplay: quake camera impulse prediction glitch

**Date:** 2026-07-02  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Scope:** `decomp/src/ef/efmanager.c`

## Symptom

Firefox/bumper soak `387489795` passed deterministic checks after the lost effect NULL guards were
restored:

```
LOAD_HASH_DRIFT: 0
frame-commit cross-peer diverge: 0
RESULT: PASS
```

The remaining bug was visual: the camera still popped/glitched when Firefox spawned quake effects.
The sync report still showed a soft `sim_state` diagnostic mismatch around tick 522 in `item,eff`,
but no authoritative rollback/hash partition failed.

## Root Cause

`efManagerQuakeProcUpdate` advances the quake effect normally, then applies a presentation-only camera
impulse with `gmCameraSetVelAt(&pos)`. The camera integrates that field in `gmCameraApplyVel`, which
adds `gGMCameraStruct.vel_at` to `cobj->vec.at` and immediately zeros it.

Rollback snapshots restore the camera CObj and `GMCamera` state, and the authoritative hashes intentionally
do not treat the transient camera shake as gameplay. During predicted input frames and forward resim replay,
that means a mispredicted Firefox quake can visibly shake the rendered camera, then rollback restores and
replays the corrected presentation, causing a pop even though the final sim state is deterministic.

## Fix

In netmenu builds only, suppress the quake camera velocity write when the current tick is speculative:

- `syNetRollbackIsResimulating() != FALSE`
- `syNetInputSimTickUsedPredictedRemote(syNetInputGetTick()) != FALSE`

The quake effect, animation, priority, lifetime, and snapshot identity still advance normally. Only the
presentation-side `gmCameraSetVelAt` impulse is skipped for frames that may be visually undone by rollback.
`SSB64_NETPLAY_QUAKE_CAMERA_DIAG=1` keeps logging suppressed impulses after the default bounded log budget.

## Verify

Re-run the Firefox/bumper quake soak. Expected result: deterministic report remains a pass, with no SIGSEGV
or load drift, and the visible camera pop during Firefox quakes should be gone or substantially reduced.
