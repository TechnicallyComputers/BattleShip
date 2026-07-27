# Hash-confirm suppress wire GGPO (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak); follow-on [mismatch_force_rewind](netplay_hash_confirm_mismatch_force_rewind_2026-07-26.md)  
**Bucket:** frame-commit / invent GGPO tax  
**After:** [snap_tick_defer](netplay_hash_confirm_snap_tick_defer_2026-07-26.md)

## Symptom

Soak `seed=1451059971`: `HASH_CONFIRM_DEFER_RESOLVE class=hash_confirm` fired (11× Android) but **10/11** still opened `GGPO deferred` for the same `mismatch_tick` a few lines later. Resim/100 improved only to ~6.6–6.9.

## Root cause

`syNetInputCommitRemoteConfirmedWire` calls `QueueOrWiden` / `RequestInputCorrection` **before** ledger refresh. That path logged `CORRECTION_TUPLE` + armed deferred GGPO while published was still predicted hold_last.

LEDGER then armed soft-defer and resolved `hash_confirm` (no second `QueueOrWiden`) — but the deferred episode from the wire path was already pending → `BeginResim` anyway.

## Fix

1. **`syNetInputStickReplaceTryHashConfirmSchedule`** — shared gate: Promote-only or soft-defer Arm; cancels deferred for that mismatch tick.
2. Call from **`QueueOrWidenStickCorrection`** (before fold) and **`RequestInputCorrection`** (before `CORRECTION_TUPLE` / queue).
3. **`syNetRollbackCancelDeferredInputCorrectionIfMismatchTick`** — drop pending deferred when Promote/defer owns the invent REPLACE; also on defer→confirm Pump.
4. LEDGER uses the same Schedule helper (tag `hash_confirm_schedule`).

## Acceptance

Matched APK + Linux:

- After `HASH_CONFIRM_DEFER_RESOLVE class=hash_confirm` / `skipped class=hash_confirm (schedule)`: **no** `GGPO deferred` for that `mismatch_tick` (unless a later material REPLACE)
- `HASH_CONFIRM_CANCEL_DEFERRED` may appear when wire raced ahead of Schedule
- `skipped_hash_confirm` > 0 and `resim begin`/100 ↓ vs ~6.6–6.9
- Snap mismatch still rewinds; 0× JA SoftLip PEER from hash_confirm (740113729)

## Related

- [`netplay_hash_confirm_snap_tick_defer_2026-07-26.md`](netplay_hash_confirm_snap_tick_defer_2026-07-26.md)
- [`netplay_snap_agree_hash_confirm_watermark_2026-07-26.md`](netplay_snap_agree_hash_confirm_watermark_2026-07-26.md)
