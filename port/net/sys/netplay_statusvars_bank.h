#pragma once

/*
 * FTStatusVars parallel overlay bank — Approach C2b.
 *
 * Under PORT && SSB64_NETMENU the aliasing FTStruct.status_vars union stops being the authority:
 * each fighter owns nFTStatusVarsOverlayCount independent overlay slots in this sidecar (one per
 * FTStatusVarsOverlay), so forward sim cannot stomp another status's overlay bytes through the
 * union. ftStatusVars* accessors redirect here (see ftstatusvars.h); the union becomes a
 * projection of the live overlay for any residual raw reader.
 *
 * Keyed by fighter (sim player) slot, not by pointer, so rollback / fighter recreation cannot
 * strand the bank. Offline builds never compile this — accessors keep returning &status_vars.
 */

#include <ssb_types.h>

struct FTStruct;

#if defined(PORT) && defined(SSB64_NETMENU)

#include <ft/ftstatusvars.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reset all players' banks + live overlay tags (VS start / session reset). */
extern void syNetplayStatusVarsBankResetSession(void);

/* Zero + tag a fighter's bank when its GObj (re)allocates to a slot. */
extern void syNetplayStatusVarsBankInitFighter(s32 player);

/* Accessor target: bank slot for `overlay` on this fighter (never NULL). */
extern void *syNetplayStatusVarsBankSlot(struct FTStruct *fp, enum FTStatusVarsOverlay overlay);

/* Accessor target when fp is unknown/NULL: falls back to the union projection. */
extern void *syNetplayStatusVarsBankSlotOrUnion(struct FTStruct *fp, enum FTStatusVarsOverlay overlay,
                                                void *union_member);

/*
 * Forward-sim authority slot: the bank when rollback semantics are active (online VS/resim),
 * else the union member — offline modes inside the netmenu binary keep vanilla union aliasing.
 * Only migrate overlays whose SetStatus initializes every field (the bank slot is not seeded
 * from the union on first redirect). Migrated: Turn, KneeBend, JumpAerial, Dead, Rebirth,
 * Damage, Squat, Landing, FallSpecial.
 */
extern void *syNetplayStatusVarsBankAuthoritySlot(struct FTStruct *fp, enum FTStatusVarsOverlay overlay,
                                                  void *union_member);

/* Mirror of the live overlay index, for ring capture tag + SetStatus switch. */
extern s32 syNetplayStatusVarsBankLiveOverlay(s32 player);
extern void syNetplayStatusVarsBankSetLiveOverlay(s32 player, s32 overlay);

/* Copy the live overlay bytes into the union projection (residual raw readers). */
extern void syNetplayStatusVarsBankProjectUnion(struct FTStruct *fp);

/* Snapshot of one overlay slot (ring capture payload) / restore (ring apply). */
extern void syNetplayStatusVarsBankCopyOverlayOut(s32 player, s32 overlay, void *dst, u32 len);
extern void syNetplayStatusVarsBankCopyOverlayIn(s32 player, s32 overlay, const void *src, u32 len);

#ifdef __cplusplus
}
#endif

#endif /* PORT && SSB64_NETMENU */
