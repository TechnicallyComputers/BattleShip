# Fireball double spawn per neutral B (emergency + anim)

**Date:** 2026-05-22  
**Status:** FIX SHIPPED (soak pending)

## Symptoms

Mario/Luigi netplay soak: ~2.2 `fireball_spawn` logs per neutral B (`11` SpecialN passes → `24` spawns on host). Pattern `path=emergency` @ frame 3 then `path=anim` @ ~frame 32 on the same SpecialN. Worse when fighters are close — first fireball dies before anim frame, clearing the latch and allowing a second spawn.

## Root cause

| Layer | Issue |
|-------|--------|
| **Dual paths** | `syNetRbSnapTrySpawnFireballFromAccessory` fired emergency at frame ≥3 on every forward tick, then anim `flag0` could spawn again on the same status entry. |
| **flag1 latch** | Phase 4.4 cleared `flag1` when owned fireball disappeared mid-throw, re-arming anim spawn after emergency had already fired. |

Vanilla offline uses anim `flag0` only. Emergency exists for rollback load/resim missed events.

## Fix

| Change | Location |
|--------|----------|
| **`flag1` per throw** — latch for entire SpecialN entry; clear only in `InitStatusVars` (PK Fire pattern) | `syNetRbSnapTrySpawnFireballFromAccessory()` |
| **Emergency gated** — only when `syNetRollbackIsResimulating()` or `syNetRbSnapFireballProcAccessoryWillRun()` is false (ProcUpdate physics-map fallback) | same |

Forward sim with live `proc_accessory`: anim `flag0` only. Resim / accessory-skipped paths still get emergency @ frame 3.

## Follow-up: synctest rebind empty throw (2026-05-22)

Gating emergency off forward sim exposed a separate bug when periodic synctest ran mid-SpecialN: rebind NULLed `proc_accessory` without restoring Mario/Luigi fireball callback. See [fireball_synctest_rebind](fireball_synctest_rebind_2026-05-22.md).

## Soak pass criteria

- One `fireball_spawn` per B press (Mario, Luigi, Kirby copy) at any range
- No `emergency` + `anim` pair on the same SpecialN during forward soak
- Resim mid-throw still recovers via emergency when anim event was missed

## Related

- [`fireball_spawn_phase5b_2026-05-22.md`](fireball_spawn_phase5b_2026-05-22.md)
- [`netplay_ness_pkfire_spawn_2026-05-22.md`](netplay_ness_pkfire_spawn_2026-05-22.md) — latch pattern source
