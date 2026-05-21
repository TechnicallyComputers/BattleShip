# Fighter Slope Contour Stale Tilt on Attack Colliders (2026-05-20)

**Symptoms:** Yoshi Egg Lay tongue grab hitbox follows floor slope angle instead of extending horizontally. Reproduces after run/brake on sloped stages (Peach's Castle roof, etc.); tongue reach tilts up/down with the slope.

**Root cause:** Full-body slope contour (`SetSlopeContour(4)` / `FTSLOPECONTOUR_FLAG_FULL`) writes the fighter root `DObj->rotate.vec.f.x` from `floor_angle` each frame in `mpCommonUpdateFighterSlopeContour`. Attack collider world positions inherit that tilt via `gmCollisionGetFighterPartsWorldPosition` in `ftMainProcAccessory`.

When a new status clears `fp->slope_contour` through `ftMainSetStatus` (without `FTSTATUS_PRESERVE_SLOPECONTOUR`), the flag was zeroed but **root tilt was not reset**. Only the motion-script `SetSlopeContour(0)` event path zeroes `rotate.vec.f.x`. Egg Lay entry uses `FTSTATUS_PRESERVE_NONE`, so stale tilt from run/brake persisted into the tongue submotion (`dYoshiMainMotion_0x1730`), which never managed slope contour.

**Fix:**

1. **`decomp/src/ft/ftmain.c`** — When clearing `slope_contour` on status change, also zero root `rotate.vec.f.x` if `FTSLOPECONTOUR_FLAG_FULL` was active (same semantics as `SetSlopeContour(0)` motion events). Fixes all fighters on status transitions from slope-contoured locomotion.

2. **`decomp/src/relocData/246_YoshiMainMotion.c`** — `SetSlopeContour(0)` at start of tongue submotion `dYoshiMainMotion_0x1730` (matches regular grab ending pattern in `dYoshiMainMotion_0x0D34`).

3. **`decomp/src/relocData/228_KirbyMainMotion.c`** — Same for Kirby copy-Yoshi tongue (`dKirbyMainMotion_0x21D8`) and inhale vacuum window (`dKirbyMainMotion_0x1CCC`).

**Verification:** Build `ssb64`. Manual: run on Peach's Castle slope → Egg Lay; tongue should reach horizontally. Repeat for Kirby inhale and Kirby copy-Yoshi Egg Lay. Compare wait-on-slope → Egg Lay (should already be fine pre-fix).

**Related:** Foot-only contour LP64 alias — `docs/bugs/fighter_slope_contour_lp64_alias_2026-04-29.md` (separate issue).

**Audit note:** Other horizontal grab/search colls (common grab scripts, Captain Falcon grab, etc.) benefit from the global `ftMainSetStatus` fix when entered from a prior full-contour state. Moves that enable `SetSlopeContour(4)` during their own hitbox window (e.g. dash attack) are unchanged.
