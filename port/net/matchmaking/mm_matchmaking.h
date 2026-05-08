#ifndef MM_MATCHMAKING_H
#define MM_MATCHMAKING_H

#include <PR/ultratypes.h>
#include <ssb_types.h>

#if defined(PORT) && defined(SSB64_NETMENU) && !defined(_WIN32)

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

/* Credentials: load/store under XDG_CONFIG_HOME/ssb64/ (see mm_matchmaking.c). */
extern sb32 mmMatchmakingLoadCredentials(sb32 verbose);

extern void mmMatchmakingEnqueueEnsurePlayer(sb32 verbose);
extern void mmMatchmakingEnqueueJoinQueue(sb32 verbose, const char *udp_endpoint, u8 fighter_kind, sb32 has_fkind,
                                          const char *lan_endpoint_opt);
extern void mmMatchmakingEnqueueHeartbeat(sb32 verbose, const char *ticket_id);
extern void mmMatchmakingEnqueuePollMatch(sb32 verbose, const char *ticket_id);
extern void mmMatchmakingEnqueueCancel(sb32 verbose, const char *ticket_id);

extern sb32 mmMatchmakingDrainCompleted(MmMatchResult *out);

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
#define mmMatchmakingEnqueuePollMatch(v, t)
#define mmMatchmakingEnqueueCancel(v, t)
#define mmMatchmakingDrainCompleted(out) FALSE

#endif

#endif /* MM_MATCHMAKING_H */
