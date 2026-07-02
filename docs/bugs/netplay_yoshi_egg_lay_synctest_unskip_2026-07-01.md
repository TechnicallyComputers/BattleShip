# Netplay — Yoshi egg-lay synctest skip/probe removed; resim load cycle closed

**Date:** 2026-07-01
**Status:** Fix implemented (`PORT && SSB64_NETMENU`), soak pending
**Area:** `port/net/sys/netrollbacksnapshot.c`

## Symptom

Yoshi neutral-B egg lay during rollback/resim was covered by three overlapping defer mechanisms:

1. `SYNCTEST_SKIP reason=yoshi_egg_lay` — while any fighter is in `CaptureYoshi`/`YoshiEgg`
2. `SYNCTEST_SKIP reason=yoshi_egg_lay_attack` — while Yoshi/Kirby-copy-Yoshi is in the
   SpecialN/Catch/Release attack window (previously mis-fired on Fox Firefox charge due to missing
   `fkind` guard — fixed separately in
   [netplay_yoshi_egg_lay_attack_scope_fkind](netplay_yoshi_egg_lay_attack_scope_fkind_2026-07-01.md))
3. `SYNCTEST_SKIP reason=yoshi_egg_lay_probe` — resim-load fragile walkback while victim is in
   `CaptureYoshi`/`YoshiEgg` (deep walkback only re-entered the same poisoned window; same failure
   shape as the removed `fox_firefox_probe` / `yoshi_egg_lay_attack_probe`)

Together these masked real `eff`/`figh` drift during egg-lay resim instead of letting synctest
localize it, the same class of problem as the removed blanket `fox_firefox` skip.

## Root cause

The skip/probe layers were added before load + forward-resim presentation repair existed for the
full egg-lay window. Once `syNetRbSnapRefreshYoshiEggLayAttackPresentationFromSlot` landed (June
2026, see [netplay_yoshi_egg_lay_resim_joint](netplay_yoshi_egg_lay_resim_joint_2026-06-11.md)),
the skips became redundant coverage holes. The fragile `yoshi_egg_lay_probe` additionally forced
futile resim-anchor walkback (519→487) without escaping the capture scope.

## Fix

**Removed skips/probes:**

- Blanket `yoshi_egg_lay` and `yoshi_egg_lay_attack` branches from
  `syNetRbSnapshotSynctestShouldSkip`
- `yoshi_egg_lay_probe` branch from `syNetRbSnapshotSynctestShouldSkipProbeTick`
- `yoshi_egg_lay_probe` from `syNetRbSnapTryEnsureLiveYoshiEggLayHatchAfterSynctestFragileSkip`
  (hatch ensure still runs on `effect_count_transition_probe` and via
  `syNetRbSnapshotRecoverYoshiEggLayHatchAfterSynctest` on synctest restore)

**Closed resim loop** (same pattern as Fox Firefox un-skip):

- Renamed/expanded `syNetRbSnapRefreshYoshiEggLayAttackPresentationFromSlot` →
  `syNetRbSnapRefreshYoshiEggLayPresentationFromSlot` — covers **both** victim
  (`CaptureYoshi`/`YoshiEgg`) and attacker (SpecialN/Catch/Release) scopes; modelpart cosmetic
  replay remains attacker-only
- Added presentation-scope helpers (`syNetRbSnapBlobInYoshiEggLayPresentationScope`, live/slot
  active checks)
- **Load verify** (`syNetRbSnapshotPrepareLoadedSlotForVerify`): presentation refresh + effect
  reconcile when slot is in egg-lay presentation scope
- **Anchor resync** (`syNetRbSnapshotResyncLiveFightersFromSlotForSim`): same
- **Forward resim** (`syNetRbSnapshotRefreshIntroPresentationAfterForwardResimTick`): per-tick
  presentation refresh + `syNetRbSnapReconcileYoshiEggLayEffectsCore` while any fighter remains
  in presentation scope

`yoshi_egg` (post-escape egg shell status) and `yoshi_aerial_landing` skips are unchanged — those
are separate scopes.

## Verify

Built clean (`cmake --build build-netmenu --target ssb64`). Re-soak Fox vs Yoshi (or Kirby-copy-Yoshi)
with neutral-B egg lay during resim:

- `netplay-scan-drift.py` should show **no** `yoshi_egg_lay`, `yoshi_egg_lay_attack`, or
  `yoshi_egg_lay_probe` skip reasons
- No deep `RESIM_LOAD_ANCHOR` walkback into the capture window
- Expect `SYNCTEST_OK` or a localized `SYNCTEST_FAIL` naming the exact tick/field if drift remains
- Yoshi leg joints stable after resim; egg shell effect count should round-trip via reconcile
