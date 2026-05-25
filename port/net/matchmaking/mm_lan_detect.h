#ifndef MM_LAN_DETECT_H
#define MM_LAN_DETECT_H

#include <PR/ultratypes.h>
#include <ssb_types.h>

#if defined(PORT) && defined(SSB64_NETMENU)

/* Linux: getifaddrs. Windows: GetAdaptersAddresses (CMake links iphlpapi). */
extern sb32 mmLanDetectEndpoint(char *buf, u32 bufsize, s32 udp_fd, const char *bind_spec_opt);

/* TRUE when peer host:port is RFC1918 and on a subnet of a local non-virtual IPv4 interface. */
extern sb32 mmLanPeerHostportIsOnLocalLan(const char *peer_hostport);

/*
 * TRUE when both host:port strings parse as IPv4 and addresses match (STUN reflexive WAN IPs).
 * Automatch uses this to detect same public IP; peer_lan is tried first only when
 * mmLanPeerHostportIsOnLocalLan(peer_lan) is also TRUE (same subnet). Otherwise reflexive
 * host:port is attempted (same-WAN CGNAT / hairpin cases).
 */
extern sb32 mmHostportWanIpv4Equal(const char *local_wan_hostport, const char *peer_wan_hostport);

#endif

#endif /* MM_LAN_DETECT_H */
