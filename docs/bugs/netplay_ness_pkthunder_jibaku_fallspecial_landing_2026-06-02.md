# Netplay — Ness PK Thunder jibaku FallSpecial landing quantize (2026-06-02)

**Date:** 2026-06-02  
**Status:** Fix shipped (soak pending)  
**Area:** `port/net/sys/netplay_ness_pkthunder_gate.c`, `port/net/sys/netplay_sim_quantize.c`, `port/net/sys/netrollbacksnapshot.c`

## Symptom

After air jibaku arc, Ness enters `FallSpecial` then lands. Cross-ISA soak showed `witness stomp` @675 (`accessed=landing expected=fallspecial`) and visible floor snap. Sim hashes stayed matched — presentation / overlay lifecycle, not desync.

## Root cause

PK Thunder exit uses `ftCommonFallSpecialSetStatus` with `FTNESS_PKTHUNDER_FALLSPECIAL_DRIFT` and `FTNESS_PKTHUNDER_LANDING_LAG`. Unlike Pikachu Quick Attack, Ness had no **landing-fall scope** quantize on snapshot capture/apply. Cross-ISA grid rounding on `fallspecial.drift` / `landing_lag` could diverge slightly at the landing transition after a rollback load.

Separately, `pkthunder_pos_refresh` on capture was mutating **live** `fp->status_vars` every ring-save tick during Hold (non-vanilla); fixed in the same milestone (blob-only refresh).

## Fix

| Layer | Change |
|-------|--------|
| **Scope** | `syNetplayNessFighterInPKThunderLandingFallScope` — Ness in `FallSpecial` / `LandingFallSpecial` with PK Thunder drift + landing lag constants |
| **Quantize** | `syNetplayQuantizeNessPKThunderLandingStatusVars` on capture blob + apply (mirrors Pikachu QA landing path) |
| **Canonicalize** | Called from `syNetplayCanonicalizeFighterSimState` during live sim |
| **Capture refresh** | `syNetplayNessRefreshPKThunderPosInBlobFromHead` — head → blob `pkthunder_pos` only; live Hold state untouched |

Air→ground jibaku guards (`ShouldBlockAirJibakuGroundSnap`, defer/grace, pass-floor shallow descent) — see also [hold early exit / pass-floor](netplay_ness_pkthunder_hold_early_exit_pass_floor_2026-06-02.md).

## Verification

1. Cross-ISA Ness air jibaku → FallSpecial → land: no visible snap; optional witness clean through landing (landing accessor during FallSpecial ProcMap may still log — known vanilla pattern).
2. Hold `pkthunder_pos_refresh` only on rollback apply/reconcile (`capture_only=0`) or blob save (`capture_only=1`), not every live Hold tick.
3. Frame-commit / `sim_state_tick` still match through jibaku cycles.

## Related

- [`netplay_ness_pkthunder_jibaku_pkthunder_pos_snapshot_2026-06-02.md`](netplay_ness_pkthunder_jibaku_pkthunder_pos_snapshot_2026-06-02.md)
- [`netplay_ness_pkthunder_orphan_weapons_crash_2026-06-01.md`](netplay_ness_pkthunder_orphan_weapons_crash_2026-06-01.md)
