# Netplay intro defer presentation + forward-resim camera

**Date:** 2026-06-11  
**Status:** FIX SHIPPED (re-soak pending)  
**Soak:** soak1 Yoshi's Story (`gkind=5`), Kirby AppearL + Yoshi AppearR, intro resim @229 + FC @360

## Symptom

After Phase 1/2 preemptive baseline cap fixes (no sim freeze at `@232`):

1. **One-player offset pop** — Kirby AppearL still looked spawned at bottom/side briefly after intro resim (presentation lag while seal-wait / preemptive cap defers `gcRunAll`).
2. **Camera stops following** — `cam=0x4076A21D` flat in rollback hash during intro Appear while fighters animate; worsened during interface defer (no `gmCameraRunFuncCamera`).

## Root cause

1. **Defer window** — `syNetRollbackShouldDeferInterfaceDuringResimWait` skips `ifCommonBattleUpdateInterfaceAll` (including `gcRunAll` + camera integrate) to prevent `LOAD_SLOT_LIVE_DRIFT`, but left Appear figatree/gobj presentation stale relative to held sim.
2. **Forward resim camera** — `RefreshIntroPresentationAfterForwardResimTick` re-pinned from **load anchor** every resim tick, fighting forward camera motion (FC epoch 2: baseline/post `cam=0x4076A21D/0x6477A753`).
3. **live_apply tail** — Preemptive baseline echo applied slot `@229` but omitted `ResyncLiveFightersFromSlotForSim` anchor-probe prep.

## Fix

| Item | Change |
|------|--------|
| Defer cosmetic | ~~Appear blob re-pin~~ **2026-06-11 follow-up:** defer-wait is **camera-only**; visible Appear figatree owned by forward resim |
| Replay gate | **camera-only** at gate open; no `post_cosmetic` blob re-pin before forward resim |
| Forward intro sim | `syNetRbSnapshotRefreshLiveIntroPresentationAfterInterface` after `gcRunAll` during `game_status=Wait`; live figatree + `gmCameraRunFuncCamera` |
| Forward resim cam | Per resim tick: **live figatree only** (`RefreshIntroAppearCosmeticLiveFromSim`) + camera integrate — no slot blob re-pin |
| Load presentation | Intro scope: replay-gate **hash-oracle repair only**; skip `RefreshFigatreePresentationFromSlot` until forward resim |
| live_apply | After `RefreshPresentationForLoadedTick`, call `syNetRbSnapshotResyncLiveFightersFromSlotForSim` |

## Verify

Re-run soak1 (Yoshi's Story, Kirby/Yoshi). Expect:

- `appear_presentation_diag phase=defer_wait` during seal-wait / preemptive cap
- No visible Kirby offset pop after `RESIM_BASELINE_ECHO live_apply`
- Camera tracks through intro resim burst and post-GO; epoch 2 FC resim camera advances per tick (`cam` hash changes on resim ticks, not frozen at load anchor)
