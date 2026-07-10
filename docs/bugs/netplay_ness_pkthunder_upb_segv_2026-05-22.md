# Netplay — Ness second UP+B SIGSEGV (orphan PK Thunder weapons)

**Date:** 2026-05-22  
**Status:** FIX SHIPPED (soak pending)  
**Subsystem:** Ness SpecialHi + `port/net/sys/netrollbacksnapshot.c` + `decomp/src/wp/wpness/wpnesspkthunder.c`

## Symptom

Both peers crash on sim tick ~817 when Ness uses aerial UP+B a second time after a completed first throw (steer → jibaku or timeout → end). Crash PC in weapon trail update; `fault_addr=0x101000000df` (LP64 stale GObj / token-as-pointer pattern).

**Follow-up @2417:** UP+B into enemy collision (head `ProcHit` → jibaku) still crashed in `wpNessPKThunderHeadSetDestroyTrails+0x8f` with `fault_addr=0xc3509549` while deferred eject destroyed trail GObjs but snapshot still showed `weapon_count=4`.

Logs show `weapon_count=4` persisting from the first throw through idle/recovery while `pkthunder_gobj` was cleared by rollback scrub — second entry into `SpecialAirHiStart` then spawns a new head against orphan trail GObjs.

## Root cause

1. **No exit cleanup** — `ftNessSpecialHiInitStatusVars` reset passive timers but never destroyed owned PK Thunder head/trail weapons or cleared stale `pkthunder_gobj` before a new throw.
2. **Deferred eject gap** — unmatched orphan PK Thunder weapons were not preserved during active SpecialHi, but also were not culled on move exit; synctest weapon apply left head+trails live with decoupled fighter pointer.
3. **Trail proc hazard** — `wpNessPKThunderTrailProcUpdate` / `wpNessPKReflectTrailProcUpdate` dereferenced `owner_gobj` / `head_gobj` DObjs without NULL guards after head eject.
4. **SetDestroyTrails hazard** — head jibaku/hit teardown called `wpNessPKThunderHeadSetDestroyTrails` on trail slots whose GObjs were already ejected by rollback deferred eject; dereferenced stale `trail_gobj[]` pointers.

## Fix

| Layer | Change |
|-------|--------|
| **Entry/exit cleanup (PORT)** | `ftNessSpecialHiPortCleanupPKThunder()` on `InitStatusVars`, `EndSetStatus`, and `AirHiEndSetStatus`; destroys coupled head + culls owned head/trail weapons. |
| **Spawn guard** | NULL-check `FTNESS_PKTHUNDER_SPAWN_JOINT`; cull all owned PK Thunder then always spawn fresh head (dedup/reacquire skip removed — caused @960 desync). |
| **Rollback cull/reacquire** | `syNetRbSnapCullOwnedPKThunderForFighter`, `syNetRbSnapReacquirePKThunderHeadForFighter`; load rebind culls extras / orphans when not in SpecialHi*. |
| **Deferred eject preserve** | `syNetRbSnapLiveWeaponIsPKThunderPreserve()` — skip eject for coupled head + its trails during Ness SpecialHi Start/Hold only. |
| **Trail NULL guards** | Head owner, trail owner, reflect trail head DObj checks return destroy when references are stale. |
| **SetDestroyTrails hardening** | `wpNessPKThunderGObjIsLiveWeapon/Effect`, `wpNessPKThunderHeadOrphanTrailReference()`; safe destroy + NULL slots; pre-eject hook `syNetRbSnapPreEjectPKThunderWeapon()`. |
| **Jibaku entry cleanup** | `ftNessSpecialHiPortCleanupPKThunder()` on `JibakuInitStatusVars` before blast anim. |
| **Pre-destroy hook** | `wpNessPKThunderPreDestroyWeapon()` from `wpMainDestroyWeapon` + rollback deferred eject; clears coupled `pkthunder_gobj`, orphans trail refs. |
| **Spawn guard** | NULL-check `FTNESS_PKTHUNDER_SPAWN_JOINT`; cull all owned PK Thunder then always spawn fresh head (dedup/reacquire skip removed — caused @960 desync). |
| **Trail effect guards** | `efManagerNessPKThunderTrailMakeEffect` / reflect variant validate live head + dedup live effect slot before `efManagerMakeEffect`. |

**Follow-up (same day) — @960 desync:** spawn dedup/reacquire skip removed; see [`netplay_ness_pkthunder_desync_2026-05-22.md`](netplay_ness_pkthunder_desync_2026-05-22.md).

## Soak

1. Ness: UP+B → steer → recall or timeout → land → second UP+B (ground and air). Expect `weapon_count` → 0 between throws with `SSB64_NETPLAY_SNAPSHOT_WEAPON_DIAG=1`.
2. Repeat through several synctest intervals (~120 ticks).
3. Optional: reflect PK Thunder off shield — reflect trail proc should self-destroy if head ejected.
4. UP+B into enemy → jibaku (`SpecialAirHiJibaku` @ tick ~2417 class): no SIGSEGV in `SetDestroyTrails`.
5. UP+B steer into enemy during `SpecialAirHiHold`: no SIGSEGV in `efManagerMakeEffect` on victim `DamageE1` (electric hitstun @ tick ~1682 class).

## Follow-up (2026-05-22) — hold hit / efManager crash

**Symptom:** Single bolt visually, multi-hit on enemy, crash in `efManagerMakeEffect+0x35` (`fault_addr=0x300000003`) when victim enters electric damage after PK Thunder connect. Mid-hold `gcEjectGObj` of PK Thunder weapon left coupled pointers stale.

**Fix:** Global `wpNessPKThunderPreDestroyWeapon()` on all weapon destroys; spawn dedup when coupled head already live; guarded trail/reflect effect spawn with live head + effect-slot dedup.

## Follow-up (2026-05-22) — @2656 repeated aerial UP+B entry SIGSEGV

**Symptom:** After ~5 successful throws, both peers crash deterministically on sim tick **2656** entering aerial UP+B (`weapon_count=0`, sync hashes still match). PC `ftNessSpecialAirHiStartSetStatus+0x21`; same `fault_addr=0x101000000df` (status **0xDF** = `SpecialAirHiStart` token-as-pointer pattern).

**Root cause:** `ftNessSpecialHiPortCleanupPKThunder()` called `wpMainDestroyWeapon()` on a **stale** `pkthunder_gobj` when `wpGetStruct` returned non-NULL on a reused GObj slot, corrupting fighter heap before `fp->physics.vel_air.y` write. `ftNessSpecialAirHiStartSetStatus` also cached `fp` before `InitStatusVars` cleanup.

**Fix:** Require `wpNessPKThunderGObjIsLiveWeapon()` before destroy in `PortCleanup`; always NULL stale coupled pointer; re-fetch `fp` after cleanup in `InitStatusVars`, `JibakuInitStatusVars`, and `SpecialAirHiStartSetStatus`; live-weapon guard in `CheckCollidePKThunder`.

## Related

- [`coupled_weapon_lifecycle_audit_2026-05-20.md`](coupled_weapon_lifecycle_audit_2026-05-20.md) — Ness row marked shipped by this fix.
- [`netrollback_weapon_deferred_eject_2026-05-20.md`](netrollback_weapon_deferred_eject_2026-05-20.md) — preserve hook extended for PK Thunder.
