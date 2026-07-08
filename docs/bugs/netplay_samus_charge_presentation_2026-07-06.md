# Netplay — Samus charge-shot presentation (soak2)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptoms (soak2 Samus vs Yoshi)

- Synctest stable (`SYNCTEST_OK` through tick 2069, `state_diverge=0`), but visible bugs:
  1. Charge loop animation never stops at full charge — Samus stays in `SpecialNLoop` (status 223) with max-size ball.
  2. Charge ball lingers ~0.5–2s after Yoshi egg-lay capture before disappearing.

## Log evidence

- Samus P0 in status 223 for 353+ ticks (segment 1719–2071) — far past the ~140-tick full-charge window.
- Egg-lay @500: `weapon_count` drops 1→0 immediately (sim correct); `gcEjectGObj id=1011` repeats @503–538 while Samus is in `CaptureYoshi` (177).

## Root causes

### Full charge stuck in loop

Vanilla only transitions `SpecialNLoop` → Wait when `charge_level` increments from 6→7 inside `charge_level <= MAX-1`. Rollback blobs memcpy `passive_vars.samus.charge_level` alongside `status_id=223`. After load, `charge_level==7` + loop status never hits the increment path.

`ftSamusSpecialNPortEnsureCoupledChargeShot` then respawns/refreshes the coupled ball every loop tick.

### Ball linger after egg-lay

`ftCommonCaptureYoshiProcCapture` runs `proc_damage` (destroys coupled pointer), but duplicate/orphan charge-shot GObjs from `EnsureCoupled` respawn cycles survive until deferred eject. Snapshot apply already culls when leaving charge statuses; forward-sim capture needed the same belt-and-suspenders cull.

## Fix

1. **`ftSamusSpecialNPortReconcileMaxChargeLoopIfNeeded`** — if loop + `charge_level >= MAX`, col-anim + destroy + Wait (Samus + Kirby Copy-Samus).
2. Call reconcile before `EnsureCoupled` in `LoopProcUpdate` and `LoopSetStatus` (forward sim only — not snapshot apply).
3. **`EnsureCoupled`** — no spawn/reacquire when not in charge statuses or when already at max charge.
4. **`SetChargeShotPosition`** — reacquire only in charge statuses and below max charge.
5. **`ftCommonCaptureYoshiProcCapture`** — `syNetRbSnapCullSamusChargeShotsForFighter(fighter, NULL)` after capture setup (netplay rollback only).

## Files

- `decomp/src/ft/ftchar/ftsamus/ftsamusspecialn.c`
- `decomp/src/ft/ftchar/ftkirby/ftkirbycopysamusspecialn.c`
- `decomp/src/ft/ftcommon/ftcommoncaptureyoshi.c`
- `port/net/sys/netrollbacksnapshot.c`
