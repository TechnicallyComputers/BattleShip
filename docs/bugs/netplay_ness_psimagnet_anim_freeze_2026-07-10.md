# Netplay: Ness PSI Magnet bubble animation frozen (one frame)

**Date:** 2026-07-10  
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android soak2  
**Match:** Captain Falcon vs Ness — bubble visible after missing-VFX fix, loop stuck  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptom

After `syNetplayEnsureNessPsychicMagnetEffect` restored the Psychic Magnet bubble under synctest,
Down+B absorb shows the bubble shell but it **stays on a single animation frame** instead of
looping like offline.

## Log context (soak2-linux.log @ tick 1236+ Hold)

| Observation | Detail |
|-------------|--------|
| Input | Ness status **238** Hold, motion 213 |
| Per tick | `gcEjectGObj id=1011` (caller reconcile) then `gobj_alloc id=1011` (Ensure mint) |
| Snapshot | `effect_count=0` entire Hold window — magnet never in effect fold |
| Gameplay | Absorb glow + sfx active; bubble visible but frozen on mint frame |

Earlier soak @405+ showed the same `effect_count=0` pattern during Hold.

## Root cause (round 3 — soak2 confirmed)

Round 1 fixed mid-tick **anim scalar pinning** in ProcUpdate canonicalize. Round 2 hid the shell
from rollback fold + orphan prune. Re-soak still froze the loop because **PK wave identity stole
the magnet every forward tick**:

1. Psychic Magnet and PK wave are both `gcPlayAnimAll` + Ness parent attach, but magnet couples
   **TopN** (`nFTPartsJointTopN`) while PK wave uses **joint 5** (`SYNETRB_NESS_PKWAVE_JOINT`).
2. `syNetRbSnapLiveEffectIsNessPKWave` called `syNetRbSnapRepinNessPKWaveJoint` **before**
   identity checks, rewriting `user_data.p` to joint 5 so any TopN shell falsely matched PK wave.
3. `syNetRbSnapPruneStaleNessPKWaveEffectsLive()` runs from `syNetplayNessRunLiveJibakuCatchUpAll`
   on every forward tick (`syNetRollbackAfterBattleUpdate`). During SpecialLw Hold (status 238)
   the fighter is not in PK wave scope → prune **ejected** the magnet (`gcEjectGObj id=1011`).
4. `syNetplayEnsureNessPsychicMagnetEffect` reminted at `anim_frame=0` same id each tick — bubble
   visible but frozen (soak2 @980–1272: per-tick eject + `gobj_alloc id=1011`).

Absorb gameplay is fighter-scoped; the bubble is presentation-only (TopN joint + `gcPlayAnimAll`).

## Fix

| Layer | Change |
|-------|--------|
| **PK wave identity** | `syNetRbSnapLiveEffectIsNessPsychicMagnetShell` + early return in `syNetRbSnapLiveEffectIsNessPKWave` **before** PK wave repin |
| **Live magnet ID** | `syNetplayLiveEffectIsNessPsychicMagnet` — Hold/Hit scope + TopN repin (mirror PK wave) |
| **Rollback fold** | (Round 2) Hidden from id-keyed effect snapshot + eff hash |
| **Orphan prune** | (Round 2) Hidden check skips eject |
| **Mint** | Ensure paths only mint when missing (`HasLive` guard) |
| **ProcUpdate** | (Round 1) Fighter pose + Ensure only; no mid-tick anim pin on effect |

## Verification

1. Cross-ISA soak: Ness Down+B — bubble loops during Hold (status 238); no per-tick `gcEjectGObj id=1011` + immediate remint during stable Hold.
2. `effect_count=0` during magnet is expected (hidden cosmetic); `eff` hash unchanged by magnet anim.
3. Rollback load during magnet: Ensure mints once if missing; anim advances on forward/resim replay.

## Related

- [`netplay_ness_psimagnet_bubble_missing_2026-07-10.md`](netplay_ness_psimagnet_bubble_missing_2026-07-10.md)
- [`netplay_ness_psimagnet_camera_twitch_2026-06-02.md`](netplay_ness_psimagnet_camera_twitch_2026-06-02.md)
- [`netplay_impact_wave_cosmetic_rollback_exclusion_2026-07-02.md`](netplay_impact_wave_cosmetic_rollback_exclusion_2026-07-02.md)
