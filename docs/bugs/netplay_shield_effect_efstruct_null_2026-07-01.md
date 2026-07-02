# Netplay — Shield effect SIGSEGV when spamming Guard on both fighters

**Date:** 2026-07-01
**Status:** Fix implemented (`#if defined(PORT) && defined(SSB64_NETMENU)`, soak pending)
**Area:** `decomp/src/ef/efmanager.c`

## Symptom

Soak2 session `604955560` (max_sim_tick=630): both peers `SIGSEGV` at the same tick, no
`SYNCTEST_FAIL` / `FRAME_COMMIT_*` diverge (a crash, not a desync). User repro: both fighters
spamming Guard (shield) rapidly.

```
SSB64: !!!! CRASH SIGSEGV fault_addr=0xe0
BattleShip(efManagerShieldProcUpdate+0x0) [...]
```

`pc` at function entry, fault at struct offset `0xe0` — the same NULL `EFStruct*` dereference
shape as the previously-fixed `efManagerImpactWaveProcUpdate` (Firefox charge quake VFX) and
`efManagerKirbyVulcanJabProcUpdate` (see
[netplay_kirby_vulcanjab_efstruct_null](netplay_kirby_vulcanjab_efstruct_null_2026-07-01.md)).

## Root cause

```c
void efManagerShieldProcUpdate(GObj *effect_gobj)
{
    EFStruct *ep = efGetStruct(effect_gobj);     // <- unchecked

    if (ep->effect_vars.shield.is_damage_shield != FALSE)
    ...
```

No NULL guard on `ep`. The guard-bubble `GObj` is ejected via `syNetRbSnapEjectGObj` /
`efManagerNetSafeFreeStruct` (`guard_shield_prune path=eject reason=is_release`) when netplay
rollback reconciliation determines the shield should no longer be live for the current status —
e.g. two fighters releasing/re-raising Guard back-to-back generates rapid attach/detach churn.
The `EFStruct` is freed, but the `GObj`'s proc-update callback still runs once more that tick
(the eject takes effect on the next GC sweep, not immediately), dereferencing freed memory.

Two sibling display callbacks have the identical unguarded shape and are exposed to the same
race, just on the render side instead of the sim side:

- `efManagerShieldProcDisplay` — dereferences `ep->effect_vars.shield.*` directly.
- `efManagerYoshiShieldProcDisplay` — dereferences `ep` *and* `ftGetStruct(ep->fighter_gobj)`
  (two unchecked pointers instead of one).

## Fix

Added the same defensive NULL-guard pattern established for the damage-particle/Vulcan-Jab family:

- `efManagerShieldProcUpdate`: guard `effect_gobj`/`ep`, `gcEjectGObj` and return early instead of
  dereferencing.
- `efManagerShieldProcDisplay`: guard `effect_gobj`/`ep`, skip drawing this tick (no eject —
  display callbacks don't own the GObj's lifecycle).
- `efManagerYoshiShieldProcDisplay`: guard `effect_gobj`/`ep`/`ep->fighter_gobj`/`ftGetStruct`
  result, skip drawing this tick.

All three use `#if defined(PORT) && defined(SSB64_NETMENU)` with an `#else` preserving the
original decomp body verbatim — the hazard only exists because netplay rollback can free an
`EFStruct` while its `GObj` is still walked by the per-frame proc-update/proc-display loop; that
can't happen in the offline build (no rollback snapshot code linked), so the guard must not leak
into the offline binary. See
[netplay_kirby_vulcanjab_efstruct_null](netplay_kirby_vulcanjab_efstruct_null_2026-07-01.md) for
why the earlier bare-`#ifdef PORT` guards in this same file were also re-gated to
`PORT && SSB64_NETMENU` as part of this pass.

Note: this fix addresses the **crash**. The separate shield-clipping visual bug (shield renders
through the middle of the fighter model instead of as an overlay in front) reported in the same
session is tracked by the diagnostic instrumentation in
[netplay_guard_shield_attach_refresh_diag](netplay_guard_shield_attach_refresh_diag_2026-07-01.md)
and is not fixed by this change.

## Verify

`build-netmenu` (`SSB64_NETMENU=ON`) and `build-offline` (`SSB64_NETMENU=OFF`) both rebuild and
link clean. Re-soak reproducing rapid Guard-spam on both fighters; expect no SIGSEGV in
`efManagerShieldProcUpdate`/`ProcDisplay`/`YoshiShieldProcDisplay`.
