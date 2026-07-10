# Netplay Mario fireball double spawn at close range

**Date:** 2026-06-05  
**Status:** FIX SHIPPED (soak pending)  
**Subsystem:** `port/net/sys/netrollbacksnapshot.c`

## Symptom

Mario shoots **two fireballs** on one neutral B when standing close to Ness. Occasionally a hit
connects without the normal fireball hit reaction (weapon probe mismatch / desync window).

Trim log: `path=anim` with **`weapon_count=1` before spawn → `weapon_count=2` after** (ticks
~1452, ~1497, ~1782 close-range windows). Far-range throws showed `weapon_count=0` before spawn.

## Root cause

| Layer | Issue |
|-------|--------|
| **Carry-over** | Previous throw’s owned fireball still in flight when the next SpecialN reaches anim frame 16. |
| **Hand-only dedup** | `syNetRbSnapFireballNeedsSpawnAtHand` only blocked spawn when a ball was within 60 units of the spawn joint; in-flight carry-over was farther → new `wpMarioFireballMakeWeapon`. |
| **Hand-only dup cull** | Post-spawn cull only removed dupes within 30 units of the live hand; in-flight ball survived. |
| **Throw preserve** | `flag1 == 0` preserved **all** owned fireballs through rollback weapon apply during windup, keeping stale carry-over alive through frame 15. |

## Fix

| Change | Location |
|--------|----------|
| **`syNetRbSnapCullAllOwnedFireballsForFighter`** | Destroy all owned fireballs except `keep` before anim/retry spawn when carry-over exists. |
| **Spawn order** | Reacquire at hand first; else if owned fireballs remain, `cull_stale` then make one new weapon. |
| **Throw preserve** | When `flag1 == 0`, only preserve fireballs still at the live hand (not in-flight carry-over). |
| **Diag** | `fireball_spawn cull_stale` log when WEAPON_DIAG=1; trim script counts rows. |

## Verification

Close-range Mario vs Ness soak: one `weapon_count=1` per throw after `path=anim` (never `2` on the
spawn tick). `cull_stale` may appear when chaining B at close range — expected. No sustained
`weapon_probe_mismatch` on the same throw window.

## Related

- [`netplay_fireball_latch_retry_2026-06-05.md`](netplay_fireball_latch_retry_2026-06-05.md)
- [`fireball_double_spawn_2026-05-22.md`](fireball_double_spawn_2026-05-22.md)
