# FSM Live peer_epoch cap after commit_promote (2026-05-20)

**Status:** FIX SHIPPED (soak pending)

## Symptoms

- Soak @1541→1543: episode FSM closes cleanly (`commit_promote`, `Verify→Commit→Live`), seal/POST inp agree.
- Host immediately after Live: `rollback_epoch_hold owner=peer_epoch sim=1545 cap=1544 peer_target=1543`.
- Client advances 1545→1554; host frozen @1545; strict remote MISS stall → session abort; release desync.

## Root cause

1. Initiator arms `peer_target` + `PeerEpochAwaitingPeerResimPost` when sending `ROLLBACK_SYNC`.
2. Episode closes via FSM `commit_promote` without `RESIM_POST_MATCH` (local POST boundary only).
3. `FinishForwardResim()` retained peer epoch via `RetainPeerEpochAfterLocalResim()` — clear gated on POST match that never ran.
4. Log proof: **no** `ROLLBACK_SYNC_RECV` / symmetric re-arm between `Commit→Live` and first epoch_hold; stale from episode.

## Fix

`syNetRollbackClearPeerEpochAfterEpisodeFsmClose()` — on FSM full close in `FinishForwardResim()`, release overlapping live caps and **unconditionally** clear peer epoch (skip retain when episode FSM owns the close path).

## Verify

Re-soak analog episode @1541→1543:

- No `rollback_epoch_hold` with `peer_target=1543` after `Commit→Live`.
- Both peers `sim` past target (1546+) with matching `figh` for several ticks.
- No client strict MISS stall from host sim freeze.
