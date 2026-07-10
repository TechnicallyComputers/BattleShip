#include "mm_ice_reconnect.h"

#if defined(PORT) && defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE)

#include "mm_ice.h"
#include "mm_matchmaking.h"
#include <sys/netpeer.h>
#include "port_log.h"

#include <stdio.h>
#include <string.h>

typedef enum MmIceReconnectPhase
{
	MM_ICE_RC_PHASE_IDLE = 0,
	MM_ICE_RC_PHASE_WORKER_INIT,
	MM_ICE_RC_PHASE_GATHER,
	MM_ICE_RC_PHASE_SIGNAL,
	MM_ICE_RC_PHASE_TRICKLE,
	MM_ICE_RC_PHASE_ROLE_READY,
	MM_ICE_RC_PHASE_VALIDATE,
} MmIceReconnectPhase;

typedef enum MmIceRcWorkerStatus
{
	MM_ICE_RC_WORKER_IDLE = 0,
	MM_ICE_RC_WORKER_RUNNING,
	MM_ICE_RC_WORKER_OK,
	MM_ICE_RC_WORKER_FAILED,
} MmIceRcWorkerStatus;

static MmIceReconnectPhase sMmIceRcPhase = MM_ICE_RC_PHASE_IDLE;
static MmIceRcWorkerStatus sMmIceRcWorkerStatus = MM_ICE_RC_WORKER_IDLE;
static u32 sMmIceRcConnectEpoch;
static u32 sMmIceRcQuietTicks;
static sb32 sMmIceRcHostRole;
static sb32 sMmIceRcDescApplied;
static char sMmIceRcTicket[72];
static char sMmIceRcBind[128];

extern sb32 sSYNetPeerBootstrapIsHost;

static void mmIceRcOnLocalCandidate(const char *candidate_sdp, void *user_ptr)
{
	(void)user_ptr;
	if ((candidate_sdp == NULL) || (candidate_sdp[0] == '\0') || (sMmIceRcTicket[0] == '\0'))
	{
		return;
	}
	if (mmIceShouldSignalLocalCandidate(candidate_sdp) == FALSE)
	{
		return;
	}
	mmMatchmakingEnqueueIceSignal(FALSE, sMmIceRcTicket, candidate_sdp);
}

static void mmIceRcOnGatheringDone(void *user_ptr)
{
	(void)user_ptr;
}

static sb32 mmIceRcFillTurnConfig(MmIceServerConfig *cfg)
{
	MmIceTurnBundle turn;

	if (cfg == NULL)
	{
		return FALSE;
	}
	memset(cfg, 0, sizeof(*cfg));
	cfg->stun_host = MM_ICE_DEFAULT_COTURN_HOST;
	cfg->stun_port = MM_ICE_DEFAULT_STUN_PORT;
	cfg->turn_host = MM_ICE_DEFAULT_COTURN_HOST;
	cfg->turn_port = MM_ICE_DEFAULT_TURN_PORT;
	cfg->lan_direct_gather = FALSE;
	memset(&turn, 0, sizeof(turn));
	if (mmMatchmakingTryGetCachedTurnCredentials(&turn) != FALSE)
	{
		port_log("SSB64 ICE Reconnect: using cached TURN credentials stun=%s:%u turn=%s:%u\n", turn.stun_host,
		         (unsigned int)turn.stun_port, turn.turn_host, (unsigned int)turn.turn_port);
	}
	else if (mmMatchmakingFetchTurnCredentials(&turn) != FALSE)
	{
		port_log("SSB64 ICE Reconnect: TURN credentials ok stun=%s:%u turn=%s:%u\n", turn.stun_host,
		         (unsigned int)turn.stun_port, turn.turn_host, (unsigned int)turn.turn_port);
	}
	else
	{
		port_log("SSB64 ICE Reconnect: TURN credentials unavailable — STUN/env fallback\n");
	}
	if ((turn.turn_user[0] != '\0') && (turn.turn_pass[0] != '\0'))
	{
		if (turn.stun_host[0] != '\0')
		{
			cfg->stun_host = turn.stun_host;
		}
		cfg->stun_port = turn.stun_port;
		if (turn.turn_host[0] != '\0')
		{
			cfg->turn_host = turn.turn_host;
		}
		cfg->turn_port = turn.turn_port;
		cfg->turn_user = turn.turn_user;
		cfg->turn_pass = turn.turn_pass;
	}
	return TRUE;
}

static sb32 mmIceRcInitGather(void)
{
	MmIceServerConfig cfg;

	if (mmIceRcFillTurnConfig(&cfg) == FALSE)
	{
		return FALSE;
	}
	mmIceShutdown();
	if (mmIceInit(sMmIceRcBind, &cfg) == FALSE)
	{
		port_log("SSB64 ICE Reconnect: mmIceInit failed\n");
		return FALSE;
	}
	(void)mmIceSetIceControlling(sMmIceRcHostRole);
	mmIceSetCallbacks(mmIceRcOnLocalCandidate, mmIceRcOnGatheringDone, NULL);
	mmIceSetCandidatePolicy(FALSE, FALSE, NULL, NULL);
	if (mmIceStartGathering() == FALSE)
	{
		port_log("SSB64 ICE Reconnect: gathering start failed\n");
		mmIceShutdown();
		return FALSE;
	}
	return TRUE;
}

sb32 mmIceReconnectInitOnWorker(void)
{
	long hc;

	if (sMmIceRcTicket[0] == '\0')
	{
		port_log("SSB64 ICE Reconnect: worker init missing ticket\n");
		return FALSE;
	}
	if (sMmIceRcHostRole != FALSE)
	{
		hc = mmMatchmakingPostIceRestartSync(sMmIceRcTicket, sMmIceRcConnectEpoch);
		if ((hc < 200) || (hc > 299))
		{
			port_log("SSB64 ICE Reconnect: POST ice/restart failed http=%ld\n", hc);
			return FALSE;
		}
	}
	return mmIceRcInitGather();
}

void mmIceReconnectWorkerInitFinished(sb32 ok)
{
	sMmIceRcWorkerStatus = (ok != FALSE) ? MM_ICE_RC_WORKER_OK : MM_ICE_RC_WORKER_FAILED;
}

void mmIceReconnectBegin(u32 connect_epoch)
{
	char match_id[72];
	char ticket[72];

	mmIceReconnectShutdown();
	if (syNetPeerGetAutomatchBootstrapContext(match_id, (u32)sizeof(match_id), ticket, (u32)sizeof(ticket)) ==
	    FALSE)
	{
		port_log("SSB64 ICE Reconnect: missing automatch context\n");
		return;
	}
	snprintf(sMmIceRcTicket, sizeof(sMmIceRcTicket), "%s", ticket);
	sMmIceRcConnectEpoch = (connect_epoch != 0U) ? connect_epoch : 2U;
	sMmIceRcHostRole = (sSYNetPeerBootstrapIsHost != FALSE) ? TRUE : FALSE;
	sMmIceRcBind[0] = '\0';
	if (mmIceGetBootstrapBindHostport(NULL, sMmIceRcBind, (u32)sizeof(sMmIceRcBind)) == FALSE)
	{
		snprintf(sMmIceRcBind, sizeof(sMmIceRcBind), "0.0.0.0:0");
	}
#if defined(__ANDROID__)
	mmIcePauseIo();
#endif
	sMmIceRcWorkerStatus = MM_ICE_RC_WORKER_RUNNING;
	sMmIceRcPhase = MM_ICE_RC_PHASE_WORKER_INIT;
	mmMatchmakingEnqueueIceReconnectInit(FALSE);
	port_log("SSB64 ICE Reconnect: begin epoch=%u host=%d (worker init queued)\n",
	         (unsigned int)sMmIceRcConnectEpoch, (sMmIceRcHostRole != FALSE) ? 1 : 0);
}

void mmIceReconnectShutdown(void)
{
	sMmIceRcPhase = MM_ICE_RC_PHASE_IDLE;
	sMmIceRcWorkerStatus = MM_ICE_RC_WORKER_IDLE;
	sMmIceRcQuietTicks = 0U;
	sMmIceRcDescApplied = FALSE;
	sMmIceRcTicket[0] = '\0';
#if defined(__ANDROID__)
	mmIceEnsureIoResumed();
#endif
}

MmIceReconnectStatus mmIceReconnectTick(void)
{
	char cand[280];
	char sdp[4096];
	sb32 desc_applied;

	if (sMmIceRcPhase == MM_ICE_RC_PHASE_IDLE)
	{
		return MM_ICE_RECONNECT_IDLE;
	}
	(void)mmIcePoll();

	switch (sMmIceRcPhase)
	{
	case MM_ICE_RC_PHASE_WORKER_INIT:
		if (sMmIceRcWorkerStatus == MM_ICE_RC_WORKER_FAILED)
		{
			sMmIceRcPhase = MM_ICE_RC_PHASE_IDLE;
			return MM_ICE_RECONNECT_FAILED;
		}
		if (sMmIceRcWorkerStatus != MM_ICE_RC_WORKER_OK)
		{
			return MM_ICE_RECONNECT_WORKING;
		}
		sMmIceRcPhase = MM_ICE_RC_PHASE_GATHER;
		break;

	case MM_ICE_RC_PHASE_GATHER:
		if (mmIceGatherFailed() != FALSE)
		{
			sMmIceRcPhase = MM_ICE_RC_PHASE_IDLE;
			return MM_ICE_RECONNECT_FAILED;
		}
		if (mmIceGatherInProgress() != FALSE)
		{
			return MM_ICE_RECONNECT_WORKING;
		}
		sMmIceRcPhase = MM_ICE_RC_PHASE_SIGNAL;
		break;

	case MM_ICE_RC_PHASE_SIGNAL:
		if (mmIceGetLocalDescription(sdp, (u32)sizeof(sdp)) == FALSE)
		{
			return MM_ICE_RECONNECT_WORKING;
		}
		(void)mmIceFilterHostFromSignalingSdp(sdp);
		mmMatchmakingEnqueueIceSignal(FALSE, sMmIceRcTicket, sdp);
		sMmIceRcPhase = MM_ICE_RC_PHASE_TRICKLE;
		break;

	case MM_ICE_RC_PHASE_TRICKLE:
		(void)mmMatchmakingPollMatchIceTrickleSync(sMmIceRcTicket);
		while (mmMatchmakingPopIceCandidate(cand, (u32)sizeof(cand)) != FALSE)
		{
			desc_applied = FALSE;
			(void)mmIceApplyPeerIceSignaling(cand, &desc_applied);
			if (desc_applied != FALSE)
			{
				sMmIceRcDescApplied = TRUE;
			}
		}
		if (mmIceIsConnected() != FALSE)
		{
			sMmIceRcPhase = MM_ICE_RC_PHASE_ROLE_READY;
			break;
		}
		sMmIceRcQuietTicks++;
		if (sMmIceRcQuietTicks > 600U)
		{
			sMmIceRcPhase = MM_ICE_RC_PHASE_IDLE;
			return MM_ICE_RECONNECT_FAILED;
		}
		return MM_ICE_RECONNECT_WORKING;

	case MM_ICE_RC_PHASE_ROLE_READY:
		if (sMmIceRcHostRole != FALSE)
		{
			mmMatchmakingPostIceRoleReadySync(sMmIceRcTicket, "pair", sMmIceRcConnectEpoch);
		}
		else if (mmMatchmakingIcePeerControllingReady() == FALSE)
		{
			(void)mmMatchmakingPollMatchIceTrickleSync(sMmIceRcTicket);
			return MM_ICE_RECONNECT_WORKING;
		}
		sMmIceRcPhase = MM_ICE_RC_PHASE_VALIDATE;
		break;

	case MM_ICE_RC_PHASE_VALIDATE:
		if (mmIceIsConnected() == FALSE)
		{
			return MM_ICE_RECONNECT_WORKING;
		}
		sMmIceRcPhase = MM_ICE_RC_PHASE_IDLE;
#if defined(__ANDROID__)
		mmIceResumeIo();
#endif
		return MM_ICE_RECONNECT_CONNECTED;
	}
	return MM_ICE_RECONNECT_WORKING;
}

#endif
