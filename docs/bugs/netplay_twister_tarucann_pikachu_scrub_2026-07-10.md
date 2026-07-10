# Netplay: Twister / TaruCann / Pikachu QA status_vars scrub poison

**Date:** 2026-07-10  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)

## Symptom class

Rollback / synctest save scrub zeros inactive `FTStatusVars` common overlays. Several live
overlays alias those bytes at union offset 0; scrubbing them poisons hash-folded waits / anim
fields while apply only rebinds GObj pointers — timing and catch-up diverge after load.

## Root cause

`syNetRbSnapScrubInactiveStatusVarsInBlob` already early-returned for Kirby Stone, Fox Firefox,
Captain specials, rebirth, Samus charge, cliff, Ness PK Thunder. Three more owners were missing:

| Status | Overlay | Poisoned fields |
|--------|---------|-----------------|
| `nFTCommonStatusTwister` | `common.twister` | `release_wait` (hash-folded); `tornado_gobj` rebound on apply only |
| `nFTCommonStatusTaruCann` | `common.tarucann` | `release_wait` / `shoot_wait` — tarucann memset skipped, but dead/rebirth still alias |
| Pikachu / NPikachu Quick Attack | `pikachu.specialhi` | `anim_frames` after capture quantize → Start→Zip catch-up |

`syNetRbSnapBlobInPikachuQuickAttackSynctestDeferScope` already existed for synctest defer but was
not wired into scrub (same pattern as Ness PK Thunder).

## Fix

In `syNetRbSnapScrubInactiveStatusVarsInBlob` (`port/net/sys/netrollbacksnapshot.c`):

1. Early-return for Twister and TaruCann by `status_id`.
2. Early-return when `syNetRbSnapBlobInPikachuQuickAttackSynctestDeferScope(blob)` is true
   (forward-declare the helper next to the other scrub-scope prototypes).

## Test plan

- [ ] Hyrule Twister ride through synctest / FORCE_MISMATCH; confirm launch timing matches peers.
- [ ] DK Jungle barrel cannon ride; confirm shoot/release waits survive load.
- [ ] Pikachu Quick Attack mid-zip synctest; confirm no Start→Zip catch-up after emergency restore.
