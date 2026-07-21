# Dead-player stick GGPO → asymmetric resim LCG → Whispy Blow wind_dur (2026-07-20)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** session `1790844706`, seed `2990188781`  
**Kill:** `PEER_SNAPSHOT_DIVERGE` @ load 3905 — figh/anim/world **match**, map + rng diverge (`REPLAY_DETERMINISM`)  
**Synctest:** 32 OK / 0 FAIL, `LOAD_HASH_DRIFT=0`

## Symptoms

- Early KneeBend@391 recovered (noise).
- Stick GGPO on **dead P0** @3820 (`pred 0,0` → wire `15,23`), episode load 3819 → target 3825.
- Resim complete: **same** post `figh` / `mph` / `anim`; **rng forks**
  - Android (initiator): `rng D993C920 → D993C920` (no burn)
  - Linux (follower): `rng D993C920 → 6FE700C3` (burned)
- Open→Blow onset **matched** @3881→3882 (tick gate held).
- Blow `wind_dur` **276 vs 290** (`240 + Rand(80)`) → permanent map skew → PEER @3905.
- `PHYSICS_FORK` gut≈4241 = late Whispy/platform cascade — not SoftLip root cause.

## Root cause

Stick-only REPLACE on Dead/ghost cannot change hashed fighter state but still opened a correction episode. During that resim the follower burned gameplay LCG while the initiator did not (matched post figh/map). Later Whispy Blow rolled `whispy_wind_duration` from the forked seed.

## Fix

| Layer | Change |
|-------|--------|
| Stick GGPO | `syNetplayPlayerInDeadGhostStickAbsorbScope` — stick-only REPLACE on **Dead\*** Promote without rewind (buttons/release still rewind). Same pattern as jibaku stick absorb. **Not** bare `is_ghost` — RebirthWait leave needs stick GGPO ([`netplay_rebirth_ghost_stick_absorb_leave_peer_2026-07-20.md`](netplay_rebirth_ghost_stick_absorb_leave_peer_2026-07-20.md)). |
| RNG witness | Always store caller PC in the LCG step ring; on resim complete when baseline→post `rng` changes, dump `rng_hash_walk` with `reason=resim_rng_burn` + sites. |

## Acceptance (re-soak)

- No stick GGPO episodes while correction player is Dead/ghost (`class=dead_ghost_stick` skips OK).
- Matched Blow `wind_dur` when Open→Blow onset matches.
- If residual resim LCG burn appears: use `resim_rng_burn` walk sites to name the draw (ForcedCosmetic or restore), not SoftLip harden.

## Related

- [netplay_pupupu_whispy_open_blow_tick_gate_2026-07-15.md](netplay_pupupu_whispy_open_blow_tick_gate_2026-07-15.md) — onset gate (held here)
- [netplay_hold_last_zero_predict_stick_onset_fc_2026-07-20.md](netplay_hold_last_zero_predict_stick_onset_fc_2026-07-20.md) — prior stick-onset class (not this kill)
- [netplay_shocksmall_cosmetic_rng_fc_diverge_2026-07-08.md](netplay_shocksmall_cosmetic_rng_fc_diverge_2026-07-08.md) — ForcedCosmetic pattern for named draws
