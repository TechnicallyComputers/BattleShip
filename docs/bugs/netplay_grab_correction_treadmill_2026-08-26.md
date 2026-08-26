# Netplay — per-tick correction treadmill reverts the grab connect

**Date:** 2026-08-26
**Build:** netmenu (`SSB64_NETMENU=ON`)
**Status:** FIX IMPLEMENTED — re-soak pending
**Soak:** Android guest ↔ Linux host, 658 ticks, grab at tick 495
**Related:** [capture flag hash-blind](netplay_capture_goto_pulled_wait_hash_blind_2026-08-22.md), [frontier/rescue deadlock](netplay_frontier_floor_vs_rescue_deadlock_2026-08-25.md)

## Symptom

A grab connects on both peers, completes on the input owner, and dies on the peer
predicting that input. Afterwards the grabber whiffs repeatedly for several seconds.

`grab_setstatus` (added for this hunt) shows both peers agreeing through the connect:

```
490 p1 16->166   ftCommonCatchSetStatus
495 p1 166->167  ftCommonCatchPullProcCatch          <- connects
495 p0 10->171   ftCommonCapturePulledProcCapture
```

The owner then completes it — `167->168`, `171->172`, `168->170` throw, `172->186`.
The predicting peer logs **nothing** between 495 and 505, then reports `t=505 p1 from=166`.

**There is no `167->166` transition anywhere in the trace.** Nothing in the game
reverted the connect — a snapshot restore did.

## Root cause

Resim counts for the same 658-tick session:

| | resims |
|---|---|
| input owner (Android) | **1** — `load=493 mismatch=494 target=508` |
| predicting peer (Linux) | **20** — including `494→497`, `496→500`, `499→501`, `500→503` **before** the identical `493→508` |

The owner has nothing to correct (`reason=QUEUE` count 0); its single episode came from
the peer's symmetric notify. The predicting peer queued 19 corrections, merged 10, and
still opened 20 episodes — four of them stacked directly over the grab. Each reloads a
snapshot from before `ftCommonCatchPullProcCatch` connected and replays one or two ticks,
which is never enough to re-establish the connect, so `166->167` is repeatedly undone and
the grab can never reach CatchWait.

The coalescing that should prevent this already exists — the stick-absorb window — but it
never engaged (`stick_absorb` count 0). It is armed once at episode completion and, for
light input episodes, sized to a flat **2 ticks**:

```c
if (syNetRollbackLightInputEpisodesEnabled() != FALSE)
    absorb_window = SYNETROLLBACK_LIGHT_EPISODE_ABSORB_WINDOW;   /* 2 */
```

with the rationale, in-tree, that longer fixed windows "only added correction latency".
That folds a same-burst REPLACE pair into one span, but a *sustained* per-tick correction
stream — which is exactly what a grab produces, since the remote stick changes every tick
while choosing a throw — outruns it. Each correction lands after the window expired and
opens its own episode.

## Fix

Make the window **slide** instead of raising it: `syNetRollbackStickAbsorbExtendOnMerge()`
is called from the `MERGE_WIDEN` / `MERGE_DEEPEN` paths and pushes
`sSYNetRollbackStickAbsorbUntilSim` to `sim + 2` on each merge, capped at
`armed_from + SYNETROLLBACK_LIGHT_EPISODE_ABSORB_MAX` (12).

- A **sustained** stream folds into one Begin — the treadmill disappears.
- An **isolated** correction still Begins after the same 2 ticks as before, so the latency
  the flat window was protecting is unchanged for the common case.
- The cap bounds the worst case, so a pathological stream cannot defer a correction
  indefinitely.

Logged as `STICK_ABSORB_SLIDE tag=… sim=… until=X->Y armed_from=… ceiling=…`.

## Verification

- netmenu + offline Debug builds compile and link clean; both call sites and the
  definition are inside `#if defined(SSB64_NETMENU)`.
- Arithmetic unit-tested standalone: extends inside the window, keeps sliding on a
  sustained stream, clamps at the ceiling, no-ops once expired, never shortens, no-ops
  when unarmed.
- Re-soak expectation: predicting-peer resim count drops toward the owner's, span-1
  episodes stacked over a grab disappear, and `STICK_ABSORB_SLIDE` appears where the old
  log showed consecutive `resim initial load` lines one tick apart.

## If this regresses input latency

The knob is the cap. `SYNETROLLBACK_LIGHT_EPISODE_ABSORB_MAX` bounds how long a sustained
stream can defer a Begin; lowering it trades coalescing for responsiveness. The base
window stays 2 either way, so isolated corrections are unaffected by that choice.
