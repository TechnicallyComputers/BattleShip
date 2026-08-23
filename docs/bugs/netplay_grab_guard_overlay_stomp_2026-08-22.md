# Netplay — grab aborts after a rollback, then grabs whiff for seconds (guard/catch union stomp)

**Date:** 2026-08-22
**Build:** netmenu (`SSB64_NETMENU=ON`)
**Status:** ROOT CAUSE IDENTIFIED — the guard-overlay stomps were a false lead; see "Correction"
**Soak:** Android guest ↔ Linux host, ~3590 ticks, D=2, `SSB64_NETPLAY_STATUSVARS_WITNESS=1`
**Related:** [FTStatusVars overlay map](../refactor/ftstatusvars_overlay_map_2026-06-02.md), CLAUDE.md directive 6

## Symptom

A grab connects, a rollback lands mid-grab, and the grab **aborts instead of replaying**.
Afterwards the grabber cannot grab at all for several seconds — every attempt runs the
full 15-tick whiff animation — until they land a hit, after which grabs work again.

Grab state machine: grabber `166 Catch → 167 CatchPull → 168 CatchWait → 169/170 Throw`;
victim `171 CapturePulled → 172 CaptureWait → 186 ThrownCommon`. A successful grab shows a
**4–5 tick** Catch run; a whiff shows the full **15 tick** run and returns to Wait.

Soak 2026-08-22, p0's grab at tick 3274 — peers split on the outcome:

| | Linux | Android |
|---|---|---|
| 3279 | 167 CatchPull | 167 CatchPull |
| 3280 | 168 CatchWait | **166 Catch** (aborted) |
| 3286 | 169 ThrowF | whiff → 152 |

`figh` matches through 3279, diverges 3280–3288, reconverges at 3289 — a clean 9-tick
divergence that self-heals but leaves the grab consumed differently on each side. Then
p0 whiffs at 3347, 3374 and 3400 on **both** peers (they agree — the bad state is shared),
before recovering.

An earlier soak showed the same shape with the peers reversed (Linux aborting, Android
completing) and **nine** consecutive whiffs between the glitch and recovery, so the failing
side is simply whichever peer rolls back.

Both glitches share an input signature: the grab **button** was predicted correctly
(`btn=0x0010`) while the **stick** was mispredicted —
`old_sx=30 sy=3 → new_sx=0 sy=0`, `old_sx=-53 sy=-16 → new_sx=0 sy=0`. Stick direction
selects the throw, so a mid-grab stick correction forces a rollback exactly when the grab
is resolving.

## Root cause (narrowed)

The statusvars witness reports **union stomps on precisely the grab overlays**, identically
on both peers:

| accessed | expected | count |
|----------|----------|-------|
| guard | catchmain | 16 |
| guard | catchwait | 12 |
| guard | throwf | 10 |

and the stomp ticks land on grab ticks (907/911 for the 898 grab; 1122/1141 for the 1116
grab; 1412/1441 for the 1404 grab). Code is touching `status_vars.common.guard` while a
catch overlay is live — the exact class CLAUDE.md directive 6 describes, amplified by
rollback because snapshots `memcpy` the whole blob with no overlay tag.

Ruled out by inspection:

- The snapshot **save** path is gated `(fp->is_shield) || (status in [GuardStart, GuardEnd])`,
  and the **apply/eject** paths go through `syNetRbSnapFighterGuardEffectUnionOwned()`.
- `nFTCommonStatusGuardStart..GuardEnd` is **[152, 155]**; Catch/CatchPull/CatchWait/
  ThrowF/ThrowB are **166–170**, outside it.
- Logged `is_shield = 0` on every sampled catch-status tick.

So the gated snapshot paths are not the writer. Some other caller of `ftStatusVarsGuard()`
runs during catch — `netrollbacksnapshot.c` holds 91 of the ~120 guard-overlay accesses in
the tree, but the witness cannot distinguish them because it never recorded a call site.

## Instrumentation added

`ftStatusVarsNoteAccess()` now passes `__builtin_return_address(0)` — the accessor's caller
— to a new `syNetplayStatusVarsWitnessNoteAccessFrom()`, and the stomp line gains
`caller=%p`. The original `…NoteAccess()` remains as a NULL-caller wrapper (MSVC keeps it;
the builtin is GCC/Clang only). Diagnostics only, `PORT && SSB64_NETMENU`.

**Follow-up the same day:** a raw address proved useless in practice. The binary is PIE
and the log carried no ASLR base, so the three distinct `caller=` addresses from the first
instrumented soak (`0x55c121849be5` ×78, `0x55c121849c64` ×2, `0x55c12186a309` ×6) could
not be matched against `build/BattleShip` — neither by low-12-bit page offset nor by exact
inter-address deltas.

The witness now resolves the caller **in-process** with `dladdr()` and logs
`caller=<symbol>+0x<off> (<addr>)`. The witness runs on the normal sim path, not in a
signal handler, so `dladdr` is safe there. The build already links
`-Wl,-export-dynamic` (CMakeLists.txt:1373) with ~82k entries in `.dynsym`, so decomp and
port functions resolve by name with no post-processing.

`_GNU_SOURCE` is defined at the top of the witness TU because glibc hides `dladdr` /
`Dl_info` behind `__USE_GNU`, and the feature macro must precede the first libc header.

Caveat worth knowing when reading the output: `ftStatusVarsNoteAccess()` and the
`ftStatusVars*()` accessors are both `static inline`, so `__builtin_return_address(0)`
yields the return address of the *enclosing real function* — i.e. the log names the
**caller of the function that touched the overlay**, one level above the access itself.
That is still enough to identify the writer.

## Next step

Re-soak with `SSB64_NETPLAY_STATUSVARS_WITNESS=1`, reproduce one grab, and symbolize the
`caller=` on the first `expected=catchmain|catchwait|throwf` stomp. That names the writer,
after which the fix follows directive 6: migrate that call site to the correct accessor
(or gate it on overlay ownership the way `syNetRbSnapFighterGuardEffectUnionOwned()` does)
— **not** a new out-of-union mirror.

## Verification

- netmenu + offline Debug builds compile and link clean.
- Offline is unaffected: the whole path is `#if defined(PORT) && defined(SSB64_NETMENU)`.


---

## Correction (2026-08-22, instrumented soak)

`dladdr` resolution named the callers, and **the guard-overlay stomps are not the bug**:

| accessed ← expected | caller | count |
|---|---|---|
| guard ← squat / turn / landing | `ftCommonCatchCheckInterruptCommon` | 356 |
| guard ← catchmain | `ftMainProcUpdateInterrupt` | 32 |
| guard ← catchmain | `ftCommonCatchCheckInterruptCommon` | 12 |
| guard ← throwf / catchwait | `ftMainProcUpdateInterrupt` | 4 |
| guard ← catchmain | `syNetplayGuardGrabDiagLogGuardDropCatch` | 2 |

368 of 408 trace back to `syNetplayGuardGrabDiagLogCore()` — **this diagnostic's own
`ftStatusVarsGuard(fp)->release_lag` / `->is_release` reads**, which ran unconditionally
for every logged event. They are *reads*, so they corrupt nothing; they merely flooded the
witness and printed aliased bytes (the same idle Wait fighter showed `release_lag=178` on
Linux and `-2` on Android). Fixed: the diag now reports `-1` unless the guard overlay is
actually live.

## Actual mechanism: the grab-cancel edge lands in the wrong status during resim

Soak 2026-08-22, p1's grab at tick 1218/1220:

| | Android (input owner) | Linux (predicting peer) |
|---|---|---|
| 1220–1222 | 166 Catch | 166 Catch |
| 1223 | 167 CatchPull | 167 CatchPull |
| 1225 | **168 CatchWait** | **166 Catch** (aborted) |
| 1234 | 170 ThrowB | whiff |

The `GuardGrabDiag` R/Z **release** edge (`rel=0xA010`) arrives at tick 1226 on both peers:

```
android  event=r_edge tick=1226 player=1 status=168 rel=0xA010 resim=0   <- live, CatchWait
linux    event=r_edge tick=1226 player=1 status=166 rel=0xA010 resim=1   <- resim, Catch
```

Releasing Z during **Catch** cancels the grab; during **CatchWait** it does not. The
predicting peer replays p1 one grab-step behind, so the same release edge cancels a grab
that had already connected.

Corroborating asymmetry: every p1 `r_edge` up to tick 998 is logged `resim=1` on Linux and
`resim=0` on Android — the predicting peer re-simulates the grab owner's input edges
repeatedly, the owner runs them once. Both glitches also mispredicted the grabber's
**stick** while predicting the grab **button** correctly, and stick direction selects the
throw.

So the fix belongs in **when the release edge is evaluated relative to the replayed status
timeline**, not in overlay ownership. The union stomps were noise from the instrument.


## Narrowed further: the victim drops out, and its gate was uninstrumented

Deeper trace of the same tick-1220 grab. The divergence is on the **victim**, not the
grabber, and on the victim's *own* peer:

| p0 (victim) | Android (grabber's peer) | Linux (victim's own peer) |
|---|---|---|
| 1223 | 171 CapturePulled | 171 CapturePulled |
| 1224 | 171 CapturePulled | *(no state)* |
| 1225 | **172 CaptureWait** | **10 Wait** — fell out |
| 1234 | 186 ThrownCommon | whiff |

The grabber's `167 CatchPull -> 166 Catch` fallback is a *consequence*: the victim left
the hold, so the coupling collapsed.

`ftCommonCapturePulledProcPhysics` gates that transition on exactly one flag:

```c
if ((fp->status_id == nFTCommonStatusCapturePulled) &&
    (fp->status_vars.common.capture.is_goto_pulled_wait != FALSE))
    ftCommonCaptureWaitSetStatus(fighter_gobj);   /* 171 -> 172 */
```

Of that flag's four accesses, **only one used the accessor**:

| site | access | witness sees it |
|---|---|---|
| `ftcommoncatch2.c:33` write TRUE | `ftStatusVarsCapture()` | yes |
| `ftcommoncapturepulled.c:57` read | raw union | **no** |
| `ftcommoncapturepulled.c:168` write FALSE | raw union | **no** |
| `ftcommoncapturepulled.c:183` write FALSE | raw union | **no** |

So the witness was structurally blind to the field that decides whether a grab holds —
which is why it reported only diagnostic noise. All three raw accesses are now migrated to
`ftStatusVarsCapture()` per CLAUDE.md directive 6 (step C1, accessors). Behaviour is
identical; the difference is that a stomp on `capture.is_goto_pulled_wait` is now visible.

### Hypotheses eliminated (with evidence)

- **Input edge derivation during replay** — `syNetInputRollbackPrepareForResim()` already
  rewinds `sSYNetInputSlots[player].last_published` to `resim_start_tick - 1`, so
  `pressed`/`released` chain correctly through replay. Not the bug.
- **Coupled-pointer scrub polarity** — `syNetRbSnapClearCoupledGObjPointers…` scrubs
  `catch_gobj` when `is_catch_or_capture == FALSE` and `capture_gobj` when it is TRUE.
  That looks inverted but matches vanilla's validity model: `ftMainProcPhysicsMapCapture`
  treats *(capture_gobj, FALSE)* as the victim and *(catch_gobj, TRUE)* as the grabber, so
  both scrubs clear genuinely stale links. Not the bug.
- **guard/catch union stomps** — the grab diagnostic's own reads (see Correction above).

### Next

Re-soak with `SSB64_NETPLAY_STATUSVARS_WITNESS=1` and land a grab. If
`capture.is_goto_pulled_wait` is being stomped, the witness will now name the writer via
`caller=`. If no stomp appears, the flag is being lost in snapshot save/restore rather than
aliased, and the next place to look is the `capture` overlay's coverage in
`netrollbacksnapshot.c`.
