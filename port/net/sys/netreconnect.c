#include "netreconnect.h"

#if defined(PORT) && defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE)

#include <sys/netpeer.h>
#include <sys/netinput.h>
#include <sys/netsession_params.h>
#include <sc/scmanager.h>
#include "android_network.h"
#include <sc/scdef.h>
#include <sc/sctypes.h>
#include <gr/grdef.h>
#include <if/ifcommon.h>
#include "../sc/sccommon/scvsbattle_reconnect.h"
#include "mm_ice.h"
#include "mm_ice_reconnect.h"
#include "mm_matchmaking.h"
#include "port_log.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern char *getenv(const char *name);
extern int atoi(const char *s);

typedef enum SYNetReconnectPhase
{
	SY_NET_RECONNECT_IDLE = 0,
	SY_NET_RECONNECT_HOLD_PENDING,
	SY_NET_RECONNECT_HOLD_ACTIVE,
	SY_NET_RECONNECT_ICE_RECYCLING,
	SY_NET_RECONNECT_READY_PENDING,
	SY_NET_RECONNECT_FORFEIT_PENDING,
} SYNetReconnectPhase;

static SYNetReconnectPhase sSYNetReconnectPhase = SY_NET_RECONNECT_IDLE;
static u32 sSYNetReconnectHoldSimTick;
static u32 sSYNetReconnectHoldEpoch;
static u8 sSYNetReconnectPausingSlot;
static u8 sSYNetReconnectDisconnectSlot;
static u32 sSYNetReconnectGraceFrames;
static u32 sSYNetReconnectDetectBadFrames;
static sb32 sSYNetReconnectLocalInitiated;
static sb32 sSYNetReconnectPauseEntered;
static sb32 sSYNetReconnectResultPosted;
static sb32 sSYNetReconnectForfeitApplied;
static u32 sSYNetReconnectHoldRetransmitCooldown;
static u32 sSYNetReconnectNetworkDebounceCooldown;
static sb32 sSYNetReconnectEnvEnabled = -1;
static sb32 sSYNetReconnectOverlayEnv = -1;
static sb32 sSYNetReconnectTransportArmed = FALSE;
static u32 sSYNetReconnectArmBootTicksCache = ~(u32)0;
#define SY_NET_RECONNECT_ARM_BOOT_TICKS_DEFAULT 60U

extern sb32 sSYNetPeerBootstrapIsHost;
extern s32 sSYNetPeerLocalPlayer;
extern s32 sSYNetPeerRemotePlayer;

extern void syNetPeerSendReconnectHold(u32 sim_tick, u32 epoch, u8 pausing_slot, u8 reason);
extern void syNetPeerSendReconnectReady(u32 sim_tick, u32 epoch);
extern void syNetPeerSendReconnectAck(u32 sim_tick, u32 epoch);
extern void syNetPeerSendReconnectForfeit(u32 sim_tick, u8 forfeiting_slot, u8 winner_slot);
extern sb32 syNetPeerIsIceTransportActive(void);
extern sb32 syNetPeerGetAutomatchPeerPlayerId(char *peer_player_id_out, u32 cap);

static sb32 syNetReconnectEnvFlag(sb32 *cache, const char *name, sb32 default_on)
{
	const char *env;

	if (*cache != -1)
	{
		return (*cache != 0) ? TRUE : FALSE;
	}
	env = getenv(name);
	if ((env == NULL) || (env[0] == '\0'))
	{
		*cache = default_on ? 1 : 0;
	}
	else
	{
		*cache = (atoi(env) != 0) ? 1 : 0;
	}
	return (*cache != 0) ? TRUE : FALSE;
}

static u32 syNetReconnectGraceLimit(void)
{
	const char *env;
	s32 parsed;

	env = getenv("SSB64_NETPLAY_RECONNECT_GRACE_FRAMES");
	if ((env == NULL) || (env[0] == '\0'))
	{
		return 1800U;
	}
	parsed = atoi(env);
	return (parsed > 0) ? (u32)parsed : 1800U;
}

static u32 syNetReconnectDetectLimit(void)
{
	const char *env;
	s32 parsed;

	env = getenv("SSB64_NETPLAY_RECONNECT_DETECT_FRAMES");
	if ((env == NULL) || (env[0] == '\0'))
	{
		return 30U;
	}
	parsed = atoi(env);
	return (parsed > 0) ? (u32)parsed : 30U;
}

sb32 syNetReconnectEnabled(void)
{
	return syNetReconnectEnvFlag(&sSYNetReconnectEnvEnabled, "SSB64_NETPLAY_RECONNECT", TRUE);
}

static u32 syNetReconnectArmBootTicks(void)
{
	const char *env;
	s32 parsed;

	if (sSYNetReconnectArmBootTicksCache != ~(u32)0)
	{
		return sSYNetReconnectArmBootTicksCache;
	}
	env = getenv("SSB64_NETPLAY_RECONNECT_ARM_BOOT_TICKS");
	if ((env == NULL) || (env[0] == '\0'))
	{
		sSYNetReconnectArmBootTicksCache = SY_NET_RECONNECT_ARM_BOOT_TICKS_DEFAULT;
	}
	else
	{
		parsed = atoi(env);
		sSYNetReconnectArmBootTicksCache =
		    (parsed > 0) ? (u32)parsed : SY_NET_RECONNECT_ARM_BOOT_TICKS_DEFAULT;
	}
	return sSYNetReconnectArmBootTicksCache;
}

static sb32 syNetReconnectMidMatchBaseReady(void)
{
	u32 sim_tick;
	u32 exec_mark;
	u32 boot_ticks;

	if (syNetReconnectEnabled() == FALSE)
	{
		return FALSE;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return FALSE;
	}
	if (syNetPeerIsBootstrapRunInProgress() != FALSE)
	{
		return FALSE;
	}
	if (syNetPeerCheckBattleExecutionReady() == FALSE)
	{
		return FALSE;
	}
	if (syNetPeerBootstrapIngressSymmetrySatisfied() == FALSE)
	{
		return FALSE;
	}
	if (syNetSessionParamsAreNegotiated() == FALSE)
	{
		return FALSE;
	}
	if (gSCManagerSceneData.scene_curr != nSCKindVSBattle)
	{
		return FALSE;
	}
	if (gSCManagerBattleState == NULL)
	{
		return FALSE;
	}
	if (gSCManagerBattleState->game_status != nSCBattleGameStatusGo)
	{
		return FALSE;
	}
	exec_mark = syNetPeerGetDelaySyncDiagExecReadySimTick();
	if (exec_mark == ~(u32)0)
	{
		return FALSE;
	}
	sim_tick = syNetInputGetTick();
	if (sim_tick < exec_mark)
	{
		return FALSE;
	}
	boot_ticks = syNetReconnectArmBootTicks();
	if ((sim_tick - exec_mark) < boot_ticks)
	{
		return FALSE;
	}
	return TRUE;
}

static sb32 syNetReconnectTryLatchTransportArm(void)
{
	s32 i;
	s32 n;
	s32 slot;
	u32 wire_base;

	if (sSYNetReconnectTransportArmed != FALSE)
	{
		return TRUE;
	}
	if (syNetReconnectMidMatchBaseReady() == FALSE)
	{
		return FALSE;
	}
	n = syNetPeerGetRemoteHumanSlotCount();
	if (n <= 0)
	{
		return FALSE;
	}
	wire_base = syNetPeerGetBaseRequiredWireTick(syNetInputGetTick());
	for (i = 0; i < n; i++)
	{
		if (syNetPeerGetRemoteHumanSlotByIndex(i, &slot) == FALSE)
		{
			return FALSE;
		}
		if (syNetInputHasRemoteInputForWireTick(slot, wire_base) == FALSE)
		{
			return FALSE;
		}
	}
	sSYNetReconnectTransportArmed = TRUE;
	port_log("SSB64 Reconnect: transport armed tick=%u wire_base=%u remote_slots=%d\n",
	         (unsigned int)syNetInputGetTick(), (unsigned int)wire_base, (int)n);
	port_android_network_try_arm_monitoring();
	return TRUE;
}

void syNetReconnectPollTransportArm(void)
{
	(void)syNetReconnectTryLatchTransportArm();
}

sb32 syNetReconnectMidMatchEligible(void)
{
	if (syNetReconnectMidMatchBaseReady() == FALSE)
	{
		return FALSE;
	}
	if (sSYNetReconnectTransportArmed == FALSE)
	{
		(void)syNetReconnectTryLatchTransportArm();
	}
	return (sSYNetReconnectTransportArmed != FALSE) ? TRUE : FALSE;
}

sb32 syNetReconnectBattleEligible(void)
{
	return syNetReconnectMidMatchEligible();
}

static sb32 syNetReconnectPauseSlotSafe(s32 player)
{
	if ((player < 0) || (player >= MAXCONTROLLERS))
	{
		return FALSE;
	}
	if (gSCManagerBattleState->players[player].pkind == nFTPlayerKindNot)
	{
		return TRUE;
	}
	if ((gSCManagerBattleState->gkind == nGRKindBonus3) &&
	    (gSCManagerBattleState->players[player].pkind == nFTPlayerKindCom))
	{
		return TRUE;
	}
	return (gSCManagerBattleState->players[player].fighter_gobj != NULL) ? TRUE : FALSE;
}

sb32 syNetReconnectOverlayEnabled(void)
{
	return syNetReconnectEnvFlag(&sSYNetReconnectOverlayEnv, "SSB64_NETPLAY_RECONNECT_OVERLAY", TRUE);
}

sb32 syNetReconnectHoldActive(void)
{
	return (sSYNetReconnectPhase == SY_NET_RECONNECT_HOLD_PENDING) ||
	       (sSYNetReconnectPhase == SY_NET_RECONNECT_HOLD_ACTIVE) ||
	       (sSYNetReconnectPhase == SY_NET_RECONNECT_ICE_RECYCLING) ||
	       (sSYNetReconnectPhase == SY_NET_RECONNECT_READY_PENDING);
}

sb32 syNetReconnectBlocksUnpause(void)
{
	if (syNetReconnectHoldActive() != FALSE)
	{
		return TRUE;
	}
	if (sSYNetReconnectPhase == SY_NET_RECONNECT_FORFEIT_PENDING)
	{
		return TRUE;
	}
	return FALSE;
}

sb32 syNetReconnectShouldPreserveAutomatchContext(void)
{
	if (syNetReconnectEnabled() == FALSE)
	{
		return FALSE;
	}
	return (sSYNetReconnectPhase != SY_NET_RECONNECT_IDLE) || (sSYNetReconnectForfeitApplied != FALSE);
}

u32 syNetReconnectGraceFramesRemaining(void)
{
	return sSYNetReconnectGraceFrames;
}

void syNetReconnectReset(void)
{
	sSYNetReconnectPhase = SY_NET_RECONNECT_IDLE;
	sSYNetReconnectHoldSimTick = 0U;
	sSYNetReconnectHoldEpoch = 0U;
	sSYNetReconnectPausingSlot = 0U;
	sSYNetReconnectDisconnectSlot = 0xFFU;
	sSYNetReconnectGraceFrames = 0U;
	sSYNetReconnectDetectBadFrames = 0U;
	sSYNetReconnectLocalInitiated = FALSE;
	sSYNetReconnectPauseEntered = FALSE;
	sSYNetReconnectHoldRetransmitCooldown = 0U;
	sSYNetReconnectNetworkDebounceCooldown = 0U;
	sSYNetReconnectTransportArmed = FALSE;
}

void syNetReconnectShutdown(void)
{
	port_android_network_disarm_monitoring();
	mmIceReconnectShutdown();
	syNetReconnectReset();
	sSYNetReconnectResultPosted = FALSE;
	sSYNetReconnectForfeitApplied = FALSE;
}

static void syNetReconnectEnterHoldActive(void)
{
	if (sSYNetReconnectPhase == SY_NET_RECONNECT_HOLD_ACTIVE)
	{
		return;
	}
	sSYNetReconnectPhase = SY_NET_RECONNECT_HOLD_ACTIVE;
	sSYNetReconnectGraceFrames = syNetReconnectGraceLimit();
	if (sSYNetReconnectPauseEntered == FALSE)
	{
		if (syNetReconnectPauseSlotSafe((s32)sSYNetReconnectPausingSlot) != FALSE)
		{
			(void)ifCommonBattlePauseSetupFromPlayer((s32)sSYNetReconnectPausingSlot);
			ifCommonBattlePauseInitInterface((s32)sSYNetReconnectPausingSlot);
		}
		sSYNetReconnectPauseEntered = TRUE;
	}
	port_log("SSB64 Reconnect: hold active tick=%u epoch=%u pausing=%u grace=%u\n",
	         (unsigned int)sSYNetReconnectHoldSimTick, (unsigned int)sSYNetReconnectHoldEpoch,
	         (unsigned int)sSYNetReconnectPausingSlot, (unsigned int)sSYNetReconnectGraceFrames);
	mmIceReconnectBegin(sSYNetReconnectHoldEpoch + 1U);
	sSYNetReconnectPhase = SY_NET_RECONNECT_ICE_RECYCLING;
}

static void syNetReconnectBeginHold(u8 disconnect_slot, sb32 local_initiated)
{
	u32 tick;

	if ((syNetReconnectEnabled() == FALSE) || (syNetPeerIsIceTransportActive() == FALSE) ||
	    (syNetReconnectMidMatchEligible() == FALSE))
	{
		return;
	}
	if (syNetReconnectHoldActive() != FALSE)
	{
		return;
	}
	tick = syNetInputGetTick();
	sSYNetReconnectHoldSimTick = tick;
	sSYNetReconnectHoldEpoch++;
	sSYNetReconnectPausingSlot = disconnect_slot;
	sSYNetReconnectDisconnectSlot = disconnect_slot;
	sSYNetReconnectLocalInitiated = local_initiated;
	sSYNetReconnectPauseEntered = FALSE;
	sSYNetReconnectPhase = SY_NET_RECONNECT_HOLD_PENDING;
	sSYNetReconnectDetectBadFrames = 0U;
	syNetPeerSendReconnectHold(tick, sSYNetReconnectHoldEpoch, disconnect_slot, 1U);
	port_log("SSB64 Reconnect: hold pending tick=%u epoch=%u slot=%u local=%d\n", (unsigned int)tick,
	         (unsigned int)sSYNetReconnectHoldEpoch, (unsigned int)disconnect_slot, (local_initiated != FALSE) ? 1 : 0);
	syNetReconnectEnterHoldActive();
}

static void syNetReconnectPostForfeitResult(s32 forfeiting_slot, s32 winner_slot)
{
	char match_id[72];
	char ticket_id[72];
	char winner_id[72];
	char loser_id[72];

	if ((sSYNetReconnectResultPosted != FALSE) || (sSYNetPeerBootstrapIsHost == FALSE))
	{
		return;
	}
	if (syNetPeerGetAutomatchBootstrapContext(match_id, (u32)sizeof(match_id), ticket_id, (u32)sizeof(ticket_id)) ==
	    FALSE)
	{
		return;
	}
	if (mmMatchmakingGetLocalPlayerId(winner_id, (u32)sizeof(winner_id)) == FALSE)
	{
		return;
	}
	if (syNetPeerGetAutomatchPeerPlayerId(loser_id, (u32)sizeof(loser_id)) == FALSE)
	{
		return;
	}
	if (forfeiting_slot == sSYNetPeerLocalPlayer)
	{
		char tmp[72];

		snprintf(tmp, sizeof(tmp), "%s", winner_id);
		snprintf(winner_id, sizeof(winner_id), "%s", loser_id);
		snprintf(loser_id, sizeof(loser_id), "%s", tmp);
	}
	(void)winner_slot;
	mmMatchmakingPostMatchResultSync(match_id, winner_id, loser_id, "forfeit_timeout");
	sSYNetReconnectResultPosted = TRUE;
}

static void syNetReconnectApplyForfeit(u8 forfeiting_slot, u8 winner_slot)
{
	if (sSYNetReconnectForfeitApplied != FALSE)
	{
		return;
	}
	sSYNetReconnectPhase = SY_NET_RECONNECT_FORFEIT_PENDING;
	scVSBattleReconnectApplyForfeit((s32)forfeiting_slot, (s32)winner_slot);
	sSYNetReconnectForfeitApplied = TRUE;
	syNetReconnectPostForfeitResult((s32)forfeiting_slot, (s32)winner_slot);
	port_log("SSB64 Reconnect: forfeit applied forfeiter=%u winner=%u\n", (unsigned int)forfeiting_slot,
	         (unsigned int)winner_slot);
}

static void syNetReconnectMaybeHostForfeitTimeout(void)
{
	u8 forfeiting;
	u8 winner;

	if ((sSYNetReconnectPhase != SY_NET_RECONNECT_HOLD_ACTIVE) &&
	    (sSYNetReconnectPhase != SY_NET_RECONNECT_ICE_RECYCLING) &&
	    (sSYNetReconnectPhase != SY_NET_RECONNECT_READY_PENDING))
	{
		return;
	}
	if (sSYNetPeerBootstrapIsHost == FALSE)
	{
		return;
	}
	if (sSYNetReconnectGraceFrames > 0U)
	{
		sSYNetReconnectGraceFrames--;
		return;
	}
	forfeiting = sSYNetReconnectDisconnectSlot;
	winner = (forfeiting == (u8)sSYNetPeerLocalPlayer) ? (u8)sSYNetPeerRemotePlayer : (u8)sSYNetPeerLocalPlayer;
	syNetPeerSendReconnectForfeit(sSYNetReconnectHoldSimTick, forfeiting, winner);
	syNetReconnectApplyForfeit(forfeiting, winner);
}

static void syNetReconnectClearHold(void)
{
	port_log("SSB64 Reconnect: hold cleared tick=%u epoch=%u\n", (unsigned int)sSYNetReconnectHoldSimTick,
	         (unsigned int)sSYNetReconnectHoldEpoch);
	mmIceReconnectShutdown();
	sSYNetReconnectPhase = SY_NET_RECONNECT_IDLE;
	sSYNetReconnectDisconnectSlot = 0xFFU;
	sSYNetReconnectGraceFrames = 0U;
	sSYNetReconnectDetectBadFrames = 0U;
	sSYNetReconnectPauseEntered = FALSE;
}

void syNetReconnectOnHoldIngress(u32 sim_tick, u32 epoch, u8 pausing_slot, u8 reason)
{
	(void)reason;
	if (syNetReconnectEnabled() == FALSE)
	{
		return;
	}
	if ((sSYNetReconnectPhase == SY_NET_RECONNECT_IDLE) && (syNetReconnectMidMatchEligible() == FALSE))
	{
		return;
	}
	if ((sim_tick != syNetInputGetTick()) && (sSYNetReconnectPhase == SY_NET_RECONNECT_IDLE))
	{
		port_log("SSB64 Reconnect: ignore HOLD tick mismatch peer=%u local=%u\n", (unsigned int)sim_tick,
		         (unsigned int)syNetInputGetTick());
		return;
	}
	sSYNetReconnectHoldSimTick = sim_tick;
	sSYNetReconnectHoldEpoch = epoch;
	sSYNetReconnectPausingSlot = pausing_slot;
	sSYNetReconnectDisconnectSlot = pausing_slot;
	if (sSYNetReconnectPhase == SY_NET_RECONNECT_IDLE)
	{
		sSYNetReconnectPhase = SY_NET_RECONNECT_HOLD_PENDING;
	}
	syNetReconnectEnterHoldActive();
}

void syNetReconnectOnReadyIngress(u32 sim_tick, u32 epoch)
{
	if ((sim_tick != sSYNetReconnectHoldSimTick) || (epoch != sSYNetReconnectHoldEpoch))
	{
		return;
	}
	syNetPeerSendReconnectAck(sim_tick, epoch);
	syNetReconnectClearHold();
}

void syNetReconnectOnAckIngress(u32 sim_tick, u32 epoch)
{
	if ((sim_tick != sSYNetReconnectHoldSimTick) || (epoch != sSYNetReconnectHoldEpoch))
	{
		return;
	}
	syNetReconnectClearHold();
}

void syNetReconnectOnForfeitIngress(u32 sim_tick, u8 forfeiting_slot, u8 winner_slot)
{
	if (sim_tick != sSYNetReconnectHoldSimTick)
	{
		return;
	}
	syNetReconnectApplyForfeit(forfeiting_slot, winner_slot);
}

void syNetReconnectNotifyTransportBad(void)
{
	if (syNetReconnectEnabled() == FALSE)
	{
		return;
	}
	if (syNetReconnectMidMatchEligible() == FALSE)
	{
		sSYNetReconnectDetectBadFrames = 0U;
		return;
	}
	if (syNetReconnectHoldActive() != FALSE)
	{
		return;
	}
	sSYNetReconnectDetectBadFrames++;
	if (sSYNetReconnectDetectBadFrames < syNetReconnectDetectLimit())
	{
		return;
	}
	sSYNetReconnectDetectBadFrames = 0U;
	syNetReconnectBeginHold((u8)sSYNetPeerLocalPlayer, TRUE);
}

void syNetReconnectNotifyNetworkChange(void)
{
	if (sSYNetReconnectNetworkDebounceCooldown > 0U)
	{
		return;
	}
	syNetReconnectNotifyTransportBad();
	sSYNetReconnectNetworkDebounceCooldown = 120U;
}

void syNetReconnectNotifyPeerDisconnect(u8 slot)
{
	if (syNetReconnectMidMatchEligible() == FALSE)
	{
		return;
	}
	if (syNetReconnectHoldActive() != FALSE)
	{
		return;
	}
	syNetReconnectBeginHold(slot, FALSE);
}

sb32 syNetReconnectExportPeerDisconnect(s32 slot)
{
	if ((syNetReconnectHoldActive() == FALSE) || (syNetReconnectMidMatchEligible() == FALSE))
	{
		return 0;
	}
	if (sSYNetReconnectDisconnectSlot == 0xFFU)
	{
		return 0;
	}
	return ((s32)sSYNetReconnectDisconnectSlot == slot) ? 1 : 0;
}

void syNetReconnectUpdate(void)
{
	MmIceReconnectStatus ice_status;

	if (syNetReconnectEnabled() == FALSE)
	{
		return;
	}
	syNetReconnectPollTransportArm();
	if (sSYNetReconnectNetworkDebounceCooldown > 0U)
	{
		sSYNetReconnectNetworkDebounceCooldown--;
	}
	if (syNetReconnectHoldActive() == FALSE)
	{
		if (syNetReconnectMidMatchEligible() == FALSE)
		{
			sSYNetReconnectDetectBadFrames = 0U;
			return;
		}
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	if (sSYNetReconnectPhase == SY_NET_RECONNECT_ICE_RECYCLING)
	{
		ice_status = mmIceReconnectTick();
		if (ice_status == MM_ICE_RECONNECT_CONNECTED)
		{
			sSYNetReconnectPhase = SY_NET_RECONNECT_READY_PENDING;
			syNetPeerSendReconnectReady(sSYNetReconnectHoldSimTick, sSYNetReconnectHoldEpoch);
			port_log("SSB64 Reconnect: ICE ready, sent RECONNECT_READY\n");
		}
		else if (ice_status == MM_ICE_RECONNECT_FAILED)
		{
			port_log("SSB64 Reconnect: ICE recycle failed — staying in hold\n");
			sSYNetReconnectPhase = SY_NET_RECONNECT_HOLD_ACTIVE;
		}
	}
	if ((sSYNetReconnectPhase == SY_NET_RECONNECT_HOLD_PENDING) ||
	    (sSYNetReconnectPhase == SY_NET_RECONNECT_HOLD_ACTIVE))
	{
		if (sSYNetReconnectHoldRetransmitCooldown > 0U)
		{
			sSYNetReconnectHoldRetransmitCooldown--;
		}
		else
		{
			syNetPeerSendReconnectHold(sSYNetReconnectHoldSimTick, sSYNetReconnectHoldEpoch,
			                           sSYNetReconnectPausingSlot, 1U);
			sSYNetReconnectHoldRetransmitCooldown = 30U;
		}
	}
	syNetReconnectMaybeHostForfeitTimeout();
}

void syNetReconnectDrawOverlayCpp(void)
{
	/* Implemented in gameloop.cpp */
}

#endif
