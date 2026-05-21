# Frame-commit authoritative episode contract (2026-05-20)

**Status:** FIX SHIPPED (soak pending)

## Symptoms

- Automatch soak @630: `FRAME_COMMIT_STATE_DIVERGE` with matching input digests; host mismatch **624** (predicted-onset reanchor) vs client **630** (validation anchor); targets **631** vs **632**.
- Both peers arm independent FC recovery + cross-send `ROLLBACK_SYNC`; client `LOAD_TICK_NEGOTIATE` to **623** while host resims **624→631**; post-resim item diverge @631; sustained `rollback_epoch_hold`.

## Root cause

Frame-commit state recovery sat **outside** the authoritative symmetric episode contract:

1. **Bilateral mismatch derivation** — input-agree path used per-peer `predicted_onset` reanchor (host @625→624) while peer anchored at validation tick (630).
2. **Per-peer target clamp** — `ComputeSharedResimTarget` used local frontier/remote cap, yielding different targets on asymmetric sim lead.
3. **Episode collision** — local FC resim started before deferred peer `ROLLBACK_SYNC` flushed; `LOAD_TICK_NEGOTIATE` rewrote load without locking full `(mismatch, target, epoch)`; local FC suppressed peer notify when span “covered” local episode.

## Fix

1. **Unified FC tuple (episode authority on)** — input-agree FC anchors mismatch at `validation_tick`; target via `ComputeAuthoritativeFcTarget` (validation+1, authoritative min span). No predicted-onset reanchor on this path.
2. **Peer episode priority** — flush deferred peer symmetric **before** local FC begin; defer FC while peer episode pending/deferred.
3. **`EPISODE_YIELD`** — abort conflicting local resim/FC when peer notify carries a different authoritative tuple; peer with earlier mismatch no longer suppressed by local FC guard.
4. **Locked load** — block `LOAD_TICK_NEGOTIATE` / deeper baseline restart while `AuthoritativeEpisodeActive`; FC initiator marks authoritative episode and arms `ROLLBACK_SYNC` with remote slot + final load/epoch after load.
5. **PendingEpisode preserve** — queue path no longer overwrites wire `(load_tick, epoch_id)` already stored on notify.

## Verify

Re-soak automatch with same env as `client-auto2.log` / `host-auto2.log`:

- Both sides log matching `EPISODE_EXEC exec_mismatch` / `exec_target` on FC recovery @~630.
- No dual overlapping resim spans; no sustained epoch hold past target+slack.
- No `LOAD_TICK_NEGOTIATE` during authoritative FC episode.
