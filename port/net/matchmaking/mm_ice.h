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
	sb32 lan_direct_gather;
} MmIceServerConfig;

typedef void (*MmIceOnLocalCandidateFn)(const char *candidate_sdp, void *user_ptr);
typedef void (*MmIceOnGatheringDoneFn)(void *user_ptr);

extern sb32 mmIceInit(const char *bind_hostport, const MmIceServerConfig *cfg);
extern void mmIceShutdown(void);
/** TRUE while a libjuice agent exists (not shut down). */
extern sb32 mmIceAgentLive(void);
/** Android POLL mode: pause shared libjuice poll thread during matchmaking HTTPS. */
extern void mmIcePauseIo(void);
extern void mmIceResumeIo(void);
/** Drain stacked juice_pause_io depth so poll-mode UDP recv is not stuck after HTTPS. */
extern void mmIceEnsureIoResumed(void);
/** FALSE only when ICE is COMPLETED (VS staging recv); TRUE during CONNECTING/CONNECTED for fdsan. */
extern sb32 mmIceShouldSerializeMatchmakingHttps(void);
extern void mmIceSetLanDirectGather(sb32 enabled);
extern sb32 mmIceIsLanDirectGather(void);
extern sb32 mmIceIsFailed(void);
extern sb32 mmIceParseBindHostFromSpec(const char *bind_hostport, char *host_out, u32 host_cap);
extern sb32 mmIceParseBindPortFromSpec(const char *bind_hostport, u16 *out_port);
/** First gathered local typ=host host:port (actual bind after ephemeral gather). */
extern sb32 mmIceGetLocalHostHostport(char *out, u32 out_cap);
/** Peer-directed local typ=host host:port (multi-NIC route/subnet aware). */
extern sb32 mmIceGetLocalHostHostportForPeer(const char *peer_hostport, char *out, u32 out_cap);
/** Post-ICE bootstrap bind: RFC1918 selected local, else peer-directed/local host (never srflx/WAN). */
extern sb32 mmIceGetBootstrapBindHostport(const char *peer_hostport, char *out, u32 out_cap);
extern void mmIceSetCallbacks(MmIceOnLocalCandidateFn on_candidate, MmIceOnGatheringDoneFn on_gathering_done,
                              void *user_ptr);
extern sb32 mmIceStartGathering(void);
extern sb32 mmIceGatherInProgress(void);
extern sb32 mmIceGatherFailed(void);
extern sb32 mmIceGetLocalDescription(char *out, u32 out_cap);
extern void mmIceSetCandidatePolicy(sb32 allow_peer_host, sb32 signal_local_host, const char *peer_lan_hostport,
                                      const char *local_lan_hostport);
extern sb32 mmIceFilterHostFromSignalingSdp(char *sdp);
extern sb32 mmIceShouldAcceptRemoteCandidate(const char *candidate_sdp);
extern sb32 mmIceShouldSignalLocalCandidate(const char *candidate_sdp);
/** TRUE when SDP contains a real `a=ice-ufrag:` line (required for queue / set_remote_description). */
extern sb32 mmIceSdpHasIceUfrag(const char *sdp);
/** Parse `a=ice-ufrag` / `a=ice-pwd` from queue SDP. */
extern sb32 mmIceParseSdpIceCredentials(const char *sdp, char *ufrag_out, u32 ufrag_cap, char *pwd_out, u32 pwd_cap);
/** After mmIceInit, before gather: restore queue ufrag/pwd so peer's stored ice_sdp stays valid. */
extern sb32 mmIceSetLocalIceAttributesFromSdp(const char *sdp);
/** Block until async gather thread completes; drains callback queues once. */
extern void mmIceJoinGathering(void);
extern sb32 mmIceApplyRemoteDescription(const char *sdp);
/** Full remote SDP or single candidate line; sets *out_desc_applied when juice_set_remote_description succeeds. */
extern sb32 mmIceApplyPeerIceSignaling(const char *sdp, sb32 *out_desc_applied);
extern sb32 mmIceAddRemoteCandidate(const char *candidate_sdp);
extern void mmIceLogSelectedCandidates(void);
extern sb32 mmIceGetSelectedRemoteCandidateTyp(char *typ, u32 typ_cap);
extern sb32 mmIceValidateSelectedRemotePath(const char *peer_hostport, const char *peer_lan_hostport,
                                            const char *local_lan_hostport);
extern sb32 mmIceSetIceControlling(sb32 controlling);
extern sb32 mmIceSetRemoteGatheringDone(void);
extern MmIceState mmIcePoll(void);
/** Snapshot of last known ICE state without draining juice callbacks. */
extern MmIceState mmIceGetState(void);
extern const char *mmIceStateName(MmIceState st);
extern sb32 mmIceIsConnected(void);
extern sb32 mmIceIsCompleted(void);
/*
 * Send over ICE: returns (int)len on success, 0 if JUICE_ERR_AGAIN (retry), -1 on hard error.
 * juice_send itself returns JUICE_ERR_* codes, not byte counts — do not compare to len without this wrapper.
 */
extern int mmIceSend(const u8 *buf, u32 len);
extern int mmIceLastSendJuiceError(void);
extern const char *mmIceSendErrorString(int juice_err);
extern sb32 mmIcePopReceived(u8 *out, u32 out_cap, u32 *out_len);
extern sb32 mmIceGetSelectedPath(char *local, u32 local_cap, char *remote, u32 remote_cap);
/* Best-effort srflx host:port from gathered candidates (queue/heartbeat udp_endpoint). */
extern sb32 mmIceGetSrflxHostport(char *out, u32 out_cap);
extern sb32 mmIceGetRelayHostport(char *out, u32 out_cap);
/* Post-connect selected local address, else srflx fallback. */
extern sb32 mmIceGetReflexiveHostport(char *out, u32 out_cap);
extern sb32 mmIceFetchTurnCredentials(char *user_out, u32 user_cap, char *pass_out, u32 pass_cap);

#else

typedef enum MmIceState
{
	MM_ICE_STATE_IDLE = 0
} MmIceState;

#define mmIceInit(b, c) FALSE
#define mmIceShutdown() ((void)0)
#define mmIceAgentLive() FALSE
#define mmIcePauseIo() ((void)0)
#define mmIceResumeIo() ((void)0)
#define mmIceEnsureIoResumed() ((void)0)
#define mmIceShouldSerializeMatchmakingHttps() TRUE
#define mmIceSetLanDirectGather(e) ((void)(e))
#define mmIceIsLanDirectGather() FALSE
#define mmIceIsFailed() FALSE
#define mmIceSetCallbacks(a, b, c) ((void)0)
#define mmIceStartGathering() FALSE
#define mmIceGatherInProgress() FALSE
#define mmIceGatherFailed() FALSE
#define mmIceGetLocalDescription(o, n) FALSE
#define mmIceSetCandidatePolicy(a, s, p, l) ((void)(a), (void)(s), (void)(p), (void)(l))
#define mmIceFilterHostFromSignalingSdp(s) FALSE
#define mmIceShouldAcceptRemoteCandidate(s) TRUE
#define mmIceShouldSignalLocalCandidate(s) TRUE
#define mmIceSdpHasIceUfrag(s) FALSE
#define mmIceParseSdpIceCredentials(s, u, uc, p, pc) FALSE
#define mmIceSetLocalIceAttributesFromSdp(s) FALSE
#define mmIceJoinGathering() ((void)0)
#define mmIceApplyRemoteDescription(s) FALSE
#define mmIceApplyPeerIceSignaling(s, o) FALSE
#define mmIceAddRemoteCandidate(s) FALSE
#define mmIceLogSelectedCandidates() ((void)0)
#define mmIceGetSelectedRemoteCandidateTyp(t, c) FALSE
#define mmIceValidateSelectedRemotePath(p, l, ll) TRUE
#define mmIceSetIceControlling(c) ((void)(c), FALSE)
#define mmIceSetRemoteGatheringDone() FALSE
#define mmIcePoll() MM_ICE_STATE_IDLE
#define mmIceGetState() MM_ICE_STATE_IDLE
#define mmIceStateName(st) "disabled"
#define mmIceIsConnected() FALSE
#define mmIceIsCompleted() FALSE
#define mmIceSend(b, l) (-1)
#define mmIceLastSendJuiceError() (-1)
#define mmIceSendErrorString(e) "disabled"
#define mmIcePopReceived(o, c, l) FALSE
#define mmIceGetSelectedPath(l, lc, r, rc) FALSE
#define mmIceGetSrflxHostport(o, n) FALSE
#define mmIceGetRelayHostport(o, n) FALSE
#define mmIceGetLocalHostHostport(o, n) FALSE
#define mmIceGetLocalHostHostportForPeer(p, o, n) FALSE
#define mmIceGetBootstrapBindHostport(p, o, n) FALSE
#define mmIceGetReflexiveHostport(o, n) FALSE
#define mmIceFetchTurnCredentials(u, uc, p, pc) FALSE

#endif

#endif /* MM_ICE_H */
