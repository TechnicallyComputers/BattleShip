# Netplay: Kirby Final Cutter blade FX orphans / mid-Up+B starve

**Status:** FIX ROUND 9 (`PORT && SSB64_NETMENU`, re-soak pending)  
**Date:** 2026-07-10 (r2–r8: 2026-07-11; r9: 2026-07-11)  
**Subsystem:** `decomp/src/ft/ftchar/ftkirby/ftkirbyspecialhi.c`, `decomp/src/ft/ftparam.c`, `decomp/src/ef/efmanager.c`, `port/net/sys/netrollbacksnapshot.c`

## Symptom

1. **Orphan (stuck blade / blob on Kirby)** after Landing→Wait.
2. **Stacked** hand Trail + TopN blade during/after Landing.
3. Mid-move starve (synctest) — separate protect path.

Cosmetic only. Beam/`wpn`: [netplay_kirby_finalcutter_synctest_wpn_2026-07-11.md](netplay_kirby_finalcutter_synctest_wpn_2026-07-11.md).

## Root cause

Teardown is gated on `is_effect_attach`. Netplay clears that flag while cutter shells still live, so:

- Air land `PRESERVE_NONE` StopEffect **no-ops**
- Landing ACMD mint (flag2 2–5) does not stop-first → **stacks**
- Wait StopEffect misses wrong-`fighter_gobj` shells; reconcile dumps 5–7 leftovers

Marker (soak2 seed `2221962849`): every multi-blade Wait had `effect_attach_restore` on **Landing (257)**; early clean Waits did not.

## Fix

| Round | Change |
|-------|--------|
| r6 | Joint-first owner, reconcile, attach restore |
| r7–r8 | StopEffect full DObj-tree clear; revert bad air `PRESERVE_EFFECT` |
| r9 | `syNetRbSnapForceClearKirbyFinalCutterBlades` — stop-before-mint; land-entry force clear; stop paths when attach already false; **no** attach_restore on Landing |

## Verify

- No Landing `effect_attach_restore`; Wait should not dump 5–7 `kirby_finalcutter_blade` ejects.
- No hand + TopN stack after many Up+Bs.
- Mid-SpecialHi protect still OK.
- `cmake --build build-netmenu --target ssb64 -j 4`
