# Netplay intro facing — remove mirror-era TopN hammers (2026-06-03)

**Date:** 2026-06-03  
**Scope:** `decomp/src/ft/ftcommon/ftcommonentry.c`, `ftcommonwait.c`, `decomp/src/ft/ftmain.c`  
**Status:** FIX SHIPPED — soak pending (cross-ISA intro + offline parity)

## Symptoms

Match intro Appear animations faced ~90° away from the camera while spawn positions were correct. Layered port fixes from the `entry.lr` union-stomp era forced `TopN.rotate.y = fp->lr * 90°` during Appear figatree playback, fighting AppearL/R motion facing.

## Root cause

Stacked behavior-modifying facing repairs:

1. `fp->lr` kept live through `ftCommonAppearSetStatus` + post-SetStatus TopN preset
2. `ftCommonWaitProcPhysics` re-applied TopN yaw every Wait tick (offline `#ifdef PORT`)
3. `ftMainSetStatus` post-figatree TopN re-apply ran for Appear statuses too

Determinism paths (anim pose quantize, intro joint canonicalize, snapshot joint rotate) were unrelated and retained.

## Fix

| Change | Purpose |
|--------|---------|
| Restore vanilla Appear contract: cache `entry.lr` / `hit_lr`, clear `fp->lr` before `ftMainSetStatus` | TopN neutral at status entry; AppearProcPhysics owns yaw during anim |
| Remove post-AppearSetStatus TopN preset | Stop double-rotation vs AppearL/R figatrees |
| Simplify `ftCommonAppearProcPhysics` to `entry_lr * 90°` only | Drop fp->lr preference + union write-back repair |
| Remove `ftCommonWaitProcPhysics` TopN hammer | Vanilla friction-only Wait physics |
| Gate `ftMainSetStatus` post-figatree TopN re-apply to Wait entry only | Shield-pose figatree nudge fix without touching Appear |
| Remove PostIntro agent log scaffolding | Diagnostics only |

Related: [`netplay_intro_facing_quantize_2026-06-02.md`](netplay_intro_facing_quantize_2026-06-02.md), [`netplay_sector_z_intro_rebirth_quantize_2026-06-02.md`](netplay_sector_z_intro_rebirth_quantize_2026-06-02.md).
