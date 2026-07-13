# Stick L/R storm: deferred GGPO live-cap deadlocks rollback cooldown

**Date:** 2026-07-12  
**Session:** seed `1613454651` (Android client lp=1 ↔ Linux host lp=0)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

Rapid left↔right stick (Turn): session freezes after a few GGPO episodes.

```text
[Android] rollback_epoch_hold … sim=435 cap=434 source=peer_target
[Android] tick_commit blocked (load_fail_hold) tick=435
[Android] try_begin_fail stage=commit_begin_failed mismatch=435 target=437 … resolved_through=435
[Linux]   advances on hold-last → receives VS_SESSION_END @439
```

Three resims in ~8 ticks (onset left, stick change, direction flip). `CORRECTION_CLAMP_RESOLVED` may fire; hang is after ep3 completes.

## Root cause

1. Post-episode stick REPLACE queues deferred GGPO with `mismatch == GetTick()` (e.g. 435).
2. `DeferredCorrectionBlocksLiveAdvance` arms live-cap = `mismatch - 1` (434).
3. `GlobalCooldownAllows` (default 2 sim ticks after last Begin) still blocks the new mismatch because **cooldown advances only when sim advances**.
4. Live-cap prevents sim from reaching `LastBegin + cooldown` → `TryCommit` fails forever → `commit_begin_failed` without abandon → freeze until quit.

(Log labels `source=peer_target` when bit2 is set even if the binding cap is deferred bit8.)

## Fix

1. **`GlobalCooldownAllows`** — pending deferred GGPO mismatch bypasses cooldown (the work holding the live-cap must be allowed to BeginResim).
2. **`GgpoDeferredShouldAbandon`** — abandon when `target <= LastTargetTick` and sim already at/past mismatch.
3. **`TryBeginDeferredMismatch`** — on `commit_begin_failed` for local GGPO when `sim >= mismatch`, clear deferred (`lift_livecap`) so live can advance; later REPLACE may re-queue.

## Verify

- Re-soak Android ↔ Linux: mash left/right Turn after GO.
- Expect continued sim (or `ggpo deferred … resim` / `lift_livecap`), not multi-second `epoch_hold`/`load_fail_hold` at `cap=mismatch-1` with only `commit_begin_failed`.
- No hang requiring force-quit after a short dash-dance.
