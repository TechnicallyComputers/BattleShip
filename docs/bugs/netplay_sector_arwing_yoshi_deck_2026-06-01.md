# Sector Z Arwing deck + Yoshi jump FC desync (netplay)

**Date:** 2026-06-01  
**Scope:** `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netrollback.c`  
**Status:** FIX SHIPPED — soak pending (Yoshi move-spam on Sector Z / Arwing deck)

## Symptoms

Cross-ISA soak (Android host / Linux guest): Yoshi P1 move-spam on Sector Z; user-visible desync when jumping on the Arwing deck (~tick 3360, ~56s).

- `FRAME_COMMIT_STATE_DIVERGE` — **figh-only**; world/rng/eff/inputs agree.
- Hidden drift seed at **tick 3241** (Yoshi KneeBend on yakumono line id 1, `floor_flags=0x8000`); last agreed frame commit **3240**.
- Kirby P0 in `RebirthWait` during seed window (`SYNCTEST_SKIP reason=rebirth`).
- FC recovery: `fighter_field_diff rebirth_pos_y live=0 blob=0x462BE000`, baseline resync storm (load 3240→3238), `rollback_epoch_hold` → session stop.

Prior seal-row outbound + Yoshi egg/aerial defer patches were active (5 seal chunks sent; no `RESIM_SEAL_ROWS_TIMEOUT`).

## Root cause

1. **Moving Arwing deck collision** — cross-ISA fighter physics/coll drift on yakumono floor line 1 during walk/jump/land (same family as Sector Arwing rollback doc; distinct from egg/aerial defer windows).

2. **Rebirth catch-up during snapshot load apply** — `syNetplayRebirthCatchUpLifecycleIfDue` ran while `sSYNetRbSnapDeferNetplayCatchUpDuringApply` was TRUE for Pikachu only, advancing rebirth lifecycle before hash verify → `rebirth_pos_y` live/slot mismatch on FC recovery load.

3. **Pre-resim deeper load loop** — `LOAD_SLOT_LIVE_DRIFT` retried `load_tick-1` repeatedly, spawning multiple fc_recovery epochs.

## Fix

1. **`sector_arwing_deck` synctest defer** — live + probe skip when Sector Z Arwing collision is active and any fighter is on deck line 1 in walk/jump/land/fall fragile statuses.

2. **`syNetRbSnapshotCanonicalizeSectorArwingDeckFighter`** — extra physics/coll/top-joint (and pass/cliff floor) quantize on capture/apply for deck fighters.

3. **Gate rebirth catch-up during load apply** — defer `CatchUpDeadGate` / `CatchUpLifecycle` when `sSYNetRbSnapDeferNetplayCatchUpDuringApply`.

4. **One pre-resim deeper load per resim begin** — `sSYNetRollbackPreResimDeeperLoadUsed` caps `TryDeeperLoadBeforeResim` to a single step per epoch.

5. **Deck yakumono reconcile (2026-06-01 follow-up)** — `grSectorArwingUpdateCollisions()` after Arwing snapshot apply and before map capture so yakumono line 1 is derived from the flight tree (single source of truth) instead of independently-restored `mp_yaku[1]`. Refresh grounded fighters on line 1 `vel_speed` after stage repair; fold deck `vel_speed` + `floor_flags` in `fhash_light`.

## Test plan

1. Sector Z Yoshi vs idle opponent ≥60s: jump on Arwing deck repeatedly; no `FRAME_COMMIT_STATE_DIVERGE`.
2. Same soak with opponent in rebirth: FC recovery load at rebirth tick shows no `rebirth_pos_y` drift; no baseline resync storm past one deeper step.
3. Log: `SYNCTEST_SKIP reason=sector_arwing_deck` during deck activity; no epoch 2/3 storm from load 3240→3238 pattern.
