# NETMENU intro forward-load corruption (offline + online)

**Open 2026-06-11**

## Symptom

In `SSB64_NETMENU` builds (offline local VS and netplay, with or without
`FORCE_MISMATCH`):

- Entry intro animations do not play visibly; fighters snap to Wait idle pose.
- Appear lasts one sim tick in status trail (e.g. Kirby `status=251` → `status=10`).
- Costume / leg joints render corrupted (spinning limbs, broken mesh).
- Figatree watchdogs at GO (`opcode=31`, `raw32=0x280effe9` pattern).

Reproduces with rollback disabled — not a resim/snapshot bug.

## Root cause

Two NETMENU-only forward-sim paths interfered with vanilla intro load:

1. **`scVSBattleFuncUpdate`** called `syNetRbSnapshotPreSimUnhalfswapIntroAppearAnim`
   and `syNetRbSnapshotRefreshLiveIntroPresentationAfterInterface` every countdown
   frame on **forward sim**, including offline VS. Pre-sim force-unhalfswap on Entry
   fighters hit EVENT16 figatree bytes in the shared heap (`phase1_invalid` from
   tick 0). Per-frame `ftMainRefreshFigatreeVisual` during Appear collapsed motion.

2. **`syNetRbSnapshotIntroAnimJointRoutingActive`** required
   `syNetplayRollbackSemanticsActive()`, so offline VS used vanilla
   `gcParseDObjAnimJoint` on Appear pin — EVENT32 parser on EVENT16 hidden-part
   streams → immediate `anim_wait = AOBJ_ANIM_END` (one-tick Appear).

Same underlying EVENT32/EVENT16 heap family as
`docs/bugs/boss_event32_cache_invalidation_2026-05-01.md`.

## Fix

- Gate intro presentation repair in `scvsbattle.c` to **`syNetRollbackIsResimulating()`
  only**; forward sim runs vanilla `ifCommonBattleUpdateInterfaceAll()`.
- Enable Appear-scoped `ftParamParseDObjJointAnimIntro` for **all NETMENU VS**
  (remove rollback-semantics gate for Appear scope); **pre-GO countdown routes all
  fighter statuses** (soak attack inputs during Wait no longer drop routing).
- Register fighter figatree heap span once in `ftManagerMakeFighter`.
- **2026-06-11 follow-up:** intro parser forget+force unhalfswap; hidden-part EVENT16
  routing; EVENT32 false-positive guard on EVENT16 header; gcParse force-unhalfswap
  during countdown; evict aobj walker cache on lbReloc heap overwrite.
- **2026-06-11 follow-up (GO edge):** skip EVENT32 force-unhalfswap when EVENT16 header
  is valid (event32/event16 alias same bytes); walker rejects figatree-heap
  `0x200effxx` false-positive applies; post-GO grace ticks keep intro joint routing
  + gcParse force path through GO+2; figatree parser skips invalid EVENT16 headers;
  `syNetRbSnapshotNotifyBattleGo` evicts walker cache and rebuilds part transforms.

**Status:** Core joint/parse routing **reverted 2026-06-11** (Option B). Forward sim uses vanilla
`gcParse` / `ftAnim` again. Resim pipeline hooks (`PreSimUnhalfswap`, cache evict, slot cosmetic
repair, resim-only `scVSBattleFuncUpdate` presentation) retained. Do not re-land
`ftParamParseDObjJointAnimIntro` or global walker heuristics without intro/resim scope gates.

## Test plan

- Offline NETMENU VS: Kirby + Yoshi, Yoshi's Story — full Appear anims, clean joints.
- Netplay soak3 without `FORCE_MISMATCH` — same visual check.
- Netplay resim @ intro / @480 — oracle still passes; presentation repair still runs
  during resim via `scVSBattleFuncUpdateBattleSimOnly`.
