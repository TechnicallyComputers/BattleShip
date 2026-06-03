#ifndef SCVSBATTLE_RECONNECT_H
#define SCVSBATTLE_RECONNECT_H

#include <ssb_types.h>

#if defined(PORT) && defined(SSB64_NETMENU)

extern void scVSBattleReconnectApplyForfeit(s32 forfeiting_slot, s32 winner_slot);

#else

#define scVSBattleReconnectApplyForfeit(F, W) ((void)(F), (void)(W))

#endif

#endif
