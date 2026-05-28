#include "common.h"

#include <sys/netphase.h>

#include "port_log.h"

#include <stdlib.h>

#include <sys/netpeer_socket_platform.h>

extern char *getenv(const char *name);
extern int atoi(const char *s);

typedef enum SYNetSessionPhase
{
	SYNET_PHASE_IDLE = 0,
	SYNET_PHASE_BARRIER_WAIT,
	SYNET_PHASE_CALIBRATING,
	SYNET_PHASE_RUNNING
} SYNetSessionPhase;

static SYNetSessionPhase s_phase = SYNET_PHASE_IDLE;
static u64 s_cal_start_unix_ms;
static u32 s_cal_budget_ms;

static u64 syNetPhaseNowUnixMs(void)
{
	return syNetPeerOsMonotonicMs();
}

void syNetPhaseReset(void)
{
	s_phase = SYNET_PHASE_IDLE;
	s_cal_start_unix_ms = 0ULL;
	s_cal_budget_ms = 0U;
}

void syNetPhaseOnVSSessionStart(sb32 barrier_enabled)
{
	if (barrier_enabled != FALSE)
	{
		s_phase = SYNET_PHASE_BARRIER_WAIT;
	}
	else
	{
		s_phase = SYNET_PHASE_RUNNING;
	}
	s_cal_start_unix_ms = 0ULL;
	s_cal_budget_ms = 0U;
}

void syNetPhaseBeginOptionalWallCalibrationFromRunning(void)
{
	char *e;
	u32 budget;
	int v;

	if (s_phase != SYNET_PHASE_RUNNING)
	{
		return;
	}
	budget = 0U;
	e = getenv("SSB64_NETPLAY_TICK_GRID_CALIBRATE_MS");
	if ((e != NULL) && (e[0] != '\0'))
	{
		v = atoi(e);
		if ((v > 0) && (v < 600000))
		{
			budget = (u32)v;
		}
	}
	if (budget == 0U)
	{
		return;
	}
	s_phase = SYNET_PHASE_CALIBRATING;
	s_cal_budget_ms = budget;
	s_cal_start_unix_ms = syNetPhaseNowUnixMs();
}

void syNetPhaseOnBattleBarrierReleased(void)
{
	char *e;
	u32 budget;
	int v;

	if (s_phase == SYNET_PHASE_IDLE)
	{
		return;
	}
	if (s_phase == SYNET_PHASE_RUNNING)
	{
		return;
	}
	budget = 0U;
	e = getenv("SSB64_NETPLAY_TICK_GRID_CALIBRATE_MS");
	if ((e != NULL) && (e[0] != '\0'))
	{
		v = atoi(e);
		if ((v > 0) && (v < 600000))
		{
			budget = (u32)v;
		}
	}
	if (budget == 0U)
	{
		s_phase = SYNET_PHASE_RUNNING;
		s_cal_budget_ms = 0U;
		s_cal_start_unix_ms = 0ULL;
		return;
	}
	s_phase = SYNET_PHASE_CALIBRATING;
	s_cal_budget_ms = budget;
	s_cal_start_unix_ms = syNetPhaseNowUnixMs();
}

void syNetPhaseTickWallClock(void)
{
	u64 now;
	u64 elapsed;

	if (s_phase != SYNET_PHASE_CALIBRATING)
	{
		return;
	}
	if (s_cal_budget_ms == 0U)
	{
		s_phase = SYNET_PHASE_RUNNING;
		return;
	}
	now = syNetPhaseNowUnixMs();
	if (s_cal_start_unix_ms == 0ULL)
	{
		s_cal_start_unix_ms = now;
	}
	elapsed = (now >= s_cal_start_unix_ms) ? (now - s_cal_start_unix_ms) : 0ULL;
	if (elapsed >= (u64)s_cal_budget_ms)
	{
		s_phase = SYNET_PHASE_RUNNING;
	}
}

sb32 syNetPhaseIsRunning(void)
{
	return (s_phase == SYNET_PHASE_RUNNING) ? TRUE : FALSE;
}

sb32 syNetPhaseIsCalibrating(void)
{
	return (s_phase == SYNET_PHASE_CALIBRATING) ? TRUE : FALSE;
}

sb32 syNetPhaseAllowsTickGridFeedDeviation(void)
{
	return (s_phase == SYNET_PHASE_CALIBRATING) ? TRUE : FALSE;
}

void syNetPhaseEnterRunning(void)
{
	s_phase = SYNET_PHASE_RUNNING;
	s_cal_budget_ms = 0U;
	s_cal_start_unix_ms = 0ULL;
}
