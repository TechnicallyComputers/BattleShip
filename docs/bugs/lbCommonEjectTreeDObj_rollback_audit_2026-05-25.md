# Audit: `lbCommonEjectTreeDObj` call sites vs netrollback apply order

**Date:** 2026-05-25  
**Status:** REFERENCE (no code change in this commit)  
**Motivation:** Resim crash `SIGSEGV fault_addr=0x8` in `lbCommonEjectTreeDObj+0x25` — ROM helper assumes `dobj->child != NULL` when rewiring after `gcEjectDObj(dobj)` ([`decomp/src/lb/lbcommon.c`](../../decomp/src/lb/lbcommon.c) ~L1426). This document maps **every call site** and **rollback load ordering** so fixes target the right coupling (item hold display vs spawn setup vs sim throw).

---

## 1. Direct call sites (game tree)

| # | File | Approx | Function | When it runs |
|---|------|--------|----------|--------------|
| A | [`decomp/src/it/itmanager.c`](../../decomp/src/it/itmanager.c) | ~428 | `itManagerMakeItem` (setup tail) | After `gcSetupCustomDObjsWithMObj` / `itManagerSetupItemDObjs` + optional anim, **only if** `attr->data != NULL`. Ejects **`DObjGetStruct(item_gobj)`** (model root) as part of standard item construction. Also hit by **`itManagerMakeItemSetupCommon`** → rollback **respawn** paths. |
| B | [`decomp/src/it/itmain.c`](../../decomp/src/it/itmain.c) | ~412 | `itMainSetFighterRelease` | **Throw / drop / release from hand:** runs **before** repositioning the item; expects held layout from `itMainSetFighterHold` (attach `joint` in `item_gobj->obj`, inner model at `DObjGetStruct(item_gobj)` with `parent` = joint). |
| C | [`decomp/src/it/itmain.c`](../../decomp/src/it/itmain.c) | ~636 | `itMainDetachOrphanHoldDisplay` | **PORT only.** If `itMainItemHasOrphanHoldDisplay` and `item_gobj->obj != NULL`, ejects inner root and clears `item_gobj->obj`. |

**Note:** `decomp/src/netplay/lb/lbcommon.c` mirrors the same function for the netplay build slice; behavior matches.

---

## 2. Indirect call sites (PORT)

| Path | Mechanism |
|------|-----------|
| **`itMainDestroyItem`** → **`itMainDetachOrphanHoldDisplay`** | When `!is_hold` but `owner_gobj != NULL` (PORT branch ~L352–L360), detach runs before dust/arrow/eject of the item GObj. |
| **`syNetRbSnapReconcileOrphanHeldItems`** → **`itMainDetachOrphanHoldDisplay`** | Item apply **reconcile** pass (see §3). |

Related prior bug class: orphan-hold display vs blob `is_hold` — [`netrollback_reconcile_orphan_hold_display_segv_2026-05-19.md`](netrollback_reconcile_orphan_hold_display_segv_2026-05-19.md), [`netplay_item_drop_orphan_sweep_segv_2026-05-20.md`](netplay_item_drop_orphan_sweep_segv_2026-05-20.md).

---

## 3. Rollback: `syNetRbSnapApplySlotToLive` order

Source: [`port/net/sys/netrollbacksnapshot.c`](../../port/net/sys/netrollbacksnapshot.c) `syNetRbSnapApplySlotToLive` (~L4938).

1. **Fighters** — `syNetRbSnapApplyFighter` for each link (slot index = `fp->player`).
2. **Map** — `syNetRbSnapApplyMap(slot)`.
3. **World** — `syNetRbSnapApplyWorld(&slot->world, slot->tick)`.
4. **Items** — `syNetRbSnapApplyItems(slot)` (see §4).
5. **Fighter MP coll rebind** — `syNetRbSnapRebindAllFighterMPCollPointers`.
6. **Weapons** — `syNetRbSnapApplyWeapons(slot)`.
7. **PORT:** `syNetRbSnapRebindFighterGrabCoupling`, `syNetRbSnapRebindFighterItemHoldCoupling`.
8. **Camera** — `syNetRbSnapApplyCamera`.

Comment in source: presentation / joint anim / grab geometry **finalize** runs later in **`syNetRbSnapshotFinalizeLoad`** (not in this function).

---

## 4. Item apply sub-order (`syNetRbSnapApplyItems`)

Source: ~L3718.

1. Walk **live** item link: match blob by `gobj_id`, else by kind+position; **unmatched** → `gcEjectGObj(gobj)` (no `lbCommonEjectTreeDObj` here — full GObj eject).
2. **Apply** blob to matched GObjs (`syNetRbSnapApplyItemBlobToGObj`): sets `is_hold`, `owner_gobj`, physics, coll, **does not** rebuild DObj trees from scratch.
3. **Respawn** unmatched blobs via `syNetRbSnapRespawnItemFromBlob` → `itManagerMakeItemSetupCommon` → **call site A** (`lbCommonEjectTreeDObj` at spawn).
4. **PORT tail:** **`syNetRbSnapReconcileOrphanHeldItems(slot)`** — sets `fp->item_gobj` from fighter blobs; then for each item, if orphan hold display detected, **call site C** via `itMainDetachOrphanHoldDisplay`.

**Implication:** After step 2, **sim fields** can say “not held” while **display** still shows hold attach tree until reconcile runs. Reconcile is the intentional fix-up. If inner `DObjGetStruct(item_gobj)->child` is already **NULL** (partial teardown, double detach, or corrupt tree), **call site C crashes**.

---

## 5. Post-load pipeline (typical rollback path)

`snapshot.c` `syNetRbSnapshotLoad` applies **`syNetRbSnapApplySlotToLive`** then outer code calls e.g. **`syNetRbSnapshotFinalizeLoad`** → `syNetRbSnapshotFinalizeLoadFromSlot`: fighter presentation / joint anim reapply / grab geometry / coupled weapon refresh / deferred weapon eject.

`netrollback.c` **`syNetRollbackLoadPostTick`** (~L4501): emergency capture → `syNetRbSnapshotLoad` → **`syNetRbSnapshotFinalizeLoad`** → **`syNetRbSnapshotRebindAllFighters`** → joint anim reapply → verify → optional coupling finalize / reapply.

So **item apply + orphan reconcile** happens **before** weapon apply and **before** full presentation finalize. Forward **resim** then runs normal sim procs; **throw** paths hit **call site B** (`itMainSetFighterRelease`).

---

## 6. Risk matrix (what to suspect first)

| Scenario | Call site | Why |
|----------|-----------|-----|
| Crash **right after load** on item-heavy tick | **C** | Reconcile detach with degenerate inner tree after blob/owner mismatch. |
| Crash on **first throw** after resim | **B** | Release assumes hold DObj layout; load left incomplete tree or `child` cleared. |
| Crash when **respawning** item from blob | **A** | Unusual if `attr->data` path builds model without children (would also affect non-rollback). |

**Effects (GObj id 1011)** are **not** snapshot-applied like items; explosion resim can allocate/eject many effect GObjs while item teardown runs. That stresses ordering but does **not** add new direct `lbCommonEjectTreeDObj` sites — focus stays on **item DObj invariants** (A/B/C).

---

## 7. Recommended next steps (engineering)

1. **gdb:** break on `lbCommonEjectTreeDObj`, print `dobj`, `dobj->child`, `dobj->parent`, caller `lr`, item `kind`/`type`/`is_hold`/`owner`.
2. If caller is **`itMainDetachOrphanHoldDisplay`:** log whether **`syNetRbSnapReconcileOrphanHeldItems`** ran this tick (`SSB64_NETPLAY_SNAPSHOT_ITEM_DIAG=1`).
3. If caller is **`itMainSetFighterRelease`:** compare **`itMainSetFighterHold`** invariants vs post-load **`fp->item_gobj`** / item blob `is_hold`.
4. Optional: **`SSB64_NETPLAY_ITEM_OPCODE_TRACE`** + **`ITEM_HASH_TRACE`** (see [`netplay_environment_variables.md`](../netplay_environment_variables.md)) on both peers at crash window.
