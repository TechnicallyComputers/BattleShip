# Minimum correction tuple + 4p rollback hooks (2026-05-27)

## Summary

Tightened GGPO-style input rollback so correction episodes consistently use the **earliest validated mismatch**, load from `mismatch - 1`, and resim forward only as far as **published prediction** requires. Added slot-count-agnostic hooks for eventual 4-player netplay without changing matchmaking.

## Root cause (soak class)

Correction tuple math was scattered across wire ingress, timeline scan, outcome-aware correction, symmetric follower local-auth, and episode FSM drain. Each path could pick slightly different `(mismatch, target)` spans. The follower could re-scan and shift `eff_mismatch` away from the initiator's `ROLLBACK_SYNC` tuple when `follower_local_auth` was set. `PendingEpisode` only extended `target`, never deepened `mismatch` on a later authoritative notify. `QueueDeferredInputCorrectionEx` always extended target by the full phase-lock window even when no predicted rows remained.

## Fix

| Area | Change |
|------|--------|
| Tuple helper | `syNetRollbackComputeInputCorrectionTuple` — wire hint → per-player timeline → global min → ring scan; conditional target extension only when any remote-human slot has predicted published rows in `[mismatch, frontier)`. |
| Timeline | `syNetInputTimelineFindGlobalEarliestIncorrect`, `GetEarliestIncorrectForPlayer`, `last_remote_confirmed_sim_tick` + `GetLastRemoteConfirmedSimTick`. |
| Episode authority | `PendingEpisodeBySlot[MAXCONTROLLERS]` with per-slot merge (`mismatch=min`, `target=max`, `load=min`); `EPISODE_TUPLE_REJECT` when mismatch would rise without epoch bump. |
| Symmetric follower | When `ROLLBACK_EPISODE_AUTHORITY=1` and slot episode valid, blind-follow initiator tuple; skip local-auth re-scan. |
| Defer queue | Predicted-span probe replaces unconditional phase-lock extension; `CORRECTION_MERGE_DEEPEN` when deferral mismatch moves earlier. |
| 4p lab | Document `SSB64_NETPLAY_REMOTE_SLOTS=1,2,3`; global-min mismatch drives one whole-state resim. |

## Observability

- `SSB64_NETPLAY_ROLLBACK_CORRECTION_TUPLE_LOG=1` — `CORRECTION_TUPLE` at queue time (`source=wire|timeline_player|timeline_global|scan`).
- `EPISODE_EXEC` extended with `timeline_earliest` and `last_confirmed` for the episode slot.

## Verification

- Build: `cmake --build <worktree>/build --target ssb64 -j 4`
- 1v1 automatch soak: first stick @~450–500; matching `exec_mismatch`/`exec_target`; no follower `LOAD_TICK_ADJUST` mismatch shift with authority on.
- 4p lab (env-only): host `SSB64_NETPLAY_REMOTE_SLOTS=1,2,3`; inject mismatches on slots 1 and 3 → global-min mismatch = min(1,3); one resim span.

## Related

- [`docs/netplay_rollback_refactor_contracts.md`](../netplay_rollback_refactor_contracts.md)
- [`docs/netplay_rollback_test_matrix.md`](../netplay_rollback_test_matrix.md) case 4
