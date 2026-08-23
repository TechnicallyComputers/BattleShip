# Netplay — the flag that decides whether a grab holds was hash-blind

**Date:** 2026-08-22
**Build:** netmenu (`SSB64_NETMENU=ON`)
**Status:** FIX IMPLEMENTED — re-soak pending
**Soak:** Android guest ↔ Linux host, 2356 ticks, D=2, witness armed
**Related:** [grab overlay investigation](netplay_grab_guard_overlay_stomp_2026-08-22.md), [damage hitstun scrub](netplay_damage_hitstun_statusvars_scrub_2026-07-20.md) (same failure class)

## Symptom

A grab connects on the input owner's peer and fails on the peer predicting that
input. The grabber's status sequence splits:

| tick | Android (owner) | Linux (predicting) |
|------|-----------------|--------------------|
| 2340 | 166 Catch | 166 Catch — `figh` **match** |
| 2341 | 167 CatchPull | *(no accepted row)* |
| 2343 | 168 CatchWait | *(no accepted row)* |
| 2347–2353 | 170 ThrowB | **166 Catch** — `figh` diverges |

Grabs by the *locally owned* fighter succeed on both peers; only grabs whose
input is being predicted break. The divergence self-heals after ~7 ticks, but
the grab is already lost.

## What the evidence ruled out

For the resim spanning the grab (`load=2339 mismatch=2340 target=2355`, epoch
123 — **run identically on both peers**):

- **Inputs are byte-identical.** Published stick/buttons for the grabber match on
  every tick 2338–2356 (`sx`, `sy`, `btn`). Only the `pred=` tag differs (1 on
  the predicting peer, 0 on the owner), as expected.
- **The world replays identically.** `map_hash_save` (`hash_map` + `kin`) matches
  on *every* replayed tick 2339–2356.
- **No union stomp.** After migrating `capture.is_goto_pulled_wait` to
  `ftStatusVarsCapture()`, the witness watched it all session and reported
  **zero** stomps on either peer — so the flag is not being aliased.
- **Not the coupled-pointer scrub.** `ftCommonCaptureStatusVars` is a single
  `sb32` at union offset 0; the scrub only clears GObj pointers at offset 0x08+.

Same inputs, same world, divergent fighter state.

## Root cause

`syNetSyncHashFighterSlot*` folds `status_vars` **selectively, per status**:
JumpAerial `vel_x`/`drift`, Damage `hitstun_tics`/`is_knockback_over`, CatchWait
`throw_wait`, Landing `is_allow_interrupt`, Squat, Turn, KneeBend, TaruCann,
Twister, ItemThrow, FallSpecial, CaptureYoshi.

**The common `capture` overlay is folded nowhere.** The only "Capture" fold is
`CaptureYoshi` (the egg). So `capture.is_goto_pulled_wait` — the sole gate on

```c
/* ftCommonCapturePulledProcPhysics */
if ((fp->status_id == nFTCommonStatusCapturePulled) &&
    (ftStatusVarsCapture(fp)->is_goto_pulled_wait != FALSE))
    ftCommonCaptureWaitSetStatus(fighter_gobj);   /* 171 -> 172 */
```

i.e. **whether a grab holds at all** — never entered the fighter hash.

That is why `figh` matched at tick 2340 while the peers were already destined to
disagree: the deciding bit was invisible to the comparison. The skew only became
observable at 2347 once it had produced a *status* difference — precisely the
shape `netsync.c`'s own DamageFlyN comment describes:

> only `is_hitstun` bool was folded (full hash), so counter skew stayed
> **hash-blind** until status change → PEER figh@1126

and precisely why the witness found nothing: nothing was aliasing the flag, and
nothing was watching it.

## Fix

Fold it, for the two statuses where it is live:

```c
if ((fp->status_id == nFTCommonStatusCapturePulled) ||
    (fp->status_id == nFTCommonStatusCaptureWait))
{
    h = syNetSyncFnvAccumulateU32(h, (u32)(ftStatusVarsCapture(fp)->is_goto_pulled_wait != FALSE));
}
```

Same remedy as the DamageFlyN fix: this does not stop the skew from occurring, it
makes it **visible at the tick it happens** so the existing rollback machinery
corrects it, instead of letting an unhashed bit silently decide the grab.

## Verification

- netmenu + offline Debug builds compile and link clean.
- Re-soak expectation: the grab divergence should now surface as a normal
  single-tick `figh` mismatch that rollback heals at the tick it occurs, rather
  than a 7-tick status split ending in a lost grab.

## Note

This closes the *detection* gap. If re-soak shows the flag still skewing (now
visible as an immediate mismatch), the follow-up question is why the predicting
peer's replay fails to set it — the write lives at `ftcommoncatch2.c:33`, which
already routes through the accessor and therefore through the witness.
