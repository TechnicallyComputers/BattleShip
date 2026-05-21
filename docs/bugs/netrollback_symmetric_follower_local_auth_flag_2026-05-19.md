# Symmetric notify follower local-authority flag (2026-05-19)

## Symptoms

- Automatch first client stick @~485: client completes GGPO resim as `local_initiator`; host receives symmetric notify on slot **0** (initiator's local sim index) and misroutes to **local-authority** resim because host `GetLocalSimSlot()` is also **0**.
- Host never logs `resim begin`; sim runs live while client holds `rollback_epoch_hold` → match freeze.

## Root cause

Symmetric notify carries the initiator's **local sim slot index**, not a stable gameplay player id. Follower routing used `PeerSymmetricAuthoritySlotForPlayer(notify_slot)` — when notify slot equals follower's local sim slot, follower incorrectly chose local-authority instead of blind peer-follower resim.

Remote-human GGPO (host corrected client inputs, or client corrected host inputs) requires follower **local-authority on `GetLocalSimSlot()`**, not on the notify slot index. Same-slot notify on the non-initiating peer requires **blind follower**.

## Fix

1. **1-bit `follower_local_auth`** on symmetric notify (`SYNETROLLBACK_SYM_NOTIFY_FLAG_FOLLOWER_LOCAL_AUTH`):
   - Set when arming notify for remote-human GGPO (`syNetInputIsRemoteHumanSlot(player)`).
   - Cleared for same-peer / local-authority initiator paths.
2. **Follower dispatch** — `syNetRollbackPeerSymmetricUseFollowerLocalAuthority`: when flag set, queue local-authority resim on `GetLocalSimSlot()`; when clear, legacy slot map (blind follower when notify slot ≠ local authority slot).
3. **Wire v6/v7** — append `sym_notify_flags` u8 per slot in INPUT `peer_connect_status`; legacy v4/v5 unchanged. `ROLLBACK_SYNC` flags byte carries the same bit.

## Verify

Automatch host/client first-stick: when **client** initiates GGPO, host log should **not** show `symmetric local authority queued` for notify slot 0; should enter blind follower resim. When **host** initiates GGPO on remote human, client should show `symmetric local authority queued` with `authority_slot=GetLocalSimSlot()`.
