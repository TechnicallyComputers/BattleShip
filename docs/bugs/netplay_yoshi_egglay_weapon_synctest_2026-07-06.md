# Netplay: Yoshi egg-lay YoshiStar weapon fails synctest verify (`wpn` drift)

**Date:** 2026-07-06  
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android cross-ISA soak  
**Status:** FIX IMPLEMENTED (re-soak pending)

## Symptom

Soak2 sessions `262128915` (fail @629) and `581579579` (fail @749):

```
LOAD_HASH_DRIFT tick=629 ... wpn=0x073575FA/0x811C9DC5
LOAD_HASH_DRIFT tick=629 ... figh=0x94E48B37/0x1CBD1100  (per-slot fhash all OK)
SYNCTEST_FAIL tick=629
```

Match context: Samus (P0) victim in `nFTCommonStatusCaptureYoshi` (`status=178`, `motion=153`); Yoshi (P1) idle. `effect_count=1` (`YOSHI_EGG_LAY`, `eff` round-trips). `weapon_count=1` at capture (YoshiStar tongue weapon, `nWPKindYoshiStar`).

Prior probes OK @389, @509; first synctest inside sustained egg-lay window fails.

Follow-on (session `262128915`): `effect_eject reason=hidden_cosmetic_verify` on `ep=nil` effect shell → SIGSEGV `fault_addr=0x8` @640 after synctest fail.

## Root cause

1. **Load apply lifetime expiry:** `weapon apply matched=1` then weapon gone before audit while `sim_tick=slot+1`; `wpYoshiStarProcUpdate` → `wpMainDecLifeCheckExpire` when weapon procs run during load apply.

2. **FinalizeLoadCoupling re-eject (@749 follow-on):** First fix respawned the star in `prepare_verify_yoshi_egglay` but `syNetRbSnapshotFinalizeLoadCoupling` immediately `gcEjectGObj`'d gobj 1012 again before verify hash.

3. **Recovery eject:** `TryRepairWeaponHashForVerify` called `CommitDeferredWeaponEject`, which ejected the respawned star (not in coupled-reference preserve list).

4. **Deferred eject too late:** `sSYNetRbSnapDeferWeaponEjectUntilVerify` was set **after** `ApplySlotToLive` on the first fix pass.

Per-slot `fhash_light`/`fhash_full`/`anim` all match — aggregate `figh` drift is weapon-coupled geometry outside per-slot fhash.

## Fix

`port/net/sys/netrollbacksnapshot.c` + `decomp/src/wp/wpprocess.c`:

| Change | Purpose |
|--------|---------|
| Set `sSYNetRbSnapDeferWeaponEjectUntilVerify=TRUE` at **start** of `syNetRbSnapshotLoad` | Block deferred orphan eject through verify |
| Gate `wpProcessProcWeaponMain` while defer flag set | Block YoshiStar lifetime decay when `sim_tick > slot` during load |
| `syNetRbSnapRepairSlotWeaponsForVerify` as last step of `PrepareLoadedSlotForVerify` + tail of `ApplySlotToLive` | Re-apply blob without `FinalizeLoadCoupling` (re-ejected respawned stars @749) |
| `syNetRbSnapLiveWeaponIsYoshiEggLayStarPreserve` in deferred eject pass | Keep slot-authoritative YoshiStar during egg-lay |
| `TryRepairWeaponHashForVerify`: repair helper; skip `CommitDeferredWeaponEject` while defer active | Recovery no longer ejects freshly respawned star |
| Second `syNetRbSnapCaptureWeapons` before `hash_weapon` in `FillSlotFromLive` | Align weapon blobs with hash fold after effect tail |
| `EjectHiddenCosmeticEffectShellForVerify`: unlink-only when `ep==NULL` and `obj==NULL` | Avoid SIGSEGV on dead cosmetic shells post-fail |

## Verify

- `cmake --build build --target ssb64 -j 4` — links clean.
- Re-soak Samus vs Yoshi egg-lay: session `1465040044` passed `SYNCTEST_OK` @629 after first fix; failed @749 until weapon sim-hold + repair tail (this follow-on).

## Related

- [`netplay_yoshi_egg_lay_synctest_unskip_2026-07-01.md`](netplay_yoshi_egg_lay_synctest_unskip_2026-07-01.md) — presentation/effect reconcile for egg-lay window.
- [`netrollback_weapon_deferred_eject_2026-05-20.md`](netrollback_weapon_deferred_eject_2026-05-20.md) — deferred weapon eject contract.
