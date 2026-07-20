#include "mm_ice.h"
#include "mm_lan_detect.h"

#if defined(PORT) && defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE)

#include <juice/juice.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <wchar.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <unistd.h>
#endif

#ifdef PORT
extern void port_log(const char *fmt, ...);
#endif

extern int atoi(const char *str);

#define MM_ICE_RECV_QUEUE 64
#define MM_ICE_RECV_MAX 2048
#define MM_ICE_CANDIDATE_QUEUE 32
#define MM_ICE_CANDIDATE_MAX 280

typedef struct MmIceRecvSlot
{
	u8 data[MM_ICE_RECV_MAX];
	u32 len;
} MmIceRecvSlot;

typedef struct MmIceCandidateSlot
{
	char sdp[MM_ICE_CANDIDATE_MAX];
} MmIceCandidateSlot;

static juice_agent_t *sAgent;
/* Protects port-owned recv/candidate queues and callback-side flags only — never held across juice_* calls. */
static pthread_mutex_t sIceMutex = PTHREAD_MUTEX_INITIALIZER;
static MmIceState sIceState = MM_ICE_STATE_IDLE;
static sb32 sGatheringDonePosted;
static MmIceOnLocalCandidateFn sOnCandidate;
static MmIceOnGatheringDoneFn sOnGatheringDone;
static void *sCallbackUser;

static MmIceRecvSlot sRecvQ[MM_ICE_RECV_QUEUE];
static u32 sRecvHead;
static u32 sRecvTail;
static u32 sRecvCount;

static MmIceCandidateSlot sCandQ[MM_ICE_CANDIDATE_QUEUE];
static u32 sCandHead;
static u32 sCandTail;
static u32 sCandCount;
/*
 * Drain scratch for mmIcePoll — NOT a stack local. Each slot is MM_ICE_CANDIDATE_MAX
 * (280) bytes × 32 ≈ 9 KB. Poll runs from the VS GObj coroutine (PORT_STACK_GOBJ =
 * 64 KB) after deep snapshot-apply frames; a 9 KB frame tips the stack into the
 * PROT_NONE guard → SIGSEGV with garbage FP (= just-logged figh hash). Single-threaded
 * game poll; reentrancy guarded in mmIcePoll. See
 * docs/bugs/netplay_mmicepoll_cand_copy_stack_overflow_2026-07-12.md.
 */
static MmIceCandidateSlot sCandDrainCopy[MM_ICE_CANDIDATE_QUEUE];
static sb32 sCandDrainActive;

/* Last juice_send status for logging (not errno). */
static int sIceLastSendJuiceErr;

static char sStunHost[128];
static u16 sStunPort;
static juice_turn_server_t sTurnServers[2];
static int sTurnServerCount;
static u16 sBindPortBegin;
static u16 sBindPortEnd;
static char sLastSrflxHostport[128];
static char sLastRelayHostport[128];
static sb32 sAllowPeerHostCandidates = TRUE;
static sb32 sSignalLocalHostCandidates = TRUE;
static char sPeerLanFilterHostport[128];
static char sLocalLanFilterHostport[128];
static u32 sFilteredRemoteHostCount;
static u32 sFilteredRemoteRelayCount;
static sb32 sPendingStateLog;
static juice_state_t sPendingStateLogValue;
static char sBindAddressStorage[64];
static sb32 sLanDirectGather;
static sb32 sActiveGatherLanDirect;

static pthread_t sGatherThread;
static volatile sb32 sGatherThreadActive;
/* 0 = unknown/in progress, 1 = ok, -1 = failed */
static volatile int sGatherThreadResult;

/* Signaling omits end-of-candidates until mmIceSetRemoteGatheringDone (trickle). */
static void mmIceStripEndOfCandidates(char *sdp)
{
	char *scan;
	char *dst;

	if ((sdp == NULL) || (sdp[0] == '\0'))
	{
		return;
	}
	dst = sdp;
	for (scan = sdp; *scan != '\0';)
	{
		if (strncmp(scan, "a=end-of-candidates", 19) == 0)
		{
			while (*scan != '\0' && *scan != '\n' && *scan != '\r')
			{
				scan++;
			}
			while (*scan == '\n' || *scan == '\r')
			{
				scan++;
			}
			continue;
		}
		*dst++ = *scan++;
	}
	*dst = '\0';
}

static MmIceState mmIceMapJuiceState(juice_state_t st)
{
	switch (st)
	{
	case JUICE_STATE_DISCONNECTED:
		return MM_ICE_STATE_IDLE;
	case JUICE_STATE_GATHERING:
		return MM_ICE_STATE_GATHERING;
	case JUICE_STATE_CONNECTING:
		return MM_ICE_STATE_CONNECTING;
	case JUICE_STATE_CONNECTED:
		return MM_ICE_STATE_CONNECTED;
	case JUICE_STATE_COMPLETED:
		return MM_ICE_STATE_COMPLETED;
	case JUICE_STATE_FAILED:
		return MM_ICE_STATE_FAILED;
	default:
		return MM_ICE_STATE_IDLE;
	}
}

static void mmIceQueueRecvLocked(const char *data, size_t size)
{
	if ((data == NULL) || (size == 0U) || (size > MM_ICE_RECV_MAX))
	{
		return;
	}
	if (sRecvCount >= MM_ICE_RECV_QUEUE)
	{
		sRecvHead = (sRecvHead + 1U) % MM_ICE_RECV_QUEUE;
		sRecvCount--;
	}
	memcpy(sRecvQ[sRecvTail].data, data, size);
	sRecvQ[sRecvTail].len = (u32)size;
	sRecvTail = (sRecvTail + 1U) % MM_ICE_RECV_QUEUE;
	sRecvCount++;
}

static void mmIceQueueCandidateLocked(const char *sdp)
{
	if ((sdp == NULL) || (sdp[0] == '\0'))
	{
		return;
	}
	if (sCandCount >= MM_ICE_CANDIDATE_QUEUE)
	{
		sCandHead = (sCandHead + 1U) % MM_ICE_CANDIDATE_QUEUE;
		sCandCount--;
	}
	snprintf(sCandQ[sCandTail].sdp, sizeof(sCandQ[sCandTail].sdp), "%s", sdp);
	sCandTail = (sCandTail + 1U) % MM_ICE_CANDIDATE_QUEUE;
	sCandCount++;
}

static sb32 mmIceParseCandidateFields(const char *sdp, char *typ_out, u32 typ_cap, char *addr_out, u32 addr_cap)
{
	const char *p;
	int foundation;
	int comp_id;
	char transport[16];
	unsigned long priority;
	char addr[64];
	unsigned port;
	char typ[16];

	if (sdp == NULL)
	{
		return FALSE;
	}
	p = strstr(sdp, "candidate:");
	if (p != NULL)
	{
		p += 10;
	}
	else
	{
		p = sdp;
	}
	if (sscanf(p, "%d %d %15s %lu %63s %u typ %15s", &foundation, &comp_id, transport, &priority, addr, &port, typ) < 7)
	{
		return FALSE;
	}
	if (typ_out != NULL && typ_cap > 0U)
	{
		snprintf(typ_out, typ_cap, "%s", typ);
	}
	if (addr_out != NULL && addr_cap > 0U)
	{
		snprintf(addr_out, addr_cap, "%s:%u", addr, port);
	}
	return TRUE;
}

static sb32 mmIceTryParseSrflxFromCandidate(const char *sdp, char *out, u32 out_cap)
{
	char typ[16];

	if ((out == NULL) || (out_cap < 8U))
	{
		return FALSE;
	}
	if (mmIceParseCandidateFields(sdp, typ, sizeof(typ), out, out_cap) == FALSE)
	{
		return FALSE;
	}
	return (strcmp(typ, "srflx") == 0) ? TRUE : FALSE;
}

static sb32 mmIceLineIsCandidateAttribute(const char *line)
{
	const char *p;

	if (line == NULL)
	{
		return FALSE;
	}
	p = line;
	while (*p == ' ' || *p == '\t')
	{
		p++;
	}
	return (strncmp(p, "a=candidate:", 12) == 0) ? TRUE : FALSE;
}

static sb32 mmIceCandidateLineIsHost(const char *line)
{
	char typ[16];

	if (mmIceLineIsCandidateAttribute(line) == FALSE)
	{
		return FALSE;
	}
	if (mmIceParseCandidateFields(line, typ, sizeof(typ), NULL, 0U) == FALSE)
	{
		return FALSE;
	}
	return (strcmp(typ, "host") == 0) ? TRUE : FALSE;
}

static sb32 mmIceCandidateLineIsRelay(const char *line)
{
	char typ[16];

	if (mmIceLineIsCandidateAttribute(line) == FALSE)
	{
		return FALSE;
	}
	if (mmIceParseCandidateFields(line, typ, sizeof(typ), NULL, 0U) == FALSE)
	{
		return FALSE;
	}
	return (strcmp(typ, "relay") == 0) ? TRUE : FALSE;
}

static void mmIceExtractIceSessionLines(const char *sdp, char *out, u32 out_cap)
{
	const char *scan;

	if ((sdp == NULL) || (out == NULL) || (out_cap < 2U))
	{
		return;
	}
	out[0] = '\0';
	for (scan = sdp; *scan != '\0';)
	{
		const char *line_end = scan;
		size_t line_len;

		while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r')
		{
			line_end++;
		}
		line_len = (size_t)(line_end - scan);
		if ((line_len >= 12U) && (strncmp(scan, "a=ice-ufrag:", 12) == 0))
		{
			if (((size_t)strlen(out) + line_len + 2U) < (size_t)out_cap)
			{
				snprintf(out + strlen(out), out_cap - (u32)strlen(out), "%.*s\n", (int)line_len, scan);
			}
		}
		else if ((line_len >= 10U) && (strncmp(scan, "a=ice-pwd:", 10) == 0))
		{
			if (((size_t)strlen(out) + line_len + 2U) < (size_t)out_cap)
			{
				snprintf(out + strlen(out), out_cap - (u32)strlen(out), "%.*s\n", (int)line_len, scan);
			}
		}
		if (*line_end == '\0')
		{
			break;
		}
		scan = line_end + 1;
		while (*scan == '\n' || *scan == '\r')
		{
			scan++;
		}
	}
}

static void mmIceStripHostLinesFromSdp(char *sdp, sb32 count_filtered)
{
	char *scan;
	char *dst;

	if ((sdp == NULL) || (sdp[0] == '\0'))
	{
		return;
	}
	dst = sdp;
	for (scan = sdp; *scan != '\0';)
	{
		char *line_end = scan;

		while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r')
		{
			line_end++;
		}
		if (mmIceCandidateLineIsHost(scan) != FALSE)
		{
			if (count_filtered != FALSE)
			{
				sFilteredRemoteHostCount++;
			}
			scan = line_end;
			while (*scan == '\n' || *scan == '\r')
			{
				scan++;
			}
			continue;
		}
		while (scan < line_end)
		{
			*dst++ = *scan++;
		}
		while (*scan == '\n' || *scan == '\r')
		{
			*dst++ = *scan++;
		}
	}
	*dst = '\0';
}

static void mmIceStripHostCandidatesFromSdp(char *sdp)
{
	if ((sdp == NULL) || (sdp[0] == '\0') || (sAllowPeerHostCandidates != FALSE))
	{
		return;
	}
	mmIceStripHostLinesFromSdp(sdp, TRUE);
}

static void mmIceStripRelayLinesFromSdp(char *sdp, sb32 count_filtered)
{
	char *scan;
	char *dst;

	if ((sdp == NULL) || (sdp[0] == '\0'))
	{
		return;
	}
	dst = sdp;
	for (scan = sdp; *scan != '\0';)
	{
		char *line_end = scan;

		while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r')
		{
			line_end++;
		}
		if (mmIceCandidateLineIsRelay(scan) != FALSE)
		{
			if (count_filtered != FALSE)
			{
				sFilteredRemoteRelayCount++;
			}
			scan = line_end;
			while (*scan == '\n' || *scan == '\r')
			{
				scan++;
			}
			continue;
		}
		while (scan < line_end)
		{
			*dst++ = *scan++;
		}
		while (*scan == '\n' || *scan == '\r')
		{
			*dst++ = *scan++;
		}
	}
	*dst = '\0';
}

static void mmIceStripRelayCandidatesFromSdp(char *sdp)
{
	if ((sdp == NULL) || (sdp[0] == '\0') || (sAllowPeerHostCandidates == FALSE))
	{
		return;
	}
	mmIceStripRelayLinesFromSdp(sdp, TRUE);
}

static sb32 mmIceHostIpv4ExactMatch(const char *candidate_addr, const char *lan_ref_hostport)
{
	char cand_ip[64];
	char lan_ip[64];
	struct in_addr cand_addr;
	struct in_addr lan_addr;
	const char *colon;
	const char *lan_colon;

	colon = strrchr(candidate_addr, ':');
	lan_colon = strrchr(lan_ref_hostport, ':');
	if ((colon == NULL) || (colon <= candidate_addr) || (lan_colon == NULL) || (lan_colon <= lan_ref_hostport))
	{
		return FALSE;
	}
	snprintf(cand_ip, sizeof(cand_ip), "%.*s", (int)(colon - candidate_addr), candidate_addr);
	snprintf(lan_ip, sizeof(lan_ip), "%.*s", (int)(lan_colon - lan_ref_hostport), lan_ref_hostport);
	if (inet_pton(AF_INET, cand_ip, &cand_addr) != 1 || inet_pton(AF_INET, lan_ip, &lan_addr) != 1)
	{
		return FALSE;
	}
	return (cand_addr.s_addr == lan_addr.s_addr) ? TRUE : FALSE;
}

static sb32 mmIceHostCandidateAllowedForPeerLan(const char *candidate_sdp, const char *peer_lan_hostport)
{
	char typ[16];
	char addr[128];
	char ip[64];
	const char *colon;

	if ((peer_lan_hostport == NULL) || (peer_lan_hostport[0] == '\0'))
	{
		return TRUE;
	}
	if (mmIceParseCandidateFields(candidate_sdp, typ, sizeof(typ), addr, sizeof(addr)) == FALSE)
	{
		return TRUE;
	}
	if (strcmp(typ, "host") != 0)
	{
		return TRUE;
	}
	colon = strrchr(addr, ':');
	if (colon == NULL || colon <= addr)
	{
		return FALSE;
	}
	snprintf(ip, sizeof(ip), "%.*s", (int)(colon - addr), addr);
	if (mmLanIpv4StringIsRfc1918(ip) == FALSE)
	{
		return FALSE;
	}
	if (mmIceHostIpv4ExactMatch(addr, peer_lan_hostport) != FALSE)
	{
		return TRUE;
	}
	return mmLanIpv4HostportsOnSameSharedLanSegment(addr, peer_lan_hostport);
}

static sb32 mmIceHostCandidateAllowedForLocalLan(const char *candidate_sdp, const char *local_lan_hostport)
{
	char typ[16];
	char addr[128];

	if ((local_lan_hostport == NULL) || (local_lan_hostport[0] == '\0'))
	{
		return TRUE;
	}
	if (mmIceParseCandidateFields(candidate_sdp, typ, sizeof(typ), addr, sizeof(addr)) == FALSE)
	{
		return TRUE;
	}
	if (strcmp(typ, "host") != 0)
	{
		return TRUE;
	}
	if (mmIceHostIpv4ExactMatch(addr, local_lan_hostport) != FALSE)
	{
		return TRUE;
	}
	return mmLanPeerSharesLocalLanSubnet(addr, local_lan_hostport);
}

static sb32 mmIceSdpHasAllowedRemotePeerHost(const char *sdp, const char *peer_lan_hostport)
{
	const char *scan;

	if ((sdp == NULL) || (sdp[0] == '\0') || (peer_lan_hostport == NULL) || (peer_lan_hostport[0] == '\0'))
	{
		return FALSE;
	}
	for (scan = sdp; *scan != '\0';)
	{
		const char *line_end = scan;

		while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r')
		{
			line_end++;
		}
		if ((mmIceCandidateLineIsHost(scan) != FALSE) &&
		    (mmIceHostCandidateAllowedForPeerLan(scan, peer_lan_hostport) != FALSE))
		{
			return TRUE;
		}
		scan = line_end;
		while (*scan == '\n' || *scan == '\r')
		{
			scan++;
		}
	}
	return FALSE;
}

static void mmIceStripMisalignedHostLinesFromSdp(char *sdp, const char *lan_ref_hostport, sb32 count_filtered,
                                                 sb32 remote_peer_lan)
{
	char *scan;
	char *dst;

	if ((sdp == NULL) || (sdp[0] == '\0') || (lan_ref_hostport == NULL) || (lan_ref_hostport[0] == '\0'))
	{
		return;
	}
	dst = sdp;
	for (scan = sdp; *scan != '\0';)
	{
		char *line_end = scan;
		sb32 allowed;

		while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r')
		{
			line_end++;
		}
		allowed = (remote_peer_lan != FALSE) ? mmIceHostCandidateAllowedForPeerLan(scan, lan_ref_hostport)
		                                     : mmIceHostCandidateAllowedForLocalLan(scan, lan_ref_hostport);
		if ((mmIceCandidateLineIsHost(scan) != FALSE) && (allowed == FALSE))
		{
			if (count_filtered != FALSE)
			{
				sFilteredRemoteHostCount++;
			}
			scan = line_end;
			while (*scan == '\n' || *scan == '\r')
			{
				scan++;
			}
			continue;
		}
		while (scan < line_end)
		{
			*dst++ = *scan++;
		}
		while (*scan == '\n' || *scan == '\r')
		{
			*dst++ = *scan++;
		}
	}
	*dst = '\0';
}

static void mmIceEnsurePeerLanRemoteCandidate(juice_agent_t *agent, sb32 peer_host_missing)
{
	char cand[MM_ICE_CANDIDATE_MAX];
	char ip[64];
	const char *colon;
	unsigned port;
	int juice_ret;

	if ((agent == NULL) || (peer_host_missing == FALSE) || (sPeerLanFilterHostport[0] == '\0') ||
	    (sAllowPeerHostCandidates == FALSE))
	{
		return;
	}
	colon = strrchr(sPeerLanFilterHostport, ':');
	if (colon == NULL || colon <= sPeerLanFilterHostport)
	{
		return;
	}
	snprintf(ip, sizeof(ip), "%.*s", (int)(colon - sPeerLanFilterHostport), sPeerLanFilterHostport);
	port = (unsigned)atoi(colon + 1);
	if (port == 0U)
	{
		return;
	}
	snprintf(cand, sizeof(cand), "a=candidate:0 1 UDP 2130706431 %s %u typ host", ip, port);
	juice_ret = juice_add_remote_candidate(agent, cand);
	if (juice_ret != 0)
	{
#ifdef PORT
		port_log("SSB64 ICE: peer_lan remote candidate add failed peer_lan=%s juice_ret=%d\n", sPeerLanFilterHostport,
		         juice_ret);
#endif
		return;
	}
#ifdef PORT
	port_log("SSB64 ICE: ensured peer_lan remote candidate %s\n", sPeerLanFilterHostport);
#endif
}

void mmIceSetCandidatePolicy(sb32 allow_peer_host, sb32 signal_local_host, const char *peer_lan_hostport,
                             const char *local_lan_hostport)
{
	sAllowPeerHostCandidates = (allow_peer_host != FALSE) ? TRUE : FALSE;
	sSignalLocalHostCandidates = (signal_local_host != FALSE) ? TRUE : FALSE;
	sPeerLanFilterHostport[0] = '\0';
	sLocalLanFilterHostport[0] = '\0';
	if ((peer_lan_hostport != NULL) && (peer_lan_hostport[0] != '\0') && (sAllowPeerHostCandidates != FALSE))
	{
		snprintf(sPeerLanFilterHostport, sizeof(sPeerLanFilterHostport), "%s", peer_lan_hostport);
	}
	if ((local_lan_hostport != NULL) && (local_lan_hostport[0] != '\0'))
	{
		snprintf(sLocalLanFilterHostport, sizeof(sLocalLanFilterHostport), "%s", local_lan_hostport);
	}
	sFilteredRemoteHostCount = 0U;
	sFilteredRemoteRelayCount = 0U;
}

sb32 mmIceFilterHostFromSignalingSdp(char *sdp)
{
	if ((sdp == NULL) || (sdp[0] == '\0'))
	{
		return TRUE;
	}
	if (sSignalLocalHostCandidates == FALSE)
	{
		mmIceStripHostLinesFromSdp(sdp, FALSE);
#ifdef PORT
		port_log("SSB64 ICE: omitted local host candidate(s) from signaling SDP (no local LAN)\n");
#endif
		return TRUE;
	}
	if (sLocalLanFilterHostport[0] != '\0')
	{
		mmIceStripMisalignedHostLinesFromSdp(sdp, sLocalLanFilterHostport, FALSE, FALSE);
#ifdef PORT
		port_log("SSB64 ICE: stripped non-local-LAN host candidate(s) from signaling SDP (local_lan=%s)\n",
		         sLocalLanFilterHostport);
#endif
	}
	return TRUE;
}

sb32 mmIceShouldAcceptRemoteCandidate(const char *candidate_sdp)
{
	char typ[16];

	if (mmIceParseCandidateFields(candidate_sdp, typ, sizeof(typ), NULL, 0U) == FALSE)
	{
		return TRUE;
	}
	/* Shared LAN: accept host/srflx; drop peer relay trickle so libjuice does not nominate TURN. */
	if ((sAllowPeerHostCandidates != FALSE) && (strcmp(typ, "relay") == 0))
	{
		return FALSE;
	}
	if (sAllowPeerHostCandidates != FALSE)
	{
		if ((strcmp(typ, "host") == 0) && (sPeerLanFilterHostport[0] != '\0') &&
		    (mmIceHostCandidateAllowedForPeerLan(candidate_sdp, sPeerLanFilterHostport) == FALSE))
		{
			sFilteredRemoteHostCount++;
			return FALSE;
		}
		return TRUE;
	}
	if (strcmp(typ, "host") != 0)
	{
		return TRUE;
	}
	sFilteredRemoteHostCount++;
	return FALSE;
}

sb32 mmIceShouldSignalLocalCandidate(const char *candidate_sdp)
{
	char typ[16];

	if (mmIceParseCandidateFields(candidate_sdp, typ, sizeof(typ), NULL, 0U) == FALSE)
	{
		return TRUE;
	}
	/* Shared LAN: do not signal local relay; peer should use host/srflx only. */
	if ((sAllowPeerHostCandidates != FALSE) && (strcmp(typ, "relay") == 0))
	{
		return FALSE;
	}
	if (sSignalLocalHostCandidates != FALSE)
	{
		if ((strcmp(typ, "host") == 0) && (sLocalLanFilterHostport[0] != '\0') &&
		    (mmIceHostCandidateAllowedForLocalLan(candidate_sdp, sLocalLanFilterHostport) == FALSE))
		{
			return FALSE;
		}
		return TRUE;
	}
	return (strcmp(typ, "host") != 0) ? TRUE : FALSE;
}

static void mmIceOnStateChanged(juice_agent_t *agent, juice_state_t state, void *user_ptr)
{
	(void)agent;
	(void)user_ptr;
	(void)pthread_mutex_lock(&sIceMutex);
	sIceState = mmIceMapJuiceState(state);
	sPendingStateLogValue = state;
	sPendingStateLog = TRUE;
	(void)pthread_mutex_unlock(&sIceMutex);
}

static void mmIceFlushPendingStateLog(void)
{
	juice_state_t log_state;
	sb32 do_log;

	do_log = FALSE;
	(void)pthread_mutex_lock(&sIceMutex);
	if (sPendingStateLog != FALSE)
	{
		log_state = sPendingStateLogValue;
		sPendingStateLog = FALSE;
		do_log = TRUE;
	}
	(void)pthread_mutex_unlock(&sIceMutex);
#ifdef PORT
	if (do_log != FALSE)
	{
		port_log("SSB64 ICE: state=%s\n", juice_state_to_string(log_state));
	}
#endif
}

static void mmIceOnCandidate(juice_agent_t *agent, const char *sdp, void *user_ptr)
{
	char srflx[128];

	(void)agent;
	(void)user_ptr;
	(void)pthread_mutex_lock(&sIceMutex);
	if (mmIceTryParseSrflxFromCandidate(sdp, srflx, sizeof(srflx)) != FALSE)
	{
		snprintf(sLastSrflxHostport, sizeof(sLastSrflxHostport), "%s", srflx);
	}
	else
	{
		char typ[16];
		char relay_hp[128];

		if (mmIceParseCandidateFields(sdp, typ, sizeof(typ), relay_hp, sizeof(relay_hp)) != FALSE &&
		    strcmp(typ, "relay") == 0 && relay_hp[0] != '\0')
		{
			snprintf(sLastRelayHostport, sizeof(sLastRelayHostport), "%s", relay_hp);
#ifdef PORT
			port_log("SSB64 ICE: local relay candidate gathered\n");
#endif
		}
	}
	mmIceQueueCandidateLocked(sdp);
	(void)pthread_mutex_unlock(&sIceMutex);
}

static void mmIceOnGatheringDone(juice_agent_t *agent, void *user_ptr)
{
	(void)agent;
	(void)user_ptr;
	(void)pthread_mutex_lock(&sIceMutex);
	sGatheringDonePosted = TRUE;
	(void)pthread_mutex_unlock(&sIceMutex);
}

static void mmIceOnRecv(juice_agent_t *agent, const char *data, size_t size, void *user_ptr)
{
	(void)agent;
	(void)user_ptr;
	(void)pthread_mutex_lock(&sIceMutex);
	mmIceQueueRecvLocked(data, size);
	(void)pthread_mutex_unlock(&sIceMutex);
}

static juice_agent_t *mmIceAgentSnapshot(void)
{
	return sAgent;
}

static void mmIceGatherJoinLocked(void)
{
	if (sGatherThreadActive != FALSE)
	{
		(void)pthread_join(sGatherThread, NULL);
		sGatherThreadActive = FALSE;
	}
}

static void *mmIceGatherThreadEntry(void *arg)
{
	juice_agent_t *agent;
	int ret;

	agent = (juice_agent_t *)arg;
	if (agent != NULL)
	{
		ret = juice_gather_candidates(agent);
		sGatherThreadResult = (ret == 0) ? 1 : -1;
	}
	else
	{
		sGatherThreadResult = -1;
	}
	sGatherThreadActive = FALSE;
	return NULL;
}

static sb32 mmIceParsePortRangeSpec(const char *port_spec, u16 *port_begin, u16 *port_end)
{
	unsigned long min_p;
	unsigned long max_p;
	const char *dash;

	if ((port_spec == NULL) || (port_begin == NULL) || (port_end == NULL))
	{
		return FALSE;
	}
	dash = strchr(port_spec, '-');
	if (dash != NULL)
	{
		if (sscanf(port_spec, "%lu-%lu", &min_p, &max_p) != 2 || min_p > max_p || max_p > 65535UL)
		{
			return FALSE;
		}
		*port_begin = (u16)min_p;
		*port_end = (u16)max_p;
		return TRUE;
	}
	if (sscanf(port_spec, "%lu", &min_p) != 1 || min_p > 65535UL)
	{
		return FALSE;
	}
	*port_begin = (u16)min_p;
	*port_end = (u16)min_p;
	return TRUE;
}

static sb32 mmIceResolveBindPorts(const char *bind_hostport, u16 *port_begin, u16 *port_end)
{
	const char *colon;
	const char *env_range;
	unsigned long min_p;
	unsigned long max_p;

	if ((port_begin == NULL) || (port_end == NULL))
	{
		return FALSE;
	}
	*port_begin = 0U;
	*port_end = 0U;
	if ((bind_hostport == NULL) || (bind_hostport[0] == '\0'))
	{
		return TRUE;
	}
	colon = strrchr(bind_hostport, ':');
	if (colon == NULL)
	{
		return TRUE;
	}
	if (colon[1] == '\0')
	{
		return TRUE;
	}
	if (mmIceParsePortRangeSpec(colon + 1, port_begin, port_end) == FALSE)
	{
		return FALSE;
	}
	if ((*port_begin == 0U) && (*port_end == 0U))
	{
		env_range = getenv("SSB64_MATCHMAKING_PORT_RANGE");
		if ((env_range != NULL) && (env_range[0] != '\0') &&
		    sscanf(env_range, "%lu-%lu", &min_p, &max_p) == 2 && min_p <= max_p && max_p <= 65535UL)
		{
			*port_begin = (u16)min_p;
			*port_end = (u16)max_p;
		}
	}
	return TRUE;
}

static sb32 mmIceParseBindPort(const char *bind_hostport, u16 *out_port)
{
	u16 port_begin;
	u16 port_end;

	if ((bind_hostport == NULL) || (out_port == NULL))
	{
		return FALSE;
	}
	if (mmIceResolveBindPorts(bind_hostport, &port_begin, &port_end) == FALSE)
	{
		return FALSE;
	}
	*out_port = port_begin;
	return TRUE;
}

/* Returns TRUE when host is a concrete IPv4 (not 0.0.0.0). */
static sb32 mmIceParseBindHost(const char *bind_hostport, char *host_out, u32 host_cap)
{
	const char *colon;
	size_t host_len;
	struct in_addr addr;

	if ((bind_hostport == NULL) || (host_out == NULL) || (host_cap < 8U))
	{
		return FALSE;
	}
	host_out[0] = '\0';
	colon = strrchr(bind_hostport, ':');
	if (colon == NULL || colon == bind_hostport)
	{
		return FALSE;
	}
	host_len = (size_t)(colon - bind_hostport);
	if (host_len == 0U || host_len >= host_cap)
	{
		return FALSE;
	}
	memcpy(host_out, bind_hostport, host_len);
	host_out[host_len] = '\0';
	if (strcmp(host_out, "0.0.0.0") == 0)
	{
		host_out[0] = '\0';
		return FALSE;
	}
	if (inet_pton(AF_INET, host_out, &addr) != 1)
	{
		host_out[0] = '\0';
		return FALSE;
	}
	return TRUE;
}

static sb32 mmIceEnvLanDirectEnabled(void)
{
	const char *e;

	e = getenv("SSB64_MATCHMAKING_ICE_LAN_DIRECT");
	if (e == NULL || e[0] == '\0')
	{
		return TRUE;
	}
	return (atoi(e) != 0) ? TRUE : FALSE;
}

static void mmIceResolveBindAddress(const char *bind_hostport, char *storage, u32 storage_cap)
{
	const char *lan_env;
	char host[64];
	struct in_addr addr;

	storage[0] = '\0';
	if (mmIceParseBindHost(bind_hostport, host, sizeof(host)) != FALSE)
	{
		snprintf(storage, storage_cap, "%s", host);
		return;
	}
	lan_env = getenv("SSB64_MATCHMAKING_LAN_ENDPOINT");
	if (lan_env != NULL && lan_env[0] != '\0')
	{
		const char *colon;

		colon = strrchr(lan_env, ':');
		if (colon != NULL && colon > lan_env)
		{
			snprintf(host, sizeof(host), "%.*s", (int)(colon - lan_env), lan_env);
		}
		else
		{
			snprintf(host, sizeof(host), "%s", lan_env);
		}
		if (host[0] != '\0' && strcmp(host, "0.0.0.0") != 0 && inet_pton(AF_INET, host, &addr) == 1)
		{
			snprintf(storage, storage_cap, "%s", host);
		}
	}
}

void mmIceSetLanDirectGather(sb32 enabled)
{
	sLanDirectGather = (enabled != FALSE) ? TRUE : FALSE;
}

sb32 mmIceParseBindHostFromSpec(const char *bind_hostport, char *host_out, u32 host_cap)
{
	return mmIceParseBindHost(bind_hostport, host_out, host_cap);
}

sb32 mmIceParseBindPortFromSpec(const char *bind_hostport, u16 *out_port)
{
	return mmIceParseBindPort(bind_hostport, out_port);
}

sb32 mmIceIsFailed(void)
{
	MmIceState st;

	st = mmIcePoll();
	return (st == MM_ICE_STATE_FAILED) ? TRUE : FALSE;
}

static void mmIceLoadStunFromEnv(void)
{
	const char *e;
	const char *comma;
	size_t host_len;
	char *colon_mut;

	e = getenv("SSB64_MATCHMAKING_STUN_SERVERS");
	if ((e == NULL) || (e[0] == '\0'))
	{
		const char *coturn;

		coturn = getenv("SSB64_MATCHMAKING_TURN_HOST");
		if ((coturn == NULL) || (coturn[0] == '\0'))
		{
			coturn = MM_ICE_DEFAULT_COTURN_HOST;
		}
		snprintf(sStunHost, sizeof(sStunHost), "%s", coturn);
		sStunPort = MM_ICE_DEFAULT_STUN_PORT;
		return;
	}
	while (*e == ' ')
	{
		e++;
	}
	comma = strchr(e, ',');
	host_len = (comma != NULL) ? (size_t)(comma - e) : strlen(e);
	if (host_len >= sizeof(sStunHost))
	{
		host_len = sizeof(sStunHost) - 1U;
	}
	memcpy(sStunHost, e, host_len);
	sStunHost[host_len] = '\0';
	colon_mut = strchr(sStunHost, ':');
	if (colon_mut != NULL)
	{
		*colon_mut = '\0';
		sStunPort = (u16)atoi(colon_mut + 1);
	}
	else
	{
		sStunPort = 19302U;
	}
}

static void mmIceLoadTurnFromEnv(void)
{
	const char *host;
	const char *user;
	const char *pass;
	const char *port_e;
	unsigned long p;

	sTurnServerCount = 0;
	host = getenv("SSB64_MATCHMAKING_TURN_HOST");
	user = getenv("SSB64_MATCHMAKING_TURN_USER");
	pass = getenv("SSB64_MATCHMAKING_TURN_PASS");
	port_e = getenv("SSB64_MATCHMAKING_TURN_PORT");
	if ((host == NULL) || (host[0] == '\0'))
	{
		host = MM_ICE_DEFAULT_COTURN_HOST;
	}
	if ((user == NULL) || (user[0] == '\0') || (pass == NULL) || (pass[0] == '\0'))
	{
		sTurnServerCount = 0;
		return;
	}
	p = (unsigned long)MM_ICE_DEFAULT_TURN_PORT;
	if ((port_e != NULL) && (port_e[0] != '\0'))
	{
		p = (unsigned long)atoi(port_e);
	}
	if ((p == 0UL) || (p > 65535UL))
	{
		p = 3478UL;
	}
	sTurnServers[0].host = host;
	sTurnServers[0].username = user;
	sTurnServers[0].password = pass;
	sTurnServers[0].port = (uint16_t)p;
	sTurnServerCount = 1;
}

static juice_concurrency_mode_t mmIceSelectConcurrencyMode(void)
{
#if defined(__ANDROID__)
	const char *env;

	env = getenv("SSB64_MATCHMAKING_ICE_CONCURRENCY");
	if ((env != NULL) && (env[0] != '\0'))
	{
		if (strcmp(env, "thread") == 0)
		{
			return JUICE_CONCURRENCY_MODE_THREAD;
		}
		if (strcmp(env, "poll") == 0)
		{
			return JUICE_CONCURRENCY_MODE_POLL;
		}
	}
	/* Per-agent "juice agent" threads + HTTPS trickle on worker tick collide with Android fdsan (fatal). */
	return JUICE_CONCURRENCY_MODE_POLL;
#else
	return JUICE_CONCURRENCY_MODE_THREAD;
#endif
}

static const char *mmIceConcurrencyModeName(juice_concurrency_mode_t mode)
{
	if (mode == JUICE_CONCURRENCY_MODE_POLL)
	{
		return "poll";
	}
	if (mode == JUICE_CONCURRENCY_MODE_MUX)
	{
		return "mux";
	}
	return "thread";
}

sb32 mmIceInit(const char *bind_hostport, const MmIceServerConfig *cfg)
{
	juice_config_t config;
	juice_agent_t *agent;
	sb32 lan_direct;
	const char *bind_addr_ptr;

	mmIceShutdown();
	if (mmIceResolveBindPorts(bind_hostport, &sBindPortBegin, &sBindPortEnd) == FALSE)
	{
		sBindPortBegin = 0U;
		sBindPortEnd = 0U;
	}
	mmIceResolveBindAddress(bind_hostport, sBindAddressStorage, sizeof(sBindAddressStorage));
	bind_addr_ptr = (sBindAddressStorage[0] != '\0') ? sBindAddressStorage : NULL;

	if (cfg != NULL && cfg->stun_host != NULL && cfg->stun_host[0] != '\0')
	{
		snprintf(sStunHost, sizeof(sStunHost), "%s", cfg->stun_host);
		sStunPort = (cfg->stun_port != 0U) ? cfg->stun_port : MM_ICE_DEFAULT_STUN_PORT;
	}
	else
	{
		mmIceLoadStunFromEnv();
	}
	mmIceLoadTurnFromEnv();
	if (cfg != NULL && cfg->turn_host != NULL && cfg->turn_host[0] != '\0' && cfg->turn_user != NULL &&
	    cfg->turn_user[0] != '\0' && cfg->turn_pass != NULL && cfg->turn_pass[0] != '\0')
	{
		sTurnServers[0].host = cfg->turn_host;
		sTurnServers[0].port = (cfg->turn_port != 0U) ? cfg->turn_port : MM_ICE_DEFAULT_TURN_PORT;
		sTurnServers[0].username = cfg->turn_user;
		sTurnServers[0].password = cfg->turn_pass;
		sTurnServerCount = 1;
	}

	lan_direct = FALSE;
	if ((cfg != NULL && cfg->lan_direct_gather != FALSE) ||
	    (sLanDirectGather != FALSE && mmIceEnvLanDirectEnabled() != FALSE))
	{
		lan_direct = TRUE;
	}
	sActiveGatherLanDirect = lan_direct;
	if (lan_direct != FALSE)
	{
		sTurnServerCount = 0;
#ifdef PORT
		port_log("SSB64 ICE: LAN-direct gather (no STUN/TURN)\n");
#endif
	}

	memset(&config, 0, sizeof(config));
	config.concurrency_mode = mmIceSelectConcurrencyMode();
	if (lan_direct != FALSE)
	{
		config.stun_server_host = NULL;
		config.stun_server_port = 0U;
	}
	else
	{
		config.stun_server_host = (sStunHost[0] != '\0') ? sStunHost : NULL;
		config.stun_server_port = sStunPort;
	}
	config.turn_servers = sTurnServers;
	config.turn_servers_count = sTurnServerCount;
	config.bind_address = bind_addr_ptr;
	config.local_port_range_begin = sBindPortBegin;
	config.local_port_range_end = sBindPortEnd;
	config.cb_state_changed = mmIceOnStateChanged;
	config.cb_candidate = mmIceOnCandidate;
	config.cb_gathering_done = mmIceOnGatheringDone;
	config.cb_recv = mmIceOnRecv;
	config.user_ptr = NULL;

#ifdef PORT
	port_log("SSB64 ICE: libjuice concurrency=%s\n", mmIceConcurrencyModeName(config.concurrency_mode));
#endif
	agent = juice_create(&config);
	if (agent == NULL)
	{
		return FALSE;
	}
	{
		char *tcp_env;

		tcp_env = getenv("SSB64_MATCHMAKING_ICE_TCP");
		if ((tcp_env != NULL) && (tcp_env[0] != '\0') && (atoi(tcp_env) != 0))
		{
			if (juice_set_ice_tcp_mode(agent, JUICE_ICE_TCP_MODE_ACTIVE) != 0)
			{
				port_log("SSB64 ICE: juice_set_ice_tcp_mode(ACTIVE) failed\n");
			}
			else
			{
				port_log("SSB64 ICE: ICE-TCP active mode enabled (experimental, not TURNS)\n");
			}
		}
	}
	(void)pthread_mutex_lock(&sIceMutex);
	sAgent = agent;
	sIceState = MM_ICE_STATE_GATHERING;
	sGatheringDonePosted = FALSE;
	sRecvHead = sRecvTail = sRecvCount = 0U;
	sCandHead = sCandTail = sCandCount = 0U;
	sLastSrflxHostport[0] = '\0';
	sLastRelayHostport[0] = '\0';
	sAllowPeerHostCandidates = TRUE;
	sSignalLocalHostCandidates = TRUE;
	sPeerLanFilterHostport[0] = '\0';
	sLocalLanFilterHostport[0] = '\0';
	sFilteredRemoteHostCount = 0U;
	sFilteredRemoteRelayCount = 0U;
	sPendingStateLog = FALSE;
	(void)pthread_mutex_unlock(&sIceMutex);
#ifdef PORT
	if (bind_addr_ptr != NULL)
	{
		if ((sBindPortBegin == 0U) && (sBindPortEnd == 0U))
		{
			port_log("SSB64 ICE: bind_address=%s port=ephemeral (OS-assigned)\n", bind_addr_ptr);
		}
		else if (sBindPortBegin != sBindPortEnd)
		{
			port_log("SSB64 ICE: bind_address=%s port_range=%u-%u\n", bind_addr_ptr, (unsigned int)sBindPortBegin,
			         (unsigned int)sBindPortEnd);
		}
		else
		{
			port_log("SSB64 ICE: bind_address=%s port=%u\n", bind_addr_ptr, (unsigned int)sBindPortBegin);
		}
	}
	else if ((sBindPortBegin == 0U) && (sBindPortEnd == 0U))
	{
		port_log("SSB64 ICE: bind_address=any port=ephemeral (OS-assigned)\n");
	}
	else if (sBindPortBegin != sBindPortEnd)
	{
		port_log("SSB64 ICE: bind_address=any port_range=%u-%u\n", (unsigned int)sBindPortBegin,
		         (unsigned int)sBindPortEnd);
	}
	else
	{
		port_log("SSB64 ICE: bind_address=any port=%u\n", (unsigned int)sBindPortBegin);
	}
#endif
	return TRUE;
}

sb32 mmIceAgentLive(void)
{
	sb32 live;

	(void)pthread_mutex_lock(&sIceMutex);
	live = (sAgent != NULL) ? TRUE : FALSE;
	(void)pthread_mutex_unlock(&sIceMutex);
	return live;
}

#define MM_ICE_IO_PAUSE_DRAIN_MAX 16U

static void mmIceDrainIoPauseOnAgent(juice_agent_t *agent)
{
	u32 i;

	if (agent == NULL)
	{
		return;
	}
	for (i = 0U; i < MM_ICE_IO_PAUSE_DRAIN_MAX; i++)
	{
		(void)juice_resume_io(agent);
	}
}

void mmIcePauseIo(void)
{
	juice_agent_t *agent;

	agent = mmIceAgentSnapshot();
	if (agent != NULL)
	{
		(void)juice_pause_io(agent);
	}
}

void mmIceResumeIo(void)
{
	juice_agent_t *agent;

	agent = mmIceAgentSnapshot();
	if (agent != NULL)
	{
		(void)juice_resume_io(agent);
	}
}

void mmIceEnsureIoResumed(void)
{
	mmIceDrainIoPauseOnAgent(mmIceAgentSnapshot());
}

sb32 mmIceShouldSerializeMatchmakingHttps(void)
{
	MmIceState st;

	if (mmIceAgentLive() == FALSE)
	{
		return FALSE;
	}
	(void)pthread_mutex_lock(&sIceMutex);
	st = sIceState;
	(void)pthread_mutex_unlock(&sIceMutex);
	if (st == MM_ICE_STATE_COMPLETED)
	{
		return FALSE;
	}
	return TRUE;
}

void mmIceShutdown(void)
{
	juice_agent_t *agent;

	mmIceGatherJoinLocked();
	(void)pthread_mutex_lock(&sIceMutex);
	agent = sAgent;
	sAgent = NULL;
	sIceState = MM_ICE_STATE_IDLE;
	sGatheringDonePosted = FALSE;
	sAllowPeerHostCandidates = TRUE;
	sSignalLocalHostCandidates = TRUE;
	sPeerLanFilterHostport[0] = '\0';
	sLocalLanFilterHostport[0] = '\0';
	sFilteredRemoteHostCount = 0U;
	sFilteredRemoteRelayCount = 0U;
	sPendingStateLog = FALSE;
	sBindAddressStorage[0] = '\0';
	sActiveGatherLanDirect = FALSE;
	(void)pthread_mutex_unlock(&sIceMutex);
	sGatherThreadResult = 0;
	if (agent != NULL)
	{
		mmIceDrainIoPauseOnAgent(agent);
		juice_destroy(agent);
	}
	sIceLastSendJuiceErr = JUICE_ERR_INVALID;
}

sb32 mmIceIsLanDirectGather(void)
{
	return (sActiveGatherLanDirect != FALSE) ? TRUE : FALSE;
}

void mmIceSetCallbacks(MmIceOnLocalCandidateFn on_candidate, MmIceOnGatheringDoneFn on_gathering_done, void *user_ptr)
{
	sOnCandidate = on_candidate;
	sOnGatheringDone = on_gathering_done;
	sCallbackUser = user_ptr;
}

sb32 mmIceStartGathering(void)
{
	juice_agent_t *agent;
	int thread_ret;

	mmIceGatherJoinLocked();
	agent = mmIceAgentSnapshot();
	if (agent == NULL)
	{
		return FALSE;
	}
	sGatherThreadResult = 0;
	sGatherThreadActive = TRUE;
	thread_ret = pthread_create(&sGatherThread, NULL, mmIceGatherThreadEntry, agent);
	if (thread_ret != 0)
	{
		sGatherThreadActive = FALSE;
		sGatherThreadResult = -1;
#ifdef PORT
		port_log("SSB64 ICE: gather thread create failed ret=%d\n", thread_ret);
#endif
		return FALSE;
	}
	return TRUE;
}

sb32 mmIceGatherInProgress(void)
{
	return (sGatherThreadActive != FALSE) ? TRUE : FALSE;
}

sb32 mmIceGatherFailed(void)
{
	if (sGatherThreadActive != FALSE)
	{
		return FALSE;
	}
	return (sGatherThreadResult < 0) ? TRUE : FALSE;
}

sb32 mmIceGetLocalDescription(char *out, u32 out_cap)
{
	juice_agent_t *agent;

	if ((out == NULL) || (out_cap < 16U))
	{
		return FALSE;
	}
	agent = mmIceAgentSnapshot();
	if (agent == NULL)
	{
		return FALSE;
	}
	if (juice_get_local_description(agent, out, (size_t)out_cap) != 0)
	{
		return FALSE;
	}
	mmIceStripEndOfCandidates(out);
	return TRUE;
}

sb32 mmIceSdpHasIceUfrag(const char *sdp)
{
	return ((sdp != NULL) && (strstr(sdp, "a=ice-ufrag:") != NULL)) ? TRUE : FALSE;
}

static sb32 mmIceCopySdpAttributeValue(const char *line, const char *prefix, char *out, u32 out_cap)
{
	size_t prefix_len;
	size_t val_len;
	const char *val;

	if ((line == NULL) || (prefix == NULL) || (out == NULL) || (out_cap == 0U))
	{
		return FALSE;
	}
	prefix_len = strlen(prefix);
	if (strncmp(line, prefix, prefix_len) != 0)
	{
		return FALSE;
	}
	val = line + prefix_len;
	val_len = strlen(val);
	while (val_len > 0U && (val[val_len - 1U] == '\r' || val[val_len - 1U] == '\n'))
	{
		val_len--;
	}
	if (val_len == 0U || val_len >= (size_t)out_cap)
	{
		return FALSE;
	}
	memcpy(out, val, val_len);
	out[val_len] = '\0';
	return TRUE;
}

sb32 mmIceParseSdpIceCredentials(const char *sdp, char *ufrag_out, u32 ufrag_cap, char *pwd_out, u32 pwd_cap)
{
	const char *scan;
	sb32 have_ufrag;
	sb32 have_pwd;

	if ((sdp == NULL) || (ufrag_out == NULL) || (pwd_out == NULL))
	{
		return FALSE;
	}
	ufrag_out[0] = '\0';
	pwd_out[0] = '\0';
	have_ufrag = FALSE;
	have_pwd = FALSE;
	for (scan = sdp; *scan != '\0';)
	{
		const char *line_end = scan;
		char line[128];

		while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r')
		{
			line_end++;
		}
		if ((size_t)(line_end - scan) >= sizeof(line))
		{
			scan = line_end;
			while (*scan == '\n' || *scan == '\r')
			{
				scan++;
			}
			continue;
		}
		snprintf(line, sizeof(line), "%.*s", (int)(line_end - scan), scan);
		if (have_ufrag == FALSE && mmIceCopySdpAttributeValue(line, "a=ice-ufrag:", ufrag_out, ufrag_cap) != FALSE)
		{
			have_ufrag = TRUE;
		}
		else if (have_pwd == FALSE && mmIceCopySdpAttributeValue(line, "a=ice-pwd:", pwd_out, pwd_cap) != FALSE)
		{
			have_pwd = TRUE;
		}
		if (have_ufrag != FALSE && have_pwd != FALSE)
		{
			return TRUE;
		}
		if (*line_end == '\0')
		{
			break;
		}
		scan = line_end + 1;
		while (*scan == '\n' || *scan == '\r')
		{
			scan++;
		}
	}
	return FALSE;
}

sb32 mmIceSetLocalIceAttributesFromSdp(const char *sdp)
{
	char ufrag[32];
	char pwd[64];
	juice_agent_t *agent;
	int ret;

	if (mmIceParseSdpIceCredentials(sdp, ufrag, (u32)sizeof(ufrag), pwd, (u32)sizeof(pwd)) == FALSE)
	{
		return FALSE;
	}
	agent = mmIceAgentSnapshot();
	if (agent == NULL)
	{
		return FALSE;
	}
	ret = juice_set_local_ice_attributes(agent, ufrag, pwd);
#ifdef PORT
	if (ret != 0)
	{
		port_log("SSB64 ICE: juice_set_local_ice_attributes failed ret=%d\n", ret);
	}
#endif
	return (ret == 0) ? TRUE : FALSE;
}

void mmIceJoinGathering(void)
{
	mmIceGatherJoinLocked();
	(void)mmIcePoll();
}

static sb32 mmIceSdpIsCandidateOnly(const char *sdp)
{
	if ((sdp == NULL) || (sdp[0] == '\0'))
	{
		return FALSE;
	}
	if (mmIceSdpHasIceUfrag(sdp) != FALSE)
	{
		return FALSE;
	}
	return (strstr(sdp, "candidate:") != NULL) ? TRUE : FALSE;
}

sb32 mmIceApplyRemoteDescription(const char *sdp)
{
	char buf[JUICE_MAX_SDP_STRING_LEN];
	char session[512];
	char merged[JUICE_MAX_SDP_STRING_LEN];
	int juice_ret;
	sb32 peer_host_missing;

	juice_agent_t *agent;

	agent = mmIceAgentSnapshot();
	if ((agent == NULL) || (sdp == NULL) || (sdp[0] == '\0'))
	{
		return FALSE;
	}
	session[0] = '\0';
	mmIceExtractIceSessionLines(sdp, session, sizeof(session));
	snprintf(buf, sizeof(buf), "%s", sdp);
	mmIceStripEndOfCandidates(buf);
	mmIceStripHostCandidatesFromSdp(buf);
	mmIceStripRelayCandidatesFromSdp(buf);
	if ((sAllowPeerHostCandidates != FALSE) && (sPeerLanFilterHostport[0] != '\0'))
	{
		mmIceStripMisalignedHostLinesFromSdp(buf, sPeerLanFilterHostport, TRUE, TRUE);
	}
	peer_host_missing = FALSE;
	if ((sAllowPeerHostCandidates != FALSE) && (sPeerLanFilterHostport[0] != '\0'))
	{
		peer_host_missing = (mmIceSdpHasAllowedRemotePeerHost(buf, sPeerLanFilterHostport) == FALSE) ? TRUE : FALSE;
	}
#ifdef PORT
	if (sFilteredRemoteHostCount > 0U)
	{
		if (sPeerLanFilterHostport[0] != '\0')
		{
			port_log("SSB64 ICE: filtered %u remote host candidate(s) not on peer_lan=%s\n",
			         (unsigned int)sFilteredRemoteHostCount, sPeerLanFilterHostport);
		}
		else
		{
			port_log("SSB64 ICE: filtered %u remote host candidate(s) from peer SDP (no shared LAN)\n",
			         (unsigned int)sFilteredRemoteHostCount);
		}
	}
	if (sFilteredRemoteRelayCount > 0U)
	{
		port_log("SSB64 ICE: filtered %u remote relay candidate(s) from peer SDP (shared LAN)\n",
		         (unsigned int)sFilteredRemoteRelayCount);
	}
#endif
	if (mmIceSdpHasIceUfrag(buf) == FALSE && session[0] != '\0')
	{
		snprintf(merged, sizeof(merged), "%s%s", session, buf);
		snprintf(buf, sizeof(buf), "%s", merged);
	}
	if (mmIceSdpHasIceUfrag(buf) == FALSE)
	{
#ifdef PORT
		port_log("SSB64 ICE: juice_set_remote_description skipped (no a=ice-ufrag after filter, len=%zu)\n",
		         strlen(buf));
#endif
		return FALSE;
	}
	juice_ret = juice_set_remote_description(agent, buf);
	if (juice_ret != 0)
	{
#ifdef PORT
		port_log("SSB64 ICE: juice_set_remote_description failed ret=%d len=%zu\n", juice_ret, strlen(buf));
#endif
		return FALSE;
	}
	mmIceEnsurePeerLanRemoteCandidate(agent, peer_host_missing);
	return TRUE;
}

sb32 mmIceApplyPeerIceSignaling(const char *sdp, sb32 *out_desc_applied)
{
	if (out_desc_applied != NULL)
	{
		*out_desc_applied = FALSE;
	}
	if ((mmIceAgentSnapshot() == NULL) || (sdp == NULL) || (sdp[0] == '\0'))
	{
		return FALSE;
	}
	if (mmIceSdpHasIceUfrag(sdp) != FALSE)
	{
		sb32 ok;

		ok = mmIceApplyRemoteDescription(sdp);
		if ((ok != FALSE) && (out_desc_applied != NULL))
		{
			*out_desc_applied = TRUE;
		}
		return ok;
	}
	if (mmIceSdpIsCandidateOnly(sdp) != FALSE)
	{
#ifdef PORT
		port_log("SSB64 ICE: peer_ice_sdp is candidate-only (missing ice-ufrag); adding as remote candidate\n");
#endif
		return mmIceAddRemoteCandidate(sdp);
	}
#ifdef PORT
	port_log("SSB64 ICE: peer_ice_sdp rejected (no ice-ufrag or candidate line, len=%zu)\n", strlen(sdp));
#endif
	return FALSE;
}

sb32 mmIceSetIceControlling(sb32 controlling)
{
	juice_agent_t *agent;

	agent = mmIceAgentSnapshot();
	if (agent == NULL)
	{
		return FALSE;
	}
	return (juice_set_ice_controlling(agent, (controlling != FALSE) ? 1 : 0) == 0) ? TRUE : FALSE;
}

sb32 mmIceAddRemoteCandidate(const char *candidate_sdp)
{
	juice_agent_t *agent;

	if ((candidate_sdp == NULL) || (candidate_sdp[0] == '\0'))
	{
		return FALSE;
	}
	if (mmIceShouldAcceptRemoteCandidate(candidate_sdp) == FALSE)
	{
		return TRUE;
	}
	agent = mmIceAgentSnapshot();
	if (agent == NULL)
	{
		return FALSE;
	}
	return (juice_add_remote_candidate(agent, candidate_sdp) == 0) ? TRUE : FALSE;
}

sb32 mmIceSetRemoteGatheringDone(void)
{
	juice_agent_t *agent;

	agent = mmIceAgentSnapshot();
	if (agent == NULL)
	{
		return FALSE;
	}
	return (juice_set_remote_gathering_done(agent) == 0) ? TRUE : FALSE;
}

MmIceState mmIcePoll(void)
{
	MmIceState st;
	u32 i;
	u32 cand_count;
	sb32 gathering_done;
	MmIceOnLocalCandidateFn on_candidate;
	MmIceOnGatheringDoneFn on_gathering_done;
	void *callback_user;
	juice_agent_t *agent;

	agent = mmIceAgentSnapshot();
	if (agent != NULL)
	{
		st = mmIceMapJuiceState(juice_get_state(agent));
	}
	else
	{
		st = MM_ICE_STATE_IDLE;
	}
	/*
	 * Nested poll (candidate callback → PeerUpdate → mmIcePoll): refresh juice
	 * state only. Nested drain would clobber sCandDrainCopy mid-callback.
	 */
	if (sCandDrainActive != FALSE)
	{
		(void)pthread_mutex_lock(&sIceMutex);
		sIceState = st;
		(void)pthread_mutex_unlock(&sIceMutex);
		return st;
	}
	cand_count = 0U;
	gathering_done = FALSE;
	on_candidate = NULL;
	on_gathering_done = NULL;
	callback_user = NULL;
	sCandDrainActive = TRUE;
	(void)pthread_mutex_lock(&sIceMutex);
	sIceState = st;
	cand_count = sCandCount;
	if (cand_count > MM_ICE_CANDIDATE_QUEUE)
	{
		cand_count = MM_ICE_CANDIDATE_QUEUE;
	}
	for (i = 0U; i < cand_count; i++)
	{
		u32 idx = (sCandHead + i) % MM_ICE_CANDIDATE_QUEUE;

		sCandDrainCopy[i] = sCandQ[idx];
	}
	sCandHead = sCandTail;
	sCandCount = 0U;
	if ((sGatheringDonePosted != FALSE) && (sOnGatheringDone != NULL))
	{
		sGatheringDonePosted = FALSE;
		gathering_done = TRUE;
		on_gathering_done = sOnGatheringDone;
	}
	on_candidate = sOnCandidate;
	callback_user = sCallbackUser;
	(void)pthread_mutex_unlock(&sIceMutex);
	for (i = 0U; i < cand_count; i++)
	{
		if (on_candidate != NULL)
		{
			on_candidate(sCandDrainCopy[i].sdp, callback_user);
		}
	}
	if ((gathering_done != FALSE) && (on_gathering_done != NULL))
	{
		on_gathering_done(callback_user);
	}
	sCandDrainActive = FALSE;
	mmIceFlushPendingStateLog();
	return st;
}

MmIceState mmIceGetState(void)
{
	return sIceState;
}

const char *mmIceStateName(MmIceState st)
{
	switch (st)
	{
	case MM_ICE_STATE_IDLE:
		return "idle";
	case MM_ICE_STATE_GATHERING:
		return "gathering";
	case MM_ICE_STATE_CONNECTING:
		return "connecting";
	case MM_ICE_STATE_CONNECTED:
		return "connected";
	case MM_ICE_STATE_COMPLETED:
		return "completed";
	case MM_ICE_STATE_FAILED:
		return "failed";
	default:
		return "unknown";
	}
}

sb32 mmIceIsConnected(void)
{
	MmIceState st;

	st = sIceState;
	return (st == MM_ICE_STATE_CONNECTED || st == MM_ICE_STATE_COMPLETED) ? TRUE : FALSE;
}

sb32 mmIceIsCompleted(void)
{
	return (sIceState == MM_ICE_STATE_COMPLETED) ? TRUE : FALSE;
}

const char *mmIceSendErrorString(int juice_err)
{
	switch (juice_err)
	{
	case JUICE_ERR_SUCCESS:
		return "success";
	case JUICE_ERR_INVALID:
		return "invalid";
	case JUICE_ERR_FAILED:
		return "failed";
	case JUICE_ERR_NOT_AVAIL:
		return "not_avail";
	case JUICE_ERR_IGNORED:
		return "ignored";
	case JUICE_ERR_AGAIN:
		return "again";
	case JUICE_ERR_TOO_LARGE:
		return "too_large";
	default:
		return "unknown";
	}
}

int mmIceLastSendJuiceError(void)
{
	return sIceLastSendJuiceErr;
}

int mmIceSend(const u8 *buf, u32 len)
{
	int juice_ret;
	juice_agent_t *agent;

	if ((buf == NULL) || (len == 0U))
	{
		sIceLastSendJuiceErr = JUICE_ERR_INVALID;
		return -1;
	}
	agent = mmIceAgentSnapshot();
	if (agent == NULL)
	{
		sIceLastSendJuiceErr = JUICE_ERR_INVALID;
		return -1;
	}
	juice_ret = juice_send(agent, (const char *)buf, (size_t)len);
	sIceLastSendJuiceErr = juice_ret;
	if (juice_ret == JUICE_ERR_SUCCESS)
	{
		return (int)len;
	}
	if (juice_ret == JUICE_ERR_AGAIN)
	{
		return 0;
	}
	return -1;
}

sb32 mmIcePopReceived(u8 *out, u32 out_cap, u32 *out_len)
{
	if ((out == NULL) || (out_len == NULL))
	{
		return FALSE;
	}
	*out_len = 0U;
	(void)pthread_mutex_lock(&sIceMutex);
	/*
	 * Never leave an oversized head parked: returning FALSE without dequeue jammed the
	 * whole ICE recv ring (INPUT/FC blackhole). Discard + continue so later datagrams drain.
	 * See docs/bugs/netplay_fc_recv_max_ice_queue_jam_2026-07-18.md.
	 */
	while (sRecvCount > 0U)
	{
		u32 head_len = sRecvQ[sRecvHead].len;

		if (head_len > out_cap)
		{
			port_log("SSB64 ICE: discard oversized recv head len=%u out_cap=%u q=%u\n", head_len, out_cap,
			         sRecvCount);
			sRecvHead = (sRecvHead + 1U) % MM_ICE_RECV_QUEUE;
			sRecvCount--;
			continue;
		}
		memcpy(out, sRecvQ[sRecvHead].data, head_len);
		*out_len = head_len;
		sRecvHead = (sRecvHead + 1U) % MM_ICE_RECV_QUEUE;
		sRecvCount--;
		(void)pthread_mutex_unlock(&sIceMutex);
		return TRUE;
	}
	(void)pthread_mutex_unlock(&sIceMutex);
	return FALSE;
}

sb32 mmIceGetSelectedPath(char *local, u32 local_cap, char *remote, u32 remote_cap)
{
	juice_agent_t *agent;

	agent = mmIceAgentSnapshot();
	if (agent == NULL)
	{
		return FALSE;
	}
	return (juice_get_selected_addresses(agent, local, (size_t)local_cap, remote, (size_t)remote_cap) == 0) ? TRUE
	                                                                                                        : FALSE;
}

sb32 mmIceGetSrflxHostport(char *out, u32 out_cap)
{
	if ((out == NULL) || (out_cap < 8U))
	{
		return FALSE;
	}
	(void)pthread_mutex_lock(&sIceMutex);
	if (sLastSrflxHostport[0] == '\0')
	{
		out[0] = '\0';
		(void)pthread_mutex_unlock(&sIceMutex);
		return FALSE;
	}
	snprintf(out, out_cap, "%s", sLastSrflxHostport);
	(void)pthread_mutex_unlock(&sIceMutex);
	return TRUE;
}

sb32 mmIceGetRelayHostport(char *out, u32 out_cap)
{
	if ((out == NULL) || (out_cap < 8U))
	{
		return FALSE;
	}
	out[0] = '\0';
	(void)pthread_mutex_lock(&sIceMutex);
	if (sLastRelayHostport[0] == '\0')
	{
		(void)pthread_mutex_unlock(&sIceMutex);
		return FALSE;
	}
	snprintf(out, out_cap, "%s", sLastRelayHostport);
	(void)pthread_mutex_unlock(&sIceMutex);
	return TRUE;
}

static void mmIcePreferBindIpv4(char *out, u32 out_cap)
{
	const char *lan_env;
	const char *bind_env;
	const char *colon;

	out[0] = '\0';
	lan_env = getenv("SSB64_MATCHMAKING_LAN_ENDPOINT");
	if (lan_env != NULL && lan_env[0] != '\0')
	{
		colon = strrchr(lan_env, ':');
		if (colon != NULL && colon > lan_env)
		{
			snprintf(out, out_cap, "%.*s", (int)(colon - lan_env), lan_env);
		}
		else
		{
			snprintf(out, out_cap, "%s", lan_env);
		}
		return;
	}
	bind_env = getenv("SSB64_MATCHMAKING_BIND");
	if (bind_env != NULL && bind_env[0] != '\0')
	{
		char bind_host[64];

		if (mmIceParseBindHostFromSpec(bind_env, bind_host, sizeof(bind_host)) != FALSE && bind_host[0] != '\0' &&
		    (strncmp(bind_host, "0.0.0.0", 7) != 0) && (strcmp(bind_host, "*") != 0))
		{
			snprintf(out, out_cap, "%s", bind_host);
		}
	}
}

static u32 mmIceCollectLocalHostHostports(const char *hosts[16], char host_addrs[16][128])
{
	char sdp[4096];
	const char *scan;
	u32 host_count;

	host_count = 0U;
	if (mmIceGetLocalDescription(sdp, sizeof(sdp)) == FALSE)
	{
		return 0U;
	}
	for (scan = sdp; *scan != '\0' && host_count < 16U;)
	{
		char typ[16];
		char addr[128];
		const char *line_end = scan;

		while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r')
		{
			line_end++;
		}
		if (strstr(scan, "candidate:") != NULL &&
		    mmIceParseCandidateFields(scan, typ, sizeof(typ), addr, sizeof(addr)) != FALSE && strcmp(typ, "host") == 0 &&
		    addr[0] != '\0')
		{
			snprintf(host_addrs[host_count], sizeof(host_addrs[host_count]), "%s", addr);
			hosts[host_count] = host_addrs[host_count];
			host_count++;
		}
		if (*line_end == '\0')
		{
			break;
		}
		scan = line_end + 1;
		while (*scan == '\n' || *scan == '\r')
		{
			scan++;
		}
	}
	return host_count;
}

static sb32 mmIceHostportHasValidPort(const char *hostport)
{
	const char *colon;
	u16 port;

	if ((hostport == NULL) || (hostport[0] == '\0'))
	{
		return FALSE;
	}
	colon = strrchr(hostport, ':');
	if ((colon == NULL) || (colon == hostport) || (colon[1] == '\0'))
	{
		return FALSE;
	}
	if (sscanf(colon + 1, "%hu", &port) != 1)
	{
		return FALSE;
	}
	return (port > 0U) ? TRUE : FALSE;
}

sb32 mmIceGetLocalHostHostport(char *out, u32 out_cap)
{
	const char *hosts[16];
	char host_addrs[16][128];
	u32 host_count;
	char prefer_ip[64];

	if ((out == NULL) || (out_cap < 8U))
	{
		return FALSE;
	}
	out[0] = '\0';
	host_count = mmIceCollectLocalHostHostports(hosts, host_addrs);
	if (host_count == 0U)
	{
		return FALSE;
	}
	mmIcePreferBindIpv4(prefer_ip, sizeof(prefer_ip));
	if (mmLanPickBestHostportFromCandidates(hosts, host_count, out, out_cap, prefer_ip[0] != '\0' ? prefer_ip : NULL) !=
	    FALSE)
	{
		return TRUE;
	}
	snprintf(out, out_cap, "%s", hosts[0]);
	return (out[0] != '\0') ? TRUE : FALSE;
}

sb32 mmIceGetLocalHostHostportForPeer(const char *peer_hostport, char *out, u32 out_cap)
{
	const char *hosts[16];
	char host_addrs[16][128];
	u32 host_count;
	char prefer_ip[64];

	if ((out == NULL) || (out_cap < 8U))
	{
		return FALSE;
	}
	out[0] = '\0';
	host_count = mmIceCollectLocalHostHostports(hosts, host_addrs);
	if (host_count == 0U)
	{
		return FALSE;
	}
	mmIcePreferBindIpv4(prefer_ip, sizeof(prefer_ip));
	if (mmLanPickHostportForPeer(peer_hostport, hosts, host_count, out, out_cap,
	                             prefer_ip[0] != '\0' ? prefer_ip : NULL) != FALSE)
	{
		return TRUE;
	}
	if (mmLanPickBestHostportFromCandidates(hosts, host_count, out, out_cap, prefer_ip[0] != '\0' ? prefer_ip : NULL) !=
	    FALSE)
	{
		return TRUE;
	}
	snprintf(out, out_cap, "%s", hosts[0]);
	return (out[0] != '\0') ? TRUE : FALSE;
}

sb32 mmIceGetBootstrapBindHostport(const char *peer_hostport, char *out, u32 out_cap)
{
	char local[JUICE_MAX_ADDRESS_STRING_LEN];
	char remote[JUICE_MAX_ADDRESS_STRING_LEN];
	char ip[64];
	const char *colon;

	if ((out == NULL) || (out_cap < 8U))
	{
		return FALSE;
	}
	out[0] = '\0';
	/*
	 * Bind must be locally assignable. Never fall through to srflx/WAN
	 * (EADDRNOTAVAIL / err=99). ICE transport uses the selected path for
	 * bookkeeping; raw-UDP abort/fallback needs a host/LAN address only.
	 */
	if (mmIceGetSelectedPath(local, sizeof(local), remote, sizeof(remote)) != FALSE && local[0] != '\0' &&
	    mmIceHostportHasValidPort(local) != FALSE)
	{
		colon = strrchr(local, ':');
		if ((colon != NULL) && (colon > local) && ((u32)(colon - local) < sizeof(ip)))
		{
			memcpy(ip, local, (size_t)(colon - local));
			ip[colon - local] = '\0';
			if (mmLanIpv4StringIsRfc1918(ip) != FALSE)
			{
				snprintf(out, out_cap, "%s", local);
				return TRUE;
			}
		}
	}
	if (mmIceGetLocalHostHostportForPeer(peer_hostport, out, out_cap) != FALSE &&
	    mmIceHostportHasValidPort(out) != FALSE)
	{
		return TRUE;
	}
	if (mmIceGetLocalHostHostport(out, out_cap) != FALSE && mmIceHostportHasValidPort(out) != FALSE)
	{
		return TRUE;
	}
	out[0] = '\0';
	return FALSE;
}

sb32 mmIceGetReflexiveHostport(char *out, u32 out_cap)
{
	char local[JUICE_MAX_ADDRESS_STRING_LEN];
	char remote[JUICE_MAX_ADDRESS_STRING_LEN];

	(void)remote;
	if ((out == NULL) || (out_cap < 8U))
	{
		return FALSE;
	}
	if (mmIceGetSrflxHostport(out, out_cap) != FALSE)
	{
		return TRUE;
	}
	if (mmIceGetLocalHostHostport(out, out_cap) != FALSE)
	{
		return TRUE;
	}
	if (mmIceGetSelectedPath(local, sizeof(local), remote, sizeof(remote)) == FALSE)
	{
		out[0] = '\0';
		return FALSE;
	}
	snprintf(out, out_cap, "%s", local);
	return (out[0] != '\0') ? TRUE : FALSE;
}

sb32 mmIceFetchTurnCredentials(char *user_out, u32 user_cap, char *pass_out, u32 pass_cap)
{
	/* Populated by mm_matchmaking GET /v1/turn-credentials when server issues ephemeral creds. */
	(void)user_out;
	(void)user_cap;
	(void)pass_out;
	(void)pass_cap;
	return FALSE;
}

void mmIceLogSelectedCandidates(void)
{
	char local_sdp[JUICE_MAX_CANDIDATE_SDP_STRING_LEN];
	char remote_sdp[JUICE_MAX_CANDIDATE_SDP_STRING_LEN];
	char typ[16];
	char addr[128];
	int juice_ret;

	juice_agent_t *agent;

	memset(local_sdp, 0, sizeof(local_sdp));
	memset(remote_sdp, 0, sizeof(remote_sdp));
	agent = mmIceAgentSnapshot();
	if (agent == NULL)
	{
		return;
	}
	juice_ret = juice_get_selected_candidates(agent, local_sdp, sizeof(local_sdp), remote_sdp, sizeof(remote_sdp));
	if (juice_ret != 0)
	{
		port_log("SSB64 ICE: selected candidates unavailable\n");
		return;
	}
	if (mmIceParseCandidateFields(local_sdp, typ, sizeof(typ), addr, sizeof(addr)) != FALSE)
	{
		port_log("SSB64 ICE: selected local typ=%s addr=%s\n", typ, addr);
	}
	if (mmIceParseCandidateFields(remote_sdp, typ, sizeof(typ), addr, sizeof(addr)) != FALSE)
	{
		port_log("SSB64 ICE: selected remote typ=%s addr=%s\n", typ, addr);
	}
}

static void mmIceCopyIpv4Host(const char *hostport, char *out, u32 out_cap)
{
	const char *colon;

	if ((hostport == NULL) || (out == NULL) || (out_cap == 0U))
	{
		return;
	}
	out[0] = '\0';
	colon = strrchr(hostport, ':');
	if (colon != NULL && colon > hostport)
	{
		snprintf(out, out_cap, "%.*s", (int)(colon - hostport), hostport);
	}
	else
	{
		snprintf(out, out_cap, "%s", hostport);
	}
}

sb32 mmIceGetSelectedRemoteCandidateTyp(char *typ, u32 typ_cap)
{
	char local_sdp[JUICE_MAX_CANDIDATE_SDP_STRING_LEN];
	char remote_sdp[JUICE_MAX_CANDIDATE_SDP_STRING_LEN];
	juice_agent_t *agent;
	int juice_ret;

	if ((typ == NULL) || (typ_cap < 2U))
	{
		return FALSE;
	}
	typ[0] = '\0';
	agent = mmIceAgentSnapshot();
	if (agent == NULL)
	{
		return FALSE;
	}
	juice_ret = juice_get_selected_candidates(agent, local_sdp, sizeof(local_sdp), remote_sdp, sizeof(remote_sdp));
	if (juice_ret != 0)
	{
		return FALSE;
	}
	return mmIceParseCandidateFields(remote_sdp, typ, typ_cap, NULL, 0U);
}

sb32 mmIceValidateSelectedRemotePath(const char *peer_hostport, const char *peer_lan_hostport,
                                     const char *local_lan_hostport)
{
	char remote[128];
	char peer_ip[64];
	char selected_ip[64];
	sb32 shared_lan;
	sb32 remote_on_lan;

	if (mmIceGetSelectedPath(NULL, 0U, remote, sizeof(remote)) == FALSE || remote[0] == '\0')
	{
		port_log("SSB64 ICE: path validation failed (no selected remote)\n");
		return FALSE;
	}
	shared_lan =
	    ((peer_lan_hostport != NULL) && (peer_lan_hostport[0] != '\0') && (sAllowPeerHostCandidates != FALSE)) ? TRUE
	                                                                                                            : FALSE;
	if (shared_lan == FALSE)
	{
		if ((peer_lan_hostport == NULL) || (peer_lan_hostport[0] == '\0'))
		{
			if (mmLanPeerHostportIsOnLocalLan(remote) != FALSE)
			{
				port_log("SSB64 ICE: path validation failed (cross_nat_host remote=%s peer_lan=none)\n", remote);
				return FALSE;
			}
		}
	}
	if (shared_lan != FALSE)
	{
		char local_sdp[JUICE_MAX_CANDIDATE_SDP_STRING_LEN];
		char remote_sdp[JUICE_MAX_CANDIDATE_SDP_STRING_LEN];
		char remote_typ[16];
		int juice_ret;
		juice_agent_t *agent;

		agent = mmIceAgentSnapshot();
		if (agent != NULL)
		{
			juice_ret =
			    juice_get_selected_candidates(agent, local_sdp, sizeof(local_sdp), remote_sdp, sizeof(remote_sdp));
		}
		else
		{
			juice_ret = -1;
		}
		if (juice_ret == 0 && mmIceParseCandidateFields(remote_sdp, remote_typ, sizeof(remote_typ), NULL, 0U) != FALSE &&
		    strcmp(remote_typ, "relay") == 0)
		{
			port_log(
			    "SSB64 ICE: path validation failed (shared LAN but selected typ=relay remote=%s peer_lan=%s)\n",
			    remote, peer_lan_hostport);
			return FALSE;
		}
		if ((local_lan_hostport != NULL) && (local_lan_hostport[0] != '\0'))
		{
			remote_on_lan = mmLanPeerSharesLocalLanSubnet(remote, local_lan_hostport);
		}
		else
		{
			remote_on_lan = mmLanPeerHostportIsOnLocalLan(remote);
		}
		if (remote_on_lan == FALSE)
		{
			port_log("SSB64 ICE: path validation failed (LAN match but remote=%s not reachable on local_lan=%s)\n",
			         remote,
			         (local_lan_hostport != NULL && local_lan_hostport[0] != '\0') ? local_lan_hostport : "(auto)");
			return FALSE;
		}
	}
	if ((peer_hostport != NULL) && (peer_hostport[0] != '\0'))
	{
		mmIceCopyIpv4Host(peer_hostport, peer_ip, sizeof(peer_ip));
		mmIceCopyIpv4Host(remote, selected_ip, sizeof(selected_ip));
		if ((peer_ip[0] != '\0') && (selected_ip[0] != '\0') && (strcmp(peer_ip, selected_ip) != 0))
		{
			if (shared_lan != FALSE)
			{
				port_log("SSB64 ICE: path note selected=%s peer_wan=%s (LAN data path ok; dual-NIC/WAN register)\n",
				         remote, peer_hostport);
			}
			else
			{
				port_log("SSB64 ICE: path warning selected=%s peer_hostport=%s (address mismatch)\n", remote,
				         peer_hostport);
			}
		}
	}
	return TRUE;
}

#endif /* PORT && SSB64_NETMENU && SSB64_NETPLAY_ICE */
