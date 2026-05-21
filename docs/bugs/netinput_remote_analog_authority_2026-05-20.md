# Remote-human analog prediction authority under episode FSM (2026-05-20)

**Status:** FIX SHIPPED (soak pending)

## Symptoms

- Automatch soak ~tick 947: client P2 analog stick onset; host logs `analog_onset_predict` + `defer_analog_correction` with `pub_sx=20` while wire neutral.
- `figh` diverges from tick 947 while `world`/`rng` stay matched (~13 ticks).
- Frame-commit @960: `input_digest` mismatch; rollback episode 960→962; `PEER_SNAPSHOT_DIVERGE` at `load_tick=959` (poisoned baseline band).

## Root cause

**Prediction contamination:** `syNetInputMakePredictedFrameRemoteHuman()` applied optimistic analog onset to **both** `gSYControllerDevices` (sim) and `sSYNetInputHistory` (frame-commit / seal authority). `defer_analog_correction` blocked wire patch while sim ran predicted sticks — published ring could stay neutral in digests but sim state diverged.

Episode FSM sealed/published tables that still read predicted rows could not reconverge after load @959.

## Fix

When `SSB64_NETPLAY_ROLLBACK_EPISODE_FSM=1` (and `SSB64_NETPLAY_REMOTE_ANALOG_ONSET_PRED` unset):

1. **`syNetInputResolveRemoteHumanAuthoritativeFrame`** — wire-confirmed or hold-last only; no `analog_onset_predict`.
2. **`syNetInputRemoteHumanAuthoritativeOnly`** — gates live resolve; bisect env restores legacy onset.
3. **Defer path** — no `defer_analog_correction` return under FSM; wire patches published and queues correction when significant.
4. **`syNetInputCopyEpisodeRemoteHumanSealFrame`** — seal from strict wire-confirmed, never `RemotePredicted` rows.
5. **`EPISODE_FSM seal_authority_mismatch`** — if baselines disagree on `figh` only after seal-rows complete, input correction instead of `PEER_SNAPSHOT_DIVERGE` stop.

**Rollout:** `EPISODE_FSM` remains **default off** until soak passes; no default-on in this change.

## Verify

Re-soak with `SSB64_NETPLAY_ROLLBACK_EPISODE_FSM=1`:

- No `analog_onset_predict` for remote human under FSM (unless `REMOTE_ANALOG_ONSET_PRED=1`).
- Matching `figh` through tick 959 on stick onset.
- No `PEER_SNAPSHOT_DIVERGE` at load 959; episode closes without poisoned baseline.

## Related

- [`netinput_analog_onset_prediction_2026-05-18.md`](netinput_analog_onset_prediction_2026-05-18.md) — onset prediction (legacy path when FSM off)
- [`netrollback_episode_input_seal_2026-05-20.md`](netrollback_episode_input_seal_2026-05-20.md) — seal-rows wire exchange
- [`netplay_rollback_refactor_contracts.md`](../netplay_rollback_refactor_contracts.md) — deferred: timeline rewrite, snapshot byte exchange
