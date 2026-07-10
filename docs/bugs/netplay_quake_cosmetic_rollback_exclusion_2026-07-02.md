# Netplay: quakes excluded from rollback hash + snapshot (cosmetic)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)
**Date:** 2026-07-02
**Builds:** `build-netmenu` + `build-offline` link clean.

## Symptom

Soak2 session `736861014` (host=`soak2-android.log`, guest=`soak2-linux.log`):

- Guest deterministic eff-only `SYNCTEST_FAIL` / `LOAD_HASH_DRIFT` at **tick 1949** (`eff=0xA14BB28D/0x9AC0E7AA`,
  all other partitions identical).
- Host `SIGSEGV` at tick 1950 (`fault_addr=0xc8`, `x0=0x0`, empty backtrace).
- Separately, a `FRAME_COMMIT_STATE_DIVERGE` [item,rng] with matching inputs at tick 600 — the known
  Castle bumper cross-ISA float issue, tracked elsewhere, **not** addressed here.

## Root cause

Same class as `netplay_impact_wave_cosmetic_rollback_exclusion`, but for quakes.

At tick 1949 the guest live-fold captured **two distinct live quakes both sharing recycled
`gobj_id=1011`**, both `respawn=1` (QUAKE), both `priority=1`, at different positions/anim frames:

```
capture tick=1949 count=2
  idx0 gobj_id=1011 respawn=1 anim=0x41880000 quake_pri=1 pos=(0,0x40C9C180,0x4114FE10)
  idx1 gobj_id=1011 respawn=1 anim=0x41600000 quake_pri=1 pos=(0,0xC2546DF8,0xC1DE21B8)
```

(Castle bumpers bouncing spawn a quake per hit; two hits the same tick mint two quakes that both take
the recycled pool id 1011.)

The id-keyed snapshot cannot canonicalize two effects with the same `gobj_id`. Worse, both have the
**same priority**, so `syNetRbSnapLiveEffectMatchesBlob`'s priority disambiguation can't tell them
apart. Verify collapsed 2 → 1 and the survivor's priority round-tripped `1 → 0`:

```
slot_effect_enforce tick=1949 ejected=5 canonical=1 slot_count=2
quake_surplus_prune  tick=1949 ejected=2 slot_count=2
verify tick=1949 count=1  idx0 gobj_id=1011 respawn=1 quake_pri=0   <-- was pri=1
```

Two verify passes even produced two different hashes (`0x9AC0E7AA`, then `0xA14BB28D` after a second
enforce/`quake_unmatched_prune`) — non-idempotent save/load, i.e. a local eff `SYNCTEST_FAIL` on the
guest (not a cross-peer input divergence).

On the host, the same tick's repeated respawn/enforce of recycled id=1011 (log shows a burst of
`gobj_alloc ... id=1011 link=6` from several callers) stranded a proc that dereferenced NULL at
`+0xc8` → `SIGSEGV` (`x0=0x0`).

## Why exclusion is the right fix

`efManagerQuakeProcUpdate`'s only side effect is `gmCameraSetVelAt(&pos)` — a camera-shake velocity
impulse computed from the quake DObj translate and the current camera eye/at. It touches no
fighter/item/world/rng state. The shell carries only `effect_vars.quake.priority` (`3 - magnitude`),
derived from the collision that spawned it. The camera impulse is **already** suppressed on
speculative/resim frames (`efManagerNetplayShouldSuppressQuakeCameraImpulse`, see
`netplay_quake_camera_impulse_prediction_glitch`).

So quakes are cosmetic:

- The upstream cause (bumper/item collision magnitude) is already hashed in item/rng/world, so folding
  the derived quake priority into the eff hash adds no real cross-peer coverage.
- Both peers spawn quakes deterministically in forward sim, so the camera partition stays aligned
  without snapshot round-tripping the quake shells.
- Excluding them from the snapshot means rollback never respawns/canonicalizes/prunes recycled-id
  quakes, eliminating both the non-idempotent verify and the id-1011 churn crash.

This mirrors the Firefox ImpactWave (`netplay_impact_wave_cosmetic_rollback_exclusion`) and
Yoshi-egg-hatch cosmetic precedents.

## Fix

`port/net/sys/netrollbacksnapshot.c` — `syNetRbSnapEffectHiddenFromRollback` now returns TRUE for
genuine live quakes, in addition to the Yoshi-egg-hatch and ImpactWave cosmetics. This single
chokepoint feeds `syNetRbEnumerateActiveEffectsSorted` (the eff hash fold in `netsync.c`) and every
capture/enforce/eject/match pass, so quakes drop out of the entire rollback effect pipeline
consistently.

The hide test uses only **reliable proc-identity** checks:

- `efManagerQuakeProcUpdate` on the GObj process list, or
- `ep->proc_update == efManagerQuakeProcUpdate`, or
- `gobj->func_run == efManagerQuakeFuncRun` (freshly minted, proc not yet bound).

It deliberately does **not** use `syNetRbSnapLiveEffectIsQuake`'s "stamped/frozen" heuristic branch,
which reads the `effect_vars.quake.priority` union byte for any `fighter_gobj==NULL / anim_frame>0 /
func_run==NULL` effect. That branch only existed to keep snapshot-restored quakes classified; since
quakes are no longer restored/stamped, live quakes always carry a genuine quake proc, and using the
heuristic for a hide decision would risk excluding an unrelated cosmetic VFX whose union byte aliases
`<= 3`.

The existing quake canonicalization machinery (same-id coexist, priority match, surplus/unmatched
prune, ensure/respawn) is now dormant for quakes — left in place (like the ImpactWave path) to keep
the change minimal; a follow-up can remove it once this soak confirms determinism.

The camera-impulse suppression in `efManagerQuakeProcUpdate` is unchanged and still governs the
presentation shake.

## Not addressed

- Tick 600 `FRAME_COMMIT_STATE_DIVERGE` [item,rng] with matching inputs — separate Castle bumper
  cross-ISA float determinism issue.

## Verify

- `build-netmenu` `ssb64` target: links clean.
- `build-offline` `ssb64` target: links clean (change is `SSB64_NETMENU`-gated).
- Soak pending.
