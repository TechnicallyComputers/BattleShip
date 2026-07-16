# Baseline universe mismatch: state-vs-input routing

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-13  
**Session:** soak1 `1160137450` seed `1744440090` (Android client ↔ Linux host, jump mash)

## Symptom

Drift scan **PASS** (no FC `state_diverge`, no `LOAD_HASH_DRIFT`, synctest OK). After two clean stick GGPOs (@420, @603), a third feel-0 onset @742 opens an episode; baselines at **load 741** disagree on **figh only** (world/rng/map match). Session hangs then host `VS_SESSION_END`:

- `BASELINE_UNIVERSE_MISMATCH → input correction mismatch=742`
- FSM `AwaitingBaseline → Live`
- `try_begin_fail commit_target_not_wider`
- `BASELINE_ECHO_RETRY … snapshot_not_ready`

## Root cause (architectural)

Wrong **mismatch class** routing — not the jump/PASS physics seed itself:

| Class | Meaning | Correct recovery |
|-------|---------|------------------|
| **Input-poisoned load** | `FindEarliestInputMismatch` ≤ `load_tick` | GGPO / deferred input correction (~1032 soak) |
| **State diverge at load** | Inputs agree through `load_tick`; earliest mismatch is after load (or none) | Deeper load / peer baseline resync |

The hang path treated a **post-load** stick mismatch (742 > 741) as proof the load band was input-poisoned, then:

1. Re-queued the same GGPO span already in flight.
2. `ResetBaselineResimState` → episode **Live** while preemptive live-cap still armed.
3. Frozen sim / session end.

Same policy smell as [`netrollback_fc600_recovery_storm_2026-05-19.md`](netrollback_fc600_recovery_storm_2026-05-19.md) (FC figh split → input correction) and the midmatch doc’s incomplete “no mismatch → deepen” rule (a *later* mismatch still must not poison the load).

### Audit — easy-win gaps closed in the same change

1. **`AbortToInputCorrectionFromUniverseMismatch`** — invented `mismatch=load+1` when no input mismatch; abandoned gate to Live.
2. **`AbortPendingResimForBaselineMismatch`** — on universe mismatch outside FC recovery, **always** bounced to (1) and **never** reached deeper-load fallthrough.
3. **`seal_authority_mismatch`** — sealed inputs + figh-only → still called (1) instead of deeper load (contrary to [`netrollback_fighter_midmatch_drift_2026-05-20.md`](netrollback_fighter_midmatch_drift_2026-05-20.md) recovery policy).

### Still open (not this fix)

- Silent figh fork in the 721–741 jump/PASS window (physics / snapshot fidelity) — recovery no longer hangs on the wrong class, but peers may still `PEER_SNAPSHOT_DIVERGE` after deeper exhausted.
- FC `TryFcStateRecoveryDeepen` → `AbortPending` InFlight no-op for **input-poisoned** FC cases (separate; PreferStateRecovery still gates that path).

## Fix (`port/net/sys/netrollback.c`)

1. **`syNetRollbackUniverseMismatchInputPoisonedAtLoad`** — TRUE iff earliest published↔remote value mismatch exists and `mismatch <= load_tick`.
2. **`AbortToInputCorrectionFromUniverseMismatch`** — if not poisoned → log `→ state deepen`, clear live-cap, `AbortPending` (keep pending armed); only poisoned rows queue GGPO.
3. **`AbortPendingResimForBaselineMismatch`** — universe mismatch: poisoned → GGPO (or FC deepen); else **fall through** to negotiated deeper load / peer resync.
4. **`seal_authority_mismatch`** — call `AbortPending` (deeper) instead of `AbortToInputCorrection`.

## Verify

Re-soak Android↔Linux jump mash (Dream Land PASS):

- On figh-only baseline split with stick GGPO after load: expect `→ state deepen` / `universe state diverge … → deeper`, **not** `→ input correction` then `AwaitingBaseline → Live` freeze.
- Input-poisoned loads (mismatch ≤ load) still log `→ input correction`.
- Deeper exhausted still fail-closed (`PEER_SNAPSHOT_DIVERGE` / map-only path) rather than hang.

Related: [`netplay_baseline_universe_mismatch_ignored_2026-07-12.md`](netplay_baseline_universe_mismatch_ignored_2026-07-12.md), [`netplay_feel0_send_before_sample_release_skew_2026-07-13.md`](netplay_feel0_send_before_sample_release_skew_2026-07-13.md), [`netrollback_fc600_recovery_storm_2026-05-19.md`](netrollback_fc600_recovery_storm_2026-05-19.md).
