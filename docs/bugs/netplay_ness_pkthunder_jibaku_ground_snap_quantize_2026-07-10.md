# Netplay — Ness PK Thunder air jibaku ground-snap cross-ISA FC drift (2026-07-10)

**Date:** 2026-07-10  
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android soak2 session `1042832583`  
**Match:** Captain Falcon vs Ness — `FRAME_COMMIT_STATE_DIVERGE @606` `figh` only, inputs MATCH  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptom

Cross-ISA soak after PSI Magnet / jibaku fixes: first frame-commit failure at validation **606**
(snap tick **605**). Both peers agree on inputs, world, rng, eff — **fighter hash only**.

| Field | Linux (guest) | Android (host) |
|-------|---------------|----------------|
| `player` | 1 (Ness) | 1 |
| `status` | 231 (ground jibaku) | 231 |
| `fhash_light` @605 | `0x07D5DA1F` | `0x34F62B17` |
| `topn_tx` | `0xC4B331F4` (-1433.56) | `0xC49EE31C` (-1271.10) |

~162 world-unit X gap on TopN joint translate during ground jibaku hold (ticks 602–605).

## Timeline (Linux log)

| Tick | Event |
|------|-------|
| 600 | PK Thunder self-hit → air jibaku launch (status 236) |
| 602 | `air_jibaku_ground_snap source=procmap_pass_cliff` → status **231** |
| 602–605 | Ground jibaku slide (motion 206) |
| 606 | `FRAME_COMMIT_FIGHTER_SLOT_DIVERGE` on Ness `topn_tx` |

## Root cause

`ftNessSpecialAirHiJibakuSwitchStatusGround` (air 236 → ground 231) calls
`mpCommonSetFighterGround`, which copies `vel_air.x * lr` into `vel_ground.x` with **no grid
alignment**, then `ftMainSetStatus` — but unlike the mirror path
`ftNessSpecialHiJibakuSwitchStatusAir` (ground→air slide-off, fixed 2026-06-04), there was **no**
`syNetplayCanonicalizeNessPKJibakuSimState` after the transition.

Cross-ISA float drift in landing velocity/position baked into ground jibaku and propagated through
`ftNessSpecialHiJibakuProcPhysics` for several ticks before frame-commit compared snap 605.

`syNetplayCanonicalizeNessPKJibakuSimState` also lacked TopN/root translate + MPColl quantize, so
mid-tick physics could not pull peers onto the shared grid after map integration.

## Fix

| Layer | Change |
|-------|--------|
| **Ground snap** | `ftNessSpecialAirHiJibakuSwitchStatusGround` — call `syNetplayCanonicalizeNessPKJibakuSimState` after `SetStatus` (mirror slide-off path) |
| **Jibaku sim** | Extend `syNetplayCanonicalizeNessPKJibakuSimState` — quantize `MPCollData`, root translate, all joint translates (incl. TopN) each ProcPhysics/collide/update hook |

## Verification

1. Re-run cross-ISA soak session pattern (PK Thunder hold → self-hit jibaku → pass-floor landing).
2. Expect no `FRAME_COMMIT_STATE_DIVERGE` through validation 606+; Ness `fhash_light` matches on
   snap 605 across peers.
3. Offline jibaku landing trajectory unchanged (quantize gated on netmenu rollback semantics).

## Related

- [`netplay_ness_pkthunder_jibaku_slideoff_relaunch_2026-06-04.md`](netplay_ness_pkthunder_jibaku_slideoff_relaunch_2026-06-04.md) — mirror ground→air path
- [`netplay_ness_pkthunder_jibaku_quantize_2026-06-01.md`](netplay_ness_pkthunder_jibaku_quantize_2026-06-01.md)
- [`netplay_ness_pkthunder_hold_early_exit_pass_floor_2026-06-02.md`](netplay_ness_pkthunder_hold_early_exit_pass_floor_2026-06-02.md)
