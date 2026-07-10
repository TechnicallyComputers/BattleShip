# Netplay: Pikachu HaveStruct+TopN VFX outside AttackS4 fails synctest (eff)

**Date:** 2026-07-10  
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android soak2  
**Session:** `1727548480` / seed `766113625` (Pikachu vs Kirby); reconfirmed `1713259860` / seed `2533598402` @1589  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending — requires redeployed netplay AppImage/APK; Jun 27 AppImage still fails)

## Symptom

```
SYNCTEST_FAIL tick=2669
LOAD_HASH_DRIFT tick=2669 diverged=eff  (all other partitions match)
```

Both peers identical. 27 prior `SYNCTEST_OK`, 330 intro skips.

## Log evidence

Capture @2669:

```
effect_count=1 hash=0x486CED27
idx=0 gobj_id=1011 bank=0 respawn=0 parent_id=1000 anim_frame=0x42A20000 (81.0)
```

P0 Pikachu `status=232` (`SpecialHiStart` / Quick Attack startup), P1 Kirby `FuraSleep`.
Effect lived from tick ~2589 (2 frames after SpecialHiStart entry) through the probe.

Verify:

```
effect_eject reason=verify_non_canonical ... gobj_id=1011 anim_frame=81.0
eff_fold_diag tag=verify count=0 hash=0x811C9DC5
```

Reconfirm @1589 (`1713259860` / seed `2533598402`): same P0 SpecialHiStart + capture `effect_count=1` /
`respawn=0` / `parent_id=1000` → `verify_non_canonical` → empty eff. Soak peers were the **Jun 27**
`BattleShip-Netplay-x86_64.AppImage` (and matching Android), which predate this predicate — eject
logged `hidden=0 excluded=0` as expected on that binary.

## Root cause

Same class as [Link spin blade](netplay_link_spin_attack_effect_synctest_2026-07-06.md):

- Shell uses `efManagerHaveStructProcUpdate` + TopN joint attach (ThunderShock geometry family).
- `syNetRbSnapEffectRespawnKindFromLive` only returns `RESPAWN_PIKACHU_THUNDER_SHOCK` while the owner
  is in AttackS4; outside that scope the shell is `RESPAWN_NONE`.
- Snapshot captures it into the id-keyed effect ring; verify enforce has no mint path and ejects it
  as `verify_non_canonical` → live eff empty vs slot nonempty.

Forward smash ThunderShock remain authoritative (ensure/prune/respawn) when AttackS4 is live.

## Fix

`port/net/sys/netrollbacksnapshot.c`:

- `syNetRbSnapLiveEffectIsPikachuThunderShockOrphanCosmetic` — Pikachu/`NPikachu`, HaveStruct, TopN
  attach, **not** in AttackS4.
- Exclude via `syNetRbSnapEffectHiddenFromRollback` + `syNetRbSnapLiveEffectExcludedFromRollbackHash`
  (Link spin pattern).

## Test plan

- [ ] Package + deploy netplay Linux AppImage and Android APK from a tree that includes
      `syNetRbSnapLiveEffectIsPikachuThunderShockOrphanCosmetic` (Jun 27 AppImage does **not**).
- [ ] Re-soak Pikachu vs Kirby (or any long Quick Attack Start window) with synctest; no eff-only
      `SYNCTEST_FAIL` / `LOAD_HASH_DRIFT` at probes during SpecialHiStart (known fails: @2669, @1589).
- [ ] Control: Pikachu forward smash ThunderShock VFX still appears after rollback/resim.
