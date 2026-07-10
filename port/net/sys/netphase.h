#ifndef SY_NET_PHASE_H
#define SY_NET_PHASE_H

#include <ssb_types.h>

/*
 * VS UDP match lifecycle: barrier → optional bounded tick-grid calibration → RUNNING.
 * See docs/netplay_running_invariants.md.
 */

void syNetPhaseReset(void);
void syNetPhaseOnVSSessionStart(sb32 barrier_enabled);
void syNetPhaseOnBattleBarrierReleased(void);
/*
 * When the session is already RUNNING (no clock barrier) and `SSB64_NETPLAY_TICK_GRID_CALIBRATE_MS` is set,
 * enter CALIBRATING so bounded tick-grid feed deviation can run before the first committed sim tick
 * (e.g. during BATTLE_EXEC_SYNC / execution hold).
 */
void syNetPhaseBeginOptionalWallCalibrationFromRunning(void);
/* Wall-clock progress for CALIBRATING timeout (call from battle gate / peer update). */
void syNetPhaseTickWallClock(void);

sb32 syNetPhaseIsRunning(void);
sb32 syNetPhaseIsCalibrating(void);
/* TRUE only during CALIBRATING — the only phase where syNetTickGridLockFeedDeviation may mutate state. */
sb32 syNetPhaseAllowsTickGridFeedDeviation(void);
void syNetPhaseEnterRunning(void);

#endif /* SY_NET_PHASE_H */
