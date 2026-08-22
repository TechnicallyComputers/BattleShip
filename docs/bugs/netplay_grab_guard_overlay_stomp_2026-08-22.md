# Netplay — grab aborts after a rollback, then grabs whiff for seconds (guard/catch union stomp)

**Date:** 2026-08-22
**Build:** netmenu (`SSB64_NETMENU=ON`)
**Status:** ROOT CAUSE NARROWED — instrumentation landed, stomping writer not yet named
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
