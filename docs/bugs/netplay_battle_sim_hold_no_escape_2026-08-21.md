# Netplay — BATTLE_SIM_HOLD armed with no escape: match freezes instead of ending

**Date:** 2026-08-21
**Build:** netmenu (`SSB64_NETMENU=ON`)
**Status:** FIX IMPLEMENTED — re-soak pending
**Soak:** Android guest ↔ Linux host, 8375 ticks, D=2
**Related:** [walkback floor](netplay_light_frontier_load_fail_walkback_floor_2026-08-21.md) (the failure that armed the hold), [light exclusive-frontier poison](netplay_light_exclusive_frontier_poison_load_2026-07-28.md)

## Symptom

After a resim load failure the battle froze permanently. The peer stayed
connected, frames kept rendering, and nothing ever ended the match — the player
had to quit the app.

```
NetRollback: BATTLE_SIM_HOLD armed sim=8382 load_tick=8381 reason=resim_load_fail fail_count=2
NetRollback: BATTLE_SIM_HOLD blocking battle advance sim=8382 resim=0 peer_vs_active=1 rollback_active=1
VS: battle update frozen (BATTLE_SIM_HOLD) sim=8382 peer_vs_active=0 resim=0
```

For the rest of the session, across both peers:

| log line | count |
|---|---|
| `load_fail battle exit` | **0** |
| `resim_load_fail — tear down` | **0** |
| `BATTLE_SIM_HOLD cleared` | **0** |

## Root cause

The escape machinery was complete and correct — **it was simply never armed by the
path that fires in practice.**

`syNetRollbackPumpLoadFailBattleExit()` is called every frame from the hold branch
of `scvsbattle.c` (it and `syNetPeerUpdate` are the only things still running while
held). It performs the scene retarget — automatch abort to CSS, otherwise
`is_reset` + `ifCommonAnnounceEndMessage()`. Its preconditions were:

1. `sSYNetRollbackLoadFailBattleExitPending != FALSE`
2. `syNetRollbackIsBattleSimHoldActive() != FALSE`
3. `syNetPeerIsVSSessionActive() == FALSE`

Of the five `syNetRollbackArmBattleSimHoldAfterLoadFail()` call sites, only some
also called `syNetRollbackRequestLoadFailBattleExit()`:

| arm site | requested exit? | tore down VS? |
|---|---|---|
| replay-gate blocked (`TryOpenResimReplayGate`) | yes | yes |
| `syNetRollbackOnPeerLoadFailAbort` | yes | n/a |
| **BeginResim: `ResolveLoadTickForSnapshot` fail** | **no** | **no** |
| **BeginResim: fidelity-walkback fail** | **no** | **no** |
| BeginResim aborted | yes | yes |

The two BeginResim load-fail sites — the ones that actually fire — armed the hold
and nothing else. Condition 1 was never satisfied, so the pump returned on its
first check every frame, forever.

Precondition 3 compounds it: even once the flag is set, the exit waits for VS to
go down. With a live peer that never happens on its own, so a hold taken while
`peer_vs_active=1` would still hang until the *other* player quit.

Because the hold blocks the battle sim, **no in-sim path can end the match** — the
pump is the only escape, so a missing flag is unrecoverable by construction.

## Fix

`port/net/sys/netrollback.c`:

1. **The exit is armed by the hold, not by callers.**
   `syNetRollbackArmBattleSimHoldAfterLoadFail()` now calls
   `syNetRollbackRequestLoadFailBattleExit()` itself and records the load tick.
   No call site can forget it; sites that already requested it are idempotent.
2. **Escape watchdog for a live peer.** While held with VS still active, the pump
   counts frames and after `SYNETROLLBACK_BATTLE_SIM_HOLD_TEARDOWN_FRAMES` (120,
   ~2 s @ 60 Hz) calls `syNetRollbackStopVsSessionForLoadFail(..., "battle_sim_hold_watchdog")`
   once. That notifies the peer with the load-fail flag and stops VS, so
   precondition 3 becomes reachable and both peers exit cleanly instead of one
   waiting on the other. Logged as `BATTLE_SIM_HOLD escape watchdog`.
3. **Re-arm on a stale hold.** If the pump ever sees the hold active with no
   pending exit, it re-requests and logs `BATTLE_SIM_HOLD escape re-armed`.
4. Watchdog state resets with the hold and at `Init` / `StartVSSession`.

`syNetRollbackStopVSSession` deliberately does **not** clear the hold ("survives
StopVSSession until scene exit"), so tearing down from the watchdog leaves the
hold intact for the pump to consume on the next frame — which is what performs
the retarget.

## Scope — what this does and does not do

This makes a load-fail hold a **clean, surfaced match end** instead of a hang. It
is *not* a mid-match state recovery: it does not resync from the peer or resume
the frozen sim. Real recovery happens one layer earlier — the deeper-load rescue
in [the walkback-floor fix](netplay_light_frontier_load_fail_walkback_floor_2026-08-21.md),
which now runs before the hold can arm. The hold is the genuine last resort, and
last resort should end the match, not freeze it.

## Verification

- netmenu + offline Debug builds compile and link clean, no new warnings; offline
  still exports only the three `syNetRollback*` stubs.
- Re-soak: a hold should now be followed within ~2 s by
  `BATTLE_SIM_HOLD escape watchdog` → `resim_load_fail — tear down` →
  `load_fail battle exit` → `BATTLE_SIM_HOLD cleared`, and land back at CSS /
  results rather than a frozen battle.


---

## Follow-up (2026-08-25): the escape was starved, not broken

The watchdog above never ran. Soak 2026-08-25 armed a hold
(`FRONTIER_BEGIN_FLOOR load failed load_tick=1039`, `fail_count=3`) during a grab
(p0 `167 CatchPull`, p1 `171 CapturePulled`) and froze for ~6 s until the player
killed the app — yet the log shows **zero** `battle update frozen`,
`escape watchdog`, and `BATTLE_SIM_HOLD cleared` lines.

Cause: `syNetRollbackPumpLoadFailBattleExit()` — which owns the watchdog — is
called only from `scvsbattle`'s hold branch, and that branch sits *after* the
seal-wait defer:

```c
if (VS active && syNetRollbackShouldDeferInterfaceDuringResimWait())
{
    ...; syNetPeerUpdate(); return;               /* returns first */
}
if (syNetRollbackIsBattleSimHoldActive())
{
    ...; syNetRollbackPumpLoadFailBattleExit();   /* never reached */
}
```

With a hold armed *and* the episode in seal-wait, the defer branch returns every
frame and the escape never ticks. Two fixes:

1. **Ordering** — the hold check now precedes the seal-wait defer. A load-fail
   hold is terminal for the match, so it outranks presentation deferral.
2. **Unstarvable driver** — `PortPushFrame` also calls the pump each frame
   (netmenu-gated, no-op when no hold is armed). This escape has now been starved
   twice by control flow it does not own; driving it from the frame loop removes
   the class of failure rather than one instance.

## Related: the rescue and the frontier floor deadlock each other

The same soak shows the walkback rescue from
[the walkback-floor fix](netplay_light_frontier_load_fail_walkback_floor_2026-08-21.md)
working, then being undone:

```
RESIM_LOAD_FIDELITY_RETRY failed=1039 deeper=1038 mismatch=1039 attempt=1 (below episode floor — rescue)
FRONTIER_BEGIN_FLOOR mismatch=1039->1040 load=1038->1039 frontier=1040 target=1041
FRONTIER_BEGIN_FLOOR load failed load_tick=1039
BATTLE_SIM_HOLD armed sim=1041 load_tick=1039 reason=resim_load_fail fail_count=3
```

The rescue walks to 1038; `FRONTIER_BEGIN_FLOOR` clamps back to 1039 because the
shared frontier is 1040; 1039 fails again (`LOAD_HASH_DRIFT … reason=fighter_mismatch`).
A genuine deadlock between two floors, tracked separately — the escape work here
only ensures it ends the match cleanly instead of hanging.
