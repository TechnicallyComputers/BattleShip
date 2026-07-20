#include "mm_ice_automatch.h"

#if defined(PORT) && defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE)

#include "mm_ice.h"
#include "mm_lan_detect.h"
#include <mm_matchmaking.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int atoi(const char *str);
#include <sys/netpeer.h>
#include <sys/netpeer_socket_platform.h>

#include "port_log.h"

#define ICE_PRETICKET_CAND_QUEUE 16
/* Quiet trickle polls before remote gathering done (~60 Hz). Wait for connected + drain. */
#define ICE_TRICKLE_QUIET_TICKS 30U
#define ICE_TRICKLE_QUIET_CONNECTED_TICKS 12U
/* Shared-LAN: wait for host-pair re-nomination if libjuice completes on relay first. */
#define ICE_LAN_RELAY_SETTLE_TICKS 60U
/* Guest may match before host; allow time for host match + role-ready poll (60 Hz). */
#define ICE_ROLE_READY_WAIT_MAX_TICKS 480U
/*
 * CONNECTING trickle cadence (non-LAN). Was 2 (~30 Hz) and starved Android poll-mode
 * ICE via juice_pause_io on every HTTPS GET — soak connectivity lag + offer timeout.
 */
#define ICE_CONNECT_TRICKLE_INTERVAL_TICKS 30U
#define ICE_CONNECT_TRICKLE_MIN_MS_DEFAULT 300U

static char sIceTicket[64];
static char sIceBind[144];
static char sIceLocalLan[144];
static sb32 sIceRemoteDescApplied;
/** Peer signaling SDP is applied on the live libjuice agent (skip re-apply on rehydrate). */
static sb32 sIcePeerSdpOnAgent;
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
/** TRUE when this matched session shares a LAN subnet with the peer (trickle HTTPS off). */
static sb32 sIceSharedLanSession;
static u32 sIceCompletedSettleTicks;
static u32 sIceLanRelaySettleTicks;
static char sIceCompletedRemote[128];
static const char *sIceConnectFailReason = "ICE connection failed";

static sb32 sIcePeerSetupFailed;
static sb32 sIceAwaitPeerControllingReady;
static sb32 sIceAwaitPeerRoleReadyLogged;
static u32 sIceRoleReadyWaitTicks;
static char sIceDeferredPeerSdp[4096];
/** Set from BeginConnect until connect tick success/fail or Reset — blocks worker match polls. */
static sb32 sIceConnectPhaseActive;
/** ConnectTick already returned success once — block nested bootstrap during offer-exchange yield. */
static sb32 sIceBootstrapHandoff;
static u64 sIceConnectTrickleLastEnqueueMs;

static char sIceQueueSdp[4096];
static char sIceSavedStunHost[128];
static char sIceSavedTurnHost[128];
static char sIceSavedTurnUser[256];
static char sIceSavedTurnPass[256];
static u16 sIceSavedStunPort;
static u16 sIceSavedTurnPort;
static sb32 sIceSavedLanDirectGather;
static sb32 sIcePollSuspended;
static char sIceAppliedPeerSdp[4096];

#if defined(__ANDROID__)
static pthread_mutex_t sIceNetSerializeMutex = PTHREAD_MUTEX_INITIALIZER;
static sb32 sIceHttpsPauseActive;
static sb32 sIceConnectTickInside;
static sb32 sIceConnectTickMutexHeld;
#endif

#if defined(__ANDROID__)
static u64 sQueuePollLastEnqueueMs;
static u64 sQueuePollCooldownUntilMs;
static u32 sQueuePollStillQueuedStreak;

#define MN_AM_QUEUE_POLL_MIN_MS_DEFAULT 400U
#define MN_AM_QUEUE_POLL_HTTP0_COOLDOWN_MS_DEFAULT 750U
#define MN_AM_QUEUE_POLL_MAX_MS_DEFAULT 4000U

static u32 mnVSNetAutomatchAMQueuePollMinMs(void)
{
	const char *e;
	s32 ms;

	e = getenv("SSB64_MATCHMAKING_ANDROID_QUEUE_POLL_MIN_MS");
	ms = (e != NULL && e[0] != '\0') ? atoi(e) : (s32)MN_AM_QUEUE_POLL_MIN_MS_DEFAULT;
	if (ms < 100)
	{
		ms = 100;
	}
	if (ms > 10000)
	{
		ms = 10000;
	}
	return (u32)ms;
}

static u32 mnVSNetAutomatchAMQueuePollHttp0CooldownMs(void)
{
	const char *e;
	s32 ms;

	e = getenv("SSB64_MATCHMAKING_ANDROID_QUEUE_POLL_HTTP0_COOLDOWN_MS");
	ms = (e != NULL && e[0] != '\0') ? atoi(e) : (s32)MN_AM_QUEUE_POLL_HTTP0_COOLDOWN_MS_DEFAULT;
	if (ms < 100)
	{
		ms = 100;
	}
	if (ms > 30000)
	{
		ms = 30000;
	}
	return (u32)ms;
}

static u32 mnVSNetAutomatchAMQueuePollEffectiveMinMs(void)
{
	u32 min_ms;
	u32 shift;

	min_ms = mnVSNetAutomatchAMQueuePollMinMs();
	if (sQueuePollStillQueuedStreak <= 1U)
	{
		return min_ms;
	}
	shift = sQueuePollStillQueuedStreak - 1U;
	if (shift > 3U)
	{
		shift = 3U;
	}
	min_ms <<= shift;
	if (min_ms > MN_AM_QUEUE_POLL_MAX_MS_DEFAULT)
	{
		min_ms = MN_AM_QUEUE_POLL_MAX_MS_DEFAULT;
	}
	return min_ms;
}

void mnVSNetAutomatchAMQueuePollReset(void)
{
	sQueuePollLastEnqueueMs = 0ULL;
	sQueuePollCooldownUntilMs = 0ULL;
	sQueuePollStillQueuedStreak = 0U;
}

sb32 mnVSNetAutomatchAMQueuePollMayEnqueue(sb32 trickle_only)
{
	u64 now;
	u32 min_ms;

	if (trickle_only != FALSE)
	{
		return TRUE;
	}
	if (sIcePollSuspended == FALSE)
	{
		return TRUE;
	}
	now = syNetPeerOsMonotonicMs();
	if ((sQueuePollCooldownUntilMs != 0ULL) && (now < sQueuePollCooldownUntilMs))
	{
		return FALSE;
	}
	min_ms = mnVSNetAutomatchAMQueuePollEffectiveMinMs();
	if ((sQueuePollLastEnqueueMs != 0ULL) && ((now - sQueuePollLastEnqueueMs) < (u64)min_ms))
	{
		return FALSE;
	}
	return TRUE;
}

void mnVSNetAutomatchAMQueuePollNoteEnqueued(void)
{
	sQueuePollLastEnqueueMs = syNetPeerOsMonotonicMs();
}

void mnVSNetAutomatchAMQueuePollNoteStillQueued(void)
{
	if (sQueuePollStillQueuedStreak < 16U)
	{
		sQueuePollStillQueuedStreak++;
	}
}

void mnVSNetAutomatchAMQueuePollNoteHttp0Cooldown(void)
{
	u64 now;

	now = syNetPeerOsMonotonicMs();
	sQueuePollCooldownUntilMs = now + (u64)mnVSNetAutomatchAMQueuePollHttp0CooldownMs();
	if (sQueuePollStillQueuedStreak < 16U)
	{
		sQueuePollStillQueuedStreak++;
	}
}
#else
void mnVSNetAutomatchAMQueuePollReset(void)
{
}

sb32 mnVSNetAutomatchAMQueuePollMayEnqueue(sb32 trickle_only)
{
	(void)trickle_only;
	return TRUE;
}

void mnVSNetAutomatchAMQueuePollNoteEnqueued(void)
{
}

void mnVSNetAutomatchAMQueuePollNoteStillQueued(void)
{
}

void mnVSNetAutomatchAMQueuePollNoteHttp0Cooldown(void)
{
}
#endif

static sb32 mnVSNetAutomatchAMIceInitGatherAgent(const char *bind_spec);
static void mnVSNetAutomatchAMIceOnLocalCandidate(const char *sdp, void *user_ptr);
static void mnVSNetAutomatchAMIceOnGatheringDone(void *user_ptr);
static void mnVSNetAutomatchAMIceApplyCandidatePolicy(const MmMatchResult *mr);
static sb32 mnVSNetAutomatchAMIceApplyPeerSdp(const char *peer_sdp, sb32 *out_desc_applied);
static void mnVSNetAutomatchAMIceDrainRemoteCandidates(void);

static sb32 mnVSNetAutomatchAMIceHttpsSerializeEnabled(void)
{
#if defined(__ANDROID__)
	const char *e;

	e = getenv("SSB64_MATCHMAKING_ICE_HTTPS_SERIALIZE");
	if ((e != NULL) && (e[0] != '\0'))
	{
		return (atoi(e) != 0) ? TRUE : FALSE;
	}
	/* Pause libjuice poll I/O during matchmaking HTTPS (Android fdsan); not destroy/rehydrate. */
	return TRUE;
#else
	(void)0;
	return FALSE;
#endif
}

static sb32 mnVSNetAutomatchAMIcePollSuspendEnabled(void)
{
#if defined(__ANDROID__)
	const char *e;

	e = getenv("SSB64_MATCHMAKING_ICE_SUSPEND_POLL");
	if ((e != NULL) && (e[0] != '\0'))
	{
		return (atoi(e) != 0) ? TRUE : FALSE;
	}
	/* Avoid libjuice poll thread + curl overlap during MN_AM_POLL (Android fdsan). */
	return TRUE;
#else
	(void)0;
	return FALSE;
#endif
}

/*
 * Phase 1 (2026-06-26): keep the libjuice agent ALIVE across the queue-poll
 * HTTPS window instead of destroying and rebuilding it. The destroy/rehydrate
 * path discarded every gathered candidate — including the ephemeral TURN relay
 * candidate that a CGNAT peer needs — and forced a full re-gather on connect,
 * which never re-signalled the relay. Keeping the agent alive with libjuice I/O
 * globally paused for the duration of the queue wait is just as fdsan-safe (the
 * poll thread is parked, so worker curl can never race the agent UDP fds) while
 * preserving all gathered candidates and the relay allocation.
 *
 * Default ON (Android). Set SSB64_MATCHMAKING_ICE_KEEP_AGENT=0 to A/B test the
 * legacy destroy/rehydrate suspend path.
 */
static sb32 mnVSNetAutomatchAMIceKeepAgentOnSuspendEnabled(void)
{
#if defined(__ANDROID__)
	const char *e;

	if (mnVSNetAutomatchAMIcePollSuspendEnabled() == FALSE)
	{
		return FALSE;
	}
	e = getenv("SSB64_MATCHMAKING_ICE_KEEP_AGENT");
	if ((e != NULL) && (e[0] != '\0'))
	{
		return (atoi(e) != 0) ? TRUE : FALSE;
	}
	return TRUE;
#else
	(void)0;
	return FALSE;
#endif
}

/*
 * Phase 2 (2026-06-26): the controlled peer used to poll for the host's
 * "controlling role_ready" with a BLOCKING sync HTTPS GET on the game thread
 * every 16 ticks while deferring the peer SDP. Each GET stalled sim + audio for
 * a full round-trip, so the queue/connect window ran at ~42-51 fps (audible
 * music lag) for the ~0.8s it took the signal to arrive.
 *
 * When enabled, role_ready is fetched by an ASYNC worker trickle GET and the
 * connect tick only reads the cached flag — the sim keeps running at 60 Hz. The
 * historical connected->completed stall (worker pausing libjuice I/O while the
 * game thread polled a nominated pair) does NOT apply here because this path is
 * strictly PRE-SDP: libjuice has no remote description yet, so there is no
 * candidate pair to starve and nothing to receive. Once the SDP is applied the
 * code falls back to the existing connect-phase behavior unchanged.
 *
 * Default ON. Set SSB64_MATCHMAKING_ICE_ASYNC_ROLE_READY=0 to fall back to the
 * legacy blocking main-thread sync trickle for A/B testing.
 */
static sb32 mnVSNetAutomatchAMIceAsyncRoleReadyEnabled(void)
{
	const char *e;

	e = getenv("SSB64_MATCHMAKING_ICE_ASYNC_ROLE_READY");
	if ((e != NULL) && (e[0] != '\0'))
	{
		return (atoi(e) != 0) ? TRUE : FALSE;
	}
	return TRUE;
}

/*
 * Aggressive early-SDP (opt-in, default OFF as of 2026-06-26).
 *
 * When enabled, the controlled (guest) side applies the host SDP and starts
 * connectivity checks immediately at BeginConnect instead of waiting for the
 * host's "controlling role_ready". That removes the host-rendezvous wait, but it
 * lets the guest send controlled checks before the host has promoted to
 * controlling — so for a brief window both ends are controlled and libjuice logs
 * `ICE role conflict (both controlled)`. The conflict self-heals via the RFC 8445
 * tiebreaker (the host re-randomizes on promotion and converges to controlling),
 * but it is noisy and the race widens on higher-RTT/relay paths, churning
 * candidate-pair state. See docs/bugs/ice_role_ready_coordination_2026-05-27.md.
 *
 * Default path (this flag OFF) gates the immediate apply on observed
 * peer_controlling_ready: apply now if role_ready already arrived (fast, no
 * conflict), otherwise defer onto the async, non-blocking role_ready wait (Phase
 * 2) which no longer stalls the sim. SSB64_MATCHMAKING_ICE_EARLY_SDP=1 restores
 * the aggressive immediate-apply for A/B testing.
 */
static sb32 mnVSNetAutomatchAMIceEarlySdpEnabled(void)
{
	const char *e;

	e = getenv("SSB64_MATCHMAKING_ICE_EARLY_SDP");
	if ((e != NULL) && (e[0] != '\0'))
	{
		return (atoi(e) != 0) ? TRUE : FALSE;
	}
	return FALSE;
}

/*
 * Async role_ready re-poll cadence (ticks). Used only on the legacy deferral
 * path (early-SDP disabled). Each enqueue is also gated by an outstanding-poll
 * check, so this just bounds the re-arm rate and backs off the longer the host
 * takes, to avoid hammering /v1/match when many guests wait at once.
 */
static u32 mnVSNetAutomatchAMIceRoleReadyAsyncPollInterval(u32 wait_ticks)
{
	if (wait_ticks < 60U) /* first ~1s: responsive */
	{
		return 4U;
	}
	if (wait_ticks < 180U) /* ~1-3s */
	{
		return 16U;
	}
	return 32U; /* beyond ~3s: gentle */
}

static void mnVSNetAutomatchAMIceSaveServerConfig(const MmIceServerConfig *cfg)
{
	sIceSavedStunHost[0] = '\0';
	sIceSavedTurnHost[0] = '\0';
	sIceSavedTurnUser[0] = '\0';
	sIceSavedTurnPass[0] = '\0';
	sIceSavedStunPort = MM_ICE_DEFAULT_STUN_PORT;
	sIceSavedTurnPort = MM_ICE_DEFAULT_TURN_PORT;
	sIceSavedLanDirectGather = FALSE;
	if (cfg == NULL)
	{
		return;
	}
	if (cfg->stun_host != NULL && cfg->stun_host[0] != '\0')
	{
		snprintf(sIceSavedStunHost, sizeof(sIceSavedStunHost), "%s", cfg->stun_host);
	}
	if (cfg->turn_host != NULL && cfg->turn_host[0] != '\0')
	{
		snprintf(sIceSavedTurnHost, sizeof(sIceSavedTurnHost), "%s", cfg->turn_host);
	}
	if (cfg->turn_user != NULL && cfg->turn_user[0] != '\0')
	{
		snprintf(sIceSavedTurnUser, sizeof(sIceSavedTurnUser), "%s", cfg->turn_user);
	}
	if (cfg->turn_pass != NULL && cfg->turn_pass[0] != '\0')
	{
		snprintf(sIceSavedTurnPass, sizeof(sIceSavedTurnPass), "%s", cfg->turn_pass);
	}
	sIceSavedStunPort = (cfg->stun_port != 0U) ? cfg->stun_port : MM_ICE_DEFAULT_STUN_PORT;
	sIceSavedTurnPort = (cfg->turn_port != 0U) ? cfg->turn_port : MM_ICE_DEFAULT_TURN_PORT;
	sIceSavedLanDirectGather = cfg->lan_direct_gather;
}

static void mnVSNetAutomatchAMIceFillSavedServerConfig(MmIceServerConfig *cfg)
{
	if (cfg == NULL)
	{
		return;
	}
	memset(cfg, 0, sizeof(*cfg));
	cfg->stun_host = (sIceSavedStunHost[0] != '\0') ? sIceSavedStunHost : NULL;
	cfg->stun_port = sIceSavedStunPort;
	cfg->turn_host = (sIceSavedTurnHost[0] != '\0') ? sIceSavedTurnHost : NULL;
	cfg->turn_port = sIceSavedTurnPort;
	cfg->turn_user = (sIceSavedTurnUser[0] != '\0') ? sIceSavedTurnUser : NULL;
	cfg->turn_pass = (sIceSavedTurnPass[0] != '\0') ? sIceSavedTurnPass : NULL;
	cfg->lan_direct_gather = sIceSavedLanDirectGather;
}

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
	if ((out_desc_applied != NULL) && (*out_desc_applied != FALSE))
	{
		snprintf(sIceAppliedPeerSdp, sizeof(sIceAppliedPeerSdp), "%s", peer_sdp);
		sIcePeerSdpOnAgent = TRUE;
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
	mmIceSetCandidatePolicy(allow_peer, signal_local,
	                        (allow_peer != FALSE && mr != NULL && mr->peer_lan_hostport[0] != '\0')
	                            ? mr->peer_lan_hostport
	                            : NULL,
	                        (sIceLocalLan[0] != '\0') ? sIceLocalLan : NULL);
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
				port_log("SSB64 ICE: filtered remote trickle candidate %.*s\n", 120, cand);
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

static sb32 mnVSNetAutomatchAMIceInitGatherAgent(const char *bind_spec)
{
	MmIceServerConfig cfg;

	if ((bind_spec == NULL) || (bind_spec[0] == '\0'))
	{
		return FALSE;
	}
	if (sIceSavedLanDirectGather != FALSE)
	{
		mmIceSetLanDirectGather(TRUE);
	}
	else
	{
		mmIceSetLanDirectGather(FALSE);
	}
	mnVSNetAutomatchAMIceFillSavedServerConfig(&cfg);
	if (mmIceInit(bind_spec, &cfg) == FALSE)
	{
		return FALSE;
	}
	if ((sIceQueueSdp[0] != '\0') && (mmIceSetLocalIceAttributesFromSdp(sIceQueueSdp) == FALSE))
	{
		port_log("SSB64 ICE: restore queue ufrag/pwd failed\n");
		mmIceShutdown();
		return FALSE;
	}
	(void)mmIceSetIceControlling(FALSE);
	mmIceSetCallbacks(mnVSNetAutomatchAMIceOnLocalCandidate, mnVSNetAutomatchAMIceOnGatheringDone, NULL);
	if (mmIceStartGathering() == FALSE)
	{
		mmIceShutdown();
		return FALSE;
	}
	mmIceJoinGathering();
	if (mmIceGatherFailed() != FALSE)
	{
		port_log("SSB64 ICE: gather failed after agent init\n");
		mmIceShutdown();
		return FALSE;
	}
	mnVSNetAutomatchAMIceRefreshLocalLanFromGather();
	return TRUE;
}

void mnVSNetAutomatchAMIceReset(void)
{
	mnVSNetAutomatchAMQueuePollReset();
	mmMatchmakingIceSignalsClear();
	sIceTicket[0] = '\0';
	sIceBind[0] = '\0';
	sIceLocalLan[0] = '\0';
	sIceRemoteDescApplied = FALSE;
	sIcePeerSdpOnAgent = FALSE;
	sIceGatheringDone = FALSE;
	sIceHostRole = FALSE;
	sIceRemoteGatheringDonePosted = FALSE;
	sIceTrickleQuietTicks = 0U;
	sPreTicketCandHead = 0U;
	sPreTicketCandCount = 0U;
	sIceValidateMatchActive = FALSE;
	sIceLikelyLanDirect = FALSE;
	sIceSharedLanSession = FALSE;
	sIceCompletedSettleTicks = 0U;
	sIceLanRelaySettleTicks = 0U;
	sIceCompletedRemote[0] = '\0';
	sIceConnectFailReason = "ICE connection failed";
	sIcePeerSetupFailed = FALSE;
	sIceAwaitPeerControllingReady = FALSE;
	sIceAwaitPeerRoleReadyLogged = FALSE;
	sIceConnectPhaseActive = FALSE;
	sIceBootstrapHandoff = FALSE;
	sIceConnectTrickleLastEnqueueMs = 0ULL;
	sIceDeferredPeerSdp[0] = '\0';
	sIceQueueSdp[0] = '\0';
	sIceSavedStunHost[0] = '\0';
	sIceSavedTurnHost[0] = '\0';
	sIceSavedTurnUser[0] = '\0';
	sIceSavedTurnPass[0] = '\0';
	sIceSavedStunPort = MM_ICE_DEFAULT_STUN_PORT;
	sIceSavedTurnPort = MM_ICE_DEFAULT_TURN_PORT;
	sIceSavedLanDirectGather = FALSE;
	sIcePollSuspended = FALSE;
	sIceAppliedPeerSdp[0] = '\0';
#if defined(__ANDROID__)
	sIceHttpsPauseActive = FALSE;
	sIceConnectTickInside = FALSE;
	sIceConnectTickMutexHeld = FALSE;
#endif
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
	/*
	 * Flush queued pre-ticket candidates now that the ticket exists. With the
	 * legacy destroy-on-suspend path the agent (and its candidates) is gone, so
	 * we wait for the post-resume re-gather; but in keep-agent mode the agent is
	 * still live with its gathered candidates (incl. the relay), so signal them
	 * here so a CGNAT peer receives the relay candidate promptly.
	 */
	if ((mnVSNetAutomatchAMIcePollSuspendEnabled() == FALSE) ||
	    ((mnVSNetAutomatchAMIceKeepAgentOnSuspendEnabled() != FALSE) && (mmIceAgentLive() != FALSE)))
	{
		mnVSNetAutomatchAMIceFlushPreTicketCandidates();
	}
}

void mnVSNetAutomatchAMIceSuspendForQueuePoll(void)
{
	sb32 keep_agent;

	if (mnVSNetAutomatchAMIcePollSuspendEnabled() == FALSE)
	{
		return;
	}
	if (sIcePollSuspended != FALSE)
	{
		return;
	}

	keep_agent = mnVSNetAutomatchAMIceKeepAgentOnSuspendEnabled();
	if ((keep_agent != FALSE) && (mmIceAgentLive() != FALSE))
	{
		/*
		 * Keep-alive suspend: park libjuice I/O for the whole queue wait so the
		 * worker's HTTPS polls can't race the agent UDP fds, but DO NOT destroy
		 * the agent. Gathered candidates (host/srflx/relay) and the TURN relay
		 * allocation survive. Do NOT flush pre-ticket candidates here — suspend
		 * runs in MN_AM_BIND, before the ticket is assigned (MM_POLL_QUEUED), so
		 * flushing would POST to /v1/match//ice (empty ticket -> HTTP 400) and
		 * drain the relay candidate from the pre-ticket queue. The candidates
		 * stay queued and are flushed by OnTicketAssigned once the ticket exists.
		 */
#if defined(__ANDROID__)
		if (mnVSNetAutomatchAMIceHttpsSerializeEnabled() != FALSE)
		{
			(void)pthread_mutex_lock(&sIceNetSerializeMutex);
		}
#endif
		mmIceEnsureIoResumed();
		(void)mmIcePoll();
		mmIcePauseIo();
		sIcePollSuspended = TRUE;
#if defined(__ANDROID__)
		if (mnVSNetAutomatchAMIceHttpsSerializeEnabled() != FALSE)
		{
			(void)pthread_mutex_unlock(&sIceNetSerializeMutex);
		}
#endif
		port_log("SSB64 ICE: suspended (agent kept alive, I/O paused) for queue HTTPS\n");
		return;
	}

	/* Legacy destroy/rehydrate suspend (keep-agent disabled or no live agent). */
	sPreTicketCandHead = 0U;
	sPreTicketCandCount = 0U;
	mmMatchmakingIceSignalsClear();
#if defined(__ANDROID__)
	if (mnVSNetAutomatchAMIceHttpsSerializeEnabled() != FALSE)
	{
		(void)pthread_mutex_lock(&sIceNetSerializeMutex);
	}
#endif
	if (mmIceAgentLive() != FALSE)
	{
		mmIceEnsureIoResumed();
		(void)mmIcePoll();
	}
	mmIceShutdown();
	sIcePollSuspended = TRUE;
	sIceGatheringDone = TRUE;
#if defined(__ANDROID__)
	if (mnVSNetAutomatchAMIceHttpsSerializeEnabled() != FALSE)
	{
		(void)pthread_mutex_unlock(&sIceNetSerializeMutex);
	}
#endif
	port_log("SSB64 ICE: suspended agent for queue HTTPS (Android fdsan)\n");
}

sb32 mnVSNetAutomatchAMIceNeedsResume(void)
{
	return (sIcePollSuspended != FALSE) ? TRUE : FALSE;
}

sb32 mnVSNetAutomatchAMIceResumeForConnect(void)
{
	const char *bind_spec;

	if (sIcePollSuspended == FALSE)
	{
		return TRUE;
	}
	/*
	 * Keep-alive resume: the agent and all its gathered candidates / relay
	 * allocation are still live; just un-park libjuice I/O and continue. No
	 * re-init, no re-gather, no lost relay candidate.
	 */
	if (mmIceAgentLive() != FALSE)
	{
		sIcePollSuspended = FALSE;
		mnVSNetAutomatchAMQueuePollReset();
		mmIceEnsureIoResumed();
		port_log("SSB64 ICE: resumed (agent kept alive) for connect\n");
		return TRUE;
	}
	if (sIceQueueSdp[0] == '\0')
	{
		port_log("SSB64 ICE: resume failed (no saved queue SDP)\n");
		return FALSE;
	}
	bind_spec = (sIceLocalLan[0] != '\0') ? sIceLocalLan : sIceBind;
	if (bind_spec[0] == '\0')
	{
		port_log("SSB64 ICE: resume failed (no bind spec)\n");
		return FALSE;
	}
	sIceGatheringDone = FALSE;
	sIceRemoteGatheringDonePosted = FALSE;
	sIceTrickleQuietTicks = 0U;
	if (mnVSNetAutomatchAMIceInitGatherAgent(bind_spec) == FALSE)
	{
		return FALSE;
	}
	sIcePollSuspended = FALSE;
	mnVSNetAutomatchAMQueuePollReset();
	mmIceEnsureIoResumed();
	port_log("SSB64 ICE: resumed agent for connect bind=%s\n", bind_spec);
	return TRUE;
}

sb32 mnVSNetAutomatchAMIceShouldIgnorePollError(const MmMatchResult *ev)
{
	if (ev == NULL)
	{
		return FALSE;
	}
	/* Transient client/network or server errors during queue poll or ICE — keep automatch alive. */
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

sb32 mnVSNetAutomatchAMIceInitOnWorker(const char *bind_spec)
{
	MmIceTurnBundle turn;
	MmIceServerConfig cfg;
	sb32 likely_lan;

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
	if (mmMatchmakingTryGetCachedTurnCredentials(&turn) != FALSE)
	{
		port_log("SSB64 ICE: using prefetched TURN credentials stun=%s:%u turn=%s:%u\n", turn.stun_host,
		         (unsigned int)turn.stun_port, turn.turn_host, (unsigned int)turn.turn_port);
	}
	else if (mmMatchmakingFetchTurnCredentials(&turn) != FALSE)
	{
		port_log("SSB64 ICE: TURN credentials ok stun=%s:%u turn=%s:%u\n", turn.stun_host, (unsigned int)turn.stun_port,
		         turn.turn_host, (unsigned int)turn.turn_port);
	}
	if ((turn.turn_user[0] != '\0') && (turn.turn_pass[0] != '\0'))
	{
		cfg.stun_host = turn.stun_host;
		cfg.stun_port = turn.stun_port;
		cfg.turn_host = turn.turn_host;
		cfg.turn_port = turn.turn_port;
		cfg.turn_user = turn.turn_user;
		cfg.turn_pass = turn.turn_pass;
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
	mnVSNetAutomatchAMIceSaveServerConfig(&cfg);
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
	mmIceSetCandidatePolicy(FALSE, (likely_lan != FALSE) ? TRUE : FALSE, NULL, NULL);
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
	mmIceSetCandidatePolicy(FALSE, signal_local, NULL, (sIceLocalLan[0] != '\0') ? sIceLocalLan : NULL);
	if (mmIceGetLocalDescription(sdp, sizeof(sdp)) == FALSE)
	{
		port_log("SSB64 ICE: mmIceGetLocalDescription failed (bind tick)\n");
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
		(void)mmIceFilterHostFromSignalingSdp(ice_sdp_out);
		if (mmIceSdpHasIceUfrag(ice_sdp_out) == FALSE)
		{
			port_log("SSB64 ICE: queue SDP missing a=ice-ufrag after filter (len=%zu), deferring queue\n",
			         strlen(ice_sdp_out));
			return FALSE;
		}
		snprintf(sIceQueueSdp, sizeof(sIceQueueSdp), "%s", ice_sdp_out);
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
	sIcePeerSdpOnAgent = FALSE;
	sIceRemoteGatheringDonePosted = FALSE;
	sIceTrickleQuietTicks = 0U;
	sIceCompletedSettleTicks = 0U;
	sIceLanRelaySettleTicks = 0U;
	sIceCompletedRemote[0] = '\0';
	sIceConnectFailReason = "ICE connection failed";
	sIcePeerSetupFailed = FALSE;
	sIceAwaitPeerControllingReady = FALSE;
	sIceAwaitPeerRoleReadyLogged = FALSE;
	sIceRoleReadyWaitTicks = 0U;
	sIceBootstrapHandoff = FALSE;
	sIceConnectTrickleLastEnqueueMs = 0ULL;
	sIceDeferredPeerSdp[0] = '\0';
	sIceHostRole = (mr != NULL && mr->you_are_host != FALSE) ? TRUE : FALSE;
#if defined(__ANDROID__)
	sIceHttpsPauseActive = FALSE;
#endif
	mnVSNetAutomatchAMIceRefreshLocalLanForPeer(mr);
	mnVSNetAutomatchAMIceApplyCandidatePolicy(mr);
	sIceSharedLanSession = mnVSNetAutomatchAMIceDeriveAllowPeerHost(mr);
	if (sIceSharedLanSession != FALSE)
	{
		port_log("SSB64 ICE: shared-LAN session - CONNECTING trickle HTTPS disabled\n");
	}
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
	sIceConnectPhaseActive = TRUE;
	if (sIceTicket[0] != '\0')
	{
		mmMatchmakingDropPendingPollMatchJobs(sIceTicket);
	}
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
		mmMatchmakingPostIceRoleReadySync(sIceTicket, "pair", 1U);
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
			/*
			 * Apply the host SDP immediately only once the host is known to be
			 * controlling (peer_ready). Applying before that lets the guest send
			 * controlled checks while the host is still controlled -> "ICE role
			 * conflict (both controlled)". When role_ready has not arrived we fall
			 * through to the async, non-blocking deferral below. The opt-in
			 * early-SDP flag forces the immediate apply regardless (accepts the
			 * transient role conflict for lower match-start latency).
			 */
			if ((peer_ready != FALSE) || (mnVSNetAutomatchAMIceEarlySdpEnabled() != FALSE))
			{
				if (peer_ready == FALSE)
				{
					/* Opt-in early-SDP path: start checks before the host promotes;
					 * they retransmit (and may role-conflict) until the host is up. */
					port_log("SSB64 ICE: applying peer SDP immediately (early-SDP opt-in, role_ready not yet observed)\n");
				}
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
				if (mnVSNetAutomatchAMIceAsyncRoleReadyEnabled() != FALSE)
				{
					/* Drive role_ready via an async worker trickle GET (non-blocking);
					 * the connect tick just reads the cached flag at 60 Hz. */
					if (sIceTicket[0] != '\0')
					{
						mmMatchmakingEnqueuePollIceTrickle(FALSE, sIceTicket);
					}
				}
				else
				{
					mmMatchmakingDropPendingPollMatchJobs(sIceTicket);
				}
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

void mnVSNetAutomatchAMIceHttpsLockBeforeRequest(void)
{
#if defined(__ANDROID__)
	if (mnVSNetAutomatchAMIceHttpsSerializeEnabled() == FALSE)
	{
		return;
	}
	(void)pthread_mutex_lock(&sIceNetSerializeMutex);
	if (sIcePollSuspended != FALSE)
	{
		return;
	}
	sIceHttpsPauseActive = FALSE;
	if (mmIceAgentLive() != FALSE)
	{
		mmIcePauseIo();
		sIceHttpsPauseActive = TRUE;
	}
#endif
}

void mnVSNetAutomatchAMIceHttpsUnlockAfterRequest(void)
{
#if defined(__ANDROID__)
	if (mnVSNetAutomatchAMIceHttpsSerializeEnabled() == FALSE)
	{
		return;
	}
	if (sIcePollSuspended == FALSE)
	{
		if (sIceHttpsPauseActive != FALSE)
		{
			mmIceResumeIo();
			sIceHttpsPauseActive = FALSE;
		}
		mmIceEnsureIoResumed();
	}
	(void)pthread_mutex_unlock(&sIceNetSerializeMutex);
#endif
}

/*
 * TRUE while we want NON-BLOCKING async worker trickle GETs (no serialize-mutex
 * hold across the connect tick, skip a concurrent game-thread mmIcePoll while a
 * worker GET is outstanding). Two windows qualify:
 *   1. Pre-SDP role_ready wait — no remote description applied yet (Phase 2).
 *   2. Post-SDP CONNECTING — remote description applied but not yet CONNECTED.
 *      Needed so late remote candidates (esp. the TURN relay, which the soak
 *      logs show arriving via trickle, not in the queue SDP) are still received
 *      while connectivity checks run.
 * It deliberately stops at CONNECTED: once a pair is nominated the
 * connected->completed window must stay main-thread only (a worker juice_pause_io
 * there causes the recv blackout that strands ICE at state=connected).
 */
static sb32 mnVSNetAutomatchAMIceConnectingTrickleAsyncActive(void)
{
	if (mnVSNetAutomatchAMIceAsyncRoleReadyEnabled() == FALSE)
	{
		return FALSE;
	}
	/* Shared-LAN match: queue SDP already carries host candidates; trickle HTTPS only hurts. */
	if (sIceSharedLanSession != FALSE)
	{
		return FALSE;
	}
	if ((sIceAwaitPeerControllingReady != FALSE) && (sIceRemoteDescApplied == FALSE))
	{
		return TRUE;
	}
	if ((sIceRemoteDescApplied != FALSE) && (sIceConnectPhaseActive != FALSE) && (mmIceAgentLive() != FALSE) &&
	    (mmIceIsConnected() == FALSE))
	{
		return TRUE;
	}
	return FALSE;
}

static u32 mnVSNetAutomatchAMIceConnectTrickleMinMs(void)
{
	const char *e;
	s32 ms;

	e = getenv("SSB64_MATCHMAKING_ICE_CONNECT_TRICKLE_MIN_MS");
	ms = (e != NULL && e[0] != '\0') ? atoi(e) : (s32)ICE_CONNECT_TRICKLE_MIN_MS_DEFAULT;
	if (ms < 50)
	{
		ms = 50;
	}
	if (ms > 2000)
	{
		ms = 2000;
	}
	return (u32)ms;
}

sb32 mnVSNetAutomatchAMIceConnectTrickleMayEnqueue(void)
{
	u64 now;
	u32 min_ms;

	if (sIceSharedLanSession != FALSE)
	{
		return FALSE;
	}
	if ((mmIceIsConnected() != FALSE) || (mmIceIsCompleted() != FALSE))
	{
		return FALSE;
	}
	now = syNetPeerOsMonotonicMs();
	min_ms = mnVSNetAutomatchAMIceConnectTrickleMinMs();
	if ((sIceConnectTrickleLastEnqueueMs != 0ULL) &&
	    ((now - sIceConnectTrickleLastEnqueueMs) < (u64)min_ms))
	{
		return FALSE;
	}
	return TRUE;
}

void mnVSNetAutomatchAMIceConnectTrickleNoteEnqueued(void)
{
	sIceConnectTrickleLastEnqueueMs = syNetPeerOsMonotonicMs();
}

sb32 mnVSNetAutomatchAMIceWorkerMatchPollBlocked(sb32 trickle_only)
{
	/*
	 * Allow trickle-only worker GETs in the non-blocking async window (pre-SDP
	 * role_ready wait + post-SDP CONNECTING) so remote candidates — including a
	 * late TURN relay — keep arriving. Connectivity checks before nomination
	 * just retransmit across a brief juice_pause_io, so there is no nominated
	 * pair to starve here.
	 */
	if ((trickle_only != FALSE) && (mnVSNetAutomatchAMIceConnectingTrickleAsyncActive() != FALSE))
	{
		return FALSE;
	}
	/* CONNECTED..COMPLETED (and full match polls): main-thread trickle only. */
	if ((sIceConnectPhaseActive != FALSE) && (mmIceAgentLive() != FALSE) && (mmIceIsCompleted() == FALSE))
	{
		return TRUE;
	}
	if (sIceAwaitPeerControllingReady != FALSE)
	{
		return TRUE;
	}
	return FALSE;
}

#if defined(__ANDROID__)
static void mnVSNetAutomatchAMIceConnectTickAcquireMutex(void)
{
	if (mnVSNetAutomatchAMIceHttpsSerializeEnabled() == FALSE)
	{
		return;
	}
	if ((sIceConnectTickInside != FALSE) && (sIceConnectTickMutexHeld == FALSE))
	{
		(void)pthread_mutex_lock(&sIceNetSerializeMutex);
		sIceConnectTickMutexHeld = TRUE;
	}
}
#endif

static void mnVSNetAutomatchAMIceRoleReadyTricklePollSync(void)
{
	if (sIceTicket[0] == '\0')
	{
		return;
	}
#if defined(__ANDROID__)
	if (mnVSNetAutomatchAMIceHttpsSerializeEnabled() != FALSE)
	{
		/* Unlock serialize mutex so worker POST …/ice can proceed; pause juice on this thread only. */
		if (sIceConnectTickMutexHeld != FALSE)
		{
			(void)pthread_mutex_unlock(&sIceNetSerializeMutex);
			sIceConnectTickMutexHeld = FALSE;
		}
		if ((sIcePollSuspended == FALSE) && (mmIceAgentLive() != FALSE))
		{
			mmIcePauseIo();
		}
		(void)mmMatchmakingPollMatchIceTrickleSyncEx(sIceTicket, FALSE);
		if (sIcePollSuspended == FALSE)
		{
			mmIceEnsureIoResumed();
		}
		return;
	}
#endif
	(void)mmMatchmakingPollMatchIceTrickleSync(sIceTicket);
}

sb32 mnVSNetAutomatchAMIceConnectTickEnter(void)
{
#if defined(__ANDROID__)
	sIceConnectTickMutexHeld = FALSE;
	if (mnVSNetAutomatchAMIceHttpsSerializeEnabled() == FALSE)
	{
		return TRUE;
	}
	sIceConnectTickInside = TRUE;
	/*
	 * In the async trickle window (role_ready wait + CONNECTING) leave the
	 * serialize mutex free so the worker can run its non-blocking trickle GET;
	 * the connect tick coordinates by skipping mmIcePoll while one is in flight.
	 * Once CONNECTED, hold the mutex so the connected->completed phase serializes
	 * worker HTTPS against the main-thread poll (recv-blackout fix).
	 */
	if (mnVSNetAutomatchAMIceConnectingTrickleAsyncActive() == FALSE)
	{
		(void)pthread_mutex_lock(&sIceNetSerializeMutex);
		sIceConnectTickMutexHeld = TRUE;
	}
#endif
	return TRUE;
}

void mnVSNetAutomatchAMIceConnectTickLeave(void)
{
#if defined(__ANDROID__)
	if (mnVSNetAutomatchAMIceHttpsSerializeEnabled() == FALSE)
	{
		return;
	}
	sIceConnectTickInside = FALSE;
	if (sIceConnectTickMutexHeld != FALSE)
	{
		(void)pthread_mutex_unlock(&sIceNetSerializeMutex);
		sIceConnectTickMutexHeld = FALSE;
	}
#endif
}

s32 mnVSNetAutomatchAMIceConnectTick(void)
{
	MmIceState st;
	s32 ret;

	if (sIcePeerSetupFailed != FALSE)
	{
		return -1;
	}
	/* Offer-exchange yields the game thread; do not re-enter bootstrap. */
	if (sIceBootstrapHandoff != FALSE)
	{
		(void)mmIcePoll();
		return 0;
	}
	if (mnVSNetAutomatchAMIceConnectTickEnter() == FALSE)
	{
		mnVSNetAutomatchAMIceConnectTickLeave();
		return -1;
	}
	if (sIceAwaitPeerControllingReady != FALSE)
	{
		sb32 desc_applied;
		sb32 peer_ready;

		sb32 async_role_ready;

		async_role_ready = mnVSNetAutomatchAMIceAsyncRoleReadyEnabled();
		peer_ready = mmMatchmakingIcePeerControllingReady();
		if (peer_ready == FALSE)
		{
			sIceRoleReadyWaitTicks++;
			if (async_role_ready != FALSE)
			{
				/* Non-blocking: re-arm an async worker trickle GET when none is in
				 * flight. The sim keeps advancing at 60 Hz while we wait; the
				 * cadence backs off the longer the host takes. */
				if ((sIceTicket[0] != '\0') &&
				    (mmMatchmakingPollMatchOutstanding(sIceTicket) == FALSE) &&
				    ((sIceRoleReadyWaitTicks %
				      mnVSNetAutomatchAMIceRoleReadyAsyncPollInterval(sIceRoleReadyWaitTicks)) == 1U))
				{
					mmMatchmakingEnqueuePollIceTrickle(FALSE, sIceTicket);
				}
			}
			else if ((sIceTicket[0] != '\0') && (((sIceRoleReadyWaitTicks % 16U) == 1U)))
			{
				mnVSNetAutomatchAMIceRoleReadyTricklePollSync();
			}
			peer_ready = mmMatchmakingIcePeerControllingReady();
			if (peer_ready == FALSE && sIceRoleReadyWaitTicks < ICE_ROLE_READY_WAIT_MAX_TICKS)
			{
				if (sIceAwaitPeerRoleReadyLogged == FALSE)
				{
					port_log("SSB64 ICE: waiting for peer controlling role_ready\n");
					sIceAwaitPeerRoleReadyLogged = TRUE;
				}
				/* Pre-SDP: skip the game-thread poll while an async worker trickle
				 * GET is in flight (avoids poll-mode fd overlap; nothing to recv
				 * before the remote description is applied). */
				if ((async_role_ready == FALSE) || (mmMatchmakingPollMatchOutstanding(sIceTicket) == FALSE))
				{
					(void)mmIcePoll();
				}
				mnVSNetAutomatchAMIceDrainRemoteCandidates();
				ret = 0;
				goto ice_connect_done;
			}
			if (peer_ready != FALSE)
			{
				port_log("SSB64 ICE: controlling role_ready received (%u ticks) — applying deferred peer SDP\n",
				         (unsigned int)sIceRoleReadyWaitTicks);
			}
			else
			{
				port_log(
				    "SSB64 ICE: peer_controlling_ready TIMEOUT (%u ticks) — applying deferred peer SDP anyway (check host role-ready / trickle polls)\n",
				    (unsigned int)sIceRoleReadyWaitTicks);
			}
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
				ret = -1;
				goto ice_connect_done;
			}
			if (desc_applied != FALSE)
			{
				sIceRemoteDescApplied = TRUE;
			}
			sIceDeferredPeerSdp[0] = '\0';
			sIceAwaitPeerControllingReady = FALSE;
			/* Clear pre-SDP queued polls at the transition; the CONNECTING phase
			 * re-drives its own trickle cadence (ConnectTricklePollInterval) so a
			 * late remote relay still arrives. Worker GETs stop only at CONNECTED
			 * (see mnVSNetAutomatchAMIceConnectingTrickleAsyncActive). */
			if ((async_role_ready != FALSE) && (sIceTicket[0] != '\0'))
			{
				mmMatchmakingDropPendingPollMatchJobs(sIceTicket);
			}
			mnVSNetAutomatchAMIceDrainRemoteCandidates();
			port_log("SSB64 ICE: peer SDP applied after controlling role_ready\n");
		}
	}
#if defined(__ANDROID__)
	/*
	 * CONNECTING async window: a worker trickle GET pauses libjuice I/O on this
	 * agent, so don't poll concurrently this tick — skip it and let the in-flight
	 * GET land remote candidates (incl. a late relay). The serialize mutex was
	 * left free by ConnectTickEnter, so the worker isn't blocked by the game
	 * thread and the sim keeps advancing.
	 */
	if ((mnVSNetAutomatchAMIceConnectingTrickleAsyncActive() != FALSE) &&
	    (mmMatchmakingPollMatchOutstanding(sIceTicket) != FALSE))
	{
		mnVSNetAutomatchAMIceDrainRemoteCandidates();
		ret = 0;
		goto ice_connect_done;
	}
	mnVSNetAutomatchAMIceConnectTickAcquireMutex();
#endif
	st = mmIcePoll();
	mnVSNetAutomatchAMIceDrainRemoteCandidates();
	mnVSNetAutomatchAMIceMaybePostRemoteGatheringDone();
	/*
	 * CONNECTED is enough for bootstrap datagrams. Waiting only for COMPLETED
	 * lets Android (poll + HTTPS pause) lag the host's offer window after the
	 * pair is already nominated — soak offer-exchange timeouts.
	 */
	if ((st == MM_ICE_STATE_COMPLETED) || (st == MM_ICE_STATE_CONNECTED))
	{
		char remote[128];
		sb32 skip_path_validate;
		const char *ready_word;

		remote[0] = '\0';
		skip_path_validate = FALSE;
		ready_word = (st == MM_ICE_STATE_COMPLETED) ? "completed" : "connected";
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
				if ((st == MM_ICE_STATE_CONNECTED) && (mnVSNetAutomatchAMIceEnvVerbose() != FALSE) &&
				    ((sIceCompletedSettleTicks % 120U) == 1U))
				{
					port_log("SSB64 ICE: waiting for selected path (state=connected)\n");
				}
				ret = 0;
				goto ice_connect_done;
			}
			else if (mmIceIsConnected() != FALSE)
			{
				port_log("SSB64 ICE: %s but selected path unavailable after settle (continuing)\n", ready_word);
				skip_path_validate = TRUE;
			}
			else
			{
				sIceConnectFailReason = "ICE path validation failed (no nominated path)";
				port_log("SSB64 ICE: path validation failed (no selected remote)\n");
				sIceValidateMatchActive = FALSE;
				ret = -1;
				goto ice_connect_done;
			}
		}
		else
		{
			snprintf(sIceCompletedRemote, sizeof(sIceCompletedRemote), "%s", remote);
			sIceCompletedSettleTicks = 0U;
			port_log("SSB64 ICE: %s remote=%s\n", ready_word, remote);
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
						ret = 0;
						goto ice_connect_done;
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
					ret = -1;
					goto ice_connect_done;
				}
			}
			sIceValidateMatchActive = FALSE;
		}
		if (sIceTicket[0] != '\0')
		{
			mmMatchmakingDropPendingPollMatchJobs(sIceTicket);
		}
		mmIceEnsureIoResumed();
		syNetPeerSetIceTransport(TRUE);
		sIceBootstrapHandoff = TRUE;
		ret = 1;
		goto ice_connect_done;
	}
	sIceCompletedSettleTicks = 0U;
	sIceLanRelaySettleTicks = 0U;
	if (st == MM_ICE_STATE_FAILED)
	{
		sIceConnectFailReason = "ICE connection failed";
		port_log("SSB64 ICE: connection failed\n");
		ret = -1;
		goto ice_connect_done;
	}
	ret = 0;

ice_connect_done:
	if (ret != 0)
	{
		sIceConnectPhaseActive = FALSE;
	}
	if (ret > 0)
	{
		mmIceEnsureIoResumed();
	}
	mnVSNetAutomatchAMIceConnectTickLeave();
	return ret;
}

const char *mnVSNetAutomatchAMIceConnectFailureReason(void)
{
	return sIceConnectFailReason;
}

void mnVSNetAutomatchAMIceLogConnectAbortDiag(const char *reason, u32 elapsed_ms)
{
	MmIceState st;
	char local_path[128];
	char remote_path[128];
	char remote_typ[16];
	char srflx[128];
	char relay[128];
	const char *why;
	const char *fail_reason;
	sb32 have_path;
	sb32 have_typ;
	sb32 have_srflx;
	sb32 have_relay;

	why = (reason != NULL && reason[0] != '\0') ? reason : "aborted";
	fail_reason = (sIceConnectFailReason != NULL && sIceConnectFailReason[0] != '\0') ? sIceConnectFailReason
											 : "(none)";
	st = mmIceGetState();
	local_path[0] = '\0';
	remote_path[0] = '\0';
	remote_typ[0] = '\0';
	srflx[0] = '\0';
	relay[0] = '\0';
	have_path = mmIceGetSelectedPath(local_path, sizeof(local_path), remote_path, sizeof(remote_path));
	have_typ = mmIceGetSelectedRemoteCandidateTyp(remote_typ, sizeof(remote_typ));
	have_srflx = mmIceGetSrflxHostport(srflx, sizeof(srflx));
	have_relay = mmIceGetRelayHostport(relay, sizeof(relay));

	port_log(
	    "SSB64 Automatch: connect_abort reason=%s ice_fail=%s ice_state=%s agent_live=%d connected=%d completed=%d "
	    "failed=%d elapsed_ms=%u remote_typ=%s local_path=%s remote_path=%s local_lan=%s srflx=%s relay=%s "
	    "host_role=%s connect_phase=%d validate_active=%d\n",
	    why, fail_reason, mmIceStateName(st), (int)(mmIceAgentLive() != FALSE), (int)(mmIceIsConnected() != FALSE),
	    (int)(mmIceIsCompleted() != FALSE), (int)(st == MM_ICE_STATE_FAILED), (unsigned int)elapsed_ms,
	    (have_typ != FALSE && remote_typ[0] != '\0') ? remote_typ : "(none)",
	    (have_path != FALSE && local_path[0] != '\0') ? local_path : "(none)",
	    (have_path != FALSE && remote_path[0] != '\0') ? remote_path : "(none)",
	    (sIceLocalLan[0] != '\0') ? sIceLocalLan : "(none)",
	    (have_srflx != FALSE && srflx[0] != '\0') ? srflx : "(none)",
	    (have_relay != FALSE && relay[0] != '\0') ? relay : "(none)",
	    (sIceHostRole != FALSE) ? "controlling" : "controlled", (int)(sIceConnectPhaseActive != FALSE),
	    (int)(sIceValidateMatchActive != FALSE));

	/* Best-effort candidate typ/addr lines when a pair was nominated. */
	if (have_path != FALSE)
	{
		mmIceLogSelectedCandidates();
	}
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
	if ((mr->ticket_id[0] != '\0'))
	{
		mmMatchmakingDropPendingPollMatchJobs(mr->ticket_id);
	}
	mmIceEnsureIoResumed();
	syNetPeerSetAutomatchBootstrapContextEx(mr->match_id, mr->ticket_id, mr->peer_player_id);
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
	if (mmIceIsCompleted() != FALSE)
	{
		return 0U;
	}
	if (sIceAwaitPeerControllingReady != FALSE)
	{
		return 0U;
	}
	/* connected→completed: main-thread mmIcePoll only (no worker GET pause windows). */
	if (mmIceIsConnected() != FALSE)
	{
		return 0U;
	}
	/* Shared-LAN match: host/srflx already in peer SDP; 30 Hz trickle was the connectivity lag. */
	if (sIceSharedLanSession != FALSE)
	{
		return 0U;
	}
	return ICE_CONNECT_TRICKLE_INTERVAL_TICKS;
}

#endif /* PORT && SSB64_NETMENU && SSB64_NETPLAY_ICE */
