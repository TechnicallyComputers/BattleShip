#include <sys/netinput.h>

/*
 * NetInput implementation: ring buffers keyed by `tick % SYNETINPUT_HISTORY_LENGTH`.
 * `syNetInputResolveFrame` chooses source; `syNetInputPublishFrame` materializes edge-detected taps into `gSYControllerDevices`.
 * On PORT, `syNetInputFuncRead` snapshots hardware into `sSYNetInputHardwareLatch` and clears `gSYControllerDevices` before
 * resolve/publish so sim globals are never read for gameplay between raw HID and publish. At most one HID snapshot is taken per
 * `sSYNetInputTick` (taskman may call FuncRead several times while skew pacing holds sim without advancing the tick).
 *
 * GGPO-shaped contract: live VS feeds resolve from raw HID (add_local_input) then publishes — battle must use published frames.
 * During rollback resim, local HID is ignored; `syNetInputMakeLocalFrame` replays from published history for that tick (like
 * ggpo_synchronize_inputs replacing local samples during rewind).
 */

#include <sys/controller.h>
#include <sys/netpeer.h>
#include <sys/netrollback.h>
#include <sys/taskman.h>

#if defined(PORT) && !defined(_WIN32)
#include <sys/netdesyncclassifier.h>
#endif

#ifdef PORT
#include <string.h>
extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);
static sb32 sSYNetInputPredictNeutral;
static SYController sSYNetInputHardwareLatch[MAXCONTROLLERS];
/* Sim-tick the latch was last filled for; 0xFFFFFFFFU => next FuncRead must sample HID. */
static u32 sSYNetInputPortHwLatchTick = 0xFFFFFFFFU;
static sb32 sSYNetInputSuppressSceneUpdateAfterRead;

sb32 syNetInputTakeSuppressSceneUpdate(void)
{
	sb32 skip;

	skip = sSYNetInputSuppressSceneUpdateAfterRead;
	sSYNetInputSuppressSceneUpdateAfterRead = FALSE;
	return skip;
}

static void syNetInputNeutralizeAllControllerDevices(void)
{
	s32 i;

	for (i = 0; i < MAXCONTROLLERS; i++)
	{
		gSYControllerDevices[i].button_hold = 0;
		gSYControllerDevices[i].button_tap = 0;
		gSYControllerDevices[i].button_update = 0;
		gSYControllerDevices[i].button_release = 0;
		gSYControllerDevices[i].stick_range.x = 0;
		gSYControllerDevices[i].stick_range.y = 0;
	}
}
#endif

typedef struct SYNetInputSlot
{
	SYNetInputSource source;
	SYNetInputFrame last_confirmed;
	SYNetInputFrame last_published;

} SYNetInputSlot;

SYNetInputSlot sSYNetInputSlots[MAXCONTROLLERS];
SYNetInputFrame sSYNetInputHistory[MAXCONTROLLERS][SYNETINPUT_HISTORY_LENGTH];
SYNetInputFrame sSYNetInputRemoteHistory[MAXCONTROLLERS][SYNETINPUT_HISTORY_LENGTH];
SYNetInputFrame sSYNetInputSavedHistory[MAXCONTROLLERS][SYNETINPUT_HISTORY_LENGTH];
SYNetInputFrame sSYNetInputReplayFrames[MAXCONTROLLERS][SYNETINPUT_REPLAY_MAX_FRAMES];
SYNetInputReplayMetadata sSYNetInputReplayMetadata;
u32 sSYNetInputTick;
#ifdef PORT
/* Completed VS battle scene-update steps since session reset — advances with netinput tick after each full battle update. */
static u32 sSYNetGgpoBattleFrame;
#endif
u32 sSYNetInputRecordedFrameCount;
sb32 sSYNetInputIsRecording;
sb32 sSYNetInputIsReplayMetadataValid;

u32 syNetInputGetTick(void)
{
	return sSYNetInputTick;
}

void syNetInputSetTick(u32 tick)
{
	sSYNetInputTick = tick;
#ifdef PORT
	sSYNetGgpoBattleFrame = tick;
	/* Rollback resim and other explicit rewinds: do not treat the next FuncRead as a repeat of the prior sim tick. */
	sSYNetInputPortHwLatchTick = 0xFFFFFFFFU;
#endif
}

#ifdef PORT
u32 syNetGgpoBattleFrameGet(void)
{
	return sSYNetGgpoBattleFrame;
}

void syNetGgpoBattleFrameAdvance(void)
{
	/* Deprecated: `sSYNetGgpoBattleFrame` advances in `syNetInputAdvanceAuthoritativeSimTick` with `sSYNetInputTick`. */
}

void syNetGgpoBattleFrameSet(u32 frame)
{
	syNetInputSetTick(frame);
}

#endif

void syNetInputAdvanceAuthoritativeSimTick(void)
{
	sSYNetInputTick++;
#ifdef PORT
	sSYNetGgpoBattleFrame++;
#endif
}

#if defined(PORT) && !defined(_WIN32)
static u64 sSYNetInputAdmissionP;
static u64 sSYNetInputAdmissionE;
static u64 sSYNetInputAdmissionS;
static u64 sSYNetInputAdmissionK;
static u64 sSYNetInputAdmissionR;
static int sSYNetInputStrictContractEnvCache = -1;
static sb32 sSYNetInputStrictContractSkippedPublish;
static u32 sSYNetInputStrictRStuckSimTick = ~(u32)0;
static u32 sSYNetInputStrictRStuckFrames;
static int sSYNetInputAdmissionSummaryIvCache = -999;
static int sSYNetInputStallUntilRemoteEnvCache = -1;
static int sSYNetInputPredictDiagLevelCache = -999;
static int sSYNetInputFrameCommitDiagLevelCache = -999;
static int sSYNetInputDelaySyncDiagLevelCache = -999;
static u32 sSYNetInputDelaySyncDiagLastCommittedD = ~(u32)0;

int g_NetInputDelayFrames = 0;
sb32 g_UseInputPrediction = TRUE;

int syNetInputEnvGetMatchInputDelayOrNeg1(void)
{
	const char *e;
	int v;

	e = getenv("SSB64_NETPLAY_MATCH_INPUT_DELAY");
	if ((e == NULL) || (e[0] == '\0'))
	{
		return -1;
	}
	v = atoi(e);
	if (v < 0)
	{
		v = 0;
	}
	/* Align with `syNetPeerConfigureUdpForAutomatch`: values above 99 are treated as "use default" for u32 arg. */
	if (v > 99)
	{
		v = 99;
	}
	return v;
}

static void syNetInputLoadExecutionDelayAndPredictionFromEnv(void)
{
	char *e;
	int v;
	int md;

	g_NetInputDelayFrames = 0;
	md = syNetInputEnvGetMatchInputDelayOrNeg1();
	if (md >= 0)
	{
		v = md;
		if (v > 4)
		{
			v = 4;
		}
		g_NetInputDelayFrames = v;
	}
	else
	{
		e = getenv("SSB64_NET_DELAY_FRAMES");
		if ((e == NULL) || (e[0] == '\0'))
		{
			e = getenv("SSB64_NETPLAY_INPUT_EXEC_DELAY_FRAMES");
		}
		if ((e != NULL) && (e[0] != '\0'))
		{
			v = atoi(e);
			if (v < 0)
			{
				v = 0;
			}
			if (v > 4)
			{
				v = 4;
			}
			g_NetInputDelayFrames = v;
		}
	}
	g_UseInputPrediction = TRUE;
	e = getenv("SSB64_NETPLAY_INPUT_PREDICTION");
	if ((e != NULL) && (e[0] != '\0') && (atoi(e) == 0))
	{
		g_UseInputPrediction = FALSE;
	}
}

int syNetInputGetExecutionDelayFrames(void)
{
	int d;

	d = g_NetInputDelayFrames;
	if (d < 0)
	{
		return 0;
	}
	if (d > 4)
	{
		return 4;
	}
	return d;
}

sb32 syNetInputGetUseInputPrediction(void)
{
	return g_UseInputPrediction;
}

/** Sim tick used to probe remote ring for strict-contract path R (readiness through history for tick minus execution delay). */
static u32 syNetInputStrictContractRemoteProbeSimTick(u32 tick)
{
	int d;

	d = syNetInputGetExecutionDelayFrames();
	if (d == 0)
	{
		return tick;
	}
	{
		u32 du;

		du = (u32)d;
		return (tick > du) ? (tick - du) : 0U;
	}
}

static void syNetInputResetAdmissionStatsInternal(void)
{
	sSYNetInputAdmissionP = 0ULL;
	sSYNetInputAdmissionE = 0ULL;
	sSYNetInputAdmissionS = 0ULL;
	sSYNetInputAdmissionK = 0ULL;
	sSYNetInputAdmissionR = 0ULL;
}

static void syNetInputAdmissionBump(char path)
{
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	switch (path)
	{
	case 'P':
		sSYNetInputAdmissionP++;
		break;
	case 'E':
		sSYNetInputAdmissionE++;
		break;
	case 'S':
		sSYNetInputAdmissionS++;
		break;
	case 'K':
		sSYNetInputAdmissionK++;
		break;
	case 'R':
		sSYNetInputAdmissionR++;
		break;
	default:
		break;
	}
	syNetDesyncClassifierOnAdmissionPath(syNetInputGetTick(), path);
}

void syNetInputLogAdmissionStatsSummary(const char *tag, sb32 reset_counts_after)
{
	u64 tot;
	double dtot;

	tot = sSYNetInputAdmissionP + sSYNetInputAdmissionE + sSYNetInputAdmissionS + sSYNetInputAdmissionK + sSYNetInputAdmissionR;
	if (tot == 0ULL)
	{
		if (reset_counts_after != FALSE)
		{
			syNetInputResetAdmissionStatsInternal();
		}
		return;
	}
	dtot = (double)tot;
	port_log(
	    "SSB64 NetInput: admission_summary tag=%s P=%llu E=%llu S=%llu K=%llu R=%llu total=%llu pct_P=%.2f pct_E=%.2f pct_S=%.2f pct_K=%.2f pct_R=%.2f\n",
	    (tag != NULL) ? tag : "?",
	    (unsigned long long)sSYNetInputAdmissionP,
	    (unsigned long long)sSYNetInputAdmissionE,
	    (unsigned long long)sSYNetInputAdmissionS,
	    (unsigned long long)sSYNetInputAdmissionK,
	    (unsigned long long)sSYNetInputAdmissionR,
	    (unsigned long long)tot,
	    100.0 * (double)sSYNetInputAdmissionP / dtot,
	    100.0 * (double)sSYNetInputAdmissionE / dtot,
	    100.0 * (double)sSYNetInputAdmissionS / dtot,
	    100.0 * (double)sSYNetInputAdmissionK / dtot,
	    100.0 * (double)sSYNetInputAdmissionR / dtot);
	if (reset_counts_after != FALSE)
	{
		syNetInputResetAdmissionStatsInternal();
	}
}

static void syNetInputMaybeAdmissionPeriodicSummary(u32 tick)
{
	char *e;
	int iv;

	if (sSYNetInputAdmissionSummaryIvCache == -999)
	{
		e = getenv("SSB64_NETPLAY_FRAME_COMMIT_SUMMARY");
		sSYNetInputAdmissionSummaryIvCache = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 0;
		if (sSYNetInputAdmissionSummaryIvCache < 0)
		{
			sSYNetInputAdmissionSummaryIvCache = 0;
		}
	}
	iv = sSYNetInputAdmissionSummaryIvCache;
	if (iv <= 0)
	{
		return;
	}
	if ((tick % (u32)iv) != 0U)
	{
		return;
	}
	syNetInputLogAdmissionStatsSummary("periodic", FALSE);
}

sb32 syNetInputStrictInputContractEnabled(void)
{
	char *e;

	if (sSYNetInputStrictContractEnvCache < 0)
	{
		e = getenv("SSB64_NETPLAY_STRICT_INPUT_CONTRACT");
		sSYNetInputStrictContractEnvCache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	}
	return (sSYNetInputStrictContractEnvCache != 0) ? TRUE : FALSE;
}

sb32 syNetInputStrictContractSkippedPublishThisPass(void)
{
	return sSYNetInputStrictContractSkippedPublish;
}

void syNetInputRefreshCachedNetplayEnvForNewMatch(void)
{
	sSYNetInputAdmissionSummaryIvCache = -999;
	sSYNetInputStallUntilRemoteEnvCache = -1;
	sSYNetInputPredictDiagLevelCache = -999;
	sSYNetInputFrameCommitDiagLevelCache = -999;
	sSYNetInputDelaySyncDiagLevelCache = -999;
	sSYNetInputDelaySyncDiagLastCommittedD = ~(u32)0;
}
#endif

sb32 syNetInputCheckPlayer(s32 player)
{
	return ((player >= 0) && (player < MAXCONTROLLERS)) ? TRUE : FALSE;
}

void syNetInputClearFrame(SYNetInputFrame *frame)
{
	frame->tick = 0;
	frame->buttons = 0;
	frame->stick_x = 0;
	frame->stick_y = 0;
	frame->source = nSYNetInputSourceLocal;
	frame->is_predicted = FALSE;
	frame->is_valid = FALSE;
}

void syNetInputMakeFrame(SYNetInputFrame *frame, u32 tick, u16 buttons, s8 stick_x, s8 stick_y, SYNetInputSource source, sb32 is_predicted)
{
	frame->tick = tick;
	frame->buttons = buttons;
	frame->stick_x = stick_x;
	frame->stick_y = stick_y;
	frame->source = source;
	frame->is_predicted = is_predicted;
	frame->is_valid = TRUE;
}

void syNetInputStoreFrame(SYNetInputFrame history[][SYNETINPUT_HISTORY_LENGTH], s32 player, SYNetInputFrame *frame)
{
	history[player][frame->tick % SYNETINPUT_HISTORY_LENGTH] = *frame;
}

sb32 syNetInputGetStoredFrame(SYNetInputFrame history[][SYNETINPUT_HISTORY_LENGTH], s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	SYNetInputFrame *frame;

	if (syNetInputCheckPlayer(player) == FALSE)
	{
		return FALSE;
	}
	frame = &history[player][tick % SYNETINPUT_HISTORY_LENGTH];

	if ((frame->is_valid == FALSE) || (frame->tick != tick))
	{
		return FALSE;
	}
	if (out_frame != NULL)
	{
		*out_frame = *frame;
	}
	return TRUE;
}

void syNetInputReset(void)
{
	s32 player;
	s32 i;

	sSYNetInputTick = 0;
#ifdef PORT
	sSYNetGgpoBattleFrame = 0;
#endif
	sSYNetInputRecordedFrameCount = 0;
	sSYNetInputIsRecording = FALSE;
	sSYNetInputIsReplayMetadataValid = FALSE;
#ifdef PORT
	sSYNetInputPortHwLatchTick = 0xFFFFFFFFU;
#endif
#if defined(PORT) && !defined(_WIN32)
	syNetInputResetAdmissionStatsInternal();
	sSYNetInputStrictContractEnvCache = -1;
	sSYNetInputStrictContractSkippedPublish = FALSE;
#endif

	sSYNetInputReplayMetadata.magic = SYNETINPUT_REPLAY_MAGIC;
	sSYNetInputReplayMetadata.version = SYNETINPUT_REPLAY_VERSION;
	sSYNetInputReplayMetadata.scene_kind = 0;
	sSYNetInputReplayMetadata.player_count = 0;
	sSYNetInputReplayMetadata.stage_kind = 0;
	sSYNetInputReplayMetadata.stocks = 0;
	sSYNetInputReplayMetadata.time_limit = 0;
	sSYNetInputReplayMetadata.item_switch = 0;
	sSYNetInputReplayMetadata.item_toggles = 0;
	sSYNetInputReplayMetadata.rng_seed = 0;
	sSYNetInputReplayMetadata.game_type = 0;
	sSYNetInputReplayMetadata.game_rules = 0;
	sSYNetInputReplayMetadata.is_team_battle = FALSE;
	sSYNetInputReplayMetadata.handicap = 0;
	sSYNetInputReplayMetadata.is_team_attack = FALSE;
	sSYNetInputReplayMetadata.is_stage_select = FALSE;
	sSYNetInputReplayMetadata.damage_ratio = 0;
	sSYNetInputReplayMetadata.item_appearance_rate = 0;
	sSYNetInputReplayMetadata.is_not_teamshadows = FALSE;

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		sSYNetInputSlots[player].source = nSYNetInputSourceLocal;
		syNetInputClearFrame(&sSYNetInputSlots[player].last_confirmed);
		syNetInputClearFrame(&sSYNetInputSlots[player].last_published);
		sSYNetInputReplayMetadata.player_kinds[player] = 0;
		sSYNetInputReplayMetadata.fighter_kinds[player] = 0;
		sSYNetInputReplayMetadata.costumes[player] = 0;
		sSYNetInputReplayMetadata.teams[player] = 0;
		sSYNetInputReplayMetadata.handicaps[player] = 0;
		sSYNetInputReplayMetadata.levels[player] = 0;
		sSYNetInputReplayMetadata.shades[player] = 0;

		for (i = 0; i < SYNETINPUT_HISTORY_LENGTH; i++)
		{
			syNetInputClearFrame(&sSYNetInputHistory[player][i]);
			syNetInputClearFrame(&sSYNetInputRemoteHistory[player][i]);
			syNetInputClearFrame(&sSYNetInputSavedHistory[player][i]);
		}
		for (i = 0; i < SYNETINPUT_REPLAY_MAX_FRAMES; i++)
		{
			syNetInputClearFrame(&sSYNetInputReplayFrames[player][i]);
		}
	}
	sSYNetInputReplayMetadata.netplay_sim_slot_host_hw = 0U;
	sSYNetInputReplayMetadata.netplay_sim_slot_client_hw = 1U;
}

void syNetInputStartVSSession(void)
{
	syNetInputReset();
#if defined(PORT) && !defined(_WIN32)
	syNetInputRefreshCachedNetplayEnvForNewMatch();
#endif
#ifdef PORT
	{
		char *env_pn;

		env_pn = getenv("SSB64_NETPLAY_PREDICT_NEUTRAL");
		sSYNetInputPredictNeutral = ((env_pn != NULL) && (atoi(env_pn) != 0)) ? TRUE : FALSE;
	}
#if !defined(_WIN32)
	syNetInputLoadExecutionDelayAndPredictionFromEnv();
#endif
#endif
}

void syNetInputSetSlotSource(s32 player, SYNetInputSource source)
{
	if (syNetInputCheckPlayer(player) != FALSE)
	{
		sSYNetInputSlots[player].source = source;
	}
}

SYNetInputSource syNetInputGetSlotSource(s32 player)
{
	if (syNetInputCheckPlayer(player) == FALSE)
	{
		return nSYNetInputSourceLocal;
	}
	return sSYNetInputSlots[player].source;
}

void syNetInputSetRemoteInput(s32 player, u32 tick, u16 buttons, s8 stick_x, s8 stick_y)
{
	SYNetInputFrame frame;

	if (syNetInputCheckPlayer(player) != FALSE)
	{
		syNetInputMakeFrame(&frame, tick, buttons, stick_x, stick_y, nSYNetInputSourceRemoteConfirmed, FALSE);
		syNetInputStoreFrame(sSYNetInputRemoteHistory, player, &frame);
	}
}

void syNetInputSetSavedInput(s32 player, u32 tick, u16 buttons, s8 stick_x, s8 stick_y)
{
	SYNetInputFrame frame;

	if (syNetInputCheckPlayer(player) != FALSE)
	{
		syNetInputMakeFrame(&frame, tick, buttons, stick_x, stick_y, nSYNetInputSourceSaved, FALSE);
		syNetInputStoreFrame(sSYNetInputSavedHistory, player, &frame);
	}
}

void syNetInputMakeLocalFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	s32 hw_player = player;
#ifdef PORT
	SYController *controller;

	if (syNetPeerIsOnlineP2PHardwareDecoupleActive() != FALSE)
	{
		if (player != syNetPeerGetLocalSimSlot())
		{
			syNetInputMakeFrame(out_frame, tick, 0, 0, 0, nSYNetInputSourceLocal, FALSE);
			return;
		}
	}
	/* Rollback resim: deterministic replay — same inputs as live run for this tick (GGPO synchronize_inputs semantics). */
	if (syNetRollbackIsResimulating() != FALSE)
	{
		SYNetInputFrame hist;

		if ((syNetInputGetHistoryFrame(player, tick, &hist) != FALSE) && (hist.tick == tick))
		{
			*out_frame = hist;
			out_frame->source = nSYNetInputSourceLocal;
			out_frame->is_predicted = FALSE;
			return;
		}
		syNetInputMakeFrame(out_frame, tick, 0, 0, 0, nSYNetInputSourceLocal, FALSE);
		return;
	}
	hw_player = syNetPeerResolveLocalHardwareDevice(player);
	controller = &sSYNetInputHardwareLatch[hw_player];
	syNetInputMakeFrame(out_frame, tick, controller->button_hold, controller->stick_range.x, controller->stick_range.y,
	                   nSYNetInputSourceLocal, FALSE);
#else
	{
		SYController *controller = &gSYControllerDevices[player];

		syNetInputMakeFrame(out_frame, tick, controller->button_hold, controller->stick_range.x, controller->stick_range.y,
		                   nSYNetInputSourceLocal, FALSE);
	}
#endif
#ifdef PORT
	{
		const char *log_env;

		log_env = getenv("SSB64_NETPLAY_LOG_LOCAL_INPUT");
		if ((log_env != NULL) && (log_env[0] != '\0') && (atoi(log_env) != 0) && (syNetPeerIsVSSessionActive() != FALSE) &&
		    (player == syNetPeerGetLocalSimSlot()) && (syNetPeerGetLocalSimSlot() != 0))
		{
			if ((tick % 128U) == 0U)
			{
				port_log(
				    "SSB64 NetInput (net guest): after_local_hw_map sim=%d sampled_hw=%d tick=%u -> frame "
				    "buttons=0x%04x stick=(%d,%d) (syNetInputPublishFrame writes this to gSYControllerDevices[sim])\n",
				    (int)player, (int)hw_player, (unsigned)tick, (unsigned int)out_frame->buttons,
				    (int)out_frame->stick_x, (int)out_frame->stick_y);
			}
		}
	}
#endif
}

void syNetInputMakePredictedFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	SYNetInputFrame *last_confirmed = &sSYNetInputSlots[player].last_confirmed;

#ifdef PORT
	if (sSYNetInputPredictNeutral != FALSE)
	{
		syNetInputMakeFrame(out_frame, tick, 0, 0, 0, nSYNetInputSourceRemotePredicted, TRUE);
		return;
	}
#endif
	if (last_confirmed->is_valid != FALSE)
	{
		syNetInputMakeFrame
		(
			out_frame,
			tick,
			last_confirmed->buttons,
			last_confirmed->stick_x,
			last_confirmed->stick_y,
			nSYNetInputSourceRemotePredicted,
			TRUE
		);
	}
	else syNetInputMakeFrame(out_frame, tick, 0, 0, 0, nSYNetInputSourceRemotePredicted, TRUE);
}

/*
 * Remote ingress stores `SYNetInputFrame.tick` as the **wire** label (`sim + committed_input_delay`, see
 * `syNetPeerGatherHistoryBundle`). Published history and rollback use **sim** tick as the frame key; map sim→wire for ring
 * lookup, then normalize `out_frame->tick` to `sim_tick` on success.
 */
#if defined(PORT)
static u32 syNetInputRemoteHistoryWireLookupTick(u32 sim_tick)
{
	u32 ring_tick;
	u32 d;

	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return sim_tick;
	}
	return syNetPeerDelayWireTickFromSim(sim_tick);
}
#else
static u32 syNetInputRemoteHistoryWireLookupTick(u32 sim_tick)
{
	return sim_tick;
}
#endif

static sb32 syNetInputTryGetRemoteHistoryForSimTick(s32 player, u32 sim_tick, SYNetInputFrame *out_frame)
{
	u32 wire_tick;
	sb32 ok;

	wire_tick = syNetInputRemoteHistoryWireLookupTick(sim_tick);
	ok = syNetInputGetStoredFrame(sSYNetInputRemoteHistory, player, wire_tick, out_frame);
	if ((ok != FALSE) && (out_frame != NULL))
	{
		out_frame->tick = sim_tick;
	}
	return ok;
}

#if defined(PORT) && !defined(_WIN32)
/*
 * Inject strict-contract predicted remote into the wire-keyed ring (`store.tick` is the wire index).
 * Fills gaps so `syNetInputRemoteSlotsMissingRingFrameForTick` and validation see a stored cell for this sim step.
 */
static void syNetInputStoreRemotePredictedWireFromSimTick(s32 player, u32 sim_tick, SYNetInputFrame *frame)
{
	SYNetInputFrame store;
	u32 wt;

	if (syNetInputCheckPlayer(player) == FALSE)
	{
		return;
	}
	store = *frame;
	wt = syNetInputRemoteHistoryWireLookupTick(sim_tick);
	store.tick = wt;
	syNetInputStoreFrame(sSYNetInputRemoteHistory, player, &store);
}
#endif

/* Build one logical frame for `player` at `tick` without touching globals (feeds `syNetInputPublishFrame`). */
void syNetInputResolveFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	switch (sSYNetInputSlots[player].source)
	{
	case nSYNetInputSourceRemoteConfirmed:
	case nSYNetInputSourceRemotePredicted:
		if (syNetInputTryGetRemoteHistoryForSimTick(player, tick, out_frame) != FALSE)
		{
			sSYNetInputSlots[player].last_confirmed = *out_frame;
		}
#if defined(PORT) && !defined(_WIN32)
		else if (syNetInputStrictInputContractEnabled() != FALSE)
		{
			if (g_UseInputPrediction != FALSE)
			{
				syNetInputMakePredictedFrame(player, tick, out_frame);
				syNetInputStoreRemotePredictedWireFromSimTick(player, tick, out_frame);
			}
			else
			{
				syNetInputClearFrame(out_frame);
			}
		}
#endif
		else syNetInputMakePredictedFrame(player, tick, out_frame);
		break;

	case nSYNetInputSourceSaved:
		if (syNetInputGetReplayFrame(player, tick, out_frame) != FALSE)
		{
			out_frame->source = nSYNetInputSourceSaved;
			out_frame->is_predicted = FALSE;
		}
		else if (syNetInputGetStoredFrame(sSYNetInputSavedHistory, player, tick, out_frame) == FALSE)
		{
			syNetInputMakeFrame(out_frame, tick, 0, 0, 0, nSYNetInputSourceSaved, FALSE);
		}
		break;

	case nSYNetInputSourceLocal:
	default:
		syNetInputMakeLocalFrame(player, tick, out_frame);
		break;
	}
}

/* Write resolved frame into sim-facing `gSYControllerDevices` + published history ring (`sSYNetInputHistory`). */
void syNetInputPublishFrame(s32 player, SYNetInputFrame *frame)
{
	SYNetInputFrame *last_published = &sSYNetInputSlots[player].last_published;
	u16 prev_buttons = (last_published->is_valid != FALSE) ? last_published->buttons : 0;
	u16 pressed = (frame->buttons ^ prev_buttons) & frame->buttons;
	u16 released = (frame->buttons ^ prev_buttons) & prev_buttons;

	gSYControllerDevices[player].button_hold = frame->buttons;
	gSYControllerDevices[player].button_tap = pressed;
	gSYControllerDevices[player].button_release = released;
	gSYControllerDevices[player].button_update = pressed;
	gSYControllerDevices[player].stick_range.x = frame->stick_x;
	gSYControllerDevices[player].stick_range.y = frame->stick_y;

	sSYNetInputSlots[player].last_published = *frame;
	syNetInputStoreFrame(sSYNetInputHistory, player, frame);
}

void syNetInputPublishMainController(void)
{
	s32 main_slot = 0;
#ifdef PORT
	if (syNetPeerIsOnlineP2PHardwareDecoupleActive() != FALSE)
	{
		main_slot = syNetPeerGetLocalSimSlot();
		if ((main_slot < 0) || (main_slot >= MAXCONTROLLERS))
		{
			main_slot = 0;
		}
	}
#endif
	gSYControllerMain.button_hold = gSYControllerDevices[main_slot].button_hold;
	gSYControllerMain.button_tap = gSYControllerDevices[main_slot].button_tap;
	gSYControllerMain.button_update = gSYControllerDevices[main_slot].button_update;
	gSYControllerMain.button_release = gSYControllerDevices[main_slot].button_release;
	gSYControllerMain.stick_range.x = gSYControllerDevices[main_slot].stick_range.x;
	gSYControllerMain.stick_range.y = gSYControllerDevices[main_slot].stick_range.y;
}

sb32 syNetInputGetHistoryFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	return syNetInputGetStoredFrame(sSYNetInputHistory, player, tick, out_frame);
}

sb32 syNetInputGetPublishedFrame(s32 player, SYNetInputFrame *out_frame)
{
	if (syNetInputCheckPlayer(player) == FALSE)
	{
		return FALSE;
	}
	if (sSYNetInputSlots[player].last_published.is_valid == FALSE)
	{
		return FALSE;
	}
	if (out_frame != NULL)
	{
		*out_frame = sSYNetInputSlots[player].last_published;
	}
	return TRUE;
}

u32 syNetInputGetHistoryChecksum(s32 player, u32 tick_begin, u32 frame_count)
{
	SYNetInputFrame frame;
	u32 checksum = 2166136261U;
	u32 i;

	for (i = 0; i < frame_count; i++)
	{
		if (syNetInputGetHistoryFrame(player, tick_begin + i, &frame) != FALSE)
		{
			checksum ^= frame.tick;
			checksum *= 16777619U;
			checksum ^= frame.buttons;
			checksum *= 16777619U;
			checksum ^= (u8)frame.stick_x;
			checksum *= 16777619U;
			checksum ^= (u8)frame.stick_y;
			checksum *= 16777619U;
			checksum ^= frame.source;
			checksum *= 16777619U;
			checksum ^= frame.is_predicted;
			checksum *= 16777619U;
		}
	}
	return checksum;
}

u32 syNetInputAccumulateInputChecksum(u32 checksum, s32 player, SYNetInputFrame *frame)
{
	checksum ^= (u32)player;
	checksum *= 16777619U;
	checksum ^= frame->tick;
	checksum *= 16777619U;
	checksum ^= frame->buttons;
	checksum *= 16777619U;
	checksum ^= (u8)frame->stick_x;
	checksum *= 16777619U;
	checksum ^= (u8)frame->stick_y;
	checksum *= 16777619U;

	return checksum;
}

#ifdef PORT
static u32 syNetInputAccumulateInputChecksumDiag(u32 checksum, s32 player, SYNetInputFrame *frame)
{
	checksum = syNetInputAccumulateInputChecksum(checksum, player, frame);
	checksum ^= (u32)frame->source;
	checksum *= 16777619U;
	checksum ^= (u32)frame->is_predicted;
	checksum *= 16777619U;
	checksum ^= (u32)frame->is_valid;
	checksum *= 16777619U;

	return checksum;
}
#endif

u32 syNetInputGetHistoryInputChecksum(u32 frame_count)
{
	SYNetInputFrame frame;
	u32 checksum = 2166136261U;
	u32 tick;
	s32 player;

	for (tick = 0; tick < frame_count; tick++)
	{
		for (player = 0; player < MAXCONTROLLERS; player++)
		{
			if (syNetInputGetHistoryFrame(player, tick, &frame) != FALSE)
			{
				checksum = syNetInputAccumulateInputChecksum(checksum, player, &frame);
			}
		}
	}
	return checksum;
}

u32 syNetInputGetHistoryInputValueChecksumForPlayer(s32 player, u32 tick_begin, u32 frame_count)
{
	SYNetInputFrame frame;
	u32 checksum = 2166136261U;
	u32 i;

	for (i = 0; i < frame_count; i++)
	{
		if (syNetInputGetHistoryFrame(player, tick_begin + i, &frame) != FALSE)
		{
			checksum = syNetInputAccumulateInputChecksum(checksum, player, &frame);
		}
	}
	return checksum;
}

u32 syNetInputGetRemoteHistoryValueChecksumForPlayer(s32 player, u32 tick_begin, u32 frame_count)
{
	SYNetInputFrame frame;
	u32 checksum = 2166136261U;
	u32 i;

	for (i = 0; i < frame_count; i++)
	{
		if (syNetInputTryGetRemoteHistoryForSimTick(player, tick_begin + i, &frame) != FALSE)
		{
			checksum = syNetInputAccumulateInputChecksum(checksum, player, &frame);
		}
	}
	return checksum;
}

void syNetInputGetHistoryInputValueChecksumWindow(u32 tick_begin, u32 frame_count, u32 *out_checksums,
                                                  u32 *out_combined_checksum)
{
	SYNetInputFrame frame;
	u32 checksum = 2166136261U;
	u32 tick_limit;
	u32 tick;
	s32 player;

	tick_limit = tick_begin + frame_count;

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		u32 player_checksum = 2166136261U;

		for (tick = tick_begin; tick < tick_limit; tick++)
		{
			if (syNetInputGetHistoryFrame(player, tick, &frame) != FALSE)
			{
				player_checksum = syNetInputAccumulateInputChecksum(player_checksum, player, &frame);
			}
		}
		checksum ^= player_checksum;
		checksum *= 16777619U;

		if (out_checksums != NULL)
		{
			out_checksums[player] = player_checksum;
		}
	}
	if (out_combined_checksum != NULL)
	{
		*out_combined_checksum = checksum;
	}
}

#ifdef PORT
void syNetInputGetRemoteHistoryValueChecksumWindow(u32 tick_begin, u32 frame_count, u32 *out_checksums,
                                                  u32 *out_combined_checksum)
{
	SYNetInputFrame frame;
	u32 checksum = 2166136261U;
	u32 tick_limit;
	u32 tick;
	s32 player;

	tick_limit = tick_begin + frame_count;

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		u32 player_checksum = 2166136261U;

		for (tick = tick_begin; tick < tick_limit; tick++)
		{
			if (syNetInputTryGetRemoteHistoryForSimTick(player, tick, &frame) != FALSE)
			{
				player_checksum = syNetInputAccumulateInputChecksum(player_checksum, player, &frame);
			}
		}
		checksum ^= player_checksum;
		checksum *= 16777619U;

		if (out_checksums != NULL)
		{
			out_checksums[player] = player_checksum;
		}
	}
	if (out_combined_checksum != NULL)
	{
		*out_combined_checksum = checksum;
	}
}

void syNetInputGetHistoryInputDiagChecksumWindow(u32 tick_begin, u32 frame_count, u32 *out_checksums,
                                                 u32 *out_combined_checksum)
{
	SYNetInputFrame frame;
	u32 checksum = 2166136261U;
	u32 tick_limit;
	u32 tick;
	s32 player;

	tick_limit = tick_begin + frame_count;

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		u32 player_checksum = 2166136261U;

		for (tick = tick_begin; tick < tick_limit; tick++)
		{
			if (syNetInputGetHistoryFrame(player, tick, &frame) != FALSE)
			{
				player_checksum = syNetInputAccumulateInputChecksumDiag(player_checksum, player, &frame);
			}
		}
		checksum ^= player_checksum;
		checksum *= 16777619U;

		if (out_checksums != NULL)
		{
			out_checksums[player] = player_checksum;
		}
	}
	if (out_combined_checksum != NULL)
	{
		*out_combined_checksum = checksum;
	}
}

void syNetInputGetRemoteHistoryDiagChecksumWindow(u32 tick_begin, u32 frame_count, u32 *out_checksums,
                                                  u32 *out_combined_checksum)
{
	SYNetInputFrame frame;
	u32 checksum = 2166136261U;
	u32 tick_limit;
	u32 tick;
	s32 player;

	tick_limit = tick_begin + frame_count;

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		u32 player_checksum = 2166136261U;

		for (tick = tick_begin; tick < tick_limit; tick++)
		{
			if (syNetInputTryGetRemoteHistoryForSimTick(player, tick, &frame) != FALSE)
			{
				player_checksum = syNetInputAccumulateInputChecksumDiag(player_checksum, player, &frame);
			}
		}
		checksum ^= player_checksum;
		checksum *= 16777619U;

		if (out_checksums != NULL)
		{
			out_checksums[player] = player_checksum;
		}
	}
	if (out_combined_checksum != NULL)
	{
		*out_combined_checksum = checksum;
	}
}

s32 syNetInputGetAbortOnInputMismatchMask(void)
{
	const char *e;

	e = getenv("SSB64_NETPLAY_ABORT_ON_INPUT_MISMATCH");
	if ((e == NULL) || (e[0] == '\0'))
	{
		return 0;
	}
	return atoi(e);
}

sb32 syNetInputGetAbortOnInputMismatchFatal(void)
{
	const char *e;

	e = getenv("SSB64_NETPLAY_ABORT_ON_INPUT_MISMATCH_FATAL");
	return ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? TRUE : FALSE;
}

sb32 syNetInputDiagFindFirstPublishedRemoteMismatch(u32 tick_begin, u32 frame_count, s32 *out_player, u32 *out_tick,
                                                  u32 *out_kind)
{
	SYNetInputFrame hf;
	SYNetInputFrame rf;
	u32 tick_limit;
	u32 t;
	s32 player;

	tick_limit = tick_begin + frame_count;

	for (t = tick_begin; t < tick_limit; t++)
	{
		for (player = 0; player < MAXCONTROLLERS; player++)
		{
			sb32 hv = syNetInputGetHistoryFrame(player, t, &hf);
			sb32 rv = syNetInputTryGetRemoteHistoryForSimTick(player, t, &rf);

			if ((hv == FALSE) && (rv == FALSE))
			{
				continue;
			}
			if (hv != rv)
			{
				if (out_player != NULL)
				{
					*out_player = player;
				}
				if (out_tick != NULL)
				{
					*out_tick = t;
				}
				if (out_kind != NULL)
				{
					*out_kind = 0U;
				}
				return TRUE;
			}
			if ((hf.tick != rf.tick) || (hf.buttons != rf.buttons) || (hf.stick_x != rf.stick_x) ||
			    (hf.stick_y != rf.stick_y))
			{
				if (out_player != NULL)
				{
					*out_player = player;
				}
				if (out_tick != NULL)
				{
					*out_tick = t;
				}
				if (out_kind != NULL)
				{
					*out_kind = 1U;
				}
				return TRUE;
			}
		}
	}
	return FALSE;
}

void syNetInputLogDesyncNeedle(u32 validation_tick, u32 needle_tick, int trace_level)
{
	u32 pub_c[MAXCONTROLLERS];
	u32 ring_c[MAXCONTROLLERS];
	s32 p;
	SYNetInputFrame hf;
	SYNetInputFrame rf;
	sb32 hv;
	sb32 rv;

	if (trace_level < 1)
	{
		return;
	}
	for (p = 0; p < MAXCONTROLLERS; p++)
	{
		pub_c[p] = syNetInputGetHistoryInputValueChecksumForPlayer(p, needle_tick, 1U);
		ring_c[p] = syNetInputGetRemoteHistoryValueChecksumForPlayer(p, needle_tick, 1U);
	}
	port_log(
	    "SSB64 NetSync: desync_needle validation_tick=%u needle_tick=%u pub_crc p0=0x%08X p1=0x%08X p2=0x%08X p3=0x%08X | "
	    "ring_crc p0=0x%08X p1=0x%08X p2=0x%08X p3=0x%08X\n",
	    validation_tick,
	    needle_tick,
	    pub_c[0],
	    pub_c[1],
	    pub_c[2],
	    pub_c[3],
	    ring_c[0],
	    ring_c[1],
	    ring_c[2],
	    ring_c[3]);
	if (trace_level < 2)
	{
		return;
	}
	for (p = 0; p < MAXCONTROLLERS; p++)
	{
		hv = syNetInputGetHistoryFrame(p, needle_tick, &hf);
		rv = syNetInputGetRemoteHistoryFrame(p, needle_tick, &rf);
		port_log(
		    "SSB64 NetSync: desync_needle_detail slot=%d hist_valid=%d tick=%u btn=0x%04X sx=%d sy=%d src=%u pred=%u | "
		    "ring_valid=%d tick=%u btn=0x%04X sx=%d sy=%d src=%u pred=%u\n",
		    (int)p,
		    (hv != FALSE) ? 1 : 0,
		    (unsigned int)((hv != FALSE) ? hf.tick : 0U),
		    (unsigned int)((hv != FALSE) ? hf.buttons : 0U),
		    (hv != FALSE) ? hf.stick_x : 0,
		    (hv != FALSE) ? hf.stick_y : 0,
		    (unsigned int)((hv != FALSE) ? hf.source : 0U),
		    (unsigned int)((hv != FALSE) ? hf.is_predicted : 0U),
		    (rv != FALSE) ? 1 : 0,
		    (unsigned int)((rv != FALSE) ? rf.tick : 0U),
		    (unsigned int)((rv != FALSE) ? rf.buttons : 0U),
		    (rv != FALSE) ? rf.stick_x : 0,
		    (rv != FALSE) ? rf.stick_y : 0,
		    (unsigned int)((rv != FALSE) ? rf.source : 0U),
		    (unsigned int)((rv != FALSE) ? rf.is_predicted : 0U));
	}
}

void syNetInputClearRemoteSlotPredictionState(void)
{
	s32 i;
	s32 n;
	s32 slot;

	n = syNetPeerGetRemoteHumanSlotCount();
	for (i = 0; i < n; i++)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(i, &slot) == FALSE)
		{
			continue;
		}
		if (syNetInputCheckPlayer(slot) == FALSE)
		{
			continue;
		}
		syNetInputClearFrame(&sSYNetInputSlots[slot].last_confirmed);
	}
}
#endif

void syNetInputSetRecordingEnabled(sb32 is_enabled)
{
	sSYNetInputIsRecording = is_enabled;

	if (is_enabled != FALSE)
	{
		sSYNetInputRecordedFrameCount = 0;
	}
}

sb32 syNetInputGetRecordingEnabled(void)
{
	return sSYNetInputIsRecording;
}

u32 syNetInputGetRecordedFrameCount(void)
{
	return sSYNetInputRecordedFrameCount;
}

void syNetInputClearReplayFrames(void)
{
	s32 player;
	s32 i;

	sSYNetInputRecordedFrameCount = 0;

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		for (i = 0; i < SYNETINPUT_REPLAY_MAX_FRAMES; i++)
		{
			syNetInputClearFrame(&sSYNetInputReplayFrames[player][i]);
		}
	}
}

sb32 syNetInputSetReplayFrame(s32 player, u32 tick, const SYNetInputFrame *frame)
{
	if ((syNetInputCheckPlayer(player) == FALSE) || (frame == NULL) || (tick >= SYNETINPUT_REPLAY_MAX_FRAMES))
	{
		return FALSE;
	}
	sSYNetInputReplayFrames[player][tick] = *frame;
	sSYNetInputReplayFrames[player][tick].tick = tick;
	sSYNetInputReplayFrames[player][tick].is_valid = TRUE;

	if (sSYNetInputRecordedFrameCount < (tick + 1))
	{
		sSYNetInputRecordedFrameCount = tick + 1;
	}
	return TRUE;
}

sb32 syNetInputGetReplayFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	SYNetInputFrame *frame;

	if ((syNetInputCheckPlayer(player) == FALSE) || (tick >= SYNETINPUT_REPLAY_MAX_FRAMES))
	{
		return FALSE;
	}
	frame = &sSYNetInputReplayFrames[player][tick];

	if ((frame->is_valid == FALSE) || (frame->tick != tick))
	{
		return FALSE;
	}
	if (out_frame != NULL)
	{
		*out_frame = *frame;
	}
	return TRUE;
}

u32 syNetInputGetReplayInputChecksum(void)
{
	SYNetInputFrame frame;
	u32 checksum = 2166136261U;
	u32 tick;
	s32 player;

	for (tick = 0; tick < sSYNetInputRecordedFrameCount; tick++)
	{
		for (player = 0; player < MAXCONTROLLERS; player++)
		{
			if (syNetInputGetReplayFrame(player, tick, &frame) != FALSE)
			{
				checksum = syNetInputAccumulateInputChecksum(checksum, player, &frame);
			}
		}
	}
	return checksum;
}

void syNetInputSetReplayMetadata(const SYNetInputReplayMetadata *metadata)
{
	if (metadata != NULL)
	{
		sSYNetInputReplayMetadata = *metadata;
		sSYNetInputReplayMetadata.magic = SYNETINPUT_REPLAY_MAGIC;
		sSYNetInputReplayMetadata.version = SYNETINPUT_REPLAY_VERSION;
		sSYNetInputIsReplayMetadataValid = TRUE;
	}
}

sb32 syNetInputGetReplayMetadata(SYNetInputReplayMetadata *out_metadata)
{
	if (sSYNetInputIsReplayMetadataValid == FALSE)
	{
		return FALSE;
	}
	if (out_metadata != NULL)
	{
		*out_metadata = sSYNetInputReplayMetadata;
	}
	return TRUE;
}

sb32 syNetInputGetRemoteHistoryFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	return syNetInputTryGetRemoteHistoryForSimTick(player, tick, out_frame);
}

#ifdef PORT
void syNetInputDebugXorPublishedHistoryButtons(s32 player, u32 tick, u16 xor_mask)
{
	SYNetInputFrame hist;

	if (syNetInputCheckPlayer(player) == FALSE)
	{
		return;
	}
	if (syNetInputGetStoredFrame(sSYNetInputHistory, player, tick, &hist) == FALSE)
	{
		return;
	}
	hist.buttons ^= xor_mask;
	syNetInputStoreFrame(sSYNetInputHistory, player, &hist);
}
#endif

SYController *syNetInputGetSimController(s32 player)
{
	if (syNetInputCheckPlayer(player) == FALSE)
	{
		return NULL;
	}
	return &gSYControllerDevices[player];
}

void syNetInputExportPeerConnectStatus(s32 *out_last_tick, u8 *out_disconnected, s32 count)
{
	s32 i;
	s32 n;

	if ((out_last_tick == NULL) || (out_disconnected == NULL))
	{
		return;
	}
	n = count;
	if (n <= 0)
	{
		return;
	}
	if (n > MAXCONTROLLERS)
	{
		n = MAXCONTROLLERS;
	}
	for (i = 0; i < n; i++)
	{
		out_disconnected[i] = 0;
		if (sSYNetInputSlots[i].last_confirmed.is_valid != FALSE)
		{
			out_last_tick[i] = (s32)sSYNetInputSlots[i].last_confirmed.tick;
		}
		else
		{
			out_last_tick[i] = -1;
		}
	}
}

#ifdef PORT
#if !defined(_WIN32)
static sb32 syNetInputEnvStallUntilRemoteEnabled(void)
{
	const char *e;

	if (sSYNetInputStallUntilRemoteEnvCache < 0)
	{
		e = getenv("SSB64_NETPLAY_STALL_UNTIL_REMOTE");
		sSYNetInputStallUntilRemoteEnvCache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	}
	return (sSYNetInputStallUntilRemoteEnvCache != 0) ? TRUE : FALSE;
}

static sb32 syNetInputRemoteSlotsMissingRingFrameForTick(u32 tick)
{
	SYNetInputFrame frame;
	s32 i;
	s32 n;
	s32 slot;
	u32 ring_tick;

	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return FALSE;
	}
	/*
	 * Remote ring rows are keyed by **wire** tick (`GatherHistoryBundle`: sim + committed delay).
	 * Admission for authoritative sim step `tick` must probe `RemoteHistory[slot][tick + delay]`.
	 */
	ring_tick = syNetInputRemoteHistoryWireLookupTick(tick);
	n = syNetPeerGetRemoteHumanSlotCount();
	for (i = 0; i < n; i++)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(i, &slot) == FALSE)
		{
			continue;
		}
		if (syNetInputCheckPlayer(slot) == FALSE)
		{
			continue;
		}
		if (syNetInputGetStoredFrame(sSYNetInputRemoteHistory, slot, ring_tick, &frame) == FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * Strict remote-miss: publish **local** sim slot(s) from HID latch into published history so `syNetPeerGatherHistoryBundle`
 * can emit INPUT before remote `RemoteHistory[slot][sim_tick + input_delay]` exists; full resolve/publish for all slots stays deferred.
 *
 * The latch in `syNetInputFuncRead` is always for **this** authoritative sim `tick`. `syNetInputMakeLocalFrame` labels the
 * frame with its `tick` argument — it must be **`tick`**, not `tick - exec_delay`. Execution delay only shifts **which remote
 * sim tick** we probe in `syNetInputRemoteSlotsMissingRingFrameForTick(syNetInputStrictContractRemoteProbeSimTick(tick))`, not
 * the local publish key (mis-labeling broke inputs for exec_delay > 0).
 */
static void syNetInputStrictContractPartialPublishLocalFromLatch(u32 tick)
{
	SYNetInputFrame frame;
	s32 slot;
	s32 extra;
	const u32 pub_tick = tick;

	slot = syNetPeerGetLocalSimSlot();
	if ((slot >= 0) && (slot < MAXCONTROLLERS))
	{
		syNetInputMakeLocalFrame(slot, pub_tick, &frame);
		syNetInputPublishFrame(slot, &frame);
	}
	extra = syNetPeerGetExtraLocalSenderSimSlot();
	if ((extra >= 0) && (extra < MAXCONTROLLERS) && (extra != slot))
	{
		syNetInputMakeLocalFrame(extra, pub_tick, &frame);
		syNetInputPublishFrame(extra, &frame);
	}
	syNetInputPublishMainController();
}

#endif
#endif

/*
 * Fill `out_frames[0..MAXCONTROLLERS)` with the authoritative `SYNetInputFrame` for each player at `tick`.
 * Mirrors GGPO’s `ggpo_synchronize_inputs`: live path resolves HID latch + remote rings; rollback resim resolves locals from
 * published history inside `syNetInputMakeLocalFrame`. Call after HID latch is current for `tick`.
 */
static void syNetInputSynchronizeInputsForTick(u32 tick, SYNetInputFrame *out_frames)
{
	s32 player;

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		syNetInputResolveFrame(player, tick, &out_frames[player]);
	}
}

#if defined(PORT) && !defined(_WIN32)
/*
 * `SSB64_NETPLAY_INPUT_PREDICT_DIAG=1`: rate-limited (60 ticks) when strict VS uses predicted remote input.
 * `=2`: log every qualifying tick.
 */
static void syNetInputMaybeLogInputPredictDiag(u32 tick, const SYNetInputFrame *synced)
{
	s32 ri;
	s32 n;
	s32 slot;
	sb32 any_pred;
	u32 probe;

	if (sSYNetInputPredictDiagLevelCache == -999)
	{
		char *e;

		e = getenv("SSB64_NETPLAY_INPUT_PREDICT_DIAG");
		sSYNetInputPredictDiagLevelCache = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 0;
		if (sSYNetInputPredictDiagLevelCache < 0)
		{
			sSYNetInputPredictDiagLevelCache = 0;
		}
	}
	if (sSYNetInputPredictDiagLevelCache == 0)
	{
		return;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	if (syNetInputStrictInputContractEnabled() == FALSE)
	{
		return;
	}
	any_pred = FALSE;
	n = syNetPeerGetRemoteHumanSlotCount();
	for (ri = 0; ri < n; ri++)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(ri, &slot) == FALSE)
		{
			continue;
		}
		if (syNetInputCheckPlayer(slot) == FALSE)
		{
			continue;
		}
		if ((synced[slot].source == nSYNetInputSourceRemotePredicted) && (synced[slot].is_valid != FALSE))
		{
			any_pred = TRUE;
			break;
		}
	}
	if (any_pred == FALSE)
	{
		return;
	}
	if ((sSYNetInputPredictDiagLevelCache < 2) && ((tick % 60U) != 0U))
	{
		return;
	}
	probe = syNetInputStrictContractRemoteProbeSimTick(tick);
	port_log(
	    "SSB64 NetInput: input_predict_diag tick=%u probe_sim=%u exec_delay=%d prediction_env_on=%d\n",
	    tick,
	    probe,
	    syNetInputGetExecutionDelayFrames(),
	    (g_UseInputPrediction != FALSE) ? 1 : 0);
}

/*
 * `SSB64_NETPLAY_FRAME_COMMIT_DIAG=1`: rate-limited admission trace (active VS, non-resim).
 * `=2`: log every FuncRead admission outcome for active VS (very noisy).
 */
static void syNetInputMaybeLogFrameCommitDiag(u32 tick, char path, sb32 exec_ok, sb32 publish)
{
	u32 hr;

	if (sSYNetInputFrameCommitDiagLevelCache == -999)
	{
		char *e;

		e = getenv("SSB64_NETPLAY_FRAME_COMMIT_DIAG");
		sSYNetInputFrameCommitDiagLevelCache = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 0;
		if (sSYNetInputFrameCommitDiagLevelCache < 0)
		{
			sSYNetInputFrameCommitDiagLevelCache = 0;
		}
	}
	if (sSYNetInputFrameCommitDiagLevelCache == 0)
	{
		return;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	if ((sSYNetInputFrameCommitDiagLevelCache < 2) && ((tick % 60U) != 0U))
	{
		return;
	}
	hr = syNetPeerGetHighestRemoteTick();
	port_log(
	    "SSB64 NetInput: frame_commit_diag tick=%u path=%c exec_ok=%d publish=%d sup=%d hr=%u\n",
	    tick,
	    (int)(unsigned char)path,
	    (exec_ok != FALSE) ? 1 : 0,
	    (publish != FALSE) ? 1 : 0,
	    (sSYNetInputSuppressSceneUpdateAfterRead != FALSE) ? 1 : 0,
	    (unsigned int)hr);
}

#define SYNETINPUT_DELAY_SYNC_DIAG_BOOT_TICKS 300U

static int syNetInputGetDelaySyncDiagLevel(void)
{
	char *e;

	if (sSYNetInputDelaySyncDiagLevelCache != -999)
	{
		return sSYNetInputDelaySyncDiagLevelCache;
	}
	e = getenv("SSB64_NETPLAY_DELAY_SYNC_DIAG");
	sSYNetInputDelaySyncDiagLevelCache = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 0;
	if (sSYNetInputDelaySyncDiagLevelCache < 0)
	{
		sSYNetInputDelaySyncDiagLevelCache = 0;
	}
	if (sSYNetInputDelaySyncDiagLevelCache > 2)
	{
		sSYNetInputDelaySyncDiagLevelCache = 2;
	}
	return sSYNetInputDelaySyncDiagLevelCache;
}

static void syNetInputMaybeLogDelaySyncDiag(u32 sim_tick_for_read)
{
	u32 mark;
	u32 prev_tracked_d;
	u32 probe_sim;
	u32 wire_tick;
	u32 d;
	u32 hr;
	int lvl;
	s32 n;
	s32 i;
	s32 slot;
	sb32 boot_window;
	sb32 d_changed;
	sb32 have_ring;
	SYNetInputFrame frame;
	s32 log_slot[2];
	int log_ok[2];
	s32 log_n;

	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	lvl = syNetInputGetDelaySyncDiagLevel();
	if (lvl == 0)
	{
		return;
	}
	prev_tracked_d = sSYNetInputDelaySyncDiagLastCommittedD;
	d = syNetPeerGetCommittedInputDelay();
	d_changed = (sb32)((prev_tracked_d != ~(u32)0U) && (prev_tracked_d != d));
	sSYNetInputDelaySyncDiagLastCommittedD = d;

	mark = syNetPeerGetDelaySyncDiagExecReadySimTick();
	boot_window = FALSE;
	if ((mark != ~(u32)0U) && (sim_tick_for_read >= mark))
	{
		if ((sim_tick_for_read - mark) < SYNETINPUT_DELAY_SYNC_DIAG_BOOT_TICKS)
		{
			boot_window = TRUE;
		}
	}

	if ((lvl < 2) && (d_changed == FALSE) && (boot_window == FALSE))
	{
		return;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}

	probe_sim = syNetInputStrictContractRemoteProbeSimTick(sim_tick_for_read);
	wire_tick = syNetInputRemoteHistoryWireLookupTick(probe_sim);
	hr = syNetPeerGetHighestRemoteTick();

	log_slot[0] = -1;
	log_slot[1] = -1;
	log_ok[0] = -1;
	log_ok[1] = -1;
	log_n = 0;
	n = syNetPeerGetRemoteHumanSlotCount();
	for (i = 0; (i < n) && (log_n < 2); i++)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(i, &slot) == FALSE)
		{
			continue;
		}
		if (syNetInputCheckPlayer(slot) == FALSE)
		{
			continue;
		}
		have_ring = syNetInputGetStoredFrame(sSYNetInputRemoteHistory, slot, wire_tick, &frame);
		log_slot[log_n] = slot;
		log_ok[log_n] = (have_ring != FALSE) ? 1 : 0;
		log_n++;
	}

	port_log(
	    "SSB64 NetInput: delay_sync_diag sim_read=%u probe_sim=%u D=%u wire=%u exec_d=%d hr=%u ls=%d rs0=%d rok0=%d rs1=%d rok1=%d d_ch=%d boot=%d\n",
	    (unsigned int)sim_tick_for_read,
	    (unsigned int)probe_sim,
	    (unsigned int)d,
	    (unsigned int)wire_tick,
	    syNetInputGetExecutionDelayFrames(),
	    (unsigned int)hr,
	    (int)syNetPeerGetLocalSimSlot(),
	    (log_n > 0) ? (int)log_slot[0] : -1,
	    (log_n > 0) ? log_ok[0] : -1,
	    (log_n > 1) ? (int)log_slot[1] : -1,
	    (log_n > 1) ? log_ok[1] : -1,
	    (d_changed != FALSE) ? 1 : 0,
	    (boot_window != FALSE) ? 1 : 0);
}
#endif

/*
 * Per sim tick: sample HID at most once (see `sSYNetInputPortHwLatchTick`), **before** stall-until-remote / skew checks
 * so local capture is indexed only by `sSYNetInputTick`, then synchronize all slots, publish to globals + rings,
 * optional replay capture. Tick advances only after a full `scVSBattleFuncUpdate` (`syNetInputAdvanceAuthoritativeSimTick`).
 *
 * Linux UDP VS: `syNetPeerUpdateBattleGate` + execution / stall / skew admission run **before** resolve+publish so a frame
 * cycle cannot publish authoritative inputs and then fail to sim for execution-revocation reasons in the same step.
 */
void syNetInputFuncRead(void)
{
	SYNetInputFrame synchronized[MAXCONTROLLERS];
	u32 tick;
	s32 player;

#ifdef PORT
	sSYNetInputSuppressSceneUpdateAfterRead = FALSE;
#if !defined(_WIN32)
	sSYNetInputStrictContractSkippedPublish = FALSE;
	if (syNetRollbackIsResimulating() == FALSE)
	{
		if (syNetPeerIsVSSessionActive() != FALSE)
		{
			sb32 block_on_exec;

			syNetPeerUpdateBattleGate();
			block_on_exec = (syNetPeerCheckBattleExecutionReady() == FALSE) ? TRUE : FALSE;
			if (syNetInputStrictInputContractEnabled() != FALSE)
			{
				block_on_exec = FALSE;
			}
			if (block_on_exec != FALSE)
			{
				tick = syNetInputGetTick();
				syNetInputAdmissionBump('E');
				syNetInputMaybeLogFrameCommitDiag(tick, 'E', FALSE, FALSE);
				return;
			}
		}
		else
		{
			syNetPeerPumpIngressBeforeInputRead();
		}
	}
#endif
#endif
	tick = syNetInputGetTick();
#ifdef PORT
	/* Resim takes local inputs from history, not HID — avoid sampling fresh hardware mid-rewind. */
	if (syNetRollbackIsResimulating() == FALSE)
	{
		/*
		 * Latch HID **before** VS admission that depends on remote ring / skew (network timing). Otherwise a stall or
		 * skew hold defers the first `syControllerFuncRead` for this tick until later wall time, binding a different
		 * physical sample to the same `tick` — local sim index must own local input capture, not ingress pacing.
		 */
		if (tick != sSYNetInputPortHwLatchTick)
		{
			syControllerFuncRead();
			memcpy(sSYNetInputHardwareLatch, gSYControllerDevices, sizeof(SYController) * (size_t)MAXCONTROLLERS);
			syNetInputNeutralizeAllControllerDevices();
			sSYNetInputPortHwLatchTick = tick;
		}
#if !defined(_WIN32)
		if (syNetPeerIsVSSessionActive() != FALSE)
		{
			if (syNetInputStrictInputContractEnabled() != FALSE)
			{
				u32 probe_sim;
				sb32 remote_miss;
				sb32 force_advance;
				char *fe;

				probe_sim = syNetInputStrictContractRemoteProbeSimTick(tick);
				syNetInputMaybeLogDelaySyncDiag(tick);
				remote_miss = syNetInputRemoteSlotsMissingRingFrameForTick(probe_sim);
				if (remote_miss == FALSE)
				{
					sSYNetInputStrictRStuckSimTick = ~(u32)0;
					sSYNetInputStrictRStuckFrames = 0U;
				}
				force_advance = FALSE;
				fe = getenv("SSB64_NETPLAY_STRICT_R_STUCK_FORCE_DIAG");
				if ((remote_miss != FALSE) && (syNetInputGetExecutionDelayFrames() == 0) && (fe != NULL) &&
				    (fe[0] != '\0') && (atoi(fe) != 0))
				{
					if (tick == sSYNetInputStrictRStuckSimTick)
					{
						sSYNetInputStrictRStuckFrames++;
					}
					else
					{
						sSYNetInputStrictRStuckSimTick = tick;
						sSYNetInputStrictRStuckFrames = 1U;
					}
					if (sSYNetInputStrictRStuckFrames > 60U)
					{
						port_log("[NET] FORCE ADVANCE on stuck tick %u (delay=0)\n", (unsigned)tick);
						sSYNetInputStrictRStuckSimTick = ~(u32)0;
						sSYNetInputStrictRStuckFrames = 0U;
						force_advance = TRUE;
					}
				}
				if ((remote_miss != FALSE) && (force_advance == FALSE))
				{
					/*
					 * Partial publish for GatherHistoryBundle / wire INPUT; same sim tick must not run a full
					 * `ifCommonBattleUpdateInterfaceAll` until the next FuncRead can full-publish (S/K skew/stall
					 * paths already use scene suppress for this — strict R must match or inputs/physics apply twice).
					 */
					syNetInputStrictContractPartialPublishLocalFromLatch(tick);
					sSYNetInputStrictContractSkippedPublish = TRUE;
					sSYNetInputSuppressSceneUpdateAfterRead = TRUE;
					syNetInputAdmissionBump('R');
					syNetInputMaybeLogFrameCommitDiag(tick, 'R', TRUE, FALSE);
					return;
				}
			}
			else
			{
				(void)syNetPeerRunCatchUpBehindBeforeInputStall(tick);
				if (syNetInputEnvStallUntilRemoteEnabled() != FALSE)
				{
					if (syNetInputRemoteSlotsMissingRingFrameForTick(tick) != FALSE)
					{
						if (syNetPeerShouldRelaxStallUntilRemoteForCatchUp(tick) == FALSE)
						{
							sSYNetInputSuppressSceneUpdateAfterRead = TRUE;
							syNetInputAdmissionBump('S');
							syNetInputMaybeLogFrameCommitDiag(tick, 'S', TRUE, FALSE);
							return;
						}
						/* Catch-up behind: proceed without strict stall for this FuncRead (experimental). */
					}
				}
				if (syNetPeerShouldHoldSimTickForSkewPacing(tick, NULL) != FALSE)
				{
					sSYNetInputSuppressSceneUpdateAfterRead = TRUE;
					syNetInputAdmissionBump('K');
					syNetInputMaybeLogFrameCommitDiag(tick, 'K', TRUE, FALSE);
					return;
				}
			}
		}
#else
		if (syNetPeerIsVSSessionActive() != FALSE)
		{
			(void)syNetPeerRunCatchUpBehindBeforeInputStall(tick);
			if (syNetInputEnvStallUntilRemoteEnabled() != FALSE)
			{
				if (syNetInputRemoteSlotsMissingRingFrameForTick(tick) != FALSE)
				{
					if (syNetPeerShouldRelaxStallUntilRemoteForCatchUp(tick) == FALSE)
					{
						sSYNetInputSuppressSceneUpdateAfterRead = TRUE;
						syNetInputAdmissionBump('S');
						syNetInputMaybeLogFrameCommitDiag(tick, 'S', TRUE, FALSE);
						return;
					}
				}
			}
			if (syNetPeerShouldHoldSimTickForSkewPacing(tick, NULL) != FALSE)
			{
				sSYNetInputSuppressSceneUpdateAfterRead = TRUE;
				syNetInputAdmissionBump('K');
				syNetInputMaybeLogFrameCommitDiag(tick, 'K', TRUE, FALSE);
				return;
			}
		}
#endif
	}
#else
	syControllerFuncRead();
#endif

	syNetInputSynchronizeInputsForTick(tick, synchronized);
#if defined(PORT) && !defined(_WIN32)
	syNetInputMaybeLogInputPredictDiag(tick, synchronized);
#endif
	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		syNetInputPublishFrame(player, &synchronized[player]);

		if (sSYNetInputIsRecording != FALSE)
		{
			syNetInputSetReplayFrame(player, tick, &synchronized[player]);
		}
	}
	syNetInputPublishMainController();

#if defined(PORT) && !defined(_WIN32)
	if ((syNetPeerIsVSSessionActive() != FALSE) && (syNetRollbackIsResimulating() == FALSE))
	{
		sSYNetInputStrictRStuckSimTick = ~(u32)0;
		sSYNetInputStrictRStuckFrames = 0U;
		syNetInputAdmissionBump('P');
		syNetInputMaybeLogFrameCommitDiag(tick, 'P', TRUE, TRUE);
		syNetInputMaybeAdmissionPeriodicSummary(tick);
	}
#endif
}

static void syNetInputRestoreRemoteConfirmedSeed(s32 player, u32 resim_start_tick)
{
	SYNetInputFrame frame;
	u32 t;

	if (syNetInputCheckPlayer(player) == FALSE)
	{
		return;
	}
	if (resim_start_tick == 0)
	{
		syNetInputClearFrame(&sSYNetInputSlots[player].last_confirmed);
		return;
	}
	for (t = resim_start_tick; t > 0; t--)
	{
		if (syNetInputTryGetRemoteHistoryForSimTick(player, t - 1, &frame) != FALSE)
		{
			sSYNetInputSlots[player].last_confirmed = frame;
			return;
		}
	}
	syNetInputClearFrame(&sSYNetInputSlots[player].last_confirmed);
}

/*
 * Before `syNetRollbackRunResim` replays ticks: rewind `last_published` to the state *before* mismatch tick and restore
 * remote “last confirmed” used by `MakePredictedFrame` so gaps still predict coherently mid-resim.
 */
void syNetInputRollbackPrepareForResim(u32 resim_start_tick)
{
	s32 player;
	SYNetInputFrame frame;

	if (resim_start_tick > 0)
	{
		for (player = 0; player < MAXCONTROLLERS; player++)
		{
			if (syNetInputGetHistoryFrame(player, resim_start_tick - 1, &frame) != FALSE)
			{
				sSYNetInputSlots[player].last_published = frame;
			}
			else syNetInputClearFrame(&sSYNetInputSlots[player].last_published);
		}
	}
	else
	{
		for (player = 0; player < MAXCONTROLLERS; player++)
		{
			syNetInputClearFrame(&sSYNetInputSlots[player].last_published);
		}
	}
	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		syNetInputRestoreRemoteConfirmedSeed(player, resim_start_tick);
	}
}
