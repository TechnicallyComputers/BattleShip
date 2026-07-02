# Netplay — Fox Firefox (SpecialAirHi) resim cross-ISA fidelity

**Date:** 2026-07-01  
**Status:** Fix extended (Appear/SpecialN/charge VFX + FC recovery; soak pending)  
**Area:** `port/net/sys/netplay_fox_firefox_gate.c`, `port/net/sys/netplay_sim_quantize.c`, `port/net/sys/netrollbacksnapshot.c`, `decomp/src/ef/efmanager.c`

See also: [netplay_fox_appear_firefox_charge_soak2_2026-07-01.md](netplay_fox_appear_firefox_charge_soak2_2026-07-01.md)

## Symptom

Sector Z soak2 after patrol map fix (`session=55707289`): `FRAME_COMMIT_STATE_DIVERGE` @600 with **figh + rng**, identical inputs. Fox P1 in `status=232` (`SpecialAirHi` travel). Cross-peer split visible @524 (`JumpB` vs `JumpAerialF`) after first resim @519; grows through Firefox window.

## Root cause

1. **Gate timers** — restore/resim can leave `launch_delay` / `anim_frames` negative; vanilla procs only transition on `== 0`, catch-up used `> 0` early-out without sanitizing first.
2. **Travel angle / physics** — `status_vars.fox.specialhi.angle` and travel physics were not canonicalized each sim boundary (unlike Pikachu Quick Attack).
3. **Forward resim presentation** — no per-tick slot re-pin during Firefox defer scope (egg-lay / shield patterns already had forward resim refresh).

## Fix

| Change | Purpose |
|--------|---------|
| `syNetplayFoxSanitizeFirefoxStatusVars` | Clamp negative gate timers; quantize travel angle |
| `syNetplayFoxCatchUpAllAfterLoadVerify` | Post load-verify catch-up (mirror Pikachu QA) |
| `syNetplayCanonicalizeFoxFirefoxSimState` | Per-tick sim-boundary quantize during defer scope |
| `syNetRbSnapRefreshFoxFirefoxResimPresentationFromSlot` | Per forward resim tick: re-pin fold + catch-up + proc rebind |

## Verify

Fox vs Kirby Sector Z cross-ISA soak through Firefox use; expect no `FRAME_COMMIT` @600 class failure; `fox_firefox` synctest skips may be reducible once stable.
