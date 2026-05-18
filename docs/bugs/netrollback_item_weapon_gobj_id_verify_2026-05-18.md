# NetRollback item/weapon load-verify vs GObj id

**Date:** 2026-05-18  
**Status:** RESOLVED (verify alignment)

## Symptoms

After extended prediction stress, rollback could abort with `LOAD_HASH_DRIFT` where **only** `item=` differed (stored vs live) while fighter/world/RNG/camera/anim matched. Logs showed item spawn/respawn around the failure window.

## Root Cause

`syNetSyncHashActiveItems()` and `syNetSyncHashActiveWeapons()` accumulate each object’s **`gobj->id`**. Snapshot apply **respawns** missing items/weapons via normal setup paths, which allocate **new** GObj ids. The slot’s stored `hash_item` / `hash_weapon` were taken after capture using the **old** ids, so post-load verify failed even when semantic item/weapon state matched the blob.

## Fix

- `syNetSyncHashActiveItemsForRollback()` — same traversal and gameplay fields as the diagnostic hash, but **no item GObj id**; adds **`ip->type`** and **`ip->team`** so the fold tracks blob-relevant item identity.
- `syNetSyncHashActiveWeaponsForRollback()` — same for weapons (**no weapon GObj id**; includes **`wp->team`**).

Rollback snapshot save, load verify, `syNetRollbackCollectHashes()`, and NetSync `item=` / `wpn=` lines use the ForRollback helpers so peer-visible digests match load-verify semantics.

**Note:** Verify still assumes **link-walk order** matches before/after apply. If respawn ever reorders the item chain differently, hashes can still drift; a sort-key multiset hash would be the next step.
