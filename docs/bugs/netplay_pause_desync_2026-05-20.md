# Netplay pause desync (2026-05-20)

## Symptom

Mid-match pause in VS netplay caused hard desync: one peer entered `nSCBattleGameStatusPause` while the other stayed in `Go`. World hash diverged on `game_status` / `time_passed`, rollback tolerated anim-only drift briefly, then `FRAME_COMMIT_STATE_DIVERGE` or partition mismatch. Client-side pause was worse (camera jump, no real pause UI on peer).

## Root cause

Stock pause is **local-only**: `ifCommonBattleGoUpdateInterface` calls `ifCommonBattlePauseInitInterface` immediately on Start, changing `game_status`, camera zoom, and BGM without any network contract. Rollback hashes include `game_status` and pause camera state; asymmetric pause is an immediate shared-state fork.

Anim-only rollback continue during the pause transition window allowed fighter/camera setup to drift before `game_status` matched, amplifying the split.

## Fix

1. **`port/net/sys/netpause.c`** — Pause/unpause contract for active VS sessions:
   - Start during `Go` arms a pause at the current sim tick and sends `SYNETPEER_PACKET_BATTLE_PAUSE` (27).
   - Start during `Pause` arms unpause and sends `SYNETPEER_PACKET_BATTLE_UNPAUSE` (28).
   - Both peers apply pause/unpause only when `syNetInputGetTick()` reaches the agreed tick (deterministic camera setup via `ifCommonBattlePauseSetupFromPlayer`).
   - **Symmetric sim hold:** both peers freeze authoritative sim advance while pause/unpause is pending or active (`syNetPauseShouldHoldSimTick`).
   - **Deferred battle sim:** `syNetPauseShouldDeferBattleSim` skips `gcRunAll` once pause is armed for the current tick.
   - **Apply hooks:** `syNetPauseTryApplyAtBattleBoundary` runs after interface update and after ingress (pre-interface pump via `syNetPeerPumpIngressTransport`).
   - **Late-packet rewind:** if pause/unpause applies when `syNetInputGetTick()` is past the agreed tick, `syNetRollbackRewindToPauseBoundary` loads the snapshot for `pause_tick - 1` and rewinds sim before camera/`game_status` mutation (fixes host-ahead apply at tick+4 freeze).
   - **Synced Start poll:** `syNetPausePollSyncedInputAtTick` arms pause from published input history (any player) so both peers agree on the same tick when wire input is already present.
   - **Pause episode gating:** rollback input scan and frame-commit state recovery are suppressed while pause/unpause is pending or active.
2. **`decomp/src/if/ifcommon.c`** — Netplay Start only requests pause/unpause; apply happens at battle boundary, not inline.
3. **`port/net/sc/sccommon/scvsbattle.c`** — Hold authoritative sim tick while `Pause` / `Unpause` (no asymmetric `time_passed` advance).
4. **`port/net/sys/netrollback.c`** — Disable anim-only load-hash continue while pause/unpause is pending or active.

## Verification

Re-run the four-log repro (host pause / client pause) and confirm both peers log `SSB64 NetPause: applied pause` on the same tick, remain in `Pause` together, and resume without `FRAME_COMMIT_STATE_DIVERGE`.
