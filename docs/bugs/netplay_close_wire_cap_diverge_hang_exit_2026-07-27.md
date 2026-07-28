# Netplay — light Close raises frontier past wire; PEER_SNAPSHOT_DIVERGE suppressed forever → hang

**Date:** 2026-07-27
**Build:** netmenu (`SSB64_NETMENU=ON`)
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)
**Soak:** Android guest ↔ Linux host (both with wire-ready build), Dream Land, seed `891751718`
**Follow-on to:** [wire-ready coalesce](netplay_light_pred_cap_wire_ready_coalesce_2026-07-27.md)

## Symptom

First dual-wire-ready soak: `LIGHT_WIRE_READY` fires on both peers (263/430), `PRED_CAP`
essentially gone (0/2). Session still dies:

- Permanent SoftLip-edge fork from ~**391** (`figh` snap mismatch; world/rng/item agree;
  P1 status 10 vs 15 one tick after a span-1 light episode).
- FC@**961** `state_diverge=1`, inputs agree (`inp_local == inp_peer`), onset=449,
  `class=replay_determinism`.
- **Hang, not exit:** `RESIM_BASELINE_TIMEOUT` (seal rows missing) → state deepen
  exhausted → `PEER_SNAPSHOT_DIVERGE` — then **suppressed forever**:
  - Linux: `suppressed (resim_seal_wait)` — the seal wait itself never terminates.
  - Android: `suppressed (stick_absorb)` — absorb window re-arms from ongoing episodes.
  - ~1000 `try_begin_fail stage=commit_suppress_reload/fc_commit_failed` spam until
    session timeout `VS_SESSION_END` @964.

## Root cause (two contract gaps)

1. **Close/frontier not bound by the wire.** Begin clamps light targets to the
   contiguous remote-confirmed exclusive end (`LIGHT_WIRE_READY_*`), but Close
   (`CloseCorrectionEpisode` / `NoteEpisodeResimCompleted`) raised `resolved_through`
   to `completed_target` bounded only by the predicted-replay **watermark**. The
   watermark is per-replay state and can miss spans (reset between replays / partial
   refresh), letting the frontier protect ticks no wire ever confirmed →
   `CORRECTION_CLAMP_RESOLVED` promote-only loss of the late wire → unhealed physics
   fork that FC later reports with agreeing inputs.

2. **No progress criterion on the fail-closed exit.** `FailPeerSnapshotDiverge` runs
   only after deepen is exhausted, yet two context windows (`resim_seal_wait`,
   `stick_absorb`) could re-suppress it on **every** retransmitted peer baseline. For
   `inputs_agree ∧ state_disagree` (replay_determinism) no wait can heal — the wait
   itself is the hang.

## Fix

`PORT && SSB64_NETMENU`, `port/net/sys/netrollback.c`:

1. **`RESOLVED_THROUGH_WIRE_CAP`** — `syNetRollbackCapResolvedThroughForPredictedReplay`
   (both raise sites) additionally clamps a **light** episode's raise to
   `syNetRollbackLightCloseWireReadyExclusiveEnd(mismatch, correction_player)` — the
   identical contiguous remote-confirmed bound Begin uses. `player < 0` takes the MIN
   across all remote human slots (the frontier protects every player). Heavy sealed
   episodes exempt: peer seal rows are authoritative even when the local wire ledger lags.

2. **Diverge hang exit** — in `syNetRollbackFailPeerSnapshotDiverge`:
   - `replay_determinism` class (inputs agree through load) **never** defers to
     `resim_seal_wait` / `stick_absorb`; it falls through to the fail-closed abort
     (deepen already exhausted upstream). Pure resim policy — no absorb/SoftLip context.
   - `protocol` class may still defer (a pending input correction can heal), but only
     `SYNETROLLBACK_PEER_DIVERGE_SUPPRESS_BUDGET` (8) times per `load_tick`; after
     that the abort proceeds (`suppress budget exhausted`).
   - `BATTLE_SIM_HOLD`, camera-cosmetic, and stale-aggregate suppressions unchanged
     (classifications, not waits).

## Acceptance

- [ ] Re-soak dual wire-ready builds: `RESOLVED_THROUGH_WIRE_CAP` fires when a light
      close would outrun the ledger; no `CORRECTION_CLAMP_RESOLVED` promote-only loss
      after a light episode.
- [ ] If a state fork still occurs: FC recovery terminates via deepen or fail-closed
      `PEER_SNAPSHOT_DIVERGE class=replay_determinism` within the suppress budget — no
      multi-hundred-line `try_begin_fail` spam, no session-timeout hang.
- [ ] `suppressed (...) budget=N/8` visible at most 8× per load_tick for protocol class.

## Follow-on

Soak `4126729879`: hang moved to FC recovery on **divergent loads** (1057 vs 1047) —
never reached `PEER_SNAPSHOT_DIVERGE`. Fixed in
[netplay_fc_recovery_divergent_load_hang_2026-07-27.md](netplay_fc_recovery_divergent_load_hang_2026-07-27.md).
