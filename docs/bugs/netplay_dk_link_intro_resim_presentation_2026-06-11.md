# Netplay DK/Link intro resim — Link model corruption + choppy Appear presentation

**Resolved 2026-06-11**

## Symptom

After a successful intro resim (soak3: DK P0 + Link P1, Yoshi's Story, `FORCE_MISMATCH` inject @230):

- Rollback/sim completed cleanly (`resim complete epoch=1`, authoritative GO @390).
- Link's model rendered corrupted during/after intro Appear (`AppearR`, status 224).
- Link stayed in Appear for ~280 ticks (should exit within tens of ticks).
- Intro felt choppy (resim hold, camera drift, long Appear phase).
- Logs spammed `gcParseDObjAnimJoint UNHANDLED opcode=64 ... — ending anim` during anchor probe +1 sim and forward resim.

Gameplay hashes matched; failure was presentation-only (`LOAD_HASH_DRIFT presentational-only`, `match_cam=0`).

## Root cause

Same family as [boss_event32_cache_invalidation](boss_event32_cache_invalidation_2026-05-01.md):

1. Fighter figatree heaps reuse addresses across modelpart swaps and `ftMainRefreshFigatreeVisual` reloads.
2. `port_aobj_event32_unhalfswap_stream` caches walker verdicts keyed on heap pointer (`sUnswappedHeads`, `sRejectedHeads`).
3. Snapshot restore / anchor-probe reconcile re-pinned joint `event32` pointers and ran figatree refresh **without** evicting stale cache entries or re-unhalfswapping streams.
4. `gcParseDObjAnimJoint` read halfswap-corrupted command words → opcode 64 → anim ended → Link stuck in Appear with broken mesh.

Prior anchor-probe fix (`syNetRbSnapshotReconcileAnchorProbeAppearSteadyFromProbeSlot` with `apply_modelparts=TRUE`) restored hash oracle parity but **poisoned live figatree** by pushing modelparts during probe reconcile, then figatree refresh left stale EVENT32 cache entries.

Intro cosmetic refresh (`syNetRbSnapRefreshIntroAppearCosmeticFromSlot`) re-pinned anim **before** `ftMainRefreshFigatreeVisual` and only restored anim scalars afterward — not full AObj re-pin + unhalfswap. Forward-resim presentation hook used live-only figatree refresh with no slot blob repair.

## Fix

### EVENT32 cache invalidation (`port/port_aobj_fixup.{h,cpp}`)

- `port_aobj_event32_unhalfswap_forget(void *head)` — drop cached verdict for one stream head.
- `port_aobj_event32_unhalfswap_evict_range(void *base, unsigned long size)` — evict all three pointer-keyed caches for a figatree heap region (same eviction as `port_aobj_register_halfswapped_range`, without registering).

### Snapshot apply hooks (`port/net/sys/netrollbacksnapshot.c`)

- `syNetRbSnapEvictFighterFigatreeEvent32Cache` / `syNetRbSnapUnhalfswapFighterJointEvent32Streams` — per-fighter helpers.
- After `syNetRbSnapApplyFighterJointPoseAndAnimFromBlob` and `syNetRbSnapApplyFighterModelPartsFromBlob`: evict + unhalfswap joint EVENT32 streams.
- `syNetRbSnapRefreshIntroAppearCosmeticFromSlot`: after `ftMainRefreshFigatreeVisual`, evict figatree cache + re-apply joint pose/anim from slot blob (includes unhalfswap).
- `syNetRbSnapshotRefreshIntroPresentationAfterForwardResimTick`: prefer slot-based cosmetic refresh when ring slot valid; fall back to live figatree refresh.
- `syNetRbSnapshotRefreshIntroPresentationAfterResimComplete`: terminal post-resim intro repair from completed target tick slot.
- Anchor probe Appear-steady reconcile: `apply_modelparts=FALSE` + explicit unhalfswap (hash still passes; avoids poisoning live DLs).

### Resim completion hook (`port/net/sys/netrollback.c`)

- `syNetRollbackOnResimCompleted` calls `syNetRbSnapshotRefreshIntroPresentationAfterResimComplete(target_tick)` before clearing load tick.

### Pre-sim EVENT32 fix (`decomp/src/sc/sccommon/scvsbattle.c`, `port/net/sys/netrollbacksnapshot.c`)

- `syNetRbSnapshotPreSimUnhalfswapIntroAppearAnim()` — before `ifCommonBattleUpdateInterfaceAll` / `gcRunAll`, evict figatree EVENT32 cache and re-unhalfswap all joint + figatree-walked DObj `anim_joint.event32` streams for fighters in intro Entry/Appear scope.
- Covers live intro ticks (pre-mismatch), resim forward sim, and anchor-probe +1 sim — closes the gap where `gcParseDObjAnimJoint` ran on stale cache before post-figatree cosmetic repair.

### Figatree heap range + force unhalfswap (2026-06-11 follow-up)

Pre-sim hook alone did not fix soak3: walker silently skipped streams outside per-file `sHalfswappedRanges` (dependency slices in the same heap, or range end shrunk on reload).

- `port_aobj_register_halfswapped_range` — same-base re-register now extends `end` via max (never shrinks).
- `lbRelocGetForceExternHeapFile` — after load, register the full utilized heap span `[heap, sLBRelocExternFileHeap)` so dependency anim files are in-range.
- `port_aobj_register_figatree_heap_span` + pre-sim call — register entire `gFTManagerFigatreeHeapSize` before intro unhalfswap.
- `port_aobj_event32_unhalfswap_stream_force` — walk even when head is outside registered ranges (walker validation still applies).
- Snapshot intro unhalfswap uses force; `gcParseDObjAnimJoint` force-retries when opcode ≥ 18 after the range-gated pass (`SSB64_NETMENU` only).
- `SSB64_AOBJ_UNHALFSWAP_DIAG=1` — log walker outcome per stream (`out_of_range`, `phase1_invalid`, `applied`, …).

### Intro presentation owned by resim (2026-06-11 follow-up)

Pre-resim blob cosmetic (`defer_wait`, replay-gate `post_cosmetic`, forward-resim slot re-pin) forced Appear figatree/modelparts onto screen while sim was frozen or fought forward hidden-part events.

- **Defer-wait:** camera integrate only — no per-frame slot re-pin.
- **Replay-gate / forward-resim / resim-complete:** hybrid one-shot slot repair (see below).

### Opcode 64 on hidden-part joints + hybrid presentation (2026-06-11)

Soak3 post-refactor still showed DK spin @112 (AppearL) and Link broken joints @134–170 (AppearR): **153×** `opcode=64` with `raw_u32=0x80001003` / `0x80000a03`, `phase1_invalid` even with `force=1`.

**Root cause (anim):** Appear motions set `fp->anim_desc.flags.is_anim_joint` (EVENT32) for the whole fighter, but hidden-part joints materialized during Appear carry **EVENT16** figatree streams in the same heap. `port_aobj_event32_unhalfswap_stream` correctly rejects them (`phase1_invalid`); `gcParseDObjAnimJoint` then reads opcode 64 and ends the anim every frame → DK spin, Link joint collapse.

**Root cause (presentation):** Phase-7 “live-only” refresh dropped entry-overlay / hidden-part / modelpart repair during countdown Wait (load @238) and at replay-gate open.

**Fix (anim)** — `decomp/src/ft/ftparam.c` `ftParamParseDObjJointAnim` (`#ifdef PORT`, **`SSB64_NETMENU` intro scope only** via `syNetRbSnapshotIntroAnimJointRoutingActive`): after unhalfswap (+ force-retry), route per joint to `gcParseDObjAnimJoint` when EVENT32 (opcode &lt; 18); Appear hidden-part roots → EVENT16 figatree when header valid; dual-invalid costume joints skipped; intro countdown Wait skips invalid EVENT16 headers. **Post-GO walk/turn/run use vanilla parse** (regression fix: global `#ifdef PORT` routing had suppressed parse on locomotion joints after resim → stuck facing + Kirby leg spin).

**Fix (anim, 2026-06-11 follow-up — reverted)** — First-u16 opcode validation alone was insufficient: EVENT32 bytes can false-positive as EVENT16 (e.g. `0x80000838` → opcode 1) and still watchdog deeper in the stream; Wait-path figatree had no guard at all (Link opcode-31 regression post-resim).

**Fix (presentation)** — `port/net/sys/netrollbacksnapshot.c`:

- `syNetRbSnapRefreshIntroCountdownPresentationFromSlot` — sanity repair + Appear slot cosmetic or Wait figatree re-pin.
- **Replay-gate open:** one-shot countdown presentation from load slot (defer-wait stays camera-only).
- **Forward resim:** fallback one-shot if gate missed; subsequent Appear ticks use live figatree refresh; camera always integrates during countdown.
- **Resim complete:** terminal countdown presentation from target tick.
- **`RefreshPresentationForLoadedTick` / `prepare_verify`:** run sanity repair whenever intro countdown is active (not only Entry/Appear scope).
- `syNetRbSnapshotResetIntroPresentationRepairState()` at replay-gate open (`netrollback.c`).
- **Wait transform tail (2026-06-11):** after slot-based countdown repair and during live Wait countdown (`RefreshLiveIntroPresentationAfterInterface`), invalidate FTParts transform caches + `ftParamInvalidateFighterTransformFromRoot` + `ftParamsUpdateFighterPartsTransformAll` via `syNetRbSnapRebuildIntroFighterPartTransforms`.

## Verification

Re-run soak3 (DK/Link, inject @230). Expect:

- `resim complete epoch=1` (unchanged).
- No `UNHANDLED opcode=64` spam during intro Appear ticks (~112 DK, ~134–170 Link).
- Link transitions Appear → Wait within normal tick range.
- Link model intact through GO @390; DK no spin during AppearL.
- No `ftAnimParseDObjFigatree` watchdog during DK AppearL tick ~150.
- DK idle pose correct through countdown Wait (ticks ~151–390) without waiting for first locomotion.
- Smoother intro presentation after rollback load @238.
