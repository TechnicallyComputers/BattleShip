# NetRollback symmetric peer rollback

**Date:** 2026-05-17  
**Status:** RESOLVED

## Symptoms

After one peer rolled back a predicted remote input mismatch, periodic NetSync still showed matching RNG seed hashes while `figh` / `world` diverged. The correcting peer resimmed; the other peer stayed on its pre-correction timeline. Physics coupling from the mispredicted remote fighter left both simulations permanently forked.

## Root Cause

Rollback mismatch detection only compares published history vs confirmed remote ring on **local receive slots**. The peer that mispredicted the other's sticks sees a mismatch; the peer who played those sticks locally does not, because their own slot was always correct.

## Fix

When rollback begins from a local published-vs-remote mismatch, the correcting peer announces the mismatch sim tick in the three previously unused `peer_connect_status` pad bytes for that slot (24-bit big-endian tick). The receiver queues a symmetric resim to the same tick (merged with any earlier local mismatch). Peer-driven resims do not re-announce, avoiding ping-pong. Disable with `SSB64_NETPLAY_ROLLBACK_SYMMETRIC=0`.

Follow-up testing found a lifecycle bug: the sender kept broadcasting the same notice for a hold window, while the receiver only deduped notices while one was pending. A peer could therefore apply the same remote notice repeatedly (`716 -> 718`, then `716 -> 720`, then `716 -> 722`...), extending the resim frontier each time and starving the other side in prediction recovery. Receiver-side notices are now one-shot per slot/tick, capture their target tick when queued, and resim only to that captured target. Sender-side notices keep a few redundant sends, then allow a newer correction on the same slot to replace the old tick.

## Verification

Build target: `ssb64`. Re-run paired rollback logs; both peers should log `peer symmetric rollback` around the same ticks as host-only prediction corrections.
