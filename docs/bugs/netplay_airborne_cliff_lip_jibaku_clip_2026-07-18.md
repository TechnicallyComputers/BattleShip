# Soft-lip AdjNew suppress — Ness jibaku map clip

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-18  
**Sessions:** soak1 post-ceil-edge build (Android ↔ Linux, Dream Land)

## Symptom

After [`netplay_airborne_cliff_lip_ceil_edge_fc_drift_2026-07-18.md`](netplay_airborne_cliff_lip_ceil_edge_fc_drift_2026-07-18.md) landed, Ness **SpecialAirHiJibaku** could clip through stage walls and fall through the bottom instead of wall-bouncing or ledge-catching.

Soak1 @422–484 (status 26): `MpLanding branch=diff fflags=CLIFF` every frame; gut 442+ `fline=-1` while Y drops below the stage. Both peers matched (gameplay regression, not desync).

## Root cause

`mpProcessNetplaySuppressAdjNewWallOnUnattachedSoftLip` keyed only on stale `floor_flags & (PASS|CLIFF)`:

- Cleared AdjNew L/R wall CheckTest → `mask_curr` never got `LWALL`/`RWALL`
- Skipped ceil-edge X adjust paths

Jibaku procmap bounce and cliff logic depend on those masks (`ftNessSpecialAirHiJibakuProcMap`). High-speed jibaku still carries CLIFF lip residue from launch, so the drift suppress disabled **real** boundary collision.

The suppress target class remains slow DamageFall TopN.x drift ([`netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md`](netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md), [`netplay_airborne_cliff_lip_ceil_edge_fc_drift_2026-07-18.md`](netplay_airborne_cliff_lip_ceil_edge_fc_drift_2026-07-18.md)).

## Fix

In `decomp/src/mp/mpprocess.c` under `PORT && SSB64_NETMENU`:

- Stash `gobj` in `sMPProcessNetplayCollGObj` for the duration of `mpProcessUpdateMain`
- **Non-jibaku** soft lip: full AdjNew suppress (CheckTest + ceil-edge X skip) unchanged
- **Jibaku/bound** soft lip: keep wall/ceil **detection** (`mask_curr`), **quantize** translate snaps via `mpProcessNetplayHardenAdjNewTranslateSnap` (replaces blanket carve-out — see [`netplay_airborne_cliff_lip_jibaku_fc_drift_2026-07-18.md`](netplay_airborne_cliff_lip_jibaku_fc_drift_2026-07-18.md))

DamageFall / Hold approach soft-lip suppress unchanged.

## Verify

Re-soak Android↔Linux Dream Land:

- Jibaku into stage wall → bound or wall physics (no clip-through)
- Jibaku past ledge lip → cliff catch or landing when vanilla would
- DamageFall soft-lip @3344 class still peer-matched (no PEER_SNAPSHOT figh-only from TopN.x drift)
- Repackage AppImage **and** reinstall Android APK

Agent verify: `cmake --build build --target ssb64` only.

## Related

- [`netplay_airborne_cliff_lip_ceil_edge_fc_drift_2026-07-18.md`](netplay_airborne_cliff_lip_ceil_edge_fc_drift_2026-07-18.md) — ceil-edge suppress (still active outside jibaku scope)
- [`netplay_ness_pkthunder_jibaku_procmap_defer_2026-06-04.md`](netplay_ness_pkthunder_jibaku_procmap_defer_2026-06-04.md) — procmap defer vs collision
