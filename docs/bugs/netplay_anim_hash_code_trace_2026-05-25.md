# Netplay `anim=` hash — code trace (sim_state_tick vs fighter_slot_hash)

**Date:** 2026-05-25  
**Status:** REFERENCE — maps `anim=` digest to code. **§3 fix landed same day:** global merge is now **slot-major** (parity with `syNetSyncHashBattleFighters`). Numeric `anim=` values vs older builds/wire captures **changed** (`hash_animation` contract).

## 1. Where `sim_state_tick` gets `anim=`

`SSB64_NETPLAY_SIM_STATE_TICK_INTERVAL` drives `syNetPeerMaybeLogSimStateTickTrace()` in `port/net/sys/netpeer.c`. It samples subsystem hashes each sim tick; **`anim_h`** is:

```10170:10178:port/net/sys/netpeer.c
	f = syNetSyncHashBattleFighters();
	m = syNetSyncHashMapCollisionKinematics();
	{
		u32 world_h = syNetSyncHashRollbackWorld();
		u32 item_h = syNetSyncHashActiveItemsForRollback();
		u32 wpn_h = syNetSyncHashActiveWeaponsForRollback();
		u32 rng_h = syNetSyncHashRNGSeed();
		u32 cam_h = syNetSyncHashGMCamera();
		u32 anim_h = syNetSyncHashFighterAnimationStateForRollback();
```

The log line is emitted a few lines below as `anim=0x%08X` with that `anim_h`.

The same function **`syNetSyncHashFighterAnimationStateForRollback()`** is used for:

- **`syNetPeerLogNetSyncValidation`** (`role= host/client … anim=0x…`) — `port/net/sys/netpeer.c`
- **Rollback snapshot slot** `hash_animation` at save — `port/net/sys/netrollbacksnapshot.c`
- **`syNetRollbackCollectHashes` / `syNetRollbackVerifyLoadedSlot`** (`LOAD_HASH_DRIFT` anim column) — `port/net/sys/netrollback.c`

So the “global” anim digest in logs is one number; it is also the **animation partition** of rollback hash sets and load verification.

## 2. What goes into one fighter’s animation fold

Per fighter, rollback uses **`syNetSyncFoldFighterAnimRollback`** (`port/net/sys/netsync.c`):

- `fighter_gobj->id` (GObj id — can differ across peers if not careful; see below)
- `fp->status_id`, `fp->motion_id`
- `fighter_gobj->anim_frame` (hashed as f32 bits)
- For **each** joint index `0 .. FTPARTS_JOINT_NUM_MAX-1`: **`syNetSyncFoldFighterAnimJointContribution`**

Joint contribution hashes:

- `joint->anim_frame`, `anim_wait`, `anim_speed`
- **Full** `AObj` chain length (`chain_total` — count of nodes)
- First **`SYNETROLLBACK_SNAPSHOT_AOBJ_CHAIN_MAX` (16)** nodes along `joint->aobj`, fields: `track`, `kind`, `length_invert`, `length`, `value_base`, `value_target`, `rate_base`, `rate_target` (f32 bit patterns via `syNetSyncHashF32`)

```173:226:port/net/sys/netsync.c
static u32 syNetSyncFoldFighterAnimJointContribution(DObj *joint, u32 fold)
{
	AObj *aobj;
	u32 aobj_steps;
	u32 chain_total;
	// ... anim_frame / anim_wait / anim_speed ...
	for (aobj = joint->aobj; aobj != NULL; aobj = aobj->next)
	{
		chain_total++;
	}
	fold = syNetSyncFnvAccumulateU32(fold, chain_total);
	for (aobj = joint->aobj, aobj_steps = 0U;
	     (aobj != NULL) && (aobj_steps < (u32)SYNETROLLBACK_SNAPSHOT_AOBJ_CHAIN_MAX);
	     aobj = aobj->next, aobj_steps++)
	{
		fold = syNetSyncFnvAccumulateU32(fold, (u32)aobj->track);
		// ... kind, floats ...
	}
	return fold;
}

static u32 syNetSyncFoldFighterAnimRollback(const FTStruct *fp, GObj *fighter_gobj)
{
	// ...
	fold = syNetSyncFnvAccumulateU32(fold, fighter_gobj->id);
	fold = syNetSyncFnvAccumulateU32(fold, (u32)fp->status_id);
	fold = syNetSyncFnvAccumulateU32(fold, (u32)fp->motion_id);
	fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(fighter_gobj->anim_frame));
	for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
	{
		fold = syNetSyncFoldFighterAnimJointContribution(fp->joints[ji], fold);
	}
	return fold;
}
```

**Tier 1 AObj rebuild** (`docs/bugs/netplay_joint_anim_desync_bisect_2026-05-23.md`) targets **topology + cursor** determinism because this fold deliberately includes **`chain_total`** and the capped prefix walk — a longer live tail than 16 nodes that differs across peers corrupts only the **`chain_total`** term unless the truncated prefix differs too.

## 3. Global merge (**slot-stable**, same scheme as **`figh`** light hash)

**Before 2026-05-25:** folds were merged with **`hash ^= fold`** plus **`FNVAccumulate(hash, player)`** while walking **`link_next`** — order-dependent unless every peer’s fighter link matched.

**Now:** `syNetSyncHashFighterAnimationStateForRollback` folds each fighter’s **`syNetSyncFoldFighterAnimRollback`** into **`slot_hash[fp->player]`** exactly like **`syNetSyncHashBattleFighters`** (same `slot ^ 0x9E3779B9U` mixing, illegal slot spills to slot `0`), then merges **`merged ^ slot_hash[i]`** with **`FNVAccumulate(merged, i)`** for **`i`** in **`0 .. GMCOMMON_PLAYERS_MAX - 1`**. Implementation lives in **`port/net/sys/netsync.c`** next to **`syNetSyncHashBattleFighters`**.

**Implication:** **`sim_state_tick anim=`** no longer spurious-splits purely from **`gGCCommonLinks[Fighter]`** head→tail ordering. **`hash_animation` numeric values changed** versus pre-fix builds — **all peers must run the same build** for rollback anim partition agreement.

## 4. Relation to **`fighter_slot_hash anim_hash=`**

`syNetSyncLogFighterSlotHashes` logs **`anim_hash=0x…`** per fighter from **`syNetSyncHashFighterSlotAnim`**, which is **one fighter’s fold** (**`syNetSyncFoldFighterAnimRollback`**):

```260:263:port/net/sys/netsync.c
u32 syNetSyncHashFighterSlotAnim(const FTStruct *fp, GObj *fighter_gobj)
{
	return syNetSyncFoldFighterAnimRollback(fp, fighter_gobj);
}
```

**Global `anim=`** is **not** a simple XOR of raw per-slot **`anim_hash`** values (empty slots contribute **`slot_hash[i] == FNV_BASIS`**, mixing constants differ); but **meaningful divergence** now tracks **merged slot payloads**, not common-link traversal order.

- **Different per-slot `anim_hash`** ⇒ that slot’s fold differs — classical AObj / joint / **`gobj->id`** work (**`netplay_joint_anim_desync_bisect`**).

## 5. Parity with **`figh=`**

Global animation merge now aligns with **`syNetSyncHashBattleFighters`** (light): **bucket by legal `fp->player`**, **fixed slot merge**.

**`syNetSyncHashFighterAnimationState`** (diagnostic-only in tree: unconstrained **full AObj** walk per joint, **no** 16-cap / **no** `status`/`motion`/`chain_total` semantics of rollback) uses the **same slot-major merge** as **`syNetSyncHashFighterAnimationStateForRollback`** and **`syNetSyncHashBattleFighters`**.

## 6. Snapshot capture alignment

After capturing fighters keyed by **`fp->player`**, the slot stores **`hash_animation = syNetSyncHashFighterAnimationStateForRollback()`** on live state (`port/net/sys/netrollbacksnapshot.c`). Load verify recomputes the same function and compares to **`syNetRbSnapshotGetSlotHashAnimation(tick)`** (`syNetRollbackVerifyLoadedSlot`).

## 7. Practical follow-ups

1. **`fighter_gobj->id`:** included in each per-fighter fold; if peers disagree on **`gobj->id`** for the same sim content, **`anim=`** splits. Item/weapon rollback hashes omit ids where noted; fighters still fold id here.

2. **Multiple fighters on one logical slot:** both **`figh`** light and **`anim`** rollup use the **same XOR-into-slot** rule; duplicated illegal players both poison slot `0` (see **`syNetSyncHashBattleFighters`** fallback).
