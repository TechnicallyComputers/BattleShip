#ifndef MM_ICE_H
#define MM_ICE_H

#include <PR/ultratypes.h>
#include <ssb_types.h>

#if defined(PORT) && defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE)

typedef enum MmIceState
{
	MM_ICE_STATE_IDLE = 0,
	MM_ICE_STATE_GATHERING,
	MM_ICE_STATE_CONNECTING,
	MM_ICE_STATE_CONNECTED,
	MM_ICE_STATE_COMPLETED,
	MM_ICE_STATE_FAILED
} MmIceState;

#define MM_ICE_DEFAULT_COTURN_HOST "coturn.technicallycomputers.ca"
#define MM_ICE_DEFAULT_STUN_PORT 3478U
#define MM_ICE_DEFAULT_TURN_PORT 3478U

typedef struct MmIceServerConfig
{
	const char *stun_host;
	u16 stun_port;
	const char *turn_host;
	u16 turn_port;
	const char *turn_user;
	const char *turn_pass;
} MmIceServerConfig;

typedef void (*MmIceOnLocalCandidateFn)(const char *candidate_sdp, void *user_ptr);
typedef void (*MmIceOnGatheringDoneFn)(void *user_ptr);

extern sb32 mmIceInit(const char *bind_hostport, const MmIceServerConfig *cfg);
extern void mmIceShutdown(void);
extern void mmIceSetCallbacks(MmIceOnLocalCandidateFn on_candidate, MmIceOnGatheringDoneFn on_gathering_done,
                              void *user_ptr);
extern sb32 mmIceStartGathering(void);
extern sb32 mmIceGetLocalDescription(char *out, u32 out_cap);
extern sb32 mmIceApplyRemoteDescription(const char *sdp);
extern sb32 mmIceAddRemoteCandidate(const char *candidate_sdp);
extern sb32 mmIceSetRemoteGatheringDone(void);
extern MmIceState mmIcePoll(void);
extern sb32 mmIceIsConnected(void);
extern sb32 mmIceIsCompleted(void);
extern int mmIceSend(const u8 *buf, u32 len);
extern sb32 mmIcePopReceived(u8 *out, u32 out_cap, u32 *out_len);
extern sb32 mmIceGetSelectedPath(char *local, u32 local_cap, char *remote, u32 remote_cap);
/* Best-effort srflx host:port for matchmaking metrics (udp_endpoint field). */
extern sb32 mmIceGetReflexiveHostport(char *out, u32 out_cap);
extern sb32 mmIceFetchTurnCredentials(char *user_out, u32 user_cap, char *pass_out, u32 pass_cap);

#else

typedef enum MmIceState
{
	MM_ICE_STATE_IDLE = 0
} MmIceState;

#define mmIceInit(b, c) FALSE
#define mmIceShutdown() ((void)0)
#define mmIceSetCallbacks(a, b, c) ((void)0)
#define mmIceStartGathering() FALSE
#define mmIceGetLocalDescription(o, n) FALSE
#define mmIceApplyRemoteDescription(s) FALSE
#define mmIceAddRemoteCandidate(s) FALSE
#define mmIceSetRemoteGatheringDone() FALSE
#define mmIcePoll() MM_ICE_STATE_IDLE
#define mmIceIsConnected() FALSE
#define mmIceIsCompleted() FALSE
#define mmIceSend(b, l) (-1)
#define mmIcePopReceived(o, c, l) FALSE
#define mmIceGetSelectedPath(l, lc, r, rc) FALSE
#define mmIceGetReflexiveHostport(o, n) FALSE
#define mmIceFetchTurnCredentials(u, uc, p, pc) FALSE

#endif

#endif /* MM_ICE_H */
