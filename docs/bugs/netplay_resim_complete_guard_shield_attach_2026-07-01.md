# Netplay — guard shield bubble mis-centered after resim complete

**Date:** 2026-07-01  
**Status:** Superseded by [netplay_guard_shield_part_transform_attach_2026-07-01.md](netplay_guard_shield_part_transform_attach_2026-07-01.md) (soak still failed @736+)  
**Area:** `port/net/sys/netrollbacksnapshot.c`

## Symptom

`soak2` cross-ISA Sector Z (~1200 ticks): after resim @519, holding shield shows the bubble **centered on the fighter body** instead of in front. Logs at guard (~tick 995) show healthy sim state (`is_shield=1`, `bubble=1`, single shield effect, cross-peer `fhash_full` match) — presentation only, not a hash desync.

## Root cause

Phase 39 ([netplay_guard_shield_presentation_reconcile_2026-06-07.md](netplay_guard_shield_presentation_reconcile_2026-06-07.md)) added `syNetRbSnapRefreshGuardShieldJointAttachFromFighters()` on load finalize and figatree refresh, re-pinning shield `user_data.p` to `joints[nFTPartsJointYRotN]` after joint pose restore.

Gameplay resim-complete path (`syNetRbSnapshotRefreshIntroPresentationAfterResimComplete`) rebuilds part transforms and integrates camera but **did not** refresh shield attach — stale YRotN matrix after part-transform rebuild.

## Fix

Call `syNetRbSnapRefreshGuardShieldJointAttachFromFighters()` at the end of the gameplay resim-complete branch (after `syNetRbSnapRebuildIntroFighterPartTransforms` + CObj restore + camera integrate).

Intro/countdown/Appear branches unchanged (no active guard bubble).

## Verify

Re-soak with shield held shortly after forced resim @520; bubble should render in front of Fox/Pikachu. `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1` should still show single `shield_player=N` row with `bubble=1`.
