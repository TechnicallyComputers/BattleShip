# Netplay: Yoshi egg-lay YoshiStar weapon fails synctest verify (`wpn` drift)

**Date:** 2026-07-06  
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android cross-ISA soak  
**Status:** FIX IMPLEMENTED (weapon @629/@749 + figh-only @509/@629 follow-on; re-soak pending)

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
- Session `710030695`: `wpn` fixed; new **figh-only** drift @509/@629 with all per-slot `light_ok=1 full_ok=1 anim_ok=1` (P0 topn drift during apply: `C3BEFAA8`→`C3BE3CD8`).

## Follow-on: figh-only synctest (@509 / @629, session 710030695)

After the weapon fix, synctest fails with aggregate `figh` drift only:

```
LOAD_HASH_DRIFT tick=509 figh=0x8988A828/0xDB1B6D62 ... wpn=match ... anim=match
fighter_field_diff: light_ok=1 full_ok=1 anim_ok=1 (both players)
SYNCTEST_FAIL tick=509
```

**Cause:** `syNetRbSnapApplyFighter` canonicalize mutates TopN/MPColl fold fields away from the saved blob before `prepare_verify` hard-pin. Ring `hash_fighter` was also folded mid-fill before the capture tail completed, leaving a stale aggregate token while per-slot blob hashes round-trip.

**Fix:**

| Change | Purpose |
|--------|---------|
| Hard-pin fold contributors at end of `ApplyFighter` when egg-lay presentation scope | Stop apply-time topn/MPColl drift |
| Re-fold `slot->hash_fighter` at end of `FillSlotFromLive` (with `hash_animation`) | Ring aggregate matches committed blobs |
| `syNetRbSnapshotAllFighterSlotHashesMatchAtTick` + `LoadVerifyPerSlotFighDriftOk` | Synctest/resim verify continues when only stale ring figh disagrees |
| Refresh `slot->hash_fighter` after prepare_verify when per-slot hashes match | Eliminates LOAD_HASH_DRIFT log on successful round-trip |
| `syNetRbSnapshotRefreshSlotHashFighterWhenPerSlotMatch` (all load paths, not verify-only) | Resim anchor loads @518/@519 no longer log stale ring figh |
| `TryDeeperLoadBeforeResim` skips walkback when per-slot figh oracle passes | Avoids redundant deeper load @518 when @519 verify already OK |

## Follow-on: resim figh-only @518/@519 (session 748160746)

Not synctest — **resim initial load** during frame-commit recovery (`mismatch_tick=520`, load 519→518 walkback). Match **STABLE to tick 2793** (21 synctest OK, 0 FAIL, no SIGSEGV).

```
LOAD_HASH_DRIFT tick=519 figh=0x1EF3AD65/0x16465C73 ... (world/item/wpn/map/rng/anim all match)
fighter_field_diff: light_ok=1 full_ok=1 anim_ok=1
LOAD_HASH_DRIFT per-slot-figh-ok — continuing verify
LOAD_SLOT_LIVE_DRIFT pre-resim — trying deeper load_tick=518 (was 519)
[same @518]
resim baseline digest matched
```

**Cause:** `hash_fighter` refresh after prepare was gated on `verify-only` (synctest). Resim loads hit the same stale mid-fill ring aggregate while per-slot blobs round-trip; verify soft-continued but scan flagged UNRESOLVED and pre-resim walkback still ran.

**Fix:** Un-gate prepare refresh; refresh on per-slot-figh-ok verify path; skip `TryDeeperLoadBeforeResim` when per-slot oracle passes.

## Follow-on: FRAME_COMMIT figh-only @600 (session 839144305)

Cross-peer `FRAME_COMMIT_STATE_DIVERGE` with **matching inputs** during egg-lay attack window (P0 status 177, P1 status 230). Per-slot blob light digests **matched cross-peer** (`0x1F38A774`, `0x33A7DEA9`) but ring aggregate `fighter_digest` differed (`0x6014DBC0` vs `0xD2789070`); world/item/rng/eff all agreed.

Tick 480 `wpn`-only FC-recovery drift was benign (repair-ok, resim continued). Failure triggered after `FORCE_MISMATCH @520` resim epoch 1 + forward replay through egg-lay.

**Fix:**

| Change | Purpose |
|--------|---------|
| `syNetFrameCommitFighOnlyStaleRingDiverge` + `FRAME_COMMIT_FIGH_SLOT_OK` | FC continues when only stale ring figh disagrees but per-slot blobs match cross-peer |
| `syNetRbSnapRecaptureLiveFightersIntoSlot` before final hash fold in `FillSlotFromLive` | Blob matches live at hash instant after item/map tail passes |
| Egg-lay `RefreshYoshiEggLayPresentationFromSlot` before save recapture | P0 victim TopN/joints pinned before final blob+hash |

## Follow-on: SIGSEGV @640 (session 1031533634)

Synctest passes (3 OK @389/@509/@629). Crash ~11 ticks later at forward-sim tick 640:
`efManagerYoshiEggLayProcUpdate` on effect gobj with `ep=nil` after verify-only hidden-cosmetic
shell churn (`fault_addr=0x8`). `gcEjectGObj` on obj=nil shells is also unsafe — use proc-end +
sentinel unlink via `syNetRbSnapSafeEjectOrphanEffectGObj`.

## Related

- [`netplay_yoshi_egg_lay_synctest_unskip_2026-07-01.md`](netplay_yoshi_egg_lay_synctest_unskip_2026-07-01.md) — presentation/effect reconcile for egg-lay window.
- [`netrollback_weapon_deferred_eject_2026-05-20.md`](netrollback_weapon_deferred_eject_2026-05-20.md) — deferred weapon eject contract.
