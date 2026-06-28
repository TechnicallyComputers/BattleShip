#pragma once

/**
 * port_aobj_fixup.h — per-stream un-halfswap for AObjEvent32 animation data.
 *
 * Fighter reloc files (`reloc_animations/FT*`, `reloc_submotions/FT*`) go
 * through portRelocFixupFighterFigatree at load time, which u16-halfswaps
 * every non-reloc u32 slot.  That is correct for AObjEvent16 figatree
 * data (u16 pairs packed into u32 slots) but corrupts AObjEvent32 u32
 * bitfield commands — opcode lands in bits 9-15 instead of 25-31 and
 * flags splits across the halfswap boundary in a way no bitfield can
 * express.
 *
 * The file contains both stream types, and at load time we can't tell
 * which token targets which kind of stream (the choice is runtime, via
 * fp->anim_desc.flags.is_anim_joint per motion).  So the fix is lazy:
 * the first time an EVENT32 reader touches a stream, we walk it from the
 * head, un-halfswap each data u32 in place, skip token slots, and record
 * the head in a visited set so subsequent passes are no-ops.
 *
 * Callers: gcParseDObjAnimJoint and gcParseMObjMatAnimJoint in
 * src/sys/objanim.c (at function entry, wrapped in #ifdef PORT).
 *
 * The head is passed as void* so the header has no game-type dependency.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Walk an AObjEvent32 stream starting at @p head, un-halfswapping every
 * data u32 encountered (command words, flag words, f32 payloads) until
 * the End opcode (0) or a stream-terminating Jump/SetAnim.  Token slots
 * inside Jump/SetAnim/SetInterp events are left alone (the reloc chain
 * already wrote native u32 token indices into those).  Jump and SetAnim
 * targets are walked recursively.  Idempotent via an internal visited
 * set — calling on an already-walked head is a no-op.
 *
 * Safe on NULL.  Aborts with a warning log on unrecognised opcodes
 * (≥24) or if a per-stream step limit is exceeded.
 */
void port_aobj_event32_unhalfswap_stream(void *head);

/**
 * Same as port_aobj_event32_unhalfswap_stream but walks even when @p head is
 * outside registered halfswapped ranges.  Walker validation still applies.
 * Used for intro Appear hidden-part EVENT32 streams that live in figatree heap
 * dependency slices not covered by per-file registration.
 */
void port_aobj_event32_unhalfswap_stream_force(void *head);

/**
 * Same as port_aobj_event32_unhalfswap_stream_force but reports whether the
 * walker actually re-applied an in-place un-halfswap to @p head: returns 1 when
 * the outcome was "applied" (bytes mutated), 0 for every other outcome
 * (out_of_range / already_unswapped / phase2_native / rejected / invalid).
 *
 * Detector hook only: pairing this with port_aobj_event32_head_is_unswapped()
 * before forgetting @p head identifies a double-swap — a head believed native
 * (no intervening heap reload/eviction) that the heuristic nonetheless re-swaps,
 * corrupting the stream. No behavior change vs the void variant.
 */
int port_aobj_event32_unhalfswap_stream_force_applied(void *head);

/**
 * Returns 1 if @p head is currently recorded as un-halfswapped (native) in the
 * walker's memo, 0 otherwise. A head still in the memo means no heap reload
 * evicted it, so its bytes are native; a force-swap that then applies is a
 * double-swap. Detector use only.
 */
int port_aobj_event32_head_is_unswapped(const void *head);

/**
 * Snapshot, into a side set, every head currently recorded as un-halfswapped
 * (native) whose address lies in [@p base, @p base + @p size).  Call this
 * immediately before port_aobj_event32_unhalfswap_evict_range wipes the live
 * memo so the double-swap detector can still tell a head was native after the
 * eviction.  Each call replaces the previous snapshot.
 */
void port_aobj_event32_capture_native_in_range(void *base, unsigned long size);

/**
 * Returns 1 if @p head was captured as native by the most recent
 * port_aobj_event32_capture_native_in_range call, 0 otherwise.  Detector use
 * only — survives a subsequent evict_range/forget that clears the live memo.
 */
int port_aobj_event32_head_was_native_preevict(const void *head);

/**
 * Record @p head in the walker memo as already-un-halfswapped (native), so the
 * next port_aobj_event32_unhalfswap_stream call on it short-circuits without
 * re-running the ambiguous halfswap heuristic.  Drops any stale "rejected"
 * verdict for the same address.
 *
 * Rollback-restore use: the figatree reload in syNetRbSnapshotSyncFighterPresentation
 * calls port_aobj_register_halfswapped_range, which evicts the whole heap's memo —
 * but a rollback re-BIND does not re-copy halfswapped bytes, so a mid-stream EVENT32
 * cursor restored from a snapshot blob still points at native data.  Without re-marking,
 * gcParseDObjAnimJoint's lazy walk re-classifies that mid-stream cursor, misclassifies a
 * native mid-stream event as halfswapped, double-swaps it, and the joint's anim collapses
 * to AOBJ_ANIM_NULL on the first resim tick (frozen/spinning leg).  Only call with a head
 * known to point at native bytes (a cursor restored from a snapshot blob).
 */
void port_aobj_event32_head_mark_unswapped(void *head);

/**
 * Drop cached walker verdicts for @p head so the next
 * port_aobj_event32_unhalfswap_stream call re-walks the stream.  Used when
 * figatree heap bytes at a stable pointer were overwritten without going
 * through port_aobj_register_halfswapped_range (modelpart swap, snapshot
 * blob re-pin, etc.).
 */
void port_aobj_event32_unhalfswap_forget(void *head);

/**
 * Evict every walker / one-shot-fixup cache entry keyed on an address inside
 * [@p base, @p base + @p size).  Same eviction as
 * port_aobj_register_halfswapped_range without registering a new range.
 */
void port_aobj_event32_unhalfswap_evict_range(void *base, unsigned long size);

/**
 * Register a memory region (from a fighter-figatree reloc file) that was
 * u16-halfswapped at load time.  The walker refuses to touch pointers
 * outside any registered region — protects against accidentally
 * un-halfswapping data that was never halfswapped in the first place
 * (e.g. EVENT32 streams in non-fighter files, or EVENT16 streams whose
 * bytes happen to be walked by mistake).  Called from lbreloc_bridge.cpp
 * after portRelocFixupFighterFigatree.
 */
void port_aobj_register_halfswapped_range(void *base, unsigned long size);

/**
 * Register an entire per-fighter figatree heap as halfswapped-eligible.
 * Extends any existing registration for the same base via max(end).
 */
void port_aobj_register_figatree_heap_span(void *heap, unsigned long heap_size);

/**
 * Clear the visited set and registered ranges.  Called on scene reset
 * so streams that get unloaded and reloaded are re-walked on next
 * access.  Currently not wired in (tokens get invalidated on reset
 * anyway) — exposed for future integration with portRelocResetPointerTable.
 */
void port_aobj_event32_unhalfswap_reset(void);

/**
 * Returns 1 if @p lies in any registered halfswapped range, 0 otherwise.
 * Used by ftkey.c to decide whether a FTKeyEvent stream needs runtime
 * Vec2b byte-swap compensation (stick operand halfwords).
 */
int port_aobj_is_in_halfswapped_range(const void *p);

/**
 * Idempotency tracking for one-shot in-place fixups on figatree-loaded
 * memory (e.g. the spline-data un-halfswap in src/sys/interp.c).  The
 * fixup callee passes the data pointer it is about to mutate; this
 * returns 1 if the pointer was already visited (= skip the fixup) or 0
 * if newly recorded (= proceed with the fixup).
 *
 * The visited set is automatically scrubbed inside
 * port_aobj_register_halfswapped_range whenever a new range is added,
 * so that figatree-heap reloads (same address, fresh halfswapped bytes)
 * trigger a fresh fixup pass.  See docs/bugs/spline_interp_block_halfswap_2026-04-25.md
 * and project_fixup_idempotency_heap_reuse for the failure mode.
 */
int port_aobj_unhalfswap_visit(const void *p);

#ifdef __cplusplus
}
#endif
