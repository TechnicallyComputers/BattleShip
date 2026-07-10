# NetRollback peer baseline resync storm (2026-05-18)

## Symptoms

- Long stable soak (~3700+ ticks) then hang: dozens of `resim begin` with no `resim complete`, `mismatch_tick` walking backward while `target_tick` pinned at frontier.
- `RESIM_BASELINE_MISMATCH` at each load tick; `peer baseline resync armed` on both peers.
- `RESIM_BASELINE_ECHO load_tick=2184` spammed ~90× after an earlier successful resim while sim continued.
- Trigger often preceded by Mario stick prediction (`sx=0 sy=0 pred=1` vs remote analog), not button fields.

## Root cause

1. **Passive baseline echo** — `syNetRollbackOnPeerBaselineDigest` called `TryEchoBaselineResponse` on every peer digest when not resimming, with no dedup or `episodeResolvedThrough` guard. Stale `load_tick` echoes repeated every frame after resim complete.

2. **Unbounded peer state resync** — `ArmPeerBaselineResync` + `TryBeginDeferredStateMismatch` did not use episode commit guards or ring-depth caps. `TryCommitCorrectionBegin` reset `episodeResolvedThrough` when walking to an earlier mismatch, re-opening already-resolved ticks.

3. **Figh-only divergence** — world/rng/item matched but fighter hash differed; baseline gate could not open, abort loop walked backward until span exceeded snapshot ring (65 ticks @ snap=64).

## Fix

1. **`syNetRollbackBaselineEchoAllowed`** — skip echo for resolved/suppressed/stale load ticks; dedup same `load_tick` within 30 sim ticks; reject load ticks outside ring depth.

2. **Storm caps** — track `PeerBaselineResyncSteps` (max 8) and ring-depth floor; log `PEER_BASELINE_RESYNC_STORM` and end session (or clear loop when `PEER_SNAPSHOT_ABORT=0`).

3. **`TryBeginDeferredStateMismatch`** — require snapshot at load tick; route through `TryCommitCorrectionBegin`; reset storm on success/failure paths.

4. **`TryCommitCorrectionBegin`** — when `PeerBaselineResyncStormActive`, deeper mismatch extends anchor without clearing `episodeResolvedThrough`.

## Env knobs

| Variable | Default | Role |
|----------|---------|------|
| `SSB64_NETPLAY_NETSYNC_LOG_INTERVAL` | 120 | NetSync validation log period (sim ticks); use 60 for long soaks |
| `SSB64_NETPLAY_ROLLBACK_DEFER_DIAG` | off | Log defer/commit stage when correction blocked |
| `SSB64_NETPLAY_ROLLBACK_SYMMETRIC_DIAG` | off | **Do not set** for symmetric resim soak — diag-only disables follower resim |

Soak preset: `scripts/netplay-analog-onset-soak.env.example`

## Related

- [`netinput_analog_onset_prediction_2026-05-18.md`](netinput_analog_onset_prediction_2026-05-18.md)
- [`netplay_rollback_test_matrix.md`](../netplay_rollback_test_matrix.md)
