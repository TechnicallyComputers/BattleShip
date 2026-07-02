# Netplay — Fox Firefox Hold/Travel truncated far short of intended duration (investigation aid)

**Date:** 2026-07-01 (travel-span tracer re-added 2026-07-02)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Area:** `port/net/sys/netplay_fox_firefox_gate.c`, `port/net/sys/netrollbacksnapshot.c`,
`decomp/src/ft/ftchar/ftfox/ftfoxspecialhi.c`

> **2026-07-02 update:** the 2026-07-01 `proc_decrement` instrumentation described below was not
> present in the current build (soak `1137610584` shows only `end_check` / `anim_frames_zero`, no
> `proc_decrement`). Re-added as a complete **travel-span tracer** that measures the exact frame-count
> loss per activation and attributes the end to forward-sim vs the load-verify gate. See
> "Travel-span tracer (2026-07-02)" below — this is the trace to read now.

## Context

User report (soak2, post `netplay_fox_firefox_start_resim_drift` fix): "Firefox resim worked
deterministically but cuts off the firefox early, and then all subsequent firefox's are also cut off
early."

`soak2-linux.log` confirms this is fully deterministic (both peers agree, no desync) but wrong. Built
a clean tick-aligned status/motion timeline for Fox (`player=1 fkind=1`) from `NetSync:
fighter_slot_hash` lines and found all four Firefox activations in the session truncated:

```
228→230(Start)→232(Hold)→234(Travel)→58(End)→59→10(Wait)
tick 493→232, tick 510→234, tick 513→58   (Hold  17 ticks, Travel  3 ticks)
tick 626→232, tick 637→234, tick 641→58   (Hold  11 ticks, Travel  4 ticks)
tick 746→232, tick 757→234, tick 761→58   (Hold  11 ticks, Travel  4 ticks)
tick 884→232, tick 895→234, tick 899→58   (Hold  11 ticks, Travel  4 ticks)
```

`decomp/src/ft/ftchar/ftfox/ftfox.h` defines:

```c
#define FTFOX_FIREFOX_LAUNCH_DELAY 35   // Startup frames required to pass for Firefox to take off
#define FTFOX_FIREFOX_TRAVEL_TIME 30    // Frames Firefox travels
```

Hold should last 35 ticks, Travel 30 — observed is roughly half and roughly a tenth, respectively,
every single time.

## Suspected mechanism (not yet confirmed)

Both countdowns are plain per-tick decrements in vanilla forward-sim (`ftFoxSpecialHiHoldProcUpdate`
decrements `launch_delay`; `ftFoxSpecialHiProcUpdate` decrements `anim_frames`; each ends the phase
when its field hits exactly `0`). The only *other* code that can end either phase is
`syNetplayFoxCatchUpFirefoxLaunchIfDue` / `syNetplayFoxCatchUpFirefoxEndIfDue`
(`port/net/sys/netplay_fox_firefox_gate.c`), invoked from `syNetplayFoxCatchUpAllAfterLoadVerify()`,
which is wired into **every** rollback load via `syNetRollbackLoadPostTickCommitSideEffects`
(`port/net/sys/netrollback.c`, 8 call sites inside `syNetRollbackLoadPostTick`) — not just the rare
full re-sync episodes (this soak's own summary logged only `resim=2` total for the whole 601-tick
match, so the catch-up runs mostly off the routine per-tick speculative-rollback path, not big
resyncs).

Confirmed `status_vars` itself round-trips through a full-size `memcpy` on both capture
(`netrollbacksnapshot.c:5721`) and restore (`:7718`), so this isn't a naive partial-restore bug —
whatever is causing the countdown to reach zero this much faster than 30/35 real ticks needs direct
before/after evidence, not more static reasoning.

## What was added

1. **`port/net/sys/netplay_fox_firefox_gate.c`** — fixed the literal `tick=?` placeholder in the
   existing `launch_delay_zero` / `anim_frames_zero` log lines (never actually wired to
   `syNetInputGetTick()`). Added `syNetplayFoxFirefoxGateLogCheck()`, called from inside
   `CatchUpFirefoxLaunchIfDue` / `CatchUpFirefoxEndIfDue` on **every** evaluation (not just the ones
   that trigger a forced transition), logging `dtick`/`dvalue` — the tick and value delta from the
   previous evaluation *for that player*. `dtick=0` with `dvalue<0` on two consecutive log lines means
   the same sim tick was evaluated more than once and the field dropped in between — i.e. something
   other than one forward-sim `ProcUpdate` call per elapsed tick is moving it.
2. **`decomp/src/ft/ftchar/ftfox/ftfoxspecialhi.c`** — added `syNetplayFoxFirefoxGateLogFieldDecrement()`
   calls (new exported diag hook, gated `#if defined(PORT) && defined(SSB64_NETMENU)`, matches the
   existing `netplay_guard_grab_diag.h` / `netplay_fallspecial_pass_diag.h` include pattern already
   used elsewhere in `decomp/src/ft/`) at the actual decrement sites:
   - `ftFoxSpecialHiHoldProcUpdate` (`launch_delay--`)
   - `ftFoxSpecialHiProcUpdate` (`anim_frames--`)
   - `ftFoxSpecialHiInitStatusVars` (the reset to `FTFOX_FIREFOX_TRAVEL_TIME` on Hold→Travel)
   - `ftFoxSpecialHiEndSetStatus` / `ftFoxSpecialAirHiEndSetStatus` (marks the exact tick/value the
     phase actually ended, whether triggered by vanilla forward-sim or by the netplay catch-up)

All of it is gated behind the existing `SSB64_NETPLAY_FOX_FIREFOX_GATE_DIAG` env var (previously
defined but never actually exercised in a soak — it doesn't appear anywhere in `soak2-linux.log`).

Log line shapes:

```
SSB64 Netplay: FOX_FIREFOX_GATE tick=%u event=launch_check|end_check player=%d status=%d value=%d dtick=%d dvalue=%d
SSB64 Netplay: FOX_FIREFOX_GATE tick=%u event=proc_decrement player=%d status=%d field=%s before=%d after=%d
```

## How to use it

Re-run the Firefox repro with `SSB64_NETPLAY_FOX_FIREFOX_GATE_DIAG=1`. For one Firefox activation:

- Count `event=proc_decrement field=anim_frames` lines between the `anim_frames_init` line and the
  `travel_end_ground`/`travel_end_air` line. If it's ~30, forward-sim itself is behaving and the
  catch-up (`end_check`) is the one ending it early — check the `end_check` trace immediately before
  `travel_end_*` for its `value`/`dtick`/`dvalue`.
- If `proc_decrement anim_frames` fires more than 30 times (or with `dtick=0` between two decrements),
  the real forward-sim update is running more than once for the same logical tick — points at the
  resim/rollback replay path itself, not the netplay catch-up gate.
- Same analysis for `launch_delay` against `FTFOX_FIREFOX_LAUNCH_DELAY` (35).

## Travel-span tracer (2026-07-02)

A self-contained per-player span tracer now lives in `port/net/sys/netplay_fox_firefox_gate.c`
(state `sSYNetplayFoxTravelSpan[2]` + `sSYNetplayFoxTravelEndPathPending[2]`), driven by four hooks:

- `syNetplayFoxFirefoxTravelSpanOnInit(fp)` — from `ftFoxSpecialHiInitStatusVars` (the single
  choke-point where `anim_frames = FTFOX_FIREFOX_TRAVEL_TIME` for both ground `SpecialHi` and air
  `SpecialAirHi`). Latches `entry_tick`, `entry_frames`, resets the decrement counter.
- `syNetplayFoxFirefoxTravelSpanOnSimDecrement(fp)` — from `ftFoxSpecialHiProcUpdate`, immediately
  after `anim_frames--`. Counts every real forward-sim decrement and logs `dtick` (0 ⇒ two
  decrements at the same wire tick ⇒ resim/replay double-stepping, not the gate).
- `syNetplayFoxFirefoxTravelSpanNoteEndPath(fp, from_gate)` — latches who is about to fire the
  `travel→End` transition: forward-sim (`ftFoxSpecialHiProcUpdate`, `from_gate=FALSE`) vs the
  load-verify gate (`syNetplayFoxCatchUpFirefoxEndIfDue`, `from_gate=TRUE`).
- `syNetplayFoxFirefoxTravelSpanOnEnd(fp)` — from `ftFoxSpecialHiEndSetStatus` /
  `ftFoxSpecialAirHiEndSetStatus` (the two `232→234` transitions, hit by both the sim and the gate).
  Emits the span summary.

All decomp hooks are `#if defined(PORT) && defined(SSB64_NETMENU)` and compile out of offline.

Log line shapes (gated on `SSB64_NETPLAY_FOX_FIREFOX_GATE_DIAG=1`):

```
FOX_FIREFOX_GATE tick=%u event=travel_span_init  player=%d status=%d entry_frames=%d expected=%d resim=%d
FOX_FIREFOX_GATE tick=%u event=proc_decrement    player=%d status=%d field=anim_frames after=%d count=%d dtick=%d resim=%d
FOX_FIREFOX_GATE tick=%u event=travel_span_end    player=%d status=%d path=sim|gate|other entry_tick=%d end_tick=%d span_ticks=%d sim_decrements=%d entry_frames=%d anim_frames=%d expected=%d lost_ticks=%d lost_decrements=%d resim=%d
```

### How to read it

For one Firefox activation, find `travel_span_init` (`expected=30`) then its `travel_span_end`:

- `path=sim` with `sim_decrements=30`, `span_ticks≈30`, `lost_*≈0` → travel is full-length; the
  reported early cutoff is **not** here (look at Hold/`launch_delay`, or a presentation-only issue).
- `path=gate` with `sim_decrements < 30` → the **load-verify gate** ended travel before forward-sim
  ran all 30 decrements. `lost_decrements = 30 − sim_decrements` is the exact frame-count loss.
  This is the smoking gun for the gate advancing sim status; fix by constraining `EndIfDue` so only
  forward sim / sealed resim replay drives `232→234`.
- `sim_decrements > 30`, or `proc_decrement` lines with `dtick=0` → forward-sim/replay is
  double-stepping the countdown for the same wire tick (resim replay path), not the gate.

`resim=%d` on every line tells you whether the event fired inside a resim/replay pass.

## Snapshot anim_frames probe (2026-07-02)

Follow-up soak (`1137610584`) proved the span tracer is working and narrowed the bug:

- Non-collision Firefoxes ran full travel: `span_ticks=30`, `sim_decrements=30`, `path=sim`.
- Collision Firefoxes were deterministic but wrong: `path=gate`, with losses like
  `lost_decrements=15`, `19`, `25`, `23`.
- At the first collision cutoff (`tick=870`), forward sim had only decremented to `anim_frames=15`,
  then a rollback load/apply path restored Fox from a snapshot and `EndIfDue` observed
  `anim_frames=0`, firing `232→234`.

Added `FOX_FIREFOX_ANIM_PROBE` lines in `port/net/sys/netrollbacksnapshot.c` to pin where the zero is
introduced. The probe is gated by `SSB64_NETPLAY_FOX_FIREFOX_GATE_DIAG=1` and only logs Fox Firefox
travel scope (`SpecialHi` / `SpecialAirHi`).

Phases:

```
SSB64 NetRbSnapshot: FOX_FIREFOX_ANIM_PROBE tick=%u phase=capture_raw                  ... live_anim=%d blob_anim=%d ...
SSB64 NetRbSnapshot: FOX_FIREFOX_ANIM_PROBE tick=%u phase=capture_final                ... live_anim=%d blob_anim=%d ...
SSB64 NetRbSnapshot: FOX_FIREFOX_ANIM_PROBE tick=%u phase=apply_blob_pre               ... live_anim=%d blob_anim=%d ...
SSB64 NetRbSnapshot: FOX_FIREFOX_ANIM_PROBE tick=%u phase=apply_live_post_status_memcpy ... live_anim=%d blob_anim=%d ...
SSB64 NetRbSnapshot: FOX_FIREFOX_ANIM_PROBE tick=%u phase=apply_live_post_canonicalize ... live_anim=%d blob_anim=%d ...
SSB64 NetRbSnapshot: FOX_FIREFOX_ANIM_PROBE tick=%u phase=apply_live_post_netplay       ... live_anim=%d blob_anim=%d ...
```

How to read the next collision cutoff:

- `capture_raw` already has `blob_anim<=0` while `live_anim>0` → live status_vars was stomped before
  capture (look for a collision/hit overlay writer).
- `capture_raw` ok but `capture_final` changes to `<=0` → snapshot scrub/normalization is corrupting
  the active Fox overlay.
- `apply_blob_pre` has `blob_anim<=0` → load selected a stored poisoned/stale blob.
- `apply_blob_pre` ok but `apply_live_post_status_memcpy` bad → restore/copy layout problem.
- `apply_live_post_status_memcpy` ok but `apply_live_post_canonicalize` or
  `apply_live_post_netplay` bad → post-apply canonicalize/sanitize/catch-up changed it.

`syNetplayFoxSanitizeFirefoxStatusVars` now also logs `event=sanitize_anim_frames` if it clamps a
negative travel countdown to zero, which separates a pre-existing negative from the gate's final
`anim_frames_zero` transition.

## Resoak result and fix (2026-07-02)

Firefox resoak showed the exact corruption point:

```
FOX_FIREFOX_ANIM_PROBE tick=498 phase=capture_raw   ... live_anim=30 blob_anim=30
FOX_FIREFOX_ANIM_PROBE tick=498 phase=capture_final ... live_anim=30 blob_anim=0
```

`capture_raw` is logged immediately after copying `fp->status_vars` into the blob; `capture_final`
is logged after `syNetRbSnapScrubInactiveStatusVarsInBlob`. The live value remained correct, but the
saved blob was poisoned on every Firefox travel capture. A later rollback apply copied
`blob_anim=0` back to live, then the load-verify gate observed `anim_frames==0` and ended travel
early:

```
FOX_FIREFOX_ANIM_PROBE tick=510 phase=apply_blob_pre                ... live_anim=18 blob_anim=0
FOX_FIREFOX_ANIM_PROBE tick=510 phase=apply_live_post_status_memcpy ... live_anim=0  blob_anim=0
FOX_FIREFOX_GATE       tick=510 event=travel_span_end ... path=gate ... lost_decrements=18
```

Root cause: `syNetRbSnapScrubInactiveStatusVarsInBlob` zeroed inactive common overlays
(`common.dead`, `common.rebirth`, etc.) even while the active owner was `status_vars.fox.specialhi`.
Those common overlays alias the same union bytes as Firefox state; `common.dead` reaches
`specialhi.anim_frames` at offset 12, and `common.rebirth` reaches the rest of the travel fields.

Fix: skip the inactive common-overlay scrub for Fox Firefox Start/Hold/Travel/End/Bound
(`nFTFoxStatusSpecialHiStart` through `nFTFoxStatusSpecialAirHiBound`), matching the existing Kirby
Stone guard in the same function. The captured Fox overlay is the authoritative owner; zeroing
inactive common aliases is not safe while it is live.

## Verify

Re-soak a collision Firefox repro with `SSB64_NETPLAY_FOX_FIREFOX_GATE_DIAG=1`. Expected:

- `capture_raw` and `capture_final` preserve the same positive `blob_anim` during Fox travel.
- Any mid-travel `apply_blob_pre` restores the remaining positive `anim_frames`, not zero.
- Collision Firefox travel ends via `path=sim` with `sim_decrements=30`, or via a legitimate vanilla
  collision/end path without `blob_anim` being zeroed by snapshot scrub.

## Resoak after scrub fix (2026-07-02)

Session `236915355` confirmed the timer path is fixed:

```
travel_span_end tick=497  path=sim span_ticks=30 sim_decrements=30 lost_decrements=0
travel_span_end tick=611  path=sim span_ticks=30 sim_decrements=30 lost_decrements=0
travel_span_end tick=746  path=sim span_ticks=30 sim_decrements=30 lost_decrements=0
travel_span_end tick=867  path=sim span_ticks=39 sim_decrements=30 lost_decrements=0
travel_span_end tick=1174 path=sim span_ticks=36 sim_decrements=30 lost_decrements=0
```

The longer `span_ticks` entries are hitlag/collision freezes: the countdown still executes all 30
forward-sim decrements and ends via vanilla sim, not the load-verify gate. No `anim_frames_zero` or
`sanitize_anim_frames` lines appeared.

Remaining report: the visible fire/aura still appears to cut off after passing through a fighter.
Static code check shows Fox Firefox itself uses fighter color animation commands
(`nGMColAnimFighterFoxSpecialHiStart` / `nGMColAnimFighterFoxSpecialHi`), not a dedicated Fox
FireSpark effect. The adjacent `transient_effect_probe` skips in this soak are one-shot hit/effect
verify masking, not the travel timer root cause.

Follow-up change: removed the broad `transient_effect_probe` synctest skip so transient-only effect
ticks surface as real `SYNCTEST_FAIL` / `LOAD_HASH_DRIFT` evidence instead of being skipped. If the
next soak fails on `eff` only, classify/adopt that specific effect family rather than restoring the
skip. If sync stays clean but the aura still visually cuts off, investigate `GMColAnim` restore /
preserve semantics around hitlag and collision, not `status_vars.fox.specialhi.anim_frames`.
