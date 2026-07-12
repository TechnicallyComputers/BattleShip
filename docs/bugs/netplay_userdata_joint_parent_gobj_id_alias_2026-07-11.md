# Netplay: Falcon Punch USERDATA_JOINT mint resolves shared fighter id 1000 to P0

**Date:** 2026-07-11  
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android cross-ISA soak  
**Status:** FIX IMPLEMENTED (soak pending). `build-netmenu` links clean.

## Symptom

Soak `1442382377` (seed `1071719393`) reported a deterministic synctest failure on **both**
peers at tick **6989** diverging only on the `eff` partition:

```
LOAD_HASH_DRIFT tick=6989 ... eff=0x36E169DD/0x811C9DC5
SYNCTEST_FAIL tick=6989
eff_fold_diag tag=capture count=1 gobj_id=1011 respawn=10 parent_id=1000
eff_fold_diag tag=verify  count=0 hash=0x811C9DC5
effect_respawn kind=USERDATA_JOINT ... fighter_gobj_id=1000 resolved_parent=1000 joint_idx=30 result=fail
```

P0 Captain was in `DamageFlyLw` (53); P1 Kirby was in `CopyCaptainSpecialN` (295) with
Falcon Punch flame (`joint_idx=30`). Fighters matched (`figh`/`anim` OK).

## Root cause

Capture folds the live Kirby Falcon Punch flame correctly: `ep->fighter_gobj` points at
Kirby, who is in `syNetRbSnapFighterInCaptainFalconPunchEffectScope`, so the July 3
userdata-joint exclusion does **not** drop it.

On verify, `syNetRbSnapResolveUserdataJointParentGobj` used
`syNetRbSnapPlayerForFighterGobjId(blob->fighter_gobj_id)` / `gcFindGObjByID`. Fighter
GObjs share **kind id 1000** (same class as Ness PK Wave / Yoshi egg-escape / shield
comments in this TU). The first slot match is P0 Captain — not in Falcon Punch scope —
so `syNetRbSnapMakeUserdataJointEffectForFighter` returns NULL, verify eff goes empty,
and capture vs verify forks.

This is **not** the Kirby-inhale fold class
([netplay_kirby_inhale_userdata_joint_eff_fold_2026-07-03.md](netplay_kirby_inhale_userdata_joint_eff_fold_2026-07-03.md)):
that bug excludes non-mintable joint FX. Here the FX is mintable but the parent resolve
picked the wrong fighter.

## Fix

`port/net/sys/netrollbacksnapshot.c`:

| Change | Purpose |
|--------|---------|
| `syNetRbSnapResolveUserdataJointParentGobj` | Resolve by live/slot Falcon Punch scope, then packed player; never shared id 1000 |
| `SYNETRB_EFFECT_SNAP_UJ_PLAYER_*` | Pack parent `player` into `snap_flags` on capture (`VALID` bit distinguishes P0 from legacy) |
| `syNetRbSnapFighterInCaptainFalconPunchEffectScope` | fkind-guard Kirby; include `SpecialAirN` / `CopyCaptainSpecialAirN` |
| `syNetRbSnapBlobInCaptainFalconPunchEffectScope` | Slot-status mirror for ensure when live scope is empty mid-load |
| Matches / slot-lists / sanitize | Prefer packed player; sanitize any Falcon Punch scope (not ground-only Kirby) |

## Verify

- `cmake --build build-netmenu --target ssb64 -j 4` — links clean.
- Re-soak Captain vs Kirby CopyCaptain with synctest: no `USERDATA_JOINT result=fail` when
  Kirby is mid-punch; no `eff`-only `SYNCTEST_FAIL` at Falcon Punch windows.

Related: [netplay_kirby_inhale_userdata_joint_eff_fold_2026-07-03.md](netplay_kirby_inhale_userdata_joint_eff_fold_2026-07-03.md),
Ness PK Wave parent resolve comments in `netrollbacksnapshot.c`.
