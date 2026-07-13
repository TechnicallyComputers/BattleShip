# Feel-0 stick release: predict deadband skips GGPO on completed sim

**Date:** 2026-07-12  
**Session:** `924034521` seed `1400526365` (Android client lp=1 ↔ Linux host lp=0, D=2)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

```text
[host/Android] SYNCTEST_FAIL tick=394
[guest/Linux]  PEER_SNAPSHOT_DIVERGE load_tick=418  figh only (world/rng/map MATCH)
```

Jump forward, release stick mid-air; peers land near the same spot visually. Drift scan: no `LOAD_HASH_DRIFT`, but session UNSTABLE.

## Timeline

1. Jump onset GGPO `391→394` succeeds; post-hashes match.
2. Stick ramp through Jump matches on both peers through tick 400 (`tr_x` equal).
3. Android releases to `0,0` at sim **401**; Linux still publishes feel-0 provisional **`13,4`** and sims 401 with that stick.
4. Linux `REMOTE_CONFIRMED_REPLACE_NEWER` `13,4→0,0` at `cur_tick=402` (**no** `GGPO … queued`).
5. TopN forks at `upt=401`: Android `tr_x=0xC4963DC4` vs Linux `tr_x=0xC49630F7`.
6. Later GGPO/baseline cannot heal → `PEER_SNAPSHOT_DIVERGE` figh @418.

`SYNCTEST_FAIL@394` is Android local ring verify during Jump (effect `id=1011` churn / `emergency_restore_effect_count_0`); cross-peer fighter pose still matched until the release fork at 401.

## Root cause

Feel-0 send-lead stamps provisional hold-last onto future sim ticks as `RemoteConfirmed`. When the real release arrives after that tick has already been simulated:

1. `CommitRemoteConfirmedWire` GGPO gates call `GameplayCorrectionIsSignificantEx(..., predicted=TRUE)`.
2. Predict deadband default **14** treats `13,4` as near-neutral, so `13,4→0,0` is **insignificant**.
3. `PromoteRemoteHumanAuthorityPublished` then silently rewrites published history to `0,0`.
4. Refresh finds `published==wire` → early return with **0× rewind** — live air drift keeps the provisional stick.

## Fix

**`netinput.c` `syNetInputCommitRemoteConfirmedWire`:** when `GetTick() > sim_tick` (already-simulated replace), bypass predict-deadband significance — any gameplay delta queues GGPO (`feel0_completed_sim_replace` / `late_wire_completed_sim`) before Promote.

## Verify

- Re-soak Android ↔ Linux: jump + mid-air stick release.
- Expect `late_wire_completed_sim` or `feel0_completed_sim_replace` + `GGPO … queued` when provisional residual (e.g. 13,4) is replaced by neutral after that sim tick completed.
- Matching TopN after release; no `PEER_SNAPSHOT_DIVERGE` figh from this path.
