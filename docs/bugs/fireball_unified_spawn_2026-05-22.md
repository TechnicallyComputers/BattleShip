# Unified fireball spawn + rollback throw preserve

**Date:** 2026-05-22  
**Status:** FIX SHIPPED (soak pending)  
**Subsystem:** `port/net/sys/netrollbacksnapshot.c`, `ftmariospecialn.c`, `ftkirbycopymariospecialn.c`

## Symptom

Netplay soak: Mario/Luigi and Kirby (copy Mario/Luigi) neutral-B could play throw anim with no fireball (`wpn=0x811C9DC5`). Kirby copy was worst — Phase 4.4 emergency spawn existed only in `ftmariospecialn.c`. Dense B-spam rollback could still deferred-eject in-flight fireballs (6 live / 5 blobs).

## Root cause

| Layer | Issue |
|-------|--------|
| **Spawn** | Kirby copy Mario/Luigi used vanilla `flag0`-only spawn; missed anim events after resim → empty throw for full SpecialN window. |
| **Apply** | Phase 4.5 deferred eject preserved coupled weapons only; owner-matched fireballs mid-throw could be ejected before spawn/reacquire completed. |

## Fix (Phase 5)

| Area | Change |
|------|--------|
| **Shared spawn** | `syNetRbSnapTrySpawnFireballFromAccessory()` — anim + frame-6 emergency, hand dedup/reacquire/cull, flag1 latch; resolves Mario/Luigi fkind and Kirby `copy_id`; joint 16 vs 17. |
| **Mario** | `ftMarioSpecialNProcAccessory` calls shared helper (removed local duplicate). |
| **Kirby copy** | Same helper + `flag1` cleared on SpecialN entry. |
| **Deferred eject** | `syNetRbSnapLiveWeaponIsFireballThrowPreserve()` skips eject for owner fireballs during throw status when `flag1==0`, `anim_frame < 25`, or near spawn hand. |
| **Diag** | `SSB64_NETPLAY_SNAPSHOT_WEAPON_DIAG=1`: `fireball_spawn path=anim|emergency|reacquire|skip_dedup|spawn`. |

## Verification

Soak Mario vs Kirby copy-Mario B-spam with rollback: no multi-frame SpecialN with empty `wpn`; deferred eject should not destroy throw-window fireballs. Optional weapon diag for spawn path lines.

## Related

- [`mario_fireball_empty_throw_2026-05-20.md`](mario_fireball_empty_throw_2026-05-20.md) Phase 4.4
- [`netrollback_weapon_deferred_eject_2026-05-20.md`](netrollback_weapon_deferred_eject_2026-05-20.md) Phase 4.5
