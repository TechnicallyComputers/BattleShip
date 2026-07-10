# Frame-commit state diverge with agreed inputs — wrong load anchor (2026-05-21)

## Symptoms

- Post-GO soak (~sim tick 450): `FRAME_COMMIT_STATE_DIVERGE` with matching `inp_*`, `world`/`item`/`rng`, split `figh` only.
- Recovery loads `load_tick=449`; `PEER_SNAPSHOT_DIVERGE` / session stop (host).
- `RESIM_ANCHOR_PROBE` `match_f=0` after one forward step from load 448.
- Intro presentation fixed; chronic `commit_token_mismatch` at tick 30 is separate (bind digest mirror slots).

## Root cause

1. **FC recovery anchor (primary)** — With `ROLLBACK_EPISODE_AUTHORITY` and matching input digests, `syNetRollbackOnPeerFrameCommitStateMismatch` forced `mismatch_tick = validation_tick` (450), so deferred recovery loaded snapshot **449** — already poisoned by silent fighter drift since the last **state-agreed** validation (**420**). Resim from 449 cannot converge; peer baselines stay inverted (`0x9020774D` vs `0xFEC1075E`).

2. **`RESIM_ANCHOR_PROBE` order** — Probe ran one battle sim step on the **current** live world before loading `load_tick`, then re-loaded without restoring emergency when not in resim — misleading `match_f=0` and possible live-world side effects.

3. **Contributing skew** — Client logs show ~2× `sim_state_tick` lines per index (predict → `INPUT recv` → re-sim) and more `STRICT MISS (R)` than host; drift accumulates between agreed FC checkpoints while aggregate input digests still match over the 120-tick window.

## Fix

1. Unify FC input-agree path (episode authority uses same logic as legacy): scan from `LastFrameCommitStateAgreedTick`, honor `predicted_onset` when present, else `ResolveStateMismatchLoadTick` from last agreed tick → `mismatch = resolved_load + 1` (cap at `validation_tick`). Log `FRAME_COMMIT_INPUT_AGREE_REANCHOR`.

2. `syNetRollbackResolveStateMismatchLoadTick`: when `DeferredStateMismatchInputAgreed`, probe at `LastFrameCommitStateAgreedTick` instead of `validation_tick - 1`.

3. `syNetRollbackMaybeResimAnchorProbe`: load → rebind → one sim step → compare; restore emergency unless resim is pending (caller keeps loaded world).

## Verify

Re-soak same automatch env. Pass: no stop @450; if state diverge recurs, log shows `FRAME_COMMIT_INPUT_AGREE_REANCHOR` with `resolved_load` near 420 and resim span ~30 ticks, not load 449. Optional: `SSB64_NETPLAY_RESIM_ANCHOR_PROBE=1` with `match_f=1` after fix when ring is healthy.

## Related

- [`netrollback_fc_authoritative_episode_2026-05-20.md`](netrollback_fc_authoritative_episode_2026-05-20.md)
- [`netrollback_fighter_midmatch_drift_2026-05-20.md`](netrollback_fighter_midmatch_drift_2026-05-20.md)
- [`fighter_snapshot_fidelity_2026-05-21.md`](fighter_snapshot_fidelity_2026-05-21.md)
