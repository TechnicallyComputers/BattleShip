# Netplay: FC input-agree asymmetric predicted onset (analog)

**Date:** 2026-07-11  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)

## Symptom

After phase-lock predict-gate fix (`netplay_predict_gate_veto_lockstep`), LAN `MATCH_INPUT_DELAY=1` soaks FC-diverged with `inputs=MATCH` / `figh` only:

- validation **1080**: P0 `status=10` vs `17` (Wait-class vs dash/run), Android `PREDICTED_ONSET onset=1020`, Linux `onset=960`
- validation **1560**: same status/motion, off-by-one `status_total_tics` / `topn_tx`
- Peers exchanged `ROLLBACK_SYNC` with **different** `mismatch_tick` → `PEER_SYMMETRIC_NOTIFY_REJECT` churn; histories digested equal but live `figh` stayed forked

Analog stick onset on P0 at sim **1021** (`sx≈-79`) was present on both peers’ STICK_SAMPLE / REMOTE_PUBLISH.

## Root cause

1. **Promote-before-correction:** `syNetInputCommitRemoteConfirmedWire` called `PromoteRemoteHumanAuthorityPublished` before comparing published vs wire. Promote wrote confirmed sticks into history, then `published == wire` early-returned **without** `syNetRollbackRequestInputCorrection`. Predicted hold-last (neutral) vs stick onset never queued immediate GGPO rollback.

2. **Asymmetric FC reanchor:** On input-agree FC mismatch, recovery used `syNetInputFindEarliestPredictedRemoteUsageInSpan` — a **local** flag of “I consumed predicted remote this tick.” With predict-ahead, the idle remote’s peer marks predicted usage from `scan_begin` (960); the peer predicting the moving stick marks onset near stick leave-deadzone (1020). Same FC window → different mismatch ticks → divergent resim.

## Fix

1. **Pre-promote correction:** Snapshot published history before Promote; if it is predicted and significantly differs from confirmed wire, `RequestInputCorrection` while the predicted row still sits in history, then Promote/patch.

2. **Shared FC onset:** Prefer `syNetInputFindEarliestHumanNonNeutralInSpan` (first published non-neutral stick/buttons in the FC window — identical when input digests match) over local predicted-usage flags. Log `FRAME_COMMIT_INPUT_AGREE_ONSET` with `shared=` / `predicted=`.

## Verification

Re-soak D=1 analog dash-dance:

- Expect `pre_promote_ggpo` / GGPO input correction near stick onset, not only FC@120
- On residual FC input-agree: both peers log the **same** `onset=` (shared non-neutral tick)
- No Wait-vs-Dash `FRAME_COMMIT_FIGHTER_SLOT_DIVERGE` with matching input digests after recovery completes
