# Pikachu DOWN+B (Thunder) SIGSEGV @ SpecialLwStart

**Date:** 2026-05-22  
**Status:** FIX SHIPPED (soak pending)  
**Subsystem:** `decomp/src/ft/ftmain.c`, `ftpikachuspeciallw.c`, `port/net/sys/netrollbacksnapshot.c`

## Symptom

Both peers crash deterministically at sim tick 1297 on DOWN+B:

```
SpecialLwCheck -> PASS, calling down-B for fkind=9
ftMainSetStatus - status=0xe0 motion=199   (SpecialLwStart)
!!!!
CRASH SIGSEGV fault_addr=0x0
pc=...+0x560ecc  lr=0x0
rb_applied=0
```

Distinct from Thunder Jolt synctest crash (`fault_addr=0x3700000000` in `mpCommonRunWeaponCollisionDefault`).

## Investigation

- Crash occurs on **live forward sim**, not rollback load.
- `fault_addr=0x0` + `lr=0x0` → NULL pointer write or indirect call through NULL.
- Likely site: `ftMainSetStatus` TransN joint reparent when `joint->parent` is NULL but `is_use_transn_joint` is set (`transn_parent->child = …` writes through NULL).
- Secondary risk: `ftPikachuSpecialLwMakeThunder` dereferencing `gMPCollisionGroundData` without a guard.

AppImage offset `+0x560ecc` did not resolve in local PIE build (ASLR); hypothesis based on code path between figatree attach and end-of-tick logging.

## Fix

| Layer | Change |
|-------|--------|
| **TransN guards** | PORT NULL checks on TransN joint reset + reparent blocks in `ftMainSetStatus`; skip reparent when `joint`, `parent`, or `child` is NULL. |
| **ftMainPlayAnim** | PORT guard before reading TransN translate for `anim_vel`. |
| **MakeThunder** | PORT NULL checks on `gMPCollisionGroundData` and thunder spawn joint. |
| **Spawn fallback** | `syNetRbSnapTrySpawnThunderFromSpecialLw()` — emergency thunder head spawn when anim `flag0` cleared by InitStatusVars (frame ≥1), with diag + guards; hooked from `ftPikachuSpecialLwStartUpdateThunder` on PORT. |

## Verification

Soak netplay with Pikachu spam DOWN+B:

- No SIGSEGV at SpecialLwStart entry.
- Thunder head appears (`weapon_count≥1` or coupled rebind) within 1–2 ticks of SpecialLwStart.
- `thunder_spawn` diag lines when weapon diag enabled.

## Follow-up (clash / AOE timing)

Emergency spawn at frame 1 fired thunder during SpecialLw **Start** while collide checks only ran in **Loop**; thunder often expired before Loop, skipping status 226 (Hit) and AOE trails.

**Fix:** Emergency frame 1 → **18**; set `flag0=1` after spawn so Loop backup spawn is skipped (vanilla guard semantics); collide check during Start ProcUpdate on PORT; Loop skips duplicate spawn when `thunder_gobj` already set.

## Regression (2026-05-22 soak)

Second soak @809: `fault_addr=0x2000000e1` (status 225 / SpecialLwLoop) on repeat DOWN+B after a prior thunder.

**Root cause:** `ftPikachuSpecialLwStartInitStatusVars` reset `is_thunder_destroy` but left a stale `thunder_gobj` from the previous thunder head. The Start ProcUpdate collide check (PORT-only) called `wpGetStruct` / `DObjGetStruct` on the freed weapon GObj.

**Fix:**

| Change | Rationale |
|--------|-----------|
| Clear `thunder_gobj = NULL` in `StartInitStatusVars` | Fresh pointer each Down-B |
| Remove Start/AirStart ProcUpdate collide checks | Match retail (collide only in Loop); avoids pre-spawn NULL path during Start |
| PORT validate `wp->kind == nWPKindThunderHead` in `CheckCollideThunder` | Defensive if pointer outlives weapon |
| Emergency frames → **21** (jolt) / **24** (thunder) | Match `WaitAsync(21)` / `Wait(24)` + `SetFlag0` in Pikachu motion scripts |

Jolt emergency at frame 4 was ~17 frames early vs retail lunge (`client-auto.log`: `path=emergency` at `anim_frame=4`).

## Related

- [netplay_pikachu_jolt_spawn_2026-05-22.md](netplay_pikachu_jolt_spawn_2026-05-22.md)
- [coupled_weapon_lifecycle_audit_2026-05-20.md](coupled_weapon_lifecycle_audit_2026-05-20.md) (Pikachu Thunder Head coupling)
