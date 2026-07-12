# Netplay stick sampling holes from R-stalls (hard dash / dash-dance)

**Date:** 2026-07-11  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Symptom:** Hard dash / dash-dance feel worse online than training lab / offline even when `|sx|` reaches ≥56 and `tap_stick_x`/`hold_stick_x` reset correctly on leave-deadzone.

## Root cause

Two layers:

1. **Admission order punched sampling holes.** `syNetPeerEvaluateSharedCommitStep` checked `RUNWAY_PREDICT_MAX_SIM_DEFICIT` (default **2**) and skew-lead holds **before** the phase_lock prediction window. Rollback sessions with `phase_lock` 4–7 still R-stalled at deficit 3+, so `syNetInputFuncRead` re-entered the same sim tick without advancing. HID was latched at most once per tick (`sSYNetInputPortHwLatchTick`), so stick motion during the hold was discarded. Dash is a peak-detection gate (`|sx|≥56` within `tap_stick_x < 3`); a 30–50 ms flick plateau fits inside a multi-frame R-hold.

2. **No wall-rate capture.** Even after preferring prediction, residual R/B/E holds (and PortPushFrame sim-skips) could still drop mid-hold HID samples.

Stick witness soaks on LAN (RTT ~17 ms, few R-stalls) showed healthy leave-deadzone resets — the corruption is stall-gated, not a `stick_prev` / device–pl desync.

## Fix

1. **Rollback commit path:** try phase_lock prediction (and epoch/frontier caps) **before** legacy runway-deficit / skew lockstep R-holds. Non-rollback sessions keep the old holds.
2. **Wall-rate HID FIFO** (depth 8, drop-oldest): poll on FuncRead same-tick holds and PortPushFrame sim-skips; each accepted sim tick pops one sample into the hardware latch. Trajectory-preserving, not peak-hold.

## Verify

- WAN / netem soak with `SSB64_STICK_SAMPLE_LOG=1`, `SSB64_STICK_TAP_WITNESS=1`, `SSB64_NETPLAY_FRAME_COMMIT_DIAG=2`.
- Expect lower `pct_R` at the same RTT vs pre-fix; leave-deadzone series should remain continuous across brief ingress gaps.
- Grep `runway_predict_hold reason=sim_deficit|skew_lead` should be rare/absent on rollback sessions during normal play.
