#include <sys/netinput.h>

/*
 * NetInput implementation: ring buffers keyed by `tick % SYNETINPUT_HISTORY_LENGTH`.
 * `syNetInputResolveFrame` chooses source; `syNetInputPublishFrame` materializes edge-detected taps into `gSYControllerDevices`.
 * On PORT, `syNetInputFuncRead` consumes one wall-rate HID sample from a capture FIFO into `sSYNetInputHardwareLatch`
 * and clears `gSYControllerDevices` before resolve/publish so sim globals are never read for gameplay between raw HID
 * and publish. The FIFO is polled on every FuncRead (including admission stalls for the same sim tick) and on PortPushFrame
 * sim-skips, so stick trajectory during rare R-holds is preserved instead of dropped. Each accepted sim tick pops one
 * FIFO sample (drop-oldest on overrun) — vanilla-shaped motion, not peak-hold.
 *
 * Phase-lock contract (NETMENU): live VS samples raw HID at sim `t` into gameplay + send-lead rings both keyed by `t`
 * (zero local feel delay). Committed `D` is send/predict runway only: wire label remains `sim + D`. During rollback
 * resim, local HID is ignored; `syNetInputMakeLocalFrame` replays from published history for that tick.
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
#if defined(SSB64_NETMENU)
#include <ft/fighter.h>
#include <sys/objman_gcport.h>
#include <sys/netplay_ness_pkthunder_gate.h>
#include <sys/netplay_rebirth_gate.h>
#endif
extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);
static sb32 sSYNetInputPredictNeutral;
#if defined(SSB64_NETMENU)
static sb32 sSYNetInputLocalLabActive;
static s32 sSYNetInputLocalLabPlayer;
#endif
static SYController sSYNetInputHardwareLatch[MAXCONTROLLERS];
/*
 * Send-lead / egress ring. Under NETMENU feel-0:
 * - authoritative HID for sim `t` is stored at tick `t` (same content as gameplay);
 * - staging also hold-last-fills `(t+1)…(t+D)` so AppendDelayed can emit wire labels through
 *   `sim+2D` and intro Wait's `DelaySim(hr)` frontier can advance (soak stuck-at-0 with D≥1).
 * Legacy non-NETMENU still stages only at sample+D for closed-loop local delay.
 */
static SYNetInputFrame sSYNetInputLocalDelayHistory[MAXCONTROLLERS][SYNETINPUT_HISTORY_LENGTH];
#if defined(SSB64_NETMENU)
/* Gameplay ring: HID owned by sample tick (feel 0). MakeLocalFrame / local authority read this. */
static SYNetInputFrame sSYNetInputLocalGameplayHistory[MAXCONTROLLERS][SYNETINPUT_HISTORY_LENGTH];
/* Highest sample tick with a gameplay row — auth_wire_frontier must not exceed this (send-before-sample). */
static u32 sSYNetInputLocalGameplayLastTick[MAXCONTROLLERS];
#endif
/* Last gameplay row enqueued for wire transmit per sim tick (peer-symmetric authority for local slot). */
static SYNetInputFrame sSYNetInputTransmittedHistory[MAXCONTROLLERS][SYNETINPUT_HISTORY_LENGTH];
static u32 sSYNetInputRemotePacketSeqHistory[MAXCONTROLLERS][SYNETINPUT_HISTORY_LENGTH];
static ub8 sSYNetInputRemotePacketSeqValid[MAXCONTROLLERS][SYNETINPUT_HISTORY_LENGTH];
#ifdef PORT
static u32 sSYNetInputSimPredictedRemoteTick[SYNETINPUT_HISTORY_LENGTH];
static ub8 sSYNetInputSimPredictedRemoteUsed[SYNETINPUT_HISTORY_LENGTH];
#endif
static u32 sSYNetInputRemoteConfirmedConflictLogsRemaining;
#if defined(SSB64_NETMENU)
/* Rate-limit SEAL_ROW dumps after SEALED_RESIM_LEDGER_SKIP gameplay mismatch. */
static u32 sSYNetInputSealSkipSpanDumpBudget;
#endif
static sb32 sSYNetInputRemoteAnalogOnsetPredEnvCache = -999;
/* Raw analog without quantize: wider defaults avoid first-gesture GGPO over correction. */
#define SYNETINPUT_GGPO_STICK_DEADBAND_DEFAULT 12
#define SYNETINPUT_GGPO_STICK_DEADBAND_PREDICT_DEFAULT 14
/*
 * Completed-sim REPLACE used to rewind on any stick delta (feel-0 release hammer). That opened
 * span-2 episodes for ±1–3 same-intent noise (soak 1490370675). Cap micro skips here; larger
 * same-intent ramps and all releases/onsets still rewind. See
 * docs/bugs/netplay_input_contract_micro_deadband_onset_peek_2026-07-17.md.
 */
#define SYNETINPUT_GGPO_STICK_COMPLETED_SIM_MICRO_DEADBAND_DEFAULT 3
#define SYNETINPUT_ANALOG_ONSET_STICK_MAG_DEFAULT 28
#define SYNETINPUT_ANALOG_ONSET_STICK_MAG_MAX 80
#define SYNETINPUT_ANALOG_ONSET_LOOKBACK_DEFAULT 60
#define SYNETINPUT_ANALOG_ONSET_FACING_THRESH_DEFAULT 4
#define SYNETINPUT_ANALOG_ONSET_LARGE_DELTA_DEFAULT 40
#define SYNETINPUT_ANALOG_SAME_INTENT_TOLERANCE 14
#define SYNETINPUT_ANALOG_ONSET_WIRE_PEEK_FRAMES 4U
#define SYNETINPUT_ANALOG_ONSET_WIRE_PEEK_AHEAD_DEFAULT 8U
/* Match FTCOMMON_DASH_STICK_RANGE_MIN — Turn→Dash gate uses sx*lr_turn >= this. */
#define SYNETINPUT_DASH_STICK_RANGE_MIN 56
static s32 sSYNetInputGgpoStickDeadband = -1;
static s32 sSYNetInputGgpoStickDeadbandPredict = -1;
static s32 sSYNetInputGgpoStickCompletedSimMicroDeadband = -1;
#if defined(SSB64_NETMENU)
static u32 sSYNetInputGgpoClassQueuedButton;
static u32 sSYNetInputGgpoClassQueuedOnset;
static u32 sSYNetInputGgpoClassQueuedRelease;
static u32 sSYNetInputGgpoClassQueuedRealStick;
static u32 sSYNetInputGgpoClassQueuedMicro; /* should stay ~0 after micro skip */
static u32 sSYNetInputGgpoClassSkippedMicro;
static u32 sSYNetInputGgpoSkipMicroLogsRemaining;
/*
 * Wait skips wire_need/runway so Appear survives Android hr pauses. At Go those gates
 * snap on while hr may still be frozen → permanent hang (soak1: wire_need next=399
 * hr=394). Soft-pace for a short post-Go window so ingress/send can recover.
 * See docs/bugs/netplay_post_go_wire_need_hang_2026-07-18.md.
 */
static sb32 sSYNetInputSawIntroWait = FALSE;
static u32 sSYNetInputPostGoWirePacingGraceUntil = ~(u32)0;
#endif
static s32 sSYNetInputAnalogOnsetStickMag = -1;
static s32 sSYNetInputAnalogOnsetLookback = -1;
static s32 sSYNetInputAnalogOnsetFacingThresh = -1;
static s32 sSYNetInputAnalogOnsetLargeDelta = -1;
static u32 sSYNetInputAnalogOnsetLogBudget;
/* Sim-tick the latch was last filled for; 0xFFFFFFFFU => next FuncRead must consume a FIFO sample. */
static u32 sSYNetInputPortHwLatchTick = 0xFFFFFFFFU;
#if defined(SSB64_NETMENU)
/*
 * Wall-rate HID capture FIFO: preserves vanilla stick trajectory across admission stalls /
 * decouple sim-skips. Depth covers a short R-hold without inventing peaks.
 */
#define SYNETINPUT_HW_CAPTURE_FIFO_LEN 8U
typedef struct SYNetInputHwCaptureSample
{
	u16 buttons;
	s8 stick_x;
	s8 stick_y;
} SYNetInputHwCaptureSample;
static SYNetInputHwCaptureSample sSYNetInputHwCaptureFifo[MAXCONTROLLERS][SYNETINPUT_HW_CAPTURE_FIFO_LEN];
static u32 sSYNetInputHwCaptureFifoHead[MAXCONTROLLERS];
static u32 sSYNetInputHwCaptureFifoCount[MAXCONTROLLERS];
#endif
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

static void syNetInputStageLocalDelayFramesFromLatch(u32 sample_tick);

#if defined(SSB64_NETMENU)
static void syNetInputHwCaptureFifoReset(void)
{
	s32 player;

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		sSYNetInputHwCaptureFifoHead[player] = 0U;
		sSYNetInputHwCaptureFifoCount[player] = 0U;
	}
}

static void syNetInputHwCaptureFifoPushFromDevices(void)
{
	s32 player;
	u32 head;
	u32 count;
	SYNetInputHwCaptureSample *slot;
	const SYController *controller;

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		controller = &gSYControllerDevices[player];
		count = sSYNetInputHwCaptureFifoCount[player];
		head = sSYNetInputHwCaptureFifoHead[player];
		if (count >= SYNETINPUT_HW_CAPTURE_FIFO_LEN)
		{
			/* Drop oldest — keep freshest wall-rate trajectory, never invent peaks. */
			head = (head + 1U) % SYNETINPUT_HW_CAPTURE_FIFO_LEN;
			sSYNetInputHwCaptureFifoHead[player] = head;
			count = SYNETINPUT_HW_CAPTURE_FIFO_LEN - 1U;
		}
		slot = &sSYNetInputHwCaptureFifo[player][(head + count) % SYNETINPUT_HW_CAPTURE_FIFO_LEN];
		slot->buttons = (u16)(controller->button_hold | controller->button_tap);
		slot->stick_x = controller->stick_range.x;
		slot->stick_y = controller->stick_range.y;
		sSYNetInputHwCaptureFifoCount[player] = count + 1U;
	}
}

static sb32 syNetInputHwCaptureFifoPopIntoLatch(void)
{
	s32 player;
	u32 head;
	u32 count;
	const SYNetInputHwCaptureSample *sample;
	SYController *latch;

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		if (sSYNetInputHwCaptureFifoCount[player] == 0U)
		{
			return FALSE;
		}
	}
	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		head = sSYNetInputHwCaptureFifoHead[player];
		count = sSYNetInputHwCaptureFifoCount[player];
		sample = &sSYNetInputHwCaptureFifo[player][head];
		latch = &sSYNetInputHardwareLatch[player];
		memset(latch, 0, sizeof(*latch));
		latch->button_hold = sample->buttons;
		latch->stick_range.x = sample->stick_x;
		latch->stick_range.y = sample->stick_y;
		sSYNetInputHwCaptureFifoHead[player] = (head + 1U) % SYNETINPUT_HW_CAPTURE_FIFO_LEN;
		sSYNetInputHwCaptureFifoCount[player] = count - 1U;
	}
	return TRUE;
}

static sb32 syNetInputHwCaptureActive(void)
{
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return FALSE;
	}
	if (syNetPeerIsVSSessionActive() != FALSE)
	{
		return TRUE;
	}
	if (sSYNetInputLocalLabActive != FALSE)
	{
		return TRUE;
	}
	return FALSE;
}

/*
 * Wall-rate HID poll into the capture FIFO. Safe on admission stalls and PortPushFrame sim-skips.
 * Neutralizes devices after capture so live HID cannot leak into sim globals.
 */
void syNetInputPollHardwareCaptureFifo(void)
{
	if (syNetInputHwCaptureActive() == FALSE)
	{
		return;
	}
	syControllerFuncRead();
	syNetInputHwCaptureFifoPushFromDevices();
	syNetInputNeutralizeAllControllerDevices();
}

static void syNetInputConsumeHardwareLatchForSimTick(u32 tick)
{
	if (syNetInputHwCaptureFifoPopIntoLatch() == FALSE)
	{
		syNetInputPollHardwareCaptureFifo();
		if (syNetInputHwCaptureFifoPopIntoLatch() == FALSE)
		{
			/* Empty after poll: fall back to a direct latch (should be rare). */
			syControllerFuncRead();
			memcpy(sSYNetInputHardwareLatch, gSYControllerDevices, sizeof(SYController) * (size_t)MAXCONTROLLERS);
			syNetInputNeutralizeAllControllerDevices();
		}
	}
	syNetInputStageLocalDelayFramesFromLatch(tick);
	sSYNetInputPortHwLatchTick = tick;
}
#endif
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
#if defined(PORT) && defined(SSB64_NETMENU)
/*
 * Sim-tick keyed remote authority ledger. Dual-written by wire confirm + episode seal;
 * seal origin outranks wire. Read path prefers ledger over the wire ring.
 * See docs/bugs/netplay_confirmed_publish_write_once_2026-07-12.md.
 */
static SYNetInputFrame sSYNetInputRemoteAuthorityLedger[MAXCONTROLLERS][SYNETINPUT_HISTORY_LENGTH];
static u8 sSYNetInputRemoteAuthorityLedgerOrigin[MAXCONTROLLERS][SYNETINPUT_HISTORY_LENGTH];
/*
 * Parallel provenance for published History (tick-keyed with the History ring).
 * Not embedded in SYNetInputFrame — keeps replay / seal wire sizeof stable.
 */
static u8 sSYNetInputHistoryProvenance[MAXCONTROLLERS][SYNETINPUT_HISTORY_LENGTH];
static u8 sSYNetInputStoreProvenance = (u8)nSYNetInputHistoryProvNone;
static const char *sSYNetInputStoreProvenanceWriter = NULL;
#endif
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
#if defined(SSB64_NETMENU)
	syNetInputHwCaptureFifoReset();
#endif
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
	/*
	 * Resim BattleSimOnly must always Advance to the exclusive frontier. Wire/hr caps are
	 * live-pacing only. Soak 1133978048: Android follower hr starved Advance after the last
	 * replayed tick → POST_RESIM_LIVE sim=target-1 → live re-sim under resolved_through
	 * save-skip → permanent +1 phase skew (map/figh) → PEER_SNAPSHOT_DIVERGE.
	 * See docs/bugs/netplay_post_resim_exclusive_tick_wire_cap_skew_2026-07-15.md.
	 */
	if (syNetRollbackIsResimulating() != FALSE)
	{
		(void)next_sim_tick;
		return TRUE;
	}
#endif
	if ((syNetSessionParamsRollbackEnabled() == FALSE) || (syNetPeerIsVSSessionActive() == FALSE))
	{
		return TRUE;
	}
#if defined(PORT)
	{
		sb32 soft_wire_pacing;
		sb32 intro_wait;
		sb32 post_go_grace;

		soft_wire_pacing = FALSE;
		intro_wait = FALSE;
		post_go_grace = FALSE;
#if defined(SSB64_NETMENU)
		if ((gSCManagerBattleState != NULL) &&
		    (gSCManagerBattleState->game_status == nSCBattleGameStatusWait))
		{
			intro_wait = TRUE;
			sSYNetInputSawIntroWait = TRUE;
		}
		else if ((gSCManagerBattleState != NULL) &&
		         (gSCManagerBattleState->game_status == nSCBattleGameStatusGo) &&
		         (sSYNetInputSawIntroWait != FALSE))
		{
			if (sSYNetInputPostGoWirePacingGraceUntil == ~(u32)0)
			{
				u32 grace;

				/*
				 * Cover pred+D plus a short ICE/hr recovery window. soak1 froze hr at 394
				 * from late Wait through Go; hard wire_need at next=399 hung forever.
				 */
				grace = syNetPeerGetCommittedInputDelay() +
				        syNetPeerGetPhaseLockPredictionWindowTicks() + 24U;
				if (grace < 32U)
				{
					grace = 32U;
				}
				sSYNetInputPostGoWirePacingGraceUntil = next_sim_tick + grace;
				port_log(
				    "SSB64 NetInput: post_go_wire_pacing_grace until=%u next_sim=%u grace=%u hr=%u\n",
				    (unsigned int)sSYNetInputPostGoWirePacingGraceUntil,
				    (unsigned int)next_sim_tick,
				    (unsigned int)grace,
				    (unsigned int)syNetPeerGetHighestRemoteTick());
			}
			if (next_sim_tick <= sSYNetInputPostGoWirePacingGraceUntil)
			{
				post_go_grace = TRUE;
			}
		}
#else
		intro_wait = ((gSCManagerBattleState != NULL) &&
		              (gSCManagerBattleState->game_status == nSCBattleGameStatusWait))
		                 ? TRUE
		                 : FALSE;
#endif
		/*
		 * Intro Wait (force-neutral Appear) and a short post-Go grace: do not apply
		 * wire_need / full phase_lock runway_cap. Those gates stall sim when hr pauses
		 * (Android ICE recv blackholes). SYNCTEST_SKIP intro_wait only skips hash compare.
		 * Soak 979771282: post-Go grace still must apply zero-onset D+1 cap — inventing
		 * remote (0,0) through grace (until=423) while owner onset@419 seeded Wait vs Turn.
		 * See docs/bugs/netplay_intro_wait_advance_frontier_deadlock_2026-07-18.md,
		 * docs/bugs/netplay_post_go_wire_need_hang_2026-07-18.md, and
		 * docs/bugs/netplay_zero_onset_predict_runway_peer_2026-07-20.md.
		 */
		soft_wire_pacing = ((intro_wait != FALSE) || (post_go_grace != FALSE)) ? TRUE : FALSE;
		if (soft_wire_pacing != FALSE)
		{
			hr = syNetPeerGetHighestRemoteTick();
			if (hr == 0U)
			{
				static u32 sLastSoftHr0AdvanceHoldLogTick = ~(u32)0;

				if (next_sim_tick != sLastSoftHr0AdvanceHoldLogTick)
				{
					port_log(
					    "SSB64 NetInput: sim advance blocked (%s) next_sim=%u "
					    "peer_vs_active=%d\n",
					    (intro_wait != FALSE) ? "intro_wait_hr0" : "post_go_grace_hr0",
					    (unsigned int)next_sim_tick, (int)syNetPeerIsVSSessionActive());
					sLastSoftHr0AdvanceHoldLogTick = next_sim_tick;
				}
				return FALSE;
			}
#if defined(SSB64_NETMENU)
			/*
			 * Post-Go soft pacing: keep wire_need off. Zero-onset / analog-ramp / dual-hot
			 * only apply D+1 runway when hr is live — do not hard-lock every hold tick
			 * (soak 1809694209 dual-stick hang). Soft onset invent (0,0) still capped.
			 */
			if ((post_go_grace != FALSE) && (intro_wait == FALSE) &&
			    ((syNetInputRemoteHumanZeroOnsetPredictRestrict(next_sim_tick) != FALSE) ||
			     (syNetInputRemoteHumanAnalogRampPredictTighten(next_sim_tick) != FALSE) ||
			     (syNetInputDualStickHotPredictTighten(next_sim_tick) != FALSE)))
			{
				u32 onset_cap;

				onset_cap = syNetPeerDelaySimTickFromWire(hr) + syNetPeerGetCommittedInputDelay() +
				            1U;
				if (next_sim_tick > onset_cap)
				{
					static u32 sLastZeroOnsetGraceHoldLogTick = ~(u32)0;

					if (next_sim_tick != sLastZeroOnsetGraceHoldLogTick)
					{
						port_log(
						    "SSB64 NetInput: sim advance blocked "
						    "(runway_cap_zero_onset_grace) next_sim=%u hr=%u "
						    "cap=%u frontier_sim=%u D=%u grace_until=%u\n",
						    (unsigned int)next_sim_tick, (unsigned int)hr,
						    (unsigned int)onset_cap,
						    (unsigned int)syNetPeerDelaySimTickFromWire(hr),
						    (unsigned int)syNetPeerGetCommittedInputDelay(),
						    (unsigned int)sSYNetInputPostGoWirePacingGraceUntil);
						sLastZeroOnsetGraceHoldLogTick = next_sim_tick;
					}
					return FALSE;
				}
			}
#endif
			return TRUE;
		}
	}
#endif
#ifdef PORT
	/*
	 * Live (post-Go) wire-contract pacing. Subtract phase_lock so this is not stricter than
	 * FuncRead prediction (D==lead otherwise collapses to hr>=next_sim).
	 * Zero-onset invent: no pred credit. Analog-ramp / dual-hot: shrink credit to D+1.
	 */
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
		if ((syNetSessionParamsRollbackEnabled() != FALSE) && (syNetInputGetUseInputPrediction() != FALSE))
		{
			u32 pred_credit;

			pred_credit = syNetPeerGetPhaseLockPredictionWindowTicks();
#if defined(SSB64_NETMENU)
			if (syNetInputRemoteHumanZeroOnsetPredictRestrict(next_sim_tick) != FALSE)
			{
				pred_credit = 0U;
			}
			else if ((syNetInputRemoteHumanAnalogRampPredictTighten(next_sim_tick) != FALSE) ||
			         (syNetInputDualStickHotPredictTighten(next_sim_tick) != FALSE))
			{
				u32 tight;

				tight = syNetPeerGetCommittedInputDelay() + 1U;
				if (tight < 2U)
				{
					tight = 2U;
				}
				if (pred_credit > tight)
				{
					pred_credit = tight;
				}
			}
#endif
			if (wire_need > pred_credit)
			{
				wire_need -= pred_credit;
			}
			else
			{
				wire_need = 0U;
			}
		}
		hr = syNetPeerGetHighestRemoteTick();
		if (hr < wire_need)
		{
			static u32 sLastWireNeedAdvanceHoldLogTick = ~(u32)0;

			if (next_sim_tick != sLastWireNeedAdvanceHoldLogTick)
			{
				port_log(
				    "SSB64 NetInput: sim advance blocked (wire_need) next_sim=%u hr=%u wire_need=%u D=%u "
				    "lead_b=%u pred=%u\n",
				    (unsigned int)next_sim_tick, (unsigned int)hr, (unsigned int)wire_need,
				    (unsigned int)syNetPeerGetCommittedInputDelay(), (unsigned int)lead_b,
				    (unsigned int)syNetPeerGetPhaseLockPredictionWindowTicks());
				sLastWireNeedAdvanceHoldLogTick = next_sim_tick;
			}
			return FALSE;
		}
	}
#endif
	hr = syNetPeerGetHighestRemoteTick();
	if (hr == 0U)
	{
		return TRUE;
	}
	cap = syNetPeerDelaySimTickFromWire(hr) + syNetPeerGetCommittedInputDelay() +
	      syNetPeerGetPhaseLockPredictionWindowTicks();
#if defined(PORT) && defined(SSB64_NETMENU)
	/*
	 * Post-grace zero-onset invent: hard-stall. Grace / dual-hot / analog-ramp: D+1 only
	 * (do not hard-lock stick holds with normal D lag — soak 1809694209).
	 */
	if (syNetInputRemoteHumanZeroOnsetPredictRestrict(next_sim_tick) != FALSE)
	{
		if (syNetInputPostGoWirePacingGraceActive(next_sim_tick) == FALSE)
		{
			static u32 sLastZeroOnsetStallHoldLogTick = ~(u32)0;

			if (next_sim_tick != sLastZeroOnsetStallHoldLogTick)
			{
				port_log(
				    "SSB64 NetInput: sim advance blocked (zero_onset_stall) next_sim=%u hr=%u "
				    "frontier_sim=%u D=%u dual_hot=%d grace=%d\n",
				    (unsigned int)next_sim_tick, (unsigned int)hr,
				    (unsigned int)syNetPeerDelaySimTickFromWire(hr),
				    (unsigned int)syNetPeerGetCommittedInputDelay(),
				    (int)syNetInputDualStickHotPredictTighten(next_sim_tick),
				    (int)syNetInputPostGoWirePacingGraceActive(next_sim_tick));
				sLastZeroOnsetStallHoldLogTick = next_sim_tick;
			}
			return FALSE;
		}
		{
			u32 onset_cap;

			onset_cap = syNetPeerDelaySimTickFromWire(hr) + syNetPeerGetCommittedInputDelay() + 1U;
			if (onset_cap < cap)
			{
				cap = onset_cap;
			}
		}
	}
	else if ((syNetInputRemoteHumanAnalogRampPredictTighten(next_sim_tick) != FALSE) ||
	         (syNetInputDualStickHotPredictTighten(next_sim_tick) != FALSE))
	{
		u32 tight_cap;

		tight_cap = syNetPeerDelaySimTickFromWire(hr) + syNetPeerGetCommittedInputDelay() + 1U;
		if (tight_cap < cap)
		{
			cap = tight_cap;
		}
	}
#endif
	if (next_sim_tick > cap)
	{
		static u32 sLastRunwayAdvanceHoldLogTick = ~(u32)0;

		if (next_sim_tick != sLastRunwayAdvanceHoldLogTick)
		{
			port_log(
			    "SSB64 NetInput: sim advance blocked (runway_cap) next_sim=%u hr=%u cap=%u frontier_sim=%u "
			    "D=%u pred=%u\n",
			    (unsigned int)next_sim_tick, (unsigned int)hr, (unsigned int)cap,
			    (unsigned int)syNetPeerDelaySimTickFromWire(hr),
			    (unsigned int)syNetPeerGetCommittedInputDelay(),
			    (unsigned int)syNetPeerGetPhaseLockPredictionWindowTicks());
			sLastRunwayAdvanceHoldLogTick = next_sim_tick;
		}
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
#if defined(PORT) && defined(SSB64_NETMENU)
	/*
	 * Stick sample uses the completed tick's published controllers + fighter tap counters (post-sim).
	 * Must run before the counter advances so mode=training|netvs lines align with dash-window math.
	 */
	syNetInputMaybeLogStickSample(syNetInputIsLocalLabActive() != FALSE ? "training" : "netvs");
	syNetInputMaybeLogStickTapWitness(syNetInputIsLocalLabActive() != FALSE ? "training" : "netvs");
#endif
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

#if defined(PORT) && defined(SSB64_NETMENU)
/* SSB64_NETPLAY_STRICT_INPUT=1 — log-only input-authority witness (defined with the other ring helpers). */
static void syNetInputStrictWitnessOnStore(SYNetInputFrame history[][SYNETINPUT_HISTORY_LENGTH], s32 player,
                                           const SYNetInputFrame *frame);
static void syNetInputStrictWitnessTagWriter(const char *tag);
#define SYNETINPUT_STRICT_TAG(tag) syNetInputStrictWitnessTagWriter(tag)
static sb32 syNetInputFrameIsRemoteStrictConfirmed(const SYNetInputFrame *frame);
static sb32 syNetInputRemoteConfirmedWriteOnceBlocks(s32 player, const SYNetInputFrame *existing,
                                                     const SYNetInputFrame *incoming, const char *reason);
static void syNetInputRemoteConfirmedWriteOnceQueueCorrection(s32 player, u32 sim_tick,
                                                              SYNetInputFrame *published_mut);
/* Phase 2: published ring refresh from ledger only (confirmed path). */
static sb32 syNetInputRefreshPublishedFromAuthorityLedger(s32 player, u32 sim_tick, const char *reason);
static sb32 syNetInputFrameGameplayEquals(const SYNetInputFrame *a, const SYNetInputFrame *b);
static sb32 syNetInputGetLocalGameplayFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);
sb32 syNetInputGetStoredFrame(SYNetInputFrame history[][SYNETINPUT_HISTORY_LENGTH], s32 player, u32 tick,
                              SYNetInputFrame *out_frame);

/*
 * History provenance: only GAMEPLAY / LOCAL_PUBLISH mint gameplay-authoritative rows.
 * Local && !predicted alone is insufficient (soak1 67923985: gap 436 frozen as auth_history
 * without LOCAL_PUBLISH). See docs/bugs/netplay_history_provenance_2026-07-20.md.
 */
static void syNetInputHistoryProvenanceTag(u8 provenance, const char *writer)
{
	sSYNetInputStoreProvenance = provenance;
	sSYNetInputStoreProvenanceWriter = writer;
}

#define SYNETINPUT_PROVENANCE_TAG(prov, writer) syNetInputHistoryProvenanceTag((u8)(prov), (writer))

static void syNetInputHistoryProvenanceClearPending(void)
{
	sSYNetInputStoreProvenance = (u8)nSYNetInputHistoryProvNone;
	sSYNetInputStoreProvenanceWriter = NULL;
}

static const char *syNetInputHistoryProvenanceTagName(u8 provenance)
{
	switch (provenance)
	{
	case nSYNetInputHistoryProvPrediction:
		return "prediction";
	case nSYNetInputHistoryProvGameplay:
		return "gameplay";
	case nSYNetInputHistoryProvLocalPublish:
		return "local_publish";
	case nSYNetInputHistoryProvRemoteConfirmed:
		return "remote_confirmed";
	case nSYNetInputHistoryProvGapHold:
		return "gap_hold";
	case nSYNetInputHistoryProvLatch:
		return "latch";
	default:
		return "none";
	}
}

static sb32 syNetInputHistoryProvenanceIsGameplayAuth(u8 provenance)
{
	return ((provenance == (u8)nSYNetInputHistoryProvGameplay) ||
	        (provenance == (u8)nSYNetInputHistoryProvLocalPublish))
	           ? TRUE
	           : FALSE;
}

static u8 syNetInputHistoryProvenanceGet(s32 player, u32 tick)
{
	SYNetInputFrame *slot;

	if (syNetInputCheckPlayer(player) == FALSE)
	{
		return (u8)nSYNetInputHistoryProvNone;
	}
	slot = &sSYNetInputHistory[player][tick % SYNETINPUT_HISTORY_LENGTH];
	if ((slot->is_valid == FALSE) || (slot->tick != tick))
	{
		return (u8)nSYNetInputHistoryProvNone;
	}
	return sSYNetInputHistoryProvenance[player][tick % SYNETINPUT_HISTORY_LENGTH];
}

static sb32 syNetInputLocalHistoryGameplayAuth(s32 player, const SYNetInputFrame *existing)
{
	if ((existing == NULL) || (existing->is_valid == FALSE) || (existing->is_predicted != FALSE) ||
	    (existing->source != nSYNetInputSourceLocal))
	{
		return FALSE;
	}
	return syNetInputHistoryProvenanceIsGameplayAuth(syNetInputHistoryProvenanceGet(player, existing->tick));
}

static sb32 syNetInputLocalHistoryAuthFreezeBlocks(s32 player, const SYNetInputFrame *existing,
                                                   const SYNetInputFrame *incoming, const char *reason)
{
	if ((existing == NULL) || (incoming == NULL) ||
	    (syNetInputLocalHistoryGameplayAuth(player, existing) == FALSE) ||
	    (existing->tick != incoming->tick))
	{
		return FALSE;
	}
	if (syNetInputFrameGameplayEquals(existing, incoming) != FALSE)
	{
		return FALSE;
	}
	port_log(
	    "SSB64 NetInput: HISTORY_AUTH_FREEZE player=%d tick=%u writer=%s prov=%s keep btn=0x%04X sx=%d sy=%d "
	    "| reject btn=0x%04X sx=%d sy=%d pred=%d\n",
	    (int)player, (unsigned int)existing->tick, (reason != NULL) ? reason : "?",
	    syNetInputHistoryProvenanceTagName(syNetInputHistoryProvenanceGet(player, existing->tick)),
	    (unsigned int)existing->buttons, (int)existing->stick_x, (int)existing->stick_y,
	    (unsigned int)incoming->buttons, (int)incoming->stick_x, (int)incoming->stick_y,
	    (int)incoming->is_predicted);
	return TRUE;
}

static void syNetInputHistoryLogFirstWrite(s32 player, const SYNetInputFrame *frame, u8 provenance,
                                           const char *writer, sb32 mint_downgraded)
{
	SYNetInputFrame gameplay;
	SYNetInputFrame tx;
	sb32 have_gameplay;
	sb32 have_tx;

	if ((frame == NULL) || (syNetInputCheckPlayer(player) == FALSE))
	{
		return;
	}
	have_gameplay = syNetInputGetLocalGameplayFrame(player, frame->tick, &gameplay);
	have_tx = syNetInputGetStoredFrame(sSYNetInputTransmittedHistory, player, frame->tick, &tx);
	port_log(
	    "SSB64 NetInput: HISTORY_AUTH_FIRST_WRITE player=%d tick=%u sim_tick=%u writer=%s prov=%s "
	    "btn=0x%04X sx=%d sy=%d pred=%d have_gameplay=%d have_tx=%d mint_downgraded=%d\n",
	    (int)player, (unsigned int)frame->tick, (unsigned int)syNetInputGetTick(),
	    (writer != NULL) ? writer : "?", syNetInputHistoryProvenanceTagName(provenance),
	    (unsigned int)frame->buttons, (int)frame->stick_x, (int)frame->stick_y, (int)frame->is_predicted,
	    (int)have_gameplay, (int)have_tx, (int)mint_downgraded);
}
#else
#define SYNETINPUT_STRICT_TAG(tag) ((void)0)
#define SYNETINPUT_PROVENANCE_TAG(prov, writer) ((void)0)
#endif

void syNetInputStoreFrame(SYNetInputFrame history[][SYNETINPUT_HISTORY_LENGTH], s32 player, SYNetInputFrame *frame)
{
#if defined(PORT) && defined(SSB64_NETMENU)
	/*
	 * Published History ownership: gameplay-authoritative rows are immutable. Mint of
	 * Local+!predicted requires GAMEPLAY/LOCAL_PUBLISH provenance — latch/unknown cannot
	 * freeze poison into seal (soak1 67923985 tick 436).
	 */
	if ((history == sSYNetInputHistory) && (frame != NULL) && (syNetInputCheckPlayer(player) != FALSE) &&
	    (frame->tick != 0U))
	{
		SYNetInputFrame *slot = &history[player][frame->tick % SYNETINPUT_HISTORY_LENGTH];
		u8 pending_prov = sSYNetInputStoreProvenance;
		const char *writer =
		    (sSYNetInputStoreProvenanceWriter != NULL) ? sSYNetInputStoreProvenanceWriter : "store_history";
		sb32 first_write;
		sb32 mint_downgraded = FALSE;
		u32 idx;

		if ((slot->is_valid != FALSE) && (slot->tick == frame->tick) &&
		    (syNetInputLocalHistoryAuthFreezeBlocks(player, slot, frame, writer) != FALSE))
		{
			syNetInputHistoryProvenanceClearPending();
			return;
		}

		/* Infer provenance when caller forgot to tag. */
		if (pending_prov == (u8)nSYNetInputHistoryProvNone)
		{
			if (frame->is_predicted != FALSE)
			{
				pending_prov = (u8)nSYNetInputHistoryProvPrediction;
			}
			else if (frame->source == nSYNetInputSourceRemoteConfirmed)
			{
				pending_prov = (u8)nSYNetInputHistoryProvRemoteConfirmed;
			}
			else if ((frame->source == nSYNetInputSourceLocal) && (frame->is_predicted == FALSE))
			{
				SYNetInputFrame gameplay;

				if ((syNetInputGetLocalGameplayFrame(player, frame->tick, &gameplay) != FALSE) &&
				    (syNetInputFrameGameplayEquals(&gameplay, frame) != FALSE))
				{
					pending_prov = (u8)nSYNetInputHistoryProvGameplay;
				}
			}
		}

		/*
		 * Mint gate: Local+!predicted without gameplay-auth / remote / explicit gap_hold
		 * cannot create seal-eligible authority — downgrade to prediction.
		 */
		if ((frame->source == nSYNetInputSourceLocal) && (frame->is_predicted == FALSE) &&
		    (syNetInputHistoryProvenanceIsGameplayAuth(pending_prov) == FALSE) &&
		    (pending_prov != (u8)nSYNetInputHistoryProvRemoteConfirmed) &&
		    (pending_prov != (u8)nSYNetInputHistoryProvGapHold))
		{
			port_log(
			    "SSB64 NetInput: HISTORY_AUTH_MINT_DOWNGRADE player=%d tick=%u writer=%s "
			    "from_prov=%s sx=%d sy=%d\n",
			    (int)player, (unsigned int)frame->tick, writer,
			    syNetInputHistoryProvenanceTagName(pending_prov), (int)frame->stick_x,
			    (int)frame->stick_y);
			frame->is_predicted = TRUE;
			pending_prov = (u8)nSYNetInputHistoryProvPrediction;
			mint_downgraded = TRUE;
		}

		first_write = ((slot->is_valid == FALSE) || (slot->tick != frame->tick)) ? TRUE : FALSE;
		idx = frame->tick % SYNETINPUT_HISTORY_LENGTH;
		syNetInputStrictWitnessOnStore(history, player, frame);
		history[player][idx] = *frame;
		sSYNetInputHistoryProvenance[player][idx] = pending_prov;
		if (first_write != FALSE)
		{
			syNetInputHistoryLogFirstWrite(player, frame, pending_prov, writer, mint_downgraded);
		}
		syNetInputHistoryProvenanceClearPending();
		return;
	}
	syNetInputStrictWitnessOnStore(history, player, frame);
	syNetInputHistoryProvenanceClearPending();
#endif
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
#if defined(SSB64_NETMENU)
	if (sSYNetInputLocalLabActive != FALSE)
	{
		return (player == sSYNetInputLocalLabPlayer) ? TRUE : FALSE;
	}
#endif
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return FALSE;
	}
	local_slot = syNetPeerGetLocalSimSlot();
	extra_slot = syNetPeerGetExtraLocalSenderSimSlot();
	return ((player == local_slot) || (player == extra_slot)) ? TRUE : FALSE;
}

/*
 * NETMENU: gameplay owner is sample_tick (feel 0). Send-lead horizon is sample+D (provisional fill).
 * Non-NETMENU keeps legacy sample+D ownership on the delay ring only.
 */
static u32 syNetInputLocalGameplayOwnerTick(u32 sample_tick)
{
#if defined(SSB64_NETMENU)
	return sample_tick;
#else
	u32 d;

	d = syNetPeerGetCommittedInputDelay();
	if ((~(u32)0 - sample_tick) < d)
	{
		return ~(u32)0;
	}
	return sample_tick + d;
#endif
}

static u32 syNetInputLocalSendLeadOwnerTick(u32 sample_tick)
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
#if !defined(SSB64_NETMENU)
static sb32 syNetInputFrameGameplayEquals(const SYNetInputFrame *a, const SYNetInputFrame *b);
#endif
#if defined(SSB64_NETMENU)
static void syNetInputFillHoldLastSoftOnsetIfNeeded(s32 player, u32 tick, SYNetInputFrame *out_frame);
static sb32 syNetInputGetLocalGameplayFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);
static sb32 syNetInputStickReplaceIsRelease(const SYNetInputFrame *old_frame, const SYNetInputFrame *wire);
static sb32 syNetInputStickSameAnalogIntent(s8 ax, s8 ay, s8 bx, s8 by);
static sb32 syNetInputStickLooksAnalog(s8 stick_x, s8 stick_y);
static u32 syNetInputGgpoStickCompletedSimMicroDeadband(void);
static void syNetInputApplyAnalogPredictionDecay(s8 *stick_x, s8 *stick_y, u32 lead_ticks);
#endif
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

#if defined(PORT) && defined(SSB64_NETMENU)
/*
 * Mash during Appear/countdown (game_status Wait) must not enter gameplay/send-lead rings or remote
 * history. Feel-0 still stages provisional runway for intro hr, but rows stay neutral so the first
 * post-Go stick onset is not a REPLACE/GGPO against intro HID (soak1 Turn@394 vs Wait).
 * Synctest remains skipped for Wait — this is input policy, not intro hash fidelity.
 */
static sb32 syNetInputIntroWaitForceNeutralActive(void)
{
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return FALSE;
	}
	if ((gSCManagerBattleState == NULL) || (gSCManagerBattleState->game_status != nSCBattleGameStatusWait))
	{
		return FALSE;
	}
	return TRUE;
}

static void syNetInputForceFrameNeutralButtonsStick(SYNetInputFrame *frame)
{
	if (frame == NULL)
	{
		return;
	}
	frame->buttons = 0;
	frame->stick_x = 0;
	frame->stick_y = 0;
}
#endif

static void syNetInputBuildLocalFrameFromLatch(s32 player, u32 owner_tick, SYNetInputFrame *out_frame)
{
	SYController *controller;
	s32 hw_player;
	s8 stick_x;
	s8 stick_y;

	hw_player = syNetPeerResolveLocalHardwareDevice(player);
	if (syNetInputCheckPlayer(hw_player) == FALSE)
	{
		syNetInputMakeFrame(out_frame, owner_tick, 0, 0, 0, nSYNetInputSourceLocal, FALSE);
		return;
	}
	controller = &sSYNetInputHardwareLatch[hw_player];
	stick_x = controller->stick_range.x;
	stick_y = controller->stick_range.y;
	syNetInputNoteLocalEncodingOnSample(player, stick_x, stick_y, owner_tick);
	if (syNetInputMixedInputQuantizeEnabled() != FALSE)
	{
		syNetInputQuantizeStickToDigitalCardinals(&stick_x, &stick_y);
	}
	syNetInputMakeFrame(out_frame, owner_tick, syNetInputButtonsFromController(controller), stick_x, stick_y,
	                   nSYNetInputSourceLocal, FALSE);
#if defined(PORT) && defined(SSB64_NETMENU)
	if (syNetInputIntroWaitForceNeutralActive() != FALSE)
	{
		syNetInputForceFrameNeutralButtonsStick(out_frame);
		if (syNetInputCheckPlayer(player) != FALSE)
		{
			syNetInputClearFrame(&sSYNetInputSlots[player].last_non_neutral);
		}
	}
#endif
}

#if defined(SSB64_NETMENU)
/*
 * Local sample ticks peers already share: TransmittedHistory (NoteTransmit lock) or published
 * History (LOCAL_PUBLISH during epoch hold before egress NoteTransmit). soak1 1842112848: Linux
 * LOCAL_PUBLISH@753 sx=18 under tick_commit blocked with Transmitted miss; post-resim
 * STICK_SAMPLE@753 hold-last 12 while Android REMOTE_PUBLISH@753 sx=18 → FC@840 figh inputs MATCH.
 * See docs/bugs/netplay_post_resim_wirelocked_hid_restage_2026-07-13.md.
 */
static sb32 syNetInputTryGetLocalWireLockedSample(s32 player, u32 sample_tick, SYNetInputFrame *out_frame)
{
	SYNetInputFrame published;

	if ((out_frame == NULL) || (syNetInputCheckPlayer(player) == FALSE) || (sample_tick == 0U))
	{
		return FALSE;
	}
	if (syNetInputGetStoredFrame(sSYNetInputTransmittedHistory, player, sample_tick, out_frame) != FALSE)
	{
		return TRUE;
	}
	if (syNetInputGetHistoryFrame(player, sample_tick, &published) == FALSE)
	{
		return FALSE;
	}
	if ((published.is_valid == FALSE) || (published.tick != sample_tick) ||
	    (syNetInputLocalHistoryGameplayAuth(player, &published) == FALSE))
	{
		return FALSE;
	}
	*out_frame = published;
	return TRUE;
}
#endif

static void syNetInputStoreLocalDelayFrameFromLatch(s32 player, u32 sample_tick)
{
	SYNetInputFrame frame;
#if defined(SSB64_NETMENU)
	u32 lead_tick;
	u32 ahead_tick;
	SYNetInputFrame prior_tx;
	sb32 wire_locked;
#endif

	if (syNetInputIsLocalDelaySlot(player) == FALSE)
	{
		return;
	}
#if defined(SSB64_NETMENU)
	/*
	 * Wire-locked sample is authority for this sim tick. After exclusive-target GGPO rewind, FuncRead
	 * restages the same tick from the wall-rate HID FIFO — a later physical sample — and must not
	 * overwrite gameplay / NoteTransmit-align transmitted to that FIFO (soak1 2017633508: Linux
	 * LOCAL_PUBLISH@510 sx=11,82 already on the wire; post-resim STICK_SAMPLE@510 used sx=8,49 while
	 * Android promoted remote 11,82 → FC@520 figh inputs MATCH). TransmittedHistory OR published
	 * local History (hold-window LOCAL_PUBLISH before NoteTransmit) both count as wire-locked.
	 * See docs/bugs/netplay_post_resim_wirelocked_hid_restage_2026-07-13.md.
	 */
	wire_locked = syNetInputTryGetLocalWireLockedSample(player, sample_tick, &prior_tx);
	if (wire_locked != FALSE)
	{
		frame = prior_tx;
		frame.tick = sample_tick;
		frame.source = nSYNetInputSourceLocal;
		frame.is_predicted = FALSE;
		frame.is_valid = TRUE;
		/* Promote History-only locks into Transmitted so later Resync / restage hits stay stable. */
		syNetInputStoreFrame(sSYNetInputTransmittedHistory, player, &frame);
	}
	else if ((sample_tick != 0U) && (sample_tick <= sSYNetInputLocalGameplayLastTick[player]))
	{
		/*
		 * Restage of an already-visited tick with no wire lock (LOCAL_PUBLISH gap during
		 * resim). Live HID by now is a wall-clock-later sample; latching it rewrites the
		 * past and the next bundle revises the wire (soak1 369009235: gap 453-454 restaged
		 * (77,31) over first-pass (-5,-22) → REPLACE_NEWER → PEER@457). Keep the first-pass
		 * gameplay row; else hold-last from the nearest earlier gameplay row.
		 */
		SYNetInputFrame kept;
		u32 back;
		sb32 have_fill = FALSE;

		if (syNetInputGetLocalGameplayFrame(player, sample_tick, &kept) != FALSE)
		{
			frame = kept;
			frame.tick = sample_tick;
			have_fill = TRUE;
		}
		else
		{
			for (back = 1U; (back <= 8U) && (back < sample_tick); back++)
			{
				if (syNetInputGetLocalGameplayFrame(player, sample_tick - back, &kept) != FALSE)
				{
					frame = kept;
					frame.tick = sample_tick;
					have_fill = TRUE;
					port_log(
					    "SSB64 NetInput: GAP_RESTAGE_HOLD_LAST player=%d tick=%u from=%u "
					    "btn=0x%04X sx=%d sy=%d\n",
					    (int)player, (unsigned int)sample_tick,
					    (unsigned int)(sample_tick - back), (unsigned int)frame.buttons,
					    (int)frame.stick_x, (int)frame.stick_y);
					break;
				}
			}
		}
		if (have_fill == FALSE)
		{
			syNetInputBuildLocalFrameFromLatch(player, sample_tick, &frame);
			port_log(
			    "SSB64 NetInput: GAP_RESTAGE_LATCH_FALLBACK player=%d tick=%u sx=%d sy=%d\n",
			    (int)player, (unsigned int)sample_tick, (int)frame.stick_x, (int)frame.stick_y);
		}
		frame.source = nSYNetInputSourceLocal;
		frame.is_predicted = FALSE;
		frame.is_valid = TRUE;
	}
	else
	{
		syNetInputBuildLocalFrameFromLatch(player, sample_tick, &frame);
	}
	syNetInputStoreFrame(sSYNetInputLocalGameplayHistory, player, &frame);
	if ((sample_tick != 0U) && (sample_tick > sSYNetInputLocalGameplayLastTick[player]))
	{
		sSYNetInputLocalGameplayLastTick[player] = sample_tick;
	}
	/*
	 * Send-lead: authoritative row at sample_tick (wire = sample+D) plus hold-last provisional rows
	 * through sample+D so AppendDelayed can raise hr past DelaySim frontier during intro Wait.
	 * Real samples overwrite provisional ahead slots when those ticks arrive.
	 * Peer egress must NoteTransmitted only for the current sample tick — provisional ahead is wire-only
	 * (see syNetPeerAppendDelayedLocalRowsToBundle); otherwise stick onset revises transmitted authority
	 * and GGPO-storms into rollback_epoch / load_fail_hold.
	 */
	syNetInputStoreFrame(sSYNetInputLocalDelayHistory, player, &frame);
	/*
	 * If egress NoteTransmitted provisional delay[sim] before this sample, realign transmitted +
	 * published to the feel-0 HID row now (NoteTransmitted refuses pre-sample locks).
	 * Skip when restaging an already wire-locked tick — that realign is what destroyed host sim
	 * vs peer wire after exclusive-target resim.
	 */
	if (wire_locked == FALSE)
	{
		if ((syNetInputGetStoredFrame(sSYNetInputTransmittedHistory, player, frame.tick, &prior_tx) != FALSE) &&
		    (syNetInputFrameGameplayEquals(&prior_tx, &frame) == FALSE))
		{
			syNetInputNoteTransmittedSimFrame(player, &frame);
		}
	}
	lead_tick = syNetInputLocalSendLeadOwnerTick(sample_tick);
	if ((lead_tick != ~(u32)0) && (lead_tick > sample_tick))
	{
		for (ahead_tick = sample_tick + 1U; ahead_tick <= lead_tick; ahead_tick++)
		{
			frame.tick = ahead_tick;
			syNetInputStoreFrame(sSYNetInputLocalDelayHistory, player, &frame);
			if (ahead_tick == lead_tick)
			{
				break;
			}
		}
	}
#else
	syNetInputBuildLocalFrameFromLatch(player, sample_tick, &frame);
	syNetInputStoreFrame(sSYNetInputLocalDelayHistory, player, &frame);
#endif
}

static void syNetInputStageLocalDelayFramesFromLatch(u32 sample_tick)
{
	u32 owner_tick;
	s32 local_slot;
	s32 extra_slot;

	if (syNetInputAuthoritativeWireContractEnabled() == FALSE)
	{
		return;
	}
	owner_tick = syNetInputLocalGameplayOwnerTick(sample_tick);
#if defined(SSB64_NETMENU)
	if (sSYNetInputLocalLabActive != FALSE)
	{
		if ((sSYNetInputLocalLabPlayer >= 0) && (sSYNetInputLocalLabPlayer < MAXCONTROLLERS))
		{
			syNetInputStoreLocalDelayFrameFromLatch(sSYNetInputLocalLabPlayer, owner_tick);
		}
		return;
	}
#endif
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
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

#if defined(SSB64_NETMENU)
static sb32 syNetInputGetLocalGameplayFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	if (syNetInputIsLocalDelaySlot(player) == FALSE)
	{
		return FALSE;
	}
	return syNetInputGetStoredFrame(sSYNetInputLocalGameplayHistory, player, tick, out_frame);
}

u32 syNetInputGetLocalGameplayAuthSimTick(s32 player)
{
	if ((player < 0) || (player >= MAXCONTROLLERS))
	{
		return 0U;
	}
	return sSYNetInputLocalGameplayLastTick[player];
}

sb32 syNetInputTryGetLocalWireResendFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	SYNetInputFrame frame;

	if ((out_frame == NULL) || (syNetInputCheckPlayer(player) == FALSE) || (tick == 0U))
	{
		return FALSE;
	}
	if (syNetInputGetLocalGameplayFrame(player, tick, &frame) != FALSE)
	{
		*out_frame = frame;
		out_frame->tick = tick;
		out_frame->source = nSYNetInputSourceLocal;
		out_frame->is_predicted = FALSE;
		out_frame->is_valid = TRUE;
		return TRUE;
	}
	if (syNetInputGetStoredFrame(sSYNetInputTransmittedHistory, player, tick, &frame) != FALSE)
	{
		*out_frame = frame;
		out_frame->tick = tick;
		out_frame->source = nSYNetInputSourceLocal;
		out_frame->is_predicted = FALSE;
		out_frame->is_valid = TRUE;
		return TRUE;
	}
	return FALSE;
}
#endif

#define nSYNetLocalAuthoritySourceNone 0
#define nSYNetLocalAuthoritySourceLatch 1
#define nSYNetLocalAuthoritySourceDelay 2
#define nSYNetLocalAuthoritySourceTransmitted 3
#if defined(SSB64_NETMENU)
#define nSYNetLocalAuthoritySourceGameplay 4
#endif

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
#if defined(SSB64_NETMENU)
	case nSYNetLocalAuthoritySourceGameplay:
		return "gameplay";
#endif
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
#if defined(SSB64_NETMENU)
	/*
	 * Feel-0: gameplay ring is sim + FC authority. Prefer it over Transmitted — egress can
	 * NoteTransmit delay[sim] before FuncRead stages the real sample (provisional hold-last from
	 * sample-1 still sits in send-lead), which left FC hist≠authority (soak 1989925098 @600).
	 */
	if (syNetInputGetLocalGameplayFrame(player, tick, out_frame) != FALSE)
	{
		out_frame->tick = tick;
		out_frame->source = nSYNetInputSourceLocal;
		out_frame->is_predicted = FALSE;
		out_frame->is_valid = TRUE;
		if (out_source_rank != NULL)
		{
			*out_source_rank = nSYNetLocalAuthoritySourceGameplay;
		}
		return TRUE;
	}
#endif
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
#if !defined(SSB64_NETMENU)
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
#endif
	/* Resim must not invent authority from live HID. */
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return FALSE;
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

	if ((syNetInputAuthoritativeWireContractEnabled() == FALSE) ||
	    ((syNetPeerIsVSSessionActive() == FALSE)
#if defined(SSB64_NETMENU)
	     && (sSYNetInputLocalLabActive == FALSE)
#endif
	         ) ||
	    (syNetRollbackIsResimulating() != FALSE) || (tick == 0U) ||
	    (syNetInputResolveLocalAuthorityFrameEx(player, tick, &resolved, &source_rank) == FALSE))
	{
		return;
	}
#if defined(PORT) && defined(SSB64_NETMENU)
	/*
	 * Latch never mints gameplay-authoritative History — not only past ticks
	 * (soak1 1023513151 past mass-republish; soak1 67923985 gap 436 under live-ahead).
	 * Authority requires a gameplay or transmitted sample already staged for `tick`.
	 */
	if (source_rank == nSYNetLocalAuthoritySourceLatch)
	{
		port_log(
		    "SSB64 NetInput: LOCAL_PUBLISH_LATCH_REFUSE player=%d tick=%u frontier=%u sx=%d sy=%d\n",
		    (int)player, (unsigned int)tick, (unsigned int)sSYNetInputLocalGameplayLastTick[player],
		    (int)resolved.stick_x, (int)resolved.stick_y);
		return;
	}
#endif
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
#if defined(PORT) && defined(SSB64_NETMENU)
	if ((had_published != FALSE) &&
	    (syNetInputLocalHistoryAuthFreezeBlocks(player, &published, &resolved, "promote_local") != FALSE))
	{
		return;
	}
	SYNETINPUT_PROVENANCE_TAG(nSYNetInputHistoryProvLocalPublish, "promote_local");
#endif
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

	if ((syNetInputAuthoritativeWireContractEnabled() == FALSE) || (syNetRollbackIsResimulating() != FALSE) ||
	    (tick == 0U))
	{
		return;
	}
#if defined(SSB64_NETMENU)
	if (sSYNetInputLocalLabActive != FALSE)
	{
		if ((sSYNetInputLocalLabPlayer >= 0) && (sSYNetInputLocalLabPlayer < MAXCONTROLLERS))
		{
			syNetInputPromoteLocalAuthorityPublished(sSYNetInputLocalLabPlayer, tick);
		}
		return;
	}
#endif
	if (syNetPeerIsVSSessionActive() == FALSE)
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
	/*
	 * Speculative hold-last while wire for this sim tick is not confirmed yet. Must be tagged predicted so
	 * rollback mismatch / NoteSimTickPredictedRemoteUsage can correct when the real row arrives — marking this
	 * as RemoteConfirmed (legacy) made phase-lock prediction invisible to recovery.
	 */
	syNetInputMakeFrame(out_frame, tick, buttons, stick_x, stick_y, nSYNetInputSourceRemotePredicted, TRUE);
#if defined(SSB64_NETMENU)
	/*
	 * Hold-last previously kept full last_confirmed mag with no decay (MakePredicted decays;
	 * Resolve did not) — smash −66 survived send-lead through Turn allow → false did_dash
	 * (soak1 179193526 @464). Decay by lead, then smash-release / flip / dash-gate clamp.
	 * See docs/bugs/netplay_hold_last_dash_gate_send_lead_peer_2026-07-20.md.
	 */
	if ((last_confirmed->is_valid != FALSE) && (tick > last_confirmed->tick) &&
	    (syNetInputStickLooksAnalog(out_frame->stick_x, out_frame->stick_y) != FALSE))
	{
		syNetInputApplyAnalogPredictionDecay(&out_frame->stick_x, &out_frame->stick_y,
		                                     tick - last_confirmed->tick);
	}
	syNetInputFillHoldLastSoftOnsetIfNeeded(player, tick, out_frame);
	/*
	 * Hard (0,0) invent after soft-onset failed: input-contract witness for dual-stick
	 * onset / Wait→Turn forks (soak 979771282). See ZERO_ONSET_PREDICT + restrict.
	 */
	if (syNetInputFrameStickGameplayNeutral(out_frame) != FALSE)
	{
		static u32 sZeroOnsetInventLogTick = ~(u32)0;
		static s32 sZeroOnsetInventLogPlayer = -1;
		u32 hr;
		u32 frontier;
		sb32 restrict_on;
		sb32 in_grace;

		restrict_on = syNetInputRemoteHumanZeroOnsetPredictRestrict(tick);
		hr = syNetPeerGetHighestRemoteTick();
		frontier = (hr != 0U) ? syNetPeerDelaySimTickFromWire(hr) : 0U;
		in_grace = ((sSYNetInputPostGoWirePacingGraceUntil != ~(u32)0) &&
		            (tick <= sSYNetInputPostGoWirePacingGraceUntil))
		               ? TRUE
		               : FALSE;
		if ((tick != sZeroOnsetInventLogTick) || (player != sZeroOnsetInventLogPlayer))
		{
			port_log(
			    "SSB64 NetInput: ZERO_ONSET_PREDICT phase=invent player=%d tick=%u sx=%d sy=%d "
			    "pred=1 restrict=%d grace=%d grace_until=%u hr=%u frontier_sim=%u "
			    "last_conf_tick=%u last_conf_sx=%d last_conf_sy=%d\n",
			    (int)player, (unsigned int)tick, (int)out_frame->stick_x, (int)out_frame->stick_y,
			    (int)restrict_on, (int)in_grace, (unsigned int)sSYNetInputPostGoWirePacingGraceUntil,
			    (unsigned int)hr, (unsigned int)frontier,
			    (unsigned int)((last_confirmed->is_valid != FALSE) ? last_confirmed->tick : 0U),
			    (int)((last_confirmed->is_valid != FALSE) ? last_confirmed->stick_x : 0),
			    (int)((last_confirmed->is_valid != FALSE) ? last_confirmed->stick_y : 0));
			sZeroOnsetInventLogTick = tick;
			sZeroOnsetInventLogPlayer = player;
		}
	}
#endif
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
#if defined(PORT) && defined(SSB64_NETMENU)
	/*
	 * Phase 2: confirmed published comes only from the authority ledger. If resolve saw wire
	 * but ledger is empty (ordering race), dual-write then refresh. Hold-last predicted path
	 * below is the only non-ledger publish promote may still invent.
	 * See docs/bugs/netplay_confirmed_publish_write_once_2026-07-12.md.
	 */
	if (source_rank == nSYNetRemoteAuthoritySourceWireConfirmed)
	{
		if (syNetInputAuthorityLedgerTryGet(player, tick, NULL, NULL) == FALSE)
		{
			syNetInputAuthorityLedgerCommitWire(player, tick, &resolved);
		}
		if (syNetInputRefreshPublishedFromAuthorityLedger(player, tick, "promote_remote_authority") != FALSE)
		{
			return;
		}
	}
#endif
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
#if defined(PORT) && defined(SSB64_NETMENU)
	/*
	 * Hold-last invent into published is the seed of completed-sim LEDGER_REFRESH corrections.
	 * Skip promote invent when tick is already completed — inventing provisional stick onto
	 * finished history only creates a later ±1 GGPO when wire arrives.
	 * Do NOT skip invent during live Hold via PublishFrame: soaking without durable
	 * provisional rows made ledger refresh silent (inputs MATCH, thunder head forked).
	 * Mid-Hold GGPO is preferred; weapon-only baseline absorb covers deepen storms.
	 * See docs/bugs/netplay_ness_pk_hold_skip_durable_aim_fork_2026-07-15.md.
	 */
	if (source_rank == nSYNetRemoteAuthoritySourceHoldLast)
	{
		u32 now_sim = syNetInputGetTick();

		if ((tick < now_sim) || (syNetplayNessAnyLiveFighterInFcResimDeferScope() != FALSE))
		{
			if (syNetInputAuthorityPublishLogEnabled() != FALSE)
			{
				port_log(
				    "SSB64 NetInput: REMOTE_PUBLISH_SKIP player=%d sim_tick=%u reason=%s\n",
				    (int)player, (unsigned int)tick,
				    (tick < now_sim) ? "hold_last_completed_sim" : "hold_last_ness_pk_scope");
			}
			return;
		}
		/*
		 * Zero-onset hard invent: do not mint History (0,0) while Restrict is armed.
		 * Prefer stall / leave gap over Wait vs Walk/Turn (soak 250667155 STATUS_FORK@406).
		 */
		if ((syNetInputFrameStickGameplayNeutral(&resolved) != FALSE) &&
		    (syNetInputRemoteHumanZeroOnsetPredictRestrict(tick) != FALSE))
		{
			if (syNetInputAuthorityPublishLogEnabled() != FALSE)
			{
				port_log(
				    "SSB64 NetInput: REMOTE_PUBLISH_SKIP player=%d sim_tick=%u "
				    "reason=zero_onset_stall\n",
				    (int)player, (unsigned int)tick);
			}
			return;
		}
		/*
		 * True analog-ramp (peek differs from last_confirmed): do not mint hold_last
		 * of the stale mag — soft-onset / wire should supply the peek instead.
		 */
		if ((syNetInputFrameStickGameplayNeutral(&resolved) == FALSE) &&
		    (syNetInputRemoteHumanAnalogRampPredictTighten(tick) != FALSE))
		{
			if (syNetInputAuthorityPublishLogEnabled() != FALSE)
			{
				port_log(
				    "SSB64 NetInput: REMOTE_PUBLISH_SKIP player=%d sim_tick=%u "
				    "reason=analog_ramp_tighten sx=%d sy=%d\n",
				    (int)player, (unsigned int)tick, (int)resolved.stick_x,
				    (int)resolved.stick_y);
			}
			return;
		}
	}
	/*
	 * Thin write-once safety: hold-last / non-ledger path must not mutate confirmed published.
	 * Ledger refresh above bypasses this gate.
	 */
	if ((had_published != FALSE) &&
	    (syNetInputRemoteConfirmedWriteOnceBlocks(player, &published, &resolved, "promote_remote_authority") !=
	     FALSE))
	{
		syNetInputRemoteConfirmedWriteOnceQueueCorrection(player, tick, &published);
		return;
	}
	/* Confirmed invent without ledger must not happen — refuse. */
	if (source_rank == nSYNetRemoteAuthoritySourceWireConfirmed)
	{
		return;
	}
#endif
	sim_now = syNetInputGetTick();
	if ((syNetInputAuthorityPublishLogEnabled() != FALSE) && (source_rank == nSYNetRemoteAuthoritySourceWireConfirmed) &&
	    (sim_now > tick) && ((sim_now - tick) > 2U))
	{
		port_log("SSB64 NetInput: REMOTE_PUBLISH_LATE player=%d sim_tick=%u sim_now=%u sx=%d sy=%d\n", (int)player,
		         (unsigned int)tick, (unsigned int)sim_now, (int)resolved.stick_x, (int)resolved.stick_y);
	}
	SYNETINPUT_STRICT_TAG("promote_remote_authority");
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
 * TRUE when every remote-human slot has a strict confirmed remote row for `sim_tick`.
 * Used as the confirmed-path readiness check. On rollback sessions, phase-lock prediction may advance without
 * this (see FuncRead wire admission + Republish) and consume hold-last tagged RemotePredicted until wire arrives.
 * Prediction-recovery windows still require confirmed (`syNetRollbackPredictionRecoveryRequiresConfirmed`).
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

/*
 * TRUE when shared commit admits this sim tick via the phase_lock prediction window (ring missing at wire_base)
 * and prediction-recovery is not forcing confirmed-only stalls.
 */
static sb32 syNetInputPhaseLockPredictAdvanceAllowed(u32 sim_tick)
{
	SYNetPeerSharedCommitStep shared;

	if ((syNetSessionParamsRollbackEnabled() == FALSE) || (syNetInputGetUseInputPrediction() == FALSE))
	{
		return FALSE;
	}
	if (syNetRollbackPredictionRecoveryRequiresConfirmed(sim_tick) != FALSE)
	{
		return FALSE;
	}
	syNetPeerEvaluateSharedCommitStep(sim_tick, &shared);
	if ((shared.advance == FALSE) || (shared.uses_prediction == FALSE))
	{
		return FALSE;
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
		/* Predict-approved: publish speculative remote frames and allow battle sim; rollback corrects later. */
		if (syNetInputPhaseLockPredictAdvanceAllowed(tick) == FALSE)
		{
			return FALSE;
		}
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

/*
 * TRUE when every remote-human slot has strict-confirmed (ledger/wire) input for `sim_tick`
 * AND the published history row matches it gameplay-wise. Used to retroactively promote
 * rollback snapshots captured under prediction to load-safe once the prediction is proven
 * correct. See docs/bugs/netplay_divergent_load_tick_baseline_stall_2026-07-12.md.
 */
sb32 syNetInputRemoteHumanPublishedMatchesConfirmedForSimTick(u32 sim_tick)
{
	SYNetInputFrame confirmed;
	SYNetInputFrame published;
	s32 i;
	s32 n;
	s32 slot;

	if (sim_tick == 0U)
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
		if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(slot, sim_tick, &confirmed) == FALSE)
		{
			return FALSE;
		}
		if (syNetInputGetHistoryFrame(slot, sim_tick, &published) == FALSE)
		{
			return FALSE;
		}
		if (syNetInputFrameGameplayEquals(&published, &confirmed) == FALSE)
		{
			return FALSE;
		}
	}
	return TRUE;
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
#if defined(SSB64_NETMENU)
	syNetInputHwCaptureFifoReset();
#endif
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
#if defined(SSB64_NETMENU)
	sSYNetInputSealSkipSpanDumpBudget = 12U;
	sSYNetInputGgpoStickCompletedSimMicroDeadband = -1;
	sSYNetInputGgpoClassQueuedButton = 0U;
	sSYNetInputGgpoClassQueuedOnset = 0U;
	sSYNetInputGgpoClassQueuedRelease = 0U;
	sSYNetInputGgpoClassQueuedRealStick = 0U;
	sSYNetInputGgpoClassQueuedMicro = 0U;
	sSYNetInputGgpoClassSkippedMicro = 0U;
	sSYNetInputGgpoSkipMicroLogsRemaining = 16U;
	sSYNetInputSawIntroWait = FALSE;
	sSYNetInputPostGoWirePacingGraceUntil = ~(u32)0;
#endif
	{
		const char *env_onset_log;
		const char *env_turn_dash;
		s32 onset_n;

		/*
		 * Budget: ANALOG_ONSET_LOG=N (N>1) or =1 → 64. Auto-enable 64 lines when
		 * TURN_DASH_WITNESS is on (soak1 1981389058: need hold_last_smash_* visibility
		 * without a separate env). Explicit ANALOG_ONSET_LOG=0 still disables.
		 */
		env_onset_log = getenv("SSB64_NETPLAY_ANALOG_ONSET_LOG");
		env_turn_dash = getenv("SSB64_TURN_DASH_WITNESS");
		sSYNetInputAnalogOnsetLogBudget = 0U;
		if ((env_onset_log != NULL) && (env_onset_log[0] != '\0'))
		{
			onset_n = atoi(env_onset_log);
			if (onset_n < 0)
			{
				onset_n = 0;
			}
			if (onset_n == 1)
			{
				sSYNetInputAnalogOnsetLogBudget = 64U;
			}
			else if (onset_n > 1)
			{
				if (onset_n > 256)
				{
					onset_n = 256;
				}
				sSYNetInputAnalogOnsetLogBudget = (u32)onset_n;
			}
		}
		else if ((env_turn_dash != NULL) && (env_turn_dash[0] != '\0') && (atoi(env_turn_dash) != 0))
		{
			sSYNetInputAnalogOnsetLogBudget = 64U;
		}
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
#if defined(SSB64_NETMENU)
			syNetInputClearFrame(&sSYNetInputLocalGameplayHistory[player][i]);
			syNetInputClearFrame(&sSYNetInputRemoteAuthorityLedger[player][i]);
			sSYNetInputRemoteAuthorityLedgerOrigin[player][i] = SYNETINPUT_AUTH_LEDGER_ORIGIN_NONE;
			sSYNetInputHistoryProvenance[player][i] = (u8)nSYNetInputHistoryProvNone;
#endif
			sSYNetInputRemotePacketSeqHistory[player][i] = 0U;
			sSYNetInputRemotePacketSeqValid[player][i] = FALSE;
#endif
		}
#if defined(PORT) && defined(SSB64_NETMENU)
		sSYNetInputLocalGameplayLastTick[player] = 0U;
#endif
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
#if defined(PORT) && defined(SSB64_NETMENU)
	/* Flush previous match's strict-input witness counters before rings reset. */
	syNetInputStrictWitnessLogMatchSummary("session_start");
#endif
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

#if defined(PORT) && defined(SSB64_NETMENU)
sb32 syNetInputIsLocalLabActive(void)
{
	return sSYNetInputLocalLabActive;
}

s32 syNetInputGetLocalLabPlayer(void)
{
	return sSYNetInputLocalLabPlayer;
}

void syNetInputEndLocalLabSession(void)
{
	if (sSYNetInputLocalLabActive == FALSE)
	{
		return;
	}
	sSYNetInputLocalLabActive = FALSE;
	sSYNetInputLocalLabPlayer = 0;
	port_log("SSB64 NetInput: local_lab end\n");
}

void syNetInputStartLocalLabSession(s32 local_player, u32 input_delay)
{
	s32 p;

	if ((local_player < 0) || (local_player >= MAXCONTROLLERS))
	{
		local_player = 0;
	}
	if (sSYNetInputLocalLabActive != FALSE)
	{
		syNetInputEndLocalLabSession();
	}
	syNetInputStartVSSession();
	syNetPeerCommitLabInputDelay(input_delay, "training_lab");
	sSYNetInputLocalLabPlayer = local_player;
	sSYNetInputLocalLabActive = TRUE;
	for (p = 0; p < MAXCONTROLLERS; p++)
	{
		syNetInputSetSlotSource(p, nSYNetInputSourceLocal);
	}
	port_log("SSB64 NetInput: local_lab start player=%d D=%u\n", (int)local_player, (unsigned int)input_delay);
}

void syNetInputMaybeLogStickSample(const char *mode)
{
	static sb32 sStickSampleCached = -999;
	GObj *fighter_gobj;
	const char *e;
	u32 tick;

	if (sStickSampleCached == -999)
	{
		e = getenv("SSB64_STICK_SAMPLE_LOG");
		sStickSampleCached = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	}
	if (sStickSampleCached == 0)
	{
		return;
	}
	tick = syNetInputGetTick();
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		SYController *controller;
		SYNetInputFrame hist;
		s32 player;
		s32 pred;
		s8 sx;
		s8 sy;
		u16 btn;

		fp = ftGetStruct(fighter_gobj);
		if ((fp == NULL) || (fp->pkind != nFTPlayerKindMan) || (fp->is_control_disable != FALSE))
		{
			continue;
		}
		player = fp->player;
		if ((player < 0) || (player >= MAXCONTROLLERS))
		{
			continue;
		}
		controller = &gSYControllerDevices[player];
		/*
		 * Prefer published history for this sim tick, then fighter pl latch, then device.
		 * Soak1 857278917 Android: gSYControllerDevices stayed (0,0) for all STICK_SAMPLE
		 * rows while LOCAL_PUBLISH / wire had smash — false RESIM_STICK_FORK vs Linux.
		 */
		sx = controller->stick_range.x;
		sy = controller->stick_range.y;
		btn = controller->button_hold;
		pred = 0;
		if (syNetInputGetHistoryFrame(player, tick, &hist) != FALSE)
		{
			sx = hist.stick_x;
			sy = hist.stick_y;
			btn = hist.buttons;
			/*
			 * pred=1 rows are prediction-window guesses for remote slots; cross-peer
			 * disagreement there is normal rollback operation, not a seal/ledger fork.
			 * Scan only trusts pred=0 vs pred=0 comparisons for RESIM_STICK_FORK.
			 */
			if ((hist.is_predicted != FALSE) || (hist.source != nSYNetInputSourceRemoteConfirmed &&
			                                     syNetInputIsRemoteHumanSlot(player) != FALSE))
			{
				pred = 1;
			}
		}
		else
		{
			sx = fp->input.pl.stick_range.x;
			sy = fp->input.pl.stick_range.y;
			if ((sx == 0) && (sy == 0))
			{
				sx = controller->stick_range.x;
				sy = controller->stick_range.y;
			}
			if (syNetInputIsRemoteHumanSlot(player) != FALSE)
			{
				pred = 1;
			}
		}
		port_log("SSB64 STICK_SAMPLE mode=%s tick=%u player=%d sx=%d sy=%d tap_x=%d hold_x=%d btn=0x%04X pred=%d\n",
		         (mode != NULL) ? mode : "?", (unsigned int)tick, (int)player, (int)sx, (int)sy,
		         (int)fp->tap_stick_x, (int)fp->hold_stick_x, (unsigned int)btn, (int)pred);
	}
}

void syNetInputMaybeLogStickTapWitness(const char *mode)
{
	static sb32 sTapWitnessCached = -999;
	GObj *fighter_gobj;
	const char *e;
	u32 tick;
	u32 d;
	s32 local_only;

	if (sTapWitnessCached == -999)
	{
		e = getenv("SSB64_STICK_TAP_WITNESS");
		sTapWitnessCached = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	}
	if (sTapWitnessCached == 0)
	{
		return;
	}
	tick = syNetInputGetTick();
	d = syNetPeerGetCommittedInputDelay();
	/*
	 * Prefer the local human only (lab player, else peer local sim slot). Remote Man slots are
	 * often neutral/predicted and drown the log.
	 */
	local_only = -1;
	if (sSYNetInputLocalLabActive != FALSE)
	{
		local_only = sSYNetInputLocalLabPlayer;
	}
	else if (syNetPeerIsVSSessionActive() != FALSE)
	{
		local_only = syNetPeerGetLocalSimSlot();
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		FTPlayerInput *pl;
		SYController *controller;
		s32 player;
		s32 sx_dev;
		s32 sy_dev;
		s32 sx_pl;
		s32 sy_pl;
		s32 prev_x;
		s32 prev_y;
		u32 tap;
		u32 hold;
		sb32 mismatch;
		sb32 burned;
		sb32 tap_max;
		const char *reason;

		fp = ftGetStruct(fighter_gobj);
		if ((fp == NULL) || (fp->pkind != nFTPlayerKindMan) || (fp->is_control_disable != FALSE))
		{
			continue;
		}
		player = fp->player;
		if ((player < 0) || (player >= MAXCONTROLLERS))
		{
			continue;
		}
		if ((local_only >= 0) && (player != local_only))
		{
			continue;
		}
		controller = &gSYControllerDevices[player];
		pl = &fp->input.pl;
		sx_dev = (s32)controller->stick_range.x;
		sy_dev = (s32)controller->stick_range.y;
		sx_pl = (s32)pl->stick_range.x;
		sy_pl = (s32)pl->stick_range.y;
		prev_x = (s32)pl->stick_prev.x;
		prev_y = (s32)pl->stick_prev.y;
		tap = (u32)fp->tap_stick_x;
		hold = (u32)fp->hold_stick_x;
		mismatch = ((sx_dev != sx_pl) || (sy_dev != sy_pl)) ? TRUE : FALSE;
		{
			s32 abs_sx_dev;

			abs_sx_dev = (sx_dev < 0) ? -sx_dev : sx_dev;
			burned = ((abs_sx_dev >= 56) && (tap >= 3U)) ? TRUE : FALSE;
			tap_max = ((abs_sx_dev >= 20) && (tap >= 250U)) ? TRUE : FALSE;
		}
		if ((mismatch == FALSE) && (burned == FALSE) && (tap_max == FALSE))
		{
			continue;
		}
		if (mismatch != FALSE)
		{
			reason = "device_pl_mismatch";
		}
		else if (tap_max != FALSE)
		{
			reason = "tap_max_held";
		}
		else
		{
			reason = "burned_dash";
		}
		port_log(
		    "SSB64 STICK_TAP_WITNESS mode=%s reason=%s tick=%u player=%d D=%u lab=%d "
		    "sx_dev=%d sy_dev=%d sx_pl=%d sy_pl=%d prev=(%d,%d) tap_x=%u hold_x=%u btn=0x%04X\n",
		    (mode != NULL) ? mode : "?", reason, (unsigned int)tick, (int)player, (unsigned int)d,
		    (sSYNetInputLocalLabActive != FALSE) ? 1 : 0, sx_dev, sy_dev, sx_pl, sy_pl, prev_x, prev_y,
		    (unsigned int)tap, (unsigned int)hold, (unsigned int)controller->button_hold);
	}
}
#endif

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

#if defined(SSB64_NETMENU)
static u32 syNetInputGgpoStickCompletedSimMicroDeadband(void)
{
	const char *env;
	s32 parsed;

	if (sSYNetInputGgpoStickCompletedSimMicroDeadband >= 0)
	{
		return (u32)sSYNetInputGgpoStickCompletedSimMicroDeadband;
	}
	parsed = SYNETINPUT_GGPO_STICK_COMPLETED_SIM_MICRO_DEADBAND_DEFAULT;
	env = getenv("SSB64_NETPLAY_GGPO_STICK_COMPLETED_SIM_MICRO_DEADBAND");
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
	sSYNetInputGgpoStickCompletedSimMicroDeadband = parsed;
	return (u32)sSYNetInputGgpoStickCompletedSimMicroDeadband;
}
#endif

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

/* Exact (0,0) only — soft sticks inside NearNeutral(14) are real releases (soak 1981389058 / 582675261). */
static sb32 syNetInputFrameSticksHardZero(const SYNetInputFrame *frame)
{
	if (frame == NULL)
	{
		return FALSE;
	}
	return ((frame->stick_x == 0) && (frame->stick_y == 0)) ? TRUE : FALSE;
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

#if defined(SSB64_NETMENU)
/* Confirmed or provisional ring row for t — includes near-neutral (release detection). */
static sb32 syNetInputTryPeekRemoteStickRowAtTick(s32 player, u32 t, SYNetInputFrame *out_frame)
{
	SYNetInputFrame wire_row;

	if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, t, out_frame) != FALSE)
	{
		return TRUE;
	}
	if (syNetInputTryGetRemoteHistoryForSimTick(player, t, &wire_row) != FALSE)
	{
		if (out_frame != NULL)
		{
			*out_frame = wire_row;
		}
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetInputStickDashGateActiveX(s8 stick_x)
{
	return (syNetInputAbsS8Diff(stick_x, 0) >= SYNETINPUT_DASH_STICK_RANGE_MIN) ? TRUE : FALSE;
}

/*
 * Turn→Dash product proxy without lr_turn: |sx| crosses DASH_MIN and/or X sign flips while
 * either side is smash-class (soak1 179193526: hold −66 vs wire +28 at allow).
 */
static sb32 syNetInputStickDashGateDisagreeX(s8 hold_x, s8 wire_x)
{
	sb32 hold_dash;
	sb32 wire_dash;
	s32 hold_sign;
	s32 wire_sign;

	hold_dash = syNetInputStickDashGateActiveX(hold_x);
	wire_dash = syNetInputStickDashGateActiveX(wire_x);
	if (hold_dash != wire_dash)
	{
		return TRUE;
	}
	hold_sign = syNetInputStickSign(hold_x);
	wire_sign = syNetInputStickSign(wire_x);
	if ((hold_dash != FALSE) && (hold_sign != 0) && (wire_sign != 0) && (hold_sign != wire_sign))
	{
		return TRUE;
	}
	return FALSE;
}

/*
 * Seal / ledger / local-truth refuse: dash-gate cross or opposite analog smash intent.
 * Soft same-intent mag noise is not a refuse (keeps seal).
 */
static sb32 syNetInputStickSealIntentDisagree(s8 ax, s8 ay, s8 bx, s8 by)
{
	if (syNetInputStickDashGateDisagreeX(ax, bx) != FALSE)
	{
		return TRUE;
	}
	if ((syNetInputStickLooksAnalog(ax, ay) != FALSE) && (syNetInputStickLooksAnalog(bx, by) != FALSE) &&
	    (syNetInputStickSameAnalogIntent(ax, ay, bx, by) == FALSE))
	{
		return TRUE;
	}
	return FALSE;
}

static void syNetInputClampSmashHoldLastBelowDashGate(SYNetInputFrame *out_frame)
{
	s32 sign;
	s32 mag;

	if (out_frame == NULL)
	{
		return;
	}
	mag = syNetInputAbsS8Diff(out_frame->stick_x, 0);
	if (mag < SYNETINPUT_DASH_STICK_RANGE_MIN)
	{
		return;
	}
	sign = syNetInputStickSign(out_frame->stick_x);
	out_frame->stick_x = (s8)(sign * (SYNETINPUT_DASH_STICK_RANGE_MIN - 1));
}
#endif

static sb32 syNetInputTryPeekRemoteAnalogForOnset(s32 player, u32 tick, u32 max_lookback, SYNetInputFrame *out_frame)
{
	u32 t;
	u32 oldest;
	u32 newest;
	u32 peek_ahead;
	u32 ahead_start;

	if ((out_frame == NULL) || (tick == 0U))
	{
		return FALSE;
	}
	peek_ahead = syNetInputAnalogOnsetWirePeekAheadFrames();
	newest = tick + peek_ahead;
	/*
	 * max_lookback==0: send-lead / current+ahead (hold-last soft onset). Include `tick` so wire that
	 * already landed in remote history for this sim tick is used before inventing 0,0 (soak
	 * 1156067044 RESIM_STICK host=0,0 vs guest nonzero — ahead-only skipped the current row).
	 * Backward lookback stays disabled (max_lookback==0 early-out below) to avoid release reinflate
	 * (soak 1645329949). TryPeek requires StickLooksAnalog so a provisional hard-zero at `tick` is ignored.
	 */
	ahead_start = tick;
	for (t = ahead_start; t <= newest; t++)
	{
		if (syNetInputTryPeekRemoteAnalogStickAtTick(player, t, out_frame) != FALSE)
		{
			return TRUE;
		}
	}
	if (max_lookback == 0U)
	{
		return FALSE;
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

#if defined(SSB64_NETMENU)
/*
 * Prefer a non-neutral that landed after last_confirmed over inventing 0,0. Never reinflate when
 * last_confirmed is a newer near-neutral (release) — last_nn.tick must be strictly after confirm.
 */
static sb32 syNetInputTryFillFromLastNonNeutral(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	const SYNetInputFrame *last_nn;
	const SYNetInputFrame *last_confirmed;
	s32 floor_mag;

	(void)tick;
	if ((out_frame == NULL) || (syNetInputCheckPlayer(player) == FALSE))
	{
		return FALSE;
	}
	last_nn = &sSYNetInputSlots[player].last_non_neutral;
	last_confirmed = &sSYNetInputSlots[player].last_confirmed;
	if ((last_nn->is_valid == FALSE) || (syNetInputFrameSticksNearNeutral(last_nn) != FALSE) ||
	    (syNetInputStickLooksAnalog(last_nn->stick_x, last_nn->stick_y) == FALSE))
	{
		return FALSE;
	}
	if ((last_confirmed->is_valid != FALSE) && (last_nn->tick <= last_confirmed->tick))
	{
		return FALSE;
	}
	floor_mag = (s32)syNetInputAnalogOnsetStickMag();
	syNetInputApplyAnalogOnsetStick(&out_frame->stick_x, &out_frame->stick_y, last_nn, floor_mag,
	                                SYNETINPUT_ANALOG_ONSET_STICK_MAG_MAX);
	out_frame->is_predicted = TRUE;
	out_frame->source = nSYNetInputSourceRemotePredicted;
	return TRUE;
}

static void syNetInputFillHoldLastSoftOnsetIfNeeded(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	SYNetInputFrame peek;
	s32 floor_mag;
	s8 was_x;
	s8 was_y;

	if ((out_frame == NULL) || (syNetInputCheckPlayer(player) == FALSE) || (tick == 0U))
	{
		return;
	}
	if (syNetInputFrameIsQuasiDigitalKeyboard(out_frame) != FALSE)
	{
		return;
	}
	/*
	 * Non-neutral hold-last (dual dash-dance / Turn allow):
	 * 1) Tick wire present — opposite intent, dash-gate XOR, or release → take wire.
	 * 2) Smash-class unless tick row is *strict*-confirmed same dash-gate — send-lead ahead
	 *    release/flip, else clamp |sx| below DASH_MIN. Same-intent *provisional* smash in the
	 *    ring must not skip the clamp (soak1 1272919275: stale provisional kept |sx|≥56).
	 * See docs/bugs/netplay_hold_last_dash_gate_send_lead_peer_2026-07-20.md and
	 * docs/bugs/netplay_wire_neutral_downgrade_dual_stick_onset_2026-07-20.md.
	 */
	if (syNetInputFrameSticksNearNeutral(out_frame) == FALSE)
	{
		sb32 smash_class;
		sb32 have_tick_row;
		sb32 tick_strict_same_gate;
		u32 peek_ahead;
		u32 t;
		u32 newest;
		const char *reason;

		smash_class = syNetInputStickDashGateActiveX(out_frame->stick_x);
		was_x = out_frame->stick_x;
		was_y = out_frame->stick_y;
		reason = NULL;
		have_tick_row = FALSE;
		tick_strict_same_gate = FALSE;

		have_tick_row = syNetInputTryPeekRemoteStickRowAtTick(player, tick, &peek);
		if (have_tick_row != FALSE)
		{
			if (syNetInputFrameSticksNearNeutral(&peek) != FALSE)
			{
				if (smash_class != FALSE)
				{
					out_frame->stick_x = peek.stick_x;
					out_frame->stick_y = peek.stick_y;
					reason = "smash_release";
				}
			}
			else if ((syNetInputStickLooksAnalog(peek.stick_x, peek.stick_y) != FALSE) &&
			         ((syNetInputStickSameAnalogIntent(was_x, was_y, peek.stick_x, peek.stick_y) == FALSE) ||
			          (syNetInputStickDashGateDisagreeX(was_x, peek.stick_x) != FALSE)))
			{
				out_frame->stick_x = peek.stick_x;
				out_frame->stick_y = peek.stick_y;
				reason = "smash_flip";
			}
			else if ((smash_class != FALSE) && (syNetInputFrameIsRemoteStrictConfirmed(&peek) != FALSE) &&
			         (syNetInputStickDashGateDisagreeX(was_x, peek.stick_x) == FALSE))
			{
				/* Strict wire agrees on dash gate — keep hold smash (authority). */
				tick_strict_same_gate = TRUE;
			}
		}
		if ((reason == NULL) && (smash_class != FALSE) && (tick_strict_same_gate == FALSE))
		{
			peek_ahead = syNetInputAnalogOnsetWirePeekAheadFrames();
			newest = tick + peek_ahead;
			for (t = tick + 1U; t <= newest; t++)
			{
				if (syNetInputTryPeekRemoteStickRowAtTick(player, t, &peek) == FALSE)
				{
					continue;
				}
				if (syNetInputFrameSticksNearNeutral(&peek) != FALSE)
				{
					out_frame->stick_x = 0;
					out_frame->stick_y = 0;
					reason = "smash_release_ahead";
					break;
				}
				if ((syNetInputStickLooksAnalog(peek.stick_x, peek.stick_y) != FALSE) &&
				    ((syNetInputStickSameAnalogIntent(was_x, was_y, peek.stick_x, peek.stick_y) ==
				      FALSE) ||
				     (syNetInputStickDashGateDisagreeX(was_x, peek.stick_x) != FALSE)))
				{
					out_frame->stick_x = peek.stick_x;
					out_frame->stick_y = peek.stick_y;
					reason = "smash_flip_ahead";
					break;
				}
			}
			if (reason == NULL)
			{
				syNetInputClampSmashHoldLastBelowDashGate(out_frame);
				if ((out_frame->stick_x != was_x) || (out_frame->stick_y != was_y))
				{
					reason = "smash_dash_clamp";
				}
			}
		}
		if (reason != NULL)
		{
			out_frame->is_predicted = TRUE;
			out_frame->source = nSYNetInputSourceRemotePredicted;
			if (sSYNetInputAnalogOnsetLogBudget > 0U)
			{
				port_log(
				    "SSB64 NetInput: hold_last_%s player=%d tick=%u was=(%d,%d) now=(%d,%d)\n",
				    reason,
				    (int)player,
				    (unsigned int)tick,
				    (int)was_x,
				    (int)was_y,
				    (int)out_frame->stick_x,
				    (int)out_frame->stick_y);
				sSYNetInputAnalogOnsetLogBudget--;
			}
		}
		else if ((smash_class != FALSE) && (tick_strict_same_gate != FALSE) &&
		         (sSYNetInputAnalogOnsetLogBudget > 0U))
		{
			/* Diag only — clamp skipped; tick wire is strict same dash-gate. */
			port_log(
			    "SSB64 NetInput: hold_last_keep_strict_same_gate player=%d tick=%u was=(%d,%d) now=(%d,%d)\n",
			    (int)player,
			    (unsigned int)tick,
			    (int)was_x,
			    (int)was_y,
			    (int)out_frame->stick_x,
			    (int)out_frame->stick_y);
			sSYNetInputAnalogOnsetLogBudget--;
		}
		return;
	}
	/*
	 * Current+ahead peek (max_lookback=0). Backward peek re-inflates the pre-release stick after a
	 * near-neutral wire confirm (soak1 seed 1645329949 @2316). See
	 * docs/bugs/netplay_hold_last_soft_onset_lookback_release_fc_2026-07-18.md.
	 * Soak 1156067044: also fall back to last_non_neutral when wire has not entered the peek window yet.
	 */
	if (syNetInputTryPeekRemoteAnalogForOnset(player, tick, 0U, &peek) != FALSE)
	{
		floor_mag = (s32)syNetInputAnalogOnsetStickMag();
		syNetInputApplyAnalogOnsetStick(&out_frame->stick_x, &out_frame->stick_y, &peek, floor_mag,
		                                SYNETINPUT_ANALOG_ONSET_STICK_MAG_MAX);
		out_frame->is_predicted = TRUE;
		out_frame->source = nSYNetInputSourceRemotePredicted;
		if (sSYNetInputAnalogOnsetLogBudget > 0U)
		{
			port_log(
			    "SSB64 NetInput: hold_last_soft_onset player=%d tick=%u sx=%d sy=%d (peek sx=%d sy=%d)\n",
			    (int)player,
			    (unsigned int)tick,
			    (int)out_frame->stick_x,
			    (int)out_frame->stick_y,
			    (int)peek.stick_x,
			    (int)peek.stick_y);
			sSYNetInputAnalogOnsetLogBudget--;
		}
		return;
	}
	if (syNetInputTryFillFromLastNonNeutral(player, tick, out_frame) != FALSE)
	{
		if (sSYNetInputAnalogOnsetLogBudget > 0U)
		{
			port_log(
			    "SSB64 NetInput: hold_last_soft_onset player=%d tick=%u sx=%d sy=%d (last_non_neutral)\n",
			    (int)player,
			    (unsigned int)tick,
			    (int)out_frame->stick_x,
			    (int)out_frame->stick_y);
			sSYNetInputAnalogOnsetLogBudget--;
		}
	}
}

sb32 syNetInputPostGoWirePacingGraceActive(u32 sim_tick)
{
	if ((sim_tick == 0U) || (sSYNetInputPostGoWirePacingGraceUntil == ~(u32)0))
	{
		return FALSE;
	}
	return (sim_tick <= sSYNetInputPostGoWirePacingGraceUntil) ? TRUE : FALSE;
}

static sb32 syNetInputSlotStickHotRecent(s32 player, u32 sim_tick, u32 lookback)
{
	const SYNetInputFrame *frame;
	u32 age;

	if ((syNetInputCheckPlayer(player) == FALSE) || (sim_tick == 0U))
	{
		return FALSE;
	}
	frame = &sSYNetInputSlots[player].last_published;
	if ((frame->is_valid != FALSE) && (frame->tick <= sim_tick) &&
	    (syNetInputFrameSticksNearNeutral(frame) == FALSE) &&
	    (syNetInputStickLooksAnalog(frame->stick_x, frame->stick_y) != FALSE))
	{
		age = sim_tick - frame->tick;
		if (age <= lookback)
		{
			return TRUE;
		}
	}
	frame = &sSYNetInputSlots[player].last_non_neutral;
	if ((frame->is_valid != FALSE) && (frame->tick <= sim_tick) &&
	    (syNetInputFrameSticksNearNeutral(frame) == FALSE) &&
	    (syNetInputStickLooksAnalog(frame->stick_x, frame->stick_y) != FALSE))
	{
		age = sim_tick - frame->tick;
		if (age <= lookback)
		{
			return TRUE;
		}
	}
	frame = &sSYNetInputSlots[player].last_confirmed;
	if ((frame->is_valid != FALSE) && (frame->tick <= sim_tick) &&
	    (syNetInputFrameSticksNearNeutral(frame) == FALSE) &&
	    (syNetInputStickLooksAnalog(frame->stick_x, frame->stick_y) != FALSE))
	{
		age = sim_tick - frame->tick;
		if (age <= lookback)
		{
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * Local stick hot + (Restrict or remote stick hot): dual-stick onset / spam class.
 * Used to harden zero-onset stall inside grace and shrink wire_need predict credit.
 */
sb32 syNetInputDualStickHotPredictTighten(u32 sim_tick)
{
	s32 local_slot;
	s32 i;
	s32 n;
	s32 slot;
	sb32 remote_hot;

	if ((sim_tick == 0U) || (syNetPeerIsVSSessionActive() == FALSE) ||
	    (syNetSessionParamsRollbackEnabled() == FALSE) || (syNetInputGetUseInputPrediction() == FALSE))
	{
		return FALSE;
	}
	if (syNetInputIntroWaitForceNeutralActive() != FALSE)
	{
		return FALSE;
	}
	if ((gSCManagerBattleState != NULL) && (gSCManagerBattleState->game_status == nSCBattleGameStatusWait))
	{
		return FALSE;
	}
	local_slot = syNetPeerGetLocalSimSlot();
	if (syNetInputSlotStickHotRecent(local_slot, sim_tick, 8U) == FALSE)
	{
		return FALSE;
	}
	if (syNetInputRemoteHumanZeroOnsetPredictRestrict(sim_tick) != FALSE)
	{
		return TRUE;
	}
	remote_hot = FALSE;
	n = syNetPeerGetRemoteHumanSlotCount();
	for (i = 0; i < n; i++)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(i, &slot) == FALSE)
		{
			continue;
		}
		if (syNetInputSlotStickHotRecent(slot, sim_tick, 8U) != FALSE)
		{
			remote_hot = TRUE;
			break;
		}
	}
	return remote_hot;
}

/*
 * Soak 1040202646 / 1809694209: v1 stalled on every predict past analog last_confirmed
 * (peek miss) → dual-stick hold + D lag felt like a hang. Narrow to a *true* ramp:
 * peek-ahead stick disagrees with last_confirmed. Peek miss / same-mag → FALSE so
 * hold_last may predict (rare GGPO on unseen ramps is OK).
 */
static sb32 syNetInputStickAnalogRampDisagree(s8 conf_x, s8 conf_y, s8 peek_x, s8 peek_y)
{
	if (syNetInputStickSealIntentDisagree(conf_x, conf_y, peek_x, peek_y) != FALSE)
	{
		return TRUE;
	}
	/* Same intent but large mag ramp (−45→−65). */
	if ((syNetInputAbsS8Diff(conf_x, peek_x) > (s32)SYNETINPUT_ANALOG_SAME_INTENT_TOLERANCE) ||
	    (syNetInputAbsS8Diff(conf_y, peek_y) > (s32)SYNETINPUT_ANALOG_SAME_INTENT_TOLERANCE))
	{
		return TRUE;
	}
	return FALSE;
}

sb32 syNetInputRemoteHumanAnalogRampPredictTighten(u32 sim_tick)
{
	s32 i;
	s32 n;
	s32 slot;
	SYNetInputFrame peek;
	const SYNetInputFrame *last_confirmed;
	sb32 hit;

	if ((sim_tick == 0U) || (syNetPeerIsVSSessionActive() == FALSE) ||
	    (syNetSessionParamsRollbackEnabled() == FALSE) || (syNetInputGetUseInputPrediction() == FALSE))
	{
		return FALSE;
	}
	if (syNetInputIntroWaitForceNeutralActive() != FALSE)
	{
		return FALSE;
	}
	if ((gSCManagerBattleState != NULL) && (gSCManagerBattleState->game_status == nSCBattleGameStatusWait))
	{
		return FALSE;
	}
	n = syNetPeerGetRemoteHumanSlotCount();
	hit = FALSE;
	slot = -1;
	last_confirmed = NULL;
	for (i = 0; i < n; i++)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(i, &slot) == FALSE)
		{
			continue;
		}
		if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(slot, sim_tick, NULL) != FALSE)
		{
			continue;
		}
		last_confirmed = &sSYNetInputSlots[slot].last_confirmed;
		if ((last_confirmed->is_valid == FALSE) || (last_confirmed->tick >= sim_tick) ||
		    (syNetInputFrameSticksNearNeutral(last_confirmed) != FALSE) ||
		    (syNetInputStickLooksAnalog(last_confirmed->stick_x, last_confirmed->stick_y) == FALSE))
		{
			continue;
		}
		/*
		 * True ramp only: peek-ahead differs from last_confirmed. Peek miss or
		 * same-mag hold → allow hold_last (do not R-lock every D-lag hold tick).
		 */
		/* Ahead-only peek (max_lookback=0); env peek_ahead still covers send-lead. */
		if (syNetInputTryPeekRemoteAnalogForOnset(slot, sim_tick, 0U, &peek) == FALSE)
		{
			continue;
		}
		if (syNetInputStickAnalogRampDisagree(last_confirmed->stick_x, last_confirmed->stick_y,
		                                      peek.stick_x, peek.stick_y) == FALSE)
		{
			continue;
		}
		hit = TRUE;
		break;
	}
	if (hit != FALSE)
	{
		static u32 sAnalogRampTightenLogTick = ~(u32)0;
		u32 hr;
		sb32 in_grace;

		hr = syNetPeerGetHighestRemoteTick();
		in_grace = syNetInputPostGoWirePacingGraceActive(sim_tick);
		if (sim_tick != sAnalogRampTightenLogTick)
		{
			port_log(
			    "SSB64 NetInput: ANALOG_RAMP_PREDICT phase=tighten player=%d tick=%u grace=%d "
			    "grace_until=%u hr=%u frontier_sim=%u D=%u last_conf_tick=%u "
			    "last_conf_sx=%d last_conf_sy=%d peek_sx=%d peek_sy=%d\n",
			    (int)slot, (unsigned int)sim_tick, (int)in_grace,
			    (unsigned int)sSYNetInputPostGoWirePacingGraceUntil, (unsigned int)hr,
			    (unsigned int)((hr != 0U) ? syNetPeerDelaySimTickFromWire(hr) : 0U),
			    (unsigned int)syNetPeerGetCommittedInputDelay(),
			    (unsigned int)((last_confirmed != NULL && last_confirmed->is_valid != FALSE)
			                       ? last_confirmed->tick
			                       : 0U),
			    (int)((last_confirmed != NULL && last_confirmed->is_valid != FALSE)
			              ? last_confirmed->stick_x
			              : 0),
			    (int)((last_confirmed != NULL && last_confirmed->is_valid != FALSE)
			              ? last_confirmed->stick_y
			              : 0),
			    (int)peek.stick_x, (int)peek.stick_y);
			sAnalogRampTightenLogTick = sim_tick;
		}
		return TRUE;
	}
	return FALSE;
}

/*
 * Soak1 871504438 / 979771282 / 250667155: inventing remote (0,0) for a full phase_lock
 * window while the owner already applied local stick seeds Wait vs Walk/Turn → PEER.
 * TRUE when any remote-human slot would invent hard zero onset (no strict wire, no
 * soft-onset peek, no usable last_nn). Active during post-Go grace. Off only during
 * intro Wait / Appear force-neutral. Consumers hard-stall (post-grace or dual-hot) or
 * cap to D+1 (grace-only).
 */
sb32 syNetInputRemoteHumanZeroOnsetPredictRestrict(u32 sim_tick)
{
	s32 i;
	s32 n;
	s32 slot;
	SYNetInputFrame peek;
	const SYNetInputFrame *last_confirmed;
	const SYNetInputFrame *last_nn;
	sb32 hit;

	if ((sim_tick == 0U) || (syNetPeerIsVSSessionActive() == FALSE) ||
	    (syNetSessionParamsRollbackEnabled() == FALSE) || (syNetInputGetUseInputPrediction() == FALSE))
	{
		return FALSE;
	}
	if (syNetInputIntroWaitForceNeutralActive() != FALSE)
	{
		return FALSE;
	}
	if ((gSCManagerBattleState != NULL) && (gSCManagerBattleState->game_status == nSCBattleGameStatusWait))
	{
		return FALSE;
	}
	/*
	 * Do NOT early-out on post-Go grace. grace until=423 covered soak 979771282 onset@419;
	 * wire_need stays soft, but zero-onset D+1 must still bind admit/advance.
	 */
	n = syNetPeerGetRemoteHumanSlotCount();
	hit = FALSE;
	slot = -1;
	last_confirmed = NULL;
	for (i = 0; i < n; i++)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(i, &slot) == FALSE)
		{
			continue;
		}
		if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(slot, sim_tick, NULL) != FALSE)
		{
			continue;
		}
		last_confirmed = &sSYNetInputSlots[slot].last_confirmed;
		if ((last_confirmed->is_valid != FALSE) &&
		    (syNetInputFrameSticksNearNeutral(last_confirmed) == FALSE) &&
		    (syNetInputStickLooksAnalog(last_confirmed->stick_x, last_confirmed->stick_y) != FALSE))
		{
			continue;
		}
		if (syNetInputTryPeekRemoteAnalogForOnset(slot, sim_tick, 0U, &peek) != FALSE)
		{
			continue;
		}
		last_nn = &sSYNetInputSlots[slot].last_non_neutral;
		if ((last_nn->is_valid != FALSE) && (syNetInputFrameSticksNearNeutral(last_nn) == FALSE) &&
		    (syNetInputStickLooksAnalog(last_nn->stick_x, last_nn->stick_y) != FALSE) &&
		    ((last_confirmed->is_valid == FALSE) || (last_nn->tick > last_confirmed->tick)))
		{
			continue;
		}
		hit = TRUE;
		break;
	}
	if (hit != FALSE)
	{
		static u32 sZeroOnsetRestrictLogTick = ~(u32)0;
		u32 hr;
		sb32 in_grace;

		hr = syNetPeerGetHighestRemoteTick();
		in_grace = ((sSYNetInputPostGoWirePacingGraceUntil != ~(u32)0) &&
		            (sim_tick <= sSYNetInputPostGoWirePacingGraceUntil))
		               ? TRUE
		               : FALSE;
		if (sim_tick != sZeroOnsetRestrictLogTick)
		{
			port_log(
			    "SSB64 NetInput: ZERO_ONSET_PREDICT phase=restrict player=%d tick=%u restrict=1 "
			    "grace=%d grace_until=%u hr=%u frontier_sim=%u D=%u pred_win=%u "
			    "last_conf_tick=%u last_conf_sx=%d last_conf_sy=%d\n",
			    (int)slot, (unsigned int)sim_tick, (int)in_grace,
			    (unsigned int)sSYNetInputPostGoWirePacingGraceUntil, (unsigned int)hr,
			    (unsigned int)((hr != 0U) ? syNetPeerDelaySimTickFromWire(hr) : 0U),
			    (unsigned int)syNetPeerGetCommittedInputDelay(),
			    (unsigned int)syNetPeerGetPhaseLockPredictionWindowTicks(),
			    (unsigned int)((last_confirmed != NULL && last_confirmed->is_valid != FALSE)
			                       ? last_confirmed->tick
			                       : 0U),
			    (int)((last_confirmed != NULL && last_confirmed->is_valid != FALSE)
			              ? last_confirmed->stick_x
			              : 0),
			    (int)((last_confirmed != NULL && last_confirmed->is_valid != FALSE)
			              ? last_confirmed->stick_y
			              : 0));
			sZeroOnsetRestrictLogTick = sim_tick;
		}
		return TRUE;
	}
	return FALSE;
}
#endif

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
 *
 * Release (published analog → wire nearer/at neutral) must never defer — that is the opposite of
 * onset-ahead and silent Promote forks pose (soak 2132381039: 24× REPLACE, 0× GGPO after ep4).
 */
static sb32 syNetInputStickReplaceIsRelease(const SYNetInputFrame *old_frame, const SYNetInputFrame *wire)
{
	u32 confirmed_db;
	s32 old_mag;
	s32 wire_mag;

	if ((old_frame == NULL) || (wire == NULL))
	{
		return FALSE;
	}
	confirmed_db = syNetInputGgpoStickDeadband();
	if (syNetInputFrameSticksNearNeutralWithDeadband(old_frame, confirmed_db) != FALSE)
	{
		return FALSE;
	}
	if (syNetInputFrameSticksNearNeutralWithDeadband(wire, confirmed_db) != FALSE)
	{
		return TRUE;
	}
	old_mag = syNetInputAbsS8Diff(old_frame->stick_x, 0);
	if (syNetInputAbsS8Diff(old_frame->stick_y, 0) > old_mag)
	{
		old_mag = syNetInputAbsS8Diff(old_frame->stick_y, 0);
	}
	wire_mag = syNetInputAbsS8Diff(wire->stick_x, 0);
	if (syNetInputAbsS8Diff(wire->stick_y, 0) > wire_mag)
	{
		wire_mag = syNetInputAbsS8Diff(wire->stick_y, 0);
	}
	/* Clearly shedding magnitude (not a same-intent ramp up). */
	return ((wire_mag + (s32)confirmed_db) < old_mag) ? TRUE : FALSE;
}

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
	/*
	 * Already-simulated ticks must rewind on significant wire replace. Feel-0 marks a
	 * superseded provisional publish predicted then RequestInputCorrection; without this
	 * gate, analog→neutral release hits the onset-ahead defer and Promote silently forks
	 * (same class as post-jump stick REPLACE storms after debounce).
	 */
	if (syNetInputGetTick() > sim_tick)
	{
		return FALSE;
	}
	/* Release is not onset-ahead — never defer (see StickReplaceIsRelease). */
	if (syNetInputStickReplaceIsRelease(published, remote) != FALSE)
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

#if defined(PORT) && defined(SSB64_NETMENU)
sb32 syNetInputStickReplaceNeedsRewind(s32 player, u32 sim_tick, const SYNetInputFrame *old_frame,
                                       const SYNetInputFrame *wire, const SYNetInputFrame *defer_published)
{
	const SYNetInputFrame *pub;

	if ((old_frame == NULL) || (wire == NULL) || (sim_tick == 0U))
	{
		return FALSE;
	}
	if (syNetInputFrameGameplayEquals(old_frame, wire) != FALSE)
	{
		return FALSE;
	}
	/*
	 * Jibaku/bound: launch velocity is locked at entry; stick REPLACE cannot change the
	 * trajectory but still queues GGPO under ness_pk_defer until the status exits — soak
	 * 11903082 @661 (sy 4→15) grew target to 679 (span 18) then PEER_SNAPSHOT figh @691.
	 * Buttons/release still rewind. Hold/Start keep stick rewind (aim). Promote writes wire.
	 * See docs/bugs/netplay_ness_jibaku_stick_ggpo_storm_eff_load_2026-07-17.md.
	 */
	if ((old_frame->buttons == wire->buttons) &&
	    (syNetInputStickReplaceIsRelease(old_frame, wire) == FALSE) &&
	    (syNetplayNessPlayerInJibakuStickAbsorbScope(player) != FALSE))
	{
		if (sSYNetInputGgpoSkipMicroLogsRemaining > 0U)
		{
			port_log(
			    "SSB64 NetInput: GGPO stick replace skipped class=jibaku_stick player=%d sim_tick=%u "
			    "old sx=%d sy=%d | wire sx=%d sy=%d\n",
			    (int)player,
			    (unsigned int)sim_tick,
			    (int)old_frame->stick_x,
			    (int)old_frame->stick_y,
			    (int)wire->stick_x,
			    (int)wire->stick_y);
			sSYNetInputGgpoSkipMicroLogsRemaining--;
		}
		return FALSE;
	}
	/*
	 * Dead*: stick REPLACE cannot change hashed fighter/map sim but still opened a
	 * span-5 resim that burned gameplay LCG on one peer only (soak 1790844706 @3820 →
	 * matched Open→Blow onset, wind_dur 276 vs 290). Promote wire; buttons/release rewind.
	 * RebirthWait is_ghost is intentionally NOT in scope (soak 1174892281 leave stick).
	 * See docs/bugs/netplay_dead_stick_ggpo_resim_rng_whispy_blow_2026-07-20.md.
	 */
	if ((old_frame->buttons == wire->buttons) &&
	    (syNetInputStickReplaceIsRelease(old_frame, wire) == FALSE) &&
	    (syNetplayPlayerInDeadGhostStickAbsorbScope(player) != FALSE))
	{
		/* Always log Dead* absorbs — rate-limit hid rebirth false positives previously. */
		port_log(
		    "SSB64 NetInput: GGPO stick replace skipped class=dead_ghost_stick player=%d "
		    "sim_tick=%u old sx=%d sy=%d | wire sx=%d sy=%d\n",
		    (int)player,
		    (unsigned int)sim_tick,
		    (int)old_frame->stick_x,
		    (int)old_frame->stick_y,
		    (int)wire->stick_x,
		    (int)wire->stick_y);
		return FALSE;
	}
	/*
	 * Already-simulated tick: release / buttons / onset / non-micro stick still rewind.
	 * Same-intent ±micro deadband (default 3) Promote without episode — soak 1490370675
	 * paid span-2 GGPO for 70/60→71/59 class noise. Feel-0 release (13,4→0,0) stays covered
	 * by StickReplaceIsRelease. See docs/bugs/netplay_feel0_release_deadband_skips_ggpo_2026-07-12.md
	 * and docs/bugs/netplay_input_contract_micro_deadband_onset_peek_2026-07-17.md.
	 */
	if (syNetInputGetTick() > sim_tick)
	{
		u32 micro_db;
		s32 dx;
		s32 dy;

		if (old_frame->buttons != wire->buttons)
		{
			return TRUE;
		}
		if (syNetInputStickReplaceIsRelease(old_frame, wire) != FALSE)
		{
			return TRUE;
		}
		micro_db = syNetInputGgpoStickCompletedSimMicroDeadband();
		dx = syNetInputAbsS8Diff(old_frame->stick_x, wire->stick_x);
		dy = syNetInputAbsS8Diff(old_frame->stick_y, wire->stick_y);
		/*
		 * Never micro-skip when Turn dash gate would disagree (|sx| crosses 56 / smash sign
		 * flip) — soak1 179193526 class false did_dash. Same-intent ±3 still absorbs noise.
		 */
		if ((micro_db > 0U) && (dx <= (s32)micro_db) && (dy <= (s32)micro_db) &&
		    (syNetInputStickLooksAnalog(old_frame->stick_x, old_frame->stick_y) != FALSE) &&
		    (syNetInputStickLooksAnalog(wire->stick_x, wire->stick_y) != FALSE) &&
		    (syNetInputStickSameAnalogIntent(old_frame->stick_x, old_frame->stick_y, wire->stick_x,
		                                     wire->stick_y) != FALSE) &&
		    (syNetInputStickDashGateDisagreeX(old_frame->stick_x, wire->stick_x) == FALSE))
		{
			sSYNetInputGgpoClassSkippedMicro++;
			if (sSYNetInputGgpoSkipMicroLogsRemaining > 0U)
			{
				port_log(
				    "SSB64 NetInput: GGPO stick replace skipped class=micro_stick player=%d sim_tick=%u "
				    "old sx=%d sy=%d | wire sx=%d sy=%d micro_db=%u\n",
				    (int)player,
				    (unsigned int)sim_tick,
				    (int)old_frame->stick_x,
				    (int)old_frame->stick_y,
				    (int)wire->stick_x,
				    (int)wire->stick_y,
				    (unsigned int)micro_db);
				sSYNetInputGgpoSkipMicroLogsRemaining--;
			}
			return FALSE;
		}
		/* Any remaining stick/button gameplay delta on completed sim still rewinds. */
		return TRUE;
	}
	/*
	 * Analog→neutral / shedding magnitude: never treat as onset-ahead defer, always rewind.
	 * See docs/bugs/netplay_stick_ramp_predict_deadband_silent_promote_2026-07-12.md.
	 */
	if (syNetInputStickReplaceIsRelease(old_frame, wire) != FALSE)
	{
		return TRUE;
	}
	pub = (defer_published != NULL) ? defer_published : old_frame;
	if (syNetInputShouldDeferPredictedAnalogCorrection(player, sim_tick, pub, wire) != FALSE)
	{
		return FALSE;
	}
	/*
	 * Runway / feel-0 REPLACE: use confirmed deadband (default 12), not predict (14).
	 * Continuous stick ramps (Δ≈5–12) were "insignificant" under predict and Promote'd
	 * without GGPO → p1 topn phase lag → FC inputs+figh diverge.
	 */
	return syNetInputGameplayCorrectionIsSignificantEx(old_frame, wire, FALSE);
}

const char *syNetInputClassifyGgpoCorrection(const SYNetInputFrame *old_frame, const SYNetInputFrame *wire)
{
	u32 micro_db;
	s32 dx;
	s32 dy;

	if ((old_frame == NULL) || (wire == NULL))
	{
		return "unknown";
	}
	if (old_frame->buttons != wire->buttons)
	{
		return "button";
	}
	if (syNetInputStickReplaceIsRelease(old_frame, wire) != FALSE)
	{
		return "release";
	}
	if ((syNetInputFrameSticksNearNeutral(old_frame) != FALSE) &&
	    (syNetInputFrameSticksNearNeutral(wire) == FALSE))
	{
		return "onset_from_zero";
	}
	micro_db = syNetInputGgpoStickCompletedSimMicroDeadband();
	dx = syNetInputAbsS8Diff(old_frame->stick_x, wire->stick_x);
	dy = syNetInputAbsS8Diff(old_frame->stick_y, wire->stick_y);
	if ((micro_db > 0U) && (dx <= (s32)micro_db) && (dy <= (s32)micro_db) &&
	    (syNetInputStickLooksAnalog(old_frame->stick_x, old_frame->stick_y) != FALSE) &&
	    (syNetInputStickLooksAnalog(wire->stick_x, wire->stick_y) != FALSE) &&
	    (syNetInputStickSameAnalogIntent(old_frame->stick_x, old_frame->stick_y, wire->stick_x, wire->stick_y) !=
	     FALSE))
	{
		return "micro_stick";
	}
	return "real_stick";
}

void syNetInputNoteGgpoCorrectionQueued(const SYNetInputFrame *old_frame, const SYNetInputFrame *wire)
{
	const char *cls;

	cls = syNetInputClassifyGgpoCorrection(old_frame, wire);
	if (strcmp(cls, "button") == 0)
	{
		sSYNetInputGgpoClassQueuedButton++;
	}
	else if (strcmp(cls, "onset_from_zero") == 0)
	{
		sSYNetInputGgpoClassQueuedOnset++;
	}
	else if (strcmp(cls, "release") == 0)
	{
		sSYNetInputGgpoClassQueuedRelease++;
	}
	else if (strcmp(cls, "micro_stick") == 0)
	{
		sSYNetInputGgpoClassQueuedMicro++;
	}
	else
	{
		sSYNetInputGgpoClassQueuedRealStick++;
	}
}

void syNetInputLogGgpoCorrectionClassSummary(void)
{
	port_log(
	    "SSB64 NetInput: GGPO_CLASS_SUMMARY queued button=%u onset_from_zero=%u release=%u real_stick=%u "
	    "micro_stick=%u | skipped_micro=%u\n",
	    (unsigned int)sSYNetInputGgpoClassQueuedButton,
	    (unsigned int)sSYNetInputGgpoClassQueuedOnset,
	    (unsigned int)sSYNetInputGgpoClassQueuedRelease,
	    (unsigned int)sSYNetInputGgpoClassQueuedRealStick,
	    (unsigned int)sSYNetInputGgpoClassQueuedMicro,
	    (unsigned int)sSYNetInputGgpoClassSkippedMicro);
}

static void syNetInputMarkPublishedPredictedForStickReplace(s32 player, SYNetInputFrame *published)
{
	if ((published == NULL) || (syNetInputCheckPlayer(player) == FALSE))
	{
		return;
	}
	published->is_predicted = TRUE;
	if (published->source == nSYNetInputSourceRemoteConfirmed)
	{
		published->source = nSYNetInputSourceRemotePredicted;
	}
	SYNETINPUT_STRICT_TAG("mark_predicted_replace");
	syNetInputStoreFrame(sSYNetInputHistory, player, published);
}
#endif

/* Strict wire authority: real INPUT packets only (not hold-last gap fill). */
static sb32 syNetInputFrameIsRemoteStrictConfirmed(const SYNetInputFrame *frame)
{
	return ((frame != NULL) && (frame->is_valid != FALSE) &&
	        (frame->source == nSYNetInputSourceRemoteConfirmed) && (frame->is_predicted == FALSE))
	           ? TRUE
	           : FALSE;
}

#if defined(PORT) && defined(SSB64_NETMENU)
/*
 * Sealed-episode rows outrank write-once. The seal-rows exchange is the definitive cross-peer
 * input agreement (each peer contributes its own local-authority rows), so a store whose
 * gameplay matches the sealed row for that sim tick is the mechanical reconcile path — never
 * block it. Soak (seed 3284691918, kill @596): publish_frame/store_published_api write-once
 * swapped Android's sealed -74 back to a stale provisional-confirmed -43 mid-resim → figh fork.
 */
static sb32 syNetInputFrameMatchesSealedEpisodeRow(s32 player, u32 sim_tick, const SYNetInputFrame *incoming)
{
	SYNetInputFrame sealed;

	if ((incoming == NULL) || (sim_tick == 0U))
	{
		return FALSE;
	}
	if (syNetRollbackEpisodeGetSealedFrame(player, sim_tick, &sealed) == FALSE)
	{
		return FALSE;
	}
	return syNetInputFrameGameplayEquals(&sealed, incoming);
}

/*
 * Refuse mutating an already-confirmed published remote row with different gameplay.
 * Compare sticks/buttons only — callers pass published[sim_tick] vs wire/resolved whose
 * `.tick` may be a wire tick (patch_publish); requiring tick equality let patch bypass
 * write-once (soak 1468769950). Sealed-episode rows always pass (mechanical reconcile).
 * resim_wire* / mark_predicted_replace still StoreFrame directly.
 * See docs/bugs/netplay_confirmed_publish_write_once_2026-07-12.md.
 */
static sb32 syNetInputRemoteConfirmedWriteOnceBlocks(s32 player, const SYNetInputFrame *existing,
                                                     const SYNetInputFrame *incoming, const char *reason)
{
	if ((existing == NULL) || (incoming == NULL) || (syNetInputIsRemoteHumanSlot(player) == FALSE))
	{
		return FALSE;
	}
	if (syNetInputFrameIsRemoteStrictConfirmed(existing) == FALSE)
	{
		return FALSE;
	}
	if (syNetInputFrameGameplayEquals(existing, incoming) != FALSE)
	{
		return FALSE;
	}
	if (syNetInputFrameMatchesSealedEpisodeRow(player, existing->tick, incoming) != FALSE)
	{
		/*
		 * soak1 1857971875: seal packed opposite smash P0@419 (73,49) over confirmed
		 * (-75,-41); INTENT_OVERRIDE kept ledger during resim then SEAL_OVERRIDE stomped
		 * publish back to the poison → PHYSICS_FORK@420 + RESIM_STICK_FORK storm. Refuse
		 * seal stomp when confirmed and sealed disagree on dash-gate / analog intent.
		 */
		if (syNetInputStickSealIntentDisagree(existing->stick_x, existing->stick_y, incoming->stick_x,
		                                      incoming->stick_y) != FALSE)
		{
			port_log(
			    "SSB64 NetInput: REMOTE_PUBLISH_SEAL_OVERRIDE_REFUSE_INTENT player=%d sim_tick=%u "
			    "writer=%s keep btn=0x%04X sx=%d sy=%d | sealed btn=0x%04X sx=%d sy=%d\n",
			    (int)player, (unsigned int)existing->tick, (reason != NULL) ? reason : "?",
			    (unsigned int)existing->buttons, (int)existing->stick_x, (int)existing->stick_y,
			    (unsigned int)incoming->buttons, (int)incoming->stick_x, (int)incoming->stick_y);
			return TRUE;
		}
		if (syNetInputAuthorityPublishLogEnabled() != FALSE)
		{
			port_log(
			    "SSB64 NetInput: REMOTE_PUBLISH_SEAL_OVERRIDE player=%d sim_tick=%u writer=%s "
			    "old btn=0x%04X sx=%d sy=%d | sealed btn=0x%04X sx=%d sy=%d\n",
			    (int)player, (unsigned int)existing->tick, (reason != NULL) ? reason : "?",
			    (unsigned int)existing->buttons, (int)existing->stick_x, (int)existing->stick_y,
			    (unsigned int)incoming->buttons, (int)incoming->stick_x, (int)incoming->stick_y);
		}
		return FALSE;
	}
	if (syNetInputAuthorityPublishLogEnabled() != FALSE)
	{
		port_log(
		    "SSB64 NetInput: REMOTE_PUBLISH_SKIP player=%d sim_tick=%u reason=confirmed_write_once(%s) "
		    "old btn=0x%04X sx=%d sy=%d | new btn=0x%04X sx=%d sy=%d\n",
		    (int)player,
		    (unsigned int)existing->tick,
		    (reason != NULL) ? reason : "?",
		    (unsigned int)existing->buttons,
		    (int)existing->stick_x,
		    (int)existing->stick_y,
		    (unsigned int)incoming->buttons,
		    (int)incoming->stick_x,
		    (int)incoming->stick_y);
	}
	return TRUE;
}

/* Write-once blocked a confirmed publish mutate: force mechanical rewind instead of silent drift. */
static void syNetInputRemoteConfirmedWriteOnceQueueCorrection(s32 player, u32 sim_tick,
                                                              SYNetInputFrame *published_mut)
{
	if ((sim_tick == 0U) || (published_mut == NULL) || (syNetRollbackIsActive() == FALSE))
	{
		return;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	syNetInputMarkPublishedPredictedForStickReplace(player, published_mut);
	syNetRollbackQueueOrWidenStickCorrection(player, sim_tick);
}
#endif

/*
 * Wire-ring presence including provisional gap-fill (hold-last / auth-frontier runway).
 * Used for hold-last resolve and resim reconcile fallbacks — NOT for shared-commit ring_ready.
 * Gap-fill still raises `hr` via StoreFrame; counting it as ring_ready forced the confirmed
 * path while WireReady required strict → R-stall instead of phase_lock prediction.
 * See docs/bugs/netplay_provisional_ring_ready_blocks_predict_2026-07-12.md.
 */
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

/* Read-only transmitted (wire-locked) row for `tick` — egress append-only guard. */
sb32 syNetInputTryGetTransmittedSimFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	if ((syNetInputCheckPlayer(player) == FALSE) || (tick == 0U))
	{
		return FALSE;
	}
	return syNetInputGetStoredFrame(sSYNetInputTransmittedHistory, player, tick, out_frame);
}

void syNetInputNoteTransmittedSimFrame(s32 player, const SYNetInputFrame *frame)
{
	SYNetInputFrame store;
	SYNetInputFrame prior;
	sb32 had_prior;
#if defined(SSB64_NETMENU)
	SYNetInputFrame gameplay;
	const SYNetInputFrame *authority;
#endif

	if ((frame == NULL) || (syNetInputCheckPlayer(player) == FALSE) || (frame->tick == 0U))
	{
		return;
	}
#if defined(SSB64_NETMENU)
	authority = frame;
	if (syNetInputIsLocalDelaySlot(player) != FALSE)
	{
		if (syNetInputGetLocalGameplayFrame(player, frame->tick, &gameplay) == FALSE)
		{
			/* Send-before-sample: delay[sim] may still be hold-last provisional — do not lock. */
			return;
		}
		if (syNetInputFrameGameplayEquals(&gameplay, frame) == FALSE)
		{
			authority = &gameplay;
		}
	}
	had_prior = syNetInputGetStoredFrame(sSYNetInputTransmittedHistory, player, authority->tick, &prior);
	store = *authority;
#else
	had_prior = syNetInputGetStoredFrame(sSYNetInputTransmittedHistory, player, frame->tick, &prior);
	store = *frame;
#endif
	syNetInputStoreFrame(sSYNetInputTransmittedHistory, player, &store);
	if ((syNetInputAuthoritativeWireContractEnabled() != FALSE) && (syNetInputIsLocalDelaySlot(player) != FALSE))
	{
		syNetInputPromoteLocalAuthorityPublished(player, store.tick);
		if ((had_prior != FALSE) && (syNetInputFrameGameplayEquals(&prior, &store) == FALSE))
		{
			syNetRollbackNotifyLocalAuthorityTransmitRevision(player, store.tick);
		}
		return;
	}
	if ((had_prior != FALSE) && (syNetInputFrameGameplayEquals(&prior, &store) == FALSE))
	{
		SYNetInputFrame patch;

		patch = store;
		patch.tick = store.tick;
		patch.source = nSYNetInputSourceLocal;
		patch.is_predicted = FALSE;
		syNetInputStoreFrame(sSYNetInputHistory, player, &patch);
		syNetInputStrictReadyCacheInvalidate();
		syNetRollbackNotifyLocalAuthorityTransmitRevision(player, store.tick);
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
#if defined(PORT) && defined(SSB64_NETMENU)
	/*
	 * Phase 2: patch no longer invents confirmed published from the wire row. Dual-write ledger
	 * (wire already committed in CommitRemoteConfirmedWire) then refresh published from ledger.
	 */
	if (syNetInputIsRemoteHumanSlot(player) != FALSE)
	{
		SYNetInputFrame ledger_wire;

		ledger_wire = *confirmed;
		ledger_wire.tick = sim_tick;
		syNetInputAuthorityLedgerCommitWire(player, sim_tick, &ledger_wire);
		if (syNetInputRefreshPublishedFromAuthorityLedger(player, sim_tick,
		                                                  (reason != NULL) ? reason : "patch_publish") != FALSE)
		{
			return;
		}
	}
#endif
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
#if defined(PORT) && defined(SSB64_NETMENU)
	if ((published.is_valid != FALSE) &&
	    (syNetInputRemoteConfirmedWriteOnceBlocks(player, &published, confirmed, "patch_publish") != FALSE))
	{
		/* Gameplay differs: do not Store; mark predicted + GGPO (sim_tick keyed, not wire tick). */
		syNetInputRemoteConfirmedWriteOnceQueueCorrection(player, sim_tick, &published);
		return;
	}
	/* Non-ledger invent of confirmed published is refused (Phase 2). */
	if (syNetInputIsRemoteHumanSlot(player) != FALSE)
	{
		return;
	}
#endif
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
	SYNETINPUT_STRICT_TAG("patch_publish");
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
	SYNETINPUT_STRICT_TAG("wire_commit");
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
#if defined(PORT) && defined(SSB64_NETMENU)
	/*
	 * Dual-write authority ledger (sim-tick keyed). Provisional path never reaches here.
	 * Seal origin already present with different gameplay is preserved (store helper).
	 */
	if (sim_tick != 0U)
	{
		SYNetInputFrame ledger_wire;

		ledger_wire = *frame;
		ledger_wire.tick = sim_tick;
		syNetInputAuthorityLedgerCommitWire(player, sim_tick, &ledger_wire);
	}
#endif
	{
		SYNetInputFrame published;
		SYNetInputFrame wire_view;
		sb32 had_published;
		sb32 queued_predicted_correction;

		wire_view = *frame;
		wire_view.tick = sim_tick;
		had_published = (sim_tick != 0U) ? syNetInputGetHistoryFrame(player, sim_tick, &published) : FALSE;
		queued_predicted_correction = FALSE;
#if defined(PORT) && defined(SSB64_NETMENU)
		/*
		 * Stick REPLACE policy (syNetInputStickReplaceNeedsRewind + QueueOrWiden):
		 * Feel-0 provisional / gap-fill priors stamped RemoteConfirmed need mark-predicted + GGPO
		 * before Promote. Completed-sim: buttons/release/non-micro stick; same-intent ±micro
		 * Promote-only; release never deferred; runway uses confirmed deadband (not predict-14).
		 * See docs/bugs/netplay_stick_ramp_predict_deadband_silent_promote_2026-07-12.md
		 * and docs/bugs/netplay_input_contract_micro_deadband_onset_peek_2026-07-17.md.
		 */
		if ((sim_tick != 0U) && (had_published != FALSE) && (had_prior_ring != FALSE) && (prior_ring != NULL) &&
		    (syNetInputIsRemoteHumanSlot(player) != FALSE) &&
		    ((syNetInputFrameIsRemoteStrictConfirmed(prior_ring) != FALSE) ||
		     (syNetInputFrameIsRemoteGapFilled(prior_ring) != FALSE)) &&
		    (syNetRollbackShouldQueueGgpoCorrection(sim_tick) != FALSE) &&
		    (syNetInputStickReplaceNeedsRewind(player, sim_tick, prior_ring, &wire_view, &published) != FALSE))
		{
			syNetInputMarkPublishedPredictedForStickReplace(player, &published);
			syNetInputLogInputGameplayRow("defer_analog_correction", player, sim_tick, wire_tick,
			                              (syNetInputFrameIsRemoteGapFilled(prior_ring) != FALSE)
			                                  ? "feel0_gapfill_replace"
			                                  : ((syNetInputGetTick() > sim_tick)
			                                         ? "feel0_completed_sim_replace"
			                                         : "feel0_provisional_replace"),
			                              &published, &wire_view);
			syNetRollbackQueueOrWidenStickCorrection(player, sim_tick);
			queued_predicted_correction = TRUE;
			had_published = TRUE;
		}
		/*
		 * Late wire for an already-completed sim tick when the prior ring was not strict/gap-fill
		 * (or feel0 path missed). Promote alone rewrites history without rewind — force GGPO.
		 */
		if ((queued_predicted_correction == FALSE) && (sim_tick != 0U) && (had_published != FALSE) &&
		    (syNetInputIsRemoteHumanSlot(player) != FALSE) && (syNetInputGetTick() > sim_tick) &&
		    (syNetRollbackShouldQueueGgpoCorrection(sim_tick) != FALSE) &&
		    (syNetInputStickReplaceNeedsRewind(player, sim_tick, &published, &wire_view, &published) != FALSE))
		{
			syNetInputMarkPublishedPredictedForStickReplace(player, &published);
			syNetInputLogInputGameplayRow("defer_analog_correction", player, sim_tick, wire_tick,
			                              "late_wire_completed_sim", &published, &wire_view);
			syNetRollbackQueueOrWidenStickCorrection(player, sim_tick);
			queued_predicted_correction = TRUE;
			had_published = TRUE;
		}
#endif
		/*
		 * Queue GGPO correction *before* Promote overwrites published history with confirmed wire.
		 * Otherwise predicted hold-last (neutral) vs stick onset becomes published==wire and we return
		 * without rollback — FC later reanchors from asymmetric local predicted-onset flags (Wait vs Dash).
		 */
		if ((sim_tick != 0U) && (had_published != FALSE) && (syNetInputIsRemoteHumanSlot(player) != FALSE) &&
		    ((published.is_predicted != FALSE) || (published.source == nSYNetInputSourceRemotePredicted)) &&
		    (syNetRollbackShouldQueueGgpoCorrection(sim_tick) != FALSE) &&
#if defined(PORT) && defined(SSB64_NETMENU)
		    (syNetInputStickReplaceNeedsRewind(player, sim_tick, &published, &wire_view, &published) != FALSE)
#else
		    (syNetInputFrameGameplayEquals(&published, &wire_view) == FALSE) &&
		    (syNetInputShouldDeferPredictedAnalogCorrection(player, sim_tick, &published, &wire_view) == FALSE) &&
		    (syNetInputGameplayCorrectionIsSignificantEx(&published, &wire_view, TRUE) != FALSE)
#endif
		    )
		{
			if (queued_predicted_correction == FALSE)
			{
				syNetInputLogInputGameplayRow("defer_analog_correction", player, sim_tick, wire_tick,
				                              "pre_promote_ggpo", &published, &wire_view);
#if defined(PORT) && defined(SSB64_NETMENU)
				syNetRollbackQueueOrWidenStickCorrection(player, sim_tick);
#else
				syNetRollbackRequestInputCorrection(player, sim_tick);
#endif
			}
			queued_predicted_correction = TRUE;
		}
		if ((sim_tick != 0U) && (syNetInputIsRemoteHumanSlot(player) != FALSE) &&
		    (syNetInputAuthoritativeWireContractEnabled() != FALSE))
		{
			syNetInputPromoteRemoteHumanAuthorityPublished(player, sim_tick);
		}
		had_published = (sim_tick != 0U) ? syNetInputGetHistoryFrame(player, sim_tick, &published) : FALSE;
		if ((had_published != FALSE) && (syNetInputFrameGameplayEquals(&published, &wire_view) != FALSE))
		{
			return;
		}
		if (queued_predicted_correction != FALSE)
		{
			if (syNetInputEpisodeSealedSpanBlocksPatch(sim_tick) == FALSE)
			{
				syNetInputPatchPublishedFromRemoteConfirmedReason(player, wire_tick, frame, "post_pre_promote");
			}
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
#if defined(PORT) && defined(SSB64_NETMENU)
		    (syNetInputStickReplaceNeedsRewind(player, sim_tick, prior_ring, &wire_view,
		                                       (had_published != FALSE) ? &published : NULL) != FALSE)
#else
		    (syNetInputGameplayCorrectionIsSignificantEx(prior_ring, frame, TRUE) != FALSE)
#endif
		    )
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
#if defined(PORT) && defined(SSB64_NETMENU)
			syNetRollbackQueueOrWidenStickCorrection(player, sim_tick);
#else
			syNetRollbackRequestInputCorrection(player, sim_tick);
#endif
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
#if defined(PORT) && defined(SSB64_NETMENU)
	if (syNetInputIntroWaitForceNeutralActive() != FALSE)
	{
		syNetInputForceFrameNeutralButtonsStick(&seed);
	}
#endif
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
		SYNETINPUT_STRICT_TAG("wire_gap_fill");
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

sb32 syNetInputSetRemoteInputFromPacketEx(s32 player, u32 tick, u16 buttons, s8 stick_x, s8 stick_y, u32 packet_seq,
                                          u32 current_tick, s32 frame_index, sb32 provisional)
{
	SYNetInputFrame frame;
	SYNetInputFrame existing;

	if (syNetInputCheckPlayer(player) == FALSE)
	{
		return FALSE;
	}
#if defined(PORT) && defined(SSB64_NETMENU)
	if (syNetInputIntroWaitForceNeutralActive() != FALSE)
	{
		buttons = 0;
		stick_x = 0;
		stick_y = 0;
		syNetInputClearFrame(&sSYNetInputSlots[player].last_non_neutral);
	}
#endif
	/*
	 * Sender-stamped provisional row (wire tick above the sender's authoritative frontier):
	 * runway / future-ahead extrapolation the sender has not simulated. Store gap-filled so it
	 * raises hr / pacing and hold-last resolution, but is never RemoteConfirmed — fake confirms
	 * poisoned write-once/reconcile against the real input (seed 3284691918 kill @596).
	 * Real (non-provisional) wire for the same tick replaces it via the normal commit below.
	 */
	if (provisional != FALSE)
	{
		sb32 have_existing;

		have_existing = syNetInputGetStoredFrame(sSYNetInputRemoteHistory, player, tick, &existing);
		if ((have_existing != FALSE) && (syNetInputFrameIsRemoteStrictConfirmed(&existing) != FALSE))
		{
			return FALSE;
		}
		syNetInputMakeFrame(&frame, tick, buttons, stick_x, stick_y, nSYNetInputSourceRemoteGapFilled, FALSE);
		if ((have_existing != FALSE) && (existing.source == nSYNetInputSourceRemoteGapFilled) &&
		    (syNetInputFrameGameplayEquals(&existing, &frame) != FALSE))
		{
			return FALSE;
		}
		SYNETINPUT_STRICT_TAG("wire_provisional");
		syNetInputStoreFrame(sSYNetInputRemoteHistory, player, &frame);
		syNetInputClearRemotePacketSeq(player, tick);
		syNetInputStrictReadyCacheInvalidate();
		return TRUE;
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
			/*
			 * GapFilled → matching Strict: must still Commit (ledger dual-write + promote/
			 * GGPO vs predicted published). Silent StoreRemoteConfirmed left hold_last
			 * published without a ledger row and skipped release REPLACE follow-ups.
			 */
			syNetInputCommitRemoteConfirmedWire(player, tick, packet_seq, &frame, &existing, TRUE);
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
				/*
				 * Dual-stick Go onset (soak1 857278917): while Android was stalled mid-GGPO
				 * (cur_tick≪wire), a newer pkt_seq rewrote confirmed smash ticks 394–401 to
				 * (0,0) — STRICT_INPUT wire_overwrite / deferred_queue_drop storm. First strict
				 * analog confirm for a tick is stick-authoritative; refuse *hard-zero* stick
				 * downgrade when buttons are unchanged. Soft sticks inside NearNeutral(14)
				 * (e.g. 10,5 / 4,5) are real releases — must REPLACE (soak 1981389058 /
				 * 582675261 soft_nz false positives).
				 * See docs/bugs/netplay_wire_neutral_downgrade_dual_stick_onset_2026-07-20.md.
				 */
				if ((syNetInputStickLooksAnalog(existing.stick_x, existing.stick_y) != FALSE) &&
				    (syNetInputFrameSticksHardZero(&frame) != FALSE) &&
				    (existing.buttons == frame.buttons))
				{
					if (sSYNetInputRemoteConfirmedConflictLogsRemaining > 0U)
					{
						s32 keep_mag_x;
						s32 keep_mag_y;

						keep_mag_x = syNetInputAbsS8Diff(existing.stick_x, 0);
						keep_mag_y = syNetInputAbsS8Diff(existing.stick_y, 0);
						port_log(
						    "SSB64 NetInput: REMOTE_CONFIRMED_REPLACE_REJECT_NEUTRAL_DOWNGRADE "
						    "player=%d wire=%u old_pkt_seq=%u pkt_seq=%u cur_tick=%u idx=%d "
						    "keep btn=0x%04X sx=%d sy=%d keep_mag=%d,%d | "
						    "reject btn=0x%04X sx=%d sy=%d reject_mag=0,0 "
						    "near_neutral=1 hard_zero=1 soft_nz=0 deadband=%u\n",
						    (int)player,
						    tick,
						    (unsigned int)((have_existing_packet_seq != FALSE) ? existing_packet_seq
						                                                      : 0U),
						    (unsigned int)packet_seq,
						    (unsigned int)current_tick,
						    (int)frame_index,
						    (unsigned int)existing.buttons,
						    existing.stick_x,
						    existing.stick_y,
						    (int)keep_mag_x,
						    (int)keep_mag_y,
						    (unsigned int)frame.buttons,
						    frame.stick_x,
						    frame.stick_y,
						    (unsigned int)syNetInputGgpoStickDeadbandPredict());
						sSYNetInputRemoteConfirmedConflictLogsRemaining--;
					}
					syNetInputStoreRemotePacketSeq(player, tick, packet_seq);
					return TRUE;
				}
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

sb32 syNetInputSetRemoteInputFromPacket(s32 player, u32 tick, u16 buttons, s8 stick_x, s8 stick_y, u32 packet_seq,
                                        u32 current_tick, s32 frame_index)
{
	return syNetInputSetRemoteInputFromPacketEx(player, tick, buttons, stick_x, stick_y, packet_seq, current_tick,
	                                            frame_index, FALSE);
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
#if defined(SSB64_NETMENU)
		if (syNetInputGetLocalGameplayFrame(player, tick, out_frame) != FALSE)
		{
			out_frame->source = nSYNetInputSourceLocal;
			out_frame->is_predicted = FALSE;
			syNetInputMaybeLogLocalInputFrame(player, hw_player, tick, out_frame, "gameplay_slot");
			return;
		}
#else
		if (syNetInputGetLocalDelayedFrame(player, tick, out_frame) != FALSE)
		{
			out_frame->source = nSYNetInputSourceLocal;
			out_frame->is_predicted = FALSE;
			syNetInputMaybeLogLocalInputFrame(player, hw_player, tick, out_frame, "delay_slot");
			return;
		}
#endif
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
#if defined(SSB64_NETMENU)
			SYNetInputFrame soft;

			syNetInputMakeFrame(&soft, tick, buttons, 0, 0, nSYNetInputSourceRemotePredicted, TRUE);
			syNetInputFillHoldLastSoftOnsetIfNeeded(player, tick, &soft);
			if (syNetInputFrameSticksNearNeutral(&soft) == FALSE)
			{
				stick_x = soft.stick_x;
				stick_y = soft.stick_y;
			}
			else
#endif
			{
				stick_x = 0;
				stick_y = 0;
				had_stick_seed = FALSE;
			}
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
#if defined(SSB64_NETMENU)
	/*
	 * After decay / seed assembly: smash-flip or soft-onset from wire already in the ring for
	 * this tick (dash-dance Turn allow). See FillHoldLastSoftOnsetIfNeeded.
	 */
	syNetInputFillHoldLastSoftOnsetIfNeeded(player, tick, out_frame);
#endif
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

#if defined(PORT) && defined(SSB64_NETMENU)
sb32 syNetInputAuthorityLedgerTryGet(s32 player, u32 sim_tick, SYNetInputFrame *out_frame, u8 *out_origin)
{
	u32 index;
	const SYNetInputFrame *row;
	u8 origin;

	if ((syNetInputCheckPlayer(player) == FALSE) || (sim_tick == 0U))
	{
		return FALSE;
	}
	index = sim_tick % SYNETINPUT_HISTORY_LENGTH;
	origin = sSYNetInputRemoteAuthorityLedgerOrigin[player][index];
	row = &sSYNetInputRemoteAuthorityLedger[player][index];
	if ((origin == SYNETINPUT_AUTH_LEDGER_ORIGIN_NONE) || (row->is_valid == FALSE) || (row->tick != sim_tick))
	{
		return FALSE;
	}
	if (out_frame != NULL)
	{
		*out_frame = *row;
		out_frame->tick = sim_tick;
		out_frame->source = nSYNetInputSourceRemoteConfirmed;
		out_frame->is_predicted = FALSE;
		out_frame->is_valid = TRUE;
	}
	if (out_origin != NULL)
	{
		*out_origin = origin;
	}
	return TRUE;
}

static void syNetInputAuthorityLedgerStore(s32 player, u32 sim_tick, const SYNetInputFrame *frame, u8 origin)
{
	u32 index;
	SYNetInputFrame store;

	if ((frame == NULL) || (syNetInputCheckPlayer(player) == FALSE) || (sim_tick == 0U) ||
	    (origin == SYNETINPUT_AUTH_LEDGER_ORIGIN_NONE))
	{
		return;
	}
	if (syNetInputIsRemoteHumanSlot(player) == FALSE)
	{
		return;
	}
	index = sim_tick % SYNETINPUT_HISTORY_LENGTH;
	/*
	 * Seal outranks wire: a wire commit must not replace a sealed ledger row with different
	 * gameplay (the prior fake-confirm vs seal fight). Matching gameplay is a no-op refresh.
	 */
	if ((origin == SYNETINPUT_AUTH_LEDGER_ORIGIN_WIRE) &&
	    (sSYNetInputRemoteAuthorityLedgerOrigin[player][index] == SYNETINPUT_AUTH_LEDGER_ORIGIN_SEAL) &&
	    (sSYNetInputRemoteAuthorityLedger[player][index].is_valid != FALSE) &&
	    (sSYNetInputRemoteAuthorityLedger[player][index].tick == sim_tick) &&
	    (syNetInputFrameGameplayEquals(&sSYNetInputRemoteAuthorityLedger[player][index], frame) == FALSE))
	{
		return;
	}
	/*
	 * Mirror REMOTE_CONFIRMED_REPLACE_REJECT: first strict analog ledger row is stick-
	 * authoritative against hard-zero poison only (soak1 871504438). Soft NearNeutral
	 * sticks must store (soak 582675261 soft_nz). Skip when gameplay already equals.
	 */
	if ((origin == SYNETINPUT_AUTH_LEDGER_ORIGIN_WIRE) &&
	    (sSYNetInputRemoteAuthorityLedgerOrigin[player][index] != SYNETINPUT_AUTH_LEDGER_ORIGIN_NONE) &&
	    (sSYNetInputRemoteAuthorityLedger[player][index].is_valid != FALSE) &&
	    (sSYNetInputRemoteAuthorityLedger[player][index].tick == sim_tick) &&
	    (syNetInputFrameGameplayEquals(&sSYNetInputRemoteAuthorityLedger[player][index], frame) == FALSE) &&
	    (syNetInputStickLooksAnalog(sSYNetInputRemoteAuthorityLedger[player][index].stick_x,
	                                sSYNetInputRemoteAuthorityLedger[player][index].stick_y) != FALSE) &&
	    (syNetInputFrameSticksHardZero(frame) != FALSE) &&
	    (sSYNetInputRemoteAuthorityLedger[player][index].buttons == frame->buttons))
	{
		if (syNetInputAuthorityPublishLogEnabled() != FALSE)
		{
			s32 keep_mag_x;
			s32 keep_mag_y;

			keep_mag_x = syNetInputAbsS8Diff(sSYNetInputRemoteAuthorityLedger[player][index].stick_x, 0);
			keep_mag_y = syNetInputAbsS8Diff(sSYNetInputRemoteAuthorityLedger[player][index].stick_y, 0);
			port_log(
			    "SSB64 NetInput: LEDGER_REJECT_NEUTRAL_DOWNGRADE player=%d sim_tick=%u "
			    "keep sx=%d sy=%d keep_mag=%d,%d | reject sx=%d sy=%d reject_mag=0,0 "
			    "near_neutral=1 hard_zero=1 soft_nz=0 deadband=%u\n",
			    (int)player, (unsigned int)sim_tick,
			    (int)sSYNetInputRemoteAuthorityLedger[player][index].stick_x,
			    (int)sSYNetInputRemoteAuthorityLedger[player][index].stick_y, (int)keep_mag_x,
			    (int)keep_mag_y, (int)frame->stick_x, (int)frame->stick_y,
			    (unsigned int)syNetInputGgpoStickDeadbandPredict());
		}
		return;
	}
	store = *frame;
	store.tick = sim_tick;
	store.source = nSYNetInputSourceRemoteConfirmed;
	store.is_predicted = FALSE;
	store.is_valid = TRUE;
	sSYNetInputRemoteAuthorityLedger[player][index] = store;
	sSYNetInputRemoteAuthorityLedgerOrigin[player][index] = origin;
}

void syNetInputAuthorityLedgerCommitWire(s32 player, u32 sim_tick, const SYNetInputFrame *frame)
{
	syNetInputAuthorityLedgerStore(player, sim_tick, frame, SYNETINPUT_AUTH_LEDGER_ORIGIN_WIRE);
}

void syNetInputAuthorityLedgerCommitSeal(s32 player, u32 sim_tick, const SYNetInputFrame *frame)
{
	syNetInputAuthorityLedgerStore(player, sim_tick, frame, SYNETINPUT_AUTH_LEDGER_ORIGIN_SEAL);
}

/*
 * Phase 2: copy ledger → published history. Confirmed published authority is ledger-owned;
 * promote/patch/publish call this instead of inventing RemoteConfirmed from wire/hold-last.
 * Write-once does not block ledger refresh (ledger outranks published).
 */
static sb32 syNetInputRefreshPublishedFromAuthorityLedger(s32 player, u32 sim_tick, const char *reason)
{
	SYNetInputFrame ledger;
	SYNetInputFrame published;
	u8 origin;
	sb32 had_published;

	if ((syNetInputCheckPlayer(player) == FALSE) || (sim_tick == 0U) ||
	    (syNetInputIsRemoteHumanSlot(player) == FALSE))
	{
		return FALSE;
	}
	if (syNetInputAuthorityLedgerTryGet(player, sim_tick, &ledger, &origin) == FALSE)
	{
		return FALSE;
	}
	had_published = syNetInputGetHistoryFrame(player, sim_tick, &published);
	if ((had_published != FALSE) && (syNetInputFrameGameplayEquals(&published, &ledger) != FALSE) &&
	    (published.is_predicted == FALSE) && (published.source == nSYNetInputSourceRemoteConfirmed))
	{
		sSYNetInputSlots[player].last_confirmed = ledger;
		syNetInputNoteRemoteNonNeutralStick(player, &ledger);
		return TRUE;
	}
	/*
	 * If durable history was skipped for this tick (legacy hold_confirmed_only), the stick the
	 * sim consumed still lives in last_published — treat that as the published baseline for
	 * completed-sim correction. See docs/bugs/netplay_ness_pk_hold_skip_durable_aim_fork_2026-07-15.md.
	 */
	if ((had_published == FALSE) && (sSYNetInputSlots[player].last_published.is_valid != FALSE) &&
	    (sSYNetInputSlots[player].last_published.tick == sim_tick))
	{
		published = sSYNetInputSlots[player].last_published;
		had_published = TRUE;
	}
	/*
	 * Belt: refuse *confirmed* published analog → ledger *hard-zero* refresh. Soft NearNeutral
	 * ledger sticks must refresh. Predictions never outrank confirmed release (soak 1511856153).
	 */
	if ((had_published != FALSE) && (published.is_predicted == FALSE) &&
	    (published.source == nSYNetInputSourceRemoteConfirmed) &&
	    (syNetInputStickLooksAnalog(published.stick_x, published.stick_y) != FALSE) &&
	    (syNetInputFrameSticksHardZero(&ledger) != FALSE) && (published.buttons == ledger.buttons))
	{
		if (syNetInputAuthorityPublishLogEnabled() != FALSE)
		{
			s32 keep_mag_x;
			s32 keep_mag_y;

			keep_mag_x = syNetInputAbsS8Diff(published.stick_x, 0);
			keep_mag_y = syNetInputAbsS8Diff(published.stick_y, 0);
			port_log(
			    "SSB64 NetInput: LEDGER_REFRESH_REJECT_NEUTRAL_DOWNGRADE player=%d sim_tick=%u "
			    "keep sx=%d sy=%d keep_mag=%d,%d | reject sx=%d sy=%d reject_mag=0,0 "
			    "near_neutral=1 hard_zero=1 soft_nz=0 deadband=%u writer=%s\n",
			    (int)player, (unsigned int)sim_tick, (int)published.stick_x, (int)published.stick_y,
			    (int)keep_mag_x, (int)keep_mag_y, (int)ledger.stick_x, (int)ledger.stick_y,
			    (unsigned int)syNetInputGgpoStickDeadbandPredict(),
			    (reason != NULL) ? reason : "?");
		}
		return TRUE;
	}
	SYNETINPUT_STRICT_TAG("ledger_publish_refresh");
	syNetInputStoreFrame(sSYNetInputHistory, player, &ledger);
	syNetInputStrictReadyCacheInvalidate();
	/*
	 * Hold-last seeds from last_confirmed. Ledger wire/seal is authority — advance the seed so a
	 * release confirm (-1,0) is not followed by hold_last of the pre-release stick (-64,-4) while
	 * soft onset is suppressed. Soak1 1645329949: post-GGPO REMOTE_PUBLISH@2315 still hold_last
	 * -64,-4 after ledger_wire@2313 -1,0.
	 */
	sSYNetInputSlots[player].last_confirmed = ledger;
	syNetInputNoteRemoteNonNeutralStick(player, &ledger);
	if ((syNetInputAuthorityPublishLogEnabled() != FALSE) &&
	    ((ledger.stick_x != 0) || (ledger.stick_y != 0) || (ledger.buttons != 0)))
	{
		port_log(
		    "SSB64 NetInput: REMOTE_PUBLISH player=%d sim_tick=%u sx=%d sy=%d source=ledger_%s writer=%s\n",
		    (int)player, (unsigned int)sim_tick, (int)ledger.stick_x, (int)ledger.stick_y,
		    (origin == SYNETINPUT_AUTH_LEDGER_ORIGIN_SEAL) ? "seal" : "wire",
		    (reason != NULL) ? reason : "?");
	}
	/*
	 * Completed-sim tick and the ledger row changed the gameplay the sim already consumed
	 * (hold-last / provisional publish superseded by confirmed wire): the published rewrite
	 * alone silently forks the snapshot history — the state at sim_tick was built from the
	 * old row while published now claims confirmed truth, so "inputs agree" checks pass on a
	 * forked universe (soak1 @861: ±1 stick absorb → RESIM_BASELINE_MISMATCH with inputs
	 * agreeing through load). Any published≠ledger delta on an already-simmed tick must
	 * queue a correction, regardless of deadband significance.
	 * See docs/bugs/netplay_input_authority_tuple_fork_fail_closed_2026-07-15.md.
	 */
	if ((had_published != FALSE) && (published.is_valid != FALSE) &&
	    (syNetInputFrameGameplayEquals(&published, &ledger) == FALSE) &&
	    (syNetInputGetTick() > sim_tick) && (syNetRollbackIsResimulating() == FALSE))
	{
		if (syNetInputAuthorityPublishLogEnabled() != FALSE)
		{
			port_log(
			    "SSB64 NetInput: LEDGER_REFRESH_COMPLETED_SIM_CORRECT player=%d sim_tick=%u sim_now=%u "
			    "old_sx=%d old_sy=%d old_btn=0x%04X new_sx=%d new_sy=%d new_btn=0x%04X writer=%s\n",
			    (int)player, (unsigned int)sim_tick, (unsigned int)syNetInputGetTick(),
			    (int)published.stick_x, (int)published.stick_y, (unsigned int)published.buttons,
			    (int)ledger.stick_x, (int)ledger.stick_y, (unsigned int)ledger.buttons,
			    (reason != NULL) ? reason : "?");
		}
		syNetRollbackQueueOrWidenStickCorrection(player, sim_tick);
	}
	return TRUE;
}
#endif

#ifdef PORT
static sb32 syNetInputTryGetRemoteConfirmedHistoryForSimTick(s32 player, u32 sim_tick, SYNetInputFrame *out_frame)
{
	SYNetInputFrame frame;
#if defined(SSB64_NETMENU)
	/*
	 * Prefer authority ledger (wire/seal dual-write) over the wire ring. Ledger is sim-tick
	 * keyed and seal-outranks-wire; wire ring still holds provisional gap-fill for pacing.
	 */
	if (syNetInputAuthorityLedgerTryGet(player, sim_tick, &frame, NULL) != FALSE)
	{
		if (out_frame != NULL)
		{
			*out_frame = frame;
		}
		return TRUE;
	}
#endif

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

#if defined(PORT) && defined(SSB64_NETMENU)
/*
 * Strict input-authority witness (SSB64_NETPLAY_STRICT_INPUT=1, log-only, netmenu builds).
 *
 * Target end-state (docs/bugs/netplay_strict_input_authority_witness_2026-07-12.md): confirmed remote
 * input is single-writer + write-once; rollback is a mechanical consumed-vs-confirmed compare; frame
 * commit covers confirmed inputs only. This witness enumerates every writer that violates those
 * invariants today so the migration can retire the replace/deadband heuristics call site by call site.
 *
 * Violation kinds (all log-only, zero behavior change):
 * - wire_overwrite      : strict-confirmed wire ring row rewritten with different gameplay (the
 *                         feel-0 runway resend — sender fabricated a future tick as confirmed).
 * - wire_downgrade      : strict-confirmed wire ring row rewritten by a non-strict source.
 * - fabricated_confirm  : published row stamped RemoteConfirmed with no matching strict wire row
 *                         (sealed-row reconcile, gap-fill promotion, runway stamping).
 * - confirm_rewrite     : confirmed published row rewritten by confirmed with different gameplay.
 * - confirm_downgrade   : confirmed published row rewritten by a non-confirmed source with
 *                         different gameplay (Promote/predict paths un-confirming history).
 */
#define SYNETINPUT_STRICT_WITNESS_KINDS 5
#define SYNETINPUT_STRICT_WITNESS_LOG_BUDGET 200U
static const char *sSYNetInputStrictWitnessKindNames[SYNETINPUT_STRICT_WITNESS_KINDS] = {
	"wire_overwrite", "wire_downgrade", "fabricated_confirm", "confirm_rewrite", "confirm_downgrade",
};
enum
{
	nSYNetInputStrictWitnessWireOverwrite = 0,
	nSYNetInputStrictWitnessWireDowngrade,
	nSYNetInputStrictWitnessFabricatedConfirm,
	nSYNetInputStrictWitnessConfirmRewrite,
	nSYNetInputStrictWitnessConfirmDowngrade
};
static u32 sSYNetInputStrictWitnessCounts[SYNETINPUT_STRICT_WITNESS_KINDS];
static u32 sSYNetInputStrictWitnessLogsUsed;
static const char *sSYNetInputStrictWitnessWriterTag;

static sb32 syNetInputStrictWitnessEnabled(void)
{
	static s32 sCached = -999;
	const char *e;

	if (sCached == -999)
	{
		e = getenv("SSB64_NETPLAY_STRICT_INPUT");
		sCached = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	}
	return (sCached != 0) ? TRUE : FALSE;
}

static void syNetInputStrictWitnessTagWriter(const char *tag)
{
	sSYNetInputStrictWitnessWriterTag = tag;
}

static sb32 syNetInputStrictWitnessGameplayDiffers(const SYNetInputFrame *a, const SYNetInputFrame *b)
{
	/* Tick excluded on purpose: wire vs sim keying differs across rings. */
	return ((a->buttons != b->buttons) || (a->stick_x != b->stick_x) || (a->stick_y != b->stick_y)) ? TRUE : FALSE;
}

static void syNetInputStrictWitnessReport(s32 kind, s32 player, const SYNetInputFrame *old_frame,
                                          const SYNetInputFrame *new_frame, const char *writer)
{
	u32 total;
	s32 i;

	sSYNetInputStrictWitnessCounts[kind]++;
	total = 0U;
	for (i = 0; i < SYNETINPUT_STRICT_WITNESS_KINDS; i++)
	{
		total += sSYNetInputStrictWitnessCounts[i];
	}
	if (sSYNetInputStrictWitnessLogsUsed < SYNETINPUT_STRICT_WITNESS_LOG_BUDGET)
	{
		sSYNetInputStrictWitnessLogsUsed++;
		port_log(
		    "SSB64 NetInput: STRICT_INPUT kind=%s writer=%s player=%d tick=%u cur_tick=%u "
		    "old btn=0x%04X sx=%d sy=%d src=%d pred=%u | new btn=0x%04X sx=%d sy=%d src=%d pred=%u\n",
		    sSYNetInputStrictWitnessKindNames[kind], (writer != NULL) ? writer : "untagged", (int)player,
		    (unsigned int)new_frame->tick, (unsigned int)syNetInputGetTick(),
		    (old_frame != NULL) ? (unsigned int)old_frame->buttons : 0U,
		    (old_frame != NULL) ? (int)old_frame->stick_x : 0, (old_frame != NULL) ? (int)old_frame->stick_y : 0,
		    (old_frame != NULL) ? (int)old_frame->source : -1,
		    (old_frame != NULL) ? (unsigned int)old_frame->is_predicted : 0U, (unsigned int)new_frame->buttons,
		    (int)new_frame->stick_x, (int)new_frame->stick_y, (int)new_frame->source,
		    (unsigned int)new_frame->is_predicted);
	}
	if ((total % 256U) == 0U)
	{
		port_log(
		    "SSB64 NetInput: STRICT_INPUT_SUMMARY total=%u wire_overwrite=%u wire_downgrade=%u "
		    "fabricated_confirm=%u confirm_rewrite=%u confirm_downgrade=%u logged=%u\n",
		    total, sSYNetInputStrictWitnessCounts[0], sSYNetInputStrictWitnessCounts[1],
		    sSYNetInputStrictWitnessCounts[2], sSYNetInputStrictWitnessCounts[3],
		    sSYNetInputStrictWitnessCounts[4], sSYNetInputStrictWitnessLogsUsed);
	}
}

static void syNetInputStrictWitnessOnStore(SYNetInputFrame history[][SYNETINPUT_HISTORY_LENGTH], s32 player,
                                           const SYNetInputFrame *frame)
{
	const SYNetInputFrame *old_row;
	const char *writer;
	sb32 old_matches_tick;
	sb32 new_is_confirmed;

	writer = sSYNetInputStrictWitnessWriterTag;
	sSYNetInputStrictWitnessWriterTag = NULL;
	if ((syNetInputStrictWitnessEnabled() == FALSE) || (frame == NULL) || (frame->is_valid == FALSE) ||
	    (syNetInputCheckPlayer(player) == FALSE) || (syNetPeerIsVSSessionActive() == FALSE))
	{
		return;
	}
	old_row = &history[player][frame->tick % SYNETINPUT_HISTORY_LENGTH];
	old_matches_tick = ((old_row->is_valid != FALSE) && (old_row->tick == frame->tick)) ? TRUE : FALSE;
	new_is_confirmed = syNetInputFrameIsRemoteStrictConfirmed(frame);

	if (history == sSYNetInputRemoteHistory)
	{
		if ((old_matches_tick == FALSE) || (syNetInputFrameIsRemoteStrictConfirmed(old_row) == FALSE))
		{
			return;
		}
		if (syNetInputStrictWitnessGameplayDiffers(old_row, frame) == FALSE)
		{
			return;
		}
		syNetInputStrictWitnessReport((new_is_confirmed != FALSE) ? nSYNetInputStrictWitnessWireOverwrite
		                                                          : nSYNetInputStrictWitnessWireDowngrade,
		                              player, old_row, frame, writer);
		return;
	}
	if (history != sSYNetInputHistory)
	{
		return;
	}
	if (syNetInputIsRemoteHumanSlot(player) == FALSE)
	{
		return;
	}
	if (new_is_confirmed != FALSE)
	{
		SYNetInputFrame wire;

		if ((syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, frame->tick, &wire) == FALSE) ||
		    (syNetInputStrictWitnessGameplayDiffers(&wire, frame) != FALSE))
		{
			syNetInputStrictWitnessReport(nSYNetInputStrictWitnessFabricatedConfirm, player, old_row, frame,
			                              writer);
		}
	}
	if ((old_matches_tick == FALSE) || (syNetInputFrameIsRemoteStrictConfirmed(old_row) == FALSE) ||
	    (syNetInputStrictWitnessGameplayDiffers(old_row, frame) == FALSE))
	{
		return;
	}
	syNetInputStrictWitnessReport((new_is_confirmed != FALSE) ? nSYNetInputStrictWitnessConfirmRewrite
	                                                          : nSYNetInputStrictWitnessConfirmDowngrade,
	                              player, old_row, frame, writer);
}

void syNetInputStrictWitnessLogMatchSummary(const char *when)
{
	u32 total;
	s32 i;

	if (syNetInputStrictWitnessEnabled() == FALSE)
	{
		return;
	}
	total = 0U;
	for (i = 0; i < SYNETINPUT_STRICT_WITNESS_KINDS; i++)
	{
		total += sSYNetInputStrictWitnessCounts[i];
	}
	if (total != 0U)
	{
		port_log(
		    "SSB64 NetInput: STRICT_INPUT_SUMMARY when=%s total=%u wire_overwrite=%u wire_downgrade=%u "
		    "fabricated_confirm=%u confirm_rewrite=%u confirm_downgrade=%u logged=%u\n",
		    (when != NULL) ? when : "unknown", total, sSYNetInputStrictWitnessCounts[0],
		    sSYNetInputStrictWitnessCounts[1], sSYNetInputStrictWitnessCounts[2],
		    sSYNetInputStrictWitnessCounts[3], sSYNetInputStrictWitnessCounts[4],
		    sSYNetInputStrictWitnessLogsUsed);
	}
	for (i = 0; i < SYNETINPUT_STRICT_WITNESS_KINDS; i++)
	{
		sSYNetInputStrictWitnessCounts[i] = 0U;
	}
	sSYNetInputStrictWitnessLogsUsed = 0U;
}
#endif /* PORT && SSB64_NETMENU */

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
	SYNETINPUT_STRICT_TAG("wire_predicted");
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
#if defined(SSB64_NETMENU)
				/*
				 * soak1 582675261: sealed (-66,-69) vs ledger (78,-6) @435 — opposite
				 * smash from latch/pack poison. Prefer ledger when dash-gate or analog
				 * intent disagree so Turn allow does not see phantom |sx|≥56.
				 *
				 * soak1 1857971875: INTENT_OVERRIDE was remote-only; Linux local P0
				 * applied seal (73,49) while Android remote kept ledger (-75,-41) →
				 * PHYSICS_FORK@420. Local slots prefer wire-locked / history / gameplay.
				 */
				{
					SYNetInputFrame truth;
					sb32 have_truth = FALSE;
					const char *truth_tag = "truth";

					if (syNetInputIsRemoteHumanSlot(player) != FALSE)
					{
						if (syNetInputAuthorityLedgerTryGet(player, tick, &truth, NULL) != FALSE)
						{
							have_truth = TRUE;
							truth_tag = "ledger";
						}
					}
					else if (syNetInputIsLocalDelaySlot(player) != FALSE)
					{
						if (syNetInputTryGetLocalWireLockedSample(player, tick, &truth) != FALSE)
						{
							have_truth = TRUE;
							truth_tag = "wire";
						}
						else if ((syNetInputGetHistoryFrame(player, tick, &truth) != FALSE) &&
						         (truth.is_predicted == FALSE))
						{
							have_truth = TRUE;
							truth_tag = "history";
						}
						else if (syNetInputGetLocalGameplayFrame(player, tick, &truth) != FALSE)
						{
							have_truth = TRUE;
							truth_tag = "gameplay";
						}
					}
					if ((have_truth != FALSE) &&
					    (syNetInputStickSealIntentDisagree(out_frame->stick_x, out_frame->stick_y,
					                                       truth.stick_x, truth.stick_y) != FALSE))
					{
						port_log(
						    "SSB64 NetInput: SEAL_LEDGER_INTENT_OVERRIDE player=%d "
						    "sim_tick=%u sealed sx=%d sy=%d | %s sx=%d sy=%d\n",
						    (int)player, (unsigned int)tick, (int)out_frame->stick_x,
						    (int)out_frame->stick_y, truth_tag, (int)truth.stick_x,
						    (int)truth.stick_y);
						*out_frame = truth;
						out_frame->tick = tick;
						out_frame->source = (syNetInputIsRemoteHumanSlot(player) != FALSE)
						                        ? nSYNetInputSourceRemoteConfirmed
						                        : nSYNetInputSourceLocal;
						out_frame->is_predicted = FALSE;
						out_frame->is_valid = TRUE;
					}
				}
#endif
				if (syNetInputIsRemoteHumanSlot(player) != FALSE)
				{
#if defined(SSB64_NETMENU)
					port_log(
					    "SSB64 NetInput: RESIM_INPUT_SOURCE player=%d tick=%u selected=sealed "
					    "wire=-1 sealed=1 hist=-1 hist_conf=-1 ledger=-1 in_span=1 sx=%d sy=%d\n",
					    (int)player, (unsigned int)tick, (int)out_frame->stick_x,
					    (int)out_frame->stick_y);
#endif
					sSYNetInputSlots[player].last_confirmed = *out_frame;
					syNetInputNoteRemoteNonNeutralStick(player, out_frame);
				}
				return;
			}
			/*
			 * Sealed miss (tick outside [mismatch, target) — e.g. load_tick while
			 * mismatch = load+1). Do NOT invent stick-neutral: PublishFrame then
			 * skips ledger because EpisodeInputsSealed, so resim applies (0,0)
			 * while forward/ledger still has the cliff stick (soak1 2104045952:
			 * SEALED_RESIM_LEDGER_SKIP@607 sealed=0,0 ledger=43,66 → PEER@607).
			 * Fall through to confirmed / published History for that tick.
			 * See docs/bugs/netplay_sealed_resim_load_tick_neutral_invent_2026-07-19.md.
			 */
		}
		if (syNetInputIsRemoteHumanSlot(player) != FALSE)
		{
#if defined(SSB64_NETMENU)
			/*
			 * Remote resim selection (soak1 256718957 @442): never promote hold_last /
			 * predicted History to RemoteConfirmed. Log RESIM_INPUT_SOURCE so the
			 * chosen tier is explicit. See netplay_resim_input_source_2026-07-20.md.
			 */
			{
				SYNetInputFrame sealed_probe;
				SYNetInputFrame hist_probe;
				SYNetInputFrame ledger_probe;
				sb32 have_sealed;
				sb32 have_wire;
				sb32 have_hist;
				sb32 hist_confirmed;
				sb32 have_ledger;
				sb32 in_span;

				have_sealed = ((syNetRollbackEpisodeInputsSealed() != FALSE) &&
				               (syNetRollbackEpisodeGetSealedFrame(player, tick, &sealed_probe) != FALSE))
				                  ? TRUE
				                  : FALSE;
				have_wire = syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, tick, out_frame);
				have_hist = syNetInputGetHistoryFrame(player, tick, &hist_probe);
				hist_confirmed = ((have_hist != FALSE) &&
				                  (syNetInputFrameIsRemoteStrictConfirmed(&hist_probe) != FALSE))
				                     ? TRUE
				                     : FALSE;
				have_ledger = syNetInputAuthorityLedgerTryGet(player, tick, &ledger_probe, NULL);
				in_span = ((syNetRollbackEpisodeInputsSealed() != FALSE) &&
				           (syNetRollbackEpisodeTickInSealedSpan(tick) != FALSE))
				              ? TRUE
				              : FALSE;

				if (have_sealed != FALSE)
				{
					*out_frame = sealed_probe;
					out_frame->tick = tick;
					out_frame->source = nSYNetInputSourceRemoteConfirmed;
					out_frame->is_predicted = FALSE;
					out_frame->is_valid = TRUE;
					port_log(
					    "SSB64 NetInput: RESIM_INPUT_SOURCE player=%d tick=%u selected=sealed "
					    "wire=%d sealed=1 hist=%d hist_conf=%d ledger=%d in_span=%d "
					    "sx=%d sy=%d\n",
					    (int)player, (unsigned int)tick, (int)have_wire, (int)have_hist,
					    (int)hist_confirmed, (int)have_ledger, (int)in_span, (int)out_frame->stick_x,
					    (int)out_frame->stick_y);
					sSYNetInputSlots[player].last_confirmed = *out_frame;
					syNetInputNoteRemoteNonNeutralStick(player, out_frame);
					return;
				}
				if (have_wire != FALSE)
				{
					port_log(
					    "SSB64 NetInput: RESIM_INPUT_SOURCE player=%d tick=%u selected=wire "
					    "wire=1 sealed=0 hist=%d hist_conf=%d ledger=%d in_span=%d sx=%d sy=%d\n",
					    (int)player, (unsigned int)tick, (int)have_hist, (int)hist_confirmed,
					    (int)have_ledger, (int)in_span, (int)out_frame->stick_x,
					    (int)out_frame->stick_y);
					sSYNetInputSlots[player].last_confirmed = *out_frame;
					syNetInputNoteRemoteNonNeutralStick(player, out_frame);
					return;
				}
				if (hist_confirmed != FALSE)
				{
					*out_frame = hist_probe;
					out_frame->source = nSYNetInputSourceRemoteConfirmed;
					out_frame->is_predicted = FALSE;
					port_log(
					    "SSB64 NetInput: RESIM_INPUT_SOURCE player=%d tick=%u selected=history "
					    "wire=0 sealed=0 hist=1 hist_conf=1 ledger=%d in_span=%d sx=%d sy=%d\n",
					    (int)player, (unsigned int)tick, (int)have_ledger, (int)in_span,
					    (int)out_frame->stick_x, (int)out_frame->stick_y);
					sSYNetInputSlots[player].last_confirmed = *out_frame;
					syNetInputNoteRemoteNonNeutralStick(player, out_frame);
					return;
				}
				if ((syNetRollbackEpisodeInputsSealed() != FALSE) && (have_ledger != FALSE))
				{
					*out_frame = ledger_probe;
					out_frame->tick = tick;
					out_frame->source = nSYNetInputSourceRemoteConfirmed;
					out_frame->is_predicted = FALSE;
					out_frame->is_valid = TRUE;
					port_log(
					    "SSB64 NetInput: RESIM_INPUT_SOURCE player=%d tick=%u selected=ledger "
					    "wire=0 sealed=0 hist=%d hist_conf=%d ledger=1 in_span=%d sx=%d sy=%d\n",
					    (int)player, (unsigned int)tick, (int)have_hist, (int)hist_confirmed,
					    (int)in_span, (int)out_frame->stick_x, (int)out_frame->stick_y);
					sSYNetInputSlots[player].last_confirmed = *out_frame;
					syNetInputNoteRemoteNonNeutralStick(player, out_frame);
					return;
				}
				/*
				 * Hold-last / prediction last resort. Never stamp RemoteConfirmed.
				 * Soak1 256718957: History hold_last was promoted to confirmed →
				 * fabricated_confirm → STICK pred=0 with stale (-31,5) vs owner (-58,6).
				 */
				{
					const SYNetInputFrame *last_confirmed = &sSYNetInputSlots[player].last_confirmed;
					u16 buttons = 0;
					s8 stick_x = 0;
					s8 stick_y = 0;

					if (last_confirmed->is_valid != FALSE)
					{
						buttons = last_confirmed->buttons;
						stick_x = last_confirmed->stick_x;
						stick_y = last_confirmed->stick_y;
					}
					syNetInputMakeFrame(out_frame, tick, buttons, stick_x, stick_y,
					                    nSYNetInputSourceRemotePredicted, TRUE);
					syNetInputFillHoldLastSoftOnsetIfNeeded(player, tick, out_frame);
					port_log(
					    "SSB64 NetInput: RESIM_INPUT_SOURCE player=%d tick=%u selected=hold_last "
					    "wire=0 sealed=0 hist=%d hist_conf=%d ledger=%d in_span=%d sx=%d sy=%d\n",
					    (int)player, (unsigned int)tick, (int)have_hist, (int)hist_confirmed,
					    (int)have_ledger, (int)in_span, (int)out_frame->stick_x,
					    (int)out_frame->stick_y);
					if ((in_span != FALSE) || (have_hist != FALSE && hist_confirmed == FALSE))
					{
						port_log(
						    "SSB64 NetInput: RESIM_INPUT_SOURCE_ASSERT player=%d tick=%u "
						    "selected=hold_last in_span=%d hist_pred_or_unconf=%d "
						    "reason=authoritative_tier_missing_for_span_or_stale_hist\n",
						    (int)player, (unsigned int)tick, (int)in_span,
						    (int)(have_hist != FALSE && hist_confirmed == FALSE));
					}
					return;
				}
			}
#else
			if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, tick, out_frame) != FALSE)
			{
				sSYNetInputSlots[player].last_confirmed = *out_frame;
				syNetInputNoteRemoteNonNeutralStick(player, out_frame);
				return;
			}
			if (syNetInputGetHistoryFrame(player, tick, out_frame) != FALSE)
			{
				if (syNetInputFrameIsRemoteStrictConfirmed(out_frame) != FALSE)
				{
					return;
				}
			}
			syNetInputMakeFrame(out_frame, tick, 0, 0, 0, nSYNetInputSourceRemotePredicted, TRUE);
			return;
#endif
		}
#if defined(SSB64_NETMENU)
		/*
		 * Local slot sealed miss during episode resim: prefer wire-locked /
		 * published History over inventing neutral (same soak class as remote).
		 */
		if (syNetRollbackEpisodeInputsSealed() != FALSE)
		{
			SYNetInputFrame wire;

			if (syNetInputTryGetLocalWireLockedSample(player, tick, &wire) != FALSE)
			{
				*out_frame = wire;
				out_frame->tick = tick;
				out_frame->source = nSYNetInputSourceLocal;
				out_frame->is_predicted = FALSE;
				out_frame->is_valid = TRUE;
				return;
			}
			if ((syNetInputGetHistoryFrame(player, tick, out_frame) != FALSE) &&
			    (out_frame->is_predicted == FALSE))
			{
				out_frame->source = nSYNetInputSourceLocal;
				out_frame->is_predicted = FALSE;
				return;
			}
		}
#endif
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
#if defined(PORT) && defined(SSB64_NETMENU)
	SYNetInputFrame confirmed_keep;
#endif
	u16 prev_buttons = 0;
	u16 preserved_tap = 0;
	u16 preserved_release = 0;
	u16 pressed;
	u16 released;

#if defined(PORT) && defined(SSB64_NETMENU)
	/*
	 * Local authoritative History is immutable: if PublishFrame tries to restage a gap tick
	 * with later HID, keep the frozen row so controllers + History stay consistent.
	 */
	if ((frame != NULL) && (syNetInputIsLocalDelaySlot(player) != FALSE) && (frame->tick != 0U) &&
	    (syNetInputGetHistoryFrame(player, frame->tick, &confirmed_keep) != FALSE) &&
	    (syNetInputLocalHistoryAuthFreezeBlocks(player, &confirmed_keep, frame, "publish_frame") != FALSE))
	{
		*frame = confirmed_keep;
	}
	/*
	 * Phase 2: remote publish prefers authority ledger over inventing sticks. Thin write-once
	 * remains if ledger is empty and published is already confirmed.
	 *
	 * Sealed-episode resim: when ResolveFrame filled `frame` from a *valid sealed
	 * span row, do not replace it with ledger — initiator remote ledger can disagree
	 * with the sealed local-authority row the follower applies (soak1 1043859099 @979).
	 * Seal is the resim contract for ticks in [mismatch, target); ledger refresh runs
	 * after CommitPromoteSealed.
	 *
	 * Ticks outside that span (load_tick while mismatch=load+1) are not sealed rows —
	 * allow ledger/history as usual (soak1 2104045952: skip@607 sealed invent 0,0 vs
	 * ledger 43,66). See docs/bugs/netplay_sealed_resim_load_tick_neutral_invent_2026-07-19.md
	 * and docs/bugs/netplay_seal_ledger_resim_stick_fork_2026-07-19.md.
	 */
	if ((frame != NULL) && (syNetInputIsRemoteHumanSlot(player) != FALSE) && (frame->tick != 0U) &&
	    ((syNetRollbackIsResimulating() == FALSE) || (syNetRollbackEpisodeInputsSealed() == FALSE) ||
	     (syNetRollbackEpisodeTickInSealedSpan(frame->tick) == FALSE)))
	{
		SYNetInputFrame ledger;

		if (syNetInputAuthorityLedgerTryGet(player, frame->tick, &ledger, NULL) != FALSE)
		{
			*frame = ledger;
		}
		else if ((syNetInputGetHistoryFrame(player, frame->tick, &confirmed_keep) != FALSE) &&
		         (syNetInputRemoteConfirmedWriteOnceBlocks(player, &confirmed_keep, frame, "publish_frame") !=
		          FALSE))
		{
			*frame = confirmed_keep;
		}
	}
	else if ((frame != NULL) && (syNetInputIsRemoteHumanSlot(player) != FALSE) && (frame->tick != 0U) &&
	         (syNetRollbackIsResimulating() != FALSE) && (syNetRollbackEpisodeInputsSealed() != FALSE) &&
	         (syNetRollbackEpisodeTickInSealedSpan(frame->tick) != FALSE))
	{
		SYNetInputFrame ledger;
		sb32 intent_disagree;

		if ((syNetInputAuthorityLedgerTryGet(player, frame->tick, &ledger, NULL) != FALSE) &&
		    (syNetInputFrameGameplayEquals(frame, &ledger) == FALSE))
		{
			intent_disagree = syNetInputStickSealIntentDisagree(frame->stick_x, frame->stick_y,
			                                                    ledger.stick_x, ledger.stick_y);
			/*
			 * Opposite smash / dash-gate vs ledger: fail-closed to ledger (soak1 582675261
			 * sealed -66 vs ledger +78 @435). Soft same-intent mag noise still keeps seal.
			 */
			if (intent_disagree != FALSE)
			{
				port_log(
				    "SSB64 NetInput: SEAL_LEDGER_INTENT_OVERRIDE player=%d sim_tick=%u "
				    "sealed sx=%d sy=%d | ledger sx=%d sy=%d\n",
				    (int)player, (unsigned int)frame->tick, (int)frame->stick_x,
				    (int)frame->stick_y, (int)ledger.stick_x, (int)ledger.stick_y);
				*frame = ledger;
			}
			else if (syNetInputAuthorityPublishLogEnabled() != FALSE)
			{
				u32 mismatch_tick;
				u32 target_tick;
				u32 t;
				u32 dumped;

				port_log(
				    "SSB64 NetInput: SEALED_RESIM_LEDGER_SKIP player=%d sim_tick=%u "
				    "sealed btn=0x%04X sx=%d sy=%d | ledger btn=0x%04X sx=%d sy=%d\n",
				    (int)player, (unsigned int)frame->tick, (unsigned int)frame->buttons,
				    (int)frame->stick_x, (int)frame->stick_y, (unsigned int)ledger.buttons,
				    (int)ledger.stick_x, (int)ledger.stick_y);
				if (sSYNetInputSealSkipSpanDumpBudget > 0U)
				{
					mismatch_tick = syNetRollbackEpisodeFsmGetMismatchTick();
					target_tick = syNetRollbackEpisodeFsmGetTargetTick();
					if ((mismatch_tick != 0U) && (target_tick > mismatch_tick) &&
					    ((target_tick - mismatch_tick) <= 32U))
					{
						port_log(
						    "SSB64 NetInput: SEALED_RESIM_LEDGER_SKIP_SPAN player=%d "
						    "mismatch=%u target=%u skip_tick=%u\n",
						    (int)player, (unsigned int)mismatch_tick,
						    (unsigned int)target_tick, (unsigned int)frame->tick);
						dumped = 0U;
						for (t = mismatch_tick; (t < target_tick) && (dumped < 16U); t++)
						{
							SYNetInputFrame seal_row;

							if (syNetRollbackEpisodeGetSealedFrame(player, t, &seal_row) !=
							    FALSE)
							{
								port_log(
								    "SSB64 NetInput: SEAL_ROW player=%d tick=%u "
								    "btn=0x%04X sx=%d sy=%d valid=1\n",
								    (int)player, (unsigned int)t,
								    (unsigned int)seal_row.buttons,
								    (int)seal_row.stick_x, (int)seal_row.stick_y);
							}
							else
							{
								port_log(
								    "SSB64 NetInput: SEAL_ROW player=%d tick=%u "
								    "valid=0\n",
								    (int)player, (unsigned int)t);
							}
							dumped++;
						}
						sSYNetInputSealSkipSpanDumpBudget--;
					}
				}
			}
		}
	}
#endif
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

	SYNETINPUT_STRICT_TAG("publish_frame");
#if defined(PORT) && defined(SSB64_NETMENU)
	/*
	 * Tag provenance before StoreFrame mint gate. Gameplay ring match → GAMEPLAY;
	 * predicted / remote confirmed keep their origins; untagged Local+!predicted is
	 * downgraded (cannot mint seal authority from latch/empty).
	 */
	if (syNetInputIsRemoteHumanSlot(player) != FALSE)
	{
		if (frame->is_predicted != FALSE)
		{
			SYNETINPUT_PROVENANCE_TAG(nSYNetInputHistoryProvPrediction, "publish_frame");
		}
		else
		{
			SYNETINPUT_PROVENANCE_TAG(nSYNetInputHistoryProvRemoteConfirmed, "publish_frame");
		}
	}
	else if (syNetInputIsLocalDelaySlot(player) != FALSE)
	{
		SYNetInputFrame gameplay;

		if ((syNetInputGetLocalGameplayFrame(player, frame->tick, &gameplay) != FALSE) &&
		    (syNetInputFrameGameplayEquals(&gameplay, frame) != FALSE))
		{
			SYNETINPUT_PROVENANCE_TAG(nSYNetInputHistoryProvGameplay, "publish_frame");
		}
		else if (frame->is_predicted != FALSE)
		{
			SYNETINPUT_PROVENANCE_TAG(nSYNetInputHistoryProvPrediction, "publish_frame");
		}
		else
		{
			SYNetInputFrame tx;

			if ((syNetInputGetStoredFrame(sSYNetInputTransmittedHistory, player, frame->tick, &tx) !=
			     FALSE) &&
			    (syNetInputFrameGameplayEquals(&tx, frame) != FALSE))
			{
				SYNETINPUT_PROVENANCE_TAG(nSYNetInputHistoryProvLocalPublish, "publish_frame");
			}
			else
			{
				SYNETINPUT_PROVENANCE_TAG(nSYNetInputHistoryProvNone, "publish_frame");
			}
		}
	}
#endif
	syNetInputStoreFrame(sSYNetInputHistory, player, frame);
	/* After StoreFrame: reflect mint-downgrade of is_predicted into last_published. */
	sSYNetInputSlots[player].last_published = *frame;
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
#if defined(PORT) && defined(SSB64_NETMENU)
	/*
	 * Shared-commit ring_ready / strict admission: only real RemoteConfirmed counts.
	 * Auth-frontier / feel-0 provisional gap-fill raises hr but must not short-circuit
	 * EvaluateSharedCommitStep into the confirmed path (WireReady is strict/ledger-only → R).
	 * See docs/bugs/netplay_provisional_ring_ready_blocks_predict_2026-07-12.md.
	 */
	return syNetInputFrameIsRemoteStrictConfirmed(&frame);
#elif defined(PORT)
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
	SYNETINPUT_STRICT_TAG("debug_xor");
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

/*
 * Shared across peers when FC input digests match: earliest published human row in [from,to] with
 * non-neutral sticks/buttons. Prefer this over local predicted-usage flags for FC reanchor so both
 * peers pick the same mismatch tick on analog onset (see netplay_predict_fc_asymmetric_onset).
 */
u32 syNetInputFindEarliestHumanNonNeutralInSpan(u32 from_tick, u32 to_tick)
{
	u32 t;
	s32 player;
	SYNetInputFrame frame;

	if (from_tick > to_tick)
	{
		return ~(u32)0;
	}
	for (t = from_tick; t <= to_tick; t++)
	{
		for (player = 0; player < MAXCONTROLLERS; player++)
		{
			if (syNetInputGetHistoryFrame(player, t, &frame) == FALSE)
			{
				continue;
			}
			if (syNetInputFrameStickGameplayNeutral(&frame) == FALSE)
			{
				return t;
			}
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
 * requires **every remote human slot** to have a **strict** RemoteConfirmed ring cell at
 * **`wire_base = sim_tick + D`** (`syNetPeerRemoteInputsPresentForWireTick` → `HasRemoteInputForWireTick`),
 * or admits via the bounded phase_lock prediction window (`uses_prediction`).
 * Provisional `RemoteGapFilled` (auth-frontier / feel-0 runway) raises `hr` but does **not** satisfy ring_ready —
 * otherwise shared commit takes the confirmed path and FuncRead's `WireReady` (strict/ledger) R-stalls.
 * `hr` from `syNetPeerGetHighestRemoteTick()` is the highest **wire index** seen in ingress (not sim tick).
 * **`wire_strict = wire_base + strict_slack`** (`SSB64_NETPLAY_STRICT_SLACK_FRAMES` et al., capped 0..4) caps how far
 * `syNetPeerEffectiveWireFrontierFromHr` may sit ahead when resolving frames elsewhere (`syNetPeerIsRemoteInputReadyForSimTickEx`);
 * the shared-commit gate itself keys off `wire_base` only.
 * On rollback sessions, after shared `advance`, FuncRead must **not** re-apply skew lockstep or require confirmed
 * remote history when `uses_prediction` — resolve publishes hold-last tagged `RemotePredicted` and rollback corrects
 * on mismatch. When rollback has used predicted remote input and arms recovery, `syNetRollbackPredictionRecoveryRequiresConfirmed`
 * returns TRUE until `sim_tick` reaches `frontier + PHASE_LOCK_PREDICTION_TICKS` — then missing confirmed stalls as **R**
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
		/*
		 * Rollback sessions: shared commit already decided advance (confirmed ring or phase_lock prediction).
		 * Do not re-impose skew lockstep or confirmed-only wire after a predict-approved advance — that made
		 * EvaluateSharedCommitStep prediction a no-op and forced delay-sized soft lockstep (D=1 soak).
		 * Skew must not suppress battle updates on rollback (docs/netplay_phase_lock.md).
		 * Prediction-recovery windows still require confirmed remote rows.
		 */
		if (syNetSessionParamsRollbackEnabled() != FALSE)
		{
			sb32 need_confirmed_wire;

			/*
			 * Confirmed ring path: still verify published remote history is present (belt-and-suspenders).
			 * Predict path: allow speculative hold-last (tagged RemotePredicted) unless recovery demands confirmed.
			 */
			need_confirmed_wire = (shared.uses_prediction == FALSE) ? TRUE : FALSE;
			if ((shared.uses_prediction != FALSE) &&
			    (syNetRollbackPredictionRecoveryRequiresConfirmed(tick) != FALSE))
			{
				need_confirmed_wire = TRUE;
			}
			if ((need_confirmed_wire != FALSE) &&
			    (syNetInputRemoteHumanWireReadyForSimTick(tick) == FALSE))
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
		/* Non-rollback: keep legacy skew + confirmed-wire lockstep after shared advance. */
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
		 *
		 * NETMENU: wall-rate FIFO — poll on every FuncRead (including same-tick stalls); each new sim tick pops one
		 * sample so stick trajectory during rare R-holds is preserved (vanilla shape, not peak-hold).
		 */
		if (tick != sSYNetInputPortHwLatchTick)
		{
#if defined(SSB64_NETMENU)
			syNetInputConsumeHardwareLatchForSimTick(tick);
#else
			syControllerFuncRead();
			memcpy(sSYNetInputHardwareLatch, gSYControllerDevices, sizeof(SYController) * (size_t)MAXCONTROLLERS);
			syNetInputStageLocalDelayFramesFromLatch(tick);
			syNetInputNeutralizeAllControllerDevices();
			sSYNetInputPortHwLatchTick = tick;
#endif
		}
#if defined(SSB64_NETMENU)
		else
		{
			/* Same sim tick held (admission R/V/…): keep capturing wall-rate HID into the FIFO. */
			syNetInputPollHardwareCaptureFifo();
		}
#endif
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
#if defined(PORT) && defined(SSB64_NETMENU)
	/*
	 * Pre-rewind sim may have already latched exclusive-target+ ticks. Force FuncRead to restage
	 * after exit so wire-locked StoreLocalDelay (above) can re-pin gameplay from TransmittedHistory
	 * instead of silently keeping a stale PortHwLatchTick and skipping consume.
	 */
	sSYNetInputPortHwLatchTick = ~(u32)0;
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
		SYNETINPUT_STRICT_TAG("resim_wire");
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
			SYNETINPUT_STRICT_TAG("resim_wire_pred");
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
			SYNETINPUT_STRICT_TAG("post_resim_wire");
			syNetInputStoreFrame(sSYNetInputHistory, slot, &wire);
		}
	}
}

static void syNetInputRollbackReconcileLocalSlotForResim(s32 slot, u32 t)
{
	SYNetInputFrame row;

	/*
	 * Only restage gameplay-authoritative samples into History. Never Resolve→latch
	 * (soak1 67923985: reconcile could mint Local+!predicted from live HID into a gap
	 * tick, freeze it, then seal as auth_history). Gap ticks stay empty for seal gap_hold.
	 */
#if defined(SSB64_NETMENU)
	if (syNetInputGetLocalGameplayFrame(slot, t, &row) != FALSE)
	{
		row.tick = t;
		row.source = nSYNetInputSourceLocal;
		row.is_predicted = FALSE;
		row.is_valid = TRUE;
		SYNETINPUT_PROVENANCE_TAG(nSYNetInputHistoryProvGameplay, "resim_reconcile_gameplay");
		syNetInputStoreFrame(sSYNetInputHistory, slot, &row);
		return;
	}
	if (syNetInputGetStoredFrame(sSYNetInputTransmittedHistory, slot, t, &row) != FALSE)
	{
		row.tick = t;
		row.source = nSYNetInputSourceLocal;
		row.is_predicted = FALSE;
		row.is_valid = TRUE;
		SYNETINPUT_PROVENANCE_TAG(nSYNetInputHistoryProvLocalPublish, "resim_reconcile_tx");
		syNetInputStoreFrame(sSYNetInputHistory, slot, &row);
	}
#else
	if (syNetInputResolveLocalAuthorityFrame(slot, t, &row) != FALSE)
	{
		syNetInputStoreFrame(sSYNetInputHistory, slot, &row);
	}
#endif
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

#if defined(SSB64_NETMENU)
static u16 syNetInputExpandControllerButtonHold(u16 button_hold)
{
	if (button_hold & R_TRIG)
	{
		button_hold |= (A_BUTTON | Z_TRIG);
	}
	return button_hold;
}

static void syNetInputRollbackReconcileRemoteSlotFromSealed(s32 slot, u32 t)
{
	SYNetInputFrame row;

	/*
	 * Prefer strict wire when present so seal-stamped wrong sticks (publish_frame downgrade
	 * before seal) cannot fabricate confirm. Fall back to episode seal rows.
	 * See docs/bugs/netplay_confirmed_publish_write_once_2026-07-12.md.
	 */
	if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(slot, t, &row) != FALSE)
	{
		row.tick = t;
		row.source = nSYNetInputSourceRemoteConfirmed;
		row.is_predicted = FALSE;
		SYNETINPUT_STRICT_TAG("resim_wire");
		syNetInputStoreFrame(sSYNetInputHistory, slot, &row);
		return;
	}
	if (syNetRollbackEpisodeGetSealedFrame(slot, t, &row) != FALSE)
	{
		row.tick = t;
		row.source = nSYNetInputSourceRemoteConfirmed;
		row.is_predicted = FALSE;
		SYNETINPUT_STRICT_TAG("resim_sealed");
		syNetInputStoreFrame(sSYNetInputHistory, slot, &row);
	}
}

/*
 * Post-resim: gSYControllerDevices may already hold the exclusive-target tick (wire-lock republish
 * for local slots runs just before this). ftMainProcessInput always does
 *   pl->stick_prev = pl->stick_range;  then  pl->stick_range = controller;
 * so pl->stick_range on entry MUST be the previous tick's stick — not the current device.
 * Setting stick_range from the already-republished target device made local-auth peers skip the
 * |stick|>=20 deadzone reset (soak 699967527: Linux tap_x stayed 254 at exclusive 631 while
 * Android remote reset to 1 → fhash_light FC@640). Mirror button_hold from prev history so
 * tap/release edges on the exclusive tick are not suppressed.
 */
static void syNetInputRollbackResyncFighterPlLatchFromControllers(u32 sim_tick)
{
	GObj *fighter_gobj;
	u32 prev_tick;

	if (sim_tick == 0U)
	{
		prev_tick = 0U;
	}
	else
	{
		prev_tick = sim_tick - 1U;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		FTPlayerInput *pl;
		SYController *controller;
		SYNetInputFrame prev_frame;
		s32 player;
		s32 stick_x;
		s32 stick_y;
		u16 button_hold;

		fp = ftGetStruct(fighter_gobj);
		if ((fp == NULL) || (fp->pkind != nFTPlayerKindMan) || (fp->is_control_disable != FALSE))
		{
			continue;
		}
		player = fp->player;
		if ((player < 0) || (player >= MAXCONTROLLERS))
		{
			continue;
		}
		controller = &gSYControllerDevices[player];
		pl = &fp->input.pl;
		if ((prev_tick < sim_tick) && (syNetInputGetHistoryFrame(player, prev_tick, &prev_frame) != FALSE))
		{
			stick_x = (s32)prev_frame.stick_x;
			stick_y = (s32)prev_frame.stick_y;
			button_hold = syNetInputExpandControllerButtonHold(prev_frame.buttons);
		}
		else
		{
			stick_x = (s32)controller->stick_range.x;
			stick_y = (s32)controller->stick_range.y;
			button_hold = syNetInputExpandControllerButtonHold(controller->button_hold);
		}
		if (stick_x > I_CONTROLLER_RANGE_MAX)
		{
			stick_x = I_CONTROLLER_RANGE_MAX;
		}
		if (stick_x < -I_CONTROLLER_RANGE_MAX)
		{
			stick_x = -I_CONTROLLER_RANGE_MAX;
		}
		if (stick_y > I_CONTROLLER_RANGE_MAX)
		{
			stick_y = I_CONTROLLER_RANGE_MAX;
		}
		if (stick_y < -I_CONTROLLER_RANGE_MAX)
		{
			stick_y = -I_CONTROLLER_RANGE_MAX;
		}
		pl->stick_prev.x = (s8)stick_x;
		pl->stick_prev.y = (s8)stick_y;
		/* ProcessInput copies stick_range → stick_prev before reading the device. */
		pl->stick_range.x = (s8)stick_x;
		pl->stick_range.y = (s8)stick_y;
		pl->button_hold = button_hold;
		pl->button_tap = 0;
		pl->button_release = 0;
	}
}
#endif

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

void syNetInputRollbackResyncControllersAfterResim(u32 mismatch_tick, u32 target_tick)
{
	u32 seed_tick;
	s32 player;
	SYNetInputFrame frame;
#if defined(SSB64_NETMENU)
	u32 restore_end;
	u32 frontier;
	u32 t;
	s32 local_slot;
	s32 extra_slot;
#endif

	if ((mismatch_tick == 0U) || (mismatch_tick == ~(u32)0U) || (target_tick == 0U) || (target_tick == ~(u32)0U) ||
	    (target_tick <= mismatch_tick))
	{
		return;
	}
	(void)mismatch_tick;
	seed_tick = (target_tick > 0U) ? (target_tick - 1U) : 0U;
	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		if (syNetInputGetHistoryFrame(player, seed_tick, &frame) != FALSE)
		{
			sSYNetInputSlots[player].last_published = frame;
		}
		else if (syNetInputGetHistoryFrame(player, target_tick, &frame) != FALSE)
		{
			sSYNetInputSlots[player].last_published = frame;
		}
		else
		{
			syNetInputClearFrame(&sSYNetInputSlots[player].last_published);
		}
		gSYControllerDevices[player].button_tap = 0;
		gSYControllerDevices[player].button_release = 0;
	}
#if defined(SSB64_NETMENU)
	/*
	 * Exclusive target is the first live tick after replay. Re-pin feel-0 gameplay from wire-locked
	 * transmitted OR published History for target..frontier so MakeLocalFrame cannot sim a FIFO
	 * restage that peers never saw (host LOCAL_PUBLISH vs Android REMOTE_PUBLISH split). History
	 * covers LOCAL_PUBLISH during tick_commit blocked before egress NoteTransmit (soak1 1842112848).
	 */
	restore_end = target_tick + 1U;
	frontier = syNetInputGetTick();
	if ((frontier != ~(u32)0) && ((frontier + 1U) > restore_end))
	{
		restore_end = frontier + 1U;
	}
	local_slot = syNetPeerGetLocalSimSlot();
	extra_slot = syNetPeerGetExtraLocalSenderSimSlot();
	for (t = target_tick; t < restore_end; t++)
	{
		for (player = 0; player < MAXCONTROLLERS; player++)
		{
			if ((player != local_slot) && (player != extra_slot))
			{
				continue;
			}
			if (syNetInputTryGetLocalWireLockedSample(player, t, &frame) == FALSE)
			{
				continue;
			}
			frame.tick = t;
			frame.source = nSYNetInputSourceLocal;
			frame.is_predicted = FALSE;
			frame.is_valid = TRUE;
			syNetInputStoreFrame(sSYNetInputLocalGameplayHistory, player, &frame);
			syNetInputStoreFrame(sSYNetInputLocalDelayHistory, player, &frame);
			syNetInputStoreFrame(sSYNetInputTransmittedHistory, player, &frame);
			syNetInputPublishFrame(player, &frame);
		}
	}
	syNetInputRollbackResyncFighterPlLatchFromControllers(target_tick);
#endif
	syNetInputPublishMainController();
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
#if defined(SSB64_NETMENU)
				if (syNetRollbackEpisodeInputsSealed() != FALSE)
				{
					syNetInputRollbackReconcileRemoteSlotFromSealed(slot, t);
				}
#endif
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
#if defined(PORT) && defined(SSB64_NETMENU)
		SYNETINPUT_PROVENANCE_TAG(nSYNetInputHistoryProvLocalPublish, "peer_symmetric_tx");
#endif
		syNetInputStoreFrame(sSYNetInputHistory, authority_slot, &tx);
	}
	syNetInputStrictReadyCacheInvalidate();
}

void syNetInputStorePublishedHistoryFrame(s32 player, const SYNetInputFrame *frame)
{
	SYNetInputFrame store;
#if defined(PORT) && defined(SSB64_NETMENU)
	SYNetInputFrame existing;
#endif

	if ((frame == NULL) || (syNetInputCheckPlayer(player) == FALSE))
	{
		return;
	}
	store = *frame;
#if defined(PORT) && defined(SSB64_NETMENU)
	/*
	 * Same write-once as promote/patch/publish: store_published_api must not confirm_downgrade
	 * a sealed remote row (soak 1468769950 mid-resim). Mechanical resim_wire* uses StoreFrame.
	 * Sealed-episode rows pass write-once (SEAL_OVERRIDE) and dual-write the authority ledger.
	 */
	if ((syNetInputIsRemoteHumanSlot(player) != FALSE) && (store.tick != 0U) &&
	    (syNetInputGetHistoryFrame(player, store.tick, &existing) != FALSE) &&
	    (syNetInputRemoteConfirmedWriteOnceBlocks(player, &existing, &store, "store_published_api") != FALSE))
	{
		syNetInputRemoteConfirmedWriteOnceQueueCorrection(player, store.tick, &existing);
		return;
	}
	if ((syNetInputIsRemoteHumanSlot(player) != FALSE) && (store.tick != 0U) &&
	    (syNetInputFrameMatchesSealedEpisodeRow(player, store.tick, &store) != FALSE))
	{
		syNetInputAuthorityLedgerCommitSeal(player, store.tick, &store);
	}
#endif
	SYNETINPUT_STRICT_TAG("store_published_api");
#if defined(PORT) && defined(SSB64_NETMENU)
	/*
	 * Remote human slots: CommitPromote / seal apply may arrive with source=Local.
	 * Normalize to RemoteConfirmed so provenance mint gate does not MINT_DOWNGRADE
	 * real remote sticks (soak1 256718957 hygiene). Local slots keep gameplay gate.
	 */
	if (syNetInputIsRemoteHumanSlot(player) != FALSE)
	{
		if (store.is_predicted != FALSE)
		{
			SYNETINPUT_PROVENANCE_TAG(nSYNetInputHistoryProvPrediction, "store_published_api");
		}
		else
		{
			store.source = nSYNetInputSourceRemoteConfirmed;
			SYNETINPUT_PROVENANCE_TAG(nSYNetInputHistoryProvRemoteConfirmed, "store_published_api");
		}
	}
	else if (store.is_predicted != FALSE)
	{
		SYNETINPUT_PROVENANCE_TAG(nSYNetInputHistoryProvPrediction, "store_published_api");
	}
	else if (store.source == nSYNetInputSourceLocal)
	{
		SYNetInputFrame gameplay;

		if ((syNetInputGetLocalGameplayFrame(player, store.tick, &gameplay) != FALSE) &&
		    (syNetInputFrameGameplayEquals(&gameplay, &store) != FALSE))
		{
			SYNETINPUT_PROVENANCE_TAG(nSYNetInputHistoryProvLocalPublish, "store_published_api");
		}
		else
		{
			SYNETINPUT_PROVENANCE_TAG(nSYNetInputHistoryProvNone, "store_published_api");
		}
	}
#endif
	syNetInputStoreFrame(sSYNetInputHistory, player, &store);
	syNetInputStrictReadyCacheInvalidate();
}

sb32 syNetInputCopyEpisodeLocalAuthoritySealFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	SYNetInputFrame row;
	SYNetInputFrame tx;
	SYNetInputFrame history;
	SYNetInputFrame gameplay;
	sb32 have_tx;
	sb32 have_auth;
	sb32 have_gameplay;
	u32 back;
	const char *src_tag;

	if ((out_frame == NULL) || (syNetInputCheckPlayer(player) == FALSE))
	{
		return FALSE;
	}
#if defined(PORT) && defined(SSB64_NETMENU)
	/*
	 * Seal consumes gameplay-authoritative rows only — never mutable prediction / live HID.
	 * Order: transmitted → History with GAMEPLAY/LOCAL_PUBLISH provenance → gameplay ring →
	 * gap_hold from last auth/tx neighbor. Never latch; Local&&!predicted alone is not auth
	 * (soak1 67923985). See netplay_history_provenance_2026-07-20.md.
	 */
	have_tx = syNetInputGetStoredFrame(sSYNetInputTransmittedHistory, player, tick, &tx);
	/*
	 * Gameplay-authoritative History only — not Local&&!predicted (soak1 67923985:
	 * poison auth_history@436 without LOCAL_PUBLISH). Transmission corroborates but
	 * gameplay ring / LOCAL_PUBLISH provenance defines authority.
	 */
	have_auth = ((syNetInputGetHistoryFrame(player, tick, &history) != FALSE) &&
	             (syNetInputLocalHistoryGameplayAuth(player, &history) != FALSE))
	                ? TRUE
	                : FALSE;
	have_gameplay = syNetInputGetLocalGameplayFrame(player, tick, &gameplay);
	src_tag = NULL;
	if (have_tx != FALSE)
	{
		row = tx;
		src_tag = "transmitted";
	}
	else if (have_auth != FALSE)
	{
		row = history;
		src_tag = "auth_history";
	}
	else if (have_gameplay != FALSE)
	{
		row = gameplay;
		src_tag = "gameplay";
	}
	else
	{
		/* Gap: derive from last gameplay-auth / transmitted ≤ tick-1. */
		for (back = 1U; (back <= 16U) && (back < tick); back++)
		{
			u32 prev = tick - back;

			if (syNetInputGetStoredFrame(sSYNetInputTransmittedHistory, player, prev, &tx) != FALSE)
			{
				row = tx;
				src_tag = "gap_hold_tx";
				port_log(
				    "SSB64 NetInput: SEAL_PACK_GAP_HOLD player=%d tick=%u from=%u src=transmitted "
				    "sx=%d sy=%d\n",
				    (int)player, (unsigned int)tick, (unsigned int)prev, (int)row.stick_x,
				    (int)row.stick_y);
				break;
			}
			if ((syNetInputGetHistoryFrame(player, prev, &history) != FALSE) &&
			    (syNetInputLocalHistoryGameplayAuth(player, &history) != FALSE))
			{
				row = history;
				src_tag = "gap_hold_auth";
				port_log(
				    "SSB64 NetInput: SEAL_PACK_GAP_HOLD player=%d tick=%u from=%u src=auth_history "
				    "sx=%d sy=%d\n",
				    (int)player, (unsigned int)tick, (unsigned int)prev, (int)row.stick_x,
				    (int)row.stick_y);
				break;
			}
		}
		if (src_tag == NULL)
		{
			port_log("SSB64 NetInput: SEAL_PACK_SKIP_NO_AUTH player=%d tick=%u\n", (int)player,
			         (unsigned int)tick);
			return FALSE;
		}
	}
	*out_frame = row;
	out_frame->tick = tick;
	out_frame->source = nSYNetInputSourceLocal;
	out_frame->is_predicted = FALSE;
	out_frame->is_valid = TRUE;
	port_log("SSB64 NetInput: SEAL_PACK player=%d tick=%u src=%s btn=0x%04X sx=%d sy=%d\n", (int)player,
	         (unsigned int)tick, (src_tag != NULL) ? src_tag : "?", (unsigned int)out_frame->buttons,
	         (int)out_frame->stick_x, (int)out_frame->stick_y);
	return TRUE;
#else
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
#endif
}

sb32 syNetInputCopyEpisodeRemoteAuthoritySealFrame(s32 player, u32 tick, SYNetInputFrame *out_frame)
{
	SYNetInputFrame row;

	if ((out_frame == NULL) || (syNetInputCheckPlayer(player) == FALSE) ||
	    (syNetInputIsRemoteHumanSlot(player) == FALSE))
	{
		return FALSE;
	}
#if defined(PORT) && defined(SSB64_NETMENU)
	/*
	 * Prefer ledger / confirmed wire over Resolve hold-last so episode seal does not freeze a
	 * predicted (0,0) or stale smash sign that ledger already corrected (soak1 1876984747
	 * SEALED_RESIM_LOAD_NEUTRAL sealed=0,0 vs ledger nonzero during dash-dance storms).
	 */
	if (syNetInputAuthorityLedgerTryGet(player, tick, &row, NULL) != FALSE)
	{
		*out_frame = row;
		out_frame->tick = tick;
		out_frame->source = nSYNetInputSourceRemoteConfirmed;
		out_frame->is_predicted = FALSE;
		out_frame->is_valid = TRUE;
		return TRUE;
	}
	if (syNetInputTryGetRemoteConfirmedHistoryForSimTick(player, tick, &row) != FALSE)
	{
		*out_frame = row;
		out_frame->tick = tick;
		out_frame->source = nSYNetInputSourceRemoteConfirmed;
		out_frame->is_predicted = FALSE;
		out_frame->is_valid = TRUE;
		return TRUE;
	}
#endif
	if (syNetInputResolveRemoteHumanAuthorityFrameEx(player, tick, &row, NULL) != FALSE)
	{
#if defined(PORT) && defined(SSB64_NETMENU)
		/*
		 * Resolve hold-last can still invent (0,0) before ledger/wire catch up. Prefer any
		 * provisional ring row with non-neutral sticks over sealing predicted neutral
		 * (soak1 179193526 SEALED_RESIM_LOAD_NEUTRAL host@493).
		 */
		if (syNetInputFrameSticksNearNeutral(&row) != FALSE)
		{
			SYNetInputFrame ring;

			if ((syNetInputTryGetRemoteHistoryForSimTick(player, tick, &ring) != FALSE) &&
			    (syNetInputFrameSticksNearNeutral(&ring) == FALSE))
			{
				row = ring;
			}
		}
#endif
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
