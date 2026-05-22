# Pikachu Thunder Jolt empty throw — Phase 5b spawn fallback

**Date:** 2026-05-22  
**Status:** FIX SHIPPED (Phase 5b.1 soak pending)  
**Subsystem:** `port/net/sys/netrollbacksnapshot.c`, `ftpikachuspecialn.c`, `ftkirbycopypikachuspecialn.c`

## Symptom

Netplay soak: Pikachu neutral B (`status=222` / SpecialN) enters throw anim but `weapon_count=0` for many frames — same class as Mario fireball empty throws before Phase 5b.

Log examples (`client-auto.log`):

- Tick 638: `play forward events … frame_begin=4` → jolt absent ticks 638–654 (17 frames).
- Tick 705: fresh `play all events` SpecialN → jolt absent ticks 705–724 (~20 frames).

Mario logs showed `fireball_spawn path=emergency`; Pikachu had no equivalent helper.

## Root cause

| Layer | Issue |
|-------|--------|
| **InitStatusVars** | `ftPikachuSpecialNInitStatusVars` clears `flag0` after anim events set spawn flag (same as Mario). |
| **Mid-frame entry** | Air→ground transition uses `play forward events` from `frame_begin>0`, skipping spawn event at anim start. |
| **Physics-map gap** | `proc_accessory` skipped when catch/capture routing blocks Default/Capture path (Mario Phase 5b). |
| **No port fallback** | Only Mario/Luigi had `syNetRbSnapTrySpawnFireballFromAccessory`. |

## Fix

| Area | Change |
|------|--------|
| **Spawn helper** | `syNetRbSnapTrySpawnThunderJoltFromAccessory()` — flag0 + emergency at frame ≥4, flag1 latch, hand dedup, `jolt_spawn` diag. |
| **ProcUpdate fallback** | New `ftPikachuSpecialNProcUpdate` / `SpecialAirNProcUpdate` (+ Kirby copy) call spawn when `syNetRbSnapThunderJoltProcAccessoryWillRun()` is false. |
| **ProcAccessory** | PORT path delegates to spawn helper (vanilla body under `#else`). |
| **Status desc** | Replace `ftAnimEndSetWait` / `ftAnimEndSetFall` with new ProcUpdate wrappers. |

## Verification

Soak with `SSB64_NETPLAY_SNAPSHOT_WEAPON_DIAG=1`:

- One `jolt_spawn path=anim|emergency` per B press for Pikachu P0 and Kirby copy-Pikachu.
- No multi-second `weapon_count=0` windows while `status=222`.
- Normal rollback jolt respawn unchanged (blob path still uses `nWPKindThunderJoltAir`).

## Regression (Phase 5b.1)

Emergency spawn re-armed every ~7 frames after air jolt became ground jolt: `syNetRbSnapThunderJoltOwnedByFighter()` only counted `nWPKindThunderJoltAir`, so `flag1` cleared while ground segments still lived and `anim_frame >= 4` kept emergency armed. Logs showed multiple `jolt_spawn path=emergency` per B press and `weapon_count` climbing to 9.

**Fix:** Mirror Mario fireball Phase 5 + 5b.1 — count air **and** ground jolts in owned/dedup, hand reacquire/cull, farthest cull on cap retry, `throw_preserve` latch (`flag1` + 25-frame window), and `syNetRbSnapLiveWeaponIsThunderJoltThrowPreserve()` for deferred eject.

## Timing (2026-05-22 soak)

Emergency frame **4** fired the jolt ~17 frames before retail (`WaitAsync(21)` + `SetFlag0` in `dPikachuMainMotion_0x15A4` / `0x15F0`). Logs: `path=emergency` at `anim_frame=4`, latched through lunge.

**Fix:** `SYNETRB_SNAP_THUNDERJOLT_EMERGENCY_FRAME` 4 → **21** (spam latch unchanged).

## Related

- [fireball_spawn_phase5b_2026-05-22.md](fireball_spawn_phase5b_2026-05-22.md)
- [netplay_pikachu_jolt_synctest_segv_2026-05-22.md](netplay_pikachu_jolt_synctest_segv_2026-05-22.md)
