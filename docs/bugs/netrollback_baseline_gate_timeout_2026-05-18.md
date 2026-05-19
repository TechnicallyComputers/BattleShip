# NetRollback baseline gate timeout (2026-05-18)

## Symptoms

- Soak logs show `resim baseline gate timeout load_tick=… — proceeding without peer digest` on every symmetric resim episode.
- `resim baseline gate open` never appears; first `resim_tick` line often shows divergent `figh` while `world`/`rng` still match.
- Cross-peer `figh` already differs at post-load baseline log lines before forward sim starts.

## Root cause

1. **Single-shot UDP:** `syNetPeerTrySendRollbackBaselineDigest()` ran once after load; `syNetRollbackNotePeerBaselineDigestSent()` cleared `PeerBaselineSendPending`, so lost packets were never retried during the gate wait window.
2. **Unsafe timeout policy:** After only 3 frames without a peer digest, `syNetRollbackAdvanceResimBudget()` opened the gate and forward-simmed anyway — propagating divergent fighter state from mismatched local ring slots.
3. **Gate compared live post-load hashes only:** Slot-stored subsystem digests at `load_tick` were logged but not required to match the peer digest; anim-only live drift could pass aggregate checks inconsistently.
4. **No per-fighter wire digests:** Aggregate `figh` can match while per-slot fighter light hashes differ (jump vs grounded at same load tick).

## Fix

- Retransmit `ROLLBACK_BASELINE` every frame while the gate is closed; keep send pending until gate opens or episode aborts.
- Extend gate timeout to 10 frames; on timeout **abort** forward sim and try up to two deeper loads (`load_tick - 1`) before arming peer baseline resync. Streak ≥3 timeouts in 300 ticks triggers hard recovery (session end when `PEER_SNAPSHOT_ABORT` default).
- Env `SSB64_NETPLAY_RESIM_BASELINE_PROCEED_ON_TIMEOUT=1` restores legacy proceed-on-timeout for soak experiments only.
- Gate requires peer digest to match **slot** `figh`/`world`/`item`/`rng`, live post-load digests, `anim` when `world` matches, and per-player `syNetSyncHashFighterStructLight` folds (68-byte wire packet; 56-byte legacy still accepted without slot fields).
- Send/recv logging: `RESIM_BASELINE_SEND`, `RESIM_BASELINE_RECV`, `RESIM_BASELINE_TIMEOUT`, `RESIM_BASELINE_SEND_FAIL`.
- Pre-resim `LOAD_SLOT_LIVE_DRIFT` attempts one deeper snapshot before arming baseline when slot vs live gameplay hashes disagree (non-anim-only).
- **Passive baseline echo:** When `RESIM_BASELINE_RECV` arrives and local is not in a resim episode (`ResimPending == FALSE`), temporarily load the snapshot at `load_tick` (emergency capture/restore), arm post-load digests, and send `RESIM_BASELINE` back so the resimming peer can open the gate without symmetric follower resim.
- **Symmetric transport:** Resim must still send INPUT / `ROLLBACK_SYNC` notices so the follower enters the same episode; see [`netrollback_symmetric_ggpo_transport_2026-05-18.md`](netrollback_symmetric_ggpo_transport_2026-05-18.md).

## Related

- [`netplay_rollback_test_matrix.md`](../netplay_rollback_test_matrix.md) — baseline gate soak env presets.
- [`netrollback_rng_item_identity_drift_2026-05-17.md`](netrollback_rng_item_identity_drift_2026-05-17.md) — symmetric resim execution notes.
