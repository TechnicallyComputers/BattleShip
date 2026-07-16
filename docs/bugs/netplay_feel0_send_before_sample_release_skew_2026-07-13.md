# Feel-0 send-before-sample stick release skew → hang

**Date:** 2026-07-13  
**Session:** soak1 seed `4134815356` (Android client lp=1 ↔ Linux host lp=0, D=2)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

Synctest clean through first GGPO (@459). Stick/jump mash: session hangs (Android `rollback_epoch_hold cap=570`, Linux `VS_SESSION_END`). No FC `state_diverge`.

## Timeline

1. Stick decay matches through tick **562** (`(-8,-10)`, Android `ledger_wire`).
2. Android admits **563** with `REMOTE_PUBLISH … source=hold_last` `(-8,-10)` while Linux STICK_SAMPLE@563 is **`(0,0)`**.
3. JumpAerial (status 25) pose forks; load **570** baselines disagree (`figh` `0xB634C960` vs `0x1B1530F4`).
4. Stick GGPO @571 → `BASELINE_UNIVERSE_MISMATCH` → FSM `AwaitingBaseline → Live` → Android live-cap freeze / host session end.

Zero `REMOTE_CONFIRMED_REPLACE` / release GGPO for 563 in the soak.

## Root cause

Two feel-0 egress holes piled on:

1. **`auth_wire_frontier = DelayWireTickFromSim(GetTick())`** — INPUT send often runs after phase_lock admits `next_sim` but **before** FuncReadstages HID. `delay[sim]` is still hold-last from `sample-1`, yet `GetTick()==sim` makes that row **Strict** (`wire ≤ frontier`). Peer upgrades GapFilled → Confirmed with the provisional stick.

2. **`GatherHistoryBundle` early-return** when `published.tick != GetTick()` (neutral releases skip `LOCAL_PUBLISH`, so published lags) — `MakeLocalFrame(sim)+return` **dropped history retransmission**. The real `(0,0)` for sim 563 was only eligible while `GetTick()==563` after sample; later packets never re-sent wire565=`(0,0)`. Android kept simmed hold-last forever.

Secondary hang: `AbortToInputCorrectionFromUniverseMismatch` abandoned baseline into Live **without** clearing `BASELINE_PREEMPTIVE_LIVE_CAP`, so deferred GGPO could not BeginResim (`cap=570`, `peer_target=573`).

## Fix

1. **`auth_wire_frontier`** from `syNetInputGetLocalGameplayAuthSimTick` (highest sampled gameplay tick), not `GetTick()`.
2. **NETMENU gather**: walk `max(published, sim)` history; fill holes via `syNetInputTryGetLocalWireResendFrame` (gameplay/transmitted); remove the early-return.
3. GapFilled→matching Strict goes through **`CommitRemoteConfirmedWire`** (ledger + promote/GGPO), not silent ring Store.
4. Universe-mismatch → input correction **clears** peer-symmetric reject live-cap.

## Verify

- Re-soak Android ↔ Linux stick release mid-air / jump mash (D=2).
- Expect release REPLACE/GGPO (`feel0_*` / `late_wire_completed_sim` / `GGPO … queued`) when host releases after peer predicted hold-last — matching TopN.
- No `rollback_epoch_hold` forever after `BASELINE_UNIVERSE_MISMATCH → input correction`.
