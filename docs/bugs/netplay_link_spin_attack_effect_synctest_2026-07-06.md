# Netplay: Link spin-attack blade FX fails synctest verify (non-respawnable eff)

**Date:** 2026-07-06
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android cross-ISA soak
**Status:** FIX IMPLEMENTED (re-soak pending)

## Symptom

Soak2 session `4016138857` (seed `4016138857`, session `741488095`):

```
[FAIL] tick 2069: diverged=eff  [synctest probe]
LOAD_HASH_DRIFT tick=2069 ... eff=0x37E2ED27/0x811C9DC5
```

14 prior `SYNCTEST_OK`, 330 intro skips. All other partitions matched at 2069.

`eff_fold_diag` at capture:

```
count=1 hash=0x37E2ED27
idx=0 gobj_id=1011 bank=0 respawn=0 parent_id=1000 anim_frame=0x42100000 quake_pri=3 pos=(0,0,0)
```

Verify folded zero effects (`0x811C9DC5` empty sentinel). Verify eject:

```
effect_eject reason=verify_non_canonical tick=2069 gobj_id=1011 obj_kind=1 anim_frame=36.0 proc_fp=0x595A4A99
```

Gameplay context: Link (P0, `fkind=5`) in **`nFTLinkStatusSpecialAirHi`** (`status=228`, `motion=203`) holding a bomb (`kind=21`, `hold=1`). The spin blade VFX ran from tick ~2034 through the probe window. **Not caused by F21 bomb throw coupling** — unrelated effect domain.

## Root cause

`efManagerLinkSpinAttackMakeEffect` spawns a TopN joint-attached blade shell with `efManagerHaveStructProcUpdate` (`dEFManagerLinkSpinAttackEffectDesc`). It is folded into the rollback effect hash and captured to the id-keyed snapshot (`respawn=0` = `SYNETRB_EFFECT_RESPAWN_NONE`) because:

- It is not `efManagerNoEjectProcUpdate`, so it misses `syNetRbSnapLiveEffectIsUserdataJointAttach` and the userdata-joint respawn/mint path (Captain Falcon Punch only).
- It is not a genuine quake (`efManagerQuakeProcUpdate`); `quake_pri=3` in `eff_fold_diag` is an incidental `effect_vars` union alias (not folded post 2026-07-02 quake-priority gate).

On synctest verify-load the recycled `gobj_id=1011` shell is ejected during slot enforce with no mint path, so live `eff` count drops `1 → 0` against the ring slot.

## Fix

`port/net/sys/netrollbacksnapshot.c`:

- Add `syNetRbSnapLiveEffectIsLinkSpinAttackCosmetic()` — Link/`NLink` in `SpecialHi` or `SpecialAirHi`, `efManagerHaveStructProcUpdate`, TopN joint attach (matches `efManagerLinkSpinAttackMakeEffect`).
- Exclude from rollback fold in `syNetRbSnapLiveEffectExcludedFromRollbackHash` and hide in `syNetRbSnapEffectHiddenFromRollback` (same cosmetic pattern as Kirby inhale wind, ImpactWave, rebirth halo).

Both peers drop the blade FX symmetrically at capture and verify; forward sim still spawns it deterministically from Link spin status. Weapon hitboxes remain in the `wpn` partition via `wpLinkSpinAttackMakeWeapon`.

## Verify

- `cmake --build build --target ssb64 -j 4` — links clean.
- Re-soak session `4016138857` class: synctest should pass through tick 2069+ with Link SpecialAirHi + held bomb.
