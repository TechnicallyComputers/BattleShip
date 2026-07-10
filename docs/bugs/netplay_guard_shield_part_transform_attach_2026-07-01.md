# Netplay — guard shield bubble mis-centered after part-transform rebuild

**Date:** 2026-07-01  
**Status:** Fix implemented (`PORT && SSB64_NETMENU`, soak pending)  
**Area:** `port/net/sys/netrollbacksnapshot.c`

## Symptom

Post–resim-complete soak (`soak2`, seed `2997445832`): shield bubble still renders **centered on the fighter body** during guard windows (~tick 736+) even though sim/hash is paired (`bubble=1`, `shield_player=1`, cross-peer `fhash_full` match). Resim-complete-only attach refresh ([netplay_resim_complete_guard_shield_attach_2026-07-01.md](netplay_resim_complete_guard_shield_attach_2026-07-01.md)) did not fix it — guard was held ~216 ticks after resim @520.

## Root cause

Phase 39 `syNetRbSnapRefreshGuardShieldJointAttachFromFighters()` re-pins shield `user_data.p` to `joints[nFTPartsJointYRotN]` after figatree/joint restore, but **`syNetRbSnapRebuildIntroFighterPartTransforms()`** runs in many paths (forward resim tick, residual-shield figatree repair, synctest prep, intro countdown) **without** a following attach refresh. Part-transform rebuild updates YRotN world matrices while the bubble keeps a stale attach draw state.

Resim-complete refresh was one-shot and ran before any guard bubble existed.

## Fix

1. **`syNetRbSnapRebuildIntroFighterPartTransforms`** — when `syNetRbSnapshotAnyFighterGuardScopeActive()`, call `syNetRbSnapRefreshGuardShieldJointAttachFromFighters()` at the end (covers all rebuild call sites).
2. **`syNetRbSnapReconcileGuardShieldEffectsCore`** — live-forward path: refresh attach after bubble ensure/heal/diag each tick while guard is active.
3. Remove redundant explicit refresh at resim-complete (now covered by rebuild wrapper).

## Verify

Re-soak Sector Z cross-ISA with forced resim @519; hold shield during patrol (~736+). Bubble should render in front of Fox/Pikachu. `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1`: single `shield_player=N` row, `bubble=1`.
