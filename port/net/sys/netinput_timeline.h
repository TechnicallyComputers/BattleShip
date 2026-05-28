#ifndef _SYNETINPUT_TIMELINE_H_
#define _SYNETINPUT_TIMELINE_H_

/*
 * GGPO-style per-player input timeline: tracks earliest sim tick where published inputs disagreed with
 * later strict-confirmed remote rows. Rollback consumes validated timeline entries (re-checked vs rings at scan time).
 */

#include <PR/ultratypes.h>
#include <sys/netinput.h>

#ifdef PORT
extern void syNetInputTimelineReset(void);
extern void syNetInputTimelineClearIncorrectFrom(u32 from_sim_tick);
/* Reconcile published vs remote for [from,to) and clear stale earliest-incorrect latches. */
extern void syNetInputTimelineClearResolvedSpan(u32 from_sim_tick, u32 to_sim_tick);
extern void syNetInputTimelineReconcilePublishedVsRemote(s32 player, u32 sim_tick);
extern u32 syNetInputTimelineFindEarliestValidatedMismatch(u32 frontier_tick, s32 *out_player);
/* Global min earliest incorrect across remote-human slots (GGPO GetMinIncorrectFrame style). */
extern u32 syNetInputTimelineFindGlobalEarliestIncorrect(u32 frontier_tick, s32 *out_player);
extern u32 syNetInputTimelineGetEarliestIncorrectForPlayer(s32 player);
extern u32 syNetInputTimelineGetLastRemoteConfirmedSimTick(s32 player);
extern u32 syNetInputTimelineGetEarliestIncorrect(void);
extern s32 syNetInputTimelineGetEarliestIncorrectPlayer(void);
extern void syNetInputTimelineOnRemoteConfirmedWire(s32 player, u32 wire_tick, const SYNetInputFrame *confirmed);
extern void syNetInputTimelineNotePublishedRemoteMismatch(s32 player, u32 sim_tick);
#endif

#endif /* _SYNETINPUT_TIMELINE_H_ */
