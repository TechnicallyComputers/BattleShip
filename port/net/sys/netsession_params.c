#include "netsession_params.h"

#include <ssb_types.h>
#include <stdlib.h>
#include <string.h>

#include "netpeer.h"
#include "netrollback.h"
#include "netrollbacksnapshot.h"

extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);

#define SYNETSESSION_PARAMS_SIM_HZ_DEFAULT 60U
#define SYNETSESSION_PARAMS_DELAY_MIN 1U
#define SYNETSESSION_PARAMS_DELAY_MAX 32U
#define SYNETSESSION_PARAMS_ADAPTIVE_HEADROOM_DEFAULT 1U
#define SYNETSESSION_PARAMS_PHASE_LOCK_MAX 16U
#define SYNETSESSION_PARAMS_PREDICTION_MARGIN_TICKS 2U
#define SYNETSESSION_PARAMS_PREDICTION_RUNWAY_MIN 4U
#define SYNETSESSION_PARAMS_ROLLBACK_D_MIN 2U
#define SYNETSESSION_PARAMS_ROLLBACK_D_MAX 10U
#define SYNETSESSION_PARAMS_PREDICTION_MAX 8U
#define SYNETSESSION_PARAMS_SNAPSHOT_FRAMES_MIN 48U
#define SYNETSESSION_PARAMS_SNAPSHOT_FRAMES_MAX 128U
#define SYNETSESSION_PARAMS_RESIM_TICKS_MIN 4U
#define SYNETSESSION_PARAMS_RESIM_TICKS_MAX 16U

static sb32 sSYNetSessionParamsNegotiatedValid;
static SYNetSessionParams sSYNetSessionParamsNegotiated;
static u32 sSYNetSessionParamsEffectiveDelayCeil;

static u32 syNetSessionParamsReadSimHz(void)
{
	const char *e;
	s32 hz;

	e = getenv("SSB64_NETPLAY_SIM_HZ");
	if ((e == NULL) || (e[0] == '\0'))
	{
		return SYNETSESSION_PARAMS_SIM_HZ_DEFAULT;
	}
	hz = atoi(e);
	if (hz < 30)
	{
		hz = 30;
	}
	if (hz > 240)
	{
		hz = 240;
	}
	return (u32)hz;
}

static u32 syNetSessionParamsCeilDiv(u32 num, u32 den)
{
	if (den == 0U)
	{
		return num;
	}
	return (num + den - 1U) / den;
}

sb32 syNetSessionParamsAutoNegotiationEnabled(void)
{
	const char *e;

	e = getenv("SSB64_NETPLAY_AUTO_SESSION_PARAMS");
	if ((e != NULL) && (e[0] != '\0') && (atoi(e) == 0))
	{
		return FALSE;
	}
	return TRUE;
}

sb32 syNetSessionParamsManualDelayOverrideActive(void)
{
	const char *e;

	if (getenv("SSB64_NETPLAY_MATCH_INPUT_DELAY") != NULL)
	{
		return TRUE;
	}
	e = getenv("SSB64_NETPLAY_DELAY");
	if ((e != NULL) && (e[0] != '\0'))
	{
		return TRUE;
	}
	return FALSE;
}

void syNetSessionParamsResetForNewMatch(void)
{
	sSYNetSessionParamsNegotiatedValid = FALSE;
	memset(&sSYNetSessionParamsNegotiated, 0, sizeof(sSYNetSessionParamsNegotiated));
	sSYNetSessionParamsEffectiveDelayCeil = 0U;
}

static u32 syNetSessionParamsComputeSnapshotFramesFromRtt(u32 rtt_ms)
{
	u32 frames;

	frames = 24U + (rtt_ms / 25U);
	if (frames < SYNETSESSION_PARAMS_SNAPSHOT_FRAMES_MIN)
	{
		frames = SYNETSESSION_PARAMS_SNAPSHOT_FRAMES_MIN;
	}
	if (frames > SYNETSESSION_PARAMS_SNAPSHOT_FRAMES_MAX)
	{
		frames = SYNETSESSION_PARAMS_SNAPSHOT_FRAMES_MAX;
	}
	return frames;
}

/*
 * Delay vs prediction runway (phase_lock) by connection quality.
 * Ratio is delay-heavy: prediction depth <= D on excellent links, up to ~1.5x D on good, capped on high RTT.
 */
static void syNetSessionParamsComputeDelayAndPrediction(u32 rtt_ms, u32 one_way_ticks, u32 *out_d_ticks,
							u32 *out_phase_lock)
{
	u32 d_ticks;
	u32 phase_lock;
	u32 pred_cap;

	if ((out_d_ticks == NULL) || (out_phase_lock == NULL))
	{
		return;
	}
	d_ticks = one_way_ticks;
	phase_lock = one_way_ticks;
	if (rtt_ms < 80U)
	{
		/* Excellent (<60–80ms RTT): D 2–4, prediction 2–4, ~1:1 delay-heavy */
		if (d_ticks < 2U)
		{
			d_ticks = 2U;
		}
		if (d_ticks > 4U)
		{
			d_ticks = 4U;
		}
		phase_lock = d_ticks;
		if (phase_lock < 2U)
		{
			phase_lock = 2U;
		}
	}
	else if (rtt_ms < 150U)
	{
		/* Good (80–150ms): D 3–5, prediction 4–6, up to ~1.5:1 */
		d_ticks = one_way_ticks + 1U;
		if (d_ticks < 3U)
		{
			d_ticks = 3U;
		}
		if (d_ticks > 5U)
		{
			d_ticks = 5U;
		}
		phase_lock = d_ticks + (d_ticks / 2U);
		if (phase_lock < 4U)
		{
			phase_lock = 4U;
		}
		if (phase_lock > 6U)
		{
			phase_lock = 6U;
		}
	}
	else if (rtt_ms < 220U)
	{
		/* Playable (150–220ms): D 4–7, prediction 6–8, delay-heavy */
		d_ticks = one_way_ticks + 2U;
		if (d_ticks < 4U)
		{
			d_ticks = 4U;
		}
		if (d_ticks > 7U)
		{
			d_ticks = 7U;
		}
		phase_lock = d_ticks + 2U;
		if (phase_lock < 6U)
		{
			phase_lock = 6U;
		}
		if (phase_lock > SYNETSESSION_PARAMS_PREDICTION_MAX)
		{
			phase_lock = SYNETSESSION_PARAMS_PREDICTION_MAX;
		}
	}
	else
	{
		/* High (>220ms): D 6–10+, prediction capped ~6–8, strongly delay-heavy */
		d_ticks = one_way_ticks + 4U;
		if (d_ticks < 6U)
		{
			d_ticks = 6U;
		}
		if (d_ticks > SYNETSESSION_PARAMS_ROLLBACK_D_MAX)
		{
			d_ticks = SYNETSESSION_PARAMS_ROLLBACK_D_MAX;
		}
		phase_lock = 8U;
	}
	pred_cap = d_ticks + (d_ticks / 2U);
	if (phase_lock > pred_cap)
	{
		phase_lock = pred_cap;
	}
	if (phase_lock > SYNETSESSION_PARAMS_PREDICTION_MAX)
	{
		phase_lock = SYNETSESSION_PARAMS_PREDICTION_MAX;
	}
	if (phase_lock < 2U)
	{
		phase_lock = 2U;
	}
	if (phase_lock > SYNETSESSION_PARAMS_PHASE_LOCK_MAX)
	{
		phase_lock = SYNETSESSION_PARAMS_PHASE_LOCK_MAX;
	}
	*out_d_ticks = d_ticks;
	*out_phase_lock = phase_lock;
}

static u32 syNetSessionParamsComputeResimTicksFromRtt(u32 rtt_ms)
{
	u32 ticks;

	if (rtt_ms < 120U)
	{
		ticks = 4U;
	}
	else if (rtt_ms < 220U)
	{
		ticks = 6U;
	}
	else
	{
		ticks = 8U;
	}
	if (ticks < SYNETSESSION_PARAMS_RESIM_TICKS_MIN)
	{
		ticks = SYNETSESSION_PARAMS_RESIM_TICKS_MIN;
	}
	if (ticks > SYNETSESSION_PARAMS_RESIM_TICKS_MAX)
	{
		ticks = SYNETSESSION_PARAMS_RESIM_TICKS_MAX;
	}
	return ticks;
}

void syNetSessionParamsComputeFromRttMs(u32 rtt_ms, SYNetSessionParams *out_params)
{
	u32 sim_hz;
	u32 frame_ms_num;
	u32 half_rtt_ms;
	u32 one_way_ticks;
	u32 d_ticks;
	u32 phase_lock;
	u32 redundancy;
	u32 pumps;
	u32 ceil_d;
	u32 fuzz_ticks;
	u8 rollback_flags;

	if (out_params == NULL)
	{
		return;
	}
	memset(out_params, 0, sizeof(*out_params));
	out_params->version = SYNETSESSION_PARAMS_WIRE_VERSION;
	out_params->rtt_ms = rtt_ms;
	sim_hz = syNetSessionParamsReadSimHz();
	frame_ms_num = 1000U / sim_hz;
	if (frame_ms_num == 0U)
	{
		frame_ms_num = 16U;
	}
	half_rtt_ms = (rtt_ms + 1U) / 2U;
	one_way_ticks = syNetSessionParamsCeilDiv(half_rtt_ms, frame_ms_num);
	/*
	 * Rollback-first: committed input delay (wire = sim + D) tracks RTT band; phase_lock is the prediction /
	 * rollback runway (not extra input delay). See delay:prediction table in ComputeDelayAndPrediction.
	 */
	syNetSessionParamsComputeDelayAndPrediction(rtt_ms, one_way_ticks, &d_ticks, &phase_lock);
	if (d_ticks < SYNETSESSION_PARAMS_DELAY_MIN)
	{
		d_ticks = SYNETSESSION_PARAMS_DELAY_MIN;
	}
	if (d_ticks > SYNETSESSION_PARAMS_ROLLBACK_D_MAX)
	{
		d_ticks = SYNETSESSION_PARAMS_ROLLBACK_D_MAX;
	}
	if (rtt_ms < 80U)
	{
		redundancy = 1U;
	}
	else if (rtt_ms < 150U)
	{
		redundancy = 2U;
	}
	else if (rtt_ms < 220U)
	{
		redundancy = 2U;
	}
	else
	{
		redundancy = 3U;
	}
	if (rtt_ms < 120U)
	{
		pumps = 1U;
	}
	else if (rtt_ms < 320U)
	{
		pumps = 2U;
	}
	else
	{
		pumps = 3U;
	}
	fuzz_ticks = 0U;
	if (rtt_ms >= 120U)
	{
		fuzz_ticks = 1U;
	}
	if (rtt_ms >= 220U)
	{
		fuzz_ticks = 2U;
	}
	rollback_flags = (u8)(SYNETSESSION_ROLLBACK_FLAG_ENABLED | SYNETSESSION_ROLLBACK_FLAG_SYMMETRIC);
	out_params->input_delay = (u8)d_ticks;
	out_params->phase_lock_ticks = (u8)phase_lock;
	out_params->bundle_redundancy = (u8)redundancy;
	out_params->ingress_extra_pumps = (u8)pumps;
	out_params->delay_adaptive_headroom = (u8)SYNETSESSION_PARAMS_ADAPTIVE_HEADROOM_DEFAULT;
	out_params->rollback_snapshot_frames = (u8)syNetSessionParamsComputeSnapshotFramesFromRtt(rtt_ms);
	out_params->rollback_resim_ticks_per_frame = (u8)syNetSessionParamsComputeResimTicksFromRtt(rtt_ms);
	out_params->strict_ring_fuzz_ticks = (u8)fuzz_ticks;
	out_params->rollback_flags = rollback_flags;
	ceil_d = d_ticks + (u32)out_params->delay_adaptive_headroom;
	if (ceil_d > SYNETSESSION_PARAMS_DELAY_MAX)
	{
		ceil_d = SYNETSESSION_PARAMS_DELAY_MAX;
	}
	sSYNetSessionParamsEffectiveDelayCeil = ceil_d;
}

sb32 syNetSessionParamsAreNegotiated(void)
{
	return sSYNetSessionParamsNegotiatedValid;
}

void syNetSessionParamsGetNegotiated(SYNetSessionParams *out_params)
{
	if (out_params == NULL)
	{
		return;
	}
	*out_params = sSYNetSessionParamsNegotiated;
}

u32 syNetSessionParamsGetNegotiatedRttMs(void)
{
	if (sSYNetSessionParamsNegotiatedValid == FALSE)
	{
		return 0U;
	}
	return sSYNetSessionParamsNegotiated.rtt_ms;
}

void syNetSessionParamsApplyNegotiated(const SYNetSessionParams *params, const char *tag)
{
	u32 d;
	u32 ceil_d;

	if (params == NULL)
	{
		return;
	}
	sSYNetSessionParamsNegotiated = *params;
	sSYNetSessionParamsNegotiated.version = SYNETSESSION_PARAMS_WIRE_VERSION;
	sSYNetSessionParamsNegotiatedValid = TRUE;
	if (params->delay_adaptive_headroom > 0U)
	{
		ceil_d = (u32)params->input_delay + (u32)params->delay_adaptive_headroom;
	}
	else
	{
		ceil_d = (u32)params->input_delay + SYNETSESSION_PARAMS_ADAPTIVE_HEADROOM_DEFAULT;
	}
	if (ceil_d > SYNETSESSION_PARAMS_DELAY_MAX)
	{
		ceil_d = SYNETSESSION_PARAMS_DELAY_MAX;
	}
	sSYNetSessionParamsEffectiveDelayCeil = ceil_d;
	d = (u32)params->input_delay;
	if (syNetSessionParamsManualDelayOverrideActive() == FALSE)
	{
		syNetPeerApplyAutoNegotiatedDelayContract(d, ceil_d, tag);
	}
	syNetPeerApplyAutoNegotiatedTransportParams((u32)params->phase_lock_ticks, (u32)params->bundle_redundancy,
	                                           (u32)params->ingress_extra_pumps, (u32)params->strict_ring_fuzz_ticks);
	{
		u32 skew_lead;

		skew_lead = (u32)params->input_delay + 1U;
		if (skew_lead > (u32)params->phase_lock_ticks)
		{
			skew_lead = (u32)params->phase_lock_ticks;
		}
		if (skew_lead < 2U)
		{
			skew_lead = 2U;
		}
		syNetPeerApplyAutoNegotiatedSkewLeadMax(skew_lead);
	}
	syNetRollbackApplySessionNegotiated(params);
	port_log(
	    "SSB64 NetSession: apply tag=%s rtt_ms=%u D=%u phase_lock=%u redundancy=%u pumps=%u fuzz=%u "
	    "rb_snap=%u rb_resim=%u rb_flags=0x%02X ceil=%u manual_D=%d\n",
	    (tag != NULL) ? tag : "?",
	    (unsigned int)params->rtt_ms,
	    (unsigned int)params->input_delay,
	    (unsigned int)params->phase_lock_ticks,
	    (unsigned int)params->bundle_redundancy,
	    (unsigned int)params->ingress_extra_pumps,
	    (unsigned int)params->strict_ring_fuzz_ticks,
	    (unsigned int)params->rollback_snapshot_frames,
	    (unsigned int)params->rollback_resim_ticks_per_frame,
	    (unsigned int)params->rollback_flags,
	    (unsigned int)ceil_d,
	    (syNetSessionParamsManualDelayOverrideActive() != FALSE) ? 1 : 0);
}

u32 syNetSessionParamsGetEffectiveInputDelay(void)
{
	if (sSYNetSessionParamsNegotiatedValid != FALSE)
	{
		return (u32)sSYNetSessionParamsNegotiated.input_delay;
	}
	return syNetPeerGetCommittedInputDelay();
}

u32 syNetSessionParamsGetEffectivePhaseLockTicks(void)
{
	if (sSYNetSessionParamsNegotiatedValid != FALSE)
	{
		return (u32)sSYNetSessionParamsNegotiated.phase_lock_ticks;
	}
	return syNetPeerGetPhaseLockPredictionWindowTicksFromEnv();
}

u32 syNetSessionParamsGetEffectiveBundleRedundancy(void)
{
	const char *e;

	if (sSYNetSessionParamsNegotiatedValid != FALSE)
	{
		return (u32)sSYNetSessionParamsNegotiated.bundle_redundancy;
	}
	e = getenv("SSB64_NETPLAY_INPUT_BUNDLE_REDUNDANCY");
	if ((e == NULL) || (e[0] == '\0'))
	{
		return 0U;
	}
	return (u32)atoi(e);
}

u32 syNetSessionParamsGetEffectiveIngressExtraPumps(void)
{
	const char *e;

	if (sSYNetSessionParamsNegotiatedValid != FALSE)
	{
		return (u32)sSYNetSessionParamsNegotiated.ingress_extra_pumps;
	}
	e = getenv("SSB64_NETPLAY_INGRESS_EXTRA_PUMPS_ON_STALL");
	if ((e == NULL) || (e[0] == '\0'))
	{
		return 0U;
	}
	return (u32)atoi(e);
}

u32 syNetSessionParamsGetEffectiveDelayCeil(void)
{
	if (sSYNetSessionParamsEffectiveDelayCeil > 0U)
	{
		return sSYNetSessionParamsEffectiveDelayCeil;
	}
	return syNetPeerGetInputDelayCeil();
}

u32 syNetSessionParamsGetEffectiveRollbackSnapshotFrames(void)
{
	if (sSYNetSessionParamsNegotiatedValid != FALSE)
	{
		return (u32)sSYNetSessionParamsNegotiated.rollback_snapshot_frames;
	}
	return syNetRbSnapshotRingCapacity();
}

u32 syNetSessionParamsGetEffectiveRollbackResimTicksPerFrame(void)
{
	if (sSYNetSessionParamsNegotiatedValid != FALSE)
	{
		return (u32)sSYNetSessionParamsNegotiated.rollback_resim_ticks_per_frame;
	}
	return 4U;
}

u32 syNetSessionParamsGetEffectiveStrictRingFuzzTicks(void)
{
	if (sSYNetSessionParamsNegotiatedValid != FALSE)
	{
		return (u32)sSYNetSessionParamsNegotiated.strict_ring_fuzz_ticks;
	}
	return 0U;
}

sb32 syNetSessionParamsRollbackEnabled(void)
{
	if (sSYNetSessionParamsNegotiatedValid == FALSE)
	{
		return TRUE;
	}
	return ((sSYNetSessionParamsNegotiated.rollback_flags & SYNETSESSION_ROLLBACK_FLAG_ENABLED) != 0U) ? TRUE : FALSE;
}

sb32 syNetSessionParamsRollbackSymmetricEnabled(void)
{
	if (sSYNetSessionParamsNegotiatedValid == FALSE)
	{
		return FALSE;
	}
	return ((sSYNetSessionParamsNegotiated.rollback_flags & SYNETSESSION_ROLLBACK_FLAG_SYMMETRIC) != 0U) ? TRUE
	                                                                                                    : FALSE;
}
