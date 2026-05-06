#ifndef _MM_STUN_H_
#define _MM_STUN_H_

#if defined(PORT) && defined(SSB64_NETMENU) && !defined(_WIN32)

#include <PR/ultratypes.h>
#include <ssb_types.h>

/* RFC 5389 STUN binding — query XOR-MAPPED-ADDRESS via an already-bound UDP sock.
 * Writes "host:port" IPv4 reflexive endpoint into buf (ASCII, NUL-terminated). */
extern sb32 mmStunGetReflexiveIpv4Endpoint(s32 udp_fd, char *buf, u32 bufsize);

#else

#include <PR/ultratypes.h>
#include <ssb_types.h>

#define mmStunGetReflexiveIpv4Endpoint(fd, buf, bufsize) FALSE

#endif

#endif /* _MM_STUN_H_ */
