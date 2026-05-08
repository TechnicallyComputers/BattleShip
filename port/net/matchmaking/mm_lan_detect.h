#ifndef _MM_LAN_DETECT_H_
#define _MM_LAN_DETECT_H_

#if defined(PORT) && defined(SSB64_NETMENU) && !defined(_WIN32)

#include <PR/ultratypes.h>
#include <ssb_types.h>

/* Derive "host:port" for POST /v1/queue lan_endpoint when SSB64_MATCHMAKING_LAN_ENDPOINT is unset.
 * Port: getsockname(udp_fd); on failure parses bind_spec "host:port".
 * Address: first RFC1918 IPv4 on an interface; optional SSB64_MATCHMAKING_LAN_INTERFACE=ifname. */
extern sb32 mmLanDetectEndpoint(char *buf, u32 bufsize, s32 udp_fd, const char *bind_spec_opt);

#else

#include <PR/ultratypes.h>
#include <ssb_types.h>

#define mmLanDetectEndpoint(buf, bufsize, udp_fd, bind_spec_opt) FALSE

#endif

#endif /* _MM_LAN_DETECT_H_ */
