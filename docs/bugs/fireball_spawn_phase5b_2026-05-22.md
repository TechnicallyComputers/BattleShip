# Fireball spawn Phase 5b — ground Kirby + spam cap + emergency frame

**Date:** 2026-05-22  
**Status:** FIX SHIPPED (soak pending)  
**Subsystem:** `port/net/sys/netrollbacksnapshot.c`, `ftmariospecialn.c`, `ftkirbycopymariospecialn.c`

## Symptom (post Phase 5 soak)

- Kirby **ground** Copy-Mario SpecialN (`status=269` / `0x10d`): full throw anim, `weapon_count=0`, no `fireball_spawn` log (3/3 on client+host).
- Kirby **air** Copy-Mario (`0xe7`): emergency spawn worked after tick ~1262.
- Mario/Kirby air spam: occasional empty throws at `weapon_count=5` with no log (silent `flag1` + owned-fireball early return).
- All successful spawns used `path=emergency` only; ~5-tick empty window before frame-6 emergency.

## Root cause

| Layer | Issue |
|-------|--------|
| **Physics map gap** | `proc_accessory` runs inside `ftMainProcPhysicsMap`, skipped when `catch_gobj` / `is_catch_or_capture` routing leaves neither Default nor Capture path active — ground Kirby throws (often `catch=1`, `ga=0`) never reached the spawn helper. |
| **flag1 latch** | Early `flag1 && owned` return blocked new throw spawns during B-spam while old fireballs still live. |
| **Resolve** | Kirby `copy_id` could fail resolve while status still indicated Copy Mario/Luigi SpecialN. |
| **Weapon cap** | `wpMarioFireballMakeWeapon` returned NULL at five live fireballs with no retry. |
| **UX** | Emergency threshold at frame 6 added visible empty throws. |

## Fix (Phase 5b)

| Area | Change |
|------|--------|
| **ProcUpdate fallback** | Mario/Luigi + Kirby copy Mario/Luigi SpecialN `ProcUpdate` calls spawn only when `syNetRbSnapFireballProcAccessoryWillRun()` is false (catch/physics-map gap). Luigi uses the same `ftMarioSpecialN*` callbacks from `dFTLuigiSpecialStatusDescs`. |
| **flag1** | `flag1 && owned → return` per throw; clear flag1 only when owned ball is gone (mid-throw destroy). New throw clears flag1 in `InitStatusVars`. |
| **Resolve** | Mario/Luigi/`NLuigi`/`MMario` fkind; Kirby `copy_id` + Copy Mario/Luigi status fallback (fireball index 0 vs 1). |
| **Cap** | On `MakeWeapon` NULL, cull farthest owned fireball and retry once. |
| **Emergency frame** | `SYNETRB_SNAP_FIREBALL_EMERGENCY_FRAME` 6 → **3**. |
| **Diag** | `SSB64_NETPLAY_SNAPSHOT_WEAPON_DIAG=1`: `fireball_spawn skip=wait_frame|resolve_index|spawn_joint_null|skip_dedup|make_weapon_null` with status/copy_id/anim_frame. |

## Regression (Phase 5b.1)

Clearing `flag1` when hand reacquire failed re-armed emergency spawn **every frame** after the ball left the hand (~15+ per B). Unconditional `ProcUpdate` + `ProcAccessory` doubled attempts.

**Fix:** Restore `flag1 && owned → return` latch (`skip=latched` under weapon diag). `ProcUpdate` calls spawn only when `syNetRbSnapFireballProcAccessoryWillRun()` is false (physics-map gap).

## Verification

Soak with weapon diag: one `fireball_spawn` per B press for Mario, Luigi, Kirby copy-Mario, and Kirby copy-Luigi; ground Kirby B (`0x10d`) should log `path=emergency` by frame 3; no full-window empty `wpn=0x811C9DC5` on ground or spam cap.

## Related

- [`fireball_unified_spawn_2026-05-22.md`](fireball_unified_spawn_2026-05-22.md) Phase 5
