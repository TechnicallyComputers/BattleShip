# ftMainPlayAnim NULL TransN joint SIGSEGV (Fox firefox + Castle bumper)

- **Date:** 2026-07-02
- **Build scope:** `#ifdef PORT` (general port NULL-joint hardening; mirrors the existing `ftphysics.c` sibling guard)
- **Status:** FIX IMPLEMENTED (soak pending)

## Symptom

soak2 session `1182615431` (Fox vs Kirby, Peach's Castle, firefox + bumper collision):
determinism fully passed — `LOAD_HASH_DRIFT=0`, `SYNCTEST_FAIL=0`, `fc_item_div=0`,
`fc_rng_div=0`, `netplay-scan-drift.py` → `RESULT: PASS` — but **both peers `SIGSEGV`
deterministically at sim tick 2551** (`sigsegv x1` each), `fault_addr=0x38`. The crash
handler emitted a `GFX STALE-DL DIAG` header (its default defensive GFX dump), which is a
red herring: the guest backtrace symbolized the real fault site:

```
SSB64: !!!! CRASH SIGSEGV fault_addr=0x38
/tmp/.mount_.../usr/bin/BattleShip(ftMainPlayAnim+0x18) [0x55e220581b98]
```

Player 1 (fkind=1, Fox) had just transitioned `status=18 motion=12` → `status=204 motion=179`
during the tick-2551 rollback apply, and the host log shows a rollback joint eject on that
same fighter immediately prior:

```
NetRbSnapshot: intro_joint_presence player=1 fkind=1 hpid=1 root=1 action=eject now_present=0
```

## Root cause

`ftMainPlayAnim` (`decomp/src/ft/ftmain.c`) dereferences the TransN joint unconditionally
when the anim descriptor requests it:

```c
if (fp->anim_desc.flags.is_use_transn_joint)
{
    fp->anim_vel = fp->joints[nFTPartsJointTransN]->translate.vec.f;   // ftMainPlayAnim+0x18
}
```

A fighter can legitimately have `anim_desc.flags.is_use_transn_joint` set while
`fp->joints[nFTPartsJointTransN] == NULL`:

- intro-scene fighters whose hidden-part (TransN/XRotN/YRotN) joints were never populated
  (the same struct-size class as the `project_addr64_relocation_bug`), and
- (netmenu) rollback snapshot apply, which ejects a hidden-part **root** joint via
  `syNetRbSnapReconcileFighterJointPresenceFromBlob` (logged as
  `intro_joint_presence action=eject`) when the capture blob marks it absent.

With TransN NULL, `->translate.vec.f` reads at `offsetof(DObj, translate.vec.f) == 0x38`
from a NULL base → `fault_addr=0x38`. This is identical, byte-for-byte, on both peers because
the sim state is fully in sync (post-PASS), so it presents as a deterministic matched crash.

This exact NULL-TransN hazard is already guarded in the physics path
(`ftPhysicsApplyGroundVelTransN`, `ftphysics.c:180`) and in the three joint-reset /
reparent sites in `ftMainSetStatus` (`ftmain.c:5244/5265/5283/5320`), all under `#ifdef PORT`.
`ftMainPlayAnim` was simply the one TransN-deref site missed by that hardening pass.

## Fix

Mirror the established sibling guard in `ftMainPlayAnim`: latch `anim_vel` from the TransN
joint only when the joint pointer is non-NULL, otherwise leave the prior `anim_vel` value
(matching the physics path, which falls through to the friction/air-transfer branch when
TransN is absent). Both peers take the skip identically, so the sim stays deterministic.

```c
if (fp->anim_desc.flags.is_use_transn_joint)
{
#ifdef PORT
    if (fp->joints[nFTPartsJointTransN] != NULL)
#endif
    {
        fp->anim_vel = fp->joints[nFTPartsJointTransN]->translate.vec.f;
    }
}
```

`#ifdef PORT` (not `SSB64_NETMENU`) matches the sibling guards: this is a general LP64/port
NULL-joint safety net for a pointer that would otherwise crash in both offline (intro scene)
and netmenu (rollback eject) builds. It cannot change offline sim behavior except to prevent
undefined behavior on a NULL dereference.

## Also in this change

Removed the `effect_probe_mismatch` synctest skip (`syNetRbSnapshotSynctestShouldSkipProbeTick`,
`netrollbacksnapshot.c`). It suppressed the probe-tick save→restore→re-hash whenever the live
effect count differed from the anchor slot. With quakes and Firefox impact waves now excluded
from the rollback effect set as cosmetic (`syNetRbSnapEffectHiddenFromRollback`) and the
per-effect capture/respawn paths idempotent, the last remaining fire (`x1` @`1182615431`) was
benign transient churn. Left removed so any real effect save/load gap surfaces as a
`SYNCTEST_FAIL` / `eff` divergence instead of being silently skipped. The yamabuki monster/gate
effect probes are retained — they cover a distinct ground-object liveness window, not the
generic effect round-trip.

## Verification

- `build-netmenu` and `build-offline` both link clean.
- Soak pending.
