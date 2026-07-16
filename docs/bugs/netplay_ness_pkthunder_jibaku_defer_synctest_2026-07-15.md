# Netplay: Ness PK Thunder jibaku defer window synctest eff+wpn cull

**Date:** 2026-07-15 (deepened 2026-07-16)  
**Scope:** `PORT && SSB64_NETMENU`  
**Status:** FIX DEEPENED (`PORT && SSB64_NETMENU`, re-soak pending)  
**Soak:** `2002670733` seed `1366917897` @2071; `1277442040` seed `1703073336` @751 (Android client ↔ Linux host, Dream Land, Ness)

## Symptom

Both peers `SYNCTEST_FAIL` with `LOAD_HASH_DRIFT diverged=eff,wpn` on the jibaku trigger tick:

- Forward save: `weapon_count=5`, `effect_count=1` during AirHiJibaku with `cull_at_tick = now+2`.
- Capture fold shows the PK wave still live (`eff_fold_diag count=1`) but stamped `respawn=0` once status left Hold.
- Synctest load: weapons respawn then mass-culled (`w=0`); PK wave ejected by `hidden_cosmetic_verify`; verify `eff/wpn=0x811C9DC5`.
- Weapon-repair re-applied the ring OK; effect-repair failed → `SYNCTEST_FAIL`.

## Root cause

Jibaku deferred teardown (`DeferPKCullUntilTick = now + DEFER_CULL_TICKS`) intentionally keeps Head+Trails (+ PK wave) live for two ticks after trigger. Snapshot **save** already skips `CullAllOrphanPKThunderLive` while global defer is armed.

Apply / coupled-rebind / orphan-cull / verify still treated jibaku like End or Hold-empty:

1. `syNetRbSnapFighterInNessPKThunderScope` includes Jibaku/Bound.
2. `syNetRbSnapFighterIsInPKThunderSpecialHiStatus` / `PKWaveScope` are Hold/Start only.
3. Else-branch NULLed `pkthunder_gobj` and `CullOwned(..., NULL)` → destroyed the slot-restored ring.
4. `EffectRespawnKindFromLive` stamped `respawn=NONE` on the still-live wave at the jibaku tick → `hidden_cosmetic_verify` ejected it and cleared `is_effect_attach` before Ensure could remint.
5. `LiveWeaponIsPKThunderPreserve` returned FALSE for any `slot != NULL`, so deferred unmatched eject could still mass-destroy a nonempty jibaku ring after rematch missed new instance ids.

Cannot gate preserve on `IsPKThunderGlobalDeferActive()`: synctest probes often run with `GetTick` past `cull_at` after the latch cleared.

## Fix

`port/net/sys/netrollbacksnapshot.c`:

| Path | Change |
|------|--------|
| Helper | `syNetRbSnapFighterShouldPreservePKThunderWeapons` = Hold/Start **or** `syNetplayNessFighterInPKJibakuCatchUpScope` |
| Coupled rebind | Jibaku/Bound: leave weapons alone (no Reconcile — Head is often Collide/Destroy) |
| Fighter post-apply | Jibaku/Bound: no mass-cull / no `is_effect_attach` clear |
| `CullAllOrphanPKThunderLive` | Skip mass-cull for jibaku catch-up (Hold path unchanged) |
| `LiveWeaponIsPKThunderPreserve` | Preserve during jibaku when `slot == NULL`, **or** when slot lists PK Thunder for that player |
| `EffectRespawnKindFromLive` / blob PK-wave scope | Stamp `NESS_PK_WAVE` during jibaku/bound as well as Hold/Start |
| `VerifyProtectNessPKWaveShell` | Keep deferred wave out of `hidden_cosmetic_verify` |
| `PruneStaleNessPKWaveEffects` | Keep any live PK wave during jibaku catch-up on slot apply/verify |
| `FighterInNessPKWaveScope` | Also accept `nFTKindNNess` |

Offline / non-netmenu unchanged. Hold-only live-forward prune after defer expires is unchanged (still drops the wave).

## Test plan

- [ ] Re-soak Ness Up+B → jibaku; synctest over `cull_at_tick-1` must not `LOAD_HASH_DRIFT diverged=eff,wpn`.
- [ ] Capture at jibaku tick must fold `respawn=5` (not `0`) for the deferred wave.
- [ ] Control: after defer expires, weapons+wave still tear down (`jibaku_post_cull action=cull`, `effect_count=0`).
- [ ] Control: empty-slot Hold Start verify cull still strips unmatched PK Thunder.
