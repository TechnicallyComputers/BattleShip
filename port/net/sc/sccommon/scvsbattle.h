#ifndef _SCVSBATTLE_H_
#define _SCVSBATTLE_H_

/*
 * Netmenu shadow of decomp/src/sc/sccommon/scvsbattle.h — keep in sync when the stock
 * header changes; add PORT-only entry points implemented under port/net here.
 */
#include <ssb_types.h>
#include <sys/objdef.h>
#include <PR/gbi.h>
#include <sc/scdef.h>

extern void scVSBattleFuncUpdate(void);
extern s32 scVSBattleGetStartPlayerLR(s32 this_player);
extern void scVSBattleStartBattle(void);
extern sb32 scVSBattleSetScoreCheckSuddenDeath(void);
extern void scVSBattleStartSuddenDeath(void);
extern void scVSBattleFuncLights(Gfx **dls);
extern void scVSBattleStartScene(void);
extern void scVSBattleSetupFiles(void);

#ifdef PORT
extern void scVSBattleFuncUpdateSkewPacingNetSlice(void);
#endif

#endif
