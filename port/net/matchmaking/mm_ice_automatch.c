#include "mm_ice_automatch.h"

#if defined(PORT) && defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE)

#include "mm_ice.h"
#include "mm_lan_detect.h"
#include <mm_matchmaking.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/netpeer.h>

extern void port_log(const char *fmt, ...);

#define ICE_PRETICKET_CAND_QUEUE 16

static char sIceTicket[64];
static char sIceBind[144];
static sb32 sIceRemoteDescApplied;
static sb32 sIceGatheringDone;
static sb32 sIceHostRole;
static char sPreTicketCand[ICE_PRETICKET_CAND_QUEUE][280];
static u32 sPreTicketCandHead;
static u32 sPreTicketCandCount;

static void mnVSNetAutomatchAMIceFlushPreTicketCandidates(void)
{
	while (sPreTicketCandCount > 0U)
	{
		mmMatchmakingEnqueueIceSignal(FALSE, sIceTicket, sPreTicketCand[sPreTicketCandHead]);
		sPreTicketCandHead = (sPreTicketCandHead + 1U) % ICE_PRETICKET_CAND_QUEUE;
		sPreTicketCandCount--;
	}
}

static void mnVSNetAutomatchAMIceQueuePreTicketCandidate(const char *sdp)
{
	u32 tail;

	if ((sdp == NULL) || (sdp[0] == '\0'))
	{
		return;
	}
	if (sPreTicketCandCount >= ICE_PRETICKET_CAND_QUEUE)
	{
		sPreTicketCandHead = (sPreTicketCandHead + 1U) % ICE_PRETICKET_CAND_QUEUE;
		sPreTicketCandCount--;
	}
	tail = (sPreTicketCandHead + sPreTicketCandCount) % ICE_PRETICKET_CAND_QUEUE;
	snprintf(sPreTicketCand[tail], sizeof(sPreTicketCand[tail]), "%s", sdp);
	sPreTicketCandCount++;
}

static void mnVSNetAutomatchAMIceOnLocalCandidate(const char *sdp, void *user_ptr)
{
	(void)user_ptr;
	if ((sdp == NULL) || (sdp[0] == '\0'))
	{
		return;
	}
	if (sIceTicket[0] != '\0')
	{
		mmMatchmakingEnqueueIceSignal(FALSE, sIceTicket, sdp);
	}
	else
	{
		mnVSNetAutomatchAMIceQueuePreTicketCandidate(sdp);
	}
}

static void mnVSNetAutomatchAMIceOnGatheringDone(void *user_ptr)
{
	(void)user_ptr;
	sIceGatheringDone = TRUE;
}

void mnVSNetAutomatchAMIceReset(void)
{
	sIceTicket[0] = '\0';
	sIceBind[0] = '\0';
	sIceRemoteDescApplied = FALSE;
	sIceGatheringDone = FALSE;
	sIceHostRole = FALSE;
	sPreTicketCandHead = 0U;
	sPreTicketCandCount = 0U;
}

void mnVSNetAutomatchAMIceOnTicketAssigned(const char *ticket)
{
	if ((ticket == NULL) || (ticket[0] == '\0'))
	{
		return;
	}
	snprintf(sIceTicket, sizeof(sIceTicket), "%s", ticket);
	mnVSNetAutomatchAMIceFlushPreTicketCandidates();
}

sb32 mnVSNetAutomatchAMIcePlayerReady(const char *bind_spec, char *wan_out, u32 wan_cap, char *lan_out, u32 lan_cap,
                                      char *ice_sdp_out, u32 ice_sdp_cap)
{
	MmIceTurnBundle turn;
	MmIceServerConfig cfg;
	char sdp[4096];

	mnVSNetAutomatchAMIceReset();
	memset(&cfg, 0, sizeof(cfg));
	if (mmMatchmakingFetchTurnCredentials(&turn) != FALSE)
	{
		cfg.stun_host = turn.stun_host;
		cfg.stun_port = turn.stun_port;
		cfg.turn_host = turn.turn_host;
		cfg.turn_port = turn.turn_port;
		cfg.turn_user = turn.turn_user;
		cfg.turn_pass = turn.turn_pass;
		port_log("SSB64 ICE: TURN credentials ok stun=%s:%u turn=%s:%u\n", turn.stun_host, (unsigned int)turn.stun_port,
		         turn.turn_host, (unsigned int)turn.turn_port);
		if (turn.turns_port != 0U)
		{
			port_log("SSB64 ICE: TURNS port %u not used by libjuice (UDP %u only)\n",
			         (unsigned int)turn.turns_port, (unsigned int)turn.turn_port);
		}
	}
	else
	{
		port_log("SSB64 ICE: TURN credentials unavailable — STUN-only or env fallback (CGNAT may fail)\n");
	}
	snprintf(sIceBind, sizeof(sIceBind), "%s", bind_spec);
	if (mmIceInit(bind_spec, &cfg) == FALSE)
	{
		port_log("SSB64 ICE: mmIceInit failed\n");
		return FALSE;
	}
	mmIceSetCallbacks(mnVSNetAutomatchAMIceOnLocalCandidate, mnVSNetAutomatchAMIceOnGatheringDone, NULL);
	if (mmIceStartGathering() == FALSE)
	{
		port_log("SSB64 ICE: gathering start failed\n");
		mmIceShutdown();
		return FALSE;
	}
	if (mmIceGetLocalDescription(sdp, sizeof(sdp)) == FALSE)
	{
		mmIceShutdown();
		return FALSE;
	}
	if (ice_sdp_out != NULL && ice_sdp_cap > 0U)
	{
		snprintf(ice_sdp_out, ice_sdp_cap, "%s", sdp);
	}
	if (wan_out != NULL && wan_cap > 0U)
	{
		if (mmIceGetSrflxHostport(wan_out, wan_cap) == FALSE)
		{
			snprintf(wan_out, wan_cap, "%s", bind_spec);
		}
	}
	if (lan_out != NULL && lan_cap > 0U)
	{
		lan_out[0] = '\0';
		(void)mmLanDetectEndpoint(lan_out, lan_cap, -1, bind_spec);
	}
	return TRUE;
}

sb32 mnVSNetAutomatchAMIceBindTick(char *ice_sdp_out, u32 ice_sdp_cap)
{
	char sdp[4096];

	(void)mmIcePoll();
	if (sIceGatheringDone == FALSE)
	{
		return FALSE;
	}
	if (mmIceGetLocalDescription(sdp, sizeof(sdp)) == FALSE)
	{
		return FALSE;
	}
	if (ice_sdp_out != NULL && ice_sdp_cap > 0U)
	{
		snprintf(ice_sdp_out, ice_sdp_cap, "%s", sdp);
	}
	return TRUE;
}

void mnVSNetAutomatchAMIceBeginConnect(const MmMatchResult *mr)
{
	char cand[280];

	sIceRemoteDescApplied = FALSE;
	sIceGatheringDone = FALSE;
	sIceHostRole = (mr != NULL && mr->you_are_host != FALSE) ? TRUE : FALSE;
	snprintf(sIceTicket, sizeof(sIceTicket), "%s", (mr != NULL) ? mr->ticket_id : "");
	mnVSNetAutomatchAMIceFlushPreTicketCandidates();
	if ((mr != NULL) && (mr->peer_ice_sdp[0] != '\0'))
	{
		(void)mmIceApplyRemoteDescription(mr->peer_ice_sdp);
		sIceRemoteDescApplied = TRUE;
	}
	else
	{
		port_log("SSB64 ICE: match has no peer_ice_sdp\n");
	}
	while (mmMatchmakingPopIceCandidate(cand, sizeof(cand)) != FALSE)
	{
		(void)mmIceAddRemoteCandidate(cand);
	}
	if (sIceRemoteDescApplied != FALSE)
	{
		(void)mmIceStartGathering();
	}
}

sb32 mnVSNetAutomatchAMIceConnectTick(void)
{
	char cand[280];
	MmIceState st;

	st = mmIcePoll();
	while (mmMatchmakingPopIceCandidate(cand, sizeof(cand)) != FALSE)
	{
		(void)mmIceAddRemoteCandidate(cand);
	}
	if ((sIceGatheringDone != FALSE) && (sIceRemoteDescApplied != FALSE))
	{
		(void)mmIceSetRemoteGatheringDone();
	}
	if (mmIceIsCompleted() != FALSE)
	{
		char remote[128];

		if (mmIceGetSelectedPath(NULL, 0U, remote, sizeof(remote)) != FALSE && remote[0] != '\0')
		{
			port_log("SSB64 ICE: connected remote=%s\n", remote);
		}
		syNetPeerSetIceTransport(TRUE);
		return TRUE;
	}
	if (st == MM_ICE_STATE_FAILED)
	{
		port_log("SSB64 ICE: connection failed\n");
	}
	return FALSE;
}

sb32 mnVSNetAutomatchAMIceBootstrapPeer(const MmMatchResult *mr, const char *bind)
{
	char remote[128];

	if ((mr == NULL) || (bind == NULL))
	{
		return FALSE;
	}
	syNetPeerSetAutomatchBootstrapContext(mr->match_id, mr->ticket_id);
	if (mmIceGetSelectedPath(NULL, 0U, remote, sizeof(remote)) == FALSE)
	{
		snprintf(remote, sizeof(remote), "%s", mr->peer_hostport);
	}
	if (syNetPeerConfigureUdpForAutomatch(bind, remote, mr->session_id, mr->you_are_host, 2U, TRUE) == FALSE)
	{
		return FALSE;
	}
	if (syNetPeerRunBootstrap() == FALSE)
	{
		return FALSE;
	}
	return TRUE;
}

#endif /* PORT && SSB64_NETMENU && SSB64_NETPLAY_ICE */
