# NetRollback strict wire-gap stall

**Date:** 2026-05-17  
**Status:** RESOLVED

## Symptoms

After a rollback corrected a remote-stick misprediction, both peers could finish resim and then freeze in strict input
admission. Logs showed the sim stuck on one tick while INPUT packets continued flowing:

```
STRICT: tick=1482 required_wire=1484 (eff; D=2 slack=0) hr=1487 -> MISS (R)
```

The client build with stall-abort support eventually ended the VS session; the host kept sending INPUT packets until the
window was closed. There was no crash and no `LOAD_HASH_DRIFT`.

## Root Cause

Rollback prediction recovery temporarily requires exact confirmed remote rows. After resim, a peer could observe a remote
frontier beyond the required wire tick while the exact cell was still missing from `sSYNetInputRemoteHistory`. Because
strict admission only accepted the exact `sim + D` row, a single hole in the remote ring could deadlock forward progress
even as later remote rows arrived.

The same session also showed peer-symmetric rollback applying too early on the notified peer. The correcting peer resimmed
`1479 -> 1481`, while the peer that received the symmetric notice clamped the pending target to its current frontier and
resimmed only `1479 -> 1480`.

## Fix

Inbound remote INPUT staging now performs bounded hold-last gap fill for missing confirmed wire rows. Gap-filled rows use a
separate `nSYNetInputSourceRemoteGapFilled` source so strict admission can unblock, while later real confirmed packets for
that exact wire tick replace the gap fill instead of being rejected as confirmed conflicts.

Peer-symmetric rollback notices now queue a minimum target of `mismatch + 2`, and the receiver waits until its local
frontier reaches the queued target instead of clamping the target down. This keeps the notified peer from applying a shorter
resim span simply because the notice arrived one sim tick early.

The existing strict `MISS (R)` session abort remains as a final safety valve for genuine peer death or unrecoverable input
loss.
