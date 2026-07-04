# Fox TransN Hidden-Part Leak After Verify Emergency Restore (Cosmetic Model Distortion)

**Date:** 2026-07-03
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`), soak pending
**Session:** `331358167` (soak2-android / soak2-linux, `RESULT: PASS`)

## Symptom

Deterministic soak passes cleanly (no drift, no synctest failures), but the user observes a rare
**presentational glitch on Fox's model** — hitboxes and gameplay unaffected — after colliding with the
Peach's Castle bumper at certain angles. Not reliably reproducible by hand.

## Evidence

The session's single resim window (tick 1469–1470) is exactly a FireFox-into-bumper interrupt:
predicted timeline had Fox (player 1, `fkind=1`) in FireFox (`status=234 motion=209`); the corrected
timeline bounced him into damage-fly (`status=58 motion=51`). During the verify load both peers log:

```
SSB64: ftMainUpdateHiddenPartID OK fkind=1 hpid=1 joint=1 parent=0 kind=3
SSB64 NetRbSnapshot: intro_joint_presence player=1 fkind=1 hpid=1 root=1 action=materialize now_present=1
```

(linux line ~24081, android ~18793 — byte-identical on both peers, hence still deterministic).

That is `syNetRbSnapReconcileFighterJointPresenceFromBlob` materializing Fox's **TransN hidden part**
(`root_joint_id=1`, `joint_kind=3` — the variant that reparents the entire skeleton under the new
joint) against the verify-tick blob, whose timeline legitimately owned it.

No matching `action=eject` ever follows. The `figatree-bind` trail proves the joint leaked
permanently:

- Pre-resim, Fox's tree walks 26 joints in normal statuses (`motion=14 status=20 walked=26`).
- Post-resim, the *same* motions walk 27 for the rest of the match (`walked=27` on motions 4/14/16/44/50/52…),
  and a later FireFox stacks to `walked=28`.

## Root cause

Two layers:

1. **Load-path gap.** After a synctest verify, `syNetRbSnapshotRestoreLiveEmergency` puts live back
   from `sSYNetRbEmergencySlot` (the corrected timeline stashed before the verify load). Outside
   intro-load-fidelity scope it ran **no joint-presence reconcile at all** — unlike the verify load
   itself (`syNetRbSnapRefreshFigatreePresentationFromSlot` → `syNetRbSnapVsLoadJointFidelityRepairFromSlot`)
   and the resim load (`syNetRbSnapshotResyncLiveFightersFromSlotForSim`). The blob-apply-time
   differential reconcile (`syNetRbSnapReconcileFighterHiddenPartsFromAnimDesc` in
   `syNetRbSnapApplyFighter`) is intro-scope-gated, so mid-match VS applies just memcpy
   `fp->anim_desc` over live. A hidden-part root materialized for the verify timeline therefore
   survives the emergency restore.

2. **Vanilla can never self-heal.** `ftMainSetStatus`'s hidden-part loop is *differential*: it ejects
   only on an anim_desc set→clear edge (`ftmain.c` ~5195). The emergency apply restored the corrected
   timeline's `anim_desc` with the hpid=1 bit **already clear**, so every subsequent status change
   sees clear→clear and never ejects. The leak is permanent.

## Why it presents as "model wrong, hitbox fine"

Figatree binds animation tracks by walking the DObj tree in order. One extra passthrough joint shifts
the joint→track mapping for everything after it (`walked=27` against a table authored for 26), so
joints receive their neighbor's channels — a distorted model. Gameplay (position, status logic,
hitboxes) doesn't consume the tree walk order, and both peers leak identically, so hashes stay green.

Rarity matches too: it needs a rollback resim window to land exactly on the frame where the bumper
cancels FireFox (Fox's mid-air recovery, the only common Fox state that owns this hidden part) —
i.e. specific collision angles/timings.

## Fix

In `syNetRbSnapshotRestoreLiveEmergency`, mirror the other VS load tails: when not in
intro-load-fidelity scope, run `syNetRbSnapVsLoadJointFidelityRepairFromSlot(&sSYNetRbEmergencySlot)`
after finalize/rebind. The emergency blob's `joint_is_valid[]` (captured from the corrected live
timeline) is the topology ground truth; the repair ejects the leaked hidden-part root, re-pins blob
joint anim, and hard-pins fold contributors — same sequence the resim path already uses. The repair
logs `intro_joint_presence ... action=eject` when it fires, so future soaks show the repair working.

No offline impact: the rollback snapshot TU is netmenu-only, and the vanilla `ftMainSetStatus`
differential loop is untouched.

## Verification signals for future soaks

- `intro_joint_presence ... action=eject` following an `action=materialize` inside a verify window.
- `figatree-bind ... walked=` returning to its pre-resim value after the emergency restore
  (a permanent +1 in `walked` for the same fkind/motion is this bug's signature).
