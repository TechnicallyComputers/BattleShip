# Netplay Mario fireball empty throw after latch (mid-throw retry)

**Date:** 2026-06-05  
**Status:** FIX SHIPPED (soak pending)  
**Subsystem:** `port/net/sys/netrollbacksnapshot.c`, `scripts/netplay-trim-logs.py`

## Symptom

Post–effect-xf-stabilization soak (Sector, Mario vs Ness): no SIGSEGV, but Mario fireballs sometimes
vanished early, failed to appear, or did not hit Ness at close range. Trim log showed long
`fireball_spawn skip=latched` runs with `weapon_count=0` while still in SpecialN (`status=223`).

## Root cause

| Layer | Issue |
|-------|--------|
| **flag1 latch** | Phase 5b.1 latched on any `flag1 != 0` without checking for a live owned fireball. After rollback/cull/hit removed the ball, spawn helper returned `skip=latched` for the rest of the throw → empty anim. |
| **Hand duplicate cull** | `syNetRbSnapCullOwnedFireballsNearPose` used spawn-time hand position and 60-unit radius, culling in-flight balls still near Mario when spamming B at close range. |

## Fix

| Area | Change |
|------|--------|
| **flag1** | `flag1 && owned → skip=latched`; when latched but `!syNetRbSnapFireballOwnedByFighter`, clear latch (`skip=latch_clear`) and `path=retry` once if `anim_frame < 25`. |
| **Dup cull** | Cull only fireballs within 30 units of the **live** hand joint (`syNetRbSnapFireballNearFighterHand`), not stale spawn pose / in-flight projectiles. |
| **Trim script** | `collect_fireball_spawn_summary` — path/skip counts, latch_clear/retry/emergency+anim suspects. |

## Verification

Soak with `SSB64_NETPLAY_SNAPSHOT_WEAPON_DIAG=1`: one `path=anim` or `path=retry` per B press; no long
`skip=latched` with `weapon_count=0` mid-SpecialN; `path=retry` only after `skip=latch_clear`.

## Related

- [`fireball_spawn_phase5b_2026-05-22.md`](fireball_spawn_phase5b_2026-05-22.md)
- [`fireball_double_spawn_2026-05-22.md`](fireball_double_spawn_2026-05-22.md)
- [`mario_fireball_empty_throw_2026-05-20.md`](mario_fireball_empty_throw_2026-05-20.md)
