# netplay ImpactWave misclassified as quake (Firefox VFX no animation, camera stuck, crash)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`), soak pending.

## Symptom

After the `netplay_quake_pri4_magnitude_sentinel_roundtrip_2026-07-02` fix, soak session
`107796656` became deterministic (`RESULT: PASS`, no `LOAD_HASH_DRIFT`, no `SYNCTEST_FAIL`), but
Firefox presentation regressed:

- Firefox impact-wave animation did not play.
- Camera eventually stopped following fighters.
- The game crashed shortly afterward.

The determinism fix had changed the restore path for the `pri=4` "quake" shell from
`efManagerQuakeMakeEffect(0)` to `efManagerQuakeMakeEffect(-1)`.

## Root Cause

The `pri=4` shell is not a quake. It is the Firefox landing/collision ImpactWave:

```c
effect = efManagerImpactWaveMakeEffect(&pos, 4, ...);
```

`EFStruct.effect_vars` is a union. `EFCommonEffectVarsImpactWave.index` and
`EFCommonEffectVarsQuake.priority` both live at offset 0:

```c
typedef struct EFCommonEffectVarsImpactWave
{
    u8 index;
    f32 alpha;
    f32 decay;
} EFCommonEffectVarsImpactWave;

typedef struct EFCommonEffectVarsQuake
{
    u8 priority;
} EFCommonEffectVarsQuake;
```

So an ImpactWave with `index=4` reads as `quake.priority == 4`. Earlier snapshot code widened quake
classification to `priority <= 4` to preserve this shell in effect hashing, accidentally treating an
ImpactWave as a quake.

The first round-trip fix sign-extended `quake_magnitude=0xFF` and called
`efManagerQuakeMakeEffect(-1)`. That makes vanilla quake setup compute `priority = 3 - (-1) = 4`,
which fixes the hash, but magnitude `-1` hits the quake factory's `default` switch arm and **does not
add a quake anim joint**. The restored shell therefore has no normal animation/lifetime. It can remain
live, keep binding/running quake camera update code, and eventually exhaust the finite effect pool.

## Fix

Give ImpactWave its own snapshot identity instead of pretending it is a quake:

- Add `SYNETRB_EFFECT_RESPAWN_IMPACT_WAVE`.
- Classify `efManagerImpactWaveProcUpdate` effects as ImpactWave before quake classification.
- Restrict frozen quake classification back to real quakes (`priority <= 3`).
- Respawn ImpactWave blobs with `efManagerImpactWaveMakeEffect(&blob->translate, index, blob->rotate.z)`.
- Match ImpactWave blobs by real proc + `impact_wave.index`, then let `ApplyEffectBlobToGObj` stamp
  `impact_wave.alpha`, `impact_wave.decay`, `anim_frame`, translate, and rotate from the blob.
- Hash ImpactWave state as ImpactWave state (`index`, `alpha`, `decay`, plus existing anim/position
  fold), not as `quake.priority`.
- Allow same recycled `gobj_id` coexistence between `RESPAWN_IMPACT_WAVE` and `RESPAWN_QUAKE`, which
  covers the observed Firefox ImpactWave + genuine camera quake pair at `gobj_id=1011`.

The bad sign-extension quake restore is removed; real quake restore only accepts vanilla magnitudes
0..3. ImpactWave now gets the correct proc, animation, lifecycle, and visual orientation while still
round-tripping deterministically.

## Files

- `port/net/sys/netrollbacksnapshot.c`
  - Adds `SYNETRB_EFFECT_RESPAWN_IMPACT_WAVE`.
  - Adds ImpactWave predicate / blob index helper / respawn path.
  - Captures and reapplies effect DObj `rotate`.
  - Removes `priority <= 4` quake classification and the bad `efManagerQuakeMakeEffect(-1)` path.
- `port/net/sys/netsync.c`
  - Folds `impact_wave.index`, `impact_wave.alpha`, and `impact_wave.decay` for ImpactWave effects.

## Related

- Supersedes `netplay_quake_pri4_magnitude_sentinel_roundtrip_2026-07-02.md`.
- Completes the root fix for the earlier camera-proc misbind tracked by
  `netplay_quake_pri4_shell_camera_proc_misbind_2026-07-02.md`.
