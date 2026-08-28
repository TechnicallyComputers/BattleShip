# Netplay — per-tick correction treadmill reverts the grab connect (coalescing is a dead end)

**Date:** 2026-08-26, revised 2026-08-28
**Build:** netmenu (`SSB64_NETMENU=ON`)
**Status:** ROOT CAUSE CONFIRMED — first fix attempt reverted, see "Why coalescing is closed"
**Related:** [capture flag hash-blind](netplay_capture_goto_pulled_wait_hash_blind_2026-08-22.md), [light absorb coalesce onset delay](netplay_light_absorb_coalesce_onset_delay_2026-07-27.md)

## Symptom (reproduces on whichever peer is predicting)

A grab connects on both peers, completes on the input owner, and dies on the peer
predicting that input. `grab_setstatus` on the failing peer:

```
495 p0 15->166   ftCommonCatchSetStatus
500 p0 166->167  ftCommonCatchPullProcCatch          <- connects
500 p1 10->171   ftCommonCapturePulledProcCapture
510 p0 166->10   ftCommonWaitSetStatus               <- from=166 again
```

**No `167->166` transition exists anywhere in the trace.** Nothing in the game reverted
the connect — a snapshot restore did. The owner meanwhile runs `167->168`, `171->172`,
`168->170`, `172->186` and throws normally.

2026-08-28 reproduced this with the peers **swapped** (Android predicting, 25 resims;
Linux owning, 1), confirming it follows the predicting role, not the platform.

## Root cause

The predicting peer stacks short episodes over the grab. 2026-08-28, around the connect
at tick 500:

```
resim load=491 mismatch=492 target=493
resim load=494 mismatch=495 target=497
resim load=499 mismatch=500 target=501     <- reloads before the connect
resim load=504 mismatch=505 target=506
resim load=500 mismatch=501 target=509     <- the deep one, last
```

Each short episode reloads a snapshot from before `ftCommonCatchPullProcCatch` ran and
replays one or two ticks — never enough to re-establish the connect — so `166->167` is
undone repeatedly and the grab cannot reach CatchWait.

## Why coalescing is closed (first attempt, reverted)

The obvious fix is to fold that stream into one episode. The machinery exists
(`syNetRollbackStickAbsorbCoalesceWaiting`), and a sliding absorb window was implemented
to drive it (commit `f0cb67e`). **It was a no-op, and has been reverted.**

`syNetRollbackStickAbsorbCoalesceWaiting()` returns FALSE unconditionally when light input
episodes are enabled:

```c
if (syNetRollbackLightInputEpisodesEnabled() != FALSE)
{
    return FALSE;   /* "absorb still arms (peer-cap / epoch ignore) but never delays Begin" */
}
```

So the absorb window never delays a Begin in this configuration; extending its deadline
changed nothing about episode timing. Worse, the window *is* still consulted for peer-cap
and epoch-ignore, so lengthening it was an unevidenced behaviour change on paths unrelated
to the bug — the reason for the revert rather than leaving it in as harmless.

That gate is deliberate and soak-justified (`netplay_light_absorb_coalesce_onset_delay_2026-07-27.md`):
delaying the Begin by ~3 ticks let SoftLip run on stale Wait geometry while the owner had
already Dashed, producing a permanent cam/pos fork and an FC `replay_determinism` failure
at tick 961 with inputs agreeing. **Delaying corrections for light episodes is known to
cause a worse bug than the one it fixes.**

## What is actually left

Not "reduce the number of episodes" — that door is closed. The real question is narrower:

> During the replay of tick 500, why does `ftCommonCatchPullProcCatch` not re-connect?

Both peers execute that tick from a matching snapshot. The owner connects; the predicting
peer, replaying the same tick, does not.

Coverage already ruled out — all of these are saved *and* restored:

| state | evidence |
|---|---|
| `search_gobj` (grab collision target) | saved `netrollbacksnapshot.c` 9083/9088, restored 11153 |
| `catch_gobj` / `capture_gobj` | bidirectional rebind at 7800–7831 |
| `capture.is_goto_pulled_wait` | folded into the cross-peer hash 2026-08-22 |
| `gobj->anim_frame` | saved 9179/9181, restored 9208/9210, hashed both blob and cross-peer |

So the connect's *inputs* survive the snapshot. The next step is to instrument
`ftCommonCatchPullProcCatch` itself — log `search_gobj`, the catch collision mask, and the
guard conditions on every pass including replay — and compare the replayed tick 500 on the
predicting peer against the owner's live tick 500. That distinguishes "collision never
re-detected" from "detected but rejected by a guard".
