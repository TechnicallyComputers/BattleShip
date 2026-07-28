# Netplay — Synctest emergency restore poisons DamageFly MPColl / camera

**Date:** 2026-07-27  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** Android client + Linux host (`soak1-android.log` / `soak1-linux.log`), Dream Land, seed `2040660132`

## Symptom

Prior light-episode / absorb / FC fixes healthy (`stick_absorb_coalesce=0`, `pairing_fail=0`,
`compared=12`). Permanent diverge from tick **~1715**:

| Tick | Observation |
|------|-------------|
| ≤1713 | figh/world/cam agree; P0 `DamageFlyN` (58) over soft lip |
| **1714** | figh/mph/anim **match**; world+cam diverge (Android-only) |
| **1715** | figh diverges; anim still matches; mph matches |
| 1716 | SoftLip X/vel fork (Android decelerates faster) |
| 1717 | Android → Wait; Linux stays `DamageFlyLw` |
| 1801 / 1921 | FC `replay_determinism`, inputs agree through load |

Android alone logged `SYNCTEST_OK tick=1713` between SoftLip gut=1715 and gut=1716. Linux’s
next synctest was @1727.

## Root cause

1. **Hash-blind MPColl after emergency finalize**  
   Synctest: `CaptureLiveEmergency` → load probe → verify → `RestoreLiveEmergency`
   (`Apply` + `FinalizeLoad` + joint-fidelity repair). Finalize/presentation rewrite
   `coll_data.floor_dist` while `fhash_light` stays green (`pos_prev` / `pos_diff` / TopN only).

   Evidence (Android emergency `D_finalize_end` vs SoftLip gut=1715 end):

   | Field | SoftLip@1715 (pre-synctest) | Emergency finalize |
   |-------|-----------------------------|--------------------|
   | `topn` | `0xC3B866F5` / `0x44CB9CE8` | same |
   | `floor_dist` | `0xC30DE740` (−141.9) | `0xC2ADCE80` (−86.9) |

   Next DamageFly ProcMap/physics uses the wrong floor distance → SoftLip X/vel fork while
   post-synctest figh@1714 still matched.

2. **Unconditional post-synctest camera integrate**  
   After restore, `gmCameraRunFuncCamera` advanced hashed GMCamera scalars on the probing peer
   only. Emergency `camera_apply_diag` `hash_after=0xC8F38248` (= Linux@1714); live log then
   showed `cam=0x95D46C4C`. Cliffwait yank recovery does not need a free integrate every probe.

## Fix

`PORT && SSB64_NETMENU`:

1. **`syNetRbSnapshotRestoreLiveEmergency`** — after joint-fidelity repair / quake sanitize,
   `syNetRbSnapHardPinFighterFoldContributorsFromSlot` + `syNetRbSnapRestoreSoftLipStickyFromSlot`
   so MPColl (incl. `floor_dist`) and process-local soft-lip sticky match the emergency blob.
2. **`syNetRbSnapSynctestRecoverCameraIfInterestYanked`** — post-synctest camera integrate only
   when `CObj.at` is >500 from fighter interests (cliffwait path); otherwise canonicalize only.

## Acceptance

- [ ] Re-soak Dream Land with DamageFly tumble across a synctest cadence boundary.
- [ ] No Android-only figh fork on the tick after `SYNCTEST_OK` during DamageFly*.
- [ ] `floor_dist` on emergency `D_finalize_end` / post-restore trail matches pre-capture SoftLip.
- [ ] Cam hash after synctest matches peer when interest is not yanked (no free integrate).
- [ ] CliffWait quake yank still recovers (`synctest_camera_yank_recover` when dist>500).
