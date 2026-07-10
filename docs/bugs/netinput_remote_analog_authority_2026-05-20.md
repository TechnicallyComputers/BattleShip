# Remote-human analog prediction authority under episode FSM (2026-05-20)

**Status:** FIX SHIPPED — Phase 2b local + Phase 2c remote publish promotion shipped (combined soak pending)

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

## Phase 2b — local publish authority (2026-05-20)

### Symptoms (@930 stick onset, post remote-authority fix)

- No `analog_onset_predict` / `defer_analog_correction` (remote path fixed).
- `FRAME_COMMIT_TOKEN_MISMATCH` @930: `input_digest` + `slot_binding` disagree; `figh` diverges with `world`/`rng` match.
- Client `PEER_SNAPSHOT_DIVERGE` @ load 929.
- `patch_publish` on client shows `pub_sx=0` while `wire_sx≠0` for **remote** slot only; local Kirby uses `PublishFrame`, not `patch_publish`.

### Root cause

**Local authority split:** wire/transmit path (`sSYNetInputTransmittedHistory`, delay ring) carried real sticks; `sSYNetInputHistory` (frame-commit digest) could stay neutral when delay lookup missed per tick. `syNetInputRollbackReconcilePublishedCommitWindow` could not fix rows with no transmitted entry.

### Fix (Phase 2b)

When authoritative wire contract is on (tier ≥ 1):

1. **`syNetInputResolveLocalAuthorityFrame`** — transmitted → delay → HID latch (sim tick key).
2. **`syNetInputPromoteLocalAuthorityPublished`** — idempotent store into published history (no latch downgrade over non-neutral published).
3. **`syNetInputPromoteAllLocalAuthoritySlots`** — primary: after HID latch + delay staging, **before** wire admission; secondary: `NoteTransmittedSimFrame`; safety net: after partial/full publish.
4. **`syNetInputCopyEpisodeLocalAuthoritySealFrame`** — uses resolver (parallel to remote seal).
5. **`syNetInputRollbackReconcileLocalSlotForResim`** — resolver only; no stale published fallback.

Diagnostics: `SSB64_NETPLAY_LOCAL_PUBLISH_LOG=1`; `FRAME_COMMIT_DIAG=2` → `FC_LOCAL_AUTH_MISMATCH`, `FC_SEAL_LOCAL_MISMATCH`.

## Phase 2c — remote publish promotion (2026-05-21)

### Symptoms (@1710 soak, post Phase 2b)

- 57 clean frame-commits through validation 1680; first break @1710.
- Host (authority for remote Samus P1) `patch_publish` at tick 1707: `pub_sx=0 pub_sy=0` while wire `(-11,82)`.
- `commit_token_mismatch` @1710; `slot_span_digest` mismatch; `PEER_SNAPSHOT_DIVERGE` @ load 1709.

### Root cause

**Remote authority split:** local slots had proactive `syNetInputPromoteAllLocalAuthoritySlots` at FuncRead tail; remote humans only got reactive `patch_publish` in `CommitRemoteConfirmedWire` **after** sim had already published hold-last neutral for ticks ahead of the wire frontier.

### Fix (Phase 2c)

When authoritative wire contract is on:

1. **`syNetInputResolveRemoteHumanAuthorityFrameEx`** — wire-confirmed → hold-last; never downgrade published non-neutral with neutral hold-last.
2. **`syNetInputPromoteRemoteHumanAuthorityPublished`** / **`syNetInputPromoteAllRemoteHumanAuthoritySlots`** — FuncRead tail + immediate promote on wire confirm.
3. **`syNetInputCopyEpisodeRemoteAuthoritySealFrame`** — resolver-first seal (no stale neutral published fallback).
4. **Pre-promote** remote slots in `syNetRollbackEpisodeSealInputs` before seal loop.
5. **`patch_publish`** — correction path only when promote did not already equalize published row.

Diagnostics: `SSB64_NETPLAY_AUTHORITY_PUBLISH_LOG=1` (alias for local + remote); `REMOTE_PUBLISH` / `REMOTE_PUBLISH_SKIP` / `REMOTE_PUBLISH_LATE`; `FRAME_COMMIT_DIAG=2` → `FC_REMOTE_AUTH_MISMATCH`.

## Verify

Re-soak with `SSB64_NETPLAY_ROLLBACK_EPISODE_FSM=1` + `SSB64_NETPLAY_AUTHORITY_PUBLISH_LOG=1` (or `LOCAL_PUBLISH_LOG=1`):

**Remote (regression):**

- No `analog_onset_predict` for remote human under FSM (unless `REMOTE_ANALOG_ONSET_PRED=1`).
- No sustained `defer_analog_correction reason=analog_onset` with predicted remote while wire neutral.

**Local + commit (@930 class):**

- `LOCAL_PUBLISH` with non-zero `sx`/`sy` on the moving peer during stick onset.
- No `FC_LOCAL_AUTH_MISMATCH` in frame-commit window at validation 930.
- No `FRAME_COMMIT_TOKEN_MISMATCH` at 930; `inp_local` == `inp_peer`.
- No `FC_SEAL_LOCAL_MISMATCH` during episode seal.
- No `PEER_SNAPSHOT_DIVERGE` at load 929; episode reaches replay gate or clean `Commit→Live`.

**Remote + commit (@1700 class):**

- `REMOTE_PUBLISH source=wire_confirmed` on authority peer for remote human when wire non-neutral (e.g. host P1 @1707+).
- No sustained `patch_publish` with `pub_*=0` and `wire_*≠0` without matching `REMOTE_PUBLISH` on same tick.
- No `FRAME_COMMIT_TOKEN_MISMATCH` @1710; `inp_local` == `inp_peer`.
- No `FC_REMOTE_AUTH_MISMATCH` in commit window; matching `slot_span_digest` cross-peer.
- No `PEER_SNAPSHOT_DIVERGE` @1709.

Suggested window: `FIGHTER_SLOT_HASH_TICK_MIN=1695` `MAX=1715`.

## Related

- [`netinput_analog_onset_prediction_2026-05-18.md`](netinput_analog_onset_prediction_2026-05-18.md) — onset prediction (legacy path when FSM off)
- [`netrollback_episode_input_seal_2026-05-20.md`](netrollback_episode_input_seal_2026-05-20.md) — seal-rows wire exchange
- [`netplay_rollback_refactor_contracts.md`](../netplay_rollback_refactor_contracts.md) — deferred: timeline rewrite, snapshot byte exchange
