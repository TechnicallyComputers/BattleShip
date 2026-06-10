# Stale shallow peer symmetric tuple during deep anchor walkback (netplay)

**Date:** 2026-06-10  
**Scope:** `port/net/sys/netrollback.c`  
**Status:** FIX SHIPPED (EPISODE_YIELD guard) — soak pending (`INJECT_TICK=240`)

## Symptoms

Soak @240 (Android follower / Windows initiator): after 16-step anchor probe walkback to `load=207`, `mismatch=208`, Android logs `resim baseline digest matched` but never `resim replay gate open`. `EPISODE_SEAL_ROWS_RECV` never appears; `EPISODE_SEAL_ROWS_WAIT missing_slots=0x2` (slot 1) until `RESIM_SEAL_ROWS_TIMEOUT` and session stop. Windows completes `resim complete` at the same anchor.

Smoking gun:

```
EPISODE_FSM tuple_align load=207 mismatch=208->224 target=242->242
```

Android had queued `peer symmetric rollback … mismatch_tick=224` at sim=241 from an early `ROLLBACK_SYNC_RECV`. After settling at the deeper anchor (`208`), embedded INPUT symmetric notify re-fired `tuple_align` to the stale shallow tuple. Windows sent seal rows for `mismatch=208`; Android waited for `mismatch=224`.

## Root cause

`syNetRollbackTryAlignActiveEpisodeTuple()` re-sealed the follower forward to a **shallower** peer `mismatch_tick` when `align_load == cur_load`, even though local anchor walkback had already moved the episode to a **deeper** mismatch on the same load tick. Stale `PendingPeerSymmetric` state (224) was not cleared when resim settled at 208.

## Fix

1. **`syNetRollbackPeerSymmetricNotifyIsStaleShallow()`** — ignore peer symmetric notify / tuple_align / `EPISODE_YIELD` when active resim episode has `notify_mismatch > cur_mismatch` and peer load is not deeper (`notify_load == 0`, `== cur_load`, or `> cur_load` i.e. shallower anchor while local walkback settled deeper).
2. **`syNetRollbackClearStaleShallowPeerSymmetricNotify()`** — at `resim begin`, drop pending/deferred symmetric notify when `pending_mismatch > settled_mismatch`.
3. **`tuple_align_skip_stale_shallow`** — belt-and-suspenders guard in `TryAlignActiveEpisodeTuple` for same-load forward mismatch bumps.
4. **`EPISODE_YIELD_skip_stale_shallow`** — defense before `syNetRollbackAbortInFlightResimForPeerEpisode()` when stale `ROLLBACK_SYNC_RECV` (e.g. `224/223`) arrives while follower episode is at deeper `208/207`.

## Test plan

1. Re-run `INJECT_TICK=240` soak — both peers should reach `resim replay gate open` and `resim complete`; no `tuple_align … 208->224`, no `EPISODE_YIELD` on stale `ROLLBACK_SYNC_RECV 224/223` during deep anchor.
2. Regression: `INJECT_TICK=480` soak (prior pass) unchanged.
