#ifndef MM_ICE_AUTOMATCH_H
#define MM_ICE_AUTOMATCH_H

#include <PR/ultratypes.h>
#include <ssb_types.h>
#include <mm_matchmaking.h>

#if defined(PORT) && defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE)

extern void mnVSNetAutomatchAMIceReset(void);
extern void mnVSNetAutomatchAMIceOnTicketAssigned(const char *ticket);
/** Android: tear down libjuice while queued (no poll thread during MN_AM_POLL). */
extern void mnVSNetAutomatchAMIceSuspendForQueuePoll(void);
extern sb32 mnVSNetAutomatchAMIceNeedsResume(void);
extern sb32 mnVSNetAutomatchAMIceResumeForConnect(void);
/** MM worker: HTTPS TURN fetch (cached), libjuice init, and gather start. */
extern sb32 mnVSNetAutomatchAMIceInitOnWorker(const char *bind_spec);
extern sb32 mnVSNetAutomatchAMIceBindTick(char *ice_sdp_out, u32 ice_sdp_cap);
/** Discovered LAN host:port (refreshed after gather; may be empty until bind tick). */
extern const char *mnVSNetAutomatchAMIceLocalLan(void);
/** FALSE when shared-LAN / LAN-direct gather should not register coturn relay on queue join. */
extern sb32 mnVSNetAutomatchAMIceShouldQueueTurnEndpoint(void);
extern sb32 mnVSNetAutomatchAMIceBeginConnect(const MmMatchResult *mr);
/* 0 = in progress, 1 = ICE completed and path validated, -1 = failed */
extern s32 mnVSNetAutomatchAMIceConnectTick(void);
extern const char *mnVSNetAutomatchAMIceConnectFailureReason(void);
/** Always-on abort summary for ssb64.log (ICE state, path, TURN/LAN hints). */
extern void mnVSNetAutomatchAMIceLogConnectAbortDiag(const char *reason, u32 elapsed_ms);
extern sb32 mnVSNetAutomatchAMIceShouldIgnorePollError(const MmMatchResult *ev);
extern sb32 mnVSNetAutomatchAMIceBootstrapPeer(const MmMatchResult *mr, const char *bind);
/* Trickle poll cadence while ICE_CONNECT (0 = skip poll this tick). */
extern u32 mnVSNetAutomatchAMIceConnectTricklePollInterval(void);
/** Wall-clock gate for CONNECTING trickle GETs (shared LAN always FALSE). */
extern sb32 mnVSNetAutomatchAMIceConnectTrickleMayEnqueue(void);
extern void mnVSNetAutomatchAMIceConnectTrickleNoteEnqueued(void);
extern void mnVSNetAutomatchAMIceNotifyPeerAbort(const MmMatchResult *mr);
/** Android: serialize matchmaking HTTPS with libjuice (fdsan). Worker holds lock through curl. */
extern void mnVSNetAutomatchAMIceHttpsLockBeforeRequest(void);
extern void mnVSNetAutomatchAMIceHttpsUnlockAfterRequest(void);
extern sb32 mnVSNetAutomatchAMIceConnectTickEnter(void);
extern void mnVSNetAutomatchAMIceConnectTickLeave(void);
/** TRUE while guest defers SDP — block worker full match polls (trickle-only still allowed). */
extern sb32 mnVSNetAutomatchAMIceWorkerMatchPollBlocked(sb32 trickle_only);
/** Android MN_AM_POLL: wall-clock gate before worker GET /v1/match (fdsan during queue wait). */
extern sb32 mnVSNetAutomatchAMQueuePollMayEnqueue(sb32 trickle_only);
extern void mnVSNetAutomatchAMQueuePollNoteEnqueued(void);
extern void mnVSNetAutomatchAMQueuePollNoteStillQueued(void);
extern void mnVSNetAutomatchAMQueuePollNoteHttp0Cooldown(void);
extern void mnVSNetAutomatchAMQueuePollReset(void);

#else

#define mnVSNetAutomatchAMIceReset() ((void)0)
#define mnVSNetAutomatchAMIceOnTicketAssigned(t) ((void)0)
#define mnVSNetAutomatchAMIceSuspendForQueuePoll() ((void)0)
#define mnVSNetAutomatchAMIceNeedsResume() FALSE
#define mnVSNetAutomatchAMIceResumeForConnect() FALSE
#define mnVSNetAutomatchAMIceInitOnWorker(b) FALSE
#define mnVSNetAutomatchAMIceBindTick(s, sc) FALSE
#define mnVSNetAutomatchAMIceLocalLan() NULL
#define mnVSNetAutomatchAMIceShouldQueueTurnEndpoint() FALSE
#define mnVSNetAutomatchAMIceBeginConnect(mr) FALSE
#define mnVSNetAutomatchAMIceConnectTick() 0
#define mnVSNetAutomatchAMIceConnectFailureReason() "ICE connection failed"
#define mnVSNetAutomatchAMIceLogConnectAbortDiag(r, e) ((void)(r), (void)(e))
#define mnVSNetAutomatchAMIceShouldIgnorePollError(ev) FALSE
#define mnVSNetAutomatchAMIceBootstrapPeer(mr, b) FALSE
#define mnVSNetAutomatchAMIceConnectTricklePollInterval() 0U
#define mnVSNetAutomatchAMIceConnectTrickleMayEnqueue() FALSE
#define mnVSNetAutomatchAMIceConnectTrickleNoteEnqueued() ((void)0)
#define mnVSNetAutomatchAMIceNotifyPeerAbort(mr) ((void)0)
#define mnVSNetAutomatchAMIceHttpsLockBeforeRequest() ((void)0)
#define mnVSNetAutomatchAMIceHttpsUnlockAfterRequest() ((void)0)
#define mnVSNetAutomatchAMIceConnectTickEnter() TRUE
#define mnVSNetAutomatchAMIceConnectTickLeave() ((void)0)
#define mnVSNetAutomatchAMIceWorkerMatchPollBlocked(trickle_only) FALSE
#define mnVSNetAutomatchAMQueuePollMayEnqueue(t) TRUE
#define mnVSNetAutomatchAMQueuePollNoteEnqueued() ((void)0)
#define mnVSNetAutomatchAMQueuePollNoteStillQueued() ((void)0)
#define mnVSNetAutomatchAMQueuePollNoteHttp0Cooldown() ((void)0)
#define mnVSNetAutomatchAMQueuePollReset() ((void)0)

#endif

#endif /* MM_ICE_AUTOMATCH_H */
