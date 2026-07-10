# Netplay: Castle bumper rollback apply misses animation phase

**Date:** 2026-07-03
**Scope:** `port/net/sys/netrollbacksnapshot.c`. `PORT && SSB64_NETMENU`, active VS/rollback only.
**Status:** FIX IMPLEMENTED (soak pending).
**Class:** snapshot-apply idempotence gap for an animation-driven stage hazard.

## Symptom

Android/Linux cross-ISA soak (`session=863608658`, Peach's Castle bumper, FORCE_MISMATCH on)
no longer crashes or synctest-drifts, but frame-commit validation still fails with matching inputs:

```
FRAME_COMMIT_STATE_DIVERGE validation=600 diverged=item,rng inputs=MATCH
FRAME_COMMIT_STATE_DIVERGE validation=1440 diverged=figh,item inputs=MATCH
```

Both failures fold the same live item, the Castle bumper (`gobj_id=1013`, `kind=gbumper`). The printed
bumper fold fields (`scale`, `palette_id`, `multi`, `hit_anim_length`) match cross-peer, but the item
hash differs and RNG/status diverge downstream when a bumper collision branch fires on only one peer.

## Root cause

The earlier resim canonicalize fix made the replay path grid-snap item/fighter state before saving, and
the `hit_anim_length` fold fix removed the dead free-running counter from the item hash. The remaining
non-idempotence was the bumper's driving DObj animation cursor.

The item blob already restored the bumper's position, hit-scale, and palette flash state, but the live
singleton was preserved across load and its `DObj` animation cursor (`anim_wait`, `anim_speed`,
`anim_frame`, AObj chain, and event pointer) kept whatever phase the follower had before applying the
slot. Peach's Castle bumper motion is animation-driven with zero item velocity, so restoring the slot's
position without restoring the cursor made the next replay tick advance from a different phase. The item
hash then stayed offset until a one-sided bumper contact split `rng` or fighter status.

## Fix

The internal item snapshot blob now carries a `SYNetRbSnapDObjAnimBlob` for GBumper presentation. Capture
stores the bumper root DObj cursor through the same helper used by fighters/weapons/yakumono. Apply
restores that cursor, reapplies the anim-joint pose at the captured frame, then reapplies the existing
scale/palette presentation values so hit-flash visuals and folded fields remain unchanged.

This makes a rollback load of the live Castle bumper idempotent: the singleton resumes from the slot's
animation phase instead of the follower's pre-load phase.

## 2026-07-03 hardening (same day, pre-soak)

Three robustness additions on top of the DObj cursor restore:

1. **MObj material-anim phase** — new `SYNetRbSnapMObjAnimBlob` (`anim_wait`/`anim_speed`/`anim_frame`,
   matanim event cursor, AObj chain) captured and restored for the GBumper root `MObj` via
   `syNetRbSnapCaptureMObjAnim` / `syNetRbSnapApplyMObjAnim` (same rebuild-in-capture-order policy as
   the DObj path). Without this, `gcPlayMObjMatAnim` advances palette/texture from the follower's
   pre-load material phase even when the DObj cursor is restored.
2. **Translate re-pin after pose re-seat** — `gcApplyDObjAnimJointPoseAtFrame` can write translate
   tracks from the anim stream; the GBumper presentation apply now re-pins `dobj->translate.vec.f`
   to the exact captured `blob->translate` (then grid-snaps) so the apply reproduces the slot's
   folded position bit-exactly.
3. **Apply-idempotence probe** — `gbumper_apply_probe` log line (gated on the existing item snapshot
   diag env) emitted after every GBumper blob apply: raw bits of live translate/scale/palette and
   DObj anim cursor plus `multi`/`hit_anim_length`/`attack_state` and the blob's captured cursor and
   AObj chain counts. Diff host vs guest at the load tick to name any remaining non-idempotent field
   in one soak.

## Soak procedure

Re-run the Android/Linux Castle pair with FORCE_MISMATCH enabled. The expected result is no
`FRAME_COMMIT_STATE_DIVERGE` at validation 600 or 1440, and no one-sided RNG advance from the bumper
window.

## Related

- [`netplay_castle_bumper_resim_uncanonicalized_drift`](netplay_castle_bumper_resim_uncanonicalized_drift_2026-07-02.md)
- [`netplay_castle_bumper_hit_anim_length_free_run_fold`](netplay_castle_bumper_hit_anim_length_free_run_fold_2026-07-02.md)
- [`netplay_peach_castle_bumper_rollback`](netplay_peach_castle_bumper_rollback_2026-05-30.md)
