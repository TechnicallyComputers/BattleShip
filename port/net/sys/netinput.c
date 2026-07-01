#include <sys/netinput.h>

/*
 * NetInput implementation: ring buffers keyed by `tick % SYNETINPUT_HISTORY_LENGTH`.
 * `syNetInputResolveFrame` chooses source; `syNetInputPublishFrame` materializes edge-detected taps into `gSYControllerDevices`.
 * On PORT, `syNetInputFuncRead` snapshots hardware into `sSYNetInputHardwareLatch` and clears `gSYControllerDevices` before
 * resolve/publish so sim globals are never read for gameplay between raw HID and publish. At most one HID snapshot is taken per
 * `sSYNetInputTick` (taskman may call FuncRead several times while skew pacing holds sim without advancing the tick).
 *
 * GGPO-shaped contract: live phase-locked VS samples raw HID at sim `t` into a local delay ring owned by `t + D`; battle
 * resolves local slots from that owned sim row, so committed `D` is felt locally and sent to the peer ahead of use. During
 * rollback resim, local HID is ignored; `syNetInputMakeLocalFrame` replays from published history for that tick.
 */

#include <sys/controller.h>
#include <sys/netpeer.h>
#include <sys/netpeer_transport.h>
#include <sys/netrollback.h>
#include <sys/netrollback_episode.h>
#include <sys/netsession_params.h>
#include <sys/netinput_timeline.h>
#include <sys/taskman.h>

#ifdef PORT
#include <sys/netdesyncclassifier.h>
#include "gameloop.h"
#include <sys/net_debug_agent_log.h>
#if defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE)
#include <sys/netreconnect.h>
#endif
#endif

#ifdef PORT
#include <sc/scmanager.h>
#include <sc/sctypes.h>
#include <stdio.h>
#include <string.h>
extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);
static sb32 sSYNetInputPredictNeutral;
static SYController sSYNetInputHardwareLatch[MAXCONTROLLERS];
static SYNetInputFrame sSYNetInputLocalDelayHistory[MAXCONTROLLERS][SYNETINPUT_HISTORY_LENGTH];
/* Last gameplay row enqueued for wire transmit per sim tick (peer-symmetric authority for local slot). */
static SYNetInputFrame sSYNetInputTransmittedHistory[MAXCONTROLLERS][SYNETINPUT_HISTORY_LENGTH];
static u32 sSYNetInputRemotePacketSeqHistory[MAXCONTROLLERS][SYNETINPUT_HISTORY_LENGTH];
static ub8 sSYNetInputRemotePacketSeqValid[MAXCONTROLLERS][SYNETINPUT_HISTORY_LENGTH];
#ifdef PORT
static u32 sSYNetInputSimPredictedRemoteTick[SYNETINPUT_HISTORY_LENGTH];
static ub8 sSYNetInputSimPredictedRemoteUsed[SYNETINPUT_HISTORY_LENGTH];
#endif
static u32 sSYNetInputRemoteConfirmedConflictLogsRemaining;
static sb32 sSYNetInputRemoteAnalogOnsetPredEnvCache = -999;
/* Raw analog without quantize: wider defaults avoid first-gesture GGPO over correction. */
#define SYNETINPUT_GGPO_STICK_DEADBAND_DEFAULT 12
#define SYNETINPUT_GGPO_STICK_DEADBAND_PREDICT_DEFAULT 14
#define SYNETINPUT_ANALOG_ONSET_STICK_MAG_DEFAULT 28
#define SYNETINPUT_ANALOG_ONSET_STICK_MAG_MAX 80
#define SYNETINPUT_ANALOG_ONSET_LOOKBACK_DEFAULT 60
#define SYNETINPUT_ANALOG_ONSET_FACING_THRESH_DEFAULT 4
#define SYNETINPUT_ANALOG_ONSET_LARGE_DELTA_DEFAULT 40
#define SYNETINPUT_ANALOG_SAME_INTENT_TOLERANCE 14
#define SYNETINPUT_ANALOG_ONSET_WIRE_PEEK_FRAMES 4U
#define SYNETINPUT_ANALOG_ONSET_WIRE_PEEK_AHEAD_DEFAULT 8U
static s32 sSYNetInputGgpoStickDeadband = -1;
static s32 sSYNetInputGgpoStickDeadbandPredict = -1;
static s32 sSYNetInputAnalogOnsetStickMag = -1;
static s32 sSYNetInputAnalogOnsetLookback = -1;
static s32 sSYNetInputAnalogOnsetFacingThresh = -1;
static s32 sSYNetInputAnalogOnsetLargeDelta = -1;
static u32 sSYNetInputAnalogOnsetLogBudget;
/* Sim-tick the latch was last filled for; 0xFFFFFFFFU => next FuncRead must sample HID. */
static u32 sSYNetInputPortHwLatchTick = 0xFFFFFFFFU;
static sb32 sSYNetInputLocalEncodingWasDigital[MAXCONTROLLERS];
static u32 sSYNetInputMixedInputLogBudget;
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
#ifdef PORT
	/* Newest strict-confirmed remote row with sticks outside predict deadband (analog onset). */
	SYNetInputFrame last_non_neutral;
	/* Remote stick encoding (pad vs keyboard) for mixed-input prediction. */
	sb32 remote_encoding_was_digital;
	u32 remote_encoding_grace_until_tick;
#endif

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

sb32 syNetInputRollbackSimAdvanceAllowed(u32 next_sim_tick)
{
	u32 hr;
	u32 cap;

#ifdef PORT
	if (syNetRollbackIsBattleSimHoldActive() != FALSE)
	{
		static u32 sLastHoldBlockedAdvanceLogTick = ~(u32)0;

		if (next_sim_tick != sLastHoldBlockedAdvanceLogTick)
		{
			port_log(
			    "SSB64 NetInput: sim advance blocked (BATTLE_SIM_HOLD) next_sim=%u peer_vs_active=%d\n",
			    next_sim_tick,
			    (int)syNetPeerIsVSSessionActive());
			sLastHoldBlockedAdvanceLogTick = next_sim_tick;
		}
		return FALSE;
	}
#endif
	if ((syNetSessionParamsRollbackEnabled() == FALSE) || (syNetPeerIsVSSessionActive() == FALSE))
	{
		return TRUE;
	}
#ifdef PORT
	/* Startup: do not advance sim until remote wire frontier covers sim+D (reduces strict-R abort at tick 1–4). */
	if (syNetInputAuthoritativeWireContractEnabled() != FALSE)
	{
		u32 wire_need;
		u32 lead_b;

		wire_need = syNetPeerGetBaseRequiredWireTick(next_sim_tick);
		lead_b = syNetInputGetStrictRemoteLeadBufferTicks();
		if (wire_need > lead_b)
		{
			wire_need -= lead_b;
		}
		else
		{
			wire_need = 0U;
		}
		hr = syNetPeerGetHighestRemoteTick();
		if (hr < wire_need)
		{
			return FALSE;
		}
	}
#endif
	hr = syNetPeerGetHighestRemoteTick();
#if defined(PORT)
	/* Intro wait: do not run host-ahead sim without peer wire; cap to remote sim frontier. */
	if ((gSCManagerBattleState != NULL) &&
	    (gSCManagerBattleState->game_status == nSCBattleGameStatusWait))
	{
		if (hr == 0U)
		{
			return FALSE;
		}
		{
			u32 remote_sim_frontier;

			remote_sim_frontier = syNetPeerDelaySimTickFromWire(hr);
			if (next_sim_tick > remote_sim_frontier)
			{
				return FALSE;
			}
		}
	}
#endif
	if (hr == 0U)
	{
		return TRUE;
	}
	cap = syNetPeerDelaySimTickFromWire(hr) + syNetPeerGetCommittedInputDelay() +
	      syNetPeerGetPhaseLockPredictionWindowTicks();
	if (next_sim_tick > cap)
	{
		return FALSE;
	}
#ifdef PORT
	{
		u32 epoch_cap;
		u32 cap_source;

		if ((syNetRollbackGetLiveSimCap(&epoch_cap, &cap_source) != FALSE) && (epoch_cap != ~(u32)0) &&
		    (next_sim_tick > epoch_cap))
		{
			static u32 sLastEpochCapBlockedAdvanceLogTick = ~(u32)0;

			if (next_sim_tick != sLastEpochCapBlockedAdvanceLogTick)
			{
				port_log(
				    "SSB64 NetInput: sim advance blocked (rollback_epoch_cap=%u source=%u) next_sim=%u peer_vs_active=%d\n",
				    epoch_cap,
				    cap_source,
				    next_sim_tick,
				    (int)syNetPeerIsVSSessionActive());
				sLastEpochCapBlockedAdvanceLogTick = next_sim_tick;
			}
			return FALSE;
		}
	}
#endif
	return TRUE;
}

void syNetInputAdvanceAuthoritativeSimTick(void)
{
	u32 completed_tick;
	u32 next_tick;

	next_tick = sSYNetInputTick + 1U;
	if (syNetInputRollbackSimAdvanceAllowed(next_tick) == FALSE)
	{
		return;
	}
	completed_tick = sSYNetInputTick;
	sSYNetInputTick++;
#ifdef PORT
	sSYNetGgpoBattleFrame++;
	syNetPeerNoteSharedCommitAdvanced(completed_tick);
#endif
}

#ifdef PORT
static u64 sSYNetInputAdmissionP;
static u64 sSYNetInputAdmissionE;
static u64 sSYNetInputAdmissionW;
static u64 sSYNetInputAdmissionS;
static u64 sSYNetInputAdmissionK;
static u64 sSYNetInputAdmissionR;
typedef struct SYNetInputStrictCache
{
	u32 last_checked_sim_tick;
	u32 last_required_wire_tick;
	u32 last_highest_remote_tick;
	u32 last_committed_delay;
	sb32 last_result;
	sb32 is_valid;
	sb32 last_full_aux_checks;

} SYNetInputStrictCache;
static int sSYNetInputInputContractTierEnvCache = -1;
static sb32 sSYNetInputStrictContractSkippedPublish;
static u32 sSYNetInputStrictRStuckSimTick = ~(u32)0;
static u32 sSYNetInputStrictRStuckFrames;
static int sSYNetInputAdmissionSummaryIvCache = -999;
static int sSYNetInputPredictDiagLevelCache = -999;
static int sSYNetInputFrameCommitDiagLevelCache = -999;
static int sSYNetInputIngressExtraPumpsEnvCache = -999;
static sb32 sSYNetInputSessionIngressPumpsOverrideValid;
static int sSYNetInputSessionIngressPumpsOverride;
static sb32 sSYNetInputSessionBundleRedundancyOverrideValid;
static int sSYNetInputSessionBundleRedundancyOverride;
static int sSYNetInputDelaySyncDiagLevelCache = -999;
static int sSYNetInputStrictStallDiagLevelCache = -999;
static u32 sSYNetInputStrictStallDiagLastTick = ~(u32)0;
static u32 sSYNetInputStrictStallDiagLastFrames;
static u32 sSYNetInputDelaySyncDiagLastCommittedD = ~(u32)0;
static int sSYNetInputStrictRemoteLeadBufferEnvCache = -999;
static SYNetInputStrictCache sSYNetInputStrictCache;
static s32 sSYNetInputRemoteConfirmedLastWire[MAXCONTROLLERS];
static u32 sSYNetInputRemoteGapFillLogBudget;

/* Remote ring cells are not part of `syNetInputStrictReadyCached` keys — invalidate on every remote write. */
static void syNetInputStrictReadyCacheInvalidate(void)
{
	sSYNetInputStrictCache.is_valid = FALSE;
}

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

	/*
	 * Strict extra slack (`wire_cap = sim + D + slack`): **only** `SSB64_NETPLAY_STRICT_SLACK_FRAMES` (legacy aliases).
	 * Match-linked `SSB64_NETPLAY_MATCH_INPUT_DELAY` sets committed wire delay `D` in netpeer but does **not** copy
	 * into `g_NetInputDelayFrames` here — tune slack independently from match delay.
	 * When match delay is **unset**, netpeer enforces online **minimum `D` = 1** unless `SSB64_NETPLAY_ALLOW_INPUT_DELAY_ZERO=1`;
	 * explicit **`MATCH_INPUT_DELAY=0`** still allows **`D=0`**.
	 */
	g_NetInputDelayFrames = 0;
	e = getenv("SSB64_NETPLAY_STRICT_SLACK_FRAMES");
	if ((e == NULL) || (e[0] == '\0'))
	{
		e = getenv("SSB64_NET_DELAY_FRAMES");
	}
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
	g_UseInputPrediction = TRUE;
	e = getenv("SSB64_NETPLAY_INPUT_PREDICTION");
	if ((e != NULL) && (e[0] != '\0') && (atoi(e) == 0))
	{
		g_UseInputPrediction = FALSE;
	}
}

int syNetInputGetStrictExtraSlack(void)
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

static void syNetInputResetAdmissionStatsInternal(void)
{
	sSYNetInputAdmissionP = 0ULL;
	sSYNetInputAdmissionE = 0ULL;
	sSYNetInputAdmissionW = 0ULL;
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
	case 'W':
		sSYNetInputAdmissionW++;
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
	case 'A':
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

	tot = sSYNetInputAdmissionP + sSYNetInputAdmissionE + sSYNetInputAdmissionW + sSYNetInputAdmissionS +
	      sSYNetInputAdmissionK + sSYNetInputAdmissionR;
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
	    "SSB64 NetInput: admission_summary tag=%s P=%llu E=%llu W=%llu S=%llu K=%llu R=%llu total=%llu pct_P=%.2f pct_E=%.2f pct_W=%.2f pct_S=%.2f pct_K=%.2f pct_R=%.2f\n",
	    (tag != NULL) ? tag : "?",
	    (unsigned long long)sSYNetInputAdmissionP,
	    (unsigned long long)sSYNetInputAdmissionE,
	    (unsigned long long)sSYNetInputAdmissionW,
	    (unsigned long long)sSYNetInputAdmissionS,
	    (unsigned long long)sSYNetInputAdmissionK,
	    (unsigned long long)sSYNetInputAdmissionR,
	    (unsigned long long)tot,
	    100.0 * (double)sSYNetInputAdmissionP / dtot,
	    100.0 * (double)sSYNetInputAdmissionE / dtot,
	    100.0 * (double)sSYNetInputAdmissionW / dtot,
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

int syNetInputGetInputContractTier(void)
{
	if (sSYNetInputInputContractTierEnvCache >= 0)
	{
		return sSYNetInputInputContractTierEnvCache;
	}
	sSYNetInputInputContractTierEnvCache = 2;
	return sSYNetInputInputContractTierEnvCache;
}

sb32 syNetInputAuthoritativeWireContractEnabled(void)
{
	return (syNetInputGetInputContractTier() >= 1) ? TRUE : FALSE;
}

sb32 syNetInputStrictInputContractEnabled(void)
{
	return (syNetInputGetInputContractTier() >= 2) ? TRUE : FALSE;
}

sb32 syNetInputStrictContractSkippedPublishThisPass(void)
{
	return sSYNetInputStrictContractSkippedPublish;
}

void syNetInputRefreshCachedNetplayEnvForNewMatch(void)
{
	sSYNetInputAdmissionSummaryIvCache = -999;
	sSYNetInputPredictDiagLevelCache = -999;
	sSYNetInputFrameCommitDiagLevelCache = -999;
	sSYNetInputDelaySyncDiagLevelCache = -999;
	sSYNetInputDelaySyncDiagLastCommittedD = ~(u32)0;
	sSYNetInputStrictRemoteLeadBufferEnvCache = -999;
	sSYNetInputStrictCache.is_valid = FALSE;
	sSYNetInputInputContractTierEnvCache = -1;
	sSYNetInputIngressExtraPumpsEnvCache = -999;
	sSYNetInputSessionIngressPumpsOverrideValid = FALSE;
	sSYNetInputSessionBundleRedundancyOverrideValid = FALSE;
	sSYNetInputGgpoStickDeadband = -1;
	sSYNetInputRemoteAnalogOnsetPredEnvCache = -999;
	syNetPeerResetStrictRingFuzzEnvCacheForNewMatch();
}

void syNetInputSetSessionIngressExtraPumpsOverride(s32 pumps)
{
	if (pumps < 0)
	{
		pumps = 0;
	}
	if (pumps > 4)
	{
		pumps = 4;
	}
	sSYNetInputSessionIngressPumpsOverride = pumps;
	sSYNetInputSessionIngressPumpsOverrideValid = TRUE;
}

void syNetInputSetSessionBundleRedundancyOverride(s32 redundancy)
{
	if (redundancy < 0)
	{
		redundancy = 0;
	}
	if (redundancy > 8)
	{
		redundancy = 8;
	}
	sSYNetInputSessionBundleRedundancyOverride = redundancy;
	sSYNetInputSessionBundleRedundancyOverrideValid = TRUE;
}

void syNetInputClearSessionTransportOverrides(void)
{
	sSYNetInputSessionIngressPumpsOverrideValid = FALSE;
	sSYNetInputSessionBundleRedundancyOverrideValid = FALSE;
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

#ifdef PORT
static sb32 syNetInputIsLocalDelaySlot(s32 player)
{
	s32 local_slot;
	s32 extra_slot;

	if (syNetInputCheckPlayer(player) == FALSE)
	{
		return FALSE;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return FALSE;
	}
	local_slot = syNetPeerGetLocalSimSlot();
	extra_slot = syNetPeerGetExtraLocalSenderSimSlot();
	return ((player == local_slot) || (player == extra_slot)) ? TRUE : FALSE;
}

static u32 syNetInputLocalDelayOwnerTick(u32 sample_tick)
{
	u32 d;

	d = syNetPeerGetCommittedInputDelay();
	if ((~(u32)0 - sample_tick) < d)
	{
		return ~(u32)0;
	}
	return sample_tick + d;
}

#ifdef PORT
static sb32 syNetInputFrameGameplayEquals(const SYNetInputFrame *a, const SYNetInputFrame *b);
/* One-frame taps (Start pause) live in button_tap; history rings must OR tap into stored buttons. */
static u16 syNetInputButtonsFromController(const SYController *controller)
{
	return (u16)(controller->button_hold | controller->button_tap);
}
static sb32 syNetInputMixedInputQuantizeEnabled(void);
static void syNetInputQuantizeStickToDigitalCardinals(s8 *stick_x, s8 *stick_y);
static void syNetInputSnapStickDominantAxisForPrediction(s8 *stick_x, s8 *stick_y);
static void syNetInputNoteLocalEncodingOnSample(s32 player, s8 stick_x, s8 stick_y, u32 tick);
static sb32 syNetInputTryGetRemoteConfirmedHistoryForSimTick(s32 player, u32 sim_tick, SYNetInputFrame *out_frame);
static sb32 syNetInputForkDiagWindow(u32 sim_tick, u32 *out_begin, u32 *out_end);
static void syNetInputMaybeLogForkDiagRemoteWire(s32 player, u32 wire_tick, u32 sim_tick, const SYNetInputFrame *frame,
                                                 const char *reason);
#endif

static void syNetInputStoreLocalDelayFrameFromLatch(s32 player, u32 owner_tick)
{
	SYNetInputFrame frame;
	SYController *controller;
	s32 hw_player;

	if (syNetInputIsLocalDelaySlot(player) == FALSE)
	{
		return;
	}
	hw_player = syNetPeerResolveLocalHardwareDevice(player);
	if (syNetInputCheckPlayer(hw_player) == FALSE)
	{
		return;
	}
	controller = &sSYNetInputHardwareLatch[hw_player];
	{
		s8 stick_x;
		s8 stick_y;

		stick_x = controller->stick_range.x;
		stick_y = controller->stick_range.y;
		syNetInputNoteLocalEncodingOnSample(player, stick_x, stick_y, owner_tick);
		if (syNetInputMixedInputQuantizeEnabled() != FALSE)
		{
			syNetInputQuantizeStickToDigitalCardinals(&stick_x, &stick_y);
		}
		syNetInputMakeFrame(&frame, owner_tick, syNetInputButtonsFromController(controller), stick_x, stick_y,
		                   nSYNetInputSourceLocal, FALSE);
	}
	syNetInputStoreFrame(sSYNetInputLocalDelayHistory, player, &frame);
}

static void syNetInputStageLocalDelayFramesFromLatch(u32 sample_tick)
{
	u32 owner_tick;
	s32 local_slot;
	s32 extra_slot;

	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	if (syNetInputAuthoritativeWireContractEnabled() == FALSE)
	{
		return;
	}
	owner_tick = syNetInputLocalDelayOwnerTick(sample_tick);
	local_slot = syNetPeerGetLocalSimSlot();
	extra_slot = syNetPeerGetExtraLocalSenderSimSlot();
	syNetInputStoreLocalDelayFrameFromLatch(local_slot, owner_tick);
	if (extra_slot != local_slot)
	{
		syNetInputStoreLocalDelayFrameFromLatch(extra_slot, owner_tick);
	}
}

sb32 syNetInputGetLocalDelayedFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	if (syNetInputIsLocalDelaySlot(player) == FALSE)
	{
		return FALSE;
	}
	return syNetInputGetStoredFrame(sSYNetInputLocalDelayHistory, player, tick, out_frame);
}

#define nSYNetLocalAuthoritySourceNone 0
#define nSYNetLocalAuthoritySourceLatch 1
#define nSYNetLocalAuthoritySourceDelay 2
#define nSYNetLocalAuthoritySourceTransmitted 3

static sb32 syNetInputFrameStickGameplayNeutral(const SYNetInputFrame *frame)
{
	if ((frame == NULL) || (frame->is_valid == FALSE))
	{
		return TRUE;
	}
	return ((frame->buttons == 0) && (frame->stick_x == 0) && (frame->stick_y == 0)) ? TRUE : FALSE;
}

static sb32 syNetInputAuthorityPublishLogEnabled(void)
{
	const char *e;
	static sb32 sCached = -999;

	if (sCached != -999)
	{
		return (sCached != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_AUTHORITY_PUBLISH_LOG");
	if ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0))
	{
		sCached = 1;
		return TRUE;
	}
	e = getenv("SSB64_NETPLAY_LOCAL_PUBLISH_LOG");
	sCached = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (sCached != 0) ? TRUE : FALSE;
}

static sb32 syNetInputLocalPublishLogEnabled(void)
{
	return syNetInputAuthorityPublishLogEnabled();
}

static const char *syNetInputLocalAuthoritySourceTag(s32 source_rank)
{
	switch (source_rank)
	{
	case nSYNetLocalAuthoritySourceTransmitted:
		return "transmitted";
	case nSYNetLocalAuthoritySourceDelay:
		return "delay";
	case nSYNetLocalAuthoritySourceLatch:
		return "latch";
	default:
		return "none";
	}
}

static sb32 syNetInputResolveLocalAuthorityFrameEx(s32 player, u32 tick, SYNetInputFrame *out_frame, s32 *out_source_rank)
{
	SYController *controller;
	s32 hw_player;
	s8 stick_x;
	s8 stick_y;

	if ((out_frame == NULL) || (syNetInputCheckPlayer(player) == FALSE) || (tick == 0U) ||
	    (syNetInputIsLocalDelaySlot(player) == FALSE))
	{
		return FALSE;
	}
	if (out_source_rank != NULL)
	{
		*out_source_rank = nSYNetLocalAuthoritySourceNone;
	}
	if (syNetInputGetStoredFrame(sSYNetInputTransmittedHistory, player, tick, out_frame) != FALSE)
	{
		out_frame->tick = tick;
		out_frame->source = nSYNetInputSourceLocal;
		out_frame->is_predicted = FALSE;
		out_frame->is_valid = TRUE;
		if (out_source_rank != NULL)
		{
			*out_source_rank = nSYNetLocalAuthoritySourceTransmitted;
		}
		return TRUE;
	}
	if (syNetInputGetLocalDelayedFrame(player, tick, out_frame) != FALSE)
	{
		out_frame->tick = tick;
		out_frame->source = nSYNetInputSourceLocal;
		out_frame->is_predicted = FALSE;
		out_frame->is_valid = TRUE;
		if (out_source_rank != NULL)
		{
			*out_source_rank = nSYNetLocalAuthoritySourceDelay;
		}
		return TRUE;
	}
	hw_player = syNetPeerResolveLocalHardwareDevice(player);
	if (syNetInputCheckPlayer(hw_player) == FALSE)
	{
		return FALSE;
	}
	controller = &sSYNetInputHardwareLatch[hw_player];
	stick_x = controller->stick_range.x;
	stick_y = controller->stick_range.y;
	syNetInputNoteLocalEncodingOnSample(player, stick_x, stick_y, tick);
	if (syNetInputMixedInputQuantizeEnabled() != FALSE)
	{
		syNetInputQuantizeStickToDigitalCardinals(&stick_x, &stick_y);
	}
	syNetInputMakeFrame(out_frame, tick, syNetInputButtonsFromController(controller), stick_x, stick_y,
	                   nSYNetInputSourceLocal, FALSE);
	if (out_source_rank != NULL)
	{
		*out_source_rank = nSYNetLocalAuthoritySourceLatch;
	}
	return TRUE;
}

sb32 syNetInputResolveLocalAuthorityFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	return syNetInputResolveLocalAuthorityFrameEx(player, tick, out_frame, NULL);
}

void syNetInputPromoteLocalAuthorityPublished(s32 player, u32 tick)
{
	SYNetInputFrame resolved;
	SYNetInputFrame published;
	s32 source_rank;
	sb32 had_published;

	if ((syNetInputAuthoritativeWireContractEnabled() == FALSE) || (syNetPeerIsVSSessionActive() == FALSE) ||
	    (syNetRollbackIsResimulating() != FALSE) || (tick == 0U) ||
	    (syNetInputResolveLocalAuthorityFrameEx(player, tick, &resolved, &source_rank) == FALSE))
	{
		return;
	}
	had_published = syNetInputGetHistoryFrame(player, tick, &published);
	if ((had_published == FALSE) && (syNetInputFrameStickGameplayNeutral(&resolved) != FALSE))
	{
		return;
	}
	if ((had_published != FALSE) && (syNetInputFrameGameplayEquals(&published, &resolved) != FALSE))
	{
		return;
	}
	/* Never downgrade published non-neutral sticks with latch-only neutral resolve. */
	if ((had_published != FALSE) && (syNetInputFrameStickGameplayNeutral(&resolved) != FALSE) &&
	    (syNetInputFrameStickGameplayNeutral(&published) == FALSE) && (source_rank == nSYNetLocalAuthoritySourceLatch))
	{
		return;
	}
	syNetInputStoreFrame(sSYNetInputHistory, player, &resolved);
	syNetInputStrictReadyCacheInvalidate();
	if ((syNetInputLocalPublishLogEnabled() != FALSE) &&
	    ((resolved.stick_x != 0) || (resolved.stick_y != 0) || (resolved.buttons != 0)))
	{
		port_log(
		    "SSB64 NetInput: LOCAL_PUBLISH player=%d sim_tick=%u btn=0x%04X sx=%d sy=%d source=%s\n",
		    (int)player, (unsigned int)tick, (unsigned int)resolved.buttons, (int)resolved.stick_x,
		    (int)resolved.stick_y, syNetInputLocalAuthoritySourceTag(source_rank));
	}
}

void syNetInputPromoteAllLocalAuthoritySlots(u32 tick)
{
	s32 local_slot;
	s32 extra_slot;

	if ((syNetInputAuthoritativeWireContractEnabled() == FALSE) || (syNetPeerIsVSSessionActive() == FALSE) ||
	    (syNetRollbackIsResimulating() != FALSE) || (tick == 0U))
	{
		return;
	}
	local_slot = syNetPeerGetLocalSimSlot();
	if ((local_slot >= 0) && (local_slot < MAXCONTROLLERS))
	{
		syNetInputPromoteLocalAuthorityPublished(local_slot, tick);
	}
	extra_slot = syNetPeerGetExtraLocalSenderSimSlot();
	if ((extra_slot >= 0) && (extra_slot < MAXCONTROLLERS) && (extra_slot != local_slot))
	{
		syNetInputPromoteLocalAuthorityPublished(extra_slot, tick);
	}
}

#define nSYNetRemoteAuthoritySourceNone 0
#define nSYNetRemoteAuthoritySourceWireConfirmed 1
#define nSYNetRemoteAuthoritySourceHoldLast 2

static const char *syNetInputRemoteAuthoritySourceTag(s32 source_rank)
{
	switch (source_rank)
	{
	case nSYNetRemoteAuthoritySourceWireConfirmed:
		return "wire_confirmed";
	case nSYNetRemoteAuthoritySourceHoldLast:
		return "hold_last";
	default:
		return "none";
	}
}

static sb32 syNetInputResolveRemoteHumanAuthorityFrameEx(s32 player, u32 tick, SYNetInputFrame *out_frame,
                                                        s32 *out_source_rank)
{
	SYNetInputFrame *last_confirmed;
	u16 buttons;
	s8 stick_x;
	s8 stick_y;

	if ((out_frame == NULL) || (syNetInputCheckPlayer(player) == FALSE) || (tick == 0U) ||
	    (syNetInputIsRemoteHumanSlot(player) == FALSE))
	{
		return FALSE;
	}
	if (out_source_rank != NULL)
	{
		*out_source_rank = nSYNetRemoteAuthoritySourceNone;
	}
	if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, tick, out_frame) != FALSE)
	{
		out_frame->tick = tick;
		out_frame->source = nSYNetInputSourceRemoteConfirmed;
		out_frame->is_predicted = FALSE;
		out_frame->is_valid = TRUE;
		if (out_source_rank != NULL)
		{
			*out_source_rank = nSYNetRemoteAuthoritySourceWireConfirmed;
		}
		return TRUE;
	}
	last_confirmed = &sSYNetInputSlots[player].last_confirmed;
	buttons = 0;
	stick_x = 0;
	stick_y = 0;
	if (last_confirmed->is_valid != FALSE)
	{
		buttons = last_confirmed->buttons;
		stick_x = last_confirmed->stick_x;
		stick_y = last_confirmed->stick_y;
	}
	syNetInputMakeFrame(out_frame, tick, buttons, stick_x, stick_y, nSYNetInputSourceRemoteConfirmed, FALSE);
	if (out_source_rank != NULL)
	{
		*out_source_rank = nSYNetRemoteAuthoritySourceHoldLast;
	}
	return TRUE;
}

void syNetInputPromoteRemoteHumanAuthorityPublished(s32 player, u32 tick)
{
	SYNetInputFrame resolved;
	SYNetInputFrame published;
	s32 source_rank;
	sb32 had_published;
	u32 sim_now;

	if ((syNetInputAuthoritativeWireContractEnabled() == FALSE) || (syNetPeerIsVSSessionActive() == FALSE) ||
	    (syNetRollbackIsResimulating() != FALSE) || (tick == 0U) ||
	    (syNetInputIsRemoteHumanSlot(player) == FALSE) ||
	    (syNetInputResolveRemoteHumanAuthorityFrameEx(player, tick, &resolved, &source_rank) == FALSE))
	{
		return;
	}
	had_published = syNetInputGetHistoryFrame(player, tick, &published);
	if ((had_published == FALSE) && (syNetInputFrameStickGameplayNeutral(&resolved) != FALSE))
	{
		if (syNetInputAuthorityPublishLogEnabled() != FALSE)
		{
			port_log("SSB64 NetInput: REMOTE_PUBLISH_SKIP player=%d sim_tick=%u reason=wire_neutral\n",
			         (int)player, (unsigned int)tick);
		}
		return;
	}
	if ((had_published != FALSE) && (syNetInputFrameGameplayEquals(&published, &resolved) != FALSE))
	{
		return;
	}
	/* Never downgrade published non-neutral sticks with hold-last neutral resolve. */
	if ((had_published != FALSE) && (syNetInputFrameStickGameplayNeutral(&resolved) != FALSE) &&
	    (syNetInputFrameStickGameplayNeutral(&published) == FALSE) &&
	    (source_rank == nSYNetRemoteAuthoritySourceHoldLast))
	{
		if (syNetInputAuthorityPublishLogEnabled() != FALSE)
		{
			port_log("SSB64 NetInput: REMOTE_PUBLISH_SKIP player=%d sim_tick=%u reason=downgrade_blocked\n",
			         (int)player, (unsigned int)tick);
		}
		return;
	}
	sim_now = syNetInputGetTick();
	if ((syNetInputAuthorityPublishLogEnabled() != FALSE) && (source_rank == nSYNetRemoteAuthoritySourceWireConfirmed) &&
	    (sim_now > tick) && ((sim_now - tick) > 2U))
	{
		port_log("SSB64 NetInput: REMOTE_PUBLISH_LATE player=%d sim_tick=%u sim_now=%u sx=%d sy=%d\n", (int)player,
		         (unsigned int)tick, (unsigned int)sim_now, (int)resolved.stick_x, (int)resolved.stick_y);
	}
	syNetInputStoreFrame(sSYNetInputHistory, player, &resolved);
	syNetInputStrictReadyCacheInvalidate();
	if ((syNetInputAuthorityPublishLogEnabled() != FALSE) &&
	    ((resolved.stick_x != 0) || (resolved.stick_y != 0) || (resolved.buttons != 0)))
	{
		port_log("SSB64 NetInput: REMOTE_PUBLISH player=%d sim_tick=%u sx=%d sy=%d source=%s\n", (int)player,
		         (unsigned int)tick, (int)resolved.stick_x, (int)resolved.stick_y,
		         syNetInputRemoteAuthoritySourceTag(source_rank));
	}
}

void syNetInputPromoteAllRemoteHumanAuthoritySlots(u32 tick)
{
	s32 i;
	s32 n;
	s32 slot;

	if ((syNetInputAuthoritativeWireContractEnabled() == FALSE) || (syNetPeerIsVSSessionActive() == FALSE) ||
	    (syNetRollbackIsResimulating() != FALSE) || (tick == 0U))
	{
		return;
	}
	n = syNetPeerGetRemoteHumanSlotCount();
	for (i = 0; i < n; i++)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(i, &slot) == FALSE)
		{
			continue;
		}
		syNetInputPromoteRemoteHumanAuthorityPublished(slot, tick);
	}
}

void syNetInputResolveFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);

/*
 * Drain UDP and promote wire-confirmed remote rows into published history for sim ticks leading up to `tick`.
 * Packets often land after FuncRead but before gcRunAll; without this, the host simulates with hold-last sticks.
 */
static void syNetInputPumpIngressAndPromoteRemoteThroughTick(u32 tick)
{
	s32 i;
	s32 n;
	s32 slot;
	u32 begin;
	u32 delay;
	u32 t;

	if ((syNetInputAuthoritativeWireContractEnabled() == FALSE) || (syNetPeerIsVSSessionActive() == FALSE) ||
	    (syNetRollbackIsResimulating() != FALSE) || (tick == 0U))
	{
		return;
	}
	syNetPeerPumpIngressTransport("remote_sim_ready");
	delay = syNetPeerGetCommittedInputDelay();
	begin = 1U;
	if (tick > (delay + 4U))
	{
		begin = tick - delay - 4U;
	}
	n = syNetPeerGetRemoteHumanSlotCount();
	for (i = 0; i < n; i++)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(i, &slot) == FALSE)
		{
			continue;
		}
		for (t = begin; t <= tick; t++)
		{
			syNetInputPromoteRemoteHumanAuthorityPublished(slot, t);
		}
	}
}

/*
 * Authoritative wire contract: every remote-human slot must have a strict confirmed remote row for `sim_tick`
 * before battle may consume it. Phase-lock prediction may advance shared commit while resolve still falls back to
 * hold-last/neutral — that path desyncs (client Fox @421 soak).
 */
sb32 syNetInputRemoteHumanWireReadyForSimTick(u32 sim_tick)
{
	s32 i;
	s32 n;
	s32 slot;

	if ((syNetInputAuthoritativeWireContractEnabled() == FALSE) || (syNetPeerIsVSSessionActive() == FALSE) ||
	    (syNetRollbackIsResimulating() != FALSE) || (sim_tick == 0U))
	{
		return TRUE;
	}
	n = syNetPeerGetRemoteHumanSlotCount();
	for (i = 0; i < n; i++)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(i, &slot) == FALSE)
		{
			continue;
		}
		if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(slot, sim_tick, NULL) == FALSE)
		{
			return FALSE;
		}
	}
	return TRUE;
}

sb32 syNetInputRepublishRemoteHumanControllersForTick(u32 tick)
{
	SYNetInputFrame frame;
	s32 i;
	s32 n;
	s32 slot;

	if ((syNetInputAuthoritativeWireContractEnabled() == FALSE) || (syNetPeerIsVSSessionActive() == FALSE) ||
	    (syNetRollbackIsResimulating() != FALSE) || (tick == 0U))
	{
		return TRUE;
	}
	syNetInputPumpIngressAndPromoteRemoteThroughTick(tick);
	if (syNetInputRemoteHumanWireReadyForSimTick(tick) == FALSE)
	{
		return FALSE;
	}
	n = syNetPeerGetRemoteHumanSlotCount();
	for (i = 0; i < n; i++)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(i, &slot) == FALSE)
		{
			continue;
		}
		syNetInputResolveFrame(slot, tick, &frame);
		syNetInputPublishFrame(slot, &frame);
	}
	return TRUE;
}

u32 syNetInputFindEarliestRemoteAuthorityMismatch(s32 remote_slot, u32 from_tick, u32 to_tick)
{
	SYNetInputFrame published;
	SYNetInputFrame authority;
	u32 t;

	if ((from_tick >= to_tick) || (remote_slot < 0) || (remote_slot >= MAXCONTROLLERS) ||
	    (syNetInputIsRemoteHumanSlot(remote_slot) == FALSE))
	{
		return ~(u32)0;
	}
	for (t = from_tick; t < to_tick; t++)
	{
		if (syNetInputGetHistoryFrame(remote_slot, t, &published) == FALSE)
		{
			continue;
		}
		if (syNetInputResolveRemoteHumanAuthorityFrameEx(remote_slot, t, &authority, NULL) == FALSE)
		{
			continue;
		}
		if (syNetInputFrameGameplayEquals(&published, &authority) == FALSE)
		{
			return t;
		}
	}
	return ~(u32)0;
}
#endif

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
	memset(sSYNetInputLocalEncodingWasDigital, 0, sizeof(sSYNetInputLocalEncodingWasDigital));
#endif
#ifdef PORT
	syNetInputTimelineReset();
	syNetInputResetAdmissionStatsInternal();
	sSYNetInputInputContractTierEnvCache = -1;
	sSYNetInputStrictContractSkippedPublish = FALSE;
	sSYNetInputStrictRemoteLeadBufferEnvCache = -999;
	sSYNetInputStrictCache.is_valid = FALSE;
	sSYNetInputRemoteConfirmedConflictLogsRemaining = 64U;
	sSYNetInputRemoteGapFillLogBudget = 32U;
	{
		const char *env_onset_log;

		env_onset_log = getenv("SSB64_NETPLAY_ANALOG_ONSET_LOG");
		sSYNetInputAnalogOnsetLogBudget =
		    ((env_onset_log != NULL) && (env_onset_log[0] != '\0') && (atoi(env_onset_log) != 0)) ? 8U : 0U;
	}
	{
		const char *env_mixed_log;

		env_mixed_log = getenv("SSB64_NETPLAY_MIXED_INPUT_LOG");
		sSYNetInputMixedInputLogBudget =
		    ((env_mixed_log != NULL) && (env_mixed_log[0] != '\0') && (atoi(env_mixed_log) != 0)) ? 8U : 0U;
	}
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
#ifdef PORT
		syNetInputClearFrame(&sSYNetInputSlots[player].last_non_neutral);
		sSYNetInputSlots[player].remote_encoding_was_digital = FALSE;
		sSYNetInputSlots[player].remote_encoding_grace_until_tick = 0U;
		sSYNetInputRemoteConfirmedLastWire[player] = -1;
#endif
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
			syNetInputClearFrame(&sSYNetInputTransmittedHistory[player][i]);
			syNetInputClearFrame(&sSYNetInputRemoteHistory[player][i]);
			syNetInputClearFrame(&sSYNetInputSavedHistory[player][i]);
#ifdef PORT
			syNetInputClearFrame(&sSYNetInputLocalDelayHistory[player][i]);
			sSYNetInputRemotePacketSeqHistory[player][i] = 0U;
			sSYNetInputRemotePacketSeqValid[player][i] = FALSE;
#endif
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
#ifdef PORT
	syNetRollbackClearLoadFailBattleHold();
	syNetInputRefreshCachedNetplayEnvForNewMatch();
#endif
#ifdef PORT
	{
		char *env_pn;

		env_pn = getenv("SSB64_NETPLAY_PREDICT_NEUTRAL");
		sSYNetInputPredictNeutral = ((env_pn != NULL) && (atoi(env_pn) != 0)) ? TRUE : FALSE;
	}
	syNetInputLoadExecutionDelayAndPredictionFromEnv();
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

#ifdef PORT
static sb32 syNetInputFrameGameplayEquals(const SYNetInputFrame *a, const SYNetInputFrame *b)
{
	return ((a->tick == b->tick) && (a->buttons == b->buttons) && (a->stick_x == b->stick_x) &&
	        (a->stick_y == b->stick_y))
	           ? TRUE
	           : FALSE;
}

static u32 syNetInputGgpoStickDeadband(void)
{
	const char *env;
	s32 parsed;

	if (sSYNetInputGgpoStickDeadband >= 0)
	{
		return (u32)sSYNetInputGgpoStickDeadband;
	}
	parsed = SYNETINPUT_GGPO_STICK_DEADBAND_DEFAULT;
	env = getenv("SSB64_NETPLAY_GGPO_STICK_DEADBAND");
	if ((env != NULL) && (env[0] != '\0'))
	{
		parsed = atoi(env);
		if (parsed < 0)
		{
			parsed = 0;
		}
		if (parsed > 127)
		{
			parsed = 127;
		}
	}
	sSYNetInputGgpoStickDeadband = parsed;
	return (u32)sSYNetInputGgpoStickDeadband;
}

static u32 syNetInputGgpoStickDeadbandPredict(void)
{
	const char *env;
	s32 parsed;

	if (sSYNetInputGgpoStickDeadbandPredict >= 0)
	{
		return (u32)sSYNetInputGgpoStickDeadbandPredict;
	}
	parsed = SYNETINPUT_GGPO_STICK_DEADBAND_PREDICT_DEFAULT;
	env = getenv("SSB64_NETPLAY_GGPO_STICK_DEADBAND_PREDICT");
	if ((env != NULL) && (env[0] != '\0'))
	{
		parsed = atoi(env);
		if (parsed < 0)
		{
			parsed = 0;
		}
		if (parsed > 127)
		{
			parsed = 127;
		}
	}
	sSYNetInputGgpoStickDeadbandPredict = parsed;
	return (u32)sSYNetInputGgpoStickDeadbandPredict;
}

#ifdef PORT
static s32 syNetInputEnvClampS32(s32 value, s32 min_v, s32 max_v)
{
	if (value < min_v)
	{
		return min_v;
	}
	if (value > max_v)
	{
		return max_v;
	}
	return value;
}

static u32 syNetInputAnalogOnsetStickMag(void)
{
	const char *env;
	s32 parsed;

	if (sSYNetInputAnalogOnsetStickMag >= 0)
	{
		return (u32)sSYNetInputAnalogOnsetStickMag;
	}
	parsed = SYNETINPUT_ANALOG_ONSET_STICK_MAG_DEFAULT;
	env = getenv("SSB64_NETPLAY_ANALOG_ONSET_STICK_MAG");
	if ((env != NULL) && (env[0] != '\0'))
	{
		parsed = atoi(env);
	}
	parsed = syNetInputEnvClampS32(parsed, 8, 20);
	sSYNetInputAnalogOnsetStickMag = parsed;
	return (u32)sSYNetInputAnalogOnsetStickMag;
}

static u32 syNetInputAnalogOnsetLookbackTicks(void)
{
	const char *env;
	s32 parsed;

	if (sSYNetInputAnalogOnsetLookback >= 0)
	{
		return (u32)sSYNetInputAnalogOnsetLookback;
	}
	parsed = SYNETINPUT_ANALOG_ONSET_LOOKBACK_DEFAULT;
	env = getenv("SSB64_NETPLAY_ANALOG_ONSET_LOOKBACK");
	if ((env != NULL) && (env[0] != '\0'))
	{
		parsed = atoi(env);
	}
	parsed = syNetInputEnvClampS32(parsed, 8, 120);
	sSYNetInputAnalogOnsetLookback = parsed;
	return (u32)sSYNetInputAnalogOnsetLookback;
}

static u32 syNetInputAnalogOnsetFacingThresh(void)
{
	const char *env;
	s32 parsed;

	if (sSYNetInputAnalogOnsetFacingThresh >= 0)
	{
		return (u32)sSYNetInputAnalogOnsetFacingThresh;
	}
	parsed = SYNETINPUT_ANALOG_ONSET_FACING_THRESH_DEFAULT;
	env = getenv("SSB64_NETPLAY_ANALOG_ONSET_FACING_THRESH");
	if ((env != NULL) && (env[0] != '\0'))
	{
		parsed = atoi(env);
	}
	if (parsed < 0)
	{
		parsed = 0;
	}
	if (parsed > 40)
	{
		parsed = 40;
	}
	sSYNetInputAnalogOnsetFacingThresh = parsed;
	return (u32)sSYNetInputAnalogOnsetFacingThresh;
}

static u32 syNetInputAnalogOnsetLargeDelta(void)
{
	const char *env;
	s32 parsed;

	if (sSYNetInputAnalogOnsetLargeDelta >= 0)
	{
		return (u32)sSYNetInputAnalogOnsetLargeDelta;
	}
	parsed = SYNETINPUT_ANALOG_ONSET_LARGE_DELTA_DEFAULT;
	env = getenv("SSB64_NETPLAY_ANALOG_ONSET_LARGE_DELTA");
	if ((env != NULL) && (env[0] != '\0'))
	{
		parsed = atoi(env);
	}
	if (parsed < 0)
	{
		parsed = 0;
	}
	if (parsed > 127)
	{
		parsed = 127;
	}
	sSYNetInputAnalogOnsetLargeDelta = parsed;
	return (u32)sSYNetInputAnalogOnsetLargeDelta;
}
#endif

static s32 syNetInputAbsS8Diff(s8 a, s8 b)
{
	s32 d;

	d = (s32)a - (s32)b;
	if (d < 0)
	{
		return -d;
	}
	return d;
}

static s32 syNetInputStickSign(s8 axis)
{
	if (axis > 0)
	{
		return 1;
	}
	if (axis < 0)
	{
		return -1;
	}
	return 0;
}

/* SSB digital keyboard encoding: full stick deflection on one axis. */
static sb32 syNetInputStickAxisIsDigital(s8 axis)
{
	return ((axis == 85) || (axis == -85)) ? TRUE : FALSE;
}

static sb32 syNetInputStickEncodingLooksDigital(s8 stick_x, s8 stick_y)
{
	s32 ax;
	s32 ay;

	if ((syNetInputStickAxisIsDigital(stick_x) != FALSE) || (syNetInputStickAxisIsDigital(stick_y) != FALSE))
	{
		return TRUE;
	}
	ax = syNetInputAbsS8Diff(stick_x, 0);
	ay = syNetInputAbsS8Diff(stick_y, 0);
	if ((ax == 0) && (ay == 0))
	{
		return FALSE;
	}
	/* Dominant cardinal (partial keyboard / port scaling before ±85 quantize). */
	if ((ax >= 20) && (ay <= 14))
	{
		return TRUE;
	}
	if ((ay >= 20) && (ax <= 14))
	{
		return TRUE;
	}
	/* Diagonal keyboard (e.g. sx=-23 sy=79): both axes active, neither at full analog sweep. */
	if ((ax >= 18) && (ay >= 18) && (ax <= 84) && (ay <= 84))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetInputFrameIsDigitalKeyboard(const SYNetInputFrame *frame)
{
	if (frame == NULL)
	{
		return FALSE;
	}
	return ((syNetInputStickAxisIsDigital(frame->stick_x) != FALSE) ||
	        (syNetInputStickAxisIsDigital(frame->stick_y) != FALSE))
	           ? TRUE
	           : FALSE;
}

static sb32 syNetInputFrameIsQuasiDigitalKeyboard(const SYNetInputFrame *frame)
{
	if (frame == NULL)
	{
		return FALSE;
	}
	return syNetInputStickEncodingLooksDigital(frame->stick_x, frame->stick_y);
}

static sb32 syNetInputMixedInputQuantizeEnabled(void)
{
	const char *env;
	static sb32 sCached = -1;

	if (sCached >= 0)
	{
		return (sCached != 0) ? TRUE : FALSE;
	}
	env = getenv("SSB64_NETPLAY_MIXED_INPUT_QUANTIZE");
	if ((env != NULL) && (env[0] != '\0') && (atoi(env) != 0))
	{
		sCached = 1;
	}
	else
	{
		sCached = 0;
	}
	return (sCached != 0) ? TRUE : FALSE;
}

static void syNetInputQuantizeStickToDigitalCardinals(s8 *stick_x, s8 *stick_y)
{
	s32 sign_x;
	s32 sign_y;

	if ((stick_x == NULL) || (stick_y == NULL))
	{
		return;
	}
	if (syNetInputStickEncodingLooksDigital(*stick_x, *stick_y) == FALSE)
	{
		return;
	}
	sign_x = syNetInputStickSign(*stick_x);
	sign_y = syNetInputStickSign(*stick_y);
	if (sign_x != 0)
	{
		*stick_x = (s8)(sign_x * 85);
	}
	else
	{
		*stick_x = 0;
	}
	if (sign_y != 0)
	{
		*stick_y = (s8)(sign_y * 85);
	}
	else
	{
		*stick_y = 0;
	}
}

/*
 * Remote prediction only: legacy hook — promotion from partial analog to ±85 is disabled.
 * Confirmed ±85 cardinals are already on the wire; partial sticks stay at wire magnitude.
 */
static void syNetInputSnapStickDominantAxisForPrediction(s8 *stick_x, s8 *stick_y)
{
	(void)stick_x;
	(void)stick_y;
}

static sb32 syNetInputRemoteRecentEncodingIsDigital(s32 player, u32 tick, u32 lookback_frames)
{
	SYNetInputFrame frame;
	u32 samples;
	u32 digital_samples;
	u32 t;
	u32 oldest;

	if ((lookback_frames == 0U) || (syNetInputCheckPlayer(player) == FALSE))
	{
		return FALSE;
	}
	oldest = (tick > lookback_frames) ? (tick - lookback_frames) : 0U;
	samples = 0U;
	digital_samples = 0U;
	for (t = tick; t > oldest; t--)
	{
		if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, t - 1U, &frame) == FALSE)
		{
			break;
		}
		samples++;
		if (syNetInputFrameIsDigitalKeyboard(&frame) != FALSE)
		{
			digital_samples++;
		}
	}
	return ((samples > 0U) && ((digital_samples * 2U) >= samples)) ? TRUE : FALSE;
}

static void syNetInputNoteRemoteEncodingOnConfirm(s32 player, const SYNetInputFrame *frame)
{
	sb32 now_digital;

	if ((frame == NULL) || (syNetInputCheckPlayer(player) == FALSE))
	{
		return;
	}
	now_digital = syNetInputFrameIsDigitalKeyboard(frame);
	if ((sSYNetInputSlots[player].remote_encoding_was_digital != FALSE) != (now_digital != FALSE))
	{
		syNetInputClearFrame(&sSYNetInputSlots[player].last_non_neutral);
		if (frame->tick < ~(u32)0 - 3U)
		{
			sSYNetInputSlots[player].remote_encoding_grace_until_tick = frame->tick + 3U;
		}
		else
		{
			sSYNetInputSlots[player].remote_encoding_grace_until_tick = ~(u32)0;
		}
		if (sSYNetInputMixedInputLogBudget > 0U)
		{
			port_log(
			    "SSB64 NetInput: remote_encoding_switch player=%d tick=%u digital=%d sx=%d sy=%d grace_until=%u\n",
			    (int)player,
			    frame->tick,
			    (now_digital != FALSE) ? 1 : 0,
			    frame->stick_x,
			    frame->stick_y,
			    sSYNetInputSlots[player].remote_encoding_grace_until_tick);
			sSYNetInputMixedInputLogBudget--;
		}
	}
	sSYNetInputSlots[player].remote_encoding_was_digital = now_digital;
}

static void syNetInputNoteLocalEncodingOnSample(s32 player, s8 stick_x, s8 stick_y, u32 tick)
{
	sb32 now_digital;

	if (syNetInputCheckPlayer(player) == FALSE)
	{
		return;
	}
	now_digital = syNetInputStickEncodingLooksDigital(stick_x, stick_y);
	if ((sSYNetInputLocalEncodingWasDigital[player] != FALSE) != (now_digital != FALSE))
	{
		if (sSYNetInputMixedInputLogBudget > 0U)
		{
			port_log(
			    "SSB64 NetInput: local_encoding_switch player=%d tick=%u digital=%d sx=%d sy=%d\n",
			    (int)player,
			    tick,
			    (now_digital != FALSE) ? 1 : 0,
			    stick_x,
			    stick_y);
			sSYNetInputMixedInputLogBudget--;
		}
	}
	sSYNetInputLocalEncodingWasDigital[player] = now_digital;
}

static sb32 syNetInputFrameSticksNearNeutral(const SYNetInputFrame *frame)
{
	if (frame == NULL)
	{
		return FALSE;
	}
	return ((syNetInputAbsS8Diff(frame->stick_x, 0) <= (s32)syNetInputGgpoStickDeadbandPredict()) &&
	        (syNetInputAbsS8Diff(frame->stick_y, 0) <= (s32)syNetInputGgpoStickDeadbandPredict()))
	           ? TRUE
	           : FALSE;
}

static sb32 syNetInputFrameSticksNearNeutralWithDeadband(const SYNetInputFrame *frame, u32 deadband)
{
	if (frame == NULL)
	{
		return FALSE;
	}
	return ((syNetInputAbsS8Diff(frame->stick_x, 0) <= (s32)deadband) &&
	        (syNetInputAbsS8Diff(frame->stick_y, 0) <= (s32)deadband))
	           ? TRUE
	           : FALSE;
}

#ifdef PORT
static sb32 syNetInputStickLooksAnalog(s8 stick_x, s8 stick_y);
static sb32 syNetInputTryGetRemoteConfirmedHistoryForSimTick(s32 player, u32 sim_tick, SYNetInputFrame *out_frame);
static sb32 syNetInputTryGetRemoteHistoryForSimTick(s32 player, u32 sim_tick, SYNetInputFrame *out_frame);
static sb32 syNetInputFrameIsRemoteConfirmed(const SYNetInputFrame *frame);

static s8 syNetInputClampStickAxisMag(s8 axis, s32 floor_mag, s32 ceil_mag)
{
	s32 sign;
	s32 mag;

	if (axis == 0)
	{
		return 0;
	}
	sign = syNetInputStickSign(axis);
	mag = syNetInputAbsS8Diff(axis, 0);
	if (mag < floor_mag)
	{
		mag = floor_mag;
	}
	if (mag > ceil_mag)
	{
		mag = ceil_mag;
	}
	return (s8)(sign * mag);
}

static void syNetInputApplyAnalogOnsetStick(s8 *stick_x, s8 *stick_y, const SYNetInputFrame *src, s32 floor_mag,
                                            s32 ceil_mag)
{
	if ((stick_x == NULL) || (stick_y == NULL) || (src == NULL) || (floor_mag <= 0))
	{
		return;
	}
	if (ceil_mag < floor_mag)
	{
		ceil_mag = floor_mag;
	}
	*stick_x = syNetInputClampStickAxisMag(src->stick_x, floor_mag, ceil_mag);
	*stick_y = syNetInputClampStickAxisMag(src->stick_y, floor_mag, ceil_mag);
}

static sb32 syNetInputStickSameAnalogIntent(s8 ax, s8 ay, s8 bx, s8 by)
{
	s32 min_active;

	if ((syNetInputStickLooksAnalog(ax, ay) == FALSE) || (syNetInputStickLooksAnalog(bx, by) == FALSE))
	{
		return FALSE;
	}
	min_active = 8;
	if ((syNetInputAbsS8Diff(ax, 0) > min_active) || (syNetInputAbsS8Diff(bx, 0) > min_active))
	{
		if ((syNetInputAbsS8Diff(ax, 0) > min_active) && (syNetInputAbsS8Diff(bx, 0) > min_active) &&
		    (syNetInputStickSign(ax) != syNetInputStickSign(bx)))
		{
			return FALSE;
		}
	}
	if ((syNetInputAbsS8Diff(ay, 0) > min_active) || (syNetInputAbsS8Diff(by, 0) > min_active))
	{
		if ((syNetInputAbsS8Diff(ay, 0) > min_active) && (syNetInputAbsS8Diff(by, 0) > min_active) &&
		    (syNetInputStickSign(ay) != syNetInputStickSign(by)))
		{
			return FALSE;
		}
	}
	return TRUE;
}

static u32 syNetInputAnalogOnsetWirePeekAheadFrames(void)
{
	const char *env;
	static sb32 sCached = -999;
	s32 parsed;

	if (sCached >= 0)
	{
		return (u32)sCached;
	}
	parsed = (s32)SYNETINPUT_ANALOG_ONSET_WIRE_PEEK_AHEAD_DEFAULT;
	env = getenv("SSB64_NETPLAY_ANALOG_ONSET_WIRE_PEEK_AHEAD");
	if ((env != NULL) && (env[0] != '\0'))
	{
		parsed = atoi(env);
	}
	if (parsed < 0)
	{
		parsed = 0;
	}
	if (parsed > 16)
	{
		parsed = 16;
	}
	sCached = parsed;
	return (u32)parsed;
}

static sb32 syNetInputTryPeekRemoteAnalogStickAtTick(s32 player, u32 t, SYNetInputFrame *out_frame)
{
	SYNetInputFrame wire_row;

	if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, t, out_frame) != FALSE)
	{
		if (syNetInputStickLooksAnalog(out_frame->stick_x, out_frame->stick_y) != FALSE)
		{
			return TRUE;
		}
	}
	if ((syNetInputTryGetRemoteHistoryForSimTick(player, t, &wire_row) != FALSE) &&
	    (syNetInputStickLooksAnalog(wire_row.stick_x, wire_row.stick_y) != FALSE))
	{
		if (out_frame != NULL)
		{
			*out_frame = wire_row;
		}
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetInputTryPeekRemoteAnalogForOnset(s32 player, u32 tick, u32 max_lookback, SYNetInputFrame *out_frame)
{
	u32 t;
	u32 oldest;
	u32 newest;
	u32 peek_ahead;

	if ((out_frame == NULL) || (tick == 0U) || (max_lookback == 0U))
	{
		return FALSE;
	}
	peek_ahead = syNetInputAnalogOnsetWirePeekAheadFrames();
	newest = tick + peek_ahead;
	for (t = tick; t <= newest; t++)
	{
		if (syNetInputTryPeekRemoteAnalogStickAtTick(player, t, out_frame) != FALSE)
		{
			return TRUE;
		}
	}
	oldest = tick;
	if (tick > max_lookback)
	{
		oldest = tick - max_lookback;
	}
	for (t = tick - 1U; t >= oldest; t--)
	{
		if (syNetInputTryPeekRemoteAnalogStickAtTick(player, t, out_frame) != FALSE)
		{
			return TRUE;
		}
		if (t == oldest)
		{
			break;
		}
	}
	return FALSE;
}

static void syNetInputNoteRemoteNonNeutralStick(s32 player, const SYNetInputFrame *frame)
{
	if ((frame == NULL) || (syNetInputCheckPlayer(player) == FALSE) || (frame->is_valid == FALSE) ||
	    (syNetInputFrameSticksNearNeutral(frame) != FALSE))
	{
		return;
	}
	sSYNetInputSlots[player].last_non_neutral = *frame;
}
#endif

static sb32 syNetInputDigitalStickSameCardinal(const SYNetInputFrame *a, const SYNetInputFrame *b)
{
	if ((a == NULL) || (b == NULL))
	{
		return FALSE;
	}
	if ((syNetInputStickAxisIsDigital(a->stick_x) != FALSE) && (a->stick_x == b->stick_x))
	{
		return TRUE;
	}
	if ((syNetInputStickAxisIsDigital(a->stick_y) != FALSE) && (a->stick_y == b->stick_y))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetInputGgpoDigitalTapPatchEnabled(void)
{
	const char *env;
	static sb32 sCached = -1;

	if (sCached >= 0)
	{
		return (sCached != 0) ? TRUE : FALSE;
	}
	env = getenv("SSB64_NETPLAY_GGPO_DIGITAL_TAP_PATCH");
	if ((env != NULL) && (env[0] != '\0') && (atoi(env) == 0))
	{
		sCached = 0;
	}
	else
	{
		sCached = 1;
	}
	return (sCached != 0) ? TRUE : FALSE;
}

/*
 * One-frame digital keyboard taps (jump tap, release edge) under delay: patch published row without
 * a full rollback when neighbors on the wire are neutral and the pulse is not sustained.
 */
sb32 syNetInputShouldPatchDigitalTapWithoutRollback(s32 player, u32 sim_tick, const SYNetInputFrame *published,
                                                    const SYNetInputFrame *remote)
{
	SYNetInputFrame adj;
	u32 t;

	if ((published == NULL) || (remote == NULL) || (sim_tick == 0U))
	{
		return FALSE;
	}
	if (syNetInputGgpoDigitalTapPatchEnabled() == FALSE)
	{
		return FALSE;
	}
	if ((syNetRollbackIsActive() != FALSE) && (syNetRollbackPredictionRecoveryEnabled() == FALSE))
	{
		return FALSE;
	}
	if (published->is_predicted == FALSE)
	{
		return FALSE;
	}
	/* Predicted neutral, confirmed single-frame cardinal (common jump / tap). */
	if ((syNetInputFrameSticksNearNeutral(published) != FALSE) &&
	    (syNetInputFrameIsDigitalKeyboard(remote) != FALSE))
	{
		if (sim_tick < ~(u32)0)
		{
			t = sim_tick + 1U;
			if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, t, &adj) != FALSE)
			{
				if ((syNetInputFrameIsDigitalKeyboard(&adj) != FALSE) &&
				    (syNetInputDigitalStickSameCardinal(remote, &adj) != FALSE))
				{
					return FALSE;
				}
			}
		}
		if (sim_tick > 0U)
		{
			t = sim_tick - 1U;
			if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, t, &adj) != FALSE)
			{
				if ((syNetInputFrameIsDigitalKeyboard(&adj) != FALSE) &&
				    (syNetInputDigitalStickSameCardinal(remote, &adj) != FALSE))
				{
					return FALSE;
				}
			}
		}
		if (sim_tick < ~(u32)0)
		{
			t = sim_tick + 1U;
			if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, t, &adj) != FALSE)
			{
				if (syNetInputFrameSticksNearNeutral(&adj) != FALSE)
				{
					return TRUE;
				}
			}
		}
		if (sim_tick > 0U)
		{
			t = sim_tick - 1U;
			if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, t, &adj) != FALSE)
			{
				if (syNetInputFrameSticksNearNeutral(&adj) != FALSE)
				{
					return TRUE;
				}
			}
		}
		return FALSE;
	}
	/* Hold-last predicted cardinal, confirmed release to neutral. */
	if ((syNetInputFrameIsDigitalKeyboard(published) != FALSE) &&
	    (syNetInputFrameSticksNearNeutral(remote) != FALSE))
	{
		if (sim_tick > 0U)
		{
			t = sim_tick - 1U;
			if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, t, &adj) != FALSE)
			{
				if (syNetInputDigitalStickSameCardinal(&adj, published) == FALSE)
				{
					return FALSE;
				}
			}
			else
			{
				return FALSE;
			}
		}
		else
		{
			return FALSE;
		}
		if (sim_tick < ~(u32)0)
		{
			t = sim_tick + 1U;
			if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, t, &adj) != FALSE)
			{
				if (syNetInputFrameSticksNearNeutral(&adj) != FALSE)
				{
					return TRUE;
				}
			}
		}
		return FALSE;
	}
	return FALSE;
}

#define SYNETINPUT_ANALOG_PRED_DECAY_TICKS_DEFAULT 3U
#define SYNETINPUT_ANALOG_PRED_DECAY_NUM 3
#define SYNETINPUT_ANALOG_PRED_DECAY_DEN 4
#define SYNETINPUT_ANALOG_PRED_MIN_MAG_DEFAULT 12U

static u32 syNetInputAnalogPredDecayTicks(void)
{
	const char *env;
	static sb32 sCached = -1;
	s32 v;

	if (sCached >= 0)
	{
		return (u32)sCached;
	}
	v = (s32)SYNETINPUT_ANALOG_PRED_DECAY_TICKS_DEFAULT;
	env = getenv("SSB64_NETPLAY_ANALOG_PRED_DECAY_TICKS");
	if ((env != NULL) && (env[0] != '\0'))
	{
		v = atoi(env);
	}
	if (v < 0)
	{
		v = 0;
	}
	sCached = v;
	return (u32)v;
}

static u32 syNetInputAnalogPredMinMag(void)
{
	const char *env;
	static sb32 sCached = -1;
	s32 v;

	if (sCached >= 0)
	{
		return (u32)sCached;
	}
	v = (s32)SYNETINPUT_ANALOG_PRED_MIN_MAG_DEFAULT;
	env = getenv("SSB64_NETPLAY_ANALOG_PRED_MIN_MAG");
	if ((env != NULL) && (env[0] != '\0'))
	{
		v = atoi(env);
	}
	if (v < 0)
	{
		v = 0;
	}
	sCached = v;
	return (u32)v;
}

static sb32 syNetInputStickLooksAnalog(s8 stick_x, s8 stick_y)
{
	u32 min_mag;

	min_mag = syNetInputAnalogPredMinMag();
	if ((syNetInputAbsS8Diff(stick_x, 0) <= (s32)min_mag) && (syNetInputAbsS8Diff(stick_y, 0) <= (s32)min_mag))
	{
		return FALSE;
	}
	if ((syNetInputStickAxisIsDigital(stick_x) != FALSE) || (syNetInputStickAxisIsDigital(stick_y) != FALSE))
	{
		return FALSE;
	}
	return TRUE;
}

static void syNetInputApplyAnalogPredictionDecay(s8 *stick_x, s8 *stick_y, u32 lead_ticks)
{
	u32 decay_ticks;
	u32 min_mag;
	u32 age;
	u32 i;
	s32 sx;
	s32 sy;

	if ((stick_x == NULL) || (stick_y == NULL))
	{
		return;
	}
	decay_ticks = syNetInputAnalogPredDecayTicks();
	if ((decay_ticks == 0U) || (lead_ticks <= decay_ticks))
	{
		return;
	}
	age = lead_ticks - decay_ticks;
	sx = (s32)*stick_x;
	sy = (s32)*stick_y;
	for (i = 0U; i < age; i++)
	{
		sx = (sx * SYNETINPUT_ANALOG_PRED_DECAY_NUM) / SYNETINPUT_ANALOG_PRED_DECAY_DEN;
		sy = (sy * SYNETINPUT_ANALOG_PRED_DECAY_NUM) / SYNETINPUT_ANALOG_PRED_DECAY_DEN;
	}
	min_mag = syNetInputAnalogPredMinMag();
	if (syNetInputAbsS8Diff((s8)sx, 0) < (s32)min_mag)
	{
		sx = 0;
	}
	if (syNetInputAbsS8Diff((s8)sy, 0) < (s32)min_mag)
	{
		sy = 0;
	}
	*stick_x = (s8)sx;
	*stick_y = (s8)sy;
}

sb32 syNetInputGgpoStickNeutralAnalogFlip(const SYNetInputFrame *published, const SYNetInputFrame *remote)
{
	sb32 pub_neutral;
	sb32 rem_neutral;

	if ((published == NULL) || (remote == NULL))
	{
		return FALSE;
	}
	pub_neutral = syNetInputFrameSticksNearNeutral(published);
	rem_neutral = syNetInputFrameSticksNearNeutral(remote);
	return (pub_neutral != rem_neutral) ? TRUE : FALSE;
}

/*
 * Predicted ±85 on one axis vs remote neutral/partial on that axis — heuristic promotion artifact,
 * not a committed digital jump. Suppresses oversized GGPO resim when promotion already slipped through.
 */
static sb32 syNetInputGgpoFalseDigitalHeuristicMismatch(const SYNetInputFrame *published,
                                                        const SYNetInputFrame *remote)
{
	s32 weak_thresh;

	if ((published == NULL) || (remote == NULL))
	{
		return FALSE;
	}
	weak_thresh = 25;
	if ((syNetInputStickAxisIsDigital(published->stick_y) != FALSE) &&
	    (syNetInputStickAxisIsDigital(remote->stick_y) == FALSE) &&
	    (syNetInputAbsS8Diff(published->stick_x, 0) <= 14) && (syNetInputAbsS8Diff(remote->stick_x, 0) <= 14) &&
	    (syNetInputAbsS8Diff(remote->stick_y, 0) <= weak_thresh))
	{
		return TRUE;
	}
	if ((syNetInputStickAxisIsDigital(published->stick_x) != FALSE) &&
	    (syNetInputStickAxisIsDigital(remote->stick_x) == FALSE) &&
	    (syNetInputAbsS8Diff(published->stick_y, 0) <= 14) && (syNetInputAbsS8Diff(remote->stick_y, 0) <= 14) &&
	    (syNetInputAbsS8Diff(remote->stick_x, 0) <= weak_thresh))
	{
		return TRUE;
	}
	return FALSE;
}

sb32 syNetInputGameplayCorrectionIsSignificantEx(const SYNetInputFrame *old, const SYNetInputFrame *new,
                                                sb32 correction_is_predicted)
{
	u32 deadband;
	u32 facing_thresh;
	u32 large_delta;

	if ((old == NULL) || (new == NULL))
	{
		return TRUE;
	}
	if (old->buttons != new->buttons)
	{
		return TRUE;
	}
	if ((correction_is_predicted != FALSE) &&
	    (syNetInputGgpoFalseDigitalHeuristicMismatch(old, new) != FALSE))
	{
		return FALSE;
	}
	if (correction_is_predicted != FALSE)
	{
		deadband = syNetInputGgpoStickDeadbandPredict();
	}
	else
	{
		deadband = syNetInputGgpoStickDeadband();
	}
#ifdef PORT
	facing_thresh = syNetInputAnalogOnsetFacingThresh();
	large_delta = syNetInputAnalogOnsetLargeDelta();
	if (syNetInputFrameSticksNearNeutralWithDeadband(old, deadband) != FALSE)
	{
		if (syNetInputFrameSticksNearNeutralWithDeadband(new, deadband) == FALSE)
		{
			return TRUE;
		}
		if ((syNetInputAbsS8Diff(new->stick_x, 0) > 25) || (syNetInputAbsS8Diff(new->stick_y, 0) > 25))
		{
			return TRUE;
		}
	}
	if ((syNetInputAbsS8Diff(old->stick_x, 0) > (s32)facing_thresh) &&
	    (syNetInputAbsS8Diff(new->stick_x, 0) > (s32)facing_thresh) &&
	    (syNetInputStickSign(old->stick_x) != syNetInputStickSign(new->stick_x)))
	{
		return TRUE;
	}
	if (syNetInputAbsS8Diff(old->stick_x, new->stick_x) > (s32)large_delta)
	{
		return TRUE;
	}
	if (syNetInputAbsS8Diff(old->stick_y, new->stick_y) > (s32)large_delta)
	{
		return TRUE;
	}
#endif
	if (deadband == 0U)
	{
		return ((old->stick_x != new->stick_x) || (old->stick_y != new->stick_y)) ? TRUE : FALSE;
	}
	if (syNetInputAbsS8Diff(old->stick_x, new->stick_x) > (s32)deadband)
	{
		return TRUE;
	}
	if (syNetInputAbsS8Diff(old->stick_y, new->stick_y) > (s32)deadband)
	{
		return TRUE;
	}
	if ((correction_is_predicted != FALSE) &&
	    (syNetInputFrameSticksNearNeutralWithDeadband(old, deadband) == FALSE) &&
	    (syNetInputFrameSticksNearNeutralWithDeadband(new, deadband) == FALSE) &&
	    (syNetInputStickSameAnalogIntent(old->stick_x, old->stick_y, new->stick_x, new->stick_y) != FALSE))
	{
		u32 same_intent_tol;

		same_intent_tol = SYNETINPUT_ANALOG_SAME_INTENT_TOLERANCE;
		if ((syNetInputAbsS8Diff(old->stick_x, new->stick_x) <= (s32)same_intent_tol) &&
		    (syNetInputAbsS8Diff(old->stick_y, new->stick_y) <= (s32)same_intent_tol))
		{
			return FALSE;
		}
	}
	return FALSE;
}

sb32 syNetInputGameplayCorrectionIsSignificant(const SYNetInputFrame *old, const SYNetInputFrame *new)
{
	return syNetInputGameplayCorrectionIsSignificantEx(old, new, FALSE);
}

#ifdef PORT
static sb32 syNetInputRemoteHumanAuthoritativeOnly(void);
static void syNetInputResolveRemoteHumanAuthoritativeFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);
static void syNetInputMaybeStorePredictedOverlayForDiag(s32 player, u32 tick);
#endif

/*
 * Predicted analog onset ahead of wire delay: published history may carry peek/onset sticks while the remote
 * ring row is still neutral. Defer rollback until wire confirms or peek shows a conflicting value.
 */
sb32 syNetInputShouldDeferPredictedAnalogCorrection(s32 player, u32 sim_tick, const SYNetInputFrame *published,
                                                    const SYNetInputFrame *remote)
{
	SYNetInputFrame peek;
	const SYNetInputFrame *last_nn;

	if ((published == NULL) || (remote == NULL) || (sim_tick == 0U) ||
	    (syNetInputIsRemoteHumanSlot(player) == FALSE))
	{
		return FALSE;
	}
	if ((published->is_predicted == FALSE) && (published->source != nSYNetInputSourceRemotePredicted))
	{
		return FALSE;
	}
	if (syNetInputStickLooksAnalog(published->stick_x, published->stick_y) == FALSE)
	{
		return FALSE;
	}
	if (syNetInputFrameSticksNearNeutral(remote) == FALSE)
	{
		return FALSE;
	}
	last_nn = &sSYNetInputSlots[player].last_non_neutral;
	if ((last_nn->is_valid != FALSE) && (syNetInputStickLooksAnalog(last_nn->stick_x, last_nn->stick_y) != FALSE) &&
	    (syNetInputStickSameAnalogIntent(published->stick_x, published->stick_y, last_nn->stick_x, last_nn->stick_y) !=
	     FALSE))
	{
		return TRUE;
	}
	if (syNetInputTryPeekRemoteAnalogForOnset(player, sim_tick, SYNETINPUT_ANALOG_ONSET_WIRE_PEEK_FRAMES, &peek) !=
	    FALSE)
	{
		if (syNetInputStickSameAnalogIntent(published->stick_x, published->stick_y, peek.stick_x, peek.stick_y) !=
		    FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

/* Strict wire authority: real INPUT packets only (not hold-last gap fill). */
static sb32 syNetInputFrameIsRemoteStrictConfirmed(const SYNetInputFrame *frame)
{
	return ((frame != NULL) && (frame->is_valid != FALSE) &&
	        (frame->source == nSYNetInputSourceRemoteConfirmed) && (frame->is_predicted == FALSE))
	           ? TRUE
	           : FALSE;
}

/* Admission / stall pacing: gap-filled hold-last rows count as present on the wire ring. */
static sb32 syNetInputFrameIsRemoteConfirmed(const SYNetInputFrame *frame)
{
	return ((frame != NULL) && (frame->is_valid != FALSE) &&
	        ((frame->source == nSYNetInputSourceRemoteConfirmed) ||
	         (frame->source == nSYNetInputSourceRemoteGapFilled)) &&
	        (frame->is_predicted == FALSE))
	           ? TRUE
	           : FALSE;
}

static sb32 syNetInputFrameIsRemoteGapFilled(const SYNetInputFrame *frame)
{
	return ((frame != NULL) && (frame->is_valid != FALSE) &&
	        (frame->source == nSYNetInputSourceRemoteGapFilled) && (frame->is_predicted == FALSE))
	           ? TRUE
	           : FALSE;
}

void syNetInputNoteTransmittedSimFrame(s32 player, const SYNetInputFrame *frame)
{
	SYNetInputFrame store;
	SYNetInputFrame prior;
	sb32 had_prior;

	if ((frame == NULL) || (syNetInputCheckPlayer(player) == FALSE) || (frame->tick == 0U))
	{
		return;
	}
	had_prior = syNetInputGetStoredFrame(sSYNetInputTransmittedHistory, player, frame->tick, &prior);
	store = *frame;
	syNetInputStoreFrame(sSYNetInputTransmittedHistory, player, &store);
	if ((syNetInputAuthoritativeWireContractEnabled() != FALSE) && (syNetInputIsLocalDelaySlot(player) != FALSE))
	{
		syNetInputPromoteLocalAuthorityPublished(player, frame->tick);
		if ((had_prior != FALSE) && (syNetInputFrameGameplayEquals(&prior, frame) == FALSE))
		{
			syNetRollbackNotifyLocalAuthorityTransmitRevision(player, frame->tick);
		}
		return;
	}
	if ((had_prior != FALSE) && (syNetInputFrameGameplayEquals(&prior, frame) == FALSE))
	{
		SYNetInputFrame patch;

		patch = store;
		patch.tick = frame->tick;
		patch.source = nSYNetInputSourceLocal;
		patch.is_predicted = FALSE;
		syNetInputStoreFrame(sSYNetInputHistory, player, &patch);
		syNetInputStrictReadyCacheInvalidate();
		syNetRollbackNotifyLocalAuthorityTransmitRevision(player, frame->tick);
	}
}

static sb32 syNetInputPatchPublishLogEnabled(void)
{
	const char *e;
	static sb32 sCached = -999;

	if (sCached != -999)
	{
		return (sCached != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_PATCH_PUBLISH_LOG");
	sCached = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (sCached != 0) ? TRUE : FALSE;
}

static void syNetInputLogInputGameplayRow(const char *tag, s32 player, u32 sim_tick, u32 wire_tick, const char *reason,
                                           const SYNetInputFrame *pub, const SYNetInputFrame *wire)
{
	if (syNetInputPatchPublishLogEnabled() == FALSE)
	{
		return;
	}
	if ((pub == NULL) || (wire == NULL))
	{
		return;
	}
	port_log(
	    "SSB64 NetInput: %s player=%d sim_tick=%u wire_tick=%u reason=%s "
	    "pub_btn=0x%04X pub_sx=%d pub_sy=%d pub_pred=%u | wire_btn=0x%04X wire_sx=%d wire_sy=%d\n",
	    tag, (int)player, sim_tick, wire_tick, reason, (unsigned int)pub->buttons, (int)pub->stick_x, (int)pub->stick_y,
	    (unsigned int)pub->is_predicted, (unsigned int)wire->buttons, (int)wire->stick_x, (int)wire->stick_y);
}

static void syNetInputPatchPublishedFromRemoteConfirmedReason(s32 player, u32 wire_tick,
                                                              const SYNetInputFrame *confirmed, const char *reason)
{
	u32 sim_tick;
	SYNetInputFrame published;
	SYNetInputFrame store;
	SYNetInputFrame wire_view;

	if ((confirmed == NULL) || (syNetInputCheckPlayer(player) == FALSE) ||
	    (syNetInputFrameIsRemoteStrictConfirmed(confirmed) == FALSE))
	{
		return;
	}
	sim_tick = syNetPeerDelaySimTickFromWire(wire_tick);
	if (sim_tick == 0U)
	{
		return;
	}
	if ((syNetInputIsRemoteHumanSlot(player) != FALSE) &&
	    (syNetInputAuthoritativeWireContractEnabled() != FALSE))
	{
		syNetInputPromoteRemoteHumanAuthorityPublished(player, sim_tick);
	}
	if (syNetInputGetHistoryFrame(player, sim_tick, &published) == FALSE)
	{
		memset(&published, 0, sizeof(published));
	}
	if ((published.is_valid != FALSE) && (syNetInputFrameGameplayEquals(&published, confirmed) != FALSE))
	{
		return;
	}
	wire_view = *confirmed;
	wire_view.tick = sim_tick;
	if (syNetInputPatchPublishLogEnabled() != FALSE)
	{
		/* Remote confirmed only: pub_* is pre-store published row; wire_* is incoming confirmed row. */
		port_log(
		    "SSB64 NetInput: patch_publish player=%d sim_tick=%u wire_tick=%u reason=%s "
		    "pub_btn=0x%04X pub_sx=%d pub_sy=%d pub_pred=%u | wire_btn=0x%04X wire_sx=%d wire_sy=%d\n",
		    (int)player, sim_tick, wire_tick, (reason != NULL) ? reason : "unknown",
		    (unsigned int)published.buttons, (int)published.stick_x, (int)published.stick_y,
		    (unsigned int)published.is_predicted, (unsigned int)wire_view.buttons, (int)wire_view.stick_x,
		    (int)wire_view.stick_y);
	}
	store = wire_view;
	store.source = nSYNetInputSourceRemoteConfirmed;
	store.is_predicted = FALSE;
	syNetInputStoreFrame(sSYNetInputHistory, player, &store);
	syNetInputStrictReadyCacheInvalidate();
}

void syNetInputPatchPublishedFromRemoteConfirmed(s32 player, u32 wire_tick, const SYNetInputFrame *confirmed)
{
	syNetInputPatchPublishedFromRemoteConfirmedReason(player, wire_tick, confirmed, "unknown");
}

static void syNetInputClearRemotePacketSeq(s32 player, u32 wire_tick)
{
	u32 index;

	if (syNetInputCheckPlayer(player) == FALSE)
	{
		return;
	}
	index = wire_tick % SYNETINPUT_HISTORY_LENGTH;
	sSYNetInputRemotePacketSeqValid[player][index] = FALSE;
	sSYNetInputRemotePacketSeqHistory[player][index] = 0U;
}

static void syNetInputStoreRemotePacketSeq(s32 player, u32 wire_tick, u32 packet_seq)
{
	u32 index;

	if (syNetInputCheckPlayer(player) == FALSE)
	{
		return;
	}
	index = wire_tick % SYNETINPUT_HISTORY_LENGTH;
	sSYNetInputRemotePacketSeqHistory[player][index] = packet_seq;
	sSYNetInputRemotePacketSeqValid[player][index] = TRUE;
}

static sb32 syNetInputGetRemotePacketSeq(s32 player, u32 wire_tick, u32 *out_packet_seq)
{
	u32 index;
	SYNetInputFrame frame;

	if (syNetInputCheckPlayer(player) == FALSE)
	{
		return FALSE;
	}
	index = wire_tick % SYNETINPUT_HISTORY_LENGTH;
	if (sSYNetInputRemotePacketSeqValid[player][index] == FALSE)
	{
		return FALSE;
	}
	if (syNetInputGetStoredFrame(sSYNetInputRemoteHistory, player, wire_tick, &frame) == FALSE)
	{
		return FALSE;
	}
	if (out_packet_seq != NULL)
	{
		*out_packet_seq = sSYNetInputRemotePacketSeqHistory[player][index];
	}
	return TRUE;
}

static sb32 syNetInputPacketSeqIsNewerOrEqual(u32 packet_seq, u32 existing_packet_seq)
{
	if (packet_seq == existing_packet_seq)
	{
		return TRUE;
	}
	return ((packet_seq - existing_packet_seq) < 0x80000000U) ? TRUE : FALSE;
}

static void syNetInputStoreRemoteConfirmedFrame(s32 player, const SYNetInputFrame *frame)
{
	SYNetInputFrame store;

	if ((frame == NULL) || (syNetInputCheckPlayer(player) == FALSE))
	{
		return;
	}
	store = *frame;
	syNetInputStoreFrame(sSYNetInputRemoteHistory, player, &store);
	if ((sSYNetInputRemoteConfirmedLastWire[player] < 0) ||
	    (frame->tick > (u32)sSYNetInputRemoteConfirmedLastWire[player]))
	{
		sSYNetInputRemoteConfirmedLastWire[player] = (s32)frame->tick;
	}
	syNetInputStrictReadyCacheInvalidate();
}

static void syNetInputCommitRemoteConfirmedWire(s32 player, u32 wire_tick, u32 packet_seq,
                                                 const SYNetInputFrame *frame,
                                                 const SYNetInputFrame *prior_ring, sb32 had_prior_ring)
{
	u32 sim_tick;

	syNetInputStoreRemoteConfirmedFrame(player, frame);
#ifdef PORT
	syNetInputNoteRemoteEncodingOnConfirm(player, frame);
	syNetInputNoteRemoteNonNeutralStick(player, frame);
#endif
	syNetInputStoreRemotePacketSeq(player, wire_tick, packet_seq);
	syNetInputTimelineOnRemoteConfirmedWire(player, wire_tick, frame);
	sim_tick = syNetPeerDelaySimTickFromWire(wire_tick);
#ifdef PORT
	syNetInputMaybeLogForkDiagRemoteWire(player, wire_tick, sim_tick, frame, "commit_remote_wire");
#endif
	if ((sim_tick != 0U) && (syNetInputIsRemoteHumanSlot(player) != FALSE) &&
	    (syNetInputAuthoritativeWireContractEnabled() != FALSE))
	{
		syNetInputPromoteRemoteHumanAuthorityPublished(player, sim_tick);
	}
	{
		SYNetInputFrame published;
		SYNetInputFrame wire_view;
		sb32 had_published;

		had_published = syNetInputGetHistoryFrame(player, sim_tick, &published);
		wire_view = *frame;
		wire_view.tick = sim_tick;
		if ((had_published != FALSE) && (syNetInputFrameGameplayEquals(&published, &wire_view) != FALSE))
		{
			return;
		}
		if ((had_prior_ring != FALSE) && (prior_ring != NULL) && (prior_ring->is_predicted != FALSE) &&
		    (had_published != FALSE) &&
		    (syNetInputShouldDeferPredictedAnalogCorrection(player, sim_tick, &published, &wire_view) != FALSE))
		{
			if (syNetInputRemoteHumanAuthoritativeOnly() == FALSE)
			{
				syNetInputLogInputGameplayRow("defer_analog_correction", player, sim_tick, wire_tick,
				                              "analog_onset", &published, &wire_view);
				return;
			}
			/* Episode FSM: wire is authoritative — patch and queue correction instead of deferring. */
		}
		if ((had_prior_ring != FALSE) && (prior_ring != NULL) && (prior_ring->is_predicted != FALSE) &&
		    (syNetRollbackShouldQueueGgpoCorrection(sim_tick) != FALSE) &&
		    (syNetInputGameplayCorrectionIsSignificantEx(prior_ring, frame, TRUE) != FALSE))
		{
			if (had_published != FALSE)
			{
				syNetInputLogInputGameplayRow("defer_analog_correction", player, sim_tick, wire_tick,
				                              "ggpo_queued", &published, &wire_view);
			}
			else if (prior_ring != NULL)
			{
				syNetInputLogInputGameplayRow("defer_analog_correction", player, sim_tick, wire_tick,
				                              "ggpo_queued", prior_ring, &wire_view);
			}
			syNetRollbackRequestInputCorrection(player, sim_tick);
			return;
		}
	}
	if ((had_prior_ring != FALSE) && (prior_ring != NULL) &&
	    (syNetInputGameplayCorrectionIsSignificant(prior_ring, frame) == FALSE))
	{
		if (syNetInputEpisodeSealedSpanBlocksPatch(sim_tick) == FALSE)
		{
			syNetInputPatchPublishedFromRemoteConfirmedReason(player, wire_tick, frame, "insignificant");
		}
		return;
	}
	if (syNetInputEpisodeSealedSpanBlocksPatch(sim_tick) != FALSE)
	{
		return;
	}
	if (syNetRollbackShouldQueueGgpoCorrection(sim_tick) == FALSE)
	{
		syNetInputPatchPublishedFromRemoteConfirmedReason(player, wire_tick, frame, "no_ggpo");
		return;
	}
	if ((had_prior_ring != FALSE) && (prior_ring != NULL) && (prior_ring->is_predicted != FALSE) &&
	    (syNetInputShouldPatchDigitalTapWithoutRollback(player, sim_tick, prior_ring, frame) != FALSE))
	{
		syNetInputPatchPublishedFromRemoteConfirmedReason(player, wire_tick, frame, "digital_tap");
		return;
	}
	syNetRollbackRequestInputCorrection(player, sim_tick);
	syNetInputPatchPublishedFromRemoteConfirmedReason(player, wire_tick, frame, "post_queue");
}

static sb32 syNetInputShouldSuppressRemoteGapFill(s32 player, u32 tick)
{
	u32 sim_tick;
	u32 hr;
	u32 remote_sim_frontier;

	(void)player;
	(void)tick;
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return TRUE;
	}
	sim_tick = syNetInputGetTick();
	hr = syNetPeerGetHighestRemoteTick();
	if (hr == 0U)
	{
		return FALSE;
	}
	remote_sim_frontier = syNetPeerDelaySimTickFromWire(hr);
	if ((remote_sim_frontier != 0U) && (sim_tick <= remote_sim_frontier + 2U))
	{
		return TRUE;
	}
	return FALSE;
}

static void syNetInputFillRemoteConfirmedGap(s32 player, u32 tick)
{
	SYNetInputFrame seed;
	SYNetInputFrame existing;
	SYNetInputFrame gap;
	u32 t;
	u32 last;

	if ((syNetInputCheckPlayer(player) == FALSE) || (sSYNetInputRemoteConfirmedLastWire[player] < 0))
	{
		return;
	}
	if (syNetInputShouldSuppressRemoteGapFill(player, tick) != FALSE)
	{
		return;
	}
	last = (u32)sSYNetInputRemoteConfirmedLastWire[player];
	if ((tick <= (last + 1U)) || ((tick - last) > 32U))
	{
		return;
	}
	if (syNetInputGetStoredFrame(sSYNetInputRemoteHistory, player, last, &seed) == FALSE)
	{
		return;
	}
	for (t = last + 1U; t < tick; t++)
	{
		if ((syNetInputGetStoredFrame(sSYNetInputRemoteHistory, player, t, &existing) != FALSE) &&
		    (syNetInputFrameIsRemoteStrictConfirmed(&existing) != FALSE) &&
		    (syNetInputFrameIsRemoteGapFilled(&existing) == FALSE))
		{
			continue;
		}
		syNetInputMakeFrame(&gap, t, seed.buttons, seed.stick_x, seed.stick_y,
		                    nSYNetInputSourceRemoteGapFilled, FALSE);
		syNetInputStoreFrame(sSYNetInputRemoteHistory, player, &gap);
		syNetInputClearRemotePacketSeq(player, t);
		if (sSYNetInputRemoteGapFillLogBudget > 0U)
		{
			port_log("SSB64 NetInput: remote_gap_fill player=%d wire=%u from_wire=%u btn=0x%04X sx=%d sy=%d\n",
			         (int)player,
			         (unsigned int)t,
			         (unsigned int)last,
			         (unsigned int)seed.buttons,
			         seed.stick_x,
			         seed.stick_y);
			sSYNetInputRemoteGapFillLogBudget--;
		}
	}
	syNetInputStrictReadyCacheInvalidate();
}

sb32 syNetInputSetRemoteInputFromPacket(s32 player, u32 tick, u16 buttons, s8 stick_x, s8 stick_y, u32 packet_seq,
                                        u32 current_tick, s32 frame_index)
{
	SYNetInputFrame frame;
	SYNetInputFrame existing;

	if (syNetInputCheckPlayer(player) == FALSE)
	{
		return FALSE;
	}
	syNetInputMakeFrame(&frame, tick, buttons, stick_x, stick_y, nSYNetInputSourceRemoteConfirmed, FALSE);
	syNetInputFillRemoteConfirmedGap(player, tick);
	if (syNetInputGetStoredFrame(sSYNetInputRemoteHistory, player, tick, &existing) != FALSE)
	{
		if (syNetInputFrameGameplayEquals(&existing, &frame) != FALSE)
		{
			if ((syNetInputFrameIsRemoteStrictConfirmed(&existing) != FALSE) &&
			    (syNetInputFrameIsRemoteGapFilled(&existing) == FALSE))
			{
				u32 existing_packet_seq;

				if ((syNetInputGetRemotePacketSeq(player, tick, &existing_packet_seq) == FALSE) ||
				    (syNetInputPacketSeqIsNewerOrEqual(packet_seq, existing_packet_seq) != FALSE))
				{
					syNetInputStoreRemotePacketSeq(player, tick, packet_seq);
				}
				return TRUE;
			}
			syNetInputStoreRemoteConfirmedFrame(player, &frame);
			syNetInputStoreRemotePacketSeq(player, tick, packet_seq);
			syNetInputTimelineOnRemoteConfirmedWire(player, tick, &frame);
			return TRUE;
		}
		if ((syNetInputFrameIsRemoteStrictConfirmed(&existing) != FALSE) &&
		    (syNetInputFrameIsRemoteGapFilled(&existing) == FALSE))
		{
			u32 existing_packet_seq;
			sb32 have_existing_packet_seq;

			have_existing_packet_seq = syNetInputGetRemotePacketSeq(player, tick, &existing_packet_seq);
			if ((have_existing_packet_seq == FALSE) ||
			    (syNetInputPacketSeqIsNewerOrEqual(packet_seq, existing_packet_seq) != FALSE))
			{
				if (sSYNetInputRemoteConfirmedConflictLogsRemaining > 0U)
				{
					port_log(
					    "SSB64 NetInput: REMOTE_CONFIRMED_REPLACE_NEWER player=%d wire=%u old_pkt_seq=%u pkt_seq=%u cur_tick=%u "
					    "idx=%d old btn=0x%04X sx=%d sy=%d | new btn=0x%04X sx=%d sy=%d\n",
					    (int)player,
					    tick,
					    (unsigned int)((have_existing_packet_seq != FALSE) ? existing_packet_seq : 0U),
					    (unsigned int)packet_seq,
					    (unsigned int)current_tick,
					    (int)frame_index,
					    (unsigned int)existing.buttons,
					    existing.stick_x,
					    existing.stick_y,
					    (unsigned int)frame.buttons,
					    frame.stick_x,
					    frame.stick_y);
					sSYNetInputRemoteConfirmedConflictLogsRemaining--;
				}
				syNetInputCommitRemoteConfirmedWire(player, tick, packet_seq, &frame, &existing, TRUE);
				return TRUE;
			}
			if (sSYNetInputRemoteConfirmedConflictLogsRemaining > 0U)
			{
				port_log(
				    "SSB64 NetInput: REMOTE_CONFIRMED_CONFLICT keep_newer_old player=%d wire=%u old_pkt_seq=%u pkt_seq=%u cur_tick=%u "
				    "idx=%d old btn=0x%04X sx=%d sy=%d src=%u pred=%u | new btn=0x%04X sx=%d sy=%d\n",
				    (int)player,
				    tick,
				    (unsigned int)existing_packet_seq,
				    (unsigned int)packet_seq,
				    (unsigned int)current_tick,
				    (int)frame_index,
				    (unsigned int)existing.buttons,
				    existing.stick_x,
				    existing.stick_y,
				    (unsigned int)existing.source,
				    (unsigned int)existing.is_predicted,
				    (unsigned int)frame.buttons,
				    frame.stick_x,
				    frame.stick_y);
				sSYNetInputRemoteConfirmedConflictLogsRemaining--;
			}
			return FALSE;
		}
	}
	{
		sb32 had_prior_ring;

		had_prior_ring =
		    (syNetInputGetStoredFrame(sSYNetInputRemoteHistory, player, tick, &existing) != FALSE) ? TRUE : FALSE;
		syNetInputCommitRemoteConfirmedWire(player, tick, packet_seq, &frame,
		                                    had_prior_ring ? &existing : NULL, had_prior_ring);
	}
	return TRUE;
}
#endif

void syNetInputSetRemoteInput(s32 player, u32 tick, u16 buttons, s8 stick_x, s8 stick_y)
{
#ifdef PORT
	(void)syNetInputSetRemoteInputFromPacket(player, tick, buttons, stick_x, stick_y, 0U, syNetInputGetTick(), -1);
#else
	SYNetInputFrame frame;

	if (syNetInputCheckPlayer(player) != FALSE)
	{
		syNetInputMakeFrame(&frame, tick, buttons, stick_x, stick_y, nSYNetInputSourceRemoteConfirmed, FALSE);
		syNetInputStoreFrame(sSYNetInputRemoteHistory, player, &frame);
	}
#endif
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

#ifdef PORT
static void syNetInputMaybeLogLocalInputFrame(s32 player, s32 hw_player, u32 tick, const SYNetInputFrame *frame,
                                              const char *path_tag)
{
	const char *log_env;

	if ((frame == NULL) || (path_tag == NULL))
	{
		return;
	}
	log_env = getenv("SSB64_NETPLAY_LOG_LOCAL_INPUT");
	if ((log_env == NULL) || (log_env[0] == '\0') || (atoi(log_env) == 0) || (syNetPeerIsVSSessionActive() == FALSE))
	{
		return;
	}
	if ((player != syNetPeerGetLocalSimSlot()) || (syNetPeerGetLocalSimSlot() == 0))
	{
		return;
	}
	if ((tick % 128U) != 0U)
	{
		return;
	}
	port_log(
	    "SSB64 NetInput (net guest): %s sim=%d sampled_hw=%d tick=%u -> frame buttons=0x%04x stick=(%d,%d) "
	    "(published to gSYControllerDevices[sim])\n",
	    path_tag, (int)player, (int)hw_player, (unsigned)tick, (unsigned int)frame->buttons, (int)frame->stick_x,
	    (int)frame->stick_y);
}
#endif

void syNetInputMakeLocalFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	s32 hw_player = player;
#ifdef PORT
	SYController *controller;

	if (syNetPeerIsOnlineP2PHardwareDecoupleActive() != FALSE)
	{
		if ((player != syNetPeerGetLocalSimSlot()) && (player != syNetPeerGetExtraLocalSenderSimSlot()))
		{
			syNetInputMakeFrame(out_frame, tick, 0, 0, 0, nSYNetInputSourceLocal, FALSE);
			return;
		}
	}
	/* Rollback resim: deterministic replay — sealed episode table or published history for this tick. */
	if (syNetRollbackIsResimulating() != FALSE)
	{
		SYNetInputFrame hist;

		if (syNetRollbackEpisodeGetSealedFrame(player, tick, &hist) != FALSE)
		{
			*out_frame = hist;
			out_frame->source = nSYNetInputSourceLocal;
			out_frame->is_predicted = FALSE;
			return;
		}
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
	if ((syNetInputAuthoritativeWireContractEnabled() != FALSE) && (syNetInputIsLocalDelaySlot(player) != FALSE))
	{
		hw_player = syNetPeerResolveLocalHardwareDevice(player);
		if (syNetInputGetLocalDelayedFrame(player, tick, out_frame) != FALSE)
		{
			out_frame->source = nSYNetInputSourceLocal;
			out_frame->is_predicted = FALSE;
			syNetInputMaybeLogLocalInputFrame(player, hw_player, tick, out_frame, "delay_slot");
			return;
		}
		syNetInputMakeFrame(out_frame, tick, 0, 0, 0, nSYNetInputSourceLocal, FALSE);
		syNetInputMaybeLogLocalInputFrame(player, hw_player, tick, out_frame, "delay_slot_empty");
		return;
	}
	hw_player = syNetPeerResolveLocalHardwareDevice(player);
	controller = &sSYNetInputHardwareLatch[hw_player];
	{
		s8 stick_x;
		s8 stick_y;

		stick_x = controller->stick_range.x;
		stick_y = controller->stick_range.y;
		syNetInputNoteLocalEncodingOnSample(player, stick_x, stick_y, tick);
		if (syNetInputMixedInputQuantizeEnabled() != FALSE)
		{
			syNetInputQuantizeStickToDigitalCardinals(&stick_x, &stick_y);
		}
		syNetInputMakeFrame(out_frame, tick, syNetInputButtonsFromController(controller), stick_x, stick_y,
		                   nSYNetInputSourceLocal, FALSE);
	}
	syNetInputMaybeLogLocalInputFrame(player, hw_player, tick, out_frame, "hardware_latch");
#else
	{
		SYController *controller = &gSYControllerDevices[player];

		syNetInputMakeFrame(out_frame, tick, syNetInputButtonsFromController(controller), controller->stick_range.x,
		                   controller->stick_range.y, nSYNetInputSourceLocal, FALSE);
	}
#endif
}

#ifdef PORT
static sb32 syNetInputTryGetPredictionSeedFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);
static sb32 syNetInputTryGetPredictionStickSeed(s32 player, u32 tick, s8 *out_stick_x, s8 *out_stick_y);
static sb32 syNetInputStickLooksAnalog(s8 stick_x, s8 stick_y);
static sb32 syNetInputStickSameAnalogIntent(s8 ax, s8 ay, s8 bx, s8 by);
static void syNetInputApplyAnalogPredictionDecay(s8 *stick_x, s8 *stick_y, u32 lead_ticks);
static u32 syNetInputAnalogPredDecayTicks(void);
#endif

#ifdef PORT
static sb32 syNetInputTryGetPredictionStickSeed(s32 player, u32 tick, s8 *out_stick_x, s8 *out_stick_y)
{
	SYNetInputFrame seed;

	if ((out_stick_x == NULL) || (out_stick_y == NULL))
	{
		return FALSE;
	}
	if (syNetInputTryGetPredictionSeedFrame(player, tick, &seed) == FALSE)
	{
		return FALSE;
	}
	*out_stick_x = seed.stick_x;
	*out_stick_y = seed.stick_y;
	return TRUE;
}

static void syNetInputMakePredictedFrameRemoteHuman(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	SYNetInputFrame *last_confirmed = &sSYNetInputSlots[player].last_confirmed;
	u16 buttons;
	s8 stick_x;
	s8 stick_y;
	u32 lead_ticks;
	sb32 had_stick_seed;

	buttons = 0;
	stick_x = 0;
	stick_y = 0;
	if (last_confirmed->is_valid != FALSE)
	{
		buttons = last_confirmed->buttons;
		stick_x = last_confirmed->stick_x;
		stick_y = last_confirmed->stick_y;
	}
	had_stick_seed = syNetInputTryGetPredictionStickSeed(player, tick, &stick_x, &stick_y);
	if (had_stick_seed == FALSE)
	{
		if (last_confirmed->is_valid != FALSE)
		{
			stick_x = last_confirmed->stick_x;
			stick_y = last_confirmed->stick_y;
		}
	}
	else if ((last_confirmed->is_valid != FALSE) && (syNetInputFrameSticksNearNeutral(last_confirmed) != FALSE) &&
	         (syNetInputFrameIsQuasiDigitalKeyboard(last_confirmed) == FALSE))
	{
		SYNetInputFrame seed_frame;

		if ((had_stick_seed != FALSE) && (syNetInputTryGetPredictionSeedFrame(player, tick, &seed_frame) != FALSE) &&
		    (seed_frame.tick >= last_confirmed->tick) &&
		    (syNetInputStickLooksAnalog(seed_frame.stick_x, seed_frame.stick_y) != FALSE))
		{
			stick_x = seed_frame.stick_x;
			stick_y = seed_frame.stick_y;
		}
		else
		{
			stick_x = 0;
			stick_y = 0;
			had_stick_seed = FALSE;
		}
	}
	if (last_confirmed->is_valid != FALSE)
	{
		if (syNetInputFrameIsDigitalKeyboard(last_confirmed) != FALSE)
		{
			stick_x = last_confirmed->stick_x;
			stick_y = last_confirmed->stick_y;
		}
		else if (syNetInputFrameIsQuasiDigitalKeyboard(last_confirmed) != FALSE)
		{
			stick_x = last_confirmed->stick_x;
			stick_y = last_confirmed->stick_y;
		}
		else
		{
			if (syNetInputStickAxisIsDigital(last_confirmed->stick_x) != FALSE)
			{
				stick_x = last_confirmed->stick_x;
			}
			if (syNetInputStickAxisIsDigital(last_confirmed->stick_y) != FALSE)
			{
				stick_y = last_confirmed->stick_y;
			}
		}
	}
	if ((sSYNetInputPredictNeutral != FALSE) && (syNetInputFrameIsQuasiDigitalKeyboard(last_confirmed) == FALSE) &&
	    (syNetInputRemoteRecentEncodingIsDigital(player, tick, 4U) != FALSE))
	{
		stick_x = 0;
		stick_y = 0;
	}
	if (last_confirmed->is_valid != FALSE)
	{
		if (tick > last_confirmed->tick)
		{
			lead_ticks = tick - last_confirmed->tick;
		}
		else
		{
			lead_ticks = 0U;
		}
		if ((syNetInputFrameSticksNearNeutral(last_confirmed) != FALSE) &&
		    (syNetInputFrameIsQuasiDigitalKeyboard(last_confirmed) == FALSE) &&
		    (syNetInputStickLooksAnalog(stick_x, stick_y) == FALSE) &&
		    (syNetInputRemoteRecentEncodingIsDigital(player, tick, 4U) != FALSE))
		{
			stick_x = 0;
			stick_y = 0;
		}
		else if ((lead_ticks > 0U) && (syNetInputStickLooksAnalog(stick_x, stick_y) != FALSE))
		{
			syNetInputApplyAnalogPredictionDecay(&stick_x, &stick_y, lead_ticks);
		}
	}
	syNetInputMakeFrame(out_frame, tick, buttons, stick_x, stick_y, nSYNetInputSourceRemotePredicted, TRUE);
}
#endif

void syNetInputMakePredictedFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	SYNetInputFrame *last_confirmed = &sSYNetInputSlots[player].last_confirmed;

#ifdef PORT
	SYNetInputFrame seed;

	if (syNetInputIsRemoteHumanSlot(player) != FALSE)
	{
		syNetInputMakePredictedFrameRemoteHuman(player, tick, out_frame);
		return;
	}
	if (sSYNetInputPredictNeutral != FALSE)
	{
		if ((last_confirmed->is_valid != FALSE) &&
		    (syNetInputFrameIsDigitalKeyboard(last_confirmed) != FALSE))
		{
			syNetInputMakeFrame(out_frame, tick, last_confirmed->buttons, last_confirmed->stick_x,
			                    last_confirmed->stick_y, nSYNetInputSourceRemotePredicted, TRUE);
			return;
		}
		syNetInputMakeFrame(out_frame, tick, 0, 0, 0, nSYNetInputSourceRemotePredicted, TRUE);
		return;
	}
	if (syNetInputTryGetPredictionSeedFrame(player, tick, &seed) != FALSE)
	{
		syNetInputMakeFrame(out_frame, tick, seed.buttons, seed.stick_x, seed.stick_y, nSYNetInputSourceRemotePredicted,
		                   TRUE);
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
 * lookup through `syNetPeerDelayWireLookupTickFromSim` (same pure sim + D mapping as sender), then normalize
 * `out_frame->tick` to `sim_tick` on success.
 */
#if defined(PORT)
static u32 syNetInputRemoteHistoryWireLookupTick(u32 sim_tick)
{
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return sim_tick;
	}
	return syNetPeerDelayWireLookupTickFromSim(sim_tick);
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

#ifdef PORT
static sb32 syNetInputTryGetRemoteConfirmedHistoryForSimTick(s32 player, u32 sim_tick, SYNetInputFrame *out_frame)
{
	SYNetInputFrame frame;

	if (syNetInputTryGetRemoteHistoryForSimTick(player, sim_tick, &frame) == FALSE)
	{
		return FALSE;
	}
	if (syNetInputFrameIsRemoteStrictConfirmed(&frame) == FALSE)
	{
		return FALSE;
	}
	if (out_frame != NULL)
	{
		*out_frame = frame;
	}
	return TRUE;
}
#else
static sb32 syNetInputTryGetRemoteConfirmedHistoryForSimTick(s32 player, u32 sim_tick, SYNetInputFrame *out_frame)
{
	return syNetInputTryGetRemoteHistoryForSimTick(player, sim_tick, out_frame);
}
#endif

#ifdef PORT
#define SYNETINPUT_PREDICTION_LOOKBACK 8U

sb32 syNetInputIsRemoteHumanSlot(s32 player)
{
	s32 i;
	s32 slot;
	s32 n;

	n = syNetPeerGetRemoteHumanSlotCount();
	for (i = 0; i < n; i++)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(i, &slot) == FALSE)
		{
			continue;
		}
		if (slot == player)
		{
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * Hold-last for prediction: prefer the newest confirmed remote or published row before `tick` so brief
 * stick holds survive the delay window (jump onset predicts last direction, not neutral).
 */
static sb32 syNetInputTryGetPredictionSeedFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	u32 t;
	u32 oldest;
	u32 lookback;

	if (tick == 0U)
	{
		return FALSE;
	}
	lookback = SYNETINPUT_PREDICTION_LOOKBACK;
	if (lookback > tick)
	{
		lookback = tick;
	}
	oldest = tick - lookback;
	for (t = tick - 1U; t >= oldest; t--)
	{
		SYNetInputFrame wire_row;

		if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, t, out_frame) != FALSE)
		{
			return TRUE;
		}
		if ((syNetInputTryGetRemoteHistoryForSimTick(player, t, &wire_row) != FALSE) &&
		    (syNetInputFrameIsRemoteConfirmed(&wire_row) != FALSE))
		{
			*out_frame = wire_row;
			return TRUE;
		}
		if ((syNetInputGetHistoryFrame(player, t, out_frame) != FALSE) &&
		    (out_frame->is_predicted == FALSE))
		{
			return TRUE;
		}
		if (t == oldest)
		{
			break;
		}
	}
	return FALSE;
}

/*
 * Cache speculative remote input separately by metadata: it may feed `synchronize_inputs`, but confirmed-only helpers
 * must ignore it for admission, rollback mismatch scans, and resim seed restoration.
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
	syNetInputStrictReadyCacheInvalidate();
}
#endif

/*
 * Episode FSM: remote-human sim and published history must not use optimistic analog onset prediction.
 * `SSB64_NETPLAY_REMOTE_ANALOG_ONSET_PRED=1` restores legacy onset for bisect (FSM must still be on).
 */
static sb32 syNetInputRemoteHumanAuthoritativeOnly(void)
{
	const char *e;

	if (syNetRollbackEpisodeFsmEnabled() == FALSE)
	{
		return FALSE;
	}
	if (sSYNetInputRemoteAnalogOnsetPredEnvCache == -999)
	{
		e = getenv("SSB64_NETPLAY_REMOTE_ANALOG_ONSET_PRED");
		sSYNetInputRemoteAnalogOnsetPredEnvCache =
		    ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	}
	return (sSYNetInputRemoteAnalogOnsetPredEnvCache == 0) ? TRUE : FALSE;
}

static void syNetInputResolveRemoteHumanAuthoritativeFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	s32 source_rank;

	if (syNetInputResolveRemoteHumanAuthorityFrameEx(player, tick, out_frame, &source_rank) == FALSE)
	{
		return;
	}
	if (source_rank == nSYNetRemoteAuthoritySourceWireConfirmed)
	{
		sSYNetInputSlots[player].last_confirmed = *out_frame;
		syNetInputNoteRemoteNonNeutralStick(player, out_frame);
	}
}

static void syNetInputMaybeStorePredictedOverlayForDiag(s32 player, u32 tick)
{
	SYNetInputFrame predicted;

	if ((syNetInputRemoteHumanAuthoritativeOnly() == FALSE) || (g_UseInputPrediction == FALSE))
	{
		return;
	}
	syNetInputMakePredictedFrameRemoteHuman(player, tick, &predicted);
	syNetInputStoreRemotePredictedWireFromSimTick(player, tick, &predicted);
}

/* Build one logical frame for `player` at `tick` without touching globals (feeds `syNetInputPublishFrame`). */
void syNetInputResolveFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
#ifdef PORT
	if (syNetRollbackIsResimulating() != FALSE)
	{
		if (syNetRollbackEpisodeInputsSealed() != FALSE)
		{
			if (syNetRollbackEpisodeGetSealedFrame(player, tick, out_frame) != FALSE)
			{
				if (syNetInputIsRemoteHumanSlot(player) != FALSE)
				{
					sSYNetInputSlots[player].last_confirmed = *out_frame;
					syNetInputNoteRemoteNonNeutralStick(player, out_frame);
				}
				return;
			}
			syNetInputMakeFrame(out_frame, tick, 0, 0, 0,
			                    syNetInputIsRemoteHumanSlot(player) != FALSE ? nSYNetInputSourceRemotePredicted
			                                                                 : nSYNetInputSourceLocal,
			                    syNetInputIsRemoteHumanSlot(player) != FALSE ? TRUE : FALSE);
			return;
		}
		if (syNetInputIsRemoteHumanSlot(player) != FALSE)
		{
			if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, tick, out_frame) != FALSE)
			{
				sSYNetInputSlots[player].last_confirmed = *out_frame;
				syNetInputNoteRemoteNonNeutralStick(player, out_frame);
				return;
			}
			if (syNetInputGetHistoryFrame(player, tick, out_frame) != FALSE)
			{
				out_frame->source = nSYNetInputSourceRemoteConfirmed;
				out_frame->is_predicted = FALSE;
				return;
			}
			syNetInputMakeFrame(out_frame, tick, 0, 0, 0, nSYNetInputSourceRemotePredicted, TRUE);
			return;
		}
	}
#endif
	switch (sSYNetInputSlots[player].source)
	{
	case nSYNetInputSourceRemoteConfirmed:
	case nSYNetInputSourceRemotePredicted:
#ifdef PORT
		if ((syNetRollbackIsResimulating() == FALSE) &&
		    (syNetRollbackPredictionRecoveryRequiresConfirmed(tick) != FALSE) &&
		    (syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, tick, out_frame) != FALSE))
		{
			sSYNetInputSlots[player].last_confirmed = *out_frame;
			return;
		}
#endif
		if (syNetInputTryGetRemoteHistoryForSimTick(player, tick, out_frame) != FALSE)
		{
#ifdef PORT
			if (syNetInputFrameIsRemoteStrictConfirmed(out_frame) != FALSE)
			{
				sSYNetInputSlots[player].last_confirmed = *out_frame;
				syNetInputNoteRemoteNonNeutralStick(player, out_frame);
			}
			else if ((syNetInputIsRemoteHumanSlot(player) != FALSE) && (g_UseInputPrediction != FALSE) &&
			         (syNetRollbackIsResimulating() == FALSE))
			{
				if (syNetInputRemoteHumanAuthoritativeOnly() != FALSE)
				{
					syNetInputResolveRemoteHumanAuthoritativeFrame(player, tick, out_frame);
					syNetInputMaybeStorePredictedOverlayForDiag(player, tick);
				}
				else
				{
					/*
					 * Re-derive at publish time so analog onset / wire peek see the newest staged remote
					 * rows (stale predicted-neutral cache must not win over phase-lock commit).
					 */
					syNetInputMakePredictedFrame(player, tick, out_frame);
					syNetInputStoreRemotePredictedWireFromSimTick(player, tick, out_frame);
				}
			}
#else
			sSYNetInputSlots[player].last_confirmed = *out_frame;
#endif
		}
#ifdef PORT
		else if (syNetInputAuthoritativeWireContractEnabled() != FALSE)
		{
			if ((syNetInputIsRemoteHumanSlot(player) != FALSE) &&
			    (syNetInputRemoteHumanAuthoritativeOnly() != FALSE))
			{
				syNetInputResolveRemoteHumanAuthoritativeFrame(player, tick, out_frame);
			}
			else if (g_UseInputPrediction != FALSE)
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
	SYNetInputFrame prev_tick_frame;
	u16 prev_buttons = 0;
	u16 preserved_tap = 0;
	u16 preserved_release = 0;
	u16 pressed;
	u16 released;

	if (last_published->is_valid != FALSE)
	{
		if (last_published->tick == frame->tick)
		{
			/*
			 * Same-tick republish (FuncRead then RepublishRemote before interface): edge detect against
			 * prior sim tick, not the first publish this tick — and keep taps already seen by interface.
			 */
			if ((frame->tick > 0U) &&
			    (syNetInputGetHistoryFrame(player, frame->tick - 1U, &prev_tick_frame) != FALSE))
			{
				prev_buttons = prev_tick_frame.buttons;
			}
			preserved_tap = gSYControllerDevices[player].button_tap;
			preserved_release = gSYControllerDevices[player].button_release;
		}
		else
		{
			prev_buttons = last_published->buttons;
		}
	}
	pressed = (u16)(((frame->buttons ^ prev_buttons) & frame->buttons) | preserved_tap);
	released = (u16)(((frame->buttons ^ prev_buttons) & prev_buttons) | preserved_release);

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
		if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, tick_begin + i, &frame) != FALSE)
		{
			checksum = syNetInputAccumulateInputChecksum(checksum, player, &frame);
		}
	}
	return checksum;
}

static sb32 syNetInputResolveFrameCommitAuthorityFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	s32 local_slot;
	s32 extra_slot;

	if ((out_frame == NULL) || (syNetInputCheckPlayer(player) == FALSE) || (tick == 0U))
	{
		return FALSE;
	}
	local_slot = syNetPeerGetLocalSimSlot();
	extra_slot = syNetPeerGetExtraLocalSenderSimSlot();
	if ((player == local_slot) || (player == extra_slot))
	{
		return syNetInputResolveLocalAuthorityFrame(player, tick, out_frame);
	}
	if (syNetInputIsRemoteHumanSlot(player) != FALSE)
	{
		return syNetInputResolveRemoteHumanAuthorityFrameEx(player, tick, out_frame, NULL);
	}
	return syNetInputGetHistoryFrame(player, tick, out_frame);
}

void syNetInputGetFrameCommitAuthorityChecksumWindow(u32 tick_begin, u32 frame_count, u32 *out_checksums,
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
			if (syNetInputResolveFrameCommitAuthorityFrame(player, tick, &frame) != FALSE)
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
			if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, tick, &frame) != FALSE)
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
			if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, tick, &frame) != FALSE)
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
			sb32 rv = syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, t, &rf);

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

static sb32 syNetInputDiagPublishedRemoteMismatchIsActionable(s32 player, u32 kind, s32 local_sim_slot,
                                                              s32 extra_local_sim_slot)
{
	if (kind != 0U)
	{
		return TRUE;
	}
	if (player == local_sim_slot)
	{
		return FALSE;
	}
	if ((extra_local_sim_slot >= 0) && (player == extra_local_sim_slot))
	{
		return FALSE;
	}
	return TRUE;
}

sb32 syNetInputDiagFindFirstActionablePublishedRemoteMismatch(u32 tick_begin, u32 frame_count, s32 local_sim_slot,
                                                            s32 extra_local_sim_slot, s32 *out_player, u32 *out_tick,
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
			sb32 rv = syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, t, &rf);

			if ((hv == FALSE) && (rv == FALSE))
			{
				continue;
			}
			if (hv != rv)
			{
				if (syNetInputDiagPublishedRemoteMismatchIsActionable(player, 0U, local_sim_slot,
				                                                    extra_local_sim_slot) == FALSE)
				{
					continue;
				}
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

void syNetInputLogPubVsRemoteWindowDiag(u32 validation_tick, u32 tick_begin, u32 frame_count, s32 local_sim_slot,
                                        s32 extra_local_sim_slot)
{
	typedef struct SYNetInputPubVsRemoteSlotSummary
	{
		u32 mismatch_count;
		u32 first_tick;
		u32 worst_kind;
		sb32 any;
		sb32 any_actionable;
		sb32 have_first;
	} SYNetInputPubVsRemoteSlotSummary;

	SYNetInputPubVsRemoteSlotSummary slots[MAXCONTROLLERS];
	SYNetInputFrame hf;
	SYNetInputFrame rf;
	u32 tick_limit;
	u32 t;
	s32 player;

	memset(slots, 0, sizeof(slots));
	tick_limit = tick_begin + frame_count;

	for (t = tick_begin; t < tick_limit; t++)
	{
		for (player = 0; player < MAXCONTROLLERS; player++)
		{
			sb32 hv = syNetInputGetHistoryFrame(player, t, &hf);
			sb32 rv = syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, t, &rf);
			u32 kind;
			sb32 actionable;

			if ((hv == FALSE) && (rv == FALSE))
			{
				continue;
			}
			if (hv != rv)
			{
				kind = 0U;
			}
			else if ((hf.tick != rf.tick) || (hf.buttons != rf.buttons) || (hf.stick_x != rf.stick_x) ||
			         (hf.stick_y != rf.stick_y))
			{
				kind = 1U;
			}
			else
			{
				continue;
			}
			actionable = syNetInputDiagPublishedRemoteMismatchIsActionable(player, kind, local_sim_slot,
			                                                             extra_local_sim_slot);
			slots[player].any = TRUE;
			slots[player].mismatch_count++;
			if (slots[player].have_first == FALSE)
			{
				slots[player].first_tick = t;
				slots[player].have_first = TRUE;
			}
			if (kind > slots[player].worst_kind)
			{
				slots[player].worst_kind = kind;
			}
			if (actionable != FALSE)
			{
				slots[player].any_actionable = TRUE;
			}
		}
	}

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		if (slots[player].any == FALSE)
		{
			continue;
		}
		port_log(
		    "SSB64 NetSync: pub_vs_remote_summary validation=%u win=[%u,%u) player=%d kind=%s first_tick=%u "
		    "mismatches=%u actionable=%d remote_human=%d\n",
		    validation_tick, tick_begin, tick_begin + frame_count, (int)player,
		    (slots[player].worst_kind != 0U) ? "values" : "presence", slots[player].first_tick,
		    slots[player].mismatch_count, (int)slots[player].any_actionable,
		    (int)((syNetInputIsRemoteHumanSlot(player) != FALSE) ? 1 : 0));
	}
}

void syNetInputLogStartupInputBindingSnapshot(u32 agreed_tick)
{
	SYNetInputFrame pub;
	SYNetInputFrame ring;
	s32 local_slot;
	s32 remote_slot;
	s32 extra_slot;
	s32 p;
	u32 t;
	u32 t_end;
	sb32 hv;
	sb32 rv;

	local_slot = syNetPeerGetLocalSimSlot();
	remote_slot = syNetPeerGetRemotePlayerSlot();
	extra_slot = syNetPeerGetExtraLocalSenderSimSlot();
	t_end = agreed_tick + 4U;
	port_log(
	    "SSB64 NetInput: startup_bind_snapshot agreed_tick=%u local_sim=%d remote_sim=%d extra_local=%d delay=%u\n",
	    agreed_tick, (int)local_slot, (int)remote_slot, (int)extra_slot,
	    (unsigned int)syNetPeerGetCommittedInputDelay());
	for (t = agreed_tick; t < t_end; t++)
	{
		for (p = 0; p < MAXCONTROLLERS; p++)
		{
			hv = syNetInputGetHistoryFrame(p, t, &pub);
			rv = syNetInputTryGetRemoteConfirmedHistoryForSimTick(p, t, &ring);
			if ((hv == FALSE) && (rv == FALSE))
			{
				continue;
			}
			port_log(
			    "SSB64 NetInput: startup_bind tick=%u player=%d pub=%d btn=0x%04X stick=(%d,%d) src=%d pred=%d | "
			    "remote_conf=%d btn=0x%04X stick=(%d,%d) src=%d pred=%d\n",
			    t, p, (int)hv, (unsigned int)pub.buttons, (int)pub.stick_x, (int)pub.stick_y, (int)pub.source,
			    (int)pub.is_predicted, (int)rv, (unsigned int)ring.buttons, (int)ring.stick_x, (int)ring.stick_y,
			    (int)ring.source, (int)ring.is_predicted);
		}
	}
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
		syNetInputClearFrame(&sSYNetInputSlots[slot].last_non_neutral);
		sSYNetInputSlots[slot].remote_encoding_was_digital = FALSE;
		sSYNetInputSlots[slot].remote_encoding_grace_until_tick = 0U;
	}
	memset(sSYNetInputSimPredictedRemoteTick, 0, sizeof(sSYNetInputSimPredictedRemoteTick));
	memset(sSYNetInputSimPredictedRemoteUsed, 0, sizeof(sSYNetInputSimPredictedRemoteUsed));
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
#ifdef PORT
	return syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, tick, out_frame);
#else
	return syNetInputTryGetRemoteHistoryForSimTick(player, tick, out_frame);
#endif
}

sb32 syNetInputHasRemoteInputForWireTick(s32 player, u32 wire_tick)
{
	SYNetInputFrame frame;

	if (syNetInputCheckPlayer(player) == FALSE)
	{
		return FALSE;
	}
	if (syNetInputGetStoredFrame(sSYNetInputRemoteHistory, player, wire_tick, &frame) == FALSE)
	{
		return FALSE;
	}
#ifdef PORT
	return syNetInputFrameIsRemoteConfirmed(&frame);
#else
	return TRUE;
#endif
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

#ifdef PORT
static sb32 syNetInputCheckPortHardwareConnected(s32 player)
{
	s32 i;

	if ((player < 0) || (player >= MAXCONTROLLERS))
	{
		return FALSE;
	}
	for (i = 0; i < (s32)ARRAY_COUNT(gSYControllerDeviceStatuses); i++)
	{
		if (player == gSYControllerDeviceStatuses[i])
		{
			return TRUE;
		}
	}
	return FALSE;
}

void syNetInputRefreshPortHardwareUiLatch(void)
{
	SYController replay_backup[MAXCONTROLLERS];
	s32 player;

	memcpy(replay_backup, gSYControllerDevices, sizeof(replay_backup));
	syControllerFuncRead();
	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		sSYNetInputHardwareLatch[player] = gSYControllerDevices[player];
	}
	memcpy(gSYControllerDevices, replay_backup, sizeof(replay_backup));
	syNetInputPublishMainController();
}

s32 syNetInputGetPortHardwareTapButtons(u32 buttons)
{
	s32 player;
	u16 matched;

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		if (syNetInputCheckPortHardwareConnected(player) == FALSE)
		{
			continue;
		}
		matched = (u16)(sSYNetInputHardwareLatch[player].button_tap & buttons);
		if (matched != 0)
		{
			/* Consume matched tap edges so UI does not re-fire while sim tick is frozen (replay halt). */
			sSYNetInputHardwareLatch[player].button_tap &= (u16)~matched;
			return player + 1;
		}
	}
	return 0;
}

s32 syNetInputGetPortHardwareHoldButtons(u32 buttons)
{
	s32 player;

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		if ((syNetInputCheckPortHardwareConnected(player) != FALSE) &&
		    (sSYNetInputHardwareLatch[player].button_hold & buttons))
		{
			return player + 1;
		}
	}
	return 0;
}

s32 syNetInputGetPortHardwareStickUD(s8 range, sb32 up_or_down)
{
	s32 player;

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		if (syNetInputCheckPortHardwareConnected(player) != FALSE)
		{
			if (up_or_down != 0)
			{
				if (range < sSYNetInputHardwareLatch[player].stick_range.y)
				{
					return sSYNetInputHardwareLatch[player].stick_range.y;
				}
			}
			else if (range > sSYNetInputHardwareLatch[player].stick_range.y)
			{
				return sSYNetInputHardwareLatch[player].stick_range.y;
			}
		}
	}
	return 0;
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
#if defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE)
		if (syNetReconnectExportPeerDisconnect(i) != 0)
		{
			out_disconnected[i] = 1;
		}
#endif
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
u32 syNetInputGetStrictRemoteLeadBufferTicks(void)
{
	const char *e;
	int v;

	if (sSYNetInputStrictRemoteLeadBufferEnvCache != -999)
	{
		return (u32)sSYNetInputStrictRemoteLeadBufferEnvCache;
	}
	e = getenv("SSB64_NETPLAY_STRICT_REMOTE_LEAD_BUFFER_TICKS");
	if ((e == NULL) || (e[0] == '\0'))
	{
		v = 2;
	}
	else
	{
		v = atoi(e);
	}
	if (v < 0)
	{
		v = 0;
	}
	if (v > 16)
	{
		v = 16;
	}
	sSYNetInputStrictRemoteLeadBufferEnvCache = v;
	return (u32)v;
}

/*
 * Strict remote-miss: publish **local** sim slot(s) from HID latch into published history so `syNetPeerGatherHistoryBundle`
 * can emit INPUT before remote `RemoteHistory[slot][sim_tick + input_delay]` exists; full resolve/publish for all slots stays deferred.
 *
 * The latch in `syNetInputFuncRead` is always for **this** authoritative sim `tick`. `syNetInputMakeLocalFrame` labels the
 * frame with its `tick` argument — it must be **`tick`**, not a delayed probe surrogate. Strict gate delay/slack now only
 * influences required **wire frontier** readiness, not local publish keying.
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
	syNetInputPromoteAllLocalAuthoritySlots(pub_tick);
	syNetInputPublishMainController();
}

#endif /* PORT */

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

#ifdef PORT
static sb32 syNetInputFrameConsumedPredictedRemote(const SYNetInputFrame *frame)
{
	if ((frame == NULL) || (frame->is_valid == FALSE))
	{
		return FALSE;
	}
	if ((frame->is_predicted != FALSE) || (frame->source == nSYNetInputSourceRemotePredicted))
	{
		return TRUE;
	}
	return FALSE;
}

void syNetInputNoteSimTickPredictedRemoteUsage(u32 sim_tick, const SYNetInputFrame *synced_frames)
{
	u32 idx;
	s32 ri;
	s32 n;
	s32 slot;
	sb32 used_predicted;

	if (synced_frames == NULL)
	{
		return;
	}
	idx = sim_tick % SYNETINPUT_HISTORY_LENGTH;
	used_predicted = FALSE;
	n = syNetPeerGetRemoteHumanSlotCount();
	for (ri = 0; ri < n; ri++)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(ri, &slot) == FALSE)
		{
			continue;
		}
		if (syNetInputFrameConsumedPredictedRemote(&synced_frames[slot]) != FALSE)
		{
			used_predicted = TRUE;
			break;
		}
	}
	sSYNetInputSimPredictedRemoteTick[idx] = sim_tick;
	sSYNetInputSimPredictedRemoteUsed[idx] = (used_predicted != FALSE) ? 1U : 0U;
}

sb32 syNetInputSimTickUsedPredictedRemote(u32 sim_tick)
{
	u32 idx;

	idx = sim_tick % SYNETINPUT_HISTORY_LENGTH;
	if (sSYNetInputSimPredictedRemoteTick[idx] != sim_tick)
	{
		return FALSE;
	}
	return (sSYNetInputSimPredictedRemoteUsed[idx] != 0U) ? TRUE : FALSE;
}

u32 syNetInputFindEarliestPredictedRemoteUsageInSpan(u32 from_tick, u32 to_tick)
{
	u32 t;

	if (from_tick > to_tick)
	{
		return ~(u32)0;
	}
	for (t = from_tick; t <= to_tick; t++)
	{
		if (syNetInputSimTickUsedPredictedRemote(t) != FALSE)
		{
			return t;
		}
	}
	return ~(u32)0;
}

static sb32 syNetInputDivergenceInputLogWindow(u32 tick, u32 *out_begin, u32 *out_end)
{
	const char *e;
	static sb32 sCached = -999;
	static u32 sBegin;
	static u32 sEnd;

	if (sCached == -999)
	{
		e = getenv("SSB64_NETPLAY_DIVERGENCE_INPUT_LOG");
		sCached = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
		if (sCached != 0)
		{
			e = getenv("SSB64_NETPLAY_DIVERGENCE_INPUT_LOG_BEGIN");
			sBegin = ((e != NULL) && (e[0] != '\0')) ? (u32)atoi(e) : 931U;
			e = getenv("SSB64_NETPLAY_DIVERGENCE_INPUT_LOG_END");
			sEnd = ((e != NULL) && (e[0] != '\0')) ? (u32)atoi(e) : 960U;
		}
	}
	if (sCached == 0)
	{
		return FALSE;
	}
	if ((out_begin != NULL) && (out_end != NULL))
	{
		*out_begin = sBegin;
		*out_end = sEnd;
	}
	return (tick >= sBegin) && (tick <= sEnd) ? TRUE : FALSE;
}

/*
 * Bracketed per-tick input trace for fighter-state fork bisect (session-4 class @519).
 * SSB64_NETPLAY_INPUT_FORK_DIAG=1
 * SSB64_NETPLAY_INPUT_FORK_DIAG_MIN=515  SSB64_NETPLAY_INPUT_FORK_DIAG_MAX=530  (defaults)
 */
static sb32 syNetInputForkDiagWindow(u32 sim_tick, u32 *out_begin, u32 *out_end)
{
	const char *e;
	static sb32 sCached = -999;
	static u32 sBegin = 515U;
	static u32 sEnd = 530U;

	if (sCached == -999)
	{
		e = getenv("SSB64_NETPLAY_INPUT_FORK_DIAG");
		sCached = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
		if (sCached != 0)
		{
			e = getenv("SSB64_NETPLAY_INPUT_FORK_DIAG_MIN");
			if ((e != NULL) && (e[0] != '\0'))
			{
				sBegin = (u32)atoi(e);
			}
			e = getenv("SSB64_NETPLAY_INPUT_FORK_DIAG_MAX");
			if ((e != NULL) && (e[0] != '\0'))
			{
				sEnd = (u32)atoi(e);
			}
		}
	}
	if (sCached == 0)
	{
		return FALSE;
	}
	if ((out_begin != NULL) && (out_end != NULL))
	{
		*out_begin = sBegin;
		*out_end = sEnd;
	}
	return (sim_tick >= sBegin) && (sim_tick <= sEnd) ? TRUE : FALSE;
}

sb32 syNetInputForkDiagWireInWindow(u32 wire_tick)
{
	return syNetInputForkDiagWindow(syNetPeerDelaySimTickFromWire(wire_tick), NULL, NULL);
}

static void syNetInputMaybeLogForkDiagRemoteWire(s32 player, u32 wire_tick, u32 sim_tick, const SYNetInputFrame *frame,
                                                 const char *reason)
{
	u32 begin;
	u32 end;
	u32 hr;
	u32 remote_sim_frontier;
	u32 req_wire;
	u32 d;

	if ((frame == NULL) || (syNetInputForkDiagWindow(sim_tick, &begin, &end) == FALSE))
	{
		return;
	}
	hr = syNetPeerGetHighestRemoteTick();
	remote_sim_frontier = (hr != 0U) ? syNetPeerDelaySimTickFromWire(hr) : 0U;
	req_wire = syNetPeerGetBaseRequiredWireTick(sim_tick);
	d = syNetPeerGetCommittedInputDelay();
	port_log(
	    "SSB64 NetInput: fork_wire_commit player=%d wire=%u sim=%u reason=%s window=[%u,%u] "
	    "btn=0x%04X sx=%d sy=%d D=%u req_wire=%u hr=%u remote_sim_frontier=%u local_sim=%u\n",
	    (int)player,
	    (unsigned int)wire_tick,
	    (unsigned int)sim_tick,
	    (reason != NULL) ? reason : "unknown",
	    begin,
	    end,
	    (unsigned int)frame->buttons,
	    (int)frame->stick_x,
	    (int)frame->stick_y,
	    (unsigned int)d,
	    (unsigned int)req_wire,
	    (unsigned int)hr,
	    (unsigned int)remote_sim_frontier,
	    (unsigned int)syNetInputGetTick());
}

void syNetInputMaybeLogForkDiagIngressSlot(s32 player, u32 packet_seq, u32 wire_tick, u16 buttons, s8 stick_x,
                                           s8 stick_y, u32 local_sim)
{
	u32 sim_tick;
	u32 begin;
	u32 end;
	SYNetInputFrame frame;

	if (syNetInputForkDiagWireInWindow(wire_tick) == FALSE)
	{
		return;
	}
	sim_tick = syNetPeerDelaySimTickFromWire(wire_tick);
	if (syNetInputForkDiagWindow(sim_tick, &begin, &end) == FALSE)
	{
		return;
	}
	syNetInputMakeFrame(&frame, wire_tick, buttons, stick_x, stick_y, nSYNetInputSourceRemoteConfirmed, FALSE);
	port_log(
	    "SSB64 NetInput: fork_ingress player=%d pkt_seq=%u wire=%u sim=%u btn=0x%04X sx=%d sy=%d "
	    "window=[%u,%u] local_sim=%u hr=%u remote_sim_frontier=%u\n",
	    (int)player,
	    (unsigned int)packet_seq,
	    (unsigned int)wire_tick,
	    (unsigned int)sim_tick,
	    (unsigned int)buttons,
	    (int)stick_x,
	    (int)stick_y,
	    begin,
	    end,
	    (unsigned int)local_sim,
	    (unsigned int)syNetPeerGetHighestRemoteTick(),
	    (unsigned int)((syNetPeerGetHighestRemoteTick() != 0U)
	                        ? syNetPeerDelaySimTickFromWire(syNetPeerGetHighestRemoteTick())
	                        : 0U));
}

void syNetInputMaybeLogForkDiagSimRow(u32 tick, const SYNetInputFrame *sim_consumed)
{
	u32 begin;
	u32 end;
	s32 player;
	SYNetInputFrame published;
	SYNetInputFrame remote_confirmed;
	SYNetInputFrame remote_wire;
	SYNetInputFrame local_delayed;
	const SYNetInputFrame *sim_row;
	u32 req_wire;
	u32 wire_from_sim;
	u32 hr;
	u32 remote_sim_frontier;
	u32 d;
	sb32 used_pred;

	if ((sim_consumed == NULL) || (syNetInputForkDiagWindow(tick, &begin, &end) == FALSE))
	{
		return;
	}
	req_wire = syNetPeerGetBaseRequiredWireTick(tick);
	wire_from_sim = syNetPeerDelayWireTickFromSim(tick);
	hr = syNetPeerGetHighestRemoteTick();
	remote_sim_frontier = (hr != 0U) ? syNetPeerDelaySimTickFromWire(hr) : 0U;
	d = syNetPeerGetCommittedInputDelay();
	used_pred = syNetInputSimTickUsedPredictedRemote(tick);
	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		sb32 has_pub;
		sb32 has_conf;
		sb32 has_wire;
		sb32 has_local_delayed;

		sim_row = &sim_consumed[player];
		has_pub = syNetInputGetHistoryFrame(player, tick, &published);
		has_conf = syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, tick, &remote_confirmed);
		has_wire = syNetInputTryGetRemoteHistoryForSimTick(player, tick, &remote_wire);
		has_local_delayed = syNetInputGetLocalDelayedFrame(player, tick, &local_delayed);
		port_log(
		    "SSB64 NetInput: fork_sim_row tick=%u player=%d window=[%u,%u] D=%u req_wire=%u wire_from_sim=%u "
		    "hr=%u remote_sim_frontier=%u used_pred_remote=%d local_slot=%d "
		    "pub_valid=%d pub_btn=0x%04X pub_sx=%d pub_sy=%d pub_pred=%u | "
		    "sim_valid=%d sim_btn=0x%04X sim_sx=%d sim_sy=%d sim_pred=%u | "
		    "conf=%d conf_btn=0x%04X conf_sx=%d conf_sy=%d | "
		    "wire_ring=%d wire_btn=0x%04X wire_pred=%u | "
		    "local_delayed=%d ld_btn=0x%04X ld_sx=%d ld_sy=%d\n",
		    tick,
		    (int)player,
		    begin,
		    end,
		    (unsigned int)d,
		    (unsigned int)req_wire,
		    (unsigned int)wire_from_sim,
		    (unsigned int)hr,
		    (unsigned int)remote_sim_frontier,
		    (int)used_pred,
		    (int)syNetPeerGetLocalSimSlot(),
		    (int)has_pub,
		    has_pub ? (unsigned int)published.buttons : 0U,
		    has_pub ? (int)published.stick_x : 0,
		    has_pub ? (int)published.stick_y : 0,
		    has_pub ? (unsigned int)published.is_predicted : 0U,
		    (int)sim_row->is_valid,
		    (unsigned int)sim_row->buttons,
		    (int)sim_row->stick_x,
		    (int)sim_row->stick_y,
		    (unsigned int)sim_row->is_predicted,
		    (int)has_conf,
		    has_conf ? (unsigned int)remote_confirmed.buttons : 0U,
		    has_conf ? (int)remote_confirmed.stick_x : 0,
		    has_conf ? (int)remote_confirmed.stick_y : 0,
		    (int)has_wire,
		    has_wire ? (unsigned int)remote_wire.buttons : 0U,
		    has_wire ? (unsigned int)remote_wire.is_predicted : 0U,
		    (int)has_local_delayed,
		    has_local_delayed ? (unsigned int)local_delayed.buttons : 0U,
		    has_local_delayed ? (int)local_delayed.stick_x : 0,
		    has_local_delayed ? (int)local_delayed.stick_y : 0);
	}
}

void syNetInputMaybeLogDivergenceInputRow(u32 tick, const SYNetInputFrame *sim_consumed)
{
	u32 begin;
	u32 end;
	s32 player;
	SYNetInputFrame published;
	SYNetInputFrame remote_confirmed;
	SYNetInputFrame remote_wire;
	const SYNetInputFrame *sim_row;
	const char *patch_reason;

	if (syNetInputDivergenceInputLogWindow(tick, &begin, &end) == FALSE)
	{
		return;
	}
	if (sim_consumed == NULL)
	{
		return;
	}
	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		sb32 has_pub;
		sb32 has_conf;
		sb32 has_wire;

		sim_row = &sim_consumed[player];
		has_pub = syNetInputGetHistoryFrame(player, tick, &published);
		has_conf = syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, tick, &remote_confirmed);
		has_wire = syNetInputTryGetRemoteHistoryForSimTick(player, tick, &remote_wire);
		patch_reason = "none";
		if (has_conf != FALSE)
		{
			patch_reason = "remote_confirmed";
		}
		else if (has_wire != FALSE)
		{
			if (remote_wire.is_predicted != FALSE)
			{
				patch_reason = "predicted_wire";
			}
			else
			{
				patch_reason = "wire_ring";
			}
		}
		else if (has_pub != FALSE)
		{
			patch_reason = "published_only";
		}
		port_log(
		    "SSB64 NetInput: divergence_row tick=%u player=%d window=[%u,%u] "
		    "pub_btn=0x%04X pub_sx=%d pub_sy=%d pub_pred=%u pub_valid=%d | "
		    "sim_btn=0x%04X sim_sx=%d sim_sy=%d sim_pred=%u sim_valid=%d | "
		    "remote_conf=%d conf_sx=%d conf_sy=%d | patch_reason=%s\n",
		    tick,
		    (int)player,
		    begin,
		    end,
		    has_pub ? (unsigned int)published.buttons : 0U,
		    has_pub ? (int)published.stick_x : 0,
		    has_pub ? (int)published.stick_y : 0,
		    has_pub ? (unsigned int)published.is_predicted : 0U,
		    has_pub ? (int)published.is_valid : 0,
		    (unsigned int)sim_row->buttons,
		    (int)sim_row->stick_x,
		    (int)sim_row->stick_y,
		    (unsigned int)sim_row->is_predicted,
		    (int)sim_row->is_valid,
		    (int)has_conf,
		    has_conf ? (int)remote_confirmed.stick_x : 0,
		    has_conf ? (int)remote_confirmed.stick_y : 0,
		    patch_reason);
	}
}

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
	u32 req_wire;

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
	if (syNetInputAuthoritativeWireContractEnabled() == FALSE)
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
	req_wire = syNetPeerGetStrictRequiredWireTick(tick);
	port_log(
	    "SSB64 NetInput: input_predict_diag tick=%u required_wire=%u strict_slack=%d prediction_env_on=%d\n",
	    tick,
	    req_wire,
	    syNetInputGetStrictExtraSlack(),
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
	if ((sSYNetInputFrameCommitDiagLevelCache < 2) &&
	    (syNetInputForkDiagWindow(tick, NULL, NULL) == FALSE) && ((tick % 60U) != 0U))
	{
		return;
	}
	hr = syNetPeerGetHighestRemoteTick();
	port_log(
	    "SSB64 NetInput: frame_commit_diag tick=%u path=%c exec_ok=%d publish=%d sup=%d hr=%u wire=%u commit_gen=%u pred_win=%u "
	    "remote_sim_frontier=%u D=%u\n",
	    tick,
	    (int)(unsigned char)path,
	    (exec_ok != FALSE) ? 1 : 0,
	    (publish != FALSE) ? 1 : 0,
	    (sSYNetInputSuppressSceneUpdateAfterRead != FALSE) ? 1 : 0,
	    (unsigned int)hr,
	    (unsigned int)syNetPeerGetBaseRequiredWireTick(tick),
	    (unsigned int)syNetPeerGetGlobalCommitGen(),
	    (unsigned int)syNetPeerGetPhaseLockPredictionWindowTicks(),
	    (unsigned int)((hr != 0U) ? syNetPeerDelaySimTickFromWire(hr) : 0U),
	    (unsigned int)syNetPeerGetCommittedInputDelay());
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
	u32 wire_base;
	u32 wire_req;
	u32 wire_eff;
	u32 lead_b;
	u32 hr_need;
	u32 d;
	u32 hr;
	u32 wire_slack;
	u32 buf_min_slack;
	int lvl;
	s32 n;
	s32 i;
	s32 slot;
	sb32 boot_window;
	sb32 d_changed;
	sb32 have_ring;
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

	wire_base = syNetPeerGetBaseRequiredWireTick(sim_tick_for_read);
	wire_req = syNetPeerGetStrictRequiredWireTick(sim_tick_for_read);
	hr = syNetPeerGetHighestRemoteTick();
	wire_eff = syNetPeerGetEffectiveWireFrontierForAdmission(sim_tick_for_read);
	wire_slack = (hr > wire_eff) ? (hr - wire_eff) : 0U;
	buf_min_slack = (syNetInputEnvGetMatchInputDelayOrNeg1() >= 0) ? syNetPeerGetMatchInputBufferMinSlackTicks() : 0U;
	lead_b = syNetInputGetStrictRemoteLeadBufferTicks();
	if (lead_b == 0U)
	{
		hr_need = 0U;
	}
	else
	{
		u32 slack_diag;

		slack_diag = (hr > wire_eff) ? (hr - wire_eff) : 0U;
		if (wire_eff > ~(u32)0U - ((slack_diag < lead_b) ? slack_diag : lead_b))
		{
			hr_need = ~(u32)0U;
		}
		else
		{
			hr_need = wire_eff + ((slack_diag < lead_b) ? slack_diag : lead_b);
		}
	}
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
		have_ring = syNetInputHasRemoteInputForWireTick(slot, wire_eff);
		log_slot[log_n] = slot;
		log_ok[log_n] = (have_ring != FALSE) ? 1 : 0;
		log_n++;
	}

	port_log(
	    "SSB64 NetInput: delay_sync_diag sim_read=%u D=%u wire_base=%u wire_cap=%u wire_eff=%u wire_slack=%u buf_min_slack=%u strict_slack=%d hr=%u lead_b=%u hr_need=%u ls=%d rs0=%d rok0=%d rs1=%d rok1=%d d_ch=%d boot=%d\n",
	    (unsigned int)sim_tick_for_read,
	    (unsigned int)d,
	    (unsigned int)wire_base,
	    (unsigned int)wire_req,
	    (unsigned int)wire_eff,
	    (unsigned int)wire_slack,
	    (unsigned int)buf_min_slack,
	    syNetInputGetStrictExtraSlack(),
	    (unsigned int)hr,
	    (unsigned int)lead_b,
	    (unsigned int)hr_need,
	    (int)syNetPeerGetLocalSimSlot(),
	    (log_n > 0) ? (int)log_slot[0] : -1,
	    (log_n > 0) ? log_ok[0] : -1,
	    (log_n > 1) ? (int)log_slot[1] : -1,
	    (log_n > 1) ? log_ok[1] : -1,
	    (d_changed != FALSE) ? 1 : 0,
	    (boot_window != FALSE) ? 1 : 0);
}

/*
 * Strict wire admission (`nSYNetTickCommitPhase_FuncReadWireAdmission`): `syNetPeerEvaluateSharedCommitStep` first
 * requires **every remote human slot** to have a ring cell at **`wire_base = sim_tick + D`** (`syNetPeerRemoteInputsPresentForWireTick`).
 * `hr` from `syNetPeerGetHighestRemoteTick()` is the highest **wire index** seen in ingress (not sim tick).
 * **`wire_strict = wire_base + strict_slack`** (`SSB64_NETPLAY_STRICT_SLACK_FRAMES` et al., capped 0..4) caps how far
 * `syNetPeerEffectiveWireFrontierFromHr` may sit ahead when resolving frames elsewhere (`syNetPeerIsRemoteInputReadyForSimTickEx`);
 * the shared-commit gate itself keys off `wire_base` only.
 * When rollback has used predicted remote input and arms recovery, `syNetRollbackPredictionRecoveryRequiresConfirmed`
 * returns TRUE until `sim_tick` reaches `frontier + PHASE_LOCK_PREDICTION_TICKS` — then missing `wire_base` stalls as **R**
 * (no prediction escape) until inputs arrive or `SSB64_NETPLAY_STRICT_R_ABORT_FRAMES` tears down VS.
 * On abort: verdict stays blocked (`strict_remote_stall_abort`); FuncRead returns without publish (must not
 * `syNetTickCommitVerdictAllowAll` or the stall counter resets on the accidental publish path).
 */
static int syNetInputGetStrictStallDiagLevel(void)
{
	const char *e;

	if (sSYNetInputStrictStallDiagLevelCache != -999)
	{
		return sSYNetInputStrictStallDiagLevelCache;
	}
	e = getenv("SSB64_NETPLAY_STRICT_STALL_DIAG");
	sSYNetInputStrictStallDiagLevelCache = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 0;
	if (sSYNetInputStrictStallDiagLevelCache < 0)
	{
		sSYNetInputStrictStallDiagLevelCache = 0;
	}
	if (sSYNetInputStrictStallDiagLevelCache > 2)
	{
		sSYNetInputStrictStallDiagLevelCache = 2;
	}
	return sSYNetInputStrictStallDiagLevelCache;
}

void syNetInputMaybeLogStrictStallDiag(u32 tick, char admission_letter, sb32 suppress_scene, sb32 partial_publish,
                                       const char *phase_tag)
{
	int lvl;
	u32 wire_base;
	u32 wire_eff;
	u32 hr;
	u32 wire_gap;
	sb32 should_log;

	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	lvl = syNetInputGetStrictStallDiagLevel();
	if (lvl == 0)
	{
		return;
	}
	if ((admission_letter != 'R') && (admission_letter != 'V') && (admission_letter != 'W') &&
	    (admission_letter != 'S') && (admission_letter != 'K'))
	{
		return;
	}
	if (tick != sSYNetInputStrictStallDiagLastTick)
	{
		sSYNetInputStrictStallDiagLastTick = tick;
		sSYNetInputStrictStallDiagLastFrames = 0U;
	}
	sSYNetInputStrictStallDiagLastFrames++;
	should_log = FALSE;
	if (lvl >= 2)
	{
		should_log = TRUE;
	}
	else if (sSYNetInputStrictStallDiagLastFrames == 1U)
	{
		should_log = TRUE;
	}
	else if ((sSYNetInputStrictStallDiagLastFrames % 30U) == 0U)
	{
		should_log = TRUE;
	}
	else if ((syNetRollbackGetAppliedResimCount() != 0U) && (sSYNetInputStrictStallDiagLastFrames <= 3U))
	{
		should_log = TRUE;
	}
	if (should_log == FALSE)
	{
		return;
	}
	wire_base = syNetPeerGetBaseRequiredWireTick(tick);
	wire_eff = syNetPeerGetEffectiveWireFrontierForAdmission(tick);
	hr = syNetPeerGetHighestRemoteTick();
	wire_gap = (hr < wire_base) ? (wire_base - hr) : 0U;
	port_log(
	    "SSB64 NetInput: strict_stall_diag phase=%s sim=%u admit=%c sup=%d partial=%d stuck=%u hr=%u wire_base=%u wire_eff=%u wire_gap=%u D=%u rb_resim=%d rb_applied=%u commit_gen=%u\n",
	    (phase_tag != NULL) ? phase_tag : "funcread",
	    (unsigned int)tick,
	    (int)(unsigned char)admission_letter,
	    (suppress_scene != FALSE) ? 1 : 0,
	    (partial_publish != FALSE) ? 1 : 0,
	    (unsigned int)sSYNetInputStrictStallDiagLastFrames,
	    (unsigned int)hr,
	    (unsigned int)wire_base,
	    (unsigned int)wire_eff,
	    (unsigned int)wire_gap,
	    (unsigned int)syNetPeerGetCommittedInputDelay(),
	    (syNetRollbackIsResimulating() != FALSE) ? 1 : 0,
	    (unsigned int)syNetRollbackGetAppliedResimCount(),
	    (unsigned int)syNetPeerGetGlobalCommitGen());
}

void syNetInputMaybeLogNetSliceDiag(u32 tick, sb32 allow_battle_sim, sb32 allow_net_update)
{
	int lvl;

	lvl = syNetInputGetStrictStallDiagLevel();
	if (lvl == 0)
	{
		return;
	}
	port_log(
	    "SSB64 NetPlay: net_slice_diag sim=%u allow_battle_sim=%d allow_net_update=%d rb_resim=%d rb_applied=%u hr=%u wire_base=%u\n",
	    (unsigned int)tick,
	    (allow_battle_sim != FALSE) ? 1 : 0,
	    (allow_net_update != FALSE) ? 1 : 0,
	    (syNetRollbackIsResimulating() != FALSE) ? 1 : 0,
	    (unsigned int)syNetRollbackGetAppliedResimCount(),
	    (unsigned int)syNetPeerGetHighestRemoteTick(),
	    (unsigned int)syNetPeerGetBaseRequiredWireTick(tick));
}

static void syNetInputLogStrictDecision(u32 tick, u32 wire_base, u32 d, u32 hr, sb32 miss)
{
	u32 wire_strict;
	sb32 pred_rec;

	if ((miss != FALSE) && (sSYNetInputStrictRStuckFrames > 0U) && ((sSYNetInputStrictRStuckFrames % 120U) != 0U))
	{
		return;
	}
	wire_strict = syNetPeerGetStrictRequiredWireTick(tick);
	pred_rec = syNetRollbackPredictionRecoveryRequiresConfirmed(tick);
	port_log(
	    "STRICT: tick=%u wire_base=%u (sim+D) wire_strict=%u D=%u slack=%d hr=%u pred_rec=%d -> %s\n",
	    (unsigned int)tick, (unsigned int)wire_base, (unsigned int)wire_strict, (unsigned int)d,
	    syNetInputGetStrictExtraSlack(), (unsigned int)hr, (pred_rec != FALSE) ? 1 : 0,
	    (miss != FALSE) ? "MISS (R)" : "READY");
}

#define SYNETINPUT_STRICT_R_ABORT_FRAMES_DEFAULT 180U

static sb32 syNetInputStrictRemoteMissAbortIfStuck(u32 sim_tick)
{
	u32 limit;
	const char *env;
	s32 parsed;

	if (sSYNetInputStrictRStuckSimTick != sim_tick)
	{
		sSYNetInputStrictRStuckSimTick = sim_tick;
		sSYNetInputStrictRStuckFrames = 1U;
	}
	else if (sSYNetInputStrictRStuckFrames < ~(u32)0)
	{
		sSYNetInputStrictRStuckFrames++;
	}
	limit = SYNETINPUT_STRICT_R_ABORT_FRAMES_DEFAULT;
	env = getenv("SSB64_NETPLAY_STRICT_R_ABORT_FRAMES");
	if ((env != NULL) && (env[0] != '\0'))
	{
		parsed = atoi(env);
		if (parsed > 0)
		{
			limit = (u32)parsed;
		}
	}
	if (syNetPeerStrictTeardownFastPathActive() != FALSE)
	{
		limit = 1U;
	}
#if defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE)
	if (syNetReconnectHoldActive() != FALSE)
	{
		return FALSE;
	}
#endif
	if (sSYNetInputStrictRStuckFrames < limit)
	{
		return FALSE;
	}
	sSYNetInputStrictRStuckSimTick = ~(u32)0;
	sSYNetInputStrictRStuckFrames = 0U;
	port_log(
	    "SSB64 NetInput: strict remote MISS stall abort sim=%u frames=%u hr=%u wire_base=%u wire_strict=%u D=%u "
	    "slack=%d pred_rec=%d (gate uses wire_base; see STRICT log) - ending VS session\n",
	    (unsigned int)sim_tick, (unsigned int)sSYNetInputStrictRStuckFrames,
	    (unsigned int)syNetPeerGetHighestRemoteTick(), (unsigned int)syNetPeerGetBaseRequiredWireTick(sim_tick),
	    (unsigned int)syNetPeerGetStrictRequiredWireTick(sim_tick), (unsigned int)syNetPeerGetCommittedInputDelay(),
	    syNetInputGetStrictExtraSlack(), (syNetRollbackPredictionRecoveryRequiresConfirmed(sim_tick) != FALSE) ? 1 : 0);
	syNetPeerSendVsSessionEndNotifyPeer();
	syNetPeerStopVSSession();
	return TRUE;
}

static void syNetInputMaybeIngressExtraPumpsOnStall(void)
{
	int n;
	int i;
	const char *e;

	if (sSYNetInputSessionIngressPumpsOverrideValid != FALSE)
	{
		n = sSYNetInputSessionIngressPumpsOverride;
	}
	else if (sSYNetInputIngressExtraPumpsEnvCache < 0)
	{
		e = getenv("SSB64_NETPLAY_INGRESS_EXTRA_PUMPS_ON_STALL");
		n = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 0;
		if (n < 0)
		{
			n = 0;
		}
		if (n > 4)
		{
			n = 4;
		}
		sSYNetInputIngressExtraPumpsEnvCache = n;
	}
	n = sSYNetInputIngressExtraPumpsEnvCache;
	for (i = 0; i < n; i++)
	{
		syNetPeerPumpIngressTransport("stall_extra");
	}
}

static SYNetTickCommitVerdict sSYNetTickCommitFuncReadVerdict;
static u32 sSYNetTickCommitFuncReadVerdictTick = ~(u32)0U;

static void syNetTickCommitVerdictAllowAll(SYNetTickCommitVerdict *o)
{
	o->allow_full_input_publish = TRUE;
	o->allow_battle_sim_step = TRUE;
	o->suppress_scene_update = FALSE;
	o->strict_partial_publish_local = FALSE;
	o->strict_remote_stall_abort = FALSE;
	o->admission_letter = 'P';
}

static void syNetTickCommitStoreFuncReadCache(const SYNetTickCommitVerdict *v, u32 tick)
{
	sSYNetTickCommitFuncReadVerdict = *v;
	sSYNetTickCommitFuncReadVerdictTick = tick;
}

void syNetTickCommitEvaluate(u32 tick, SYNetTickCommitPhase phase, SYNetTickCommitVerdict *out)
{
	if (out == NULL)
	{
		return;
	}
	syNetTickCommitVerdictAllowAll(out);
#if defined(PORT)
	if (syNetRollbackShouldBlockLiveBattleAdvance(tick) != FALSE)
	{
		static u32 sLastHoldBlockedTickCommitLogTick = ~(u32)0;

		if (tick != sLastHoldBlockedTickCommitLogTick)
		{
			port_log(
			    "SSB64 NetInput: tick_commit blocked (load_fail_hold) tick=%u phase=%d peer_vs_active=%d resim=%d\n",
			    tick,
			    (int)phase,
			    (int)syNetPeerIsVSSessionActive(),
			    (int)syNetRollbackIsResimulating());
			sLastHoldBlockedTickCommitLogTick = tick;
		}
		out->allow_full_input_publish = FALSE;
		out->allow_battle_sim_step = FALSE;
		out->suppress_scene_update = TRUE;
		if (out->admission_letter == 'P')
		{
			out->admission_letter = 'L';
		}
		return;
	}
#endif
	if (phase == nSYNetTickCommitPhase_NetSlice)
	{
		if ((syNetPeerIsVSSessionActive() == FALSE) || (syNetRollbackIsResimulating() != FALSE))
		{
			return;
		}
		if (syNetPeerCheckBattleExecutionReady() == FALSE)
		{
			out->allow_full_input_publish = FALSE;
			out->allow_battle_sim_step = FALSE;
			out->admission_letter = 'E';
			return;
		}
#ifdef PORT
		if (syNetPeerBootstrapIngressSymmetrySatisfied() == FALSE)
		{
			out->allow_full_input_publish = FALSE;
			out->allow_battle_sim_step = FALSE;
			out->admission_letter = 'E';
			return;
		}
#endif
		return;
	}
	if (phase == nSYNetTickCommitPhase_FuncReadExecGate)
	{
		if ((syNetPeerIsVSSessionActive() == FALSE) || (syNetRollbackIsResimulating() != FALSE))
		{
			return;
		}
		if (syNetPeerCheckBattleExecutionReady() == FALSE)
		{
			out->allow_full_input_publish = FALSE;
			out->allow_battle_sim_step = FALSE;
			out->admission_letter = 'E';
			return;
		}
#ifdef PORT
		if (syNetPeerBootstrapIngressSymmetrySatisfied() == FALSE)
		{
			out->allow_full_input_publish = FALSE;
			out->allow_battle_sim_step = FALSE;
			out->admission_letter = 'E';
			return;
		}
#endif
		return;
	}
	/* nSYNetTickCommitPhase_FuncReadWireAdmission */
	if ((syNetPeerIsVSSessionActive() == FALSE) || (syNetRollbackIsResimulating() != FALSE))
	{
		return;
	}
	if (syNetInputAuthoritativeWireContractEnabled() != FALSE)
	{
		SYNetPeerSharedCommitStep shared;

		syNetInputMaybeLogDelaySyncDiag(tick);
		syNetPeerEvaluateSharedCommitStep(tick, &shared);
		if (shared.advance == FALSE)
		{
			out->allow_full_input_publish = FALSE;
			out->allow_battle_sim_step = FALSE;
			out->suppress_scene_update =
			    (shared.hold_reason == 'R' || shared.hold_reason == 'H') ? TRUE : FALSE;
			out->strict_partial_publish_local =
			    (shared.hold_reason == 'R' || shared.hold_reason == 'H') ? TRUE : FALSE;
			out->admission_letter = shared.hold_reason;
			if (shared.hold_reason == 'R')
			{
				syNetInputMaybeIngressExtraPumpsOnStall();
				if (syNetInputStrictRemoteMissAbortIfStuck(tick) != FALSE)
				{
					out->allow_full_input_publish = FALSE;
					out->allow_battle_sim_step = FALSE;
					out->suppress_scene_update = TRUE;
					out->strict_partial_publish_local = FALSE;
					out->strict_remote_stall_abort = TRUE;
					out->admission_letter = 'A';
					return;
				}
				syNetInputLogStrictDecision(tick, shared.required_wire, syNetPeerGetCommittedInputDelay(),
				                            syNetPeerGetHighestRemoteTick(), TRUE);
			}
			else if (shared.hold_reason == 'H')
			{
				syNetInputMaybeIngressExtraPumpsOnStall();
			}
			return;
		}
		if (syNetPeerShouldHoldSimTickForSkewPacing(tick, NULL) != FALSE)
		{
			out->allow_full_input_publish = FALSE;
			out->allow_battle_sim_step = FALSE;
			out->suppress_scene_update = TRUE;
			out->strict_partial_publish_local = TRUE;
			out->admission_letter = 'R';
			syNetInputMaybeIngressExtraPumpsOnStall();
			if (syNetInputStrictRemoteMissAbortIfStuck(tick) != FALSE)
			{
				out->allow_full_input_publish = FALSE;
				out->allow_battle_sim_step = FALSE;
				out->suppress_scene_update = TRUE;
				out->strict_partial_publish_local = FALSE;
				out->strict_remote_stall_abort = TRUE;
				out->admission_letter = 'A';
				return;
			}
			return;
		}
		if (syNetInputRemoteHumanWireReadyForSimTick(tick) == FALSE)
		{
			out->allow_full_input_publish = FALSE;
			out->allow_battle_sim_step = FALSE;
			out->suppress_scene_update = TRUE;
			out->strict_partial_publish_local = TRUE;
			out->admission_letter = 'R';
			syNetInputMaybeIngressExtraPumpsOnStall();
			if (syNetInputStrictRemoteMissAbortIfStuck(tick) != FALSE)
			{
				out->allow_full_input_publish = FALSE;
				out->allow_battle_sim_step = FALSE;
				out->suppress_scene_update = TRUE;
				out->strict_partial_publish_local = FALSE;
				out->strict_remote_stall_abort = TRUE;
				out->admission_letter = 'A';
				return;
			}
			syNetInputLogStrictDecision(tick, shared.required_wire, syNetPeerGetCommittedInputDelay(),
			                            syNetPeerGetHighestRemoteTick(), TRUE);
			return;
		}
		if (shared.uses_prediction != FALSE)
		{
			syNetInputStrictReadyCacheInvalidate();
		}
		return;
	}
}

sb32 syNetTickCommitAllowsBattleSimFromLastFuncReadEvaluate(void)
{
	if (syNetRollbackIsBattleSimHoldActive() != FALSE)
	{
		return FALSE;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return TRUE;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return TRUE;
	}
	if (sSYNetTickCommitFuncReadVerdictTick != syNetInputGetTick())
	{
		return FALSE;
	}
	return sSYNetTickCommitFuncReadVerdict.allow_battle_sim_step;
}
#endif

/*
 * Per sim tick: sample HID at most once (see `sSYNetInputPortHwLatchTick`), **before** stall-until-remote / skew checks
 * so local capture is indexed only by `sSYNetInputTick`, then synchronize all slots, publish to globals + rings,
 * optional replay capture. Tick advances only after a full `scVSBattleFuncUpdate` (`syNetInputAdvanceAuthoritativeSimTick`).
 *
 * Linux UDP VS: `syNetPeerUpdateBattleGate` + `syNetTickCommitEvaluate` (exec gate then wire admission) run **before**
 * resolve+publish so a frame cycle cannot publish authoritative inputs and then fail to sim for execution-revocation reasons.
 */
void syNetInputFuncRead(void)
{
	SYNetInputFrame synchronized[MAXCONTROLLERS];
	u32 tick;
	s32 player;

#ifdef PORT
	sSYNetInputSuppressSceneUpdateAfterRead = FALSE;
	sSYNetInputStrictContractSkippedPublish = FALSE;
	/* Resim publishes via syNetInputPublishSynchronizedTick in AdvanceResimBudget — avoid double-publish. */
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	if (syNetPeerIsVSSessionActive() != FALSE)
	{
		SYNetTickCommitVerdict tcv;

		syNetPeerUpdateBattleGate();
		tick = syNetInputGetTick();
		syNetTickCommitEvaluate(tick, nSYNetTickCommitPhase_FuncReadExecGate, &tcv);
		if ((tcv.allow_full_input_publish == FALSE) && (tcv.admission_letter == 'E'))
		{
			syNetTickCommitStoreFuncReadCache(&tcv, tick);
			syNetInputAdmissionBump(tcv.admission_letter);
			syNetInputMaybeLogFrameCommitDiag(tick, tcv.admission_letter, FALSE, FALSE);
			/*
			 * Exec-hold must still pump the peer loop so INPUT_BIND / BATTLE_EXEC_SYNC can complete; battle
			 * update may not run this task iteration (decouple suppress, or early return on same verdict).
			 */
			syNetPeerUpdate();
			return;
		}
	}
	else
	{
		syNetPeerPumpIngressBeforeInputRead();
	}
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
			syNetInputStageLocalDelayFramesFromLatch(tick);
			syNetInputNeutralizeAllControllerDevices();
			sSYNetInputPortHwLatchTick = tick;
		}
		syNetInputPromoteAllLocalAuthoritySlots(tick);
		if (syNetPeerIsVSSessionActive() != FALSE)
		{
			SYNetTickCommitVerdict tcv;

			syNetTickCommitEvaluate(tick, nSYNetTickCommitPhase_FuncReadWireAdmission, &tcv);
			syNetTickCommitStoreFuncReadCache(&tcv, tick);
			if (tcv.strict_remote_stall_abort != FALSE)
			{
				syNetInputAdmissionBump(tcv.admission_letter);
				syNetInputMaybeLogFrameCommitDiag(tick, tcv.admission_letter, FALSE, FALSE);
				return;
			}
			if (tcv.allow_full_input_publish == FALSE)
			{
				if (tcv.strict_partial_publish_local != FALSE)
				{
					syNetInputStrictContractPartialPublishLocalFromLatch(tick);
					sSYNetInputStrictContractSkippedPublish = TRUE;
				}
				if (tcv.suppress_scene_update != FALSE)
				{
					sSYNetInputSuppressSceneUpdateAfterRead = TRUE;
				}
				syNetInputMaybeLogStrictStallDiag(tick, tcv.admission_letter, tcv.suppress_scene_update,
				                                  tcv.strict_partial_publish_local, "funcread");
				// #region agent log
				if ((tcv.admission_letter == 'R') || (tcv.admission_letter == 'H'))
				{
					char agent_data[384];

					snprintf(agent_data, sizeof(agent_data),
					         "{\"tick\":%u,\"admit\":\"%c\",\"sup\":%d,\"partial\":%d,\"allow_battle_sim\":%d,"
					         "\"allow_publish\":%d,\"hr\":%u,\"wire_base\":%u,\"push\":%d,\"commit_gen\":%u}",
					         (unsigned int)tick, (int)tcv.admission_letter,
					         (tcv.suppress_scene_update != FALSE) ? 1 : 0,
					         (tcv.strict_partial_publish_local != FALSE) ? 1 : 0,
					         (tcv.allow_battle_sim_step != FALSE) ? 1 : 0,
					         (tcv.allow_full_input_publish != FALSE) ? 1 : 0,
					         (unsigned int)syNetPeerGetHighestRemoteTick(),
					         (unsigned int)syNetPeerGetBaseRequiredWireTick(tick), port_get_push_frame_count(),
					         (unsigned int)syNetPeerGetGlobalCommitGen());
					net_debug_agent_log_line("BC", "netinput.c:funcread_stall", "funcread_stall_return", agent_data);
				}
				// #endregion
				syNetInputAdmissionBump(tcv.admission_letter);
				syNetInputMaybeLogFrameCommitDiag(tick, tcv.admission_letter, TRUE, FALSE);
				if ((tcv.admission_letter == 'R') || (tcv.admission_letter == 'V'))
				{
					syNetInputMaybeIngressExtraPumpsOnStall();
				}
				return;
			}
		}
	}
#else
	syControllerFuncRead();
#endif

#ifdef PORT
	if ((syNetPeerIsVSSessionActive() != FALSE) && (syNetRollbackIsResimulating() == FALSE))
	{
		syNetInputPumpIngressAndPromoteRemoteThroughTick(tick);
	}
#endif
	syNetInputSynchronizeInputsForTick(tick, synchronized);
#ifdef PORT
	syNetInputNoteSimTickPredictedRemoteUsage(tick, synchronized);
	syNetInputMaybeLogInputPredictDiag(tick, synchronized);
	syNetInputMaybeLogDivergenceInputRow(tick, synchronized);
	syNetInputMaybeLogForkDiagSimRow(tick, synchronized);
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
	syNetInputPromoteAllLocalAuthoritySlots(tick);
	syNetInputPromoteAllRemoteHumanAuthoritySlots(tick);

#ifdef PORT
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

#ifdef PORT
void syNetInputPublishSynchronizedTick(u32 tick)
{
	SYNetInputFrame synchronized[MAXCONTROLLERS];
	s32 player;

	syNetInputSynchronizeInputsForTick(tick, synchronized);
#ifdef PORT
	syNetInputNoteSimTickPredictedRemoteUsage(tick, synchronized);
	syNetInputMaybeLogDivergenceInputRow(tick, synchronized);
	syNetInputMaybeLogForkDiagSimRow(tick, synchronized);
#endif
	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		syNetInputPublishFrame(player, &synchronized[player]);
	}
	syNetInputPublishMainController();
}
#endif

static void syNetInputRestoreRemoteConfirmedSeed(s32 player, u32 resim_start_tick)
{
	SYNetInputFrame frame;
	u32 t;

	if (syNetInputCheckPlayer(player) == FALSE)
	{
		return;
	}
#ifdef PORT
		syNetInputClearFrame(&sSYNetInputSlots[player].last_non_neutral);
		sSYNetInputSlots[player].remote_encoding_was_digital = FALSE;
		sSYNetInputSlots[player].remote_encoding_grace_until_tick = 0U;
#endif
	if (resim_start_tick == 0)
	{
		syNetInputClearFrame(&sSYNetInputSlots[player].last_confirmed);
		return;
	}
	for (t = resim_start_tick; t > 0; t--)
	{
		if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, t - 1, &frame) != FALSE)
		{
			sSYNetInputSlots[player].last_confirmed = frame;
			break;
		}
	}
	if (sSYNetInputSlots[player].last_confirmed.is_valid == FALSE)
	{
		syNetInputClearFrame(&sSYNetInputSlots[player].last_confirmed);
		return;
	}
#ifdef PORT
	{
		u32 lookback;
		u32 oldest;

		lookback = syNetInputAnalogOnsetLookbackTicks();
		oldest = (resim_start_tick > lookback) ? (resim_start_tick - lookback) : 0U;
		for (t = resim_start_tick; t > oldest; t--)
		{
			if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, t - 1, &frame) != FALSE)
			{
				if (syNetInputFrameSticksNearNeutral(&frame) == FALSE)
				{
					sSYNetInputSlots[player].last_non_neutral = frame;
					sSYNetInputSlots[player].remote_encoding_was_digital =
					    (syNetInputFrameIsDigitalKeyboard(&frame) != FALSE) ? TRUE : FALSE;
					return;
				}
			}
		}
		sSYNetInputSlots[player].remote_encoding_was_digital =
		    (syNetInputFrameIsDigitalKeyboard(&sSYNetInputSlots[player].last_confirmed) != FALSE) ? TRUE : FALSE;
	}
#endif
}

/*
 * Before `syNetRollbackRunResim` replays ticks: rewind `last_published` to the state *before* mismatch tick and restore
 * remote “last confirmed” used by `MakePredictedFrame` so gaps still predict coherently mid-resim.
 */
void syNetInputRollbackPrepareForResim(u32 resim_start_tick)
{
	s32 player;
	SYNetInputFrame frame;

#ifdef PORT
	syNetInputTimelineClearIncorrectFrom(resim_start_tick);
#endif
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

#ifdef PORT
static u32 sSYNetInputResimReconcileMissLogFrom;

static void syNetInputRollbackReconcileRemoteSlotFromWire(s32 slot, u32 t)
{
	SYNetInputFrame row;

	if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(slot, t, &row) != FALSE)
	{
		row.tick = t;
		row.source = nSYNetInputSourceRemoteConfirmed;
		row.is_predicted = FALSE;
		syNetInputStoreFrame(sSYNetInputHistory, slot, &row);
		return;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		/*
		 * Forward sim may have consumed predicted remote rows while published digests stayed neutral.
		 * Replay sim-effective wire rows (confirmed or predicted) before falling back to published.
		 */
		if (syNetInputTryGetRemoteHistoryForSimTick(slot, t, &row) != FALSE)
		{
			row.tick = t;
			syNetInputStoreFrame(sSYNetInputHistory, slot, &row);
			return;
		}
		if (sSYNetInputResimReconcileMissLogFrom == ~(u32)0)
		{
			sSYNetInputResimReconcileMissLogFrom = t;
			port_log(
			    "SSB64 NetInput: resim reconcile missing remote slot=%d tick=%u (no wire or published fallback)\n",
			    (int)slot,
			    t);
		}
		return;
	}
	{
		SYNetInputFrame wire;

		if ((syNetInputTryGetRemoteHistoryForSimTick(slot, t, &wire) != FALSE) &&
		    (syNetInputFrameIsRemoteConfirmed(&wire) != FALSE))
		{
			syNetInputStoreFrame(sSYNetInputHistory, slot, &wire);
		}
	}
}

static void syNetInputRollbackReconcileLocalSlotForResim(s32 slot, u32 t)
{
	SYNetInputFrame row;

	/* GGPO local authority: transmitted → delay → latch; no stale neutral published fallback. */
	if (syNetInputResolveLocalAuthorityFrame(slot, t, &row) != FALSE)
	{
		syNetInputStoreFrame(sSYNetInputHistory, slot, &row);
	}
}

void syNetInputRollbackReconcilePublishedFromRemote(u32 from_tick, u32 to_tick)
{
	syNetInputRollbackReconcileResimSpan(from_tick, to_tick, -1);
}

static sb32 syNetInputResimReconcileLogEnabled(void)
{
	const char *e;
	static sb32 sCached = -999;

	if (sCached != -999)
	{
		return (sCached != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_RESIM_RECONCILE_LOG");
	sCached = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (sCached != 0) ? TRUE : FALSE;
}

static void syNetInputRollbackReconcileSpanTagged(u32 from_tick, u32 to_tick, s32 correction_player,
                                                  const char *log_tag);

void syNetInputRollbackReconcilePublishedCommitWindow(u32 win_begin, u32 win_end)
{
	u32 t;

	if (win_begin >= win_end)
	{
		return;
	}
	if (syNetInputAuthoritativeWireContractEnabled() != FALSE)
	{
		for (t = win_begin; t < win_end; t++)
		{
			syNetInputPromoteAllLocalAuthoritySlots(t);
			syNetInputPromoteAllRemoteHumanAuthoritySlots(t);
		}
	}
	syNetInputRollbackReconcileSpanTagged(win_begin, win_end, -1, "commit_window_reconcile");
}

void syNetInputRollbackReconcileAfterResimCompleted(u32 mismatch_tick, u32 target_tick, s32 correction_player)
{
	u32 reconcile_end;
	u32 frontier;

	if ((mismatch_tick == 0U) || (mismatch_tick == ~(u32)0U) || (target_tick == 0U) || (target_tick == ~(u32)0U) ||
	    (target_tick <= mismatch_tick))
	{
		return;
	}
	reconcile_end = target_tick;
	frontier = syNetInputGetTick();
	if (frontier < ~(u32)0)
	{
		u32 frontier_end = frontier + 1U;

		if (frontier_end > reconcile_end)
		{
			reconcile_end = frontier_end;
		}
	}
	if (syNetInputResimReconcileLogEnabled() != FALSE)
	{
		port_log(
		    "SSB64 NetInput: resim_reconcile_post_complete mismatch=%u target=%u reconcile_end=%u correction_player=%d\n",
		    mismatch_tick, target_tick, reconcile_end, (int)correction_player);
	}
	syNetInputRollbackReconcileSpanTagged(mismatch_tick, reconcile_end, correction_player, "resim_reconcile_span");
}

void syNetInputRollbackReconcileResimSpan(u32 from_tick, u32 to_tick, s32 correction_player)
{
	syNetInputRollbackReconcileSpanTagged(from_tick, to_tick, correction_player, "resim_reconcile_span");
}

static void syNetInputRollbackReconcileSpanTagged(u32 from_tick, u32 to_tick, s32 correction_player, const char *log_tag)
{
	s32 i;
	s32 n;
	s32 slot;
	s32 local_slot;
	s32 extra_slot;
	u32 t;
	sb32 log_reconcile;

	if (from_tick >= to_tick)
	{
		return;
	}
	log_reconcile = syNetInputResimReconcileLogEnabled();
	if ((log_reconcile != FALSE) && (log_tag != NULL) && (log_tag[0] != '\0'))
	{
		port_log(
		    "SSB64 NetInput: %s from=%u to=%u local_sim=%d extra_local=%d remote_humans=%d correction_player=%d\n",
		    log_tag,
		    from_tick, to_tick, (int)syNetPeerGetLocalSimSlot(), (int)syNetPeerGetExtraLocalSenderSimSlot(),
		    syNetPeerGetRemoteHumanSlotCount(), (int)correction_player);
	}
	sSYNetInputResimReconcileMissLogFrom = ~(u32)0;
	local_slot = syNetPeerGetLocalSimSlot();
	extra_slot = syNetPeerGetExtraLocalSenderSimSlot();
	n = syNetPeerGetRemoteHumanSlotCount();
	for (t = from_tick; t < to_tick; t++)
	{
		for (i = 0; i < n; i++)
		{
			if (syNetPeerGetRemoteHumanSlotByIndex(i, &slot) == FALSE)
			{
				continue;
			}
			if ((correction_player >= 0) && (correction_player < MAXCONTROLLERS) &&
			    (syNetInputIsRemoteHumanSlot(correction_player) != FALSE) && (slot != correction_player))
			{
				continue;
			}
			if (syNetRollbackEpisodeSealRowsExchangeEnabled() != FALSE &&
			    syNetRollbackEpisodeSlotRequiresPeerSealRows(slot) != FALSE)
			{
				continue;
			}
			syNetInputRollbackReconcileRemoteSlotFromWire(slot, t);
		}
		if ((local_slot >= 0) && (local_slot < MAXCONTROLLERS))
		{
			syNetInputRollbackReconcileLocalSlotForResim(local_slot, t);
		}
		if ((extra_slot >= 0) && (extra_slot < MAXCONTROLLERS) && (extra_slot != local_slot))
		{
			syNetInputRollbackReconcileLocalSlotForResim(extra_slot, t);
		}
	}
	syNetInputStrictReadyCacheInvalidate();
}

u32 syNetInputFindEarliestLocalAuthorityMismatch(s32 authority_slot, u32 from_tick, u32 to_tick)
{
	SYNetInputFrame published;
	SYNetInputFrame transmitted;
	u32 t;

	if ((from_tick >= to_tick) || (authority_slot < 0) || (authority_slot >= MAXCONTROLLERS))
	{
		return ~(u32)0;
	}
	if ((authority_slot != syNetPeerGetLocalSimSlot()) &&
	    (authority_slot != syNetPeerGetExtraLocalSenderSimSlot()))
	{
		return ~(u32)0;
	}
	for (t = from_tick; t < to_tick; t++)
	{
		if (syNetInputGetHistoryFrame(authority_slot, t, &published) == FALSE)
		{
			continue;
		}
		if (syNetInputGetStoredFrame(sSYNetInputTransmittedHistory, authority_slot, t, &transmitted) == FALSE)
		{
			continue;
		}
		if (syNetInputFrameGameplayEquals(&published, &transmitted) == FALSE)
		{
			return t;
		}
	}
	return ~(u32)0;
}

void syNetInputRollbackReconcilePeerSymmetricAuthority(s32 authority_slot, u32 from_tick, u32 to_tick)
{
	u32 t;
	SYNetInputFrame tx;

	if ((from_tick >= to_tick) || (authority_slot < 0) || (authority_slot >= MAXCONTROLLERS))
	{
		return;
	}
	if ((authority_slot != syNetPeerGetLocalSimSlot()) &&
	    (authority_slot != syNetPeerGetExtraLocalSenderSimSlot()))
	{
		return;
	}
	for (t = from_tick; t < to_tick; t++)
	{
		if (syNetInputGetStoredFrame(sSYNetInputTransmittedHistory, authority_slot, t, &tx) == FALSE)
		{
			continue;
		}
		tx.tick = t;
		tx.source = nSYNetInputSourceLocal;
		tx.is_predicted = FALSE;
		syNetInputStoreFrame(sSYNetInputHistory, authority_slot, &tx);
	}
	syNetInputStrictReadyCacheInvalidate();
}

void syNetInputStorePublishedHistoryFrame(s32 player, const SYNetInputFrame *frame)
{
	SYNetInputFrame store;

	if ((frame == NULL) || (syNetInputCheckPlayer(player) == FALSE))
	{
		return;
	}
	store = *frame;
	syNetInputStoreFrame(sSYNetInputHistory, player, &store);
	syNetInputStrictReadyCacheInvalidate();
}

sb32 syNetInputCopyEpisodeLocalAuthoritySealFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	SYNetInputFrame row;

	if ((out_frame == NULL) || (syNetInputCheckPlayer(player) == FALSE))
	{
		return FALSE;
	}
	if (syNetInputResolveLocalAuthorityFrame(player, tick, &row) != FALSE)
	{
		*out_frame = row;
		out_frame->tick = tick;
		out_frame->source = nSYNetInputSourceLocal;
		out_frame->is_predicted = FALSE;
		out_frame->is_valid = TRUE;
		return TRUE;
	}
	if ((syNetInputGetHistoryFrame(player, tick, &row) != FALSE) && (row.is_predicted == FALSE))
	{
		*out_frame = row;
		out_frame->tick = tick;
		out_frame->source = nSYNetInputSourceLocal;
		out_frame->is_predicted = FALSE;
		out_frame->is_valid = TRUE;
		return TRUE;
	}
	return FALSE;
}

sb32 syNetInputCopyEpisodeRemoteAuthoritySealFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	SYNetInputFrame row;

	if ((out_frame == NULL) || (syNetInputCheckPlayer(player) == FALSE) ||
	    (syNetInputIsRemoteHumanSlot(player) == FALSE))
	{
		return FALSE;
	}
	if (syNetInputResolveRemoteHumanAuthorityFrameEx(player, tick, &row, NULL) != FALSE)
	{
		*out_frame = row;
		out_frame->tick = tick;
		out_frame->source = nSYNetInputSourceRemoteConfirmed;
		out_frame->is_predicted = FALSE;
		out_frame->is_valid = TRUE;
		return TRUE;
	}
	return FALSE;
}

sb32 syNetInputCopyEpisodeRemoteHumanSealFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	return syNetInputCopyEpisodeRemoteAuthoritySealFrame(player, tick, out_frame);
}

sb32 syNetInputEpisodeSealedSpanBlocksPatch(u32 sim_tick)
{
	if (syNetRollbackEpisodeFsmEnabled() == FALSE)
	{
		return FALSE;
	}
	if (syNetRollbackEpisodeFsmIsActive() != FALSE)
	{
		u32 mismatch;
		u32 target;

		mismatch = syNetRollbackEpisodeFsmGetMismatchTick();
		target = syNetRollbackEpisodeFsmGetTargetTick();
		if ((sim_tick >= mismatch) && (sim_tick < target))
		{
			return TRUE;
		}
	}
	return syNetRollbackEpisodeTickInSealedSpan(sim_tick);
}

void syNetInputMaybeLogFrameCommitLocalAuthorityDiag(u32 validation_tick, u32 win_begin)
{
	s32 local_slot;
	s32 extra_slot;
	u32 mismatch_tick;
	static u32 sLastLoggedValidation = ~(u32)0;
	const char *e;

	if (sSYNetInputFrameCommitDiagLevelCache == -999)
	{
		e = getenv("SSB64_NETPLAY_FRAME_COMMIT_DIAG");
		sSYNetInputFrameCommitDiagLevelCache = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 0;
		if (sSYNetInputFrameCommitDiagLevelCache < 0)
		{
			sSYNetInputFrameCommitDiagLevelCache = 0;
		}
	}
	if (sSYNetInputFrameCommitDiagLevelCache < 2)
	{
		return;
	}
	if (validation_tick == sLastLoggedValidation)
	{
		return;
	}
	local_slot = syNetPeerGetLocalSimSlot();
	mismatch_tick = syNetInputFindEarliestLocalAuthorityMismatch(local_slot, win_begin, validation_tick);
	if (mismatch_tick != ~(u32)0)
	{
		port_log(
		    "SSB64 NetInput: FC_LOCAL_AUTH_MISMATCH validation=%u player=%d earliest_tick=%u win=[%u,%u)\n",
		    (unsigned int)validation_tick, (int)local_slot, (unsigned int)mismatch_tick, (unsigned int)win_begin,
		    (unsigned int)validation_tick);
		sLastLoggedValidation = validation_tick;
		return;
	}
	extra_slot = syNetPeerGetExtraLocalSenderSimSlot();
	if ((extra_slot >= 0) && (extra_slot < MAXCONTROLLERS) && (extra_slot != local_slot))
	{
		mismatch_tick = syNetInputFindEarliestLocalAuthorityMismatch(extra_slot, win_begin, validation_tick);
		if (mismatch_tick != ~(u32)0)
		{
			port_log(
			    "SSB64 NetInput: FC_LOCAL_AUTH_MISMATCH validation=%u player=%d earliest_tick=%u win=[%u,%u)\n",
			    (unsigned int)validation_tick, (int)extra_slot, (unsigned int)mismatch_tick,
			    (unsigned int)win_begin, (unsigned int)validation_tick);
			sLastLoggedValidation = validation_tick;
			return;
		}
	}
	{
		s32 i;
		s32 n;
		s32 remote_slot;

		n = syNetPeerGetRemoteHumanSlotCount();
		for (i = 0; i < n; i++)
		{
			if (syNetPeerGetRemoteHumanSlotByIndex(i, &remote_slot) == FALSE)
			{
				continue;
			}
			mismatch_tick = syNetInputFindEarliestRemoteAuthorityMismatch(remote_slot, win_begin, validation_tick);
			if (mismatch_tick != ~(u32)0)
			{
				port_log(
				    "SSB64 NetInput: FC_REMOTE_AUTH_MISMATCH validation=%u player=%d earliest_tick=%u win=[%u,%u)\n",
				    (unsigned int)validation_tick, (int)remote_slot, (unsigned int)mismatch_tick,
				    (unsigned int)win_begin, (unsigned int)validation_tick);
				sLastLoggedValidation = validation_tick;
				return;
			}
		}
	}
}

void syNetInputMaybeLogFrameCommitSealLocalMismatch(u32 validation_tick, u32 win_begin, u32 win_end)
{
	s32 player;
	u32 t;
	SYNetInputFrame published;
	SYNetInputFrame sealed;
	static u32 sSealMismatchLogBudget = 4U;
	const char *e;

	if (sSYNetInputFrameCommitDiagLevelCache == -999)
	{
		e = getenv("SSB64_NETPLAY_FRAME_COMMIT_DIAG");
		sSYNetInputFrameCommitDiagLevelCache = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 0;
		if (sSYNetInputFrameCommitDiagLevelCache < 0)
		{
			sSYNetInputFrameCommitDiagLevelCache = 0;
		}
	}
	if ((sSYNetInputFrameCommitDiagLevelCache < 2) || (syNetRollbackEpisodeFsmEnabled() == FALSE) ||
	    (syNetRollbackEpisodeInputsSealed() == FALSE) || (sSealMismatchLogBudget == 0U) || (win_begin >= win_end))
	{
		return;
	}
	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		if (syNetRollbackEpisodeSlotIsLocalAuthority(player) == FALSE)
		{
			continue;
		}
		for (t = win_begin; t < win_end; t++)
		{
			if (syNetRollbackEpisodeGetSealedFrame(player, t, &sealed) == FALSE)
			{
				continue;
			}
			if ((syNetInputGetHistoryFrame(player, t, &published) == FALSE) ||
			    (syNetInputFrameGameplayEquals(&published, &sealed) == FALSE))
			{
				port_log(
				    "SSB64 NetInput: FC_SEAL_LOCAL_MISMATCH validation=%u player=%d tick=%u\n",
				    (unsigned int)validation_tick, (int)player, (unsigned int)t);
				sSealMismatchLogBudget--;
				if (sSealMismatchLogBudget == 0U)
				{
					return;
				}
				break;
			}
		}
	}
}
#endif
