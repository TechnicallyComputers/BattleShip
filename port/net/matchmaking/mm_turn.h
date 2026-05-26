#ifndef MM_TURN_H
#define MM_TURN_H

#include <PR/ultratypes.h>
#include <ssb_types.h>

#if defined(PORT) && defined(SSB64_NETMENU)

/* Coturn / RFC 5766 relay on the automatch UDP socket (same fd as STUN/game). */
typedef struct MmTurnRelayResult
{
	char relay_endpoint[128];
	sb32 ok;
} MmTurnRelayResult;

extern sb32 mmTurnIsClientEnabled(void);
/* Non-zero SSB64_NETPLAY_TURN_REQUIRED: queue/match fail without a relay (or peer_turn at bootstrap). */
extern sb32 mmTurnIsRequired(void);
extern void mmTurnDrainUdpSocket(s32 udp_fd);
extern sb32 mmTurnAllocateIpv4Relay(s32 udp_fd, MmTurnRelayResult *out);

/*
 * After allocate: allow CreatePermission + ChannelBind for peer, then wrap game UDP in ChannelData.
 * `peer_hostport` is opponent reflexive or relay `ip:port` (permission uses peer IPv4).
 */
extern sb32 mmTurnBeginRelayToPeer(s32 udp_fd, const char *peer_hostport);
extern void mmTurnEndRelaySession(void);

extern sb32 mmTurnRelaySend(s32 udp_fd, const u8 *payload, u32 len);
/* If datagram is TURN ChannelData from the configured server, strips header and returns TRUE. */
extern sb32 mmTurnRelayUnwrap(const u8 *payload, s32 len, const void *from_addr, u8 *out, u32 out_cap, s32 *out_len);

extern sb32 mmTurnRelaySessionActive(void);
extern const char *mmTurnGetRelayEndpoint(void);

#endif

#endif /* MM_TURN_H */
