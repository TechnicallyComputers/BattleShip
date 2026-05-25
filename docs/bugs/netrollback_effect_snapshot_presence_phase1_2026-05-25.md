# Rollback: effect presence snapshot (phase 1) — 2026-05-25

**Status:** SHIPPED (phase 1)  
**Scope:** PORT netplay rollback ring + load apply path.

## Problem

Effect GObjs (`nGCCommonLinkIDEffect` / `nGCCommonLinkIDSpecialEffect`, userdata `EFStruct`) were **not** part of the typed snapshot. During prediction/resim, **free-floating** effects (explosion sparkles, quake timelines, Kirby vulcan‑jab style proc motion, etc.) could **diverge in count and lifetime** vs the authoritative slice at the save tick. That churn runs **alongside item apply** (`itMainDetachOrphanHoldDisplay` / spawn paths touching `lbCommonEjectTreeDObj`) and complicates teardown ordering.

Several effect procs touch **presentation-adjacent** state (camera quake velocity, DObj translates). Full physical collision on effects is uncommon, but timelines still matter for **deterministic replayfeel** vs peers.

## Phase 1 design (presence + reconcile, not generic respawn)

There is **no** `efManagerRespawnCommonFromBankId` analogue to `itManagerMakeItemSetupCommon`. Generic “respawn arbitrary effect from serialized `EFStruct`” is intentionally **not** attempted here — too many constructors and pointerful `effect_vars` union members.

Instead:

1. **Capture** up to `SYNETRB_SNAPSHOT_MAX_EFFECTS` (48) sorted userdata effects: `gobj_id`, `bank_id`, `link_id`, `fighter_gobj_id`, `anim_frame`, `proc_update` fingerprint (`syNetRbSnapPointerFingerprintLow32` — intraprocess only).
2. **Hash** (`syNetSyncHashActiveEffectsForRollback`) merges the same capped walk; fields mirror the blob (**omits effect GObj id** from the digest fold, like item rollback hashing philosophy).
3. **Apply (`syNetRbSnapReconcileSnapshotEffectsBeforeItems`)** runs **after** fighters/map/world, **before** items:
   - For each blob with a live `gcFindGObjByID` hit, **`anim_frame` is rewritten** (`syNetRbSnapApplyEffectPresence`).
   - **Eject** free-floating effects (`EFStruct::fighter_gobj == NULL`) whose `gobj->id` is **missing** from the snapshot list. Fighter-coupled effects (shields, Fox reflector, egg-lay, …) **skip** this eject branch.

Coupled-effect **pointers** in `FTStatusVars` are always scrubbed on capture/apply and rebound from blob ids — see [`netrollback_coupled_pointer_stability_2026-05-25.md`](netrollback_coupled_pointer_stability_2026-05-25.md).

## Limits / follow-ups

- **No** `effect_vars` blob / no pointer scrubbing table per `bank_id`.
- **`hash_effect`** participates in peer baseline v2 compare and resim gate match when the peer packet includes the effect tail.
- If live effect count exceeds 48 or snapshot save fails truncation guard, rollback save fails loudly (parity with item/weapons caps).

## Diagnostics

| Env | Effect |
|-----|--------|
| `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1` | Log effect save counts / truncation |

`syNetRbSnapshotSynctestProbeEffectMismatch` mirrors weapon probe semantics (truncation or live count mismatch vs slot).

## Code pointers

| Area | Symbol / file |
|------|----------------|
| Enumeration + blobs | [`port/net/sys/netrollbacksnapshot.c`](../../port/net/sys/netrollbacksnapshot.c) |
| Rollback hash | [`port/net/sys/netsync.c`](../../port/net/sys/netsync.c) `syNetSyncHashActiveEffectsForRollback` |
