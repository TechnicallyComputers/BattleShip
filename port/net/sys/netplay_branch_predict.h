#ifndef SYS_NETPLAY_BRANCH_PREDICT_H
#define SYS_NETPLAY_BRANCH_PREDICT_H

#include <PR/ultratypes.h>
#include <ssb_types.h>

struct GObj;

/*
 * Forward prediction policy for branch-sensitive gameplay evaluation.
 *
 * Continuous simulation (physics, drift, anim countdown) may run on predicted
 * remote input. Branch evaluation — prepare side effects + optional status
 * commit — must be transactional when the driving remote input is still
 * predicted: capture a preimage, let vanilla evaluate against live state, then
 * commit or discard the whole candidate together.
 *
 * A branch is not only SetStatus; preparatory writes (lr_dash, buffers, entry
 * pins, …) are part of the same logical transaction.
 *
 * See docs/bugs/netplay_branch_sensitive_predict_2026-07-20.md.
 */

typedef enum SYNetplayBranchInputClass
{
	nSYNetplayBranchInputLocal = 0,          /* local / non-remote human — always may commit */
	nSYNetplayBranchInputAuthoritative = 1, /* remote History confirmed (!predicted) */
	nSYNetplayBranchInputPredicted = 2,      /* remote History still predicted (hold_last / predict) */
	nSYNetplayBranchInputUnknown = 3         /* no History row — treat as allow (fail-open) */
} SYNetplayBranchInputClass;

#define SYNETPLAY_BRANCH_SNAPSHOT_MAX 64

#if defined(PORT) && defined(SSB64_NETMENU)

/* Classify driving History for player at the current sim tick. */
extern SYNetplayBranchInputClass syNetplayBranchClassifyDrivingInput(s32 player);

/*
 * Begin a branch-evaluation transaction.
 *
 * preimage / size: caller-captured prepare-state blob (may be NULL / 0 when the
 * branch has no side effects beyond SetStatus). When driving input is predicted
 * remote, the blob is retained for discard on Resolve.
 *
 * Returns TRUE when evaluation is speculative (predicted remote under rollback).
 */
extern sb32 syNetplayBranchEvalBegin(struct GObj *fighter_gobj, const char *transition_name,
                                     const void *preimage, u32 size);

/*
 * Finish the transaction.
 *
 * wants_branch: TRUE if vanilla would take the irreversible status transition.
 * restore_fn: when speculative, called with the Begin preimage so the caller
 * can write live state back (NULL = status-only / no side-effect restore).
 *
 * Returns TRUE only when the status transition may proceed (authoritative/local
 * and wants_branch). Speculative eval always discards prepare writes and
 * returns FALSE for status commit.
 *
 * Logs BRANCH_PREDICTED_INPUT / BRANCH_DISCARD_SIDE_EFFECTS / BRANCH_DEFERRED /
 * BRANCH_COMMITTED.
 */
typedef void (*SYNetplayBranchRestoreFn)(struct GObj *fighter_gobj, const void *preimage, u32 size);

extern sb32 syNetplayBranchEvalResolve(struct GObj *fighter_gobj, sb32 wants_branch,
                                       SYNetplayBranchRestoreFn restore_fn);

/*
 * Status-only convenience (no prepare snapshot). Prefer Begin/Resolve when the
 * branch has preparatory writes.
 */
extern sb32 syNetplayBranchSensitiveMayCommit(struct GObj *fighter_gobj, const char *transition_name,
                                              sb32 wants_branch);

/* --- Turn allow / DashCheckTurn / DashSetStatus (first transactional consumer) --- */

typedef struct SYNetplayBranchTurnDashSnap
{
	s32 lr_dash;
	s32 attacks4_buffer;
	s32 entry_lr_dash;
} SYNetplayBranchTurnDashSnap;

extern void syNetplayBranchTurnDashCapture(struct GObj *fighter_gobj, SYNetplayBranchTurnDashSnap *snap);
extern void syNetplayBranchTurnDashApply(struct GObj *fighter_gobj, const SYNetplayBranchTurnDashSnap *snap);
extern void syNetplayBranchTurnDashRestoreFn(struct GObj *fighter_gobj, const void *preimage, u32 size);

/*
 * Capture + Begin for turn_allow_dash. Call immediately before DashCheckTurn.
 * Returns TRUE when speculative (same as EvalBegin).
 */
extern sb32 syNetplayBranchTurnDashEvalBegin(struct GObj *fighter_gobj);

/* Resolve Turn/Dash transaction; restore prepare fields when speculative. */
extern sb32 syNetplayBranchTurnDashEvalResolve(struct GObj *fighter_gobj, sb32 wants_branch);

#else

#define syNetplayBranchClassifyDrivingInput(player) ((SYNetplayBranchInputClass)nSYNetplayBranchInputLocal)
#define syNetplayBranchEvalBegin(fighter_gobj, transition_name, preimage, size) ((sb32)FALSE)
#define syNetplayBranchEvalResolve(fighter_gobj, wants_branch, restore_fn) ((sb32)(wants_branch))
#define syNetplayBranchSensitiveMayCommit(fighter_gobj, transition_name, wants_branch) ((sb32)(wants_branch))
#define syNetplayBranchTurnDashCapture(fighter_gobj, snap) ((void)0)
#define syNetplayBranchTurnDashApply(fighter_gobj, snap) ((void)0)
#define syNetplayBranchTurnDashRestoreFn(fighter_gobj, preimage, size) ((void)0)
#define syNetplayBranchTurnDashEvalBegin(fighter_gobj) ((sb32)FALSE)
#define syNetplayBranchTurnDashEvalResolve(fighter_gobj, wants_branch) ((sb32)(wants_branch))

#endif /* PORT && SSB64_NETMENU */

#endif /* SYS_NETPLAY_BRANCH_PREDICT_H */
