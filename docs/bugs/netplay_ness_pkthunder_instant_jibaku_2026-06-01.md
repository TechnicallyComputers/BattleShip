# Netplay — Ness PK Thunder instant jibaku after repeated use

**Date:** 2026-06-01  
**Status:** Fix shipped (soak pending)  
**Symptom:** After several PK Thunder jibaku uses, UP+B goes straight to jibaku (self-propel down) on the first Hold frame instead of steering the bolt.

## Root cause

`FTStatusVars` is a union: `common.attackair.rehit_timer` and `ness.specialhi.pkjibaku_delay` share the same first four bytes.

On every snapshot ring save, `syNetRbSnapScrubInactiveStatusVarsInBlob()` zeroed `common.attackair` whenever the fighter was **not** in an attack-air status. Ness PK Thunder Start/Hold (232/233) is not attack-air, so the scrub wrote `pkjibaku_delay = 0` into the **saved blob** while live state still held 30.

After a rollback load (`LOAD_HASH_DRIFT`), live `pkjibaku_delay` became 0. `ftNessSpecialHiCheckCollidePKThunder()` allows self-hit only when delay is 0, so Hold's first frame with a freshly spawned head at Ness triggered instant jibaku.

Log signature (tick 1599→1600): status 233 for one frame, `rehit=0` (diagnostic alias for `pkjibaku_delay`), then status 231 with downward velocity.

## Fix

1. **Save path:** Skip attack-air scrub when `syNetRbSnapBlobInNessPKThunderScope(blob)` is true.
2. **Apply path:** `syNetplayNessSanitizePKThunderThrowStatusVars()` restores `pkjibaku_delay` on Start/Hold if scrubbed to zero — but **skips** late Hold where delay=0 is legitimate grace expiry (see [netplay_ness_pkthunder_resim_sanitize](netplay_ness_pkthunder_resim_sanitize_2026-06-01.md)).
3. **Hold entry:** Under `#ifdef PORT`, `ftNessSpecialHiHoldInitStatusVars()` resets `pkjibaku_delay` to `FTNESS_PKJIBAKU_DELAY` on Start→Hold (only path that calls HoldInit).
4. **Early-Hold floor (2026-06-01):** `syNetplayNessReconcilePKThunderDelayEarlyHold()` clamps delay to at least `FTNESS_PKJIBAKU_DELAY - status_total_tics` while still in the first 30 Hold frames — survives multi-phase LOAD_HASH_DRIFT apply that re-zeroes the union overlay.
5. **Collide guard:** `syNetplayNessHoldJibakuCollideBlocked()` blocks `ftNessSpecialHiCheckCollidePKThunder()` until `status_total_tics >= FTNESS_PKJIBAKU_DELAY`.
6. **Post slot-apply:** `syNetplayNessSanitizeAllFightersAfterSlotApply()` runs after weapon/coupling rebind in `syNetRbSnapApplySlotToLive()`.

## Files

- `port/net/sys/netrollbacksnapshot.c` — scrub guard + apply sanitize call + post-slot sanitize
- `port/net/sys/netplay_ness_pkthunder_gate.c/.h` — sanitize helper, delay floor, collide guard
- `decomp/src/ft/ftchar/ftness/ftnessspecialhi.c` — HoldInit delay reset + CheckCollide grace gate

## Verification

Re-test repeated PK Thunder jibaku in netplay rollback session; Hold should last ~30+ frames before jibaku is possible, even across `LOAD_HASH_DRIFT` during Start/Hold.
