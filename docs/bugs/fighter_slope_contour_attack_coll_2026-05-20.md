# Fighter Slope Contour — Port Stale Tilt + N64 Grab Policy (2026-05-20)

**Port symptoms (original):** After run/idle on slopes, grab reach or tongue could look wrong when `slope_contour` cleared but root `rotate.x` or `FTParts` transform caches stayed stale (LP64 foot IK was a separate fix).

**N64 reference (hardware, 2026-05-20):**
- Most standing grabs (Mario, Link, Samus, …) use `SetSlopeContour(3)` — feet follow slope, torso stays upright; reach is effectively horizontal.
- **Yoshi standing grab (`0x0D34`) uses `SetSlopeContour(4)` (FULL)** — body and tongue reach **tilt with the slope** on stock hardware. Do not “fix” Yoshi to Mario’s `(3)` pattern.

**Root cause (port-only stale tilt):**
- `FTSLOPECONTOUR_FLAG_FULL` sets root `DObj->rotate.vec.f.x` from floor angle in `mpCommonUpdateFighterSlopeContour`.
- Stale root tilt could persist when `slope_contour` cleared without zeroing rotate or invalidating `unk_dobjtrans_0x5` caches.

## Fixes kept (engine)

| Fix | File | Purpose |
|-----|------|---------|
| `ftMainApplySlopeContourFlags` | `ftmain.c` | When FULL clears via motion event, zero root pitch + invalidate transforms |
| `ftParamInvalidateFighterTransformFromRoot` | `ftparam.c` | Rebuild joint world matrices after root rotate changes |
| Stale FULL guard | `ftmain.c` | After `proc_slope`, if FULL flag off but `rotate.x` ≠ 0, clear it |
| Per-frame invalidate after slope proc | `mpcommon.c` | Ensures attack-coll sampling sees updated transforms |

## Reverted overcorrections (2026-05-20)

These were based on a false “all grabs horizontal on N64” assumption. **ROM and engine restored to vanilla policy:**

| Change | Revert |
|--------|--------|
| Yoshi `0x0D34` `(4)` → `(3)` | **Restored `(4)`** (vanilla FULL during standing grab search) |
| Yoshi `0x0EE0` added `SetSlopeContour(0)` | **Removed** (vanilla: subroutine only) |
| Kirby/Pikachu/Purin added `(3)` at standing grab | **Removed** (vanilla: no explicit contour in grab script) |
| Captain `0x1C8C` added `(0)` before SpecialHi 361 colls | **Removed** |
| `ftMainIsCatchSearchCollActive` catch-search upright gate | **Removed** — forced upright root during FULL+361 catch; blocked Yoshi `(4)` |

**Unchanged (already vanilla):** Yoshi tongue `0x1730` `SetSlopeContour(0)`; Kirby inhale/copy `(0)` scripts.

## Tooling

- [`scripts/audit-slope-contour.py`](../../scripts/audit-slope-contour.py) — triage CSV; **do not** treat class B alone as “change to `(3)`” when `slope_flags` differs (Yoshi `0x0D34` is `(4)` by design).
- [`docs/slope_contour_audit_2026-05-20.md`](../slope_contour_audit_2026-05-20.md)

## Verification

- **Build:** `cmake --build build --target ssb64 -j 4`
- **Manual vs N64 / footage:** Peach's Castle slope — Mario/Link/Samus standing grab (upright + horizontal reach); **Yoshi standing grab + Egg Lay (slope-aligned reach)**; feet still contour on all chars.
- **Class D (~100 sites):** No ROM edits without per-move N64 calibration.

**Related:** [`fighter_slope_contour_lp64_alias_2026-04-29.md`](fighter_slope_contour_lp64_alias_2026-04-29.md) (foot IK LP64 alias).
