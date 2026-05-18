#ifndef _SYNETINPUT_TIMELINE_H_
#define _SYNETINPUT_TIMELINE_H_

/*
 * GGPO-style per-player input timeline: tracks earliest sim tick where published inputs disagreed with
 * later strict-confirmed remote rows. Rollback consumes this before scanning published vs remote rings.
 */

#include <PR/ultratypes.h>
#include <sys/netinput.h>

#ifdef PORT
extern void syNetInputTimelineReset(void);
extern void syNetInputTimelineClearIncorrectFrom(u32 from_sim_tick);
extern u32 syNetInputTimelineGetEarliestIncorrect(void);
extern s32 syNetInputTimelineGetEarliestIncorrectPlayer(void);
extern void syNetInputTimelineOnRemoteConfirmedWire(s32 player, u32 wire_tick, const SYNetInputFrame *confirmed);
extern void syNetInputTimelineNotePublishedRemoteMismatch(s32 player, u32 sim_tick);
#endif

#endif /* _SYNETINPUT_TIMELINE_H_ */
