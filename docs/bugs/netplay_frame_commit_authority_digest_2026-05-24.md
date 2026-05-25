# Frame-commit authority digest + reanchor ring sizing (2026-05-24)

## Symptoms (session `986042338`, Linux WAN 1v1)

- `@120`: `FRAME_COMMIT_TOKEN_MISMATCH` with `delta_input_digest=1` while `figh` matched cross-peer (`slot_binding` differs by design — mirrored `local_sim`).
- `@840`: `FRAME_COMMIT_STATE_DIVERGE` (fighter + RNG; inputs agreed), `FRAME_COMMIT_INPUT_AGREE_REANCHOR` with `resolved_load=4294967295`, then `PEER_SNAPSHOT_DIVERGE` @ load 839 and VS stop.

## Root cause

1. **Input digest** used published history only; host/guest could seal different rows after delay/promote skew even when wire authority matched.
2. **Recovery** reanchored from `last_agreed=720` but negotiated snapshot ring was **64** frames while validation cadence is **120** ticks → `min_load` (~778 @ sim 840) excluded tick 720 → no load-safe snapshot (`resolved_load=~0`).

## Fix

1. `syNetInputGetFrameCommitAuthorityChecksumWindow` — frame-commit `input_digest` hashes per-slot **local authority + remote wire authority** (same contract both peers).
2. `syNetInputRollbackReconcilePublishedCommitWindow` — promote local/remote authority for every tick in the commit window before reconcile.
3. `syNetSessionParamsComputeSnapshotFramesFromRtt` — floor ring depth to validation cadence + slack (clamped to 128).
4. `syNetRbSnapshotPinLoadSafeAtTick` on `syNetRollbackNoteFrameCommitStateAgreed`.
5. `syNetRollbackResolveStateMismatchLoadTick` — when input-agreed reanchor fails with ring `min_load`, search from `min_tick=0`.

## Verify

Re-soak the same WAN automatch pair. Expect:

- No spurious `FRAME_COMMIT_TOKEN_MISMATCH` at validation 120 (or matching `inp_local`/`inp_peer` at later boundaries).
- On late fighter drift: `resolved_load` is a real tick (not `4294967295`), rollback attempts from last agreed validation, no immediate `PEER_SNAPSHOT_DIVERGE` if snapshots reconverge.

Optional: `SSB64_NETPLAY_FRAME_COMMIT_DIAG=2`, `SSB64_NETPLAY_RESIM_RECONCILE_LOG=1`.
