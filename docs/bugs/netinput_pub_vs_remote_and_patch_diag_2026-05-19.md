# NetInput pub_vs_remote masking + patch/defer diagnostics (2026-05-19)

**Date:** 2026-05-19  
**Status:** RESOLVED (diagnostics)  
**Area:** `port/net/sys/netinput.c`, `port/net/sys/netpeer.c`

## Symptoms

- Soak at ~tick 1740: cross-peer frame-commit `input_digest` mismatch on **P1** while `pub_vs_remote actionable` only ever logged **`player=2 kind=presence`**.
- Host **published** P1 checksum disagreed with client; host **remote_ring** P1 matched client — real divergence invisible in logs.
- `syNetInputPatchPublishedFromRemoteConfirmed` and defer-without-patch paths had **no** log tags; could not distinguish onset prediction left in place vs silent patch vs wire not yet arrived.

## Root cause (diagnostics)

1. **`pub_vs_remote actionable`** used `syNetInputDiagFindFirstActionablePublishedRemoteMismatch` (first tick × player scan). Empty slot **P2** presence at tick 1620 returned before **P1 value** mismatches later in the window.
2. **Patch/defer** paths in `syNetInputCommitRemoteConfirmedWire` were silent.

## Fix

1. **`syNetInputLogPubVsRemoteWindowDiag`** — one `pub_vs_remote_summary` line per player with any mismatch in the validation window (`kind`, `first_tick`, `mismatches`, `actionable`, `remote_human`). Emitted when extended NetSync input diag runs. Legacy line renamed to `pub_vs_remote_first` with note that summaries are authoritative.
2. **`SSB64_NETPLAY_PATCH_PUBLISH_LOG=1`** — separate tags:
   - `defer_analog_correction` — wire arrived, published row not patched (`reason=analog_onset|ggpo_queued`)
   - `patch_publish` — published overwritten without rollback (`reason=insignificant|no_ggpo|digital_tap|post_queue|unknown`)
3. **`FRAME_COMMIT_DIAG=1`** now also enables extended NetSync input diag (same window as frame-commit validation).

## Soak grep

```bash
# Per-player mismatch in 120-tick window
grep 'pub_vs_remote_summary' host-auto.log

# Onset → defer vs patch at a tick
grep -E 'analog_onset_predict|defer_analog_correction|patch_publish' host-auto.log | grep 'player=1'
```

## Related

- [`netinput_analog_onset_prediction_2026-05-18.md`](netinput_analog_onset_prediction_2026-05-18.md)
- [`docs/netplay_environment_variables.md`](../netplay_environment_variables.md) — `PATCH_PUBLISH_LOG`, `FRAME_COMMIT_DIAG`
