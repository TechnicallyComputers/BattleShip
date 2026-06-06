# Yoshi egg shield (Z) rollback presentation — 2026-06-06

**Status:** FIX SHIPPED (soak pending)  
**Scope:** `port/net/sys/netrollbacksnapshot.c`

## Symptom

Yoshi Z shield (egg bubble) under netplay rollback: bubble could respawn without hiding Yoshi's body, hit status could stay wrong after prune/heal, stale bubble ids could restore at GuardOn (vanilla has no bubble until Guard), and release teardown skipped model reset / egg-break VFX.

## Root cause

Generic guard-shield reconcile treated Yoshi like other fighters at the effect GObj layer only. Vanilla Yoshi shield also requires model-part hide/reset, per-status hit status, GuardOn-without-bubble timing, and egg-break presentation on release — none of which ran on load respawn, rebind, prune teardown, or stale heal paths.

## Fix

| Change | Purpose |
|--------|---------|
| `syNetRbSnapApplyYoshiShieldPresentation` / `TeardownYoshiShieldPresentation` / `CoupleYoshiShieldEffect` | Shared spawn/teardown: hide/reset model parts, hit status, optional egg-break VFX |
| Snapshot capture + `syNetRbSnapGuardEffectIdFromBlob` | Yoshi: save/restore bubble id only when `is_shield` is set |
| `syNetRbSnapTryRespawnEffectFromBlob` (YOSHI_SHIELD) | Load respawn couples presentation like live ensure |
| `syNetRbSnapRebindFighterEffectGobjs` | Reapply presentation after adopt/rebind when bubble is live |
| `syNetRbSnapPruneGuardOnExtraShieldEffects` | Eject orphan Yoshi bubble at GuardOn without `is_shield` |
| `syNetRbSnapFighterShieldReleaseTeardown` / heal | Yoshi model + hit-status cleanup on forced teardown / stale heal |

## Verify

Yoshi Z tap-spam and hold-release under rollback (`SSB64_NETMENU=ON`). Body hidden inside egg during Guard; visible after release; no bubble at GuardOn-only frames; egg-break VFX on genuine release teardown. Compare offline GuardOn→Guard→release sequence.

Related: [netplay_guard_shield_tap_churn_2026-06-05.md](netplay_guard_shield_tap_churn_2026-06-05.md), [netplay_guard_shield_snapshot_load_2026-06-01.md](netplay_guard_shield_snapshot_load_2026-06-01.md).
