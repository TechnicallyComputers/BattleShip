# Netrollback GGPO unified resim + conservative remote prediction (2026-05-18)

## Symptoms

- Movement phase stable (`rb=0`) but combat (~tick 1192+) produced rollback storms from **predicted remote L-trigger** (`btn=0x0020 pred=1` vs confirmed `0x0000`).
- Post-resim `figh` could match briefly while **`mph` diverged** — dual reconcile paths (remote wire + symmetric local authority fill) fed inconsistent rows into resim.
- Host saw mostly **peer symmetric follower** resims while client drove GGPO on player 0.

## Root cause

1. **Hold-last remote buttons** under input delay predicted shield taps that never happened on the wire.
2. **Split reconcile** in `syNetRollbackBeginResim` (remote published patch + optional symmetric authority forward-fill) could disagree with what each peer actually simulated.
3. **Symmetric follower** (`SSB64_NETPLAY_ROLLBACK_SYMMETRIC` auto-coupled on phase_lock) caused the non-detecting peer to resim from peer notices instead of independent GGPO mismatch detection.

## Fix

- `syNetInputMakePredictedFrameRemoteHuman`: strong hold-last **sticks**; buttons default **0** unless `SSB64_NETPLAY_PREDICT_REMOTE_BUTTONS_HOLD=1`.
- `syNetInputRollbackReconcileResimSpan`: single resim input stream — remote = strict wire confirmed; local = transmitted history (fallback non-predicted published only).
- `syNetRollbackBeginResim` calls unified reconcile only; symmetric notify armed only when `SYMMETRIC=1` (not diag-only default).
- `syNetInputNoteTransmittedSimFrame`: on transmit revision, patch published history from wire row before deferred local-authority resim.
- `syNetRollbackRequestInputCorrection`: remote human slots only.

## Follow-up (same day)

- **Session negotiate** no longer clears `SymmetricDiagOnly` when `rb_flags` includes `SYMMETRIC` (only `SSB64_NETPLAY_ROLLBACK_SYMMETRIC=1` enables follower coupling).
- **Digital tap patch**: isolated 1-frame ±85 keyboard pulses patch published history without GGPO resim (`SSB64_NETPLAY_GGPO_DIGITAL_TAP_PATCH=0` to disable).
- **Low RTT**: negotiated `phase_lock` capped at 4 when `rtt_ms < 120`.

## Verify

Soak host keyboard + client: movement without rollback storm on tap jump; host log shows `symmetric_diag_only=1` and no `peer symmetric rollback at` (only diag notices). Env: default symmetric (diag-only); optional `SSB64_NETPLAY_RESIM_RNG_VERIFY=1`.
