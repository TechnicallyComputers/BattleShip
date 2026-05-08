#include "common.h"

#include <sys/nettickgridlock.h>
#include <sys/netphase.h>

#include <stdlib.h>

#include "gameloop.h"
#include "port_log.h"

/* ---- FSM (guest): broad -> fine iteration, authority short-circuits locked ---- */

static sb32 s_authority;
static sb32 s_locked;
static s32 s_scale; /* integer damping; overshoot reduces toward 2 */
static u32 s_undershoot_streak;

static s64 syNetTickGridLockTicksToNs(s32 ticks, u32 contract_hz)
{
	u32 hz;
	s64 per;

	hz = contract_hz;
	if (hz < 1U)
	{
		hz = 60U;
	}
	if (hz > 480U)
	{
		hz = 480U;
	}
	per = 1000000000LL / (s64)hz;
	return (s64)ticks * per;
}

void syNetTickGridLockOnBarrierReleased(sb32 is_authority)
{
	s_authority = (is_authority != FALSE) ? TRUE : FALSE;
	s_locked = s_authority;
	s_scale = 2;
	s_undershoot_streak = 0U;
}

sb32 syNetTickGridLockIsLocked(void)
{
	return s_locked;
}

/*
 * Deterministic tick adjustment cycle (single step; caller re-simulates and re-feeds D).
 * Phase 1: |D| in [BROAD_LO, BROAD_HI]  -> offset += D
 * Phase 2: 1 <= |D| <= FINE_MAX         -> offset += alpha*D
 * Gap 61..BROAD_LO-1: undershoot path — accumulate streak, optional scale-up after 2 undershoots.
 * |D| > BROAD_HI: overshoot — dampen step, clamp scale back toward 2.
 */
sb32 syNetTickGridLockFeedDeviation(s32 D, u32 contract_hz)
{
	s32 ad;
	s32 adj;
	char *diag;

	if (s_authority != FALSE)
	{
		return TRUE;
	}
	if (s_locked != FALSE)
	{
		return TRUE;
	}
	if (syNetPhaseAllowsTickGridFeedDeviation() == FALSE)
	{
		static u32 s_feed_disallow_log_counter;

		if (D == 0)
		{
			s_locked = TRUE;
			return TRUE;
		}
		s_feed_disallow_log_counter++;
		if ((s_feed_disallow_log_counter & 127U) == 1U)
		{
			port_log("SSB64 TickGridLock: FeedDeviation ignored outside CALIBRATING (D=%d)\n", (int)D);
		}
		return FALSE;
	}
	if (D == 0)
	{
		s_locked = TRUE;
		return TRUE;
	}

	ad = (D < 0) ? -D : D;
	adj = 0;

	if ((ad >= SYNET_TICKGRID_BROAD_LO) && (ad <= SYNET_TICKGRID_BROAD_HI))
	{
		adj = D;
		s_undershoot_streak = 0U;
	}
	else if ((ad >= 1) && (ad <= SYNET_TICKGRID_FINE_MAX))
	{
		adj = (D * SYNET_TICKGRID_ALPHA_NUM) / SYNET_TICKGRID_ALPHA_DEN;
		s_undershoot_streak = 0U;
	}
	else if (ad > SYNET_TICKGRID_BROAD_HI)
	{
		/* Overshoot vs broad window — dampen and reset integer scale to baseline. */
		if (s_scale < 2)
		{
			s_scale = 2;
		}
		adj = D / s_scale;
		s_scale = 2;
	}
	else
	{
		/* Undershoot band (between fine max and broad low): second-pass hint. */
		adj = D;
		s_undershoot_streak++;
		if (s_undershoot_streak >= 2U)
		{
			if (s_scale < 1000000)
			{
				s_scale++;
			}
			s_undershoot_streak = 0U;
		}
	}

	if (adj != 0)
	{
		port_add_vs_decouple_barrier_latch_bias_ns((long long)syNetTickGridLockTicksToNs(adj, contract_hz));
	}

	diag = getenv("SSB64_NETPLAY_TICK_GRID_LOCK_DIAG");
	if ((diag != NULL) && (diag[0] != '\0') && (strtol(diag, NULL, 10) != 0))
	{
		port_log("SSB64 TickGridLock: D=%d adj=%d scale=%d locked=%d auth=%d\n", (int)D, (int)adj, (int)s_scale,
		         (s_locked != FALSE) ? 1 : 0, (s_authority != FALSE) ? 1 : 0);
	}

	return FALSE;
}
