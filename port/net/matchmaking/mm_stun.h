#ifndef MM_STUN_H
#define MM_STUN_H

#include <PR/ultratypes.h>
#include <ssb_types.h>

#if defined(PORT) && defined(SSB64_NETMENU)

typedef enum MmStunNatHint
{
	MM_STUN_NAT_UNKNOWN = 0,
	MM_STUN_NAT_LIKELY_CONE,       /* same mapped port across STUN servers */
	MM_STUN_NAT_SYMMETRIC_SUSPECTED /* mapped port differs between servers */
} MmStunNatHint;

typedef struct MmStunProbeResult
{
	char endpoint[128];
	MmStunNatHint nat_hint;
	sb32 ok;
} MmStunProbeResult;

/* STUN Binding on the game UDP socket (IPv4). Uses configured server list + retries. */
extern sb32 mmStunGetReflexiveIpv4Endpoint(s32 udp_fd, char *buf, u32 bufsize);

/* Full probe with NAT hint (dual-server port compare when 2+ servers resolve). */
extern sb32 mmStunProbeIpv4Endpoint(s32 udp_fd, MmStunProbeResult *out);

#endif

#endif /* MM_STUN_H */
