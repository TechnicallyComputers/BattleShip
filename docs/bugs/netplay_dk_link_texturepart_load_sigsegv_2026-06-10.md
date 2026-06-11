# Netplay DK+Link texturepart load SIGSEGV (Phase 45)

**Date:** 2026-06-10  
**Symptom:** Soak1 match 3 (DK + Link dual Appear @239) — SIGSEGV in `ftParamSetTexturePartID` during resim initial load. Matches 1–2 (Yoshi/Kirby, Phase 44 baseline) completed cleanly.

## Root cause

`syNetRbSnapRefreshFigatreePresentationFromSlot` → `syNetRbSnapApplyFighterModelPartsFromSlot` → `syNetRbSnapApplyFighterModelPartsFromBlob` replayed texturepart cursors for **every** fighter during load finalize.

Donkey Kong's `FTAttributes` has `textureparts_container = NULL` (no facial texture parts). `ftParamSetTexturePartID` dereferences the container unconditionally → null fault.

Link has a valid container; crash occurred when iterating DK (player 0) after both fighters' `apply_after` logs during `PrepareLoadedSlotForVerify`.

The cosmetic blob path already skipped `texture_id_curr < 0`; the main load path did not guard NULL container or negative texture ids.

## Fix

1. **`syNetRbSnapFighterHasTexturePartsContainer()`** — skip texturepart DL replay when `attr->textureparts_container == NULL`.

2. **`syNetRbSnapApplyFighterModelPartsFromBlob`** — skip `modelpart_id < 0`; only call `ftParamSetTexturePartID` when container exists and `texture_id_curr >= 0`.

3. **`syNetRbSnapApplyFighterModelPartsCosmeticFromBlob`** — same NULL-container guard (defense in depth).

## Files

- `port/net/sys/netrollbacksnapshot.c`

## Verification

Re-run soak1 match 3 (DK+Link dual Appear @239). Expect resim initial load to survive, baseline gate open, and `resim complete epoch=3` without SIGSEGV.
