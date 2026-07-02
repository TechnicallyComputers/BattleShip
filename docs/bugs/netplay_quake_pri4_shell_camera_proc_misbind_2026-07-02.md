# Netplay: priority-4 quake shell rebound to camera proc

**Date:** 2026-07-02
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)
**Scope:** `port/net/sys/netrollbacksnapshot.c`

## Symptom

Firefox/bumper soak `314477410` passed the authoritative checks:

```
LOAD_HASH_DRIFT: 0
SYNCTEST_FAIL: 0
RESULT: PASS
```

The remaining problem was visual: a Firefox quake still kicked the camera wildly for one frame.

The quake camera diagnostic showed the bad impulse during the resim window:

```
quake_camera_impulse_suppressed tick=520 ... vel=(0x00000000,0x451B6C9E,0x00000000)
quake_camera_impulse_suppressed tick=520 ... vel=(0x00000000,0x40400000,0x00000000)
```

The second line is a normal shake (`0x40400000` = 3.0). The first line is a world-space position
(`0x451B6C9E` is about 2486.8 units), matching the captured priority-4 effect shell:

```
eff_fold_diag tag=capture tick=515 ... quake_pri=4 ... pos=(0x44B1541E,0x451B6C9E,0x00000000)
```

## Root Cause

Real camera quakes created by `efManagerQuakeMakeEffect(magnitude)` use priorities 0..3
(`priority = 3 - magnitude`) and their DObj translate stores a small animated shake offset. That
translate is exactly what `efManagerQuakeProcUpdate` passes to `gmCameraSetVelAt`.

Snapshot matching intentionally treats priority-4 shells as quake-like so stale same-id
impact/quake pairs round-trip through save/load and effect hashing. But after stamping a captured
quake blob, `syNetRbSnapRescheduleQuakeProcIfActive` rebound `efManagerQuakeProcUpdate` for every
quake-like shell with `anim_frame > 0`.

For priority-4 shells, DObj translate can be the impact/world position rather than a shake offset.
Binding the camera proc to that shell makes `gmCameraSetVelAt` add a huge one-frame camera velocity.
The speculative-frame suppression masks the impulse during resim, but once resim completes the live
camera proc can run and create the visible pop without causing authoritative hash drift.

## Fix

Keep priority-4 shells in snapshot matching and effect hashing, but do not bind
`efManagerQuakeProcUpdate` to them. `syNetRbSnapRescheduleQuakeProcIfActive` now returns when
`ep->effect_vars.quake.priority > 3U`.

This preserves the determinism fix from `netplay_quake_duplicate_gobjid_synctest_watchdog_2026-07-02.md`
while making priority-4 shells presentation-inert for camera shake.

## Audit hook

If a soak is deterministic but the camera teleports for exactly one frame during a quake, compare
`quake_camera_impulse_suppressed` velocities against `eff_fold_diag` positions. A velocity that
matches a captured effect position is not a real shake; it means a snapshot shell was rebound to a
camera proc that interprets world-space translate as velocity.
