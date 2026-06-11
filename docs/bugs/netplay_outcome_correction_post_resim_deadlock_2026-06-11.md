# Netplay: post-intro outcome-aware correction deadlock @ sim 241 (2026-06-11)

## Symptom

DK/Link intro resim @230 **succeeds** (`anchor_probe match_f=1`, `Commit -> Live` @232). ~8 ticks later both peers freeze at **sim=241**:

```
outcome-aware correction queued mismatch=229 target=241 live_figh=… peer_figh=0xE44441BE …
rollback_epoch_hold epoch=1 sim=241 cap=228 source=peer_target
tick_commit blocked (load_fail_hold) path=L publish=0
```

Frame commit @240 agrees; no GO / no FC state diverge. Android also logs `LOAD_SLOT_LIVE_DRIFT` at load 229 during follower seal-wait.

## Root cause

1. **Stale `LastPeerOutcome`** — peer baseline digest at load tick 229 (`0xE44441BE`) survives after episode commits through 232. Outcome suppression (`target + 2×pred_window`) expires by tick 241; live full figh hash differs → spurious outcome-aware correction.

2. **Deferred cap without runnable resim** — correction queues `mismatch=229` with cap `228`. After debounce expires, `CorrectionAllowedAtTick(229)` fails (`229 < LastCommitted=230`). Deferred cap blocks live advance; resim never starts → permanent stall.

3. **Follower live drift during seal-wait** — `scVSBattleFuncUpdate` still ran live battle sim while `resim_pending && !baseline_gate_open`, mutating live state before snapshot load (`LOAD_SLOT_LIVE_DRIFT`).

## Fix

1. **`syNetRollbackClearLastPeerOutcome()`** on resim complete; reject outcome correction when peer-outcome tick `< LastCommitted` or `< EpisodeResolvedThrough`; extend episode suppression using `EpisodeResolvedThrough`.

2. **Do not `TryCommitCorrectionBegin` in outcome path** before queue — gate on `CorrectionAllowedAtTick` only; queue deferred when allowed.

3. **`syNetRollbackGgpoDeferredShouldAbandon`** — clear deferred + peer outcome when correction not allowed or commit_begin fails for stale mismatches (mirror symmetric abandon).

4. **`scVSBattleFuncUpdate`** — early return during resim seal-wait (`ShouldDeferInterfaceDuringResimWait`) before live battle sim, not only interface defer.

## Validation

Re-run DK/Link soak with inject @230: expect no `outcome-aware correction` with `mismatch=229` after successful resim; match continues past sim 241 toward GO.
