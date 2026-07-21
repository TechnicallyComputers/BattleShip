/*
 * Netplay RNG bisect: per-tick game-seed LCG step ring + optional live step log.
 * Compare host/guest at the same sim_tick; first differing step index pinpoints the
 * asymmetric consumer (Whispy wind, effect manager, item rolls, …).
 */
#include <common.h>
#include <stdlib.h>
#include <string.h>

#include <sys/netsync_rng_trace.h>

#if defined(PORT) && defined(SSB64_NETMENU)

#include <sys/netinput.h>
#include <sys/netrollback.h>
#include <sys/netsync.h>
#include <sys/utils.h>

extern void port_log(const char *fmt, ...);

#define SYNET_SYNC_RNG_STEP_RING_MAX 512U

typedef struct SYNetSyncRngStep
{
	u32 seed_after;
	u32 site;
} SYNetSyncRngStep;

static u32 sSYNetSyncRngTraceRecordingTick = ~(u32)0;
static u32 sSYNetSyncRngTraceSeedAtTickBegin;
static u32 sSYNetSyncRngTraceStepCount;
static sb32 sSYNetSyncRngTraceRingTruncated;
static SYNetSyncRngStep sSYNetSyncRngTraceRing[SYNET_SYNC_RNG_STEP_RING_MAX];

/* Completed-tick copy so frame-commit at validation T can dump snap tick T-1 steps. */
static u32 sSYNetSyncRngTraceArchivedTick = ~(u32)0;
static u32 sSYNetSyncRngTraceArchivedStepCount;
static sb32 sSYNetSyncRngTraceArchivedTruncated;
static SYNetSyncRngStep sSYNetSyncRngTraceArchivedRing[SYNET_SYNC_RNG_STEP_RING_MAX];

static sb32 sSYNetSyncRngHashTraceEnv = -1;
static sb32 sSYNetSyncRngStepTraceEnv = -1;
static sb32 sSYNetSyncRngStepSiteEnv = -1;
static s32 sSYNetSyncRngTraceTickMin = -1;
static s32 sSYNetSyncRngTraceTickMax = -1;

static sb32 syNetSyncRngEnvNonZero(const char *e)
{
	char c;

	if ((e == NULL) || (e[0] == '\0'))
	{
		return FALSE;
	}
	c = e[0];
	if ((c == '1') || (c == 'y') || (c == 'Y') || (c == 't') || (c == 'T'))
	{
		return TRUE;
	}
	if ((c >= '2') && (c <= '9'))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetSyncRngEnvFlag(const char *name, sb32 *cache)
{
	const char *e;

	if (*cache >= 0)
	{
		return (*cache != 0) ? TRUE : FALSE;
	}
	e = getenv(name);
	*cache = syNetSyncRngEnvNonZero(e) ? 1 : 0;
	return (*cache != 0) ? TRUE : FALSE;
}

static s32 syNetSyncRngParseTickEnv(const char *name, s32 default_val)
{
	const char *e;
	const char *p;
	s32 v;
	s32 sign;

	e = getenv(name);
	if ((e == NULL) || (e[0] == '\0'))
	{
		return default_val;
	}
	p = e;
	sign = 1;
	if (*p == '-')
	{
		sign = -1;
		p++;
	}
	v = 0;
	while ((*p >= '0') && (*p <= '9'))
	{
		v = (v * 10) + (*p - '0');
		p++;
	}
	return v * sign;
}

static void syNetSyncRngTraceParseTickWindow(void)
{
	if (sSYNetSyncRngTraceTickMin != -1)
	{
		return;
	}
	sSYNetSyncRngTraceTickMin = syNetSyncRngParseTickEnv("SSB64_NETPLAY_RNG_TRACE_TICK_MIN", 0);
	sSYNetSyncRngTraceTickMax = syNetSyncRngParseTickEnv("SSB64_NETPLAY_RNG_TRACE_TICK_MAX", 0);
	if (sSYNetSyncRngTraceTickMin < 0)
	{
		sSYNetSyncRngTraceTickMin = 0;
	}
	if (sSYNetSyncRngTraceTickMax < 0)
	{
		sSYNetSyncRngTraceTickMax = 0;
	}
}

static sb32 syNetSyncRngTraceTickInWindow(u32 tick)
{
	syNetSyncRngTraceParseTickWindow();
	if ((sSYNetSyncRngTraceTickMin > 0) && (tick < (u32)sSYNetSyncRngTraceTickMin))
	{
		return FALSE;
	}
	if ((sSYNetSyncRngTraceTickMax > 0) && (tick > (u32)sSYNetSyncRngTraceTickMax))
	{
		return FALSE;
	}
	return TRUE;
}

static sb32 syNetSyncRngHashTraceEnabled(void)
{
	return syNetSyncRngEnvFlag("SSB64_NETPLAY_RNG_HASH_TRACE", &sSYNetSyncRngHashTraceEnv);
}

static sb32 syNetSyncRngStepTraceEnabled(void)
{
	return syNetSyncRngEnvFlag("SSB64_NETPLAY_RNG_STEP_TRACE", &sSYNetSyncRngStepTraceEnv);
}

static sb32 syNetSyncRngStepSiteEnabled(void)
{
	return syNetSyncRngEnvFlag("SSB64_NETPLAY_RNG_STEP_SITE", &sSYNetSyncRngStepSiteEnv);
}

static sb32 syNetSyncRngTraceShouldRecord(u32 tick)
{
	/* Always ring-buffer gameplay LCG steps during an active netplay VS session so
	 * FRAME_COMMIT rng diverge can dump the prior tick without env flags. */
	if (syNetRollbackIsActive() != FALSE)
	{
		return TRUE;
	}
	if (syNetSyncRngTraceTickInWindow(tick) == FALSE)
	{
		return FALSE;
	}
	return (syNetSyncRngHashTraceEnabled() != FALSE) || (syNetSyncRngStepTraceEnabled() != FALSE);
}

static void syNetSyncRngTraceResetRing(void)
{
	sSYNetSyncRngTraceStepCount = 0U;
	sSYNetSyncRngTraceRingTruncated = FALSE;
}

static void syNetSyncRngTraceEmitTickBegin(u32 tick, s32 seed_at_begin)
{
	port_log(
	    "SSB64 NetSync: rng_tick_boundary begin sim_tick=%u live_sim=%u seed_at_begin=0x%08X cosmetic_seed=0x%08X "
	    "resim=%d\n",
	    tick,
	    (unsigned int)syNetInputGetTick(),
	    (unsigned int)(u32)seed_at_begin,
	    (unsigned int)(u32)syUtilsCosmeticRandSeed(),
	    (int)(syNetRollbackIsResimulating() != FALSE));
}

static void syNetSyncRngTraceEmitTickEnd(u32 tick)
{
	u32 hash;

	hash = syNetSyncHashRNGSeed();
	port_log(
	    "SSB64 NetSync: rng_tick_boundary end sim_tick=%u steps=%u truncated=%d hash=0x%08X seed_at_end=0x%08X "
	    "cosmetic_seed=0x%08X\n",
	    tick,
	    sSYNetSyncRngTraceStepCount,
	    (int)sSYNetSyncRngTraceRingTruncated,
	    hash,
	    (unsigned int)(u32)syUtilsRandSeed(),
	    (unsigned int)(u32)syUtilsCosmeticRandSeed());
}

static void syNetSyncRngTraceArchiveCompletedTick(u32 tick)
{
	u32 copy_count;

	copy_count = sSYNetSyncRngTraceStepCount;
	if (copy_count > SYNET_SYNC_RNG_STEP_RING_MAX)
	{
		copy_count = SYNET_SYNC_RNG_STEP_RING_MAX;
	}
	memcpy(sSYNetSyncRngTraceArchivedRing, sSYNetSyncRngTraceRing, copy_count * sizeof(SYNetSyncRngStep));
	sSYNetSyncRngTraceArchivedTick = tick;
	sSYNetSyncRngTraceArchivedStepCount = sSYNetSyncRngTraceStepCount;
	sSYNetSyncRngTraceArchivedTruncated = sSYNetSyncRngTraceRingTruncated;
}

static void syNetSyncRngTraceFlushRecordingTick(void)
{
	if (sSYNetSyncRngTraceRecordingTick == ~(u32)0)
	{
		return;
	}
	if (sSYNetSyncRngTraceStepCount > 0U)
	{
		syNetSyncRngTraceArchiveCompletedTick(sSYNetSyncRngTraceRecordingTick);
	}
	if (syNetSyncRngHashTraceEnabled() != FALSE)
	{
		syNetSyncRngTraceEmitTickEnd(sSYNetSyncRngTraceRecordingTick);
	}
	sSYNetSyncRngTraceRecordingTick = ~(u32)0;
	syNetSyncRngTraceResetRing();
}

void syNetSyncRngTraceBeforeGameSeedStep(void)
{
	u32 tick;

	tick = (u32)syNetInputGetTick();
	if (syNetSyncRngTraceShouldRecord(tick) == FALSE)
	{
		if (sSYNetSyncRngTraceRecordingTick != ~(u32)0)
		{
			syNetSyncRngTraceFlushRecordingTick();
		}
		return;
	}
	if (tick != sSYNetSyncRngTraceRecordingTick)
	{
		syNetSyncRngTraceFlushRecordingTick();
		sSYNetSyncRngTraceRecordingTick = tick;
		sSYNetSyncRngTraceSeedAtTickBegin = (u32)syUtilsRandSeed();
		syNetSyncRngTraceResetRing();
		if (syNetSyncRngHashTraceEnabled() != FALSE)
		{
			syNetSyncRngTraceEmitTickBegin(tick, syUtilsRandSeed());
		}
	}
}

void syNetSyncRngTraceAfterGameSeedStep(s32 seed_after, u32 caller_site)
{
	u32 tick;
	u32 step_idx;
	u32 site;

	tick = (u32)syNetInputGetTick();
	if (syNetSyncRngTraceShouldRecord(tick) == FALSE)
	{
		return;
	}
	if (tick != sSYNetSyncRngTraceRecordingTick)
	{
		syNetSyncRngTraceBeforeGameSeedStep();
		if (tick != sSYNetSyncRngTraceRecordingTick)
		{
			return;
		}
	}
	step_idx = sSYNetSyncRngTraceStepCount;
	/*
	 * Always capture caller PC into the ring (cheap). Verbose per-step logs stay behind
	 * SSB64_NETPLAY_RNG_STEP_TRACE / _SITE. Resim LCG burns dump sites via rng_hash_walk
	 * without requiring env (soak 1790844706 initiator vs follower post-rng).
	 */
	site = caller_site;
	if (step_idx < SYNET_SYNC_RNG_STEP_RING_MAX)
	{
		sSYNetSyncRngTraceRing[step_idx].seed_after = (u32)seed_after;
		sSYNetSyncRngTraceRing[step_idx].site = site;
	}
	else
	{
		sSYNetSyncRngTraceRingTruncated = TRUE;
	}
	sSYNetSyncRngTraceStepCount++;
	if (syNetSyncRngStepTraceEnabled() != FALSE)
	{
		if (syNetSyncRngStepSiteEnabled() != FALSE)
		{
			port_log(
			    "SSB64 NetSync: rng_step sim_tick=%u step=%u seed_after=0x%08X site=0x%08X\n",
			    tick,
			    step_idx,
			    (unsigned int)(u32)seed_after,
			    site);
		}
		else
		{
			port_log("SSB64 NetSync: rng_step sim_tick=%u step=%u seed_after=0x%08X\n",
			         tick,
			         step_idx,
			         (unsigned int)(u32)seed_after);
		}
	}
}

static void syNetSyncLogRngHashWalkDumpSteps(const SYNetSyncRngStep *steps, u32 step_count, sb32 truncated)
{
	u32 idx;
	u32 dump_count;

	dump_count = step_count;
	if (dump_count > SYNET_SYNC_RNG_STEP_RING_MAX)
	{
		dump_count = SYNET_SYNC_RNG_STEP_RING_MAX;
	}
	for (idx = 0U; idx < dump_count; idx++)
	{
		/* Sites are always recorded in the ring; emit them on every walk dump. */
		port_log("SSB64 NetSync: rng_hash_walk step=%u seed_after=0x%08X site=0x%08X\n",
		         idx,
		         steps[idx].seed_after,
		         steps[idx].site);
	}
	if (truncated != FALSE)
	{
		port_log("SSB64 NetSync: rng_hash_walk truncated steps beyond %u\n", SYNET_SYNC_RNG_STEP_RING_MAX);
	}
}

static void syNetSyncLogRngHashWalkBody(u32 sim_tick, u32 local_rng, u32 peer_rng, const char *reason)
{
	u32 hash;
	u32 dump_count;
	const SYNetSyncRngStep *steps;
	sb32 truncated;
	u32 archived_tick;

	syNetSyncRngTraceFlushRecordingTick();
	hash = syNetSyncHashRNGSeed();
	steps = sSYNetSyncRngTraceRing;
	dump_count = sSYNetSyncRngTraceStepCount;
	truncated = sSYNetSyncRngTraceRingTruncated;
	archived_tick = sSYNetSyncRngTraceArchivedTick;
	/* Frame-commit validation at T queries sim_tick=T-1; prefer the last archived tick <= sim_tick. */
	if ((sSYNetSyncRngTraceArchivedStepCount > 0U) && (archived_tick <= sim_tick))
	{
		steps = sSYNetSyncRngTraceArchivedRing;
		dump_count = sSYNetSyncRngTraceArchivedStepCount;
		truncated = sSYNetSyncRngTraceArchivedTruncated;
	}
	port_log(
	    "SSB64 NetSync: rng_hash_walk begin sim_tick=%u live_sim=%u reason=%s local_rng=0x%08X peer_rng=0x%08X "
	    "game_seed=0x%08X cosmetic_seed=0x%08X hash=0x%08X ring_steps=%u archived_tick=%u truncated=%d\n",
	    sim_tick,
	    (unsigned int)syNetInputGetTick(),
	    (reason != NULL) ? reason : "trace",
	    local_rng,
	    peer_rng,
	    (unsigned int)(u32)syUtilsRandSeed(),
	    (unsigned int)(u32)syUtilsCosmeticRandSeed(),
	    hash,
	    dump_count,
	    archived_tick,
	    (int)truncated);
	syNetSyncLogRngHashWalkDumpSteps(steps, dump_count, truncated);
	port_log(
	    "SSB64 NetSync: rng_hash_walk end sim_tick=%u count=%u hash=0x%08X local_rng=0x%08X peer_rng=0x%08X\n",
	    sim_tick,
	    dump_count,
	    hash,
	    local_rng,
	    peer_rng);
	syNetSyncRngTraceResetRing();
}

void syNetSyncLogRngHashWalkTrace(u32 sim_tick)
{
	if ((syNetSyncRngHashTraceEnabled() == FALSE) && (syNetSyncRngStepTraceEnabled() == FALSE))
	{
		return;
	}
	syNetSyncLogRngHashWalkBody(sim_tick, syNetSyncHashRNGSeed(), syNetSyncHashRNGSeed(), "env_trace");
}

void syNetSyncLogRngHashDriftDiag(u32 sim_tick, u32 local_rng, u32 peer_rng, const char *reason)
{
	syNetSyncLogRngHashWalkBody(sim_tick, local_rng, peer_rng, reason);
}

#else /* PORT && SSB64_NETMENU */

void syNetSyncRngTraceBeforeGameSeedStep(void)
{
}

void syNetSyncRngTraceAfterGameSeedStep(s32 seed_after, u32 caller_site)
{
	(void)seed_after;
	(void)caller_site;
}

void syNetSyncLogRngHashWalkTrace(u32 sim_tick)
{
	(void)sim_tick;
}

void syNetSyncLogRngHashDriftDiag(u32 sim_tick, u32 local_rng, u32 peer_rng, const char *reason)
{
	(void)sim_tick;
	(void)local_rng;
	(void)peer_rng;
	(void)reason;
}

#endif /* PORT && SSB64_NETMENU */
