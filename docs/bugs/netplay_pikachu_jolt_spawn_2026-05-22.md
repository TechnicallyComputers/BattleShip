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
| **ProcUpdate fallback** | `ftPikachuSpecialNProcUpdate` / `SpecialAirNProcUpdate` (+ Kirby copy) call spawn when `syNetRbSnapThunderJoltProcAccessoryWillRun()` is false **or** `proc_accessory == NULL` (snapshot rebind gap); `flag1` latch dedups when accessory also runs. |
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

## Missed jolt after snapshot rebind (2026-05-22 soak)

First B press could exit SpecialN with `weapon_count=0`: `jolt_spawn skip=wait_frame` through frame 10, then **no spawn attempts** until anim ended. Synctest at mid-throw runs `syNetRbSnapRebindFighterStatusProcs()`, which clears `proc_accessory`; `ftMainRebindStatusProcs` does not restore it. ProcUpdate only called spawn when `syNetRbSnapThunderJoltProcAccessoryWillRun()` was false — but that stays true when physics-map routing is normal, so spawn was skipped while `proc_accessory == NULL`.

Air→ground mid-throw could also `skip=latched` with stale `flag1` when no owned jolt existed for the current throw.

**Fix:**

| Area | Change |
|------|--------|
| **ProcUpdate** | Always call `syNetRbSnapTrySpawnThunderJoltFromAccessory()` (flag1 latch prevents double spawn with proc_accessory). |
| **Snapshot rebind** | Restore Pikachu / Kirby-copy `proc_accessory` when still in SpecialN throw status after rebind. |
| **Air↔ground switch** | Clear `flag1` when `!syNetRbSnapThunderJoltOwnedByFighter()` so landing mid-throw cannot block spawn. |

## Regression (Phase 5b.2)

Unconditional ProcUpdate + `proc_accessory` rebind restore caused **3 jolts per B press** (Mario Phase 5b.1 same class: doubled spawn paths).

**Fix:** Restore Mario gate — ProcUpdate calls spawn only when `syNetRbSnapThunderJoltProcAccessoryWillRun()` is false **or** `proc_accessory == NULL`. Rebind restore + air/ground latch clear unchanged.

## Extra jolt mid-recovery (Phase 5b.3)

Soak logs: every B press showed `path=anim` at frame 21, then `path=emergency` at frame ~43 while still in SpecialN. Tick ~460: `gcEjectGObj` on owned jolt (deferred eject after 25-frame preserve); `weapon_count=0`. Spawn helper cleared `flag1` when `!owned && anim_frame >= 25`, re-arming emergency for frames 43–63. Worse cases spammed `path=emergency` every tick after eject.

**Fix:** Never clear `flag1` inside the spawn helper once set. After preserve window, return `spawn_done` instead of clearing latch. New throw still clears `flag1` in `InitStatusVars`; air↔ground switch clears when `!owned`.

## Extra ground jolt on air→ground landing (Phase 5b.4)

Air throw spawns jolt at frame 21 (`path=anim`). On landing mid-recovery (e.g. frame 36–57), `SpecialAirNSwitchStatusGround` cleared `flag1` when `!owned` (air jolt already ejected or ground segments no longer counted at feet). Same physics tick then ran `proc_accessory` on ground SpecialN with `flag1=0` and `anim_frame>=21` → `path=emergency` at Pikachu's feet (extra bolt).

Log pattern: `skip=spawn_done status=223`, then `play forward events status=0xde frame_begin=57`, then `path=emergency`.

**Fix:** Only clear `flag1` on air↔ground switch when `anim_frame < 21` and `!owned` (stale pre-spawn latch). Preserve `flag1` after spawn so landing cannot re-arm emergency.

## Random jolt on non-Pikachu fighters (Phase 5b.5)

Soak: Falcon spamming A (`btn=0x8000`) with idle Pikachu on stage spawned Thunder Jolt owned by Falcon (`owner_player=1`, `status=222`, `joint=11`). Synctest restore runs `syNetRbSnapshotRebindAllFighters()`, which installed `ftPikachuSpecialNProcAccessory` on any fighter whose numeric `status_id` matched `nFTPikachuStatusSpecialN` — **without checking `fkind`**.

Global status IDs collide at the same offset for different moves:

| Offset | Pikachu | Other fighters at same number |
|--------|---------|-------------------------------|
| +2 (222) | SpecialN | Captain/Link **Attack100Loop**; Fox/Kirby/Purin **Attack100End** |
| +3 (223) | SpecialAirN | Mario/Luigi **SpecialN** (fireball); Captain/Link **Attack100End** |

Fox/Kirby rapid-jab **loop** is offset +1 (221) — not Pikachu SpecialN — but jab end at +2 and Captain/Link loop at +2 were in the collision set.

**Fix:** Gate `syNetRbSnapFighterIsInThunderJoltThrowStatus()` and rebind restore on `fkind == nFTKindPikachu` (or Kirby + copy-Pikachu statuses). Mirror for `syNetRbSnapFighterIsInFireballThrowStatus()` with Mario/Luigi/Kirby-copy only.

## Related

- [fireball_spawn_phase5b_2026-05-22.md](fireball_spawn_phase5b_2026-05-22.md)
- [netplay_pikachu_jolt_synctest_segv_2026-05-22.md](netplay_pikachu_jolt_synctest_segv_2026-05-22.md)
