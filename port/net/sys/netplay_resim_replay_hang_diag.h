#ifndef _NETPLAY_RESIM_REPLAY_HANG_DIAG_H_
#define _NETPLAY_RESIM_REPLAY_HANG_DIAG_H_

#include <ssb_types.h>

#if defined(PORT) && defined(SSB64_NETMENU)

struct GObj;
struct GObjProcess;

extern sb32 syNetplayResimReplayHangDiagEnabled(void);

extern void syNetplayResimReplayHangDiagNotePacketIngressEnter(void);
extern void syNetplayResimReplayHangDiagNotePacketIngressExit(void);

extern void syNetplayResimReplayHangDiagNoteReplayGateOpen(const char *caller_tag);
extern void syNetplayResimReplayHangDiagNoteReplayTickBegin(u32 tick, u32 ran_index, u32 tick_limit);
extern void syNetplayResimReplayHangDiagNoteReplayTickEnd(u32 tick);

extern void syNetplayResimReplayHangDiagNoteBattleSimOnlyBegin(const char *caller_tag);
extern void syNetplayResimReplayHangDiagNoteBattleSimOnlyEnd(void);

extern void syNetplayResimReplayHangDiagNoteGcRunGObj(struct GObj *gobj);
extern void syNetplayResimReplayHangDiagNoteGcRunGObjProcessBegin(struct GObjProcess *gobjproc);
extern void syNetplayResimReplayHangDiagNoteGcRunGObjProcessThreadRecvWait(struct GObjProcess *gobjproc,
                                                                           s32 queue_valid);
extern void syNetplayResimReplayHangDiagNoteGcRunGObjProcessEnd(void);

extern void syNetplayResimReplayHangDiagLogHangSnapshot(void);

#else

#define syNetplayResimReplayHangDiagEnabled() FALSE
#define syNetplayResimReplayHangDiagNotePacketIngressEnter() ((void)0)
#define syNetplayResimReplayHangDiagNotePacketIngressExit() ((void)0)
#define syNetplayResimReplayHangDiagNoteReplayGateOpen(caller_tag) ((void)0)
#define syNetplayResimReplayHangDiagNoteReplayTickBegin(tick, ran_index, tick_limit) ((void)0)
#define syNetplayResimReplayHangDiagNoteReplayTickEnd(tick) ((void)0)
#define syNetplayResimReplayHangDiagNoteBattleSimOnlyBegin(caller_tag) ((void)0)
#define syNetplayResimReplayHangDiagNoteBattleSimOnlyEnd() ((void)0)
#define syNetplayResimReplayHangDiagNoteGcRunGObj(gobj) ((void)0)
#define syNetplayResimReplayHangDiagNoteGcRunGObjProcessBegin(gobjproc) ((void)0)
#define syNetplayResimReplayHangDiagNoteGcRunGObjProcessThreadRecvWait(gobjproc, queue_valid) ((void)0)
#define syNetplayResimReplayHangDiagNoteGcRunGObjProcessEnd() ((void)0)
#define syNetplayResimReplayHangDiagLogHangSnapshot() ((void)0)

#endif /* PORT && SSB64_NETMENU */

#endif /* _NETPLAY_RESIM_REPLAY_HANG_DIAG_H_ */
