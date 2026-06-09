# Netplay — FallSpecial soft-platform pass allow_pass union stomp (2026-06-09)

**Date:** 2026-06-09  
**Status:** Fix shipped (re-soak pending)  
**Area:** `port/net/sys/netplay_fallspecial_pass_gate.c`, `decomp/src/ft/ftcommon/ftcommonfallspecial.c`, `decomp/src/ft/ftcommon/ftcommonlanding.c`

## Symptom

Samus aerial screw helpless fall (`FallSpecial` 58): inconsistent soft-platform pass with stick held down. `FALLSPECIAL_PASS_DIAG` showed `fallspecial_enter allow_pass=1` but later `proc_pass deny=allow_pass0` at valid pass contacts (~75% failure rate in Samus vs Link soak). Host and guest identical; `rb_applied=0` — forward-sim union stomp, not rollback snapshot corruption.

## Root cause

Vanilla sets `fallspecial.is_allow_pass = TRUE` once in `ftCommonFallSpecialSetStatus` and never clears it. Any `FALSE` read during status 58 is union corruption:

| `fallspecial.is_allow_pass` (offset +4) aliases | Stomp source |
|------------------------------------------------|--------------|
| `squat.pass_wait` | Squat/pass overlay writers |
| `jumpaerial.vel_x` | Jump-aerial overlay writers |

Witness on failed contacts: `status_id=59 accessed=landing expected=fallspecial` — `ftCommonLandingSetStatusParam` wrote `landing.is_allow_interrupt` at union +0 while status 58–59 owns the **fallspecial** overlay (stomps `drift`).

## Fix

| Layer | Change |
|-------|--------|
| **Pass gate** | `syNetplayFallSpecialPassGateHardenAllowPass` in `ftCommonFallSpecialProcPass` when `syNetplayRollbackSemanticsActive()` — restore `is_allow_pass=TRUE` before predicate (generic all helpless `FallSpecial` fighters) |
| **LandingFallSpecial accessor** | Status 59: write/read `ftStatusVarsFallSpecial(...)->is_allow_interrupt` instead of `ftStatusVarsLanding` (netmenu only) |
| **Diag** | `pass_cliff` event logs real `allow_pass`/`block` (was hardcoded) |

## Verification

Re-soak Samus screw pass attempts with `SSB64_NETPLAY_FALLSPECIAL_PASS_DIAG=1`:

1. Soft-platform contacts with `stick_y<-44`, `pass_floor=1` → `proc_pass block=0 deny=none`.
2. Witness `accessed=landing expected=fallspecial` on contact ticks should drop (landing accessor fix on 59).
3. Sim hashes still match; no new desync.

## Related

- [`netplay_samus_screw_fallspecial_pass_diag_2026-06-09.md`](netplay_samus_screw_fallspecial_pass_diag_2026-06-09.md) — diagnostic bisect
- [`netplay_ness_pkthunder_jibaku_fallspecial_landing_2026-06-02.md`](netplay_ness_pkthunder_jibaku_fallspecial_landing_2026-06-02.md) — Pikachu/Ness FallSpecial landing quantize pattern
- [`../refactor/ftstatusvars_overlay_map_2026-06-02.md`](../refactor/ftstatusvars_overlay_map_2026-06-02.md) — overlay ownership
