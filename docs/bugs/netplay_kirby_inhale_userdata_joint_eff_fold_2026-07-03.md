# Netplay: non-respawnable userdata-joint FX (Kirby inhale) folds to zero on verify

**Date:** 2026-07-03
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android cross-ISA soak
**Status:** FIX IMPLEMENTED (soak pending). `build-netmenu` + `build-offline` link clean.

## Symptom

Soak `1974820229` reported a deterministic synctest failure on **both** peers at tick 749
diverging only on the `eff` partition:

```
[FAIL] tick 749: diverged=eff  [synctest probe]
LOAD_HASH_DRIFT tick=749 ... eff=0x0F55B820/0x811C9DC5   (all other partitions match)
```

`eff_fold_diag` showed one live effect at capture, zero at verify:

```
eff_fold_diag tag=capture tick=749 count=1 hash=0x0F55B820
  idx=0 gobj_id=1011 bank=0 respawn=10 parent_id=1000 anim_frame=0x0 quake_pri=39 ...
eff_fold_diag tag=verify  tick=749 count=0 hash=0x811C9DC5   (empty)
```

Both fighters matched (`full_ok=1 anim_ok=1`). Player 1 (Kirby) was in status **258 =
`nFTKirbyStatusSpecialNStart`** (inhale). `respawn=10` = `SYNETRB_EFFECT_RESPAWN_USERDATA_JOINT`.

## Root cause

`syNetRbSnapLiveEffectIsUserdataJointAttach` classifies **any** effect whose
`proc_update == efManagerNoEjectProcUpdate` with a fighter parent and a non-null
`DObj user_data.p` (joint) as a respawnable `USERDATA_JOINT` effect. That kind was added
specifically for the Captain Falcon Punch flame (Kirby `CopyCaptainSpecialN` /
`Captain SpecialN`), which is the **only** userdata-joint effect with a mint path:
`syNetRbSnapMakeUserdataJointEffectForFighter` → `efManagerCaptainFalconPunchMakeEffect`,
gated on `syNetRbSnapFighterInCaptainFalconPunchEffectScope`.

Kirby's `SpecialNStart` inhale spawns a joint-attached FX that **also** matches the broad
`IsUserdataJointAttach` predicate, so it is classified `USERDATA_JOINT` and folded into the
rollback effect hash — but it has **no respawn/mint path**. On verify-load the recycled
effect GObj (`gobj_id=1011`) is ejected during the load reset, and the ensure pass
(`syNetRbSnapEnsureUserdataJointEffectsFromSlot`) can neither find a live match nor mint a
replacement, so the effect count drops `1 → 0` and the `eff` fold forks against the ring.
This is the same "authoritative snapshot for a purely-visual, non-round-trippable effect"
class as the quake / ImpactWave / rebirth-halo cosmetic exclusions.

## Fix

`port/net/sys/netrollbacksnapshot.c`, `syNetRbSnapLiveEffectExcludedFromRollbackHash()`:

Exclude userdata-joint FX from the rollback effect fold **unless** the parent fighter is in
the Captain Falcon Punch effect scope (the only scope with a mint path):

```c
if ((syNetRbSnapLiveEffectIsUserdataJointAttach(gobj, ep) != FALSE) &&
    (syNetRbSnapFighterInCaptainFalconPunchEffectScope(ftGetStruct(ep->fighter_gobj)) == FALSE))
{
    return TRUE;
}
```

Because both `syNetSyncHashActiveEffectsForRollback` (the eff hash) and the fold diag
enumerate through `syNetRbEnumerateActiveEffectsSorted`, which gates on this helper, the
non-respawnable joint FX is now dropped **symmetrically** at capture and verify — the fold
never counts it, so `load(slot) == slot`. Falcon Punch flame (in-scope, mintable) is
untouched and still tracked/respawned as before.

## Follow-up risk

If a Falcon Punch flame outlives its status scope by a tick, it would flip from folded to
excluded at that boundary and could produce a one-tick `eff` blip; the flame is tightly
coupled to `SpecialN`, and the mint path already shares this scope dependency, so this is
no worse than the existing behavior. Any other character's joint-attached one-shot FX that
needs true round-trip fidelity should get a dedicated respawn/mint path and be added to the
respawnable scope rather than relying on the fold.

## Verify

- `cmake --build build-netmenu --target ssb64 -j 4` — links clean.
- `cmake --build build-offline --target ssb64 -j 4` — links clean (snapshot TU is
  `SSB64_NETMENU`-only).
- Lint clean on the touched file.

## Soak checklist

- Kirby inhale (`SpecialNStart`/loop) under a synctest probe: expect **zero**
  `LOAD_HASH_DRIFT[eff]` with `eff_fold_diag` capture/verify counts equal.
- Captain Falcon Punch (Captain or Kirby copy): flame still folds and respawns across
  rollback/verify as before.
