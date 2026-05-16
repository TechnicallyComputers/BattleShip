#ifndef MM_STUN_H
#define MM_STUN_H

#include <PR/ultratypes.h>
#include <ssb_types.h>

#if defined(PORT) && defined(SSB64_NETMENU)

/* STUN Binding on the game UDP socket; Winsock on Windows, POSIX on Linux. */
extern sb32 mmStunGetReflexiveIpv4Endpoint(s32 udp_fd, char *buf, u32 bufsize);

#endif

#endif /* MM_STUN_H */
