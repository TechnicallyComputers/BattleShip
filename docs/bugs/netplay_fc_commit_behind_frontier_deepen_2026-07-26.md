# FC `commit_behind_frontier` — keep deepen arm through Commit (2026-07-26)

**Status:** FIX ACCEPTED (`PORT && SSB64_NETMENU`)  
**Soak (pre-fix):** session `1065668144` seed `706657431` — Android client ↔ Linux host  
**Soak (accept):** session `1482152420` seed `3088210091` — 0× `commit_behind_frontier` / `fc_commit_failed`; max span 5; dash feel improved  
**Bucket:** `REPLAY_DETERMINISM` / protocol  
**Phase:** 3 (defer-removal series; after [`netplay_fc_episode_begin_stall_retire_2026-07-26.md`](netplay_fc_episode_begin_stall_retire_2026-07-26.md))

## Symptom

Soft-stable PAIRED session; Phase 2 Class-B stalls cleared. Hot TryBegin noise:

| Peer | `commit_behind_frontier` → `fc_commit_failed` |
|------|-----------------------------------------------|
| Android (host label) | 164× |
| Linux (guest label) | 83× |

Cluster shape (android @~2233):

```
BASELINE_PREEMPTIVE_LIVE_CAP_CLEAR resim_complete sim=2233 resolved_through=2233
try_begin_fail stage=commit_behind_frontier mismatch=2228 target=2234 sim=2233
  last_committed=2229 resolved_through=2233
try_begin_fail stage=fc_commit_failed …
defer_diag stage=state_resync_commit_failed …
```

FC/state-hash recovery armed behind `resolved_through` immediately after a completed episode; Commit aborted and **dropped** the arm.

## Root cause

1. **`TryBeginDeferredStateMismatch` cleared `DeferredStateMismatchPending` before `TryCommitCorrectionBegin`.** Deepen eligibility is `FcStateRecoveryActive || DeferredStateMismatchPending`. Peer-baseline / some FC arms set Pending without Active → Commit saw ordinary GGPO → `commit_behind_frontier` → `ClearFcStateRecovery` → permanent drop.
2. **Deepen gate used `sim_tick <= resolved_through`.** Right after `resim_complete`, `sim == resolved`. Even with deepen allowed, Commit failed `commit_behind_resolved` and could not reanchor.

Shared-frontier policy ([`netplay_shared_correction_frontier_2026-07-19.md`](netplay_shared_correction_frontier_2026-07-19.md)) must still block ordinary GGPO behind resolved; FC deepen must proceed once live has **reached** the frontier.

## Fix (`PORT && SSB64_NETMENU`)

| Layer | Change |
|-------|--------|
| Pending lifecycle | Keep `DeferredStateMismatchPending` through successful `TryCommit`; clear only after Commit OK; on Commit/BeginResim fail re-arm pending (do not clear FC recovery) |
| Deepen gate | When deepen allowed: block only if `sim_tick < resolved_through` (allow `sim == resolved` episode reset) |
| GGPO | Unchanged: `allow_frontier_deepen == FALSE` → still `commit_behind_frontier` |

Offline / non-netmenu retains clear-before-Commit + `sim <= resolved` deepen gate.

## Acceptance

Matched APK + Linux:

- Large drop in `commit_behind_frontier` / `fc_commit_failed` pairs during soft-stable VS
- FC deepen behind resolved at `sim == resolved` logs episode reset + `deferred frame-commit state resim`, not perpetual fail
- Ordinary GGPO still cannot open `mismatch < resolved_through` (no seal-hang regression)
- Spans stay short; Phase 2 Class-B stages remain 0×

## Related

- [`netplay_shared_correction_frontier_2026-07-19.md`](netplay_shared_correction_frontier_2026-07-19.md) — GGPO behind-resolved hang (keep)
- [`netplay_fc_episode_begin_stall_retire_2026-07-26.md`](netplay_fc_episode_begin_stall_retire_2026-07-26.md) — Phase 2 accepted
- Phase 4: [`netplay_baseline_echo_retry_ahead_resolved_2026-07-26.md`](netplay_baseline_echo_retry_ahead_resolved_2026-07-26.md)
- Residual invent: Turn-Dash / hold_last (separate)
