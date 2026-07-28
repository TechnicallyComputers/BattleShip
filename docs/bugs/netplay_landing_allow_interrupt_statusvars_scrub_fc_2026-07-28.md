# Netplay — Landing `is_allow_interrupt` scrub → Turn `status_total_tics` FC / PEER

**Date:** 2026-07-28  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** Android guest ↔ Linux host, Dream Land Ness ditto, seed `3078154319`  
**Related:** [squat pass_wait scrub](netplay_squat_pass_wait_statusvars_scrub_fc_2026-07-28.md), [turn lr_dash scrub](netplay_turn_lr_dash_statusvars_scrub_synctest_2026-07-26.md), [exclusive frontier poison](netplay_light_exclusive_frontier_poison_load_2026-07-28.md)

## Symptom

- Prior Pass vs SquatWait class cleared; new kill: `PEER_SNAPSHOT_DIVERGE` @912 `class=replay_determinism` (figh+anim+cam; map/world/rng match).
- FC@803 / @913: P1 **Turn(18)** both sides; only `status_total_tics` differs (7 vs 4, then 7 vs 6).
- `inputs_agree=1` on both onsets.
- Onset window: matched Fall → LandingLight@791–793; **@795 Android Turn**, Linux still LandingLight through @797; both Turn by @798 with lasting tic skew. FC recovery @804 briefly healed hashes; skew returned → PEER on deepen @912.

## Root cause

`ftCommonLandingProcInterrupt` gates **all** Landing exits (Turn / Walk / Squat / attacks) on `status_vars.common.landing.is_allow_interrupt` (LandingFallSpecial uses `fallspecial.is_allow_interrupt`). Vanilla sets allow=`TRUE` for LandingLight/Heavy.

`syNetRbSnapScrubInactiveStatusVarsInBlob` had **no** early-return for Landing*. `memset` of `attackair` / `dead` / `rebirth` zeroed `is_allow_interrupt` on every ring save.

| Peer | Path | Effect |
|------|------|--------|
| First-pass (Android P1 owner) | Live Landing keeps allow=1 | Interrupt Turn as soon as anim window + stick |
| Light-resim host (Linux storm @788–795) | Load scrubbed Landing blob → allow=0 | Entire ProcInterrupt block skipped until anim-end → Turn ~3t late |

Same scrub class as Squat `pass_wait` / Turn `lr_dash`. Light exclusive-frontier invalidates amplify reload of poisoned Landing saves.

## Fix

`PORT && SSB64_NETMENU`:

1. **`syNetRbSnapScrubInactiveStatusVarsInBlob`** — early-return for `LandingLight`, `LandingHeavy`, `LandingAirNull`, `LandingFallSpecial`.
2. **`syNetSyncHashFighterStructLight`** — fold `is_allow_interrupt` via `ftStatusVarsLanding` / `ftStatusVarsFallSpecial` so `FIGHTER_LIGHT_ONSET` can see allow skew before Turn entry fork.

## Verify on re-soak

- Stick left/right through LandingLight during light GGPO storms: matching Turn entry tick / `status_total_tics`.
- No FC field-only `status_total_tics` diverge on Turn after Landing with `inputs_agree=1`.
- No PEER `replay_determinism` from this Landing→Turn skew path.
