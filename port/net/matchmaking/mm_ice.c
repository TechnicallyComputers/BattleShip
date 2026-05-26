#include "mm_ice.h"

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
#include <windows.h>
#else
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

static char sStunHost[128];
static u16 sStunPort;
static juice_turn_server_t sTurnServers[2];
static int sTurnServerCount;
static u16 sBindPortBegin;
static u16 sBindPortEnd;

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

static void mmIceOnStateChanged(juice_agent_t *agent, juice_state_t state, void *user_ptr)
{
	(void)agent;
	(void)user_ptr;
	(void)pthread_mutex_lock(&sIceMutex);
	sIceState = mmIceMapJuiceState(state);
	(void)pthread_mutex_unlock(&sIceMutex);
#ifdef PORT
	port_log("SSB64 ICE: state=%s\n", juice_state_to_string(state));
#endif
}

static void mmIceOnCandidate(juice_agent_t *agent, const char *sdp, void *user_ptr)
{
	(void)agent;
	(void)user_ptr;
	(void)pthread_mutex_lock(&sIceMutex);
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

static sb32 mmIceParseBindPort(const char *bind_hostport, u16 *out_port)
{
	const char *colon;
	unsigned long p;

	if ((bind_hostport == NULL) || (out_port == NULL))
	{
		return FALSE;
	}
	colon = strrchr(bind_hostport, ':');
	if (colon == NULL)
	{
		return FALSE;
	}
	p = (unsigned long)atoi(colon + 1);
	if ((p == 0UL) || (p > 65535UL))
	{
		return FALSE;
	}
	*out_port = (u16)p;
	return TRUE;
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
		snprintf(sStunHost, sizeof(sStunHost), "%s", "stun.l.google.com");
		sStunPort = 19302U;
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
		host = "coturn.technicallycomputers.ca";
	}
	if ((user == NULL) || (user[0] == '\0'))
	{
		user = "battleship";
	}
	if ((pass == NULL) || (pass[0] == '\0'))
	{
		pass = "battleship";
	}
	p = 3478UL;
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

sb32 mmIceInit(const char *bind_hostport, const MmIceServerConfig *cfg)
{
	juice_config_t config;
	u16 bind_port;

	if (sAgent != NULL)
	{
		mmIceShutdown();
	}
	if (mmIceParseBindPort(bind_hostport, &bind_port) == FALSE)
	{
		bind_port = 7778U;
	}
	sBindPortBegin = bind_port;
	sBindPortEnd = bind_port;

	if (cfg != NULL && cfg->stun_host != NULL && cfg->stun_host[0] != '\0')
	{
		snprintf(sStunHost, sizeof(sStunHost), "%s", cfg->stun_host);
		sStunPort = cfg->stun_port;
	}
	else
	{
		mmIceLoadStunFromEnv();
	}
	mmIceLoadTurnFromEnv();
	if (cfg != NULL && cfg->turn_host != NULL && cfg->turn_host[0] != '\0')
	{
		sTurnServers[0].host = cfg->turn_host;
		sTurnServers[0].port = cfg->turn_port;
		sTurnServers[0].username = cfg->turn_user;
		sTurnServers[0].password = cfg->turn_pass;
		sTurnServerCount = 1;
	}

	memset(&config, 0, sizeof(config));
	config.concurrency_mode = JUICE_CONCURRENCY_MODE_THREAD;
	config.stun_server_host = sStunHost;
	config.stun_server_port = sStunPort;
	config.turn_servers = sTurnServers;
	config.turn_servers_count = sTurnServerCount;
	config.local_port_range_begin = sBindPortBegin;
	config.local_port_range_end = sBindPortEnd;
	config.cb_state_changed = mmIceOnStateChanged;
	config.cb_candidate = mmIceOnCandidate;
	config.cb_gathering_done = mmIceOnGatheringDone;
	config.cb_recv = mmIceOnRecv;
	config.user_ptr = NULL;

	sAgent = juice_create(&config);
	if (sAgent == NULL)
	{
		return FALSE;
	}
	sIceState = MM_ICE_STATE_GATHERING;
	sGatheringDonePosted = FALSE;
	sRecvHead = sRecvTail = sRecvCount = 0U;
	sCandHead = sCandTail = sCandCount = 0U;
	return TRUE;
}

void mmIceShutdown(void)
{
	if (sAgent != NULL)
	{
		juice_destroy(sAgent);
		sAgent = NULL;
	}
	sIceState = MM_ICE_STATE_IDLE;
	sGatheringDonePosted = FALSE;
}

void mmIceSetCallbacks(MmIceOnLocalCandidateFn on_candidate, MmIceOnGatheringDoneFn on_gathering_done, void *user_ptr)
{
	sOnCandidate = on_candidate;
	sOnGatheringDone = on_gathering_done;
	sCallbackUser = user_ptr;
}

sb32 mmIceStartGathering(void)
{
	if (sAgent == NULL)
	{
		return FALSE;
	}
	return (juice_gather_candidates(sAgent) == 0) ? TRUE : FALSE;
}

sb32 mmIceGetLocalDescription(char *out, u32 out_cap)
{
	if ((sAgent == NULL) || (out == NULL) || (out_cap < 16U))
	{
		return FALSE;
	}
	return (juice_get_local_description(sAgent, out, (size_t)out_cap) == 0) ? TRUE : FALSE;
}

sb32 mmIceApplyRemoteDescription(const char *sdp)
{
	if ((sAgent == NULL) || (sdp == NULL) || (sdp[0] == '\0'))
	{
		return FALSE;
	}
	return (juice_set_remote_description(sAgent, sdp) == 0) ? TRUE : FALSE;
}

sb32 mmIceAddRemoteCandidate(const char *candidate_sdp)
{
	if ((sAgent == NULL) || (candidate_sdp == NULL) || (candidate_sdp[0] == '\0'))
	{
		return FALSE;
	}
	return (juice_add_remote_candidate(sAgent, candidate_sdp) == 0) ? TRUE : FALSE;
}

sb32 mmIceSetRemoteGatheringDone(void)
{
	if (sAgent == NULL)
	{
		return FALSE;
	}
	return (juice_set_remote_gathering_done(sAgent) == 0) ? TRUE : FALSE;
}

MmIceState mmIcePoll(void)
{
	MmIceState st;
	u32 i;

	(void)pthread_mutex_lock(&sIceMutex);
	if (sAgent != NULL)
	{
		st = mmIceMapJuiceState(juice_get_state(sAgent));
		sIceState = st;
	}
	else
	{
		st = MM_ICE_STATE_IDLE;
	}
	for (i = 0U; i < sCandCount; i++)
	{
		u32 idx = (sCandHead + i) % MM_ICE_CANDIDATE_QUEUE;
		if (sOnCandidate != NULL)
		{
			sOnCandidate(sCandQ[idx].sdp, sCallbackUser);
		}
	}
	sCandHead = sCandTail;
	sCandCount = 0U;
	if ((sGatheringDonePosted != FALSE) && (sOnGatheringDone != NULL))
	{
		sGatheringDonePosted = FALSE;
		sOnGatheringDone(sCallbackUser);
	}
	(void)pthread_mutex_unlock(&sIceMutex);
	return st;
}

sb32 mmIceIsConnected(void)
{
	MmIceState st;

	st = mmIcePoll();
	return (st == MM_ICE_STATE_CONNECTED || st == MM_ICE_STATE_COMPLETED) ? TRUE : FALSE;
}

sb32 mmIceIsCompleted(void)
{
	return (mmIcePoll() == MM_ICE_STATE_COMPLETED) ? TRUE : FALSE;
}

int mmIceSend(const u8 *buf, u32 len)
{
	if ((sAgent == NULL) || (buf == NULL) || (len == 0U))
	{
		return -1;
	}
	return juice_send(sAgent, (const char *)buf, (size_t)len);
}

sb32 mmIcePopReceived(u8 *out, u32 out_cap, u32 *out_len)
{
	if ((out == NULL) || (out_len == NULL))
	{
		return FALSE;
	}
	*out_len = 0U;
	(void)pthread_mutex_lock(&sIceMutex);
	if (sRecvCount == 0U)
	{
		(void)pthread_mutex_unlock(&sIceMutex);
		return FALSE;
	}
	if (sRecvQ[sRecvHead].len > out_cap)
	{
		(void)pthread_mutex_unlock(&sIceMutex);
		return FALSE;
	}
	memcpy(out, sRecvQ[sRecvHead].data, sRecvQ[sRecvHead].len);
	*out_len = sRecvQ[sRecvHead].len;
	sRecvHead = (sRecvHead + 1U) % MM_ICE_RECV_QUEUE;
	sRecvCount--;
	(void)pthread_mutex_unlock(&sIceMutex);
	return TRUE;
}

sb32 mmIceGetSelectedPath(char *local, u32 local_cap, char *remote, u32 remote_cap)
{
	if (sAgent == NULL)
	{
		return FALSE;
	}
	return (juice_get_selected_addresses(sAgent, local, (size_t)local_cap, remote, (size_t)remote_cap) == 0) ? TRUE
	                                                                                                        : FALSE;
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

#endif /* PORT && SSB64_NETMENU && SSB64_NETPLAY_ICE */
