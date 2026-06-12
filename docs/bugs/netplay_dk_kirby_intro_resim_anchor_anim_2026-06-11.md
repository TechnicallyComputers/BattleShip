# Netplay: DK/Kirby intro resim anchor probe anim fail @230

**Date:** 2026-06-11  
**Status:** FIX SHIPPED (re-soak pending)  
**Evidence:** `soak3-linux.log` / `soak3-android.log` — DK (fkind=2) + Kirby (fkind=5), Yoshi's Story, `INJECT_TICK=230`, `FORCE_MISMATCH=1`.

## Symptom

Forced resim walkback @229→213: every anchor probe step reports `match_f=1 step_anim_fail=1` (aggregate anim hash mismatch) while post-load and fhash_light match. Session aborts with `anchor_probe_unresolved=1` and returns to CSS. Yoshi/Kirby soak1 @229→230 passes (`match_a=1`).

## Root cause

During anchor-probe +1 sim, Kirby Appear hidden-part joints hit `gcParseDObjAnimJoint UNHANDLED opcode=64` and terminate AObj playback. Fold fields (TopN, pos_diff, j0/transn anim_frame scalars) still match ring@probe per `intro_anchor_sim_trail`, but per-fighter `anim_hash` diverges (`anim_ok=0` on player 1 while `light_ok=1`).

`ReconcileAnchorProbeAppearSteadyFromProbeSlot` only hard-pinned fold contributors — sufficient when fold drifted but anim matched (Phase 28 Yoshi/Kirby class). Insufficient when +1 sim corrupts AObj chains while fold stays aligned (mixed DK-Wait + Kirby-Appear @230).

## Fix

In `syNetRbSnapshotReconcileAnchorProbeAppearSteadyFromProbeSlot`: after steady Appear +1 sim, reapply ring@probe joint AObj + modelparts (`syNetRbSnapReapplyFighterJointAnimFromBlob(..., TRUE)`) before terminal fold pin — same policy as mixed Appear+Wait Wait-peer reconcile.

## Verification

Re-run soak3 roster (DK/Kirby, inject @230). Expect:

```
RESIM_ANCHOR_PROBE load=229 probe_tick=230 ... match_f=1 match_a=1 step_anim_fail=0
resim complete epoch=1
```

No walkback to @213; no `anchor_probe_unresolved`.
