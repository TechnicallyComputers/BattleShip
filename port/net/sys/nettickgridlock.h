#ifndef SY_NET_TICK_GRID_LOCK_H
#define SY_NET_TICK_GRID_LOCK_H

#include <ssb_types.h>

/*
 * Hybrid GGPO sim-tick index + integer tick-grid alignment (guest toward host).
 *
 * Authority: bootstrap host (`is_authority` on barrier release). 4P: same
 * contract source as BATTLE_START_TIME / barrier VI fields — one grid epoch;
 * future UDP may add explicit `phase_epoch` + `tick_mod` (reserved here).
 *
 * Linux UDP: set `SSB64_NETPLAY_TICK_GRID_EXEC_GATE=1` so `syNetPeerCheckBattleExecutionReady` requires
 * `syNetTickGridLockIsLocked()` for guests (until `syNetTickGridLockFeedDeviation` converges). Default gate off.
 *
 * Phase 1 broad window [SYNET_TICKGRID_BROAD_LO, SYNET_TICKGRID_BROAD_HI],
 * phase 2 fine |D| <= SYNET_TICKGRID_FINE_MAX with D_adjust = alpha*D.
 */

#define SYNET_TICKGRID_BROAD_LO 300
#define SYNET_TICKGRID_BROAD_HI 360
#define SYNET_TICKGRID_FINE_MAX 60
#define SYNET_TICKGRID_ALPHA_NUM 2
#define SYNET_TICKGRID_ALPHA_DEN 1

void syNetTickGridLockOnBarrierReleased(sb32 is_authority);

/**
 * One adjustment step from measured deviation D (integer tick units vs authority grid).
 * @param contract_hz VI/sim contract Hz (from syNetPeerGetVsContractViHz), for ns bias actuator.
 * @return TRUE when locked (authority, D==0, or both windows satisfied — no further adjustment).
 */
sb32 syNetTickGridLockFeedDeviation(s32 D, u32 contract_hz);

sb32 syNetTickGridLockIsLocked(void);

#endif /* SY_NET_TICK_GRID_LOCK_H */
