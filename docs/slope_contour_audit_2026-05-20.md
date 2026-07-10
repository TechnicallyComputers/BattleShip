# Slope Contour Full Audit (2026-05-20)

Master taxonomy and verification checklist for the hybrid engine + ROM slope-contour audit.

## Behavior classes

| Class | Examples | Contour expectation | Hitbox expectation |
|-------|----------|---------------------|-------------------|
| A — Locomotion | Run, walk, brake, crouch idle | `SetSlopeContour(3\|4)` OK | N/A |
| B — Grab search | Mario `0x0BEC`, Link `0x0E94` | `(3)` feet only | Angle 361, upright root |
| B′ — Grab search (FULL) | **Yoshi `0x0D34`** | **`(4)` FULL** (vanilla) | Angle 361, **slope-aligned** body/reach on N64 |
| C — Special grab search | Yoshi tongue `0x1730`, Kirby inhale `0x1CCC` | `(0)` at search window | 361; Yoshi reach may still pitch via joints |
| D — Intentional slope attack | Dash attack with `(4)` during coll | `(4)` during active frames | **N64 verify before ROM change** |
| E — World-horizontal attack | Tilts/jabs (361) without grab | Usually no FULL during coll | Horizontal |
| F — Projectile spawn | Fireball, PK Fire, boomerang | No FULL at spawn frame | World-forward spawn |

## Triage tooling

Run:

```bash
python3 scripts/audit-slope-contour.py
```

Outputs:
- `build/slope_contour_audit.csv` — per-site fields (±25 attack coll, ±50 SetThrow/Catch FGM, `class_b_unambiguous`, `catch_script_reachable`)
- `build/slope_contour_audit_summary.md` — counts and class-B priority list

### Classification rules

- `class_b_unambiguous`: ±50 window contains **both** `MakeAttackColl` angle 361 **and** `SetThrow` → fix without class-D N64 arbitration
- 361 coll without SetThrow in ±50 → class D candidate (dash attack false positive risk)
- `catch_script_reachable`: script symbol listed as `AnimCatchFileID` target in `ftdata.c`

## Phase 0.5 — N64 reference protocol

**Gate before Phase 2 class-D ROM edits and Phase 3 bulk audit.**

### Reference hierarchy

1. **Primary:** real N64 hardware (flashcart + capture)
2. **Secondary:** RMG + GLideN64 — never sole authority for class D

### Calibration case (required)

| Field | Value |
|-------|-------|
| Move | Mario standing grab (`dMarioMainMotion_0x0BEC`) |
| Stage | Peach's Castle central roof slope |
| Body tilt | Upright (no FULL root tilt) |
| Reach | Horizontal vs floor normal |
| Feet | Follow slope via `(3)`, not `(4)` |

Record port vs reference screenshot/clip below.

### Calibration results (fill in)

| Source | Body upright | Reach horizontal | Feet on slope | Notes |
|--------|--------------|------------------|---------------|-------|
| BattleShip port | _pending_ | _pending_ | _pending_ | |
| N64 hardware | _pending_ | _pending_ | _pending_ | |
| RMG + GLideN64 | _pending_ | _pending_ | _pending_ | Secondary only |

### Per-site verification template (class D/E)

```
{character, script_symbol, body_tilt Y/N, reach_axis vs floor_normal, reference_source, action}
```

## Phase 1 gate — Mandatory netplay soak

After Phase 1 engine changes, **before Phase 2 ROM edits**:

1. Rebuild: `cmake --build build --target ssb64 -j 4`
2. Deploy to test path (`~/.local/share/BattleShip/`)
3. Source `scripts/netplay-midmatch-fighter-soak.env.example` on **both** peers
4. VS on Peach's Castle: grabs, dash attacks, 5+ min
5. Pass: no new `LOAD_HASH_DRIFT` / `SYNCTEST_FAIL` on `figh`; paired `resim complete`

| Soak run | Date | Pass | Notes |
|----------|------|------|-------|
| Post-Phase-1 | 2026-05-20 | build OK | **Manual:** 2-peer soak with `scripts/netplay-midmatch-fighter-soak.env.example` on Peach's Castle (grabs + dash attacks, 5+ min). Agent cannot run dual-peer session. |

## Per-character checklist (manual)

Stage: Peach's Castle roof + Hyrule steep slope.

| Character | Wait→grab | Run→grab | Special grab | Notes |
|-----------|-----------|----------|--------------|-------|
| Mario | | | N/A | Calibration reference |
| Luigi | | | N/A | |
| Fox | | | N/A | |
| Ness | | | N/A | |
| DK | | | N/A | |
| Captain | | | Up-B grab | |
| Samus | | | Grapple | |
| Link | | | Hookshot | |
| Yoshi | | | Egg Lay | |
| Kirby | | | Inhale | |
| Pikachu | | | N/A | |
| Purin | | | N/A | |

## Phase 2 ROM edits — reverted (2026-05-20)

Initial Phase 2 treated Yoshi `(4)` at `0x0D34` as an anomaly and forced Mario-style `(3)` + a port-only catch-search upright gate. **N64 hardware shows Yoshi grabs slope-align.** Reverted to vanilla ROM; engine keeps stale-tilt fixes only. See [`docs/bugs/fighter_slope_contour_attack_coll_2026-05-20.md`](bugs/fighter_slope_contour_attack_coll_2026-05-20.md).

| Character | Script | Vanilla | Do not |
|-----------|--------|---------|--------|
| Yoshi | `0x0D34` | `(4)` at grab coll | Change to `(3)` |
| Yoshi | `0x0EE0` | No contour at entry | Add `(0)` without N64 proof |
| Kirby/Pikachu/Purin | standing grab | No explicit contour in script | Add `(3)` without N64 proof |
| Captain | `0x1C8C` | No `(0)` before SpecialHi colls | Add `(0)` without N64 proof |

## Related docs

- [`docs/bugs/fighter_slope_contour_attack_coll_2026-05-20.md`](bugs/fighter_slope_contour_attack_coll_2026-05-20.md)
- [`docs/bugs/fighter_slope_contour_lp64_alias_2026-04-29.md`](bugs/fighter_slope_contour_lp64_alias_2026-04-29.md)
- [`docs/foot_slope_misalignment_investigation_2026-04-29.md`](foot_slope_misalignment_investigation_2026-04-29.md)

## Phase 3 — Full audit batch summary (generated)


No class-D ROM edits applied without N64 calibration (Phase 0.5). Engine Phase 1 guards cover stale tilt; Phase 2 fixed standing-grab ROM gaps.


### Batch 1

- **202_MarioMainMotion.c** (50 sites): A=39, B=1, C=1, D=8, orphan=1
- **220_LuigiMainMotion.c** (49 sites): A=38, B=1, C=1, D=8, orphan=1
- Class D sites requiring N64 spot-check: **16**

### Batch 2

- **208_FoxMainMotion.c** (96 sites): A=80, B=1, C=1, D=8, orphan=6
- **238_NessMainMotion.c** (69 sites): A=52, B=4, D=11, orphan=2
- Class D sites requiring N64 spot-check: **19**

### Batch 3

- **212_DonkeyMainMotion.c** (85 sites): A=69, B=1, D=12, orphan=3
- **235_CaptainMainMotion.c** (102 sites): A=79, B=2, D=11, orphan=10
- Class D sites requiring N64 spot-check: **23**

### Batch 4

- **216_SamusMainMotion.c** (87 sites): A=73, B=1, D=8, orphan=5
- **224_LinkMainMotion.c** (105 sites): A=86, B=1, D=17, orphan=1
- Class D sites requiring N64 spot-check: **25**

### Batch 5

- **246_YoshiMainMotion.c** (55 sites): A=30, B=3, C=1, D=3, orphan=18
- **228_KirbyMainMotion.c** (90 sites): A=59, B=2, C=2, D=6, orphan=21
- Class D sites requiring N64 spot-check: **9**

### Batch 6

- **242_PikachuMainMotion.c** (64 sites): A=36, C=4, D=5, orphan=19
- **232_PurinMainMotion.c** (56 sites): A=31, C=1, D=3, orphan=21
- Class D sites requiring N64 spot-check: **8**

## Phase 4 — Special grab search (class C from triage)


11 scripts with `SetSlopeContour(0)` + angle-361 coll in ±50 window:

- `202_MarioMainMotion.c` `dMarioMainMotion_0x12FC`
- `208_FoxMainMotion.c` `dFoxMainMotion_0x1030`
- `220_LuigiMainMotion.c` `dLuigiMainMotion_0x13A4`
- `228_KirbyMainMotion.c` `dKirbyMainMotion_0x12B4`
- `228_KirbyMainMotion.c` `dKirbyMainMotion_0x12B4`
- `232_PurinMainMotion.c` `dPurinMainMotion_0x100C`
- `242_PikachuMainMotion.c` `dPikachuMainMotion_0x0E34`
- `242_PikachuMainMotion.c` `dPikachuMainMotion_0x0E80`
- `242_PikachuMainMotion.c` `dPikachuMainMotion_0x10B0`
- `242_PikachuMainMotion.c` `dPikachuMainMotion_0x1200`
- `246_YoshiMainMotion.c` `dYoshiMainMotion_0x0FAC`

### Projectile spawn audit

Spawn positions use `gmCollisionGetFighterPartsWorldPosition` in per-character `*specialn.c` accessory procs. Phase 1 **stale FULL guard** (post-`proc_slope`) and **root transform invalidation** apply before attack-coll and transform rebuild each tick — no per-character spawn C patches in this pass. Manual verify: fireball / PK Fire / boomerang from idle on Peach's Castle slope.
