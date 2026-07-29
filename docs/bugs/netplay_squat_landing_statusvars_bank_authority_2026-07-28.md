# Squat / Landing / FallSpecial: C2b bank authority

**Date:** 2026-07-28
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)
**Seed:** soak1 `733611745` (session `745199062`) — after Dead/Rebirth/Damage migration
(KO not reached): FC@569 MATCH inputs, Android **Pass (33)** vs Linux **SquatRv (30)**.
Peers matched through Squat@558; Linux resim `558→559` then stayed in squat chain while
Android entered Pass.

## Root cause

Same half-wired C2 hole: capture reads `bank[Squat]` but `ftStatusVarsSquat()` still wrote
the union. Mid-Squat load restored stale `pass_wait` / `is_allow_pass` → one peer armed Pass,
the other continued SquatWait→SquatRv. Same class as the old scrub carve-out
(`netplay_squat_pass_wait_statusvars_scrub_fc_2026-07-28.md`), now fixed at the bank layer.

Landing / FallSpecial share the interrupt-allow pattern and were still union-authoritative.

## Fix (architecture)

| Overlay | Sidecar? | Notes |
|---------|----------|-------|
| Squat | Yes (`squat_vars`) | SetStatus inits all fields; sidecar covers Pass-interrupt / SquatWait window (Turn recipe). |
| Landing | No | Single `is_allow_interrupt`; status-scoped tagged capture. |
| FallSpecial | No | SetStatus inits every field; LandingFallSpecial shares ownership. |

Files: `decomp/src/ft/ftstatusvars.h`, `port/net/sys/netrollbacksnapshot.c`,
`port/net/sys/netplay_statusvars_bank.h`.

## Migrated set (cumulative)

Turn, KneeBend, JumpAerial, Dead, Rebirth, Damage, **Squat, Landing, FallSpecial**.

## Related

- [squat pass_wait scrub FC](netplay_squat_pass_wait_statusvars_scrub_fc_2026-07-28.md)
- [landing allow interrupt scrub](netplay_landing_allow_interrupt_statusvars_scrub_fc_2026-07-28.md)
- [Dead/Rebirth/Damage bank](netplay_dead_rebirth_damage_statusvars_bank_authority_2026-07-28.md)
