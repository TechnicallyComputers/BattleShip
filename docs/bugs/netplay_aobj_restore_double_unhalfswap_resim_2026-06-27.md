# Rollback restore double-un-halfswaps native figatree EVENT32 streams → all-fighter joint spin after resim

**Date:** 2026-06-27
**Scope:** `PORT && SSB64_NETMENU` (netplay rollback restore only; offline unaffected)
**Status:** ✅ **RESOLVED** (soak-confirmed 2026-06-27, bit-identical on Linux + Android peers). Fixed in four parts (apply-step gate + native-memo re-pin after presentation reload + model-parts-apply gate + per-resim-tick/finalize repair gate). See "Follow-up" through "Follow-up 3" below; the per-resim-tick repair pass (Follow-up 3) was the corruptor still firing after the first three parts and was the change that definitively stopped the spin. All four gates default to the fixed behavior under `PORT && SSB64_NETMENU`; `SSB64_NETPLAY_RESIM_RESTORE_UNHALFSWAP=1` restores the old (corrupting) force passes for A/B.

## Symptom

After a rollback resim, fighter joints rotate continuously / lock into a spin instead of
resuming the pre-resim animation. No crash; rollback reports success; the per-tick `anim`
hash never triggers a mismatch (it is not gated on by mismatch detection). Originally read
as "DK's leg," later confirmed to affect **all characters** — a deterministic subset of
joints on every fighter.

This is the same visible family as
[netplay_aobj_interp_quantize_resim](netplay_aobj_interp_quantize_resim_2026-06-26.md), but a
**distinct, independent root cause**. The interp-quantize fix (live-sim quantization of the
AObj interpolation chain) was necessary but not sufficient; the spin persisted with
`SSB64_NETPLAY_SIM_F32_QUANTIZE=0` and was **bit-identical on Linux and Android**, ruling out
float nondeterminism.

## Diagnosis (ground truth)

`SSB64_NETPLAY_AOBJ_LEG_TRACE` (forward-vs-resim) then
`SSB64_NETPLAY_RESIM_STREAM_DIGEST_DIAG` (per restore-step digest) pinned the writer.

For each `(player, joint)` the digest of the first 16 u32 at the joint's EVENT32 cursor was
logged before/after each restore step. The corruption is **in-place across the
`apply_pre_unhalfswap → apply_post_unhalfswap` step** (the forced un-halfswap inside
`syNetRbSnapApplyFighterJointPoseAndAnimFromBlob`), at the **same cursor**:

```
apply_pre_unhalfswap   player=0 fkind=2 joint=8 ev=…b2e stream=0xb802419c af=204 aw=2
apply_post_unhalfswap  player=0 fkind=2 joint=8 ev=…b2e stream=0xde6665ea af=204 aw=2   ← WRITER
```

- `0xb802419c` is the **native** digest (matches the forward-sim `NetAObjTrace` value).
- The un-halfswap converts it to `0xde6665ea` (halfswapped), then re-running it is idempotent
  (`0xde6665ea → 0xde6665ea`) — the textbook native→halfswap **double-swap** signature.
- soak1 (2026-06-27): **10 of 38 joints** corrupted, on **both** fighters —
  P0 (DK, fkind 2): joints 8, 14, 21, 25, 26; P1 (fkind 5): joints 22, 23, 24, 27, 32.
  Identical joints and digests on the Android peer (`ev=0xb400…3b1e`, same `0xb802419c →
  0xde6665ea`). A couple more (DK 16, 17) are perturbed by the `ftMainRefreshFigatreeVisual`
  re-attach relocating the cursor; the dominant writer is the un-halfswap.

The `AOBJ_DOUBLE_SWAP` detector returned 0 hits because its `was_native` check reads only the
walker's `sUnswappedHeads` memo, and the **restored cursor is a mid-stream (post-advance)
position the load-time walker never memoized as a head** — so the memo can't vouch for it and
the detector stayed silent even though the bytes were demonstrably native.

## Root cause

The fighter figatree heap is **shared and is not part of the snapshot blob**. The blob stores
only the per-joint EVENT32 **cursor pointer** (`joint_anim_joint_event32[]`) plus pose/anim
scalars. At restore time the heap bytes are therefore the **live, already-un-halfswapped
forward-sim bytes** (native).

`syNetRbSnapApplyFighterJointPoseAndAnimFromBlob` nonetheless evicted the walker memo and ran
a **forced** un-halfswap (`port_aobj_event32_unhalfswap_stream_force_applied`) from each
restored cursor. Because the restored cursor is mid-stream (often near a terminator), the
walker's byte-order heuristic — which assumes a genuine native stream validates ≥2 commands
from any position (see `port_aobj_fixup.cpp` walk() Phase 2) — fails for these positions and
**mis-applies a swap to already-native bytes**, corrupting the figatree the parser reads on
resim → `AObj` value/rate garbage → joint spin.

Forward sim never hits this: it un-halfswaps lazily and idempotently through the **memoized**
non-force path (`gcParseDObjAnimJoint` → `port_aobj_event32_unhalfswap_stream`), so a stream is
normalized once from its real head and skipped thereafter.

Genuine halfswapped bytes only ever (re)appear after a figatree **(re)load**, which always
routes through `port_aobj_register_halfswapped_range` (lbreloc bridge load +
`ftMainRefreshFigatreeVisual` reattach) and **evicts the walker memo there**; the parser then
re-normalizes lazily from real heads. The stable restore path has no business re-deriving byte
order at all.

## Fix

Gate the forced un-halfswap inside `syNetRbSnapApplyFighterJointPoseAndAnimFromBlob` behind a
new env, **default OFF** (= corruption fixed):

```c
syNetRbSnapDiagFighterStreamDigests(fp, "apply_pre_unhalfswap");
if (syNetRbSnapRestoreForceUnhalfswapEnabled() != FALSE) {   /* SSB64_NETPLAY_RESIM_RESTORE_UNHALFSWAP=1 */
    syNetRbSnapEvictFighterFigatreeEvent32Cache(fp);
    syNetRbSnapUnhalfswapFighterJointEvent32Streams(fp);
    syNetRbSnapUnhalfswapFighterFigatreeDObjAnimStreams(fp);
}
syNetRbSnapDiagFighterStreamDigests(fp, "apply_post_unhalfswap");
```

This covers every caller of `syNetRbSnapApplyFighterJointPoseAndAnimFromBlob` (main apply +
resim/post-verify sites). The intro-Appear fresh-load pass
(`syNetRbSnapPreSimUnhalfswapIntroAppearFighter`), the modelpart-swap pass, and the Appear
presentation pass still normalize — those follow a genuine (re)load where the heads are not
memoized-native and the swap is legitimate.

`SSB64_NETPLAY_RESIM_RESTORE_UNHALFSWAP=1` restores the old (corrupting) behavior for A/B.

### Verification

- `build-netmenu` `ssb64` target builds clean.
- Expected with `SSB64_NETPLAY_RESIM_STREAM_DIGEST_DIAG=1` and the fix on:
  `apply_post_unhalfswap stream=` == `apply_pre_unhalfswap stream=` for all joints (no in-place
  byte mutation), and the post-resim spin gone on both peers.

## Audit hooks

- A restore step whose `NetRbStreamDiag` digest changes at the **same `ev=` cursor** is a
  byte-order writer; the figatree heap is shared/live, so the stable restore path must not
  re-derive byte order — trust load/reload-time normalization + memo eviction.
- `AOBJ_DOUBLE_SWAP` (memo-based) is structurally blind to mid-stream restored cursors; use the
  digest diff (`STREAM_DIGEST_DIAG` / `AOBJ_LEG_TRACE`) as ground truth.
- Anything that force-un-halfswaps from a snapshot-restored cursor (not a real stream head) is
  suspect — the walker heuristic is only reliable from heads.

## Follow-up: same double-swap re-emerges via the lazy parser after presentation reload

Gating the forced un-halfswap (above) eliminated the `apply_pre → apply_post` mutation
(`STREAM_DIGEST_DIAG` confirmed 0/38 joints mutating, `dobj->rotate` clean and bit-identical
across peers / fwd vs resim). But the spin **persisted with changed behavior**. Extending
`AOBJ_LEG_TRACE` to `phase=resim` + the visual figatree tree showed the spin was not in
`rotate`/`translate`/`scale` but in the **animation cursor collapsing** to `af=0,
aw=AOBJ_ANIM_NULL` on the first resim tick for a deterministic subset of joints.

`SSB64_NETPLAY_RESIM_CAPTURE_ANIM_DIAG` (capture clean) +
`SSB64_NETPLAY_RESIM_STREAM_DIGEST_DIAG` traced the collapse to two coupled steps:

1. **Presentation clobber.** `syNetRbSnapshotSyncFighterPresentation` →
   `ftMainRefreshFigatreeVisual` rewinds every joint's EVENT32 cursor and resets `anim_wait`
   to a figatree re-init sentinel, discarding the restored mid-animation cursor
   (`presync_post_reattach` digest diverges). A **re-pin after presync**
   (`syNetRbSnapReapplyFighterJointAnimFromSlot` after the visual refresh in
   `syNetRbSnapSyncFighterPresentationAfterJointPrep`) restores `af`/`aw`/`event32`
   bit-identically (`presync_post_repin` clean) — necessary, but **the joint still collapsed
   on the next resim tick**.

2. **Memo eviction re-exposes the double-swap (true root cause).**
   `ftMainRefreshFigatreeVisual` calls `port_aobj_register_halfswapped_range`, whose
   `evict_caches_in_range` wipes `sUnswappedHeads`/`sRejectedHeads` for the whole figatree
   heap. The rollback re-bind does **not** re-copy halfswapped bytes — the heap stays native —
   but the memo no longer knows that. On the next resim tick `gcParseDObjAnimJoint` runs the
   lazy `port_aobj_event32_unhalfswap_stream` on the **restored mid-stream cursor** with an
   empty memo, the Phase-1/2/3 heuristic mis-reads the mid-stream native event as halfswapped,
   double-swaps it, and the joint collapses to `af=0/aw=AOBJ_ANIM_NULL`. This is the **same
   double-swap class** as the apply-step bug, re-entered through the parser's lazy path after
   the eviction rather than the (now-gated) forced restore path.

### Fix (this change)

Re-pin the restored cursor as **native** in the walker memo right where
`syNetRbSnapApplyFighterJointPoseAndAnimFromBlob` restores it, so the lazy parser
short-circuits (`AlreadyUnswapped`) instead of re-running the ambiguous heuristic:

```c
fp->joints[ji]->anim_joint.event32 = (AObjEvent32 *)blob->joint_anim_joint_event32[ji];
#if defined(SSB64_NETMENU)
port_aobj_event32_head_mark_unswapped(fp->joints[ji]->anim_joint.event32);
#endif
```

`port_aobj_event32_head_mark_unswapped` (new, in `port_aobj_fixup.cpp`) inserts the head into
`sUnswappedHeads` and drops any stale `sRejectedHeads` verdict. Because this apply runs again
in the post-presentation re-pin (after `ftMainRefreshFigatreeVisual`'s eviction), the native
mark survives into the resim parse. Marking is safe: the cursor was captured from native live
state and the rollback re-bind never re-copies halfswapped bytes (`presync_post_repin` digest
== forward digest confirms native bytes post-reload). Genuine (re)loads of fresh halfswapped
bytes go through `port_aobj_register_halfswapped_range` (memo eviction) and are normalized
lazily from real heads as before — this only re-pins cursors restored from a snapshot blob.

### Verification (follow-up)

- `build-netmenu` `ssb64` target builds clean.
- Expected: the re-pinned joints keep `af`/`aw` and a stable `stream=` digest through the
  first resim tick (no collapse to `aw=AOBJ_ANIM_NULL`), and the post-resim spin is gone on
  both peers. Soak pending.

## Follow-up 2: a SECOND ungated force-un-halfswap site re-corrupts after the re-pin

The native-memo re-pin held at `presync_post_repin` but the spin **persisted with changed
behavior**. The 2026-06-27 soak (`AOBJ_UNHALFSWAP_DIAG` + `STREAM_DIGEST_DIAG` +
`AOBJ_DOUBLE_SWAP` detector) showed 49 detector hits — the forced un-halfswap was still firing
during restore, from a **second site I had not gated**.

`syNetRbSnapApplyFighterModelPartsFromBlob` carried its own copy of the evict + forced
un-halfswap, unconditional under `SSB64_NETMENU`:

```c
memcpy(fp->modelpart_status, ...);
memcpy(fp->texturepart_status, ...);
#if defined(SSB64_NETMENU)
syNetRbSnapEvictFighterFigatreeEvent32Cache(fp);   /* evicts memo (wipes re-pin native marks) */
syNetRbSnapUnhalfswapFighterJointEvent32Streams(fp); /* force pass → double-swaps native cursors */
#endif
```

It runs **after** the presentation re-pin in the terminal verify-prep sequence
(`syNetRbSnapRefreshFigatreePresentationFromSlot`):

```c
syNetRbSnapReapplyFighterJointAnimFromSlot(slot);
syNetRbSnapSyncFighterPresentationAfterJointPrep(slot);  /* presync_post_repin: native ✓ */
syNetRbSnapApplyFighterModelPartsFromSlot(slot);         /* evict + force pass → DOUBLE SWAP ✗ */
syNetRbSnapReapplyFighterJointAnimFromSlot(slot);        /* re-pin marks the now-corrupted bytes native ✗✗ */
```

The model-parts evict wipes the native marks the re-pin just set, the force pass double-swaps
the native mid-stream cursors, and the *subsequent* re-pin then marks the **corrupted** bytes
native via `port_aobj_event32_head_mark_unswapped` — locking the corruption in so the lazy
parser can't recover it. Ground truth for P1 joint 27 (`ev=…3ee4`):

```
apply/presync (×N)   stream=0x7a549f8d   ← native, held by re-pin
aobj unhalfswap      head=…3ee4 force=1 outcome=applied   → AOBJ_DOUBLE_SWAP joint=27
apply_pre/post (×N)  stream=0x455c0a25   ← corrupted, locked in
... fresh reload     stream=0x7a549f8d   ← recovers
aobj unhalfswap      head=…3ee4 force=1 outcome=applied   → AOBJ_DOUBLE_SWAP (again)
apply_pre/post       stream=0x455c0a25 → later 0xbbbc0a7b ← compounds across cycles
```

Deterministic and bit-identical across peers: `status=10 motion=4`, joints 21/25/26/28 (P0,
DK fkind 2) and 24/27/32 (P1 fkind 5).

### Fix (this change)

Gate the model-parts evict + force pass behind `syNetRbSnapRestoreForceUnhalfswapEnabled()`
(default OFF), identical to the main apply path:

```c
#if defined(SSB64_NETMENU)
if (syNetRbSnapRestoreForceUnhalfswapEnabled() != FALSE) {
    syNetRbSnapEvictFighterFigatreeEvent32Cache(fp);
    syNetRbSnapUnhalfswapFighterJointEvent32Streams(fp);
}
#endif
```

A genuine modelpart swap above (`ftParamSetModelPartID`) reloads its figatree DL through
`port_aobj_register_halfswapped_range` (memo eviction), so `gcParseDObjAnimJoint` re-normalizes
the freshly loaded stream lazily from its real head — the explicit force pass is unnecessary
and only corrupts native restored cursors.

**Appear-scope sites intentionally left alone.** `syNetRbSnapEvictFighterFigatreeEvent32Cache`
at the Appear post-verify presentation site is evict-only (no force pass) and is immediately
followed by the gated apply + re-pin, so it is not a corruptor. The force pass in
`syNetRbSnapshotReconcileAnchorProbeAppearSteadyFromProbeSlot` is load-bearing for a separate
Appear hidden-part fix (`gcParseDObjAnimJoint` opcode 64 prematurely ending Link's hidden-part
anims during the Appear probe — a genuine stale-memo case) and is `AppearPresentationScope`-gated;
it is not implicated in the `status=10` spin.

### Verification (follow-up 2)

- `build-netmenu` `ssb64` target builds clean.
- Expected: 0 `AOBJ_DOUBLE_SWAP` hits during gameplay-scope (`status != Appear`) restore, the
  `status=10 motion=4` joints hold a stable native `stream=` digest across restore cycles, and
  the post-resim spin is gone on both peers. Soak pending.

## Follow-up 3: the per-resim-tick "repair" pass re-corrupts every tick (final corruptor)

Gating both forced sites above drove the `AOBJ_DOUBLE_SWAP` detector to **0 hits**, yet the
spin **persisted** with the reported "one leg spins fast immediately, the other looks normal
then slowly starts to spin too." The 2026-06-27 soak still showed `force=1 outcome=applied`
(133×) from an *ungated* path, and a forward-vs-resim trace of P1 joint 27 at `tick=520`
caught the collapse directly:

```
forward   player=1 joint=27   af=30  aw=1
resim     player=1 joint=27   af=0   aw=-3.40282347e+38   (AOBJ_ANIM_NULL)
presync_post_repin            stream=0x7a549f8d af=29 aw=1   ← restore is clean
... during resim replay ...   head=…eee4 force=1 outcome=applied   ← re-corrupted here
```

The remaining writer is the **per-resim-tick repair pass**, called from
`decomp/src/sc/sccommon/scvsbattle.c` every replay tick:
`syNetRbSnapshotPreSimUnhalfswapGameplayResimAnim()` →
`syNetRbSnapPreSimUnhalfswapIntroAppearFighter(fp)`. That helper does
`RegisterFighterFigatreeHeapSpan` (→ `register_halfswapped_range`, **evicts the whole heap
memo**) + `EvictFighterFigatreeEvent32Cache` (**evicts again**) and then force-walks every
joint/DObj cursor. Because the cursors are restored **mid-stream** and the memo was just wiped
(so the re-pin native marks AND the pre-evict-native snapshot are both empty — which is exactly
why `AOBJ_DOUBLE_SWAP`'s `was_native` probe stayed silent), the heuristic mis-reads a native
mid-stream event (`raw_u32=0x20090001`, opcode 16) as halfswapped and double-swaps it to
`0x00012009` (opcode 0 / End) → `aw=AOBJ_ANIM_NULL` → continuous extrapolation → spin. The
`forget()` in `syNetRbSnapUnhalfswapEvent32Head` is moot here: the upstream register/evict
already cleared the memo before the per-head walk, so "drop the `forget()` and trust the memo"
cannot work as a standalone change — there is nothing left in the memo to short-circuit on.

The same applies to the four restore-finalize presentation paths (egg-lay attack, residual
shield, gameplay-anim-fragile, anchor-probe gameplay): each does `ftMainRefreshFigatreeVisual`
(evicts the memo via the reattach) immediately followed by the repair force-walk, which then
double-swaps the native mid-stream cursors before the trailing re-pin marks the corrupted
bytes native.

### Fix (this change)

The repair is **redundant in the resim/restore context**: the restore keeps the heap native,
the cursor is re-pinned native, and the per-tick resim performs no `ftMainRefreshFigatreeVisual`
reattach, so the native marks persist and `gcParseDObjAnimJoint`'s lazy path is a no-op on the
already-native stream. Route all five resim/finalize callers through a gated wrapper that no-ops
unless the A/B env is set, leaving the genuine intro-Appear fresh-load pass
(`syNetRbSnapshotPreSimUnhalfswapIntroAppearAnim`) calling the original helper:

```c
static void syNetRbSnapResimRenormalizeFighterFigatreeStreams(FTStruct *fp)
{
    if (syNetRbSnapRestoreForceUnhalfswapEnabled() == FALSE) {  /* default: skip the force pass */
        return;
    }
    syNetRbSnapPreSimUnhalfswapIntroAppearFighter(fp);
}
```

Re-pointed callers (all `PORT && SSB64_NETMENU`): `syNetRbSnapshotPreSimUnhalfswapGameplayResimAnim`
(per-tick), `syNetRbSnapRefresh{ResidualShield,GameplayAnimFragile}PresentationFromSlot`,
`syNetRbSnapReapply…EggLayAttack…`, and `syNetRbSnapshotReconcileAnchorProbeGameplayFromProbeSlot`.
Intro-Appear is unchanged (a genuine fresh load with halfswapped bytes and head-aligned cursors,
where the force walk is correct). `SSB64_NETPLAY_RESIM_RESTORE_UNHALFSWAP=1` re-enables the old
(corrupting) force pass on all five for A/B and to re-test the 2026-06-11 reload family.

### Verification (follow-up 3) — CONFIRMED

- `build-netmenu` `ssb64` target builds clean.
- Soak (2026-06-27): `force=1 outcome=applied` dropped to ~0 during gameplay-scope resim, P1
  joint 27 held `af`/`aw` across resim ticks (no `aw=AOBJ_ANIM_NULL` collapse), and **both legs
  stopped spinning on both peers** — the spin is definitively gone. Resolution confirmed
  bit-identical Linux ↔ Android, completing the four-part fix.

## Related

- [aobjevent32_halfswap](aobjevent32_halfswap_2026-04-18.md) — the original halfswap walker.
- [boss_event32_cache_invalidation](boss_event32_cache_invalidation_2026-05-01.md),
  [netplay_dk_link_intro_resim_presentation](netplay_dk_link_intro_resim_presentation_2026-06-11.md)
  — figatree heap reuse / cache eviction family.
- [netplay_aobj_interp_quantize_resim](netplay_aobj_interp_quantize_resim_2026-06-26.md) — the
  sibling resim joint-spin fix (interp quantization).
