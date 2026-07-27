# Hold-last micro-skip + completed-sim invent → PEER (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)  
**Soak:** soak1 session `740113729` seed `3390068569` — Android client (lp=1) ↔ Linux host (lp=0)  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Replay:** `20260726_130012.ssb64r`  
**Bucket:** `REPLAY_DETERMINISM` / `PEER_SNAPSHOT_DIVERGE` (`agree_through_load=1`)

## Symptom

Post [soft onset floor/ahead](netplay_hold_last_soft_onset_floor_ahead_peer_2026-07-26.md): soft-onset invent / INTENT_OVERRIDE / seal-exhaust cleared. Still PEER deepen @**408** and @**439** (figh-only).

| Check | Result |
|-------|--------|
| Soft onset invent | Cleared (verbatim current-tick) |
| INTENT_OVERRIDE / dash-gate REPLACE | Cleared |
| NetSync | ~36 protocol resims; PEER deepen @408 / @439 |
| Fork surface | P1 JumpAerial — Linux first-pass hold_last vs Android owner mag |

## Timeline (PEER@408 seed)

| Step | Detail |
|------|--------|
| First-pass | Linux P1 hold_last `(-69,-7)` vs Android owner `(-66,-7)` |
| Wire arrives | Same-intent ±3 → `GGPO stick replace skipped class=micro_stick` |
| Promote | Wire written into History **without** rewind → silent history rewrite |
| Smell | `hold_last_smash_flip_ahead` attributing later same-sign decay onto completed ticks; `REMOTE_PUBLISH_SKIP reason=hold_last_completed_sim` after computing ahead flip |

## Root cause

Three layers of the input-contract path:

1. **No tick-wire mag follow** — same-intent tick row `(-66)` existed but hold_last kept last_confirmed `(-69)` through first-pass.
2. **Micro-skip on predicted** — completed-sim ±micro deadband treated predicted hold_last like confirmed noise and skipped GGPO, so wire never replayed physics.
3. **Completed-sim invent skip** — `hold_last_completed_sim` blocked promote after FillHoldLast applied a tick-wire opposite-intent flip, leaving stale published smash; ahead same-sign dash decay was also treated as “flip.”

## Fix (`port/net/sys/netinput.c`)

| Layer | Change |
|-------|--------|
| FillHoldLast | Same-intent tick-wire mag follow (`hold_follow` / `smash_follow`) |
| Ahead flip | Opposite `SameAnalogIntent` only (drop ahead same-sign `DashGateDisagreeX`) |
| Micro-skip | Do **not** micro-skip when `old_frame->is_predicted` — predicted→wire always rewinds |
| Promote | Exception to `hold_last_completed_sim`: resolved matches **tick** row + `StickSealIntentDisagree` vs published → StoreFrame + `QueueOrWidenStickCorrection` |

## Acceptance

Matched Android APK + Linux binary:

- No `GGPO stick replace skipped class=micro_stick` when old row is predicted hold_last
- Tick wire present → first-pass follow (no `-69` vs `-66` fork before sim)
- No ahead same-sign dash “flip” onto completed ticks
- Soft GGPO OK; 0× deepen PEER from this class

Rebuild desktop **and** Android APK before re-soak.

## Related

- [`netplay_hold_last_soft_onset_floor_ahead_peer_2026-07-26.md`](netplay_hold_last_soft_onset_floor_ahead_peer_2026-07-26.md) — prior soak family (cleared)
- [`netplay_input_contract_micro_deadband_onset_peek_2026-07-17.md`](netplay_input_contract_micro_deadband_onset_peek_2026-07-17.md) — micro deadband origin
- [`netplay_feel0_release_deadband_skips_ggpo_2026-07-12.md`](netplay_feel0_release_deadband_skips_ggpo_2026-07-12.md) — release still rewinds
- [`netplay_ness_pk_hold_skip_durable_aim_fork_2026-07-15.md`](netplay_ness_pk_hold_skip_durable_aim_fork_2026-07-15.md) — why live Hold still invents durable rows
