# Netrollback episode input seal + unified FSM (2026-05-20)

## Symptoms

- `RESIM_POST_DIVERGE` with **matching `inp`** but divergent `figh` (@475–478 automatch soak).
- Longer soak (@1448): **`inp` and `figh` both diverge** on POST — sealed snapshots differed at seal time (host sealed predicted remote @1447 before wire arrived).
- Host frozen at resim target with `rollback_epoch_hold owner=peer_epoch` after POST diverge (older builds).
- Ingress `patch_publish` during baseline wait could mutate published history while forward replay read remote ring first.

## Root cause

1. **Execution vs verification** — rollback treated forward resim read path and post-hoc history checksums as the same mutable published store (partially fixed by frozen sealed table + replay-log POST).
2. **Seal-time wire skew** — initiator sealed remote-human rows from local wire/published reconcile **before** peer’s locally-authoritative rows arrived; POST hashes frozen `sealed[]`, so peers compared different input rows for the same span.
3. **Cap planes** — epoch / defer / sym caps compounded stalls after verify failure (FSM cleanup).

## Fix

Enable with `SSB64_NETPLAY_ROLLBACK_EPISODE_FSM=1` (default off):

1. **SealInputs** — reconcile local-authority slots; copy local rows into `sealed[]`; skip remote-human reconcile + leave peer-authority cells invalid until wire exchange.
2. **EPISODE_SEAL_ROWS (packet 26)** — bidirectional: each peer sends chunked sealed rows for **local-authority** slots after seal; receiver overwrites `sealed[slot]`; wire row is **7 bytes** (buttons, sticks, source/predicted/valid); sim `tick` derived from `(mismatch_tick, row_begin, i)`; **retransmit** on baseline pump; **stash** early packets before local seal.
3. **AwaitingBaseline gate** — `Replay` starts only when baseline digests match **and** `syNetRollbackEpisodeAllPeerSealRowsComplete()`.
4. **Replay** — `syNetInputResolveFrame` / `MakeLocalFrame` read sealed table only.
5. **Replay log** — per-tick digest; POST uses replay-log span digest over `sealed[]`.
6. **Commit** — promote sealed rows to published; caps / POST diverge / event queue as before.

## Follow-up (POST `inp` after wire fix)

- Soak @448–452: seal-rows `RECV` + `replay gate open` OK; **`RESIM_POST_DIVERGE` on `inp` only** (`figh`/`rng` match).
- Fixes: seal local-authority rows from **transmitted** ring; span digest only **episode human slots**; **freeze** `inp` at replay-gate open; block `patch_publish` for entire active episode span; POST diverge no longer forces `Commit→Live`.

## Follow-up (deeper load @509 soak)

- Host `LOAD_TICK_NEGOTIATE` + deeper restart re-sealed span **507–511** (4 ticks) but FSM still advertised tuple **509–511** (2 ticks) on wire; host `SEAL_ROWS_WAIT missing_slots=0x2`, client opened replay with stale 2-tick contract.
- Fix: `syNetRollbackEpisodeResealForDeeperLoad` updates FSM `load/mismatch/target`, clears peer seal + replay log, re-seals; `TryRestartResimAtDeeperLoad` syncs legacy + pushes updated `ROLLBACK_SYNC`; `syNetRollbackTryAlignActiveEpisodeTuple` re-seals follower when peer sync tuple changes during active episode.

## Verify

- `EPISODE_SEAL_ROWS_SEND` / `EPISODE_SEAL_ROWS_RECV` per symmetric episode; `EPISODE_SEAL_ROWS_WAIT` clears before `resim replay gate open`.
- `EPISODE_FSM freeze_post_inp` logged when replay gate opens (both peers should show same `inp=0x........` after seal-rows complete).
- POST: cross-peer **`inp` match** on correction span (or `RESIM_POST_MATCH`).
- `SSB64_NETPLAY_RESIM_TICK_TRACE=1`: matching baseline → matching `resim_tick` through span end.
- No `patch_publish` inside sealed span `[mismatch, target)`.
- No sustained `peer_epoch` hold at target after POST diverge.

## Related

- [`docs/netplay_rollback_refactor_contracts.md`](../netplay_rollback_refactor_contracts.md)
- [`netrollback_sym_reject_cap_post_resim_2026-05-20.md`](netrollback_sym_reject_cap_post_resim_2026-05-20.md)
