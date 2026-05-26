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

static char sIceTicket[64];
static char sIceBind[144];
static sb32 sIceRemoteDescApplied;
static sb32 sIceGatheringDone;
static sb32 sIceHostRole;

static void mnVSNetAutomatchAMIceOnLocalCandidate(const char *sdp, void *user_ptr)
{
	(void)user_ptr;
	if ((sdp != NULL) && (sdp[0] != '\0') && (sIceTicket[0] != '\0'))
	{
		mmMatchmakingEnqueueIceSignal(FALSE, sIceTicket, sdp);
	}
}

static void mnVSNetAutomatchAMIceOnGatheringDone(void *user_ptr)
{
	(void)user_ptr;
	sIceGatheringDone = TRUE;
}

sb32 mnVSNetAutomatchAMIcePlayerReady(const char *bind_spec, char *wan_out, u32 wan_cap, char *lan_out, u32 lan_cap,
                                      char *ice_sdp_out, u32 ice_sdp_cap)
{
	MmIceServerConfig cfg;
	char turn_user[192];
	char turn_pass[192];
	char realm[96];
	char sdp[4096];

	memset(&cfg, 0, sizeof(cfg));
	if (mmMatchmakingFetchTurnCredentials(turn_user, sizeof(turn_user), turn_pass, sizeof(turn_pass), realm,
	                                      sizeof(realm)) != FALSE)
	{
		cfg.turn_host = getenv("SSB64_MATCHMAKING_TURN_HOST");
		if ((cfg.turn_host == NULL) || (cfg.turn_host[0] == '\0'))
		{
			cfg.turn_host = "coturn.technicallycomputers.ca";
		}
		cfg.turn_port = 3478U;
		cfg.turn_user = turn_user;
		cfg.turn_pass = turn_pass;
	}
	snprintf(sIceBind, sizeof(sIceBind), "%s", bind_spec);
	if (mmIceInit(bind_spec, &cfg) == FALSE)
	{
		return FALSE;
	}
	mmIceSetCallbacks(mnVSNetAutomatchAMIceOnLocalCandidate, mnVSNetAutomatchAMIceOnGatheringDone, NULL);
	if (mmIceStartGathering() == FALSE)
	{
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
		if (mmIceGetReflexiveHostport(wan_out, wan_cap) == FALSE)
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

void mnVSNetAutomatchAMIceBeginConnect(const MmMatchResult *mr)
{
	char cand[280];

	sIceRemoteDescApplied = FALSE;
	sIceGatheringDone = FALSE;
	sIceHostRole = (mr != NULL && mr->you_are_host != FALSE) ? TRUE : FALSE;
	snprintf(sIceTicket, sizeof(sIceTicket), "%s", (mr != NULL) ? mr->ticket_id : "");
	if ((mr != NULL) && (mr->peer_ice_sdp[0] != '\0'))
	{
		(void)mmIceApplyRemoteDescription(mr->peer_ice_sdp);
		sIceRemoteDescApplied = TRUE;
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
