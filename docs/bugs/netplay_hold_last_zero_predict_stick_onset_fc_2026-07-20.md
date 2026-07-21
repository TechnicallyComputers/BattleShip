# Hold-last hard-zero vs stick onset → SoftLip JumpAerial FC (2026-07-20)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** `1156067044` (seed `3486553202`)  
**Kill:** FC@5210 `figh`, inputs MATCH, `REPLAY_DETERMINISM`  
**Synctest:** 70 OK / 0 FAIL, `LOAD_HASH_DRIFT=0` (DamageE2 resist snapshot held)

## Symptoms

- DamageFall DI stick GGPO episodes @5154 / 5165 / 5169; `rollback_post` matched after each.
- Agreed through JumpAerial entry @5176; SoftLipPhase bit-identical until @5195 (`fhash_light` fork).
- SoftLipPhase @5196 forks `topn` / `vel` / `ja_vel` with matched `ja_drift`, CLIFF sticky, `fline=-1`.
- Heavy `RESIM_STICK_FORK`: host often `0,0` vs guest nonzero — prediction/hold-last lag, not SoftLip math.
- PHYSICS_FORK@8428 / PEER@8431 = late cascade after FC seed (onset 5154, last_agreed 5090, recovery 5172).

## Root cause

1. **Hold-last soft onset skipped the current sim tick.** Ahead peek started at `tick+1`, so wire already present for `tick` was ignored → publish hard `0,0` → GGPO / RESIM_STICK against guest analog.
2. **No fallback when peek empty.** `last_non_neutral` existed for onset defer but was not used when inventing hold-last / remote predicted frames after near-neutral `last_confirmed`.
3. **Stick absorb window ≈ phase_lock (~8)** allowed a triple episode storm (5154→5165→5169) before JumpAerial, amplifying residual prediction skew into SoftLip @5196.

DamageE2 multi-hit resist snapshot (soak `1952491642`) is unrelated and held (synctest clean).

## Fix

| Layer | Change |
|-------|--------|
| Soft onset peek | `TryPeekRemoteAnalogForOnset(max_lookback=0)` starts at `tick` (still no backward lookback — release reinflate soak `1645329949` preserved). |
| `last_non_neutral` | If peek fails and `last_nn.tick > last_confirmed.tick`, apply analog onset floor instead of inventing `0,0`. |
| Stick absorb | Post-episode absorb = `max(2×phase_lock, 16)` capped at 32 so same-player REPLACE coalesces. |
| Diag | `DAMAGE_RESIST_BRANCH` uses `syNetInputGetTick()` when diagnostic resim tick is unset; `JA_VEL_WITNESS` auto rate-limited on JumpAerial + CLIFF. |

## Acceptance (re-soak)

- Fewer `RESIM_STICK_FORK` / stick resim episodes during DamageFall DI → JumpAerial.
- Matching peers through that window; no FC@~5210 class with matched sticks after heal.
- Do **not** SoftLip-harden from late PHYSICS_FORK@8428.

## Related

- [netplay_hold_last_soft_onset_lookback_release_fc_2026-07-18.md](netplay_hold_last_soft_onset_lookback_release_fc_2026-07-18.md) — why lookback stays off
- [netplay_damage_knockback_resist_snapshot_2026-07-20.md](netplay_damage_knockback_resist_snapshot_2026-07-20.md) — prior soak kill class
- [netplay_jumpaerial_ja_vel_witness_2026-07-19.md](netplay_jumpaerial_ja_vel_witness_2026-07-19.md) — ja_vel naming
