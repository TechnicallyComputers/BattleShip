# Post-resim published history reconcile (2026-05-19)

## Symptoms

- After GGPO on remote-human P1 (Yoshi), `FRAME_COMMIT_TOKEN_MISMATCH` at validation ~480: `p0` digests match cross-peer, `p1` diverges.
- `pub_vs_remote_summary … kind=values first_tick=452` after `defer_analog_correction` left published predicted while wire was neutral.
- `post_queue` patches continued through the commit window while defer paths skipped published updates.

## Root cause

`syNetInputRollbackReconcileResimSpan` already copies remote-confirmed → **published** (`sSYNetInputHistory`), but only at **resim begin**. Defer (`ggpo_queued`, `analog_onset`) and late wire left published rows wrong for ticks after forward resim. Frame-commit `input_digest` hashes published history over the full validation window (120 ticks).

## Fix

1. **`syNetInputRollbackReconcileAfterResimCompleted`** — called from `syNetRollbackFinishForwardResim` before episode close; reconciles `[mismatch_tick, max(target_tick, sim_frontier+1))`.
2. **`syNetInputRollbackReconcilePublishedCommitWindow`** — called from `syNetPeerFrameCommitAfterValidation` before `syNetFrameCommitBuildToken`; reconciles `[win_begin, validation_tick)` so commit digests see wire/transmitted truth.
3. **`syNetInputRollbackReconcileResimSpan`** — when `correction_player` is a remote-human slot, only reconcile that remote slot (local slots still reconciled from transmitted).

## Verify

Same soak env as frame-commit diagnostics. Pass: no `FRAME_COMMIT_TOKEN_MISMATCH` through 5+ min mixed Fox/Yoshi; host `p1` `hist_win` matches client `remote_ring p1` at validation 480. Optional: `SSB64_NETPLAY_RESIM_RECONCILE_LOG=1` for `resim_reconcile_span`, `commit_window_reconcile`, and `resim_reconcile_post_complete` lines.

## Related

- [`netinput_pub_vs_remote_and_patch_diag_2026-05-19.md`](netinput_pub_vs_remote_and_patch_diag_2026-05-19.md)
- [`netrollback_ggpo_unified_resim_2026-05-18.md`](netrollback_ggpo_unified_resim_2026-05-18.md)
