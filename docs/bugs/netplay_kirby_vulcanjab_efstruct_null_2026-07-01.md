# Netplay — Kirby Vulcan Jab (rapid A) hit-effect SIGSEGV on Fox shield hit

**Date:** 2026-07-01
**Status:** Fix implemented (`#if defined(PORT) && defined(SSB64_NETMENU)`, soak-verified stable)
**Area:** `decomp/src/ef/efmanager.c`

## Symptom

Soak2 session `1339981196` (max_sim_tick=1750): both peers `SIGSEGV` deterministically at the
same tick, no `SYNCTEST_FAIL` / `FRAME_COMMIT_*` diverge, `RESULT: PASS` from
`netplay-scan-drift.py` (a crash, not a desync). Linux backtrace symbolized cleanly:

```
SSB64: !!!! CRASH SIGSEGV fault_addr=0xe0
BattleShip(efManagerKirbyVulcanJabProcUpdate+0x0) [0x5562cd4841a0]
```

`pc` at function entry, `x0=0x0` (first arg), fault at a small struct offset (`0xe0`) — a NULL
`EFStruct*` dereference. Reproduced by the user with Kirby A-spamming (rapid jab / Vulcan Jab) a
shielding Fox; the log shows the crash lands the same tick Fox transitions out of active Guard into
a shield-hit reaction (`status=155`) and `guard_shield_prune`/`guard_shield_ensure_skip`/
`emergency_restore` all fire for player 1 on that tick.

## Root cause

```c
void efManagerKirbyVulcanJabProcUpdate(GObj *effect_gobj)
{
    EFStruct *ep = efGetStruct(effect_gobj);
    DObj *dobj = DObjGetStruct(effect_gobj);

    if (ep->effect_vars.vulcan_jab.lifetime != 0)   // <- ep unchecked
    ...
```

No NULL guard on `ep`/`dobj` before dereferencing. Same defect shape as the previously-fixed
`efManagerImpactWaveProcUpdate` (Fox Firefox charge quake VFX, round 1 of
[netplay_fox_appear_firefox_charge_soak2](netplay_fox_appear_firefox_charge_soak2_2026-07-01.md)):
a per-hit VFX `GObj` can outlive the netplay rollback/reconciliation pass that already detached its
`EFStruct` (the generic `slot_effect_enforce` ejection path, not a Vulcan-Jab-specific reconcile —
unlike Fox Firefox's ImpactWave, no per-type reconcile/eject exists for Vulcan Jab hit sparks), so
the next proc-update tick dereferences a freed/NULL struct. The guard-hit reconciliation happening
on the exact same tick as the crash is the trigger: Kirby's rapid-jab hit-effect GObjs are mid-flight
when Fox's shield-hit transition runs the effect-reconcile machinery.

## Fix

Added the same defensive NULL-guard pattern as `efManagerImpactWaveProcUpdate`
(ejects the GObj cleanly instead of dereferencing) to `efManagerKirbyVulcanJabProcUpdate`. Swept
the other generic per-hit damage-particle proc-update callbacks in `efmanager.c` sharing the
identical unchecked shape — spawned on every attack that deals damage, not character-specific, so
equally exposed to the same rollback-prune race: `efManagerDamageFlyOrbsProcUpdate`,
`efManagerDamageSpawnOrbsProcUpdate`, `efManagerStarRodSparkProcUpdate`,
`efManagerDamageFlySparksProcUpdate`, `efManagerDamageSpawnSparksProcUpdate`,
`efManagerDamageSpawnMDustProcUpdate`. A follow-up crash in `efManagerShieldProcUpdate` (same
family, guard bubble itself — see
[netplay_shield_effect_efstruct_null_2026-07-01](netplay_shield_effect_efstruct_null_2026-07-01.md))
confirmed the pattern is systemic to any proc-update/proc-display callback whose `EFStruct` can be
freed out from under it by rollback ejection, not just damage particles.

Each guarded function gets a `#if defined(PORT) && defined(SSB64_NETMENU)` branch with the null
check + `#else` preserving the vanilla decomp body untouched (decomp preservation rule —
vanilla/offline behavior unchanged). These were initially landed as bare `#ifdef PORT`, which is
wrong per the OFFLINE vs NETMENU contract: the underlying hazard (an `EFStruct` freed by netplay
rollback's `syNetRbSnapEjectGObj`/reconciliation while the owning `GObj` is still walked by the
per-frame proc-update/proc-display loop) cannot occur in the offline build at all — no rollback
snapshot code is linked there. A bare-`PORT` guard would have compiled (and executed, as dead-safe
code) into every offline port build too, which is an unreviewed netplay-only fork change bleeding
into the offline binary. Re-gated all eight guards (the six damage-particle callbacks +
`efManagerImpactWaveProcUpdate` + `efManagerKirbyVulcanJabProcUpdate`) to
`PORT && SSB64_NETMENU` so they compile only into the netmenu binary; the offline binary's `#else`
branch is byte-for-byte the original decomp body.

Did not touch the remaining, more specialized proc-update callbacks (`FoxReflector`,
`PikachuThunderTrail`, `NessPKThunderTrail`/`NessPKReflectTrail`, `MBallThrown`, `YoshiEggLay`,
`CaptureKirbyStar`, `LoseKirbyStar`, `CaptainEntryCar`, `FoxEntryArwing`) — lower frequency/more
complex bodies, worth an individual audit pass rather than a blanket guard.

## Verify

Soak2 session `1339981196` re-run reproducing Kirby A-spam vs shielding Fox showed no SIGSEGV in
this callback family; `RESULT: PASS`, only the `efManagerShieldProcUpdate` sibling crash remained
(fixed separately, see linked doc above). Both `build-netmenu` (`SSB64_NETMENU=ON`) and
`build-offline` (`SSB64_NETMENU=OFF`) rebuild and link clean after the `PORT && SSB64_NETMENU`
re-gate.
