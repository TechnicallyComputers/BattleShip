# NetRollback symmetric GGPO transport blackout (2026-05-18)

**Date:** 2026-05-18  
**Status:** RESOLVED

## Symptoms

- GGPO deferred input correction on one peer (`resim begin` @ mismatch tick) with `symmetric=1 symmetric_diag_only=0`.
- Follower never logged `peer symmetric rollback queued` / `peer symmetric rollback at`.
- Initiator sent many `RESIM_BASELINE_SEND` packets; follower only `RESIM_BASELINE_RECV` (no echo until passive baseline fix).
- Baseline gate timeout cascade and session end on initiator.

## Root cause

1. **Resim transport blackout:** While `ResimPending`, `syNetPeerUpdate` skipped `syNetPeerSendLocalInput()`. Symmetric rollback notices live in INPUT `peer_connect_status` padding (`syNetRollbackExportPeerSymmetricNotify`), so armed notifies never reached the wire during the episode.
2. **Ingress blackout:** `syNetPeerPumpIngressTransport` returns immediately when `syNetRollbackIsResimulating()`, so the resimming peer could not recv baseline echo or sync packets on that path.
3. **Notify timing:** GGPO path armed notify in `TryBeginDeferredMismatch` after the frame's INPUT send had already run; first egress was deferred to the next frame, which resim blocked.

Pure independent GGPO cannot fix the follower case: the peer who played the sticks locally has no `incorrect_prediction` on their timeline ([`netinput_timeline.c`](../port/net/sys/netinput_timeline.c)).

## Fix

**Phase 1 — Resim coordination lane**

- Resim branch in `syNetPeerUpdate`: `syNetPeerReceiveRemoteInput()` + `syNetPeerSendLocalInput()` + baseline pump + sync send.
- Arm symmetric notify at GGPO queue time in `syNetRollbackRequestInputCorrection`.
- Immediate `SendLocalInput` + `TrySendRollbackSyncNotice` at end of `syNetRollbackBeginResim`.
- Follower frontier: `peer_tick <= frontier` (was `<`).

**Phase 2 — `ROLLBACK_SYNC` packet (type 24)**

- Dedicated `SYNETPEER_PACKET_ROLLBACK_SYNC` (28 bytes): `mismatch_tick`, `target_tick`, `slot`, `flags`.
- `syNetPeerTrySendRollbackSyncNotice()` on resim frames and when arming notify.
- Legacy INPUT padding still exported for compatibility.

**Phase 3 — Episode state**

- `SYNetRollbackEpisode` consolidates mismatch/load/target/phase; syncs to legacy `ResimPending` / baseline gate flags.

## Verification

Build `ssb64`. Soak with symmetric + GGPO env; expect on follower:

```
peer symmetric rollback queued slot=… mismatch_tick=…
peer symmetric rollback at tick …
resim begin …
RESIM_BASELINE_ECHO load_tick=…
```

On initiator after follower echo:

```
RESIM_BASELINE_RECV load_tick=…
resim baseline gate open load_tick=…
resim_tick t=…
```

## Related

- [`netrollback_baseline_gate_timeout_2026-05-18.md`](netrollback_baseline_gate_timeout_2026-05-18.md)
- [`netrollback_symmetric_rollback_2026-05-17.md`](netrollback_symmetric_rollback_2026-05-17.md)
- [`netplay_rollback_test_matrix.md`](../netplay_rollback_test_matrix.md)
