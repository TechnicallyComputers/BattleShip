/*
 * BattleShip bridge into the shared retcomm-rbengine admission scheduler.
 *
 * The authoritative admission gate (syNetPeerEvaluateSharedCommitStep) is a
 * pure function of tick arithmetic: sim vs hr vs D vs a prediction window in
 * ticks. It has no time domain — no arrival latency, no invent grace, no
 * cushion concept, no pacing debt. rbe_sched (MotK-proven, shared with the
 * RetComM recomp runtimes via retcomm-rbengine) is exactly that missing
 * layer. This bridge runs it in SHADOW against live VS admission and counts
 * where the two policies disagree; tier 2 lets rbe conservatively veto
 * prediction-window advances (stall instead of predict) once tier-1 soak
 * justifies it.
 *
 * Host-owned (stays in netpeer/netinput/netrollback, never moves here):
 * tick ownership, wire contract, ring storage, publish, rollback episodes,
 * zero-onset and per-fighter gates. The shadow respects host-policy holds
 * (E/H/B, zero-onset) instead of judging them.
 *
 * See docs/netplay_rbe_sched_integration_2026-08-21.md.
 */

#include <sys/netsched_rbe.h>

#if defined(PORT) && defined(SSB64_NETMENU)

#include <stdlib.h>
#include <string.h>

#include <recomp_net/session.h>
#include <retcomm_rbengine/rbe_log.h>
#include <retcomm_rbengine/sched.h>

#include <sys/netinput.h>
#include <sys/netpeer.h>
#include <sys/netpeer_socket_platform.h>
#include <sys/netrollback.h>
#include <sys/netsession_params.h>

extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);
extern int port_get_push_frame_count(void);

#define SYNETSCHED_RBE_MAX_SLOTS 4
#define SYNETSCHED_RBE_ARRIVAL_RING 256U
#define SYNETSCHED_RBE_SUMMARY_MS 5000U
#define SYNETSCHED_RBE_DETAIL_LOGS_MAX 64

/* ------------------------------------------------------------------ */
/* Tier / env                                                          */
/* ------------------------------------------------------------------ */

static int sTierCache = -999;

int syNetRbeSchedTier(void)
{
	const char *e;

	if (sTierCache != -999)
	{
		return sTierCache;
	}
	e = getenv("SSB64_NETPLAY_RBE_SCHED");
	sTierCache = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 0;
	if (sTierCache < 0)
	{
		sTierCache = 0;
	}
	if (sTierCache > 3)
	{
		sTierCache = 3;
	}
	if (sTierCache > 0)
	{
		port_log("SSB64 NetSchedRbe: enabled tier=%d (%s)\n", sTierCache,
		         (sTierCache >= 3)   ? "shadow; predict-veto + adaptive D inert until REAL-DELAY"
		         : (sTierCache >= 2) ? "shadow; predict-veto inert until REAL-DELAY"
		                             : "shadow only");
	}
	return sTierCache;
}

/* ------------------------------------------------------------------ */
/* Bridge state                                                        */
/* ------------------------------------------------------------------ */

static RNetSession *sNullSession; /* stays NULL — netpeer owns transport */
static int sBridgeDelay;
static int sBridgePred;
static int sBridgeSlot;
static int sBridgeRollback;
static sb32 sBound = FALSE;

/* Arrival stamps: wire -> monotonic ms, per remote slot, ring keyed by
 * wire % ring (generation checked via the stored wire value). */
static u32 sArrWire[SYNETSCHED_RBE_MAX_SLOTS][SYNETSCHED_RBE_ARRIVAL_RING];
static u32 sArrMs[SYNETSCHED_RBE_MAX_SLOTS][SYNETSCHED_RBE_ARRIVAL_RING];

/* Shadow comparison counters (one VS session; reset at bind). */
static u32 sCmpAttempts;
static u32 sCmpSkippedHostHold;
static u32 sCmpSkippedZeroOnset;
static u32 sCmpAgreeHit;
static u32 sCmpAgreeInvent;
static u32 sCmpAgreeStall;
static u32 sCmpRbeStricterConfirmed; /* row present, rbe would pace/stall */
static u32 sCmpRbeWaitOnPredict;     /* actual invented, rbe would wait — headline */
static u32 sCmpRbeInventOnHold;      /* actual R-stalled, rbe would invent */
static u32 sCmpVetoApplied;          /* tier 2 only */
static u32 sWouldDelayChanges;
static int sWouldDelayLast = -1;
static u32 sAdaptiveDelayApplied = 0U;
/* The consumption mapping the bridge installs. BattleShip is ZERO-DELAY until the
 * REAL-DELAY milestone; see docs/netplay_delay_provisioning_2026-08-29.md. */
static int sRbeRealDelayForced = 0;
static u32 sZeroOnsetHoldTick = ~(u32)0;
static u32 sLastObservedTick = ~(u32)0;
static int sLastObservedFrame = -1;
static u32 sLastAdmittedTick = ~(u32)0;
static u32 sLastSummaryMs;
static int sDetailLogsRemaining = SYNETSCHED_RBE_DETAIL_LOGS_MAX;

/* Per-reason wait counts (rbe stall reasons on predicted advances). */
#define SYNETSCHED_RBE_REASONS 8
static char sWaitReason[SYNETSCHED_RBE_REASONS][24];
static u32 sWaitReasonCount[SYNETSCHED_RBE_REASONS];

/* Decomp shim string.h lacks strncpy/strncmp — bounded local helpers. */
static void syNetRbeSchedCopyReason(char *dst, const char *src, int cap)
{
	int i;

	for (i = 0; (i < cap - 1) && (src[i] != '\0'); i++)
	{
		dst[i] = src[i];
	}
	dst[i] = '\0';
}

static int syNetRbeSchedReasonEquals(const char *a, const char *b, int cap)
{
	int i;

	for (i = 0; i < cap - 1; i++)
	{
		if (a[i] != b[i])
		{
			return 0;
		}
		if (a[i] == '\0')
		{
			return 1;
		}
	}
	return 1;
}

static void syNetRbeSchedCountWaitReason(const char *why)
{
	int i;

	if ((why == NULL) || (why[0] == '\0'))
	{
		why = "unknown";
	}
	for (i = 0; i < SYNETSCHED_RBE_REASONS; i++)
	{
		if (sWaitReason[i][0] == '\0')
		{
			syNetRbeSchedCopyReason(sWaitReason[i], why, (int)sizeof(sWaitReason[i]));
			sWaitReasonCount[i] = 1U;
			return;
		}
		if (syNetRbeSchedReasonEquals(sWaitReason[i], why, (int)sizeof(sWaitReason[i])) != 0)
		{
			sWaitReasonCount[i]++;
			return;
		}
	}
}

/* ------------------------------------------------------------------ */
/* Gates + session ops                                                 */
/* ------------------------------------------------------------------ */

static void syNetRbeSchedLogSink(void *ctx, const char *line)
{
	(void)ctx;
	port_log("%s", line);
}

static u32 syNetRbeSchedNowMs(void)
{
	return (u32)syNetPeerOsMonotonicMs();
}

static uint32_t syNetRbeSchedGateNowMs(void *ctx)
{
	(void)ctx;
	return syNetRbeSchedNowMs();
}

static uint8_t syNetRbeSchedGateEpisodeActive(void *ctx)
{
	(void)ctx;
	return (syNetRollbackIsResimulating() != FALSE) ? 1 : 0;
}

static uint32_t syNetRbeSchedGateEpisodeCount(void *ctx)
{
	(void)ctx;
	return syNetRollbackGetAppliedResimCount();
}

static int syNetRbeSchedOpsCommittedDelay(void *ctx)
{
	(void)ctx;
	return (int)syNetPeerGetCommittedInputDelay();
}

static int syNetRbeSchedOpsRequestDelayChange(void *ctx, int new_delay)
{
	(void)ctx;
	sWouldDelayChanges++;

	if (syNetRbeSchedTier() >= 3)
	{
		/*
		 * Authority. syNetPeerRequestAdaptiveInputDelay is host-only and queues through
		 * the DELAY_SYNC commit-lead protocol so both peers switch D on the same sim
		 * tick -- mandatory, because consumption is wire = sim + D and an unsynchronised
		 * change desyncs immediately. It refuses redundant/unsafe proposals itself, so
		 * the controller is free to re-propose every tick; hysteresis lives in rbe.
		 */
		sb32 queued;

		queued = (new_delay > 0) ? syNetPeerRequestAdaptiveInputDelay((u32)new_delay, "rbe_sched") : FALSE;
		if (queued != FALSE)
		{
			sAdaptiveDelayApplied++;
		}
		/*
		 * Log refusals too, on the same change-or-every-50th cadence. The first cut of
		 * this returned before the shadow logging below, so the 2026-08-29 soak recorded
		 * would_delay=1 with no line saying what was proposed or why it did not land --
		 * exactly the question the log existed to answer.
		 */
		if ((new_delay != sWouldDelayLast) || ((sWouldDelayChanges % 50U) == 0U))
		{
			port_log("SSB64 NetSchedRbe: adaptive_delay %s D=%d -> %d ceil=%u (n=%u applied=%u)\n",
			         (queued != FALSE) ? "queued" : "refused", (int)syNetPeerGetCommittedInputDelay(),
			         new_delay, (unsigned int)syNetPeerGetInputDelayCeil(),
			         (unsigned int)sWouldDelayChanges, (unsigned int)sAdaptiveDelayApplied);
			sWouldDelayLast = new_delay;
		}
		return 0;
	}

	/* Tiers 1-2: observe the controller, never move the live contract. The controller
	 * keeps re-proposing, which is the promotion signal we soaked for. */
	/*
	 * Log on a value change AND every 50th repeat. Deduping on value alone hid the
	 * volume: soak 2026-08-29 emitted a single "3 -> 4" line for 247 proposals, because
	 * every one asked for the same value, and the count only survived in the scorecard.
	 * Repetition IS the promotion signal here, so it has to be visible inline.
	 */
	if ((new_delay != sWouldDelayLast) || ((sWouldDelayChanges % 50U) == 0U))
	{
		port_log("SSB64 NetSchedRbe: would_delay_change %d -> %d (n=%u)\n",
		         (int)syNetPeerGetCommittedInputDelay(), new_delay, (unsigned int)sWouldDelayChanges);
		sWouldDelayLast = new_delay;
	}
	return 0;
}

/*
 * TRUE only when a delay proposal could actually be honoured. Tier 3 routes rbe's
 * proposals into the live contract, but rbe's arrival-driven controller refuses to run
 * under ZERO-DELAY consumption -- correctly, as the 2026-08-29 soak measured: raising D
 * when wire = sim + D moves demand and supply together, buys cushion 0.00 at every value,
 * and only adds input lag. So the path is plumbed and inert until the REAL-DELAY flip.
 */
sb32 syNetRbeSchedAdaptiveDelayActive(void)
{
	/*
	 * sRbeRealDelayForced, not rbe_sched_real_delay_enabled(): rbe defaults to REAL-DELAY
	 * and BattleShip only forces ZERO-DELAY at bind, which happens AFTER session params
	 * negotiate. Querying rbe directly therefore reported "adaptive active" during
	 * negotiation and handed out ceil=8 for a controller that would never propose --
	 * exactly the unused promise this gate exists to prevent. This static reflects the
	 * mapping the bridge actually installs, and is correct from process start.
	 */
	return ((syNetRbeSchedTier() >= 3) && (sRbeRealDelayForced != 0)) ? TRUE : FALSE;
}

static uint32_t syNetRbeSchedOpsArrivalAgeMs(void *ctx, int slot, uint32_t wire)
{
	(void)ctx;
	return syNetRbeSchedRemoteWireArrivalAgeMs((s32)slot, (u32)wire);
}

static int syNetRbeSchedOpsPeekRemoteInput(void *ctx, int slot, uint32_t tick, RNetInputSample *out)
{
	(void)ctx;
	if ((slot < 0) || (slot >= SYNETSCHED_RBE_MAX_SLOTS))
	{
		return 0;
	}
	if (syNetInputHasRemoteInputForWireTick((s32)slot, (u32)tick) == FALSE)
	{
		return 0;
	}
	if (out != NULL)
	{
		memset(out, 0, sizeof(*out));
		out->tick = tick;
		out->valid = 1;
	}
	return 1;
}

static void syNetRbeSchedFillStats(RNetSessionStats *st, u32 sim_tick)
{
	u32 hr = syNetPeerGetHighestRemoteTick();
	u32 d = syNetPeerGetCommittedInputDelay();

	memset(st, 0, sizeof(*st));
	st->sim_tick = sim_tick;
	st->delay = (rnet_u8)((d > 255U) ? 255U : d);
	st->local_slot = (rnet_u8)syNetPeerGetLocalSimSlot();
	st->slot_count = (rnet_u8)(1 + syNetPeerGetRemoteHumanSlotCount());
	st->is_running = 1;
	st->highest_remote_wire = hr;
	/* rbe convention: lead = highest_remote_wire - sim (can be negative).
	 * Under zero-delay consumption wire = sim + D, so healthy lead ≈ D —
	 * the same steady state rbe's cushion logic (§44/§56) expects. */
	st->remote_lead = (int)((s64)hr - (s64)sim_tick);
}

static int syNetRbeSchedOpsGetStats(void *ctx, RNetSessionStats *out)
{
	(void)ctx;
	syNetRbeSchedFillStats(out, syNetInputGetTick());
	return 1;
}

static void syNetRbeSchedBind(void)
{
	RbeSchedBridge br;

	memset(&br, 0, sizeof(br));
	br.session = &sNullSession;
	br.input_delay = &sBridgeDelay;
	br.input_prediction = &sBridgePred;
	br.local_slot = &sBridgeSlot;
	br.force_turn = 0;
	br.rollback = &sBridgeRollback;
	br.gates.now_ms = syNetRbeSchedGateNowMs;
	br.gates.episode_active = syNetRbeSchedGateEpisodeActive;
	br.gates.episode_count = syNetRbeSchedGateEpisodeCount;
	br.sess_ops.committed_delay = syNetRbeSchedOpsCommittedDelay;
	br.sess_ops.request_delay_change = syNetRbeSchedOpsRequestDelayChange;
	br.sess_ops.remote_arrival_age_ms = syNetRbeSchedOpsArrivalAgeMs;
	br.sess_ops.peek_remote_input = syNetRbeSchedOpsPeekRemoteInput;
	br.sess_ops.get_stats = syNetRbeSchedOpsGetStats;

	rbe_set_log_sink(syNetRbeSchedLogSink, NULL);
	/*
	 * Consumption mapping follows the negotiated session (Phase 1 flip): REAL-DELAY
	 * sessions tell the library so its shadow evaluates the correct model. This does
	 * NOT promote tiers 2/3 — sRbeRealDelayForced stays 0 until Phase 3.
	 */
	rbe_sched_set_real_delay((syNetSessionParamsRealDelayActive() != FALSE) ? 1 : 0);
	/*
	 * Phase 3 gateway (2026-09-01): under a negotiated REAL-DELAY session the
	 * tier-2 predict-veto and tier-3 adaptive-D gates unlock — waiting one frame
	 * is now productive (the needed row was sampled D ticks before it is needed
	 * and is en route). First flip soak 257428529 measured why this is required:
	 * without a pacing law the leader free-runs, spends the whole D budget, and
	 * cushion re-pins at the prediction frontier (rbe_wait_on_predict 789/328).
	 * Still env-gated: tiers only act when SSB64_NETPLAY_RBE_SCHED >= 2.
	 * ZERO-DELAY sessions keep Forced=0 — the 30 Hz veto refutation stands.
	 */
	sRbeRealDelayForced = (syNetSessionParamsRealDelayActive() != FALSE) ? 1 : 0;
	rbe_sched_bind(&br); /* also resets rbe session state */

	sBridgeDelay = (int)syNetPeerGetCommittedInputDelay();
	sBridgePred = (int)syNetPeerGetPhaseLockPredictionWindowTicks();
	sBridgeSlot = (int)syNetPeerGetLocalSimSlot();
	sBridgeRollback = (syNetSessionParamsRollbackEnabled() != FALSE) ? 1 : 0;

	sCmpAttempts = 0U;
	sCmpSkippedHostHold = 0U;
	sCmpSkippedZeroOnset = 0U;
	sCmpAgreeHit = 0U;
	sCmpAgreeInvent = 0U;
	sCmpAgreeStall = 0U;
	sCmpRbeStricterConfirmed = 0U;
	sCmpRbeWaitOnPredict = 0U;
	sCmpRbeInventOnHold = 0U;
	sCmpVetoApplied = 0U;
	sWouldDelayChanges = 0U;
	sWouldDelayLast = -1;
	sAdaptiveDelayApplied = 0U;
	sZeroOnsetHoldTick = ~(u32)0;
	sLastObservedTick = ~(u32)0;
	sLastObservedFrame = -1;
	sLastAdmittedTick = ~(u32)0;
	sLastSummaryMs = 0U;
	sDetailLogsRemaining = SYNETSCHED_RBE_DETAIL_LOGS_MAX;
	memset(sWaitReason, 0, sizeof(sWaitReason));
	memset(sWaitReasonCount, 0, sizeof(sWaitReasonCount));
	memset(sArrWire, 0, sizeof(sArrWire));
	memset(sArrMs, 0, sizeof(sArrMs));

	port_log("SSB64 NetSchedRbe: bound (shadow) D=%d P=%d slot=%d rollback=%d\n",
	         sBridgeDelay, sBridgePred, sBridgeSlot, sBridgeRollback);
}

static void syNetRbeSchedEmitSummary(const char *tag)
{
	int i;

	port_log("SSB64 NetSchedRbe: %s attempts=%u agree(hit=%u invent=%u stall=%u) "
	         "rbe_wait_on_predict=%u rbe_stricter_on_confirmed=%u rbe_invent_on_hold=%u "
	         "veto=%u would_delay=%u adaptive_d_applied=%u skipped(host_hold=%u zero_onset=%u)\n",
	         tag, (unsigned int)sCmpAttempts, (unsigned int)sCmpAgreeHit, (unsigned int)sCmpAgreeInvent,
	         (unsigned int)sCmpAgreeStall, (unsigned int)sCmpRbeWaitOnPredict,
	         (unsigned int)sCmpRbeStricterConfirmed, (unsigned int)sCmpRbeInventOnHold,
	         (unsigned int)sCmpVetoApplied, (unsigned int)sWouldDelayChanges,
	         (unsigned int)sAdaptiveDelayApplied, (unsigned int)sCmpSkippedHostHold, (unsigned int)sCmpSkippedZeroOnset);
	for (i = 0; i < SYNETSCHED_RBE_REASONS; i++)
	{
		if (sWaitReason[i][0] != '\0')
		{
			port_log("SSB64 NetSchedRbe: %s wait_reason %s=%u\n", tag, sWaitReason[i],
			         (unsigned int)sWaitReasonCount[i]);
		}
	}
}

/* ------------------------------------------------------------------ */
/* Public hooks                                                        */
/* ------------------------------------------------------------------ */

void syNetRbeSchedNoteRemoteWireArrival(s32 player, u32 wire_tick)
{
	u32 idx;

	if (syNetRbeSchedTier() <= 0)
	{
		return;
	}
	if ((player < 0) || (player >= SYNETSCHED_RBE_MAX_SLOTS))
	{
		return;
	}
	idx = wire_tick & (SYNETSCHED_RBE_ARRIVAL_RING - 1U);
	if (sArrWire[player][idx] == wire_tick)
	{
		return; /* retransmit dup — keep the first-arrival stamp */
	}
	sArrWire[player][idx] = wire_tick;
	sArrMs[player][idx] = syNetRbeSchedNowMs();
}

u32 syNetRbeSchedRemoteWireArrivalAgeMs(s32 player, u32 wire_tick)
{
	u32 idx;

	if ((player < 0) || (player >= SYNETSCHED_RBE_MAX_SLOTS))
	{
		return ~(u32)0;
	}
	idx = wire_tick & (SYNETSCHED_RBE_ARRIVAL_RING - 1U);
	if (sArrWire[player][idx] != wire_tick)
	{
		return ~(u32)0;
	}
	if ((wire_tick == 0U) && (sArrMs[player][idx] == 0U))
	{
		return ~(u32)0; /* never stamped (zeroed ring) */
	}
	return syNetRbeSchedNowMs() - sArrMs[player][idx];
}

void syNetRbeSchedNoteZeroOnsetHold(u32 sim_tick)
{
	if (syNetRbeSchedTier() <= 0)
	{
		return;
	}
	sZeroOnsetHoldTick = sim_tick;
}

void syNetRbeSchedNoteResimComplete(u32 mismatch_tick, u32 resim_target_tick)
{
	u32 depth;

	if (syNetRbeSchedTier() <= 0)
	{
		return;
	}
	if (sBound == FALSE)
	{
		return;
	}
	/* One episode = at least one mispredicted row ridden for ~depth ticks.
	 * Coarser than MotK's per-row reconcile note, but the right order of
	 * magnitude for timesync pacing debt. */
	if ((mismatch_tick != ~(u32)0) && (resim_target_tick != ~(u32)0) && (resim_target_tick > mismatch_tick))
	{
		depth = resim_target_tick - mismatch_tick;
	}
	else
	{
		depth = 1U;
	}
	rbe_sched_note_mispredict(depth);
	rbe_sched_note_episode_boundary();
}

void syNetRbeSchedNoteSessionStop(void)
{
	if (sBound == FALSE)
	{
		return;
	}
	syNetRbeSchedEmitSummary("session_end");
	rbe_sched_bind(NULL);
	sBound = FALSE;
}

void syNetRbeSchedShadowObserve(u32 sim_tick, struct SYNetPeerSharedCommitStep *shared)
{
	RNetSessionStats st;
	u32 wire;
	u32 now;
	int frame;
	int rbe_pre_stall;
	int rbe_miss_stall = 0;
	int rbe_invent = 0;
	const char *why = NULL;
	int actual_confirmed;
	int actual_predicted;
	int actual_hold;
	int tier = syNetRbeSchedTier();

	if ((tier <= 0) || (shared == NULL))
	{
		return;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		if (sBound != FALSE)
		{
			syNetRbeSchedNoteSessionStop();
		}
		return;
	}
	if (sBound == FALSE)
	{
		syNetRbeSchedBind();
		sBound = TRUE;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return; /* live admission only — resim replays are engine-internal */
	}

	/* One rbe evaluation per (tick, push frame): stall spins re-evaluate the
	 * same tick once per VI, which is the admit-attempt cadence rbe's grace
	 * timers expect; re-evaluations within one frame are dropped. */
	frame = port_get_push_frame_count();
	if ((sim_tick == sLastObservedTick) && (frame == sLastObservedFrame))
	{
		return;
	}
	sLastObservedTick = sim_tick;
	sLastObservedFrame = frame;

	sCmpAttempts++;

	/* Host-policy holds: respect, never judge. */
	if ((shared->advance == FALSE) &&
	    ((shared->hold_reason == 'E') || (shared->hold_reason == 'H') || (shared->hold_reason == 'B')))
	{
		sCmpSkippedHostHold++;
		return;
	}
	if ((shared->advance == FALSE) && (sZeroOnsetHoldTick == sim_tick))
	{
		sCmpSkippedZeroOnset++;
		return;
	}

	/* Refresh bridge mirrors before the rbe pass. */
	rbe_sched_sync_delay_from_session();
	sBridgePred = (int)syNetPeerGetPhaseLockPredictionWindowTicks();
	sBridgeSlot = (int)syNetPeerGetLocalSimSlot();
	sBridgeRollback = (syNetSessionParamsRollbackEnabled() != FALSE) ? 1 : 0;

	wire = shared->required_wire;
	syNetRbeSchedFillStats(&st, sim_tick);

	actual_confirmed = ((shared->advance != FALSE) && (shared->uses_prediction == FALSE)) ? 1 : 0;
	actual_predicted = ((shared->advance != FALSE) && (shared->uses_prediction != FALSE)) ? 1 : 0;
	actual_hold = (shared->advance == FALSE) ? 1 : 0;

	rbe_pre_stall = rbe_sched_pre_admit(sim_tick, wire, &st);
	if (rbe_pre_stall != 0)
	{
		why = rbe_sched_admit_stall_tag();
	}
	else if (actual_confirmed != 0)
	{
		rbe_sched_note_remote_hit();
	}
	else
	{
		/* Row missing at wire (actual predicted or held): ask rbe. */
		s32 slot = -1;

		if (syNetPeerGetRemoteHumanSlotByIndex(0, &slot) == FALSE)
		{
			slot = (sBridgeSlot == 0) ? 1 : 0;
		}
		rbe_miss_stall = rbe_sched_on_remote_miss((int)slot, sim_tick, wire, &st, sBridgePred, &why);
		rbe_invent = (rbe_miss_stall == 0) ? 1 : 0;
	}

	/* Compare. */
	if (actual_confirmed != 0)
	{
		if (rbe_pre_stall != 0)
		{
			sCmpRbeStricterConfirmed++;
		}
		else
		{
			sCmpAgreeHit++;
		}
	}
	else if (actual_predicted != 0)
	{
		if ((rbe_pre_stall != 0) || (rbe_miss_stall != 0))
		{
			sCmpRbeWaitOnPredict++;
			syNetRbeSchedCountWaitReason(why);
			if (sDetailLogsRemaining > 0)
			{
				sDetailLogsRemaining--;
				port_log("SSB64 NetSchedRbe: DIVERGE predict-vs-wait tick=%u wire=%u hr=%u lead=%d "
				         "D=%d P=%d why=%s\n",
				         (unsigned int)sim_tick, (unsigned int)wire,
				         (unsigned int)st.highest_remote_wire, st.remote_lead, sBridgeDelay,
				         sBridgePred, (why != NULL) ? why : "?");
			}
		}
		else
		{
			sCmpAgreeInvent++;
		}
	}
	else if (actual_hold != 0)
	{
		if ((rbe_pre_stall != 0) || (rbe_miss_stall != 0))
		{
			sCmpAgreeStall++;
		}
		else
		{
			sCmpRbeInventOnHold++;
		}
	}

	/*
	 * Tier 2: conservative veto — convert this prediction advance into an R-hold.
	 *
	 * REAL-DELAY only, and that gate is the whole point. rbe's stall verdicts assume a
	 * consumption mapping where an arrival cushion exists, so a stall means "the row is
	 * genuinely late, waiting is cheap". Under ZERO-DELAY (wire = sim + D) tick T demands
	 * the newest row the peer has produced, so gap=1 is the STEADY STATE rather than an
	 * anomaly, rbe's gap1 micro-grace fires on almost every tick, and vetoing each one
	 * drops a live sim advance.
	 *
	 * Measured, soak 2026-08-29: 645 vetoes against 1552 admits (42%), of which
	 * gap1_grace was 592. Sim ticks 926 against 1579 pushes -- the 653 lost advances are
	 * the vetoes almost exactly -- with tick_ema=33 ms, i.e. the sim ran at 30 Hz under a
	 * ~120 Hz render loop. That is the "very slow" the player reported, and it is a
	 * scheduler policy mismatch, not a frame-time problem.
	 *
	 * Same root cause as the auto-D revert: rbe's policy is calibrated for REAL-DELAY.
	 * See docs/netplay_delay_provisioning_2026-08-29.md.
	 */
	if ((tier >= 2) && (sRbeRealDelayForced != 0) && (actual_predicted != 0) &&
	    ((rbe_pre_stall != 0) || (rbe_miss_stall != 0)))
	{
		shared->advance = FALSE;
		shared->uses_prediction = FALSE;
		shared->hold_reason = 'R';
		sCmpVetoApplied++;
		if (sDetailLogsRemaining > 0)
		{
			port_log("SSB64 NetSchedRbe: VETO predict->hold tick=%u why=%s (n=%u)\n",
			         (unsigned int)sim_tick, (why != NULL) ? why : "?",
			         (unsigned int)sCmpVetoApplied);
		}
	}

	/* Feed post-admit only for ticks that actually advance (post-veto). */
	if ((shared->advance != FALSE) && (sim_tick != sLastAdmittedTick))
	{
		sLastAdmittedTick = sim_tick;
		rbe_sched_post_admit((shared->uses_prediction != FALSE) ? 1 : 0);
	}

	now = syNetRbeSchedNowMs();
	if ((sLastSummaryMs == 0U) || ((u32)(now - sLastSummaryMs) >= SYNETSCHED_RBE_SUMMARY_MS))
	{
		sLastSummaryMs = (now != 0U) ? now : 1U;
		syNetRbeSchedEmitSummary("scorecard");
	}
}

#endif /* PORT && SSB64_NETMENU */
