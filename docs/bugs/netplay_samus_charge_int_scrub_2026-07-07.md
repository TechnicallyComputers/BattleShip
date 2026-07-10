# Netplay — Samus charge_int snapshot scrub (soak2)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptoms (soak2 Samus vs Yoshi, Linux + Android)

- Synctest stable (`SYNCTEST_OK`, `state_diverge=0`) but charge shot **does not grow consistently** while holding B.
- Charge loop (status 223) can run for hundreds of ticks without `charge_level` advancing after a rollback resim.
- Fully deterministic on both peers — presentation/sim policy bug, not desync.

## Log evidence

- P0 Samus (`fkind=3`) charge episodes: Start 222 (~15 ticks) → Loop 223 → egg-lay / release / capture exit.
- Loop 223 with `weapon_count=1` throughout (coupled ball present in snapshots).
- Rollback resim during charge (`prepare_verify` @509, 629, 749, …) while status 222/223.
- Long hold @1466–1809: 343 loop ticks before release — far past the ~140-tick full-charge window, yet status stays 223 until manual release (charge_int not advancing post-resim).

## Root cause

`synNetRbSnapScrubInactiveStatusVarsInBlob` zeroes inactive `status_vars.common.*` overlays on every snapshot **save**. Samus charge statuses (222/223/224, air variants) live in `status_vars.samus.specialn` (`is_release`, `charge_int`, `charge_gobj`), which **aliases byte 0** of the top-level `FTStatusVars` union — same bytes as `common.attackair`, `common.dead`, etc.

While in charge, the scrub clears those common overlays and **wipes `charge_int` to 0** in the saved blob. After resim load:

1. `charge_int` stays ≤ 0; `LoopProcUpdate` decrements it negative every tick.
2. The `charge_int == 0` increment path never runs → **`charge_level` frozen** (still serialized correctly in `passive_vars.samus`).
3. Ball size / SFX appear stuck; player reads it as “charge blocked.”

`charge_level` in `passive_vars` is unaffected; only the per-loop timer overlay is poisoned.

## Fix

1. **Skip inactive status_vars scrub** when blob is in Samus or Copy-Samus charge fragile scope (mirrors Fox Firefox / Kirby Stone guard pattern).
2. **Snapshot apply:** call `ftSamusSpecialNPortReconcileMaxChargeLoopIfNeeded` for Samus (Kirby Copy-Samus already had this).
3. **`prepare_verify`:** refresh coupled charge-shot position + GFX when slot is in charge presentation scope (weapon sim deferred during verify).

## Files

- `port/net/sys/netrollbacksnapshot.c`
- `docs/bugs/README.md`
