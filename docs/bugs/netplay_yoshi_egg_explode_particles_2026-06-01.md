# Netplay: Yoshi egg explode LBParticle missing after rollback

**Date:** 2026-06-01  
**Status:** FIX SHIPPED (soak pending)

## Symptom

Yoshi egg throw sometimes plays shatter SFX without the explode/break particle VFX. Hitbox may still connect.

## Root cause

Egg explode spawns `efManagerYoshiEggExplodeMakeEffect` and `efManagerEggBreakMakeEffect` — both **LBParticle** scripts, not EF GObjs. Sound and weapon explode hitbox are independent of particle success.

Rollback apply calls `syNetRbSnapResetParticlesForRollback()` (`lbParticleEjectStructAll`) with no Yoshi egg replay (unlike link bomb / marumine sparkle replay).

## Fix

In `port/net/sys/netrollbacksnapshot.c` (mirror link-bomb sparkle pattern):

- Detect explode egg weapon blobs: `nWPKindEggThrow`, active attack coll, `size >= WPEGGTHROW_EXPLODE_SIZE`, `lifetime <= WPEGGTHROW_EXPLODE_LIFETIME`.
- `syNetRbSnapReplayCosmeticYoshiEggExplode` respawns both particle scripts at weapon translate.
- `syNetRbSnapReapplyYoshiEggExplodeAfterBlob` rebinds `wpYoshiEggExplodeProcUpdate`, hides egg mesh (`dl = NULL`), refreshes hit positions — called from `syNetRbSnapApplyWeaponBlobToGObj`.
- Ring history scan in `syNetRbSnapReplayExplodeSparklesFromRing` replays recent explode weapon blobs (48-tick window, dedup by position).
- `syNetRbSnapSlotTickHasExplodeSparkleReplay` includes egg explode weapons for synctest defer (`explode_sparkle_probe`).

## Verification

- Build: `cmake --build build --target ssb64 -j 4`
- Manual: Yoshi egg throw through rollback/synctest; shatter particles should track SFX.
- Optional: `SSB64_NETPLAY_SNAPSHOT_PARTICLE_DIAG=1` for `particle_replay kind=yoshi_egg_explode` lines.
