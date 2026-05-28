#include "mm_ice_automatch.h"

#if defined(PORT) && defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE)

#include "mm_ice.h"
#include "mm_lan_detect.h"
#include <mm_matchmaking.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int atoi(const char *str);
#include <sys/netpeer.h>

#include "port_log.h"

#define ICE_PRETICKET_CAND_QUEUE 16
/* Quiet trickle polls before remote gathering done (~60 Hz). Wait for connected + drain. */
#define ICE_TRICKLE_QUIET_TICKS 30U
#define ICE_TRICKLE_QUIET_CONNECTED_TICKS 12U
/* Shared-LAN: wait for host-pair re-nomination if libjuice completes on relay first. */
#define ICE_LAN_RELAY_SETTLE_TICKS 60U

static char sIceTicket[64];
static char sIceBind[144];
static char sIceLocalLan[144];
static sb32 sIceRemoteDescApplied;
static sb32 sIceGatheringDone;
static sb32 sIceHostRole;
static sb32 sIceRemoteGatheringDonePosted;
static u32 sIceTrickleQuietTicks;
static char sPreTicketCand[ICE_PRETICKET_CAND_QUEUE][280];
static u32 sPreTicketCandHead;
static u32 sPreTicketCandCount;
static MmMatchResult sIceValidateMatch;
static sb32 sIceValidateMatchActive;
static sb32 sIceLikelyLanDirect;
static u32 sIceCompletedSettleTicks;
static u32 sIceLanRelaySettleTicks;
static char sIceCompletedRemote[128];
static const char *sIceConnectFailReason = "ICE connection failed";

static sb32 sIcePeerSetupFailed;
static sb32 sIceAwaitPeerControllingReady;
static sb32 sIceAwaitPeerRoleReadyLogged;
static char sIceDeferredPeerSdp[4096];

static void mnVSNetAutomatchAMIceRefreshLocalLanFromGather(void);

static sb32 mnVSNetAutomatchAMIceApplyPeerSdp(const char *peer_sdp, sb32 *out_desc_applied)
{
	size_t peer_len;
	const char *nl;
	sb32 applied;

	if ((peer_sdp == NULL) || (peer_sdp[0] == '\0'))
	{
		return TRUE;
	}
	if (out_desc_applied != NULL)
	{
		*out_desc_applied = FALSE;
	}
	peer_len = strlen(peer_sdp);
	nl = strchr(peer_sdp, '\n');
	if (nl != NULL)
	{
		char line[72];

		memset(line, 0, sizeof(line));
		snprintf(line, sizeof(line), "%.*s", (int)((nl - peer_sdp) + 1U), peer_sdp);
		port_log("SSB64 ICE: peer_ice_sdp len=%zu first_line=%s has_ufrag=%d\n", peer_len, line,
		         (int)mmIceSdpHasIceUfrag(peer_sdp));
	}
	else
	{
		port_log("SSB64 ICE: peer_ice_sdp len=%zu (no newline) has_ufrag=%d\n", peer_len,
		         (int)mmIceSdpHasIceUfrag(peer_sdp));
	}
	applied = mmIceApplyPeerIceSignaling(peer_sdp, out_desc_applied);
	if (applied == FALSE)
	{
		sIceConnectFailReason = "ICE peer setup failed (invalid SDP)";
		sIcePeerSetupFailed = TRUE;
		port_log("SSB64 ICE: mmIceApplyPeerIceSignaling failed (invalid peer SDP)\n");
		return FALSE;
	}
	return TRUE;
}

static const char *mnVSNetAutomatchAMIcePickPeerHostport(const MmMatchResult *mr, char *out, u32 out_cap)
{
	if ((mr == NULL) || (out == NULL) || (out_cap < 8U))
	{
		return NULL;
	}
	out[0] = '\0';
	if (mr->peer_lan_hostport[0] != '\0')
	{
		snprintf(out, out_cap, "%s", mr->peer_lan_hostport);
	}
	else if (mr->peer_hostport[0] != '\0')
	{
		snprintf(out, out_cap, "%s", mr->peer_hostport);
	}
	return (out[0] != '\0') ? out : NULL;
}

static void mnVSNetAutomatchAMIceRefreshLocalLanForPeer(const MmMatchResult *mr)
{
	char peer_hp[128];
	char picked[128];
	const char *prev;

	if (mr == NULL)
	{
		mnVSNetAutomatchAMIceRefreshLocalLanFromGather();
		return;
	}
	prev = (sIceLocalLan[0] != '\0') ? sIceLocalLan : NULL;
	if (mnVSNetAutomatchAMIcePickPeerHostport(mr, peer_hp, sizeof(peer_hp)) == NULL)
	{
		mnVSNetAutomatchAMIceRefreshLocalLanFromGather();
		return;
	}
	if (mmIceGetLocalHostHostportForPeer(peer_hp, picked, sizeof(picked)) == FALSE || picked[0] == '\0')
	{
		mnVSNetAutomatchAMIceRefreshLocalLanFromGather();
		return;
	}
	snprintf(sIceLocalLan, sizeof(sIceLocalLan), "%s", picked);
	if ((prev == NULL) || (strcmp(prev, picked) != 0))
	{
		port_log("SSB64 ICE: peer-directed local bind %s (peer=%s)\n", picked, peer_hp);
	}
}

static sb32 mnVSNetAutomatchAMIceEnvSignalHost(void)
{
	const char *e;

	e = getenv("SSB64_MATCHMAKING_ICE_SIGNAL_HOST");
	return (e != NULL && e[0] != '\0' && atoi(e) != 0) ? TRUE : FALSE;
}

static sb32 mnVSNetAutomatchAMIceEnvVerbose(void)
{
	const char *e;

	e = getenv("SSB64_MATCHMAKING_ICE_VERBOSE");
	return (e != NULL && e[0] != '\0' && atoi(e) != 0) ? TRUE : FALSE;
}

static sb32 mnVSNetAutomatchAMIceDeriveAllowPeerHost(const MmMatchResult *mr)
{
	if ((mr == NULL) || (mr->peer_reports_lan == FALSE))
	{
		return FALSE;
	}
	if (mr->peer_lan_hostport[0] == '\0')
	{
		return FALSE;
	}
	if (sIceLocalLan[0] != '\0')
	{
		return mmLanPeerSharesLocalLanSubnet(mr->peer_lan_hostport, sIceLocalLan);
	}
	return mmLanPeerHostportIsOnLocalLan(mr->peer_lan_hostport);
}

static sb32 mnVSNetAutomatchAMIceDeriveSignalLocalHost(void)
{
	if (mnVSNetAutomatchAMIceEnvSignalHost() != FALSE)
	{
		return TRUE;
	}
	return (sIceLocalLan[0] != '\0') ? TRUE : FALSE;
}

static sb32 mnVSNetAutomatchAMIceRequireTurnCreds(const char *local_lan)
{
	const char *e;

	e = getenv("SSB64_MATCHMAKING_ICE_REQUIRE_TURN");
	if (e != NULL && e[0] != '\0')
	{
		return (atoi(e) != 0) ? TRUE : FALSE;
	}
	return ((local_lan == NULL) || (local_lan[0] == '\0')) ? TRUE : FALSE;
}

static sb32 mnVSNetAutomatchAMIceTurnConfigured(const MmIceServerConfig *cfg)
{
	const char *user;
	const char *pass;

	if ((cfg != NULL) && (cfg->turn_host != NULL) && (cfg->turn_host[0] != '\0') && (cfg->turn_user != NULL) &&
	    (cfg->turn_user[0] != '\0') && (cfg->turn_pass != NULL) && (cfg->turn_pass[0] != '\0'))
	{
		return TRUE;
	}
	user = getenv("SSB64_MATCHMAKING_TURN_USER");
	pass = getenv("SSB64_MATCHMAKING_TURN_PASS");
	return ((user != NULL) && (user[0] != '\0') && (pass != NULL) && (pass[0] != '\0')) ? TRUE : FALSE;
}

static void mnVSNetAutomatchAMIceApplyCandidatePolicy(const MmMatchResult *mr)
{
	sb32 allow_peer;
	sb32 signal_local;

	allow_peer = mnVSNetAutomatchAMIceDeriveAllowPeerHost(mr);
	signal_local = mnVSNetAutomatchAMIceDeriveSignalLocalHost();
	mmIceSetCandidatePolicy(allow_peer, signal_local);
	port_log("SSB64 ICE: candidate policy allow_peer_host=%d signal_local_host=%d peer_lan=%s local_lan=%s\n",
	         (int)allow_peer, (int)signal_local,
	         (mr != NULL && mr->peer_lan_hostport[0] != '\0') ? mr->peer_lan_hostport : "(none)",
	         (sIceLocalLan[0] != '\0') ? sIceLocalLan : "(none)");
}

sb32 mnVSNetAutomatchAMIceShouldQueueTurnEndpoint(void)
{
	char relay_hp[128];

	if (mmIceIsLanDirectGather() != FALSE)
	{
		return FALSE;
	}
	if (mmIceGetRelayHostport(relay_hp, sizeof(relay_hp)) == FALSE)
	{
		return FALSE;
	}
	if ((sIceLikelyLanDirect != FALSE) && (sIceLocalLan[0] != '\0'))
	{
		return FALSE;
	}
	return TRUE;
}

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

static void mnVSNetAutomatchAMIceDrainRemoteCandidates(void)
{
	char cand[280];

	while (mmMatchmakingPopIceCandidate(cand, sizeof(cand)) != FALSE)
	{
		if (mmIceShouldAcceptRemoteCandidate(cand) == FALSE)
		{
			if (mnVSNetAutomatchAMIceEnvVerbose() != FALSE)
			{
				port_log("SSB64 ICE: filtered remote trickle host candidate\n");
			}
			continue;
		}
		if (sIceRemoteGatheringDonePosted != FALSE)
		{
			if (mnVSNetAutomatchAMIceEnvVerbose() != FALSE)
			{
				port_log("SSB64 ICE: dropped late remote trickle (after remote gathering done)\n");
			}
			continue;
		}
		if (mmIceAddRemoteCandidate(cand) == FALSE)
		{
			if (mnVSNetAutomatchAMIceEnvVerbose() != FALSE)
			{
				port_log("SSB64 ICE: mmIceAddRemoteCandidate failed\n");
			}
		}
	}
}

static void mnVSNetAutomatchAMIceMaybePostRemoteGatheringDone(void)
{
	u32 quiet_required;

	if (sIceRemoteGatheringDonePosted != FALSE)
	{
		return;
	}
	if ((sIceGatheringDone == FALSE) || (sIceRemoteDescApplied == FALSE))
	{
		sIceTrickleQuietTicks = 0U;
		return;
	}
	if (mmMatchmakingIceSignalsQueuedCount() != 0U)
	{
		sIceTrickleQuietTicks = 0U;
		return;
	}
	if ((mmIceIsConnected() == FALSE) && (mmIceIsCompleted() == FALSE))
	{
		sIceTrickleQuietTicks = 0U;
		return;
	}
	quiet_required =
	    (mmIceIsConnected() != FALSE) ? ICE_TRICKLE_QUIET_CONNECTED_TICKS : ICE_TRICKLE_QUIET_TICKS;
	sIceTrickleQuietTicks++;
	if (sIceTrickleQuietTicks < quiet_required)
	{
		return;
	}
	if (mmMatchmakingIceSignalsQueuedCount() != 0U)
	{
		sIceTrickleQuietTicks = 0U;
		return;
	}
	if (mmIceSetRemoteGatheringDone() != FALSE)
	{
		sIceRemoteGatheringDonePosted = TRUE;
		port_log("SSB64 ICE: remote gathering done (trickle quiet, connected=%d)\n",
		         (int)(mmIceIsConnected() != FALSE));
	}
}

static void mnVSNetAutomatchAMIceOnLocalCandidate(const char *sdp, void *user_ptr)
{
	(void)user_ptr;
	if ((sdp == NULL) || (sdp[0] == '\0'))
	{
		return;
	}
	if (mmIceShouldSignalLocalCandidate(sdp) == FALSE)
	{
		if (mnVSNetAutomatchAMIceEnvVerbose() != FALSE)
		{
			port_log("SSB64 ICE: suppressed local host trickle candidate\n");
		}
		return;
	}
	if ((mmIceIsConnected() != FALSE) || (mmIceIsCompleted() != FALSE))
	{
		if (mnVSNetAutomatchAMIceEnvVerbose() != FALSE)
		{
			port_log("SSB64 ICE: suppressed local trickle after ICE connected\n");
		}
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
	mmMatchmakingIceSignalsClear();
	sIceTicket[0] = '\0';
	sIceBind[0] = '\0';
	sIceLocalLan[0] = '\0';
	sIceRemoteDescApplied = FALSE;
	sIceGatheringDone = FALSE;
	sIceHostRole = FALSE;
	sIceRemoteGatheringDonePosted = FALSE;
	sIceTrickleQuietTicks = 0U;
	sPreTicketCandHead = 0U;
	sPreTicketCandCount = 0U;
	sIceValidateMatchActive = FALSE;
	sIceLikelyLanDirect = FALSE;
	sIceCompletedSettleTicks = 0U;
	sIceLanRelaySettleTicks = 0U;
	sIceCompletedRemote[0] = '\0';
	sIceConnectFailReason = "ICE connection failed";
	sIcePeerSetupFailed = FALSE;
	sIceAwaitPeerControllingReady = FALSE;
	sIceAwaitPeerRoleReadyLogged = FALSE;
	sIceDeferredPeerSdp[0] = '\0';
	mmMatchmakingIceConnectCacheReset();
	memset(&sIceValidateMatch, 0, sizeof(sIceValidateMatch));
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

sb32 mnVSNetAutomatchAMIceShouldIgnorePollError(const MmMatchResult *ev)
{
	if (ev == NULL)
	{
		return FALSE;
	}
	/* Transient client/network or server errors during ICE — keep connect tick alive. */
	if (ev->http_status == 0L)
	{
		return TRUE;
	}
	if (ev->http_status >= 500L)
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 mnVSNetAutomatchAMIceBindPortIsEphemeral(const char *bind_spec)
{
	u16 port;

	if ((bind_spec == NULL) || (bind_spec[0] == '\0'))
	{
		return TRUE;
	}
	if (strrchr(bind_spec, ':') == NULL)
	{
		return TRUE;
	}
	if (mmIceParseBindPortFromSpec(bind_spec, &port) == FALSE)
	{
		return TRUE;
	}
	return (port == 0U) ? TRUE : FALSE;
}

static sb32 mnVSNetAutomatchAMIceLikelyLanDirect(const char *bind_spec)
{
	const char *lan_env;
	char bind_host[64];

	lan_env = getenv("SSB64_MATCHMAKING_LAN_ENDPOINT");
	if (lan_env != NULL && lan_env[0] != '\0')
	{
		return TRUE;
	}
	if (mmIceParseBindHostFromSpec(bind_spec, bind_host, sizeof(bind_host)) != FALSE)
	{
		return TRUE;
	}
	if (mmLanDetectHasLocalRfc1918() != FALSE)
	{
		return TRUE;
	}
	return FALSE;
}

/* Skip STUN/TURN only when explicitly opted in — automatch default gathers srflx/relay for LTE peers. */
static sb32 mnVSNetAutomatchAMIceEnvLanDirectGather(void)
{
	const char *e;

	e = getenv("SSB64_MATCHMAKING_ICE_LAN_DIRECT");
	if (e == NULL || e[0] == '\0')
	{
		return FALSE;
	}
	return (atoi(e) != 0) ? TRUE : FALSE;
}

static sb32 mnVSNetAutomatchAMIceBindTickSignalLocal(void)
{
	if (mnVSNetAutomatchAMIceEnvSignalHost() != FALSE)
	{
		return TRUE;
	}
	if (sIceLocalLan[0] != '\0')
	{
		return TRUE;
	}
	if (sIceLikelyLanDirect != FALSE)
	{
		return TRUE;
	}
	return FALSE;
}

static void mnVSNetAutomatchAMIceRefreshLocalLanFromGather(void)
{
	char host_hp[128];

	if (mmIceGetLocalHostHostport(host_hp, sizeof(host_hp)) == FALSE || host_hp[0] == '\0')
	{
		return;
	}
	snprintf(sIceLocalLan, sizeof(sIceLocalLan), "%s", host_hp);
	port_log("SSB64 ICE: discovered local bind %s\n", host_hp);
}

const char *mnVSNetAutomatchAMIceLocalLan(void)
{
	return (sIceLocalLan[0] != '\0') ? sIceLocalLan : NULL;
}

sb32 mnVSNetAutomatchAMIcePlayerReady(const char *bind_spec, char *wan_out, u32 wan_cap, char *lan_out, u32 lan_cap,
                                      char *ice_sdp_out, u32 ice_sdp_cap)
{
	MmIceTurnBundle turn;
	MmIceServerConfig cfg;
	sb32 likely_lan;

	(void)wan_out;
	(void)wan_cap;
	(void)ice_sdp_out;
	(void)ice_sdp_cap;

	mnVSNetAutomatchAMIceReset();
	snprintf(sIceBind, sizeof(sIceBind), "%s", bind_spec);
	sIceLocalLan[0] = '\0';
	likely_lan = mnVSNetAutomatchAMIceLikelyLanDirect(bind_spec);
	sIceLikelyLanDirect = likely_lan;
	{
		const char *lan_env;
		char bind_host[64];

		lan_env = getenv("SSB64_MATCHMAKING_LAN_ENDPOINT");
		if (lan_env != NULL && lan_env[0] != '\0')
		{
			snprintf(sIceLocalLan, sizeof(sIceLocalLan), "%s", lan_env);
			if (lan_out != NULL && lan_cap > 0U)
			{
				snprintf(lan_out, lan_cap, "%s", lan_env);
			}
		}
		else if (mnVSNetAutomatchAMIceBindPortIsEphemeral(bind_spec) == FALSE &&
		         mmIceParseBindHostFromSpec(bind_spec, bind_host, sizeof(bind_host)) != FALSE)
		{
			u16 bind_port;

			if (mmIceParseBindPortFromSpec(bind_spec, &bind_port) != FALSE)
			{
				snprintf(sIceLocalLan, sizeof(sIceLocalLan), "%s:%u", bind_host, (unsigned int)bind_port);
			}
			else
			{
				snprintf(sIceLocalLan, sizeof(sIceLocalLan), "%s", bind_host);
			}
			if (lan_out != NULL && lan_cap > 0U)
			{
				snprintf(lan_out, lan_cap, "%s", sIceLocalLan);
			}
		}
		else if (lan_out != NULL && lan_cap > 0U)
		{
			lan_out[0] = '\0';
			if (mnVSNetAutomatchAMIceBindPortIsEphemeral(bind_spec) == FALSE)
			{
				(void)mmLanDetectEndpoint(lan_out, lan_cap, -1, bind_spec);
				snprintf(sIceLocalLan, sizeof(sIceLocalLan), "%s", lan_out);
			}
		}
		else if (mnVSNetAutomatchAMIceBindPortIsEphemeral(bind_spec) == FALSE)
		{
			(void)mmLanDetectEndpoint(sIceLocalLan, (u32)sizeof(sIceLocalLan), -1, bind_spec);
		}
	}
	if (likely_lan != FALSE && mnVSNetAutomatchAMIceEnvLanDirectGather() != FALSE)
	{
		mmIceSetLanDirectGather(TRUE);
	}
	else
	{
		mmIceSetLanDirectGather(FALSE);
	}
	memset(&cfg, 0, sizeof(cfg));
	cfg.lan_direct_gather =
	    (likely_lan != FALSE && mnVSNetAutomatchAMIceEnvLanDirectGather() != FALSE) ? TRUE : FALSE;
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
	if ((likely_lan == FALSE) && (mnVSNetAutomatchAMIceRequireTurnCreds(NULL) != FALSE) &&
	    (mnVSNetAutomatchAMIceTurnConfigured(&cfg) == FALSE))
	{
		port_log("SSB64 ICE: TURN required (no local LAN) but no TURN credentials — aborting gather\n");
		return FALSE;
	}
	if (mmIceInit(bind_spec, &cfg) == FALSE)
	{
		port_log("SSB64 ICE: mmIceInit failed\n");
		return FALSE;
	}
	/* Default controlled until match assigns host/controlling (avoids gather-time both-controlling). */
	(void)mmIceSetIceControlling(FALSE);
	port_log("SSB64 ICE: local_lan=%s lan_direct_gather=%d\n",
	         (sIceLocalLan[0] != '\0') ? sIceLocalLan : "(pending gather)", (int)cfg.lan_direct_gather);
	if ((cfg.lan_direct_gather == FALSE) && (likely_lan == FALSE))
	{
		const char *bind_env;
		const char *lan_env;

		bind_env = getenv("SSB64_MATCHMAKING_BIND");
		lan_env = getenv("SSB64_MATCHMAKING_LAN_ENDPOINT");
		if (((bind_env == NULL) || (bind_env[0] == '\0')) && ((lan_env == NULL) || (lan_env[0] == '\0')))
		{
			port_log(
			    "SSB64 ICE: full STUN/TURN gather without BIND/LAN_ENDPOINT — LAN matches may log benign TURN "
			    "CreatePermission 403; host↔host still wins via candidate policy. Set BIND/LAN_ENDPOINT or "
			    "SSB64_MATCHMAKING_ICE_LAN_DIRECT=1 to skip TURN at gather (see "
			    "docs/bugs/ice_lan_dual_nic_and_thread_safety_2026-05-26.md)\n");
		}
	}
	mmIceSetCallbacks(mnVSNetAutomatchAMIceOnLocalCandidate, mnVSNetAutomatchAMIceOnGatheringDone, NULL);
	if (mmIceStartGathering() == FALSE)
	{
		port_log("SSB64 ICE: gathering start failed\n");
		mmIceShutdown();
		return FALSE;
	}
	/* Policy refreshed after gather in bind tick; signal local host when LAN-direct is likely. */
	mmIceSetCandidatePolicy(FALSE, (likely_lan != FALSE) ? TRUE : FALSE);
	return TRUE;
}

sb32 mnVSNetAutomatchAMIceBindTick(char *ice_sdp_out, u32 ice_sdp_cap)
{
	char sdp[4096];
	sb32 signal_local;

	(void)mmIcePoll();
	if (mmIceGatherFailed() != FALSE)
	{
		port_log("SSB64 ICE: async gather failed\n");
		return FALSE;
	}
	if (sIceGatheringDone == FALSE)
	{
		return FALSE;
	}
	mnVSNetAutomatchAMIceRefreshLocalLanFromGather();
	signal_local = mnVSNetAutomatchAMIceBindTickSignalLocal();
	mmIceSetCandidatePolicy(FALSE, signal_local);
	if (mmIceGetLocalDescription(sdp, sizeof(sdp)) == FALSE)
	{
		if (port_log_debug_active())
		{
			port_log("SSB64 ICE: mmIceGetLocalDescription failed (bind tick)\n");
		}
		return FALSE;
	}
	if (mmIceSdpHasIceUfrag(sdp) == FALSE)
	{
		port_log("SSB64 ICE: local description missing a=ice-ufrag (len=%zu), deferring queue\n", strlen(sdp));
		return FALSE;
	}
	if (ice_sdp_out != NULL && ice_sdp_cap > 0U)
	{
		snprintf(ice_sdp_out, ice_sdp_cap, "%s", sdp);
		if (signal_local == FALSE)
		{
			(void)mmIceFilterHostFromSignalingSdp(ice_sdp_out);
		}
		if (mmIceSdpHasIceUfrag(ice_sdp_out) == FALSE)
		{
			port_log("SSB64 ICE: queue SDP missing a=ice-ufrag after filter (len=%zu), deferring queue\n",
			         strlen(ice_sdp_out));
			return FALSE;
		}
	}
	return TRUE;
}

sb32 mnVSNetAutomatchAMIceBeginConnect(const MmMatchResult *mr)
{
	sb32 desc_applied;
	sb32 ok;
	sb32 ice_connect_ok;
	sb32 peer_ready;

	sIceRemoteDescApplied = FALSE;
	sIceRemoteGatheringDonePosted = FALSE;
	sIceTrickleQuietTicks = 0U;
	sIceCompletedSettleTicks = 0U;
	sIceLanRelaySettleTicks = 0U;
	sIceCompletedRemote[0] = '\0';
	sIceConnectFailReason = "ICE connection failed";
	sIcePeerSetupFailed = FALSE;
	sIceAwaitPeerControllingReady = FALSE;
	sIceAwaitPeerRoleReadyLogged = FALSE;
	sIceDeferredPeerSdp[0] = '\0';
	sIceHostRole = (mr != NULL && mr->you_are_host != FALSE) ? TRUE : FALSE;
	mnVSNetAutomatchAMIceRefreshLocalLanForPeer(mr);
	mnVSNetAutomatchAMIceApplyCandidatePolicy(mr);
	if (mr != NULL)
	{
		memcpy(&sIceValidateMatch, mr, sizeof(sIceValidateMatch));
		sIceValidateMatchActive = TRUE;
	}
	else
	{
		sIceValidateMatchActive = FALSE;
	}
	(void)mmIceSetIceControlling(sIceHostRole);
	port_log("SSB64 ICE: role=%s (you_are_host=%d)\n", (sIceHostRole != FALSE) ? "controlling" : "controlled",
	         (mr != NULL) ? (int)mr->you_are_host : 0);
	snprintf(sIceTicket, sizeof(sIceTicket), "%s", (mr != NULL) ? mr->ticket_id : "");
	mnVSNetAutomatchAMIceFlushPreTicketCandidates();
	mnVSNetAutomatchAMIceDrainRemoteCandidates();
	ok = TRUE;
	ice_connect_ok = (mr != NULL && mr->ice_connect_present != FALSE) ? TRUE : mmMatchmakingIceConnectPresent();
	if (ice_connect_ok == FALSE)
	{
		sIceConnectFailReason = "ICE server missing ice_connect (update matchmaking server)";
		port_log("SSB64 ICE: %s\n", sIceConnectFailReason);
		sIceValidateMatchActive = FALSE;
		return FALSE;
	}
	if (sIceHostRole != FALSE)
	{
		mmMatchmakingEnqueueIceRoleReady(FALSE, sIceTicket, "pair", 1U);
	}
	if ((mr != NULL) && (mr->peer_ice_sdp[0] != '\0'))
	{
		if (sIceHostRole != FALSE)
		{
			desc_applied = FALSE;
			if (mnVSNetAutomatchAMIceApplyPeerSdp(mr->peer_ice_sdp, &desc_applied) == FALSE)
			{
				ok = FALSE;
			}
			else if (desc_applied != FALSE)
			{
				sIceRemoteDescApplied = TRUE;
			}
		}
		else
		{
			peer_ready = (mr->ice_peer_controlling_ready != FALSE) ? TRUE : mmMatchmakingIcePeerControllingReady();
			if (peer_ready != FALSE)
			{
				desc_applied = FALSE;
				if (mnVSNetAutomatchAMIceApplyPeerSdp(mr->peer_ice_sdp, &desc_applied) == FALSE)
				{
					ok = FALSE;
				}
				else if (desc_applied != FALSE)
				{
					sIceRemoteDescApplied = TRUE;
				}
			}
			else
			{
				snprintf(sIceDeferredPeerSdp, sizeof(sIceDeferredPeerSdp), "%s", mr->peer_ice_sdp);
				sIceAwaitPeerControllingReady = TRUE;
				port_log("SSB64 ICE: deferring peer SDP until controlling role_ready\n");
			}
		}
	}
	else if (mr != NULL)
	{
		port_log("SSB64 ICE: match has no peer_ice_sdp\n");
	}
	mnVSNetAutomatchAMIceDrainRemoteCandidates();
	if (ok == FALSE)
	{
		sIceValidateMatchActive = FALSE;
	}
	return ok;
}

s32 mnVSNetAutomatchAMIceConnectTick(void)
{
	MmIceState st;

	if (sIcePeerSetupFailed != FALSE)
	{
		return -1;
	}
	if (sIceAwaitPeerControllingReady != FALSE)
	{
		sb32 desc_applied;

		if (mmMatchmakingIcePeerControllingReady() == FALSE)
		{
			if (sIceAwaitPeerRoleReadyLogged == FALSE)
			{
				port_log("SSB64 ICE: waiting for peer controlling role_ready\n");
				sIceAwaitPeerRoleReadyLogged = TRUE;
			}
			return 0;
		}
		if (sIceDeferredPeerSdp[0] == '\0')
		{
			sIceAwaitPeerControllingReady = FALSE;
		}
		else
		{
			desc_applied = FALSE;
			if (mnVSNetAutomatchAMIceApplyPeerSdp(sIceDeferredPeerSdp, &desc_applied) == FALSE)
			{
				return -1;
			}
			if (desc_applied != FALSE)
			{
				sIceRemoteDescApplied = TRUE;
			}
			sIceDeferredPeerSdp[0] = '\0';
			sIceAwaitPeerControllingReady = FALSE;
			mnVSNetAutomatchAMIceDrainRemoteCandidates();
			port_log("SSB64 ICE: peer SDP applied after controlling role_ready\n");
		}
	}
	st = mmIcePoll();
	mnVSNetAutomatchAMIceDrainRemoteCandidates();
	mnVSNetAutomatchAMIceMaybePostRemoteGatheringDone();
	if (st == MM_ICE_STATE_COMPLETED)
	{
		char remote[128];
		sb32 skip_path_validate;

		remote[0] = '\0';
		skip_path_validate = FALSE;
		if (mmIceGetSelectedPath(NULL, 0U, remote, sizeof(remote)) == FALSE || remote[0] == '\0')
		{
			if (sIceCompletedRemote[0] != '\0')
			{
				snprintf(remote, sizeof(remote), "%s", sIceCompletedRemote);
			}
			else if (sIceCompletedSettleTicks < 30U)
			{
				sIceCompletedSettleTicks++;
				mmIceLogSelectedCandidates();
				if (mmIceGetSelectedPath(NULL, 0U, remote, sizeof(remote)) != FALSE && remote[0] != '\0')
				{
					snprintf(sIceCompletedRemote, sizeof(sIceCompletedRemote), "%s", remote);
				}
				return 0;
			}
			else if (mmIceIsConnected() != FALSE)
			{
				port_log("SSB64 ICE: completed but selected path unavailable after settle (continuing)\n");
				skip_path_validate = TRUE;
			}
			else
			{
				sIceConnectFailReason = "ICE path validation failed (no nominated path)";
				port_log("SSB64 ICE: path validation failed (no selected remote)\n");
				sIceValidateMatchActive = FALSE;
				return -1;
			}
		}
		else
		{
			snprintf(sIceCompletedRemote, sizeof(sIceCompletedRemote), "%s", remote);
			sIceCompletedSettleTicks = 0U;
			port_log("SSB64 ICE: completed remote=%s\n", remote);
		}
		mmIceLogSelectedCandidates();
		if (sIceValidateMatchActive != FALSE)
		{
			if (skip_path_validate == FALSE)
			{
				char remote_typ[16];
				sb32 shared_lan;

				shared_lan = mnVSNetAutomatchAMIceDeriveAllowPeerHost(&sIceValidateMatch);
				remote_typ[0] = '\0';
				if ((shared_lan != FALSE) &&
				    (mmIceGetSelectedRemoteCandidateTyp(remote_typ, sizeof(remote_typ)) != FALSE) &&
				    (strcmp(remote_typ, "relay") == 0))
				{
					if (sIceLanRelaySettleTicks < ICE_LAN_RELAY_SETTLE_TICKS)
					{
						sIceLanRelaySettleTicks++;
						if (sIceLanRelaySettleTicks == 1U)
						{
							port_log(
							    "SSB64 ICE: shared LAN nominated relay remote=%s; waiting for host-pair renomination\n",
							    remote);
						}
						return 0;
					}
					port_log("SSB64 ICE: shared LAN still on relay after settle remote=%s\n", remote);
				}
				sIceLanRelaySettleTicks = 0U;
				if (mmIceValidateSelectedRemotePath(sIceValidateMatch.peer_hostport,
				                                       sIceValidateMatch.peer_lan_hostport, sIceLocalLan) == FALSE)
				{
					sIceConnectFailReason = "ICE path validation failed";
					port_log("SSB64 ICE: session abort (path validation failed)\n");
					sIceValidateMatchActive = FALSE;
					return -1;
				}
			}
			sIceValidateMatchActive = FALSE;
		}
		syNetPeerSetIceTransport(TRUE);
		return 1;
	}
	sIceCompletedSettleTicks = 0U;
	sIceLanRelaySettleTicks = 0U;
	if (st == MM_ICE_STATE_CONNECTED)
	{
		if (mnVSNetAutomatchAMIceEnvVerbose() != FALSE)
		{
			static u32 sIceConnectedLogTicks;

			sIceConnectedLogTicks++;
			if ((sIceConnectedLogTicks % 120U) == 1U)
			{
				port_log("SSB64 ICE: waiting for completed (state=connected)\n");
			}
		}
	}
	if (st == MM_ICE_STATE_FAILED)
	{
		sIceConnectFailReason = "ICE connection failed";
		port_log("SSB64 ICE: connection failed\n");
		return -1;
	}
	return 0;
}

const char *mnVSNetAutomatchAMIceConnectFailureReason(void)
{
	return sIceConnectFailReason;
}

void mnVSNetAutomatchAMIceNotifyPeerAbort(const MmMatchResult *mr)
{
	char bootstrap_bind[128];
	char peer_hp[128];

	if (mr == NULL)
	{
		return;
	}
	peer_hp[0] = '\0';
	if (mnVSNetAutomatchAMIcePickPeerHostport(mr, peer_hp, sizeof(peer_hp)) == NULL)
	{
		if ((mr->peer_lan_hostport[0] != '\0') && (strchr(mr->peer_lan_hostport, ':') != NULL))
		{
			snprintf(peer_hp, sizeof(peer_hp), "%s", mr->peer_lan_hostport);
		}
		else if ((mr->peer_hostport[0] != '\0') && (strchr(mr->peer_hostport, ':') != NULL))
		{
			snprintf(peer_hp, sizeof(peer_hp), "%s", mr->peer_hostport);
		}
	}
	if (peer_hp[0] == '\0')
	{
		return;
	}
	bootstrap_bind[0] = '\0';
	if (mmIceGetBootstrapBindHostport(peer_hp, bootstrap_bind, sizeof(bootstrap_bind)) == FALSE ||
	    bootstrap_bind[0] == '\0')
	{
		if ((sIceLocalLan[0] != '\0') && (strchr(sIceLocalLan, ':') != NULL))
		{
			snprintf(bootstrap_bind, sizeof(bootstrap_bind), "%s", sIceLocalLan);
		}
	}
	if (bootstrap_bind[0] == '\0')
	{
		port_log("SSB64 ICE: peer abort notify skipped (bind unresolved peer=%s)\n", peer_hp);
		return;
	}
	syNetPeerNotifyAutomatchBootstrapPeerAbort(bootstrap_bind, peer_hp, mr->session_id, mr->you_are_host);
	if (mr->ticket_id[0] != '\0')
	{
		mmMatchmakingEnqueueCancel(FALSE, mr->ticket_id);
	}
}

sb32 mnVSNetAutomatchAMIceBootstrapPeer(const MmMatchResult *mr, const char *bind)
{
	char remote[128];
	char bootstrap_bind[128];
	char peer_hp[128];

	(void)bind;
	if (mr == NULL)
	{
		return FALSE;
	}
	syNetPeerSetAutomatchBootstrapContext(mr->match_id, mr->ticket_id);
	if ((mmIceGetSelectedPath(NULL, 0U, remote, sizeof(remote)) == FALSE) || (remote[0] == '\0'))
	{
		if (sIceCompletedRemote[0] != '\0')
		{
			snprintf(remote, sizeof(remote), "%s", sIceCompletedRemote);
		}
		else
		{
			snprintf(remote, sizeof(remote), "%s", mr->peer_hostport);
		}
	}
	peer_hp[0] = '\0';
	if (mnVSNetAutomatchAMIcePickPeerHostport(mr, peer_hp, sizeof(peer_hp)) == NULL)
	{
		snprintf(peer_hp, sizeof(peer_hp), "%s", remote);
	}
	if (mmIceGetBootstrapBindHostport(peer_hp, bootstrap_bind, sizeof(bootstrap_bind)) == FALSE ||
	    bootstrap_bind[0] == '\0')
	{
		if ((sIceLocalLan[0] != '\0') && (strchr(sIceLocalLan, ':') != NULL))
		{
			snprintf(bootstrap_bind, sizeof(bootstrap_bind), "%s", sIceLocalLan);
		}
		else
		{
			port_log("SSB64 ICE: bootstrap bind unresolved (peer=%s)\n", peer_hp);
			return FALSE;
		}
	}
	if (syNetPeerConfigureUdpForAutomatch(bootstrap_bind, remote, mr->session_id, mr->you_are_host, 2U, TRUE) == FALSE)
	{
		port_log("SSB64 ICE: bootstrap configure failed bind=%s remote=%s session=%u host=%d\n", bootstrap_bind, remote,
		         (unsigned int)mr->session_id, (int)mr->you_are_host);
		return FALSE;
	}
	if (syNetPeerRunBootstrap() == FALSE)
	{
		port_log("SSB64 ICE: bootstrap run failed bind=%s remote=%s\n", bootstrap_bind, remote);
		return FALSE;
	}
	return TRUE;
}

u32 mnVSNetAutomatchAMIceConnectTricklePollInterval(void)
{
	/* After both sides posted gathering-done, trickle polls only burn worker/HTTP; slow down until completed. */
	if (mmIceIsCompleted() != FALSE)
	{
		return 0U;
	}
	if ((mmIceIsConnected() != FALSE) && (sIceRemoteGatheringDonePosted != FALSE) &&
	    (mmMatchmakingIceSignalsQueuedCount() == 0U))
	{
		return 16U;
	}
	if (mmIceIsConnected() != FALSE)
	{
		return 8U;
	}
	return 2U;
}

#endif /* PORT && SSB64_NETMENU && SSB64_NETPLAY_ICE */
