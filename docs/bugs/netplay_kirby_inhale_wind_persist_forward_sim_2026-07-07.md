# Netplay: Kirby inhale wind persists after failed inhale (forward sim)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)
**Date:** 2026-07-07

## Symptom

Soak2 Kirby vs Fox, synctest OFF: when Kirby releases neutral-B inhale without capturing Fox,
the inhale wind VFX keeps playing **at the spot where inhale stopped** even after Kirby leaves
SpecialN and walks away. Quick tap-release often looks fine; holding B until the wind/sound
reaches full scale leaves a looping orphan at the release position.

## Root cause (round 2 — soak2 2026-07-07 logs)

Round 1 added forward-sim prune + orphan guard but the soak still reproduced on held inhale.

Log forensics (android `soak2-android.log`):

| Event | Tick | Detail |
|-------|------|--------|
| 2nd inhale start | 676 | `SpecialNCheck`, status 269→270 |
| Wind spawn | ~695 | `gobj_alloc id=1011 caller=0x7195e5c3f0` (`efManagerKirbyInhaleWindMakeEffect`) |
| Immediate save eject | 696 | `gcEjectGObj id=1011 caller=0x71960b04a0` while still status **270 Loop** |
| Release | 759→778 | status 271 End → 10 Wait, Kirby walks away |
| Orphan presentation | 838+ | Wind VFX frozen at release world position |

**Primary bug:** `syNetRbSnapEjectLiveKirbyInhaleWindBeforeFighterApply` runs on **every forward
snapshot save** (twice per tick: pre-capture + pre-anim-hash). Inhale wind is rollback-hidden
(`effect_count=0`) but the save path still stripped the live GObj each tick during SpecialN loop.

**Secondary bug:** `syNetRbSnapEjectKirbyInhaleWindEffectGObj` routed “healthy coupling” shells
through `syNetRbSnapEjectGObj` without a guaranteed `lbParticleEjectStructID`, leaving the LB
particle animating at the last translate after the GObj was recycled (`obj=0x0` on eject lines).

Held inhale differs from quick tap because `flag0` spawns the wind particle later; the save-time
strip + incomplete particle teardown leaves a fully scaled orphan that outlives loop/end.

## Fix

| Change | Purpose |
|--------|---------|
| `syNetRbSnapAnyKirbyInSpecialNInhaleScopeLive` | Detect live Start/Loop inhale |
| `syNetRbSnapEjectLiveKirbyInhaleWindBeforeFighterApplyInternal(TRUE)` on **save only** | Skip strip while Kirby is in inhale Start/Loop; load/verify still unconditional |
| `syNetRbSnapEjectKirbyInhaleWindEffectGObj` | Always `lbParticleEjectStructID` + struct recycle (mirror `ftParamStopEffect`) |
| `ftKirbySpecialNEndSetStatus` / `ftKirbySpecialAirNEndSetStatus` | `ftKirbySpecialNStopEffect` on rollback when B release enters End |
| (retained) `syNetRbSnapForwardPruneStaleKirbyInhaleWindEffects` | Forward-sim zombie sweep after battle update |
| `syNetRbSnapLiveEffectIsKirbyInhaleWindShell` | Gate sweep/per-fighter eject to inhale-wind identity only |
| (retained) `efManagerKirbyInhaleWindProcUpdate` orphan guard | Proc + particle teardown on broken coupling |

## Root cause (round 3 — Fox reflector / Firefox / Kirby cutter regression)

Round 2 added `syNetRbSnapSweepZombieKirbyInhaleWindEffects` for proc-ended inhale-wind orphans.
The `!HasUpdateProc(inhale)` branch ejected **every** fighter-attached effect whose owner was not in
Kirby inhale scope — including Fox reflector (`efManagerFoxReflectorMakeEffect`), Firefox
ImpactWave shells, and Kirby cutter blade FX. Soak2 logs show the pattern every forward tick:

| Move | Tick | Event |
|------|------|-------|
| Fox down-B reflector | 1694 | `gobj_alloc id=1011` → immediate `gcEjectGObj` on snapshot save |
| Fox Firefox travel | 2019 | ImpactWave `gobj_alloc id=1011` → same eject before `effect_count=0` save |
| Kirby up-B cutter | 210+ | blade FX starved when `is_effect_attach` cleared by mis-route eject |

Fix: `syNetRbSnapLiveEffectIsKirbyInhaleWindShell` (live inhale proc **or** Kirby particle script
`0xC` xf shell) gates the sweep and per-fighter eject helper so only inhale-wind shells are touched.

## Verify

- Kirby vs Fox soak, synctest OFF: quick release **and** held full-scale release without capture →
  wind disappears when Kirby leaves SpecialN (no frozen loop at old world position).
- Wind still tracks Kirby during SpecialN loop on both peers.
- Fox down-B reflector shield animates through Start/Loop; Firefox travel/hold VFX play; Kirby
  up-B cutter blade animates in air (not only after landing/projectile).
- No regression on inhale-wind SIGSEGV / synctest verify paths.

Related: [netplay_kirby_inhale_wind_cosmetic_rollback_exclusion_2026-07-03.md](netplay_kirby_inhale_wind_cosmetic_rollback_exclusion_2026-07-03.md),
[netplay_kirby_inhale_wind_orphan_effect_2026-06-07.md](netplay_kirby_inhale_wind_orphan_effect_2026-06-07.md).
