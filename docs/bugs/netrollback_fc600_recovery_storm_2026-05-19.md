# Frame-commit @600 recovery storm (2026-05-19)

**Status:** Fix shipped (deepen storm guard added 2026-05-19; soak verification pending)

## Symptom (client log, both peers on current package)

At validation **600**:

- `FRAME_COMMIT_STATE_DIVERGE` with **matching** `inp_local` / `inp_peer` (`0xAADB97D2`), divergent `figh` (`0x79932879` vs `0x61F07C95`).
- `FRAME_COMMIT_INPUT_AGREE_PREDICTED_ONSET onset=570` → wide recovery (`ROLLBACK_SYNC` 570→601).
- Host also notifies **peer symmetric** 600→601; client runs blind follower `EPISODE_EXEC` 600→601 (contract OK).
- During follower baseline gate: `BASELINE_UNIVERSE_MISMATCH` at `load_tick=599` (rng/world match, **figh** differs — expected while states are split).
- Code routed mismatch to **input correction** (`mismatch=600 player=0`) even though inputs already agreed.
- Cascaded `deferred frame-commit state resim` 599→604, stacked epochs, sustained `rollback_epoch_hold` (`sim=604 cap=585 peer_target=601`).

## Root cause

Not a deployment skew: the authoritative episode contract executed correctly on the follower (`req_* == exec_*`). The storm came from **recovery policy**:

1. **Overlapping episodes** — frame-commit state recovery (570→601) and a nested peer-symmetric notify (600→601) ran concurrently.
2. **Wrong mismatch class** — `BASELINE_UNIVERSE_MISMATCH` on a fighter split during frame-commit state recovery was treated as poisoned input timeline and forked into GGPO input correction. That path is for input-band poisoning (~1032 soak), not for agreed-input fighter divergence.

## Fix (`port/net/sys/netrollback.c`)

1. **`FcStateRecovery` latch** — set when frame-commit state diverges with matching input digests (including predicted-onset reanchor); stores mismatch/target span.
2. **Suppress nested peer symmetric** — `AcceptPeerSymmetricRollbackNotify` rejects notifies whose span lies inside active frame-commit state recovery (or pending deferred state recovery).
3. **Universe mismatch routing** — during FC state recovery (or deferred state / baseline storm), `BASELINE_UNIVERSE_MISMATCH` calls `AbortPendingResimForBaselineMismatch` (deeper load / resync) instead of `QueueDeferredInputCorrection`.
4. **Epoch cap** — include `FcStateRecoveryTargetTick` in peer epoch live cap; clear latch when resim completes through the FC target.

### Deepen storm guard (post-soak hang)

Soak with (3) still logged `state recovery deepen` on **every** `RESIM_BASELINE` packet (~6M lines) and wedged the main thread in `port_log`/`fflush` inside `syNetRollbackOnPeerBaselineDigest`.

5. **`TryFcStateRecoveryDeepen`** — at most `SYNETROLLBACK_FC_DEEPEN_MAX_PER_LOAD` (4) deepen attempts per `load_tick`; `FcDeepenInFlight` suppresses re-entry until gate open or storm.
6. **One** `→ state recovery deepen` log per `load_tick`; hash detail logged once via `FcDeepenDetailLogged`.
7. **`FC_DEEPEN_STORM`** after cap — clears FC recovery, resets resim, sets `FcDeepenStormActive` (drops further baseline deepen); optional VS session end when `PeerSnapshotAbort` is enabled.

## Verify

Re-soak with same diag env. At @600:

- Expect `peer symmetric suppressed by frame-commit state recovery` (or no nested 600→601 notify) on the recovering peer.
- On baseline fighter split at load 599: `BASELINE_UNIVERSE_MISMATCH … → state recovery deepen` (not `→ input correction`).
- No sustained `rollback_epoch_hold` past `peer_target+slack` after `RESIM_POST_MATCH` for the FC episode.
- At most **4** `state recovery deepen` lines per `load_tick`; then `FC_DEEPEN_STORM` or gate open — **not** millions of repeats.
- No `WATCHDOG HANG` with backtrace stuck in `syNetRollbackOnPeerBaselineDigest` → `port_log`.
- Optional: both peers reach `FRAME_COMMIT_COMPARE validation=600` (host must keep pace through 600).
