#ifndef _SYNETROLLBACK_H_
#define _SYNETROLLBACK_H_

/*
 * NetRollback — optional input-based rewind for P2P VS (PORT; enable with `SSB64_NETPLAY_ROLLBACK`).
 *
 * After gameplay finishes each sim tick it stores a **partial** world snapshot in a fixed-size ring. During the
 * NetPeer transport phase, if published local history disagrees with confirmed remote frames inside
 * `SYNETROLLBACK_SCAN_WINDOW`, we load the snapshot immediately before the divergence and fast-forward resimulate
 * (`syNetInputFuncRead` + `scVSBattleFuncUpdate`) to the live frontier.
 *
 * Ordering: barrier/execution-ready path runs first; `syNetRollbackAfterBattleUpdate` follows your normal battle
 * update; `syNetRollbackUpdate` consumes the newest remote data when not already inside a resim.
 */

#include <PR/ultratypes.h>
#include <ssb_types.h>

/* Bounded backward search vs remote ring; must stay in sync with `netrollback.c`. */
#define SYNETROLLBACK_SCAN_WINDOW 256

extern void syNetRollbackInit(void); /* Parses rollback env knobs once at startup. */
extern void syNetRollbackStartVSSession(void);
extern void syNetRollbackStopVSSession(void);
extern sb32 syNetRollbackIsActive(void);   /* Env enabled AND VS session flagged. */
extern sb32 syNetRollbackIsResimulating(void); /* TRUE while nested `syNetRollbackRunResim` loop executes. */

extern void syNetRollbackAfterBattleUpdate(void); /* Snapshot completed tick into ring (post-`scVSBattleFuncUpdate`). */
extern void syNetRollbackUpdate(void);            /* NetPeer: detect mismatch, load snapshot, resim forward. */

#ifdef PORT
extern void syNetRollbackDebugOnIncomingRemoteFrame(u32 *tick, u16 *buttons, s8 *stick_x, s8 *stick_y);
extern void syNetRollbackApplyPortSimPacing(u32 refresh_hz);
extern u32 syNetRollbackGetAppliedResimCount(void);
extern u32 syNetRollbackGetLoadFailCount(void);
/* Load snapshot for completed sim tick T (same ring index as SavePostTick(T)). */
extern sb32 syNetRollbackLoadSnapshotAfterCompletedTick(u32 completed_sim_tick);
#endif

#endif /* _SYNETROLLBACK_H_ */
