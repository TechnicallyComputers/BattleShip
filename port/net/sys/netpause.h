#ifndef _SYNETPAUSE_H_
#define _SYNETPAUSE_H_

#include <PR/ultratypes.h>
#include <ssb_types.h>

extern void syNetPauseReset(void);
extern sb32 syNetPauseRequestPauseFromGo(s32 player);
extern sb32 syNetPauseRequestUnpauseFromPause(void);
extern sb32 syNetPauseTryApplyAtBattleBoundary(u32 tick);
extern sb32 syNetPauseShouldDeferBattleSim(u32 tick);
extern sb32 syNetPauseShouldHoldSimTick(void);
extern sb32 syNetPauseRollbackRequireStrictHash(void);
extern void syNetPauseOnRemotePausePacket(u32 tick, s32 player);
extern void syNetPauseOnRemoteUnpausePacket(u32 tick);
extern void syNetPausePollSyncedInputAtTick(u32 tick);

#endif /* _SYNETPAUSE_H_ */
