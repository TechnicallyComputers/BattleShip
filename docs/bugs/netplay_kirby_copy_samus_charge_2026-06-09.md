# Netplay Kirby copy-Samus charge shot — 2026-06-09

**Status:** FIX SHIPPED (soak pending)

## Symptom

Kirby vs Samus soak: Kirby with copied Samus neutral-special held charge showed a **fixed-size** charge orb while `copysamus_charge_level` climbed; released projectile size/damage matched the real charge tier. Samus charge orb scaled normally.

Synctest: `SYNCTEST_FAIL` + `LOAD_HASH_DRIFT` (`figh`/`anim`, `guard_shield_load_drift`) @ tick 1654 while Kirby was in `CopySamusSpecialNStart`/`Loop` (Samus idle).

## Root cause

1. **Visual (netmenu)** — `ftKirbyCopySamusSpecialNPortRefreshChargeShotGfx()` read `passive_vars.samus.charge_level` instead of `passive_vars.kirby.copysamus_charge_level`. On Kirby that aliases `copy_id` (Samus = 3), pinning orb gfx at tier 3.

2. **Load-finalize** — Coupled charge rebind restored position only; DObj scale was not refreshed from passive charge level after presentation/joint finalize (Samus + Kirby copy).

3. **Synctest probe** — Historical slot ticks in copy-Samus charge statuses did not round-trip through verify finalize reliably (same probe-boundary class as other coupled-weapon windows).

## Fix

| Area | Change |
|------|--------|
| `ftkirbycopysamusspecialn.c` | Gfx refresh uses `copysamus_charge_level`; `LoopSetStatus` initializes `kirby.copysamus_specialn.charge_int`. |
| `netrollbacksnapshot.c` | `syNetRbSnapRefreshCoupledChargeShotGfx()` after charge rebind+cull (Samus + Kirby copy). |
| `netrollbacksnapshot.c` | Probe skip `samus_charge_probe` / `kirby_copy_samus_charge_probe` on charge Start/Loop/AirNStart slot blobs. |

## Soak pass criteria

Kirby inhale Samus → hold neutral-special through all charge tiers: orb grows smoothly; release size matches orb.

Synctest trim ~1590–1660: expect `SYNCTEST_SKIP reason=kirby_copy_samus_charge_probe` (or `samus_charge_probe`) instead of `SYNCTEST_FAIL` on charge slot ticks; no `LOAD_HASH_DRIFT` + `fighter_mismatch` on those probes.
