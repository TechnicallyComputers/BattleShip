# Netplay zero-delay local feel (contract split)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`; re-soak after send-lead runway fix)  
**Date:** 2026-07-11

## Symptom

Online VS dash-dance / analog feel lagged training lab even with healthy prediction (`pct_R=0`, FC inputs MATCH). Training lab uses `D=0` sample-aligned resolve; live VS applied closed-loop local delay (`owner = sample + D`).

**Soak regression (same day):** after the first feel-0 split (both rings sample-keyed only), D=1 sessions stuck at sim 0 with intro BGM. `hr` capped at 1 while intro Wait required `next_sim <= DelaySim(hr)`.

## Root cause

Committed session `D` owned both **local feel** and **wire send lead**:

```text
HID@t → DelayHistory[t+D]
gameplay@T ← History[T] (= HID@(T-D))
wire = sim + D
```

Prediction removed remote confirm stalls but could not remove the local `D`-frame HID lag.

The sample-only send-lead staging then removed the ahead wire rows intro Wait depends on (`hr` never reached `next_sim + D`).

## Fix

Split ownership under NETMENU:

```text
HID@t → GameplayRing[t]
HID@t → SendLeadRing[t] (+ provisional hold-last t+1…t+D)
gameplay@T ← GameplayRing[T] (= HID@T)   // feel 0
wire = sim + D                           // send lead unchanged
```

- `syNetInputStageLocalDelayFramesFromLatch` writes gameplay at sample tick; send-lead gets the authoritative sample row plus hold-last provisional rows through `sample+D` (real samples overwrite ahead slots later).
- `syNetInputMakeLocalFrame` / local authority resolve read the **gameplay** ring first (never provisional ahead; Transmitted is fallback only after sample).
- `syNetInputNoteTransmittedSimFrame` refuses local NoteTransmit until gameplay exists for that tick (send-before-sample must not lock hold-last); staging realigns a stale Transmitted row on sample.
- `syNetPeerAppendDelayedLocalRowsToBundle` gathers send-lead for `[sim … sim+D]`; **only `t == sim` is `NoteTransmitted` / promoted**. Provisional `t > sim` rows are wire-only so stick onset does not GGPO-revise local authority into epoch/`load_fail_hold` hangs.
- Intro Wait gate is **not** relaxed — runway comes from send-lead fill.
- Transmitted vs published mismatch scans stay sample-aligned for authoritative rows.
- Resim local reconcile refuses live HID latch fallback.
- Bootstrap: first inbound remote INPUT (including wire tick `0`) sets `remote_ingress_seen` so wire `D=0` can leave the exec gate when `hr` stays `0`.
- **Peer revise:** provisional send-lead stored as `RemoteConfirmed` must GGPO when replaced by the real sample — see `docs/bugs/netplay_feel0_provisional_remote_phase_lag_2026-07-11.md`.

Offline / non-NETMENU builds are unchanged.

## Verify

- Training lab stick/dash still `D=0`.
- LAN soak at `D=1/2`: leaves intro; first stick/dash after GO does not freeze (`no rollback_epoch_hold` / `load_fail_hold` storm).
- Dash-dance / forced mispredict: FC `inputs=MATCH`; feel matches training locally.
