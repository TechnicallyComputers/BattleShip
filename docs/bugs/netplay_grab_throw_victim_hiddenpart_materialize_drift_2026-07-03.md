# Netplay: grab/throw victim hidden-part joint never re-materialized on load (figh+anim drift)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`), soak pending
**Date:** 2026-07-03

## Symptom

VS netplay soak passes overall (STABLE, soft recovery, 0 synctest FAIL, no SIGSEGV) but
`netplay-scan-drift.py` reports a sustained `LOAD_HASH_DRIFT` window — `diverged=anim,figh` — for the
entire duration of a grab/forward-throw. In session `1786240865` it was ticks 504–519 (16 consecutive
ticks) on **both** peers, byte-identical, i.e. deterministic but a save/resim fidelity gap rather than
a cross-peer desync.

The scenario: Fox (`fkind=1`, p1) grabs and forward-throws Kirby (`fkind=8`, p0).

- Fox:   `Catch`(166) → `CatchPull`(167) → `CatchWait`(168) → `ThrowF`(169)
- Kirby: `Wait`(10) → `CapturePulled`(171) → `CaptureWait`(172) → `ThrownFoxF`(187)

Only the **grabbed** fighter (Kirby) diverges. Fox stays `full_ok=1 anim_ok=1` throughout.

## Evidence

`fighter_field_diff tag=load_drift` isolates it to joint topology, not any scalar field:

```
player=0 ... light_ok=1 full_ok=0 anim_ok=0 status=172
player=0 field=joint_presence      live=0x003FF3EF71 blob=0x003FF3EF75
player=0 field=fold_joint_blob_only live=0x0000000000 blob=0x0000000004
```

- `light_ok=1` — the light hash excludes the joint fold, so it matches.
- Bit `0x4` = joint index **2** = `nFTPartsJointXRotN`. Present in the captured blob
  (`joint_is_valid[2]=TRUE`) but absent live (`fp->joints[2]==NULL`) at load/verify.

Kirby's hidden-part table owns this joint:

```
SSB64: ftMainUpdateHiddenPartID OK fkind=8 hpid=0 joint=2 parent=0 kind=3
```

i.e. hidden part `hpid=0`, root joint `XRotN`(2), parent `TopN`(0), `joint_kind=3`. Vanilla forward
sim materializes it during the throw (29 such log lines across the soak). No `intro_joint_presence`
line ever appears — the load-side reconcile never touched it.

## Root cause

`fp->joints[XRotN]` is only ever set non-NULL by hidden-part materialize
(`ftMainUpdateHiddenPartID` / `ftMainAddHiddenPartID`, `fp->joints[root_joint_id] = root_joint`) and
NULLed by eject. During the throw the forward sim materializes Kirby's `hpid=0`, so the capture fold
records `joint_is_valid[2]=TRUE`.

On the rollback load path the per-fighter apply (`syNetRbSnapApplyFighter`) reconciles hidden parts
from `anim_desc` only in intro-load-fidelity scope, and the load-level reconcile
(`syNetRbSnapReconcileFighterJointPresenceFromSlot`) **blanket-skipped every fighter in
`syNetRbSnapFighterInGrabThrowSynctestFragileScope`**:

```c
if (syNetRbSnapFighterInGrabThrowSynctestFragileScope(fp) != FALSE)
{
    continue;   /* skipped BOTH eject and materialize */
}
changed_total += syNetRbSnapReconcileFighterJointPresenceFromBlob(fp, blob);
```

That scope was introduced to stop a SIGSEGV in `ftMainEjectHiddenPartID` during grab/throw hidden-part
*teardown* (NULL `parent_joint` chain once coupling clears —
`docs/bugs/netplay_grab_throw_hiddenpart_synctest_segv_2026-06-07.md`). But the guard was too broad:
it also suppressed the **materialize** direction, which is safe (it splices a new root joint under an
already-present parent, exactly what vanilla forward sim does). So Kirby's XRotN copy-hat root was
never re-created on load, and the live fold walked one fewer joint than the capture fold for the whole
throw → `figh`+`anim` `LOAD_HASH_DRIFT` every tick.

## Fix

Split the grab/throw safety by direction instead of skipping the whole reconcile:

1. `syNetRbSnapReconcileFighterJointPresenceFromBlob` now skips only the **eject** branch when the
   fighter is in `GrabThrowSynctestFragileScope` (the SIGSEGV-prone teardown). The **materialize**
   branch runs, still guarded by the existing parent-present check (Kirby's parent is `TopN`, always
   present), so it cannot hit the NULL `parent_joint` fault.
2. `syNetRbSnapReconcileFighterJointPresenceFromSlot` no longer blanket-`continue`s grab/throw
   fighters; it defers the per-direction decision to the function above.

This materializes the victim's hidden-part root on load to match `joint_is_valid[]`, after which
`syNetRbSnapReapplyFighterJointAnimFromSlot` / `HardPinFighterFoldContributorsFromSlot` re-pin its
pose from the blob, so the verify/resim fold matches the capture fold.

The eject-direction guard added inside `...FromBlob` also hardens the other direct callers
(appear-scope reconcile, anchor-probe pin) against the same teardown fault, at no behavioral cost
(those statuses are never grab/throw).

No offline impact: rollback snapshot TU is netmenu-only; vanilla `ftMainSetStatus` hidden-part loop is
untouched.

## Verification signals for future soaks

- `intro_joint_presence ... action=materialize now_present=1` for the grabbed fighter (`fkind=8`
  Kirby, `root=2`) inside the throw window.
- `netplay-scan-drift.py` reports no `anim,figh` `LOAD_HASH_DRIFT` across grab/forward-throw ticks.
- No SIGSEGV in `ftMainEjectHiddenPartID` at throw-release ticks (eject still skipped in fragile scope).
