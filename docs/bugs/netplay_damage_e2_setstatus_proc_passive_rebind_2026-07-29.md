# DamageE2 hitlag-exit: missing `proc_passive` rebind (`SetStatus` → Fly*)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** `1473519344` seed `641209120` (Linux host / Android guest, Ness ditto)  
**Prior:** jibaku trail `force_teardown` at `cull_at_tick=1272` matched (`wpn` empty); Hold gravity healthy.

## Symptom

- PEER@1301 `figh`+`anim`+`cam` diverge, `wpn` MATCH empty, `replay_determinism`, inputs agree through load.
- FC@1309 inputs MATCH; P0 `status` **50 vs 51**, `motion` 43 vs 44.
- Mapping: **50 = `DamageE2`**, **51 = `DamageFlyHi`** (not Air2/Air3).
- @1279 both enter DamageE2; figh/P0 light match through @1300 (`fhash_light=0xF5808328`).
- @1301 **Android** → DamageFlyHi (`status_tics=0`, `kb_stack=0`); **Linux** stays DamageE2 (`status_tics=22`, `kb_stack=0x42EA6CF4`). Same TopN / `vel_dmg_air` / hitstun at diverge.

## Root cause

Electric InitDamageVars installs `proc_passive = ftCommonDamageSetStatus` and stores the real knockback status in `damage.status_id` (FlyHi here). On hitlag expiry, SetStatus transitions E2 → that status; `ftMainSetStatus` clears `damage_knockback_stack` (Android `kb_stack=0`).

`syNetRbSnapRebindFighterStatusProcs` nulls `proc_passive` / `proc_lagupdate` and never restores damage handlers (they are not status-table procs). Linux repeatedly `rollback_load` mid-E2 (@1289–1296…) → SetStatus dropped. Android `rb_applied` stayed flat through the window → kept SetStatus → transitioned at hitlag end. Matched seals / figh through 1300; fork is forward-sim on the hitlag-clear tick.

Not an input fork; not SoftLip; not the prior resist-snapshot hole (paused/stack already restored and matched).

## Fix

1. On fighter status rebind, if `status_id` in `[DamageStart, DamageEnd]`: restore `ftCommonDamageSetStatus` for E1/E2 else `ftCommonDamageCheckSetInvincible`, and `ftCommonDamageCommonProcLagUpdate` (mirror InitDamageVars).
2. Fold `damage.status_id` into `fhash_light` / blob light when live status is E1/E2 so deferred-target skew is not hash-blind.

## Acceptance

Ness jibaku (or any electric) multi-hit that leaves a victim in DamageE2 across mid-hitlag resim:

- Both peers transition E2 → same `damage.status_id` on the hitlag-clear tick.
- Matching `kb_stack` / `status_tics` after exit; no PEER `figh` with inputs MATCH from this class.

## Related

- [netplay_damage_knockback_resist_snapshot_2026-07-20.md](netplay_damage_knockback_resist_snapshot_2026-07-20.md) — paused/stack snapshot (different hole)
- [netplay_dead_rebirth_damage_statusvars_bank_authority_2026-07-28.md](netplay_dead_rebirth_damage_statusvars_bank_authority_2026-07-28.md) — Damage C2 bank
- [netplay_ness_pkthunder_jibaku_cull_at_tick_trail_fork_2026-07-29.md](netplay_ness_pkthunder_jibaku_cull_at_tick_trail_fork_2026-07-29.md) — prior kill in same soak family
