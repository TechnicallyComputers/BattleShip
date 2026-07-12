# Feel-0 provisional send-lead → remote +1 tick phase lag (FC inputs=DIFFER)

**Date:** 2026-07-11  
**Session:** `1648332797` seed `1355060595` (Android client lp=1 ↔ Linux host lp=0, D=2)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

```text
FRAME_COMMIT @600: diverged=figh inputs=DIFFER
player0 Turn(18) status_total_tics 1↔2 / topn_tx fork
Android hist_win all matched Linux FC inp; Android FC inp differed
```

SYNCTEST clean through intro; FORCE_MISMATCH@520 recovered. Player1 (Android local) status trails matched; **player0 (Linux local / Android remote) was exactly one tick behind on every status transition** after ~536 (14/14 divergences = Android `capture_final` equals Linux at `t-1`).

## Root cause

Feel-0 send-lead emits provisional hold-last rows for `t+1…t+D` on the INPUT wire so `hr` can lead intro Wait. Those rows are **not** `NoteTransmitted` locally, but the peer still stores them via `syNetInputSetRemoteInputFromPacket` as **`RemoteConfirmed` with `is_predicted=0`**.

Consequences:

1. `RemoteHumanWireReadyForSimTick` treats provisional as strict wire → peer sims sim tick `T+1` with HID@`T`.
2. When the real sample arrives, `REMOTE_CONFIRMED_REPLACE_NEWER` updates the ring (soak logged dozens of replaces on player0).
3. `syNetRollbackRequestInputCorrection` requires `PublishedSimUsedPrediction` — false for those confirms — so **no GGPO rewind**.
4. Promote then silently rewrites published history to the real stick. State already advanced with the provisional → permanent +1 phase lag → FC `figh` + `inputs=DIFFER`.

## Fix

In `syNetInputCommitRemoteConfirmedWire`, before Promote: if a **strict-confirmed** prior ring row is replaced with a significantly different authoritative sample, mark the published history row as predicted/`RemotePredicted` and call `syNetRollbackRequestInputCorrection` (same path as true hold-last prediction misses).

## Verify

- Re-soak Linux ↔ Android with FORCE_MISMATCH armed: no systematic Android-remote `capture_final` behind-by-1 on stick onsets after GO.
- FC@600: `inputs=MATCH` (or no FC diverge); player0 `status_total_tics` / `topn_*` agree when status_id trails agree.
- Grep `feel0_provisional_replace` / `GGPO input correction queued` on replace storms; no `rollback_epoch` / `load_fail_hold` hang from over-correction.
- Intro Wait still leaves (provisional runway for `hr` unchanged).
