# Netplay Ness PK Fire item respawn (LOAD_HASH_DRIFT cascade)

**Date:** 2026-07-09  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptoms

soak2 cross-ISA pair session `1394254221` / seed `256445873` (Captain Falcon vs Ness, Dream Land):

- `LOAD_HASH_DRIFT` ticks **474–480** (host 475–480): **`item` only** → live had PK Fire pillar, post-load empty (`item=0x811C9DC5`)
- `item respawn unsupported kind=20 (0x14)` ×6–7 — `nITKindNessPKFire = nITKindFighterStart`
- `FRAME_COMMIT_INPUT_AGREE_REANCHOR` `last_agreed=480 mismatch=481 resolved_load=480`
- `FRAME_COMMIT_STATE_DIVERGE @600` **`figh,rng`** inputs MATCH (likely downstream of missing item during 481→600 resim)

Witness: falling PK Fire item (`gobj_id=1013`, `ga=1`, `hold=0`) during Ness PK Thunder window.

## Root cause

Rollback item respawn routes fighter kinds through `syNetRbSnapRespawnItemFromBlob()`. Link bomb was special-cased (2026-05-22); **Ness PK Fire** still hit the fighter-range reject and returned NULL.

Vanilla never registers PK Fire in `dITManagerProcMakeList[]` — spawn is `itNessPKFireMakeItem()` via `itManagerMakeItem(NULL, &dITNessPKFireItemDesc, …)`.

Additional hazards on respawn/apply:

- `item_vars.pkfire.xf` (`LBTransform*`) is memcpy'd into blobs but invalidated by `syNetRbSnapResetParticlesForRollback()`
- `itNessPKFire*SetStatus` / `itMainSetStatus` reset hashed physics and `attack_coll.stat_*`

## Fix

| Change | Location |
|--------|----------|
| `syNetRbSnapRespawnNessPKFireFromBlob()` — `itManagerMakeItem` + `dITNessPKFireItemDesc` | [`port/net/sys/netrollbacksnapshot.c`](port/net/sys/netrollbacksnapshot.c) |
| Route `nITKindNessPKFire` before fighter-kind reject in `syNetRbSnapRespawnItemFromBlob()` | same |
| `syNetRbSnapReapplyNessPKFireStatusAfterBlob()` — proc-only rebind from `blob->ga`, rebuild particle, `itProcessUpdateAttackPositions` | same |
| Wire PK Fire branch in `syNetRbSnapApplyItemBlobToGObjPort()` | same |
| **Owner rebind** — non-held fighter items with `owner_gobj_id==1000` resolve via `blob->player` (not `gcFindGObjByID`); PK Fire reconcile + reapply | same |

## Follow-up: Ness self-damage on pillar touch (2026-07-10)

Vanilla skips item-vs-owner hits when `fighter_gobj == ip->owner_gobj` (`ftmain.c`). Blob capture stored `owner_gobj_id=1000` for all fighters; apply used `syNetRbSnapResolveLiveGobj(1000)` → player 0, so Ness (P1) could hurt himself after rollback load/emergency restore while forward-only sim was fine.

Fix: resolve airborne PK Fire owner by `blob->player`; extend `syNetRbSnapReconcileFighterOwnedItemOwners` for non-held PK Fire.

## Re-soak pass criteria

- No `item respawn unsupported kind=20`
- No item-only `LOAD_HASH_DRIFT` in PK Fire / rollback-load window
- `FRAME_COMMIT_STATE_DIVERGE @600` should clear if it was purely missing-item resim drift (re-soak to confirm)

## Follow-up: NULL parent SIGSEGV (2026-07-19)

The respawn maker copied vanilla `(COLLPROJECT \| PARENT_WEAPON)` but passed `parent_gobj=NULL`, crashing both peers at `fault_addr=0xe0` during synctest restore (soak `383687403`). Fix: [`netplay_ness_pkfire_respawn_null_parent_segv_2026-07-19.md`](netplay_ness_pkfire_respawn_null_parent_segv_2026-07-19.md).

## Related

- [`netplay_linkbomb_synctest_segv_2026-05-22.md`](netplay_linkbomb_synctest_segv_2026-05-22.md) — Link bomb fighter-item respawn pattern; doc noted PK Fire still unsupported
- [`netplay_ness_pkfire_respawn_null_parent_segv_2026-07-19.md`](netplay_ness_pkfire_respawn_null_parent_segv_2026-07-19.md)
