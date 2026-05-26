#ifndef MM_MATCHMAKING_H
#define MM_MATCHMAKING_H

#include <PR/ultratypes.h>
#include <ssb_types.h>

#if defined(PORT) && defined(SSB64_NETMENU)

typedef enum MmPollKind
{
	MM_POLL_NONE = 0,
	MM_POLL_ERROR,
	MM_POLL_PLAYER_READY,
	MM_POLL_QUEUED,
	MM_POLL_MATCHED,
	MM_POLL_CANCEL_OK,
	MM_POLL_HEARTBEAT_OK,
} MmPollKind;

typedef struct MmMatchResult
{
	u32 session_id;
	char peer_hostport[128];
	char peer_lan_hostport[128];
	/** Opponent coturn relay `ip:port` when they allocated TURN (CGNAT fallback). */
	char peer_turn_hostport[128];
#if defined(SSB64_NETPLAY_ICE)
	char peer_ice_sdp[4096];
	char pending_ice_candidate[280];
	sb32 has_pending_ice_candidate;
#endif
	sb32 you_are_host;
	char match_id[64];
	char peer_player_id[64];
	char ticket_id[64];
	char error_message[256];
	long http_status;
	MmPollKind kind;
} MmMatchResult;

extern void mmMatchmakingStartup(void);
extern void mmMatchmakingShutdown(void);

/*
 * Credentials: load/store as matchmaking.cred in the per-user app data dir (same tree as
 * ssb64.log). Legacy XDG_CONFIG_HOME/ssb64/ and %APPDATA%\\ssb64\\ paths are migrated on load.
 * If the server rejects cached creds (missing player row / bad token), the client backs up the
 * file to matchmaking.cred.bak and registers a fresh player via POST /v1/players.
 * Worker thread + libcurl; compiled for all SSB64_NETMENU builds (incl. MinGW).
 */
extern sb32 mmMatchmakingLoadCredentials(sb32 verbose);

extern void mmMatchmakingEnqueueEnsurePlayer(sb32 verbose);
extern void mmMatchmakingEnqueueJoinQueue(sb32 verbose, const char *udp_endpoint, u8 fighter_kind, sb32 has_fkind,
                                          const char *lan_endpoint_opt);
extern void mmMatchmakingEnqueueJoinQueueEx(sb32 verbose, const char *udp_endpoint, u8 fighter_kind, sb32 has_fkind,
                                            const char *lan_endpoint_opt, const char *turn_endpoint_opt);
#if defined(SSB64_NETPLAY_ICE)
extern void mmMatchmakingEnqueueJoinQueueIce(sb32 verbose, const char *udp_endpoint, const char *ice_sdp,
                                             u8 fighter_kind, sb32 has_fkind, const char *lan_endpoint_opt);
extern void mmMatchmakingEnqueueIceSignal(sb32 verbose, const char *ticket_id, const char *candidate_sdp);
typedef struct MmIceTurnBundle
{
	char stun_host[128];
	u16 stun_port;
	char turn_host[128];
	u16 turn_port;
	u16 turns_port;
	char turn_user[192];
	char turn_pass[192];
	char realm[96];
} MmIceTurnBundle;

extern sb32 mmMatchmakingFetchTurnCredentials(MmIceTurnBundle *out);
extern sb32 mmMatchmakingPopIceCandidate(char *out, u32 out_cap);
#endif
extern void mmMatchmakingEnqueueHeartbeat(sb32 verbose, const char *ticket_id);
extern void mmMatchmakingEnqueueHeartbeatWithEndpoints(sb32 verbose, const char *ticket_id, const char *udp_endpoint,
                                                       const char *lan_endpoint_opt);
extern void mmMatchmakingEnqueueHeartbeatWithEndpointsEx(sb32 verbose, const char *ticket_id, const char *udp_endpoint,
                                                         const char *lan_endpoint_opt, const char *turn_endpoint_opt);
extern void mmMatchmakingEnqueuePollMatch(sb32 verbose, const char *ticket_id);
extern void mmMatchmakingEnqueueCancel(sb32 verbose, const char *ticket_id);

extern sb32 mmMatchmakingDrainCompleted(MmMatchResult *out);
/* Approximate pending MM worker jobs (HTTPS not yet finished); for adaptive client polling. */
extern u32 mmMatchmakingApproxPendingJobs(void);

#else

typedef enum MmPollKind
{
	MM_POLL_NONE = 0,
} MmPollKind;

typedef struct MmMatchResult
{
	MmPollKind kind;
} MmMatchResult;

#define mmMatchmakingStartup()
#define mmMatchmakingShutdown()
#define mmMatchmakingLoadCredentials(v) FALSE
#define mmMatchmakingEnqueueEnsurePlayer(v)
#define mmMatchmakingEnqueueJoinQueue(v, e, fk, hk, lan) ((void)0)
#define mmMatchmakingEnqueueHeartbeat(v, t)
#define mmMatchmakingEnqueueHeartbeatWithEndpoints(v, t, u, l)
#define mmMatchmakingEnqueuePollMatch(v, t)
#define mmMatchmakingEnqueueCancel(v, t)
#define mmMatchmakingDrainCompleted(out) FALSE
#define mmMatchmakingApproxPendingJobs() ((u32)0)

#endif

#endif /* MM_MATCHMAKING_H */
