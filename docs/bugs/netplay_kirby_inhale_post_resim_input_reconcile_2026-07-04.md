# Netplay: Kirby inhale fork after episode-FSM resim — post-resim input reconcile skipped

**Date:** 2026-07-04  
**Scope:** `port/net/sys/netrollback.c`, `port/net/sys/netinput.c`, `port/net/sys/netrollbacksnapshot.c` (`PORT && SSB64_NETMENU`)

## Symptom

After removing synctest probe skips (only `intro_wait` remains), soak2 Linux+Android reported `FRAME_COMMIT_STATE_DIVERGE` at validation 600 with **inputs MATCH**. Link (P0): Linux `status=10` Wait vs Android `status=26` DamageFall. Root gameplay fork traced to tick **524**: Android-only Kirby `SpecialNCheck PASS → inhale`, Linux host Kirby stayed Wait. Android-only inhale-wind effect at tick 577 and Link knockback followed.

Prior resim at tick 520 (`FORCE_MISMATCH` / input mismatch slot 1 `btn=0x1000` vs neutral) used episode FSM (`commit_promote mismatch=520 target=522`).

## Root cause

1. **Post-resim reconcile gated off episode FSM** — `syNetRollbackFinishForwardResim` only called `syNetInputRollbackReconcileAfterResimCompleted` when `syNetRollbackEpisodeFsmEnabled() == FALSE`, so episode-FSM resims never reconciled published input history after `CommitPromoteSealed`.
2. **Stale ephemeral `pl_button_tap`** — fighter snapshot apply restored blob `button_tap`/`button_release` even though `ftMainProcessInput` re-derives edges from `button_hold` vs controller each tick. Restored orphan taps (same class as Kirby stone rollback release) could fire `SpecialNCheck` on one peer only while input digests still matched on holds.
3. **`last_published` latch drift** — without post-resim republish, remote-slot `PublishFrame` edge detect could disagree with reconciled history on the first live ticks after resim.
4. **Controller vs fighter `pl` desync after resync republish** — `syNetInputRollbackResyncControllersAfterResim` republished `[mismatch, frontier)` including already-replayed ticks, rewriting `gSYControllerDevices` while `fp->input.pl` still reflected the resim replay. First live tick (`523`) then saw spurious stick/button edges on the guest's remote Link sim (Squat status 15 vs host Wait).

## Fix

| Change | Purpose |
|--------|---------|
| Always run `syNetInputRollbackReconcileAfterResimCompleted` after forward resim (remove episode-FSM guard) | Reconcile published history for episode-FSM resims |
| `syNetInputRollbackResyncControllersAfterResim` after resim exit | Reseed `last_published` from history and republish remote controllers over `[mismatch, frontier)` once resim flags clear |
| Resync only reseeds `last_published` (no republish over replayed span) + `syNetInputRollbackResyncFighterPlLatchFromControllers` | Republish rewrote `gSYControllerDevices` without updating `fp->input.pl`, causing spurious stick/button edges on first live tick (Link squat @523) |
| Reconcile peer-seal remote slots from sealed rows (not skip) | Belt-and-suspenders after `CommitPromoteSealed` for authority-owned remote slots |
| Zero `pl_button_tap` / `pl_button_release` on snapshot apply | Stop persisting ephemeral edges; keep `pl_button_hold` + stick prev latch |

## Verify

- `cmake --build build-netmenu --target ssb64 -j 4`
- Re-soak Linux host + Android guest: no Android-only `SpecialNCheck` at tick 524 after FORCE_MISMATCH@520 resim; no FC600 Link status fork.

## Related

- [`netinput_post_resim_published_reconcile_2026-05-19.md`](netinput_post_resim_published_reconcile_2026-05-19.md)
- [`netplay_stick_latch_resim_fork_2026-07-03.md`](netplay_stick_latch_resim_fork_2026-07-03.md)
- [`netplay_kirby_stone_rollback_release_2026-06-06.md`](netplay_kirby_stone_rollback_release_2026-06-06.md)
