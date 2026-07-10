# Netplay Samus bomb explode VFX + proximity contact — 2026-07-07

**Status:** FIX IMPLEMENTED (re-soak pending)

## Symptom

soak2 Samus down-B spam near Yoshi (session with `SpecialLwCheck` passes in logs):

1. **Instant explode when spawned overlapping/near enemy** — bomb appears to detonate immediately with no visible fuse.
2. **Intermittent missing explosion animation** — hitbox + `ExplodeS` sound play, but no white sparkle VFX.

No `SYNCTEST_FAIL` / `wpn` drift tied to bombs in the soak logs; presentation + vanilla contact semantics.

## Root cause

### Missing sparkle (port + netplay)

1. **Offline PORT:** `efManagerSparkleWhiteMultiExplodeMakeEffect` could return NULL when particle draw
   infrastructure was not initialized (same class as Link bomb / menu fixes). Fixed earlier in
   `wpsamusbomb.c` via `efDisplayEnsureParticleDrawInfrastructure()`.

2. **Netplay rollback:** LBParticles are wiped by `syNetRbSnapResetParticlesForRollback()` on every
   load. Weapon blob apply respawns Samus bombs through `wpSamusBombMakeWeapon()` (wait-phase procs)
   even when the blob is in the **explode window** (`lifetime <= 6`, `attack_coll.size == 180`).
   Unlike Yoshi egg explode, there was no `syNetRbSnapReapplySamusBombExplodeAfterBlob` proc rebind
   or cosmetic sparkle replay. Ring-history replay (`syNetRbSnapReplayExplodeSparklesFromRing`) only
   covered items + Yoshi egg weapons.

3. **Shared replay helper:** `syNetRbSnapReplayCosmeticExplodeSparkle` did not call
   `efDisplayEnsureParticleDrawInfrastructure()` before spawning particles.

### Instant explode near enemy (not a sim bug)

Samus bomb wait phase uses an active attack coll (`can_setoff`, damage 9). On spawn, if the bomb
translate overlaps an enemy hurtbox, `ftMainSearchHitWeapon` → `wpSamusBombProcHit` fires the same
frame or next frame depending on fighter link order — **vanilla SSB64 contact explode**. With missing
VFX and at most 1 frame of bomb sprite visible, this reads as a broken instant pop rather than a
contact detonation.

## Fix

| File | Change |
|------|--------|
| `decomp/src/wp/wpvars.h` | `WPSAMUSBOMB_EXPLODE_EFFECT_SCALE` (1.3F, Link bomb parity) |
| `decomp/src/wp/wpsamus/wpsamusbomb.c` | PORT sparkle helper uses shared scale constant |
| `port/net/sys/netrollbacksnapshot.c` | `syNetRbSnapWeaponBlobWantsSamusBombExplodeSparkleReplay`; `syNetRbSnapReapplySamusBombExplodeAfterBlob` (proc rebind + `dl=NULL` + sparkle); ring replay + slot tick probe; `efDisplayEnsureParticleDrawInfrastructure` in shared sparkle replay |

## Re-soak pass criteria

Samus vs Yoshi soak2, spam down-B at point-blank:

- Contact/overlap detonations show white sparkle at bomb position (both peers).
- Timer detonations show sparkle after rollback/resim loads mid-explode window.
- No new weapon hash drift / synctest failures.

**Diag env (optional):** `SSB64_NETPLAY_SNAPSHOT_PARTICLE_DIAG=1`
