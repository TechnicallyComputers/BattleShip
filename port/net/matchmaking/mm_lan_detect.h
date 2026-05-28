#ifndef MM_LAN_DETECT_H
#define MM_LAN_DETECT_H

#include <PR/ultratypes.h>
#include <ssb_types.h>

#if defined(PORT) && defined(SSB64_NETMENU)

/* Linux: getifaddrs. Windows: GetAdaptersAddresses (CMake links iphlpapi). */
extern sb32 mmLanDetectEndpoint(char *buf, u32 bufsize, s32 udp_fd, const char *bind_spec_opt);

/* TRUE when peer host:port is RFC1918 and on a subnet of a local non-virtual IPv4 interface. */
extern sb32 mmLanPeerHostportIsOnLocalLan(const char *peer_hostport);

/* TRUE when peer is RFC1918 and on the same subnet as local_lan_hostport's interface (automatch). */
extern sb32 mmLanPeerSharesLocalLanSubnet(const char *peer_hostport, const char *local_lan_hostport);

/*
 * TRUE when both host:port strings parse as IPv4 and addresses match (STUN reflexive WAN IPs).
 * Automatch uses this to detect same public IP; peer_lan is tried first only when
 * mmLanPeerSharesLocalLanSubnet(peer_lan, local_lan) is TRUE. Otherwise reflexive host:port
 * is attempted (same-WAN / different-site LAN subnets).
 */
extern sb32 mmHostportWanIpv4Equal(const char *local_wan_hostport, const char *peer_wan_hostport);

/* TRUE when ipv4_host parses as RFC1918 (10/8, 172.16/12, 192.168/16). */
extern sb32 mmLanIpv4StringIsRfc1918(const char *ipv4_host);

/* TRUE when any local non-virtual RFC1918 IPv4 interface exists (ignores port). */
extern sb32 mmLanDetectHasLocalRfc1918(void);

/*
 * Pick best host:port from candidate list for queue lan_endpoint.
 * Prefers RFC1918; optional prefer_bind_ip (from BIND/LAN_ENDPOINT) breaks ties.
 */
extern sb32 mmLanPickBestHostportFromCandidates(const char *const *hostports, u32 count, char *out, u32 out_cap,
                                                const char *prefer_bind_ip);

/*
 * Kernel route source IPv4 for reaching peer (UDP connect + getsockname). Empty on failure.
 */
extern sb32 mmLanRouteSourceIpv4ForPeer(const char *peer_hostport, char *out, u32 out_cap);

/*
 * Peer-directed host:port pick among ICE/local candidates.
 * Scores route source IP, shared subnet with peer, then RFC1918 / prefer_bind_ip.
 */
extern sb32 mmLanPickHostportForPeer(const char *peer_hostport, const char *const *hostports, u32 count, char *out,
                                     u32 out_cap, const char *prefer_bind_ip);

#endif

#endif /* MM_LAN_DETECT_H */
