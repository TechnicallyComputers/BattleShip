# Follower peer-symmetric figatree mutation before snapshot load (2026-05-20)

## Symptoms

- Automatch soak ~420 clean ticks; GGPO analog correction @429 triggers symmetric rollback load=428 target=432.
- Host load @428: `fighter_anim post-load` pass (`live_anim == slot_anim == 0x392C9EAD`).
- Client (follower, sim=430 when `ROLLBACK_SYNC_RECV` arrives): probe baseline echo @428 passes, then **`ftMainSetStatus status=0x14 motion=14`** fires **before** authoritative `item apply tick=428`; verify fails anim-only (`slot=0x392C9EAD`, `live=0x6B08914C`), soft-continues.
- Post-resim: matching `inp`/`rng`/`item`, swapped `figh`; host P1 `status=20 motion=14`, client P1 `status=10 motion=4`.
- Host epoch hold @432 (`cap=429 peer_target=432`) until client diverges.

## Root cause

Same class as [netrollback_ggpo_preload_figatree_freeze_2026-05-19.md](netrollback_ggpo_preload_figatree_freeze_2026-05-19.md), but on the **symmetric follower**:

1. `ROLLBACK_SYNC_RECV` queues `PendingPeerSymmetricTick` during ingress (`FuncRead`), but `syNetRollbackUpdate()` (which calls `BeginResim` → `syNetRbSnapshotLoad`) ran **after** `ifCommonBattleUpdateInterfaceAll()` in `scVSBattleFuncUpdate`.
2. `DeferredCorrectionBlocksLiveAdvance` did not cover pending peer-symmetric notify — only armed `PeerSymmetricRejectLiveCap` on local-authority queue failure.
3. Follower already at sim=430 > mismatch-1=428; one battle-sim step between sync receipt and load ran fighter motion (`ftMainSetStatus` 0x14/14) on poisoned live state.
4. Probe `RESIM_BASELINE_ECHO` (transient load + emergency restore) before sync was benign; authoritative load failed because live anim mutated in the same frame after sync.

Issue 3 (status 20 vs 10) and issue 4 (epoch hold) are downstream of this pre-load mutation, not separate snapshot gaps.

## Fix

1. **`syNetRollbackPeerSymmetricNotifyBlocksLiveAdvance`** — cap live sim at `notify_mismatch - 1` while `PendingPeerSymmetricTick` or `DeferredPeerSymmetricPending` is active; wire into `syNetRollbackGetLiveSimCap`.
2. **Arm cap on notify** — `syNetRollbackArmPeerSymmetricRejectLiveCap(mismatch)` in `QueuePeerSymmetricNotify` and deferred-notify path.
3. **`syNetRollbackPumpCorrectionBeforeBattleSim`** — after deferred flush, call `syNetRollbackUpdate()` when peer-symmetric notify is pending so follower loads snapshot **before** battle sim in the same frame.
4. **Suppress baseline echo** while peer-symmetric notify is pending and `sim > mismatch-1` (avoid transient load/restore while follower is ahead).

## Verify

Re-run automatch soak that produced client `client-auto2.log` / host `host-auto2.log`:

- After `ROLLBACK_SYNC_RECV`, client must **not** log `ftMainSetStatus` with motion=14 before `item apply tick=428`.
- `fighter_anim post-load` @428 on follower: `live_anim == slot_anim`.
- No anim-only `LOAD_HASH_DRIFT` on follower authoritative load; post-resim `figh` matches host; no epoch hold stall @432.
