#include <string.h>

#include "mm_lan_detect.h"

#if defined(PORT) && defined(SSB64_NETMENU)

#include <stdio.h>
#include <stdlib.h>

/* Platform-specific interface enumeration; shared RFC1918 scoring below. */
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <wchar.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#ifdef PORT
extern void port_log(const char *fmt, ...);
#endif

static sb32 addr_is_rfc1918(struct in_addr *a)
{
	u32 h = (u32)ntohl(a->s_addr);

	if ((h & 0xff000000U) == 0x0a000000U)
	{
		return TRUE;
	}
	if ((h & 0xfff00000U) == 0xac100000U)
	{
		return TRUE;
	}
	if ((h & 0xffff0000U) == 0xc0a80000U)
	{
		return TRUE;
	}
	return FALSE;
}

static int ifname_score(const char *name)
{
	if (strncmp(name, "lo", (size_t)2) == 0)
	{
		return -100;
	}
	if (strncmp(name, "docker", (size_t)6) == 0)
	{
		return -50;
	}
	if (strncmp(name, "br-", (size_t)3) == 0)
	{
		return -40;
	}
	if (strncmp(name, "veth", (size_t)4) == 0)
	{
		return -40;
	}
	if (strncmp(name, "virbr", (size_t)5) == 0)
	{
		return -40;
	}
	if (strncmp(name, "wlan", (size_t)4) == 0)
	{
		return 30;
	}
	if (strncmp(name, "wl", (size_t)2) == 0)
	{
		return 30;
	}
	if (strncmp(name, "en", (size_t)2) == 0)
	{
		return 20;
	}
	if (strncmp(name, "eth", (size_t)3) == 0)
	{
		return 20;
	}
	return 0;
}

static sb32 parse_bind_port(const char *spec, u16 *out_port)
{
	const char *colon = strrchr(spec, ':');

	if ((colon == NULL) || (colon[1] == '\0'))
	{
		return FALSE;
	}
	if (sscanf(colon + 1, "%hu", out_port) != 1)
	{
		return FALSE;
	}
	return TRUE;
}

static sb32 parse_hostport_ipv4(const char *hostport, struct in_addr *out_addr)
{
	const char *colon;
	size_t host_len;
	char host[INET_ADDRSTRLEN];

	if ((hostport == NULL) || (out_addr == NULL))
	{
		return FALSE;
	}
	colon = strrchr(hostport, ':');
	if ((colon == NULL) || (colon == hostport) || (colon[1] == '\0'))
	{
		return FALSE;
	}
	host_len = (size_t)(colon - hostport);
	if ((host_len == 0U) || (host_len >= sizeof(host)))
	{
		return FALSE;
	}
	memcpy(host, hostport, host_len);
	host[host_len] = '\0';
	if (inet_pton(AF_INET, host, out_addr) != 1)
	{
		return FALSE;
	}
	return TRUE;
}

static u32 ipv4_cidr_prefix_to_mask_host(u8 prefix_len)
{
	u32 mask;

	if (prefix_len == 0U)
	{
		return 0U;
	}
	if (prefix_len >= 32U)
	{
		return 0xFFFFFFFFU;
	}
	mask = 0xFFFFFFFFU;
	mask <<= (32U - (u32)prefix_len);
	return mask;
}

static sb32 ipv4_same_subnet_host_order(u32 peer_be, u32 if_be, u32 mask_host)
{
	u32 peer_h = ntohl(peer_be);
	u32 if_h = ntohl(if_be);

	if (mask_host == 0U)
	{
		return FALSE;
	}
	return ((peer_h & mask_host) == (if_h & mask_host)) ? TRUE : FALSE;
}

#ifdef _WIN32
static sb32 mmLanIpv4OnLocalSubnet(const struct in_addr *peer)
{
	ULONG buf_len = 15000;
	PIP_ADAPTER_ADDRESSES addrs;
	ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
	PIP_ADAPTER_ADDRESSES cur;
	sb32 ok;

	if (peer == NULL)
	{
		return FALSE;
	}
	for (;;)
	{
		addrs = (PIP_ADAPTER_ADDRESSES)malloc(buf_len);
		if (addrs == NULL)
		{
			return FALSE;
		}
		if (GetAdaptersAddresses(AF_INET, flags, NULL, addrs, &buf_len) == ERROR_SUCCESS)
		{
			break;
		}
		free(addrs);
		if (GetLastError() != ERROR_BUFFER_OVERFLOW)
		{
			return FALSE;
		}
	}

	ok = FALSE;
	for (cur = addrs; cur != NULL; cur = cur->Next)
	{
		PIP_ADAPTER_UNICAST_ADDRESS ua;
		struct sockaddr_in *sin;

		if (cur->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
		{
			continue;
		}
		for (ua = cur->FirstUnicastAddress; ua != NULL; ua = ua->Next)
		{
			u32 mask_host;
			u8 prefix;

			if ((ua->Address.lpSockaddr == NULL) || (ua->Address.lpSockaddr->sa_family != AF_INET))
			{
				continue;
			}
			sin = (struct sockaddr_in *)ua->Address.lpSockaddr;
			if (addr_is_rfc1918(&sin->sin_addr) == FALSE)
			{
				continue;
			}
			if (peer->s_addr == sin->sin_addr.s_addr)
			{
				ok = TRUE;
				break;
			}
			prefix = ua->OnLinkPrefixLength;
			mask_host = ipv4_cidr_prefix_to_mask_host(prefix);
			if (ipv4_same_subnet_host_order(peer->s_addr, sin->sin_addr.s_addr, mask_host) != FALSE)
			{
				ok = TRUE;
				break;
			}
		}
		if (ok != FALSE)
		{
			break;
		}
	}
	free(addrs);
	return ok;
}
#else
static sb32 mmLanIfaUsableForPeerReach(const char *ifname)
{
	if (ifname == NULL)
	{
		return FALSE;
	}
	return (ifname_score(ifname) >= 0) ? TRUE : FALSE;
}

static sb32 mmLanIpv4OnLocalSubnet(const struct in_addr *peer)
{
	struct ifaddrs *ifa_head;
	struct ifaddrs *ifa;
	sb32 ok;

	if (peer == NULL)
	{
		return FALSE;
	}
	if (getifaddrs(&ifa_head) != 0)
	{
		return FALSE;
	}
	ok = FALSE;
	for (ifa = ifa_head; ifa != NULL; ifa = ifa->ifa_next)
	{
		struct sockaddr_in *addr;
		struct sockaddr_in *mask;

		if ((ifa->ifa_addr == NULL) || (ifa->ifa_netmask == NULL) || (ifa->ifa_name == NULL))
		{
			continue;
		}
		if (mmLanIfaUsableForPeerReach(ifa->ifa_name) == FALSE)
		{
			continue;
		}
		if (ifa->ifa_addr->sa_family != AF_INET)
		{
			continue;
		}
		addr = (struct sockaddr_in *)ifa->ifa_addr;
		mask = (struct sockaddr_in *)ifa->ifa_netmask;
		if (addr_is_rfc1918(&addr->sin_addr) == FALSE)
		{
			continue;
		}
		if (peer->s_addr == addr->sin_addr.s_addr)
		{
			ok = TRUE;
			break;
		}
		if (((peer->s_addr & mask->sin_addr.s_addr) == (addr->sin_addr.s_addr & mask->sin_addr.s_addr)) &&
		    (mask->sin_addr.s_addr != 0))
		{
			ok = TRUE;
			break;
		}
	}
	freeifaddrs(ifa_head);
	return ok;
}
#endif

sb32 mmLanPeerHostportIsOnLocalLan(const char *peer_hostport)
{
	struct in_addr peer;

	if ((peer_hostport == NULL) || (peer_hostport[0] == '\0'))
	{
		return FALSE;
	}
	if (parse_hostport_ipv4(peer_hostport, &peer) == FALSE)
	{
		return FALSE;
	}
	if (addr_is_rfc1918(&peer) == FALSE)
	{
		return FALSE;
	}
	return mmLanIpv4OnLocalSubnet(&peer);
}

sb32 mmHostportWanIpv4Equal(const char *local_wan_hostport, const char *peer_wan_hostport)
{
	struct in_addr local_addr;
	struct in_addr peer_addr;

	if ((local_wan_hostport == NULL) || (local_wan_hostport[0] == '\0') || (peer_wan_hostport == NULL) ||
	    (peer_wan_hostport[0] == '\0'))
	{
		return FALSE;
	}
	if ((parse_hostport_ipv4(local_wan_hostport, &local_addr) == FALSE) ||
	    (parse_hostport_ipv4(peer_wan_hostport, &peer_addr) == FALSE))
	{
		return FALSE;
	}
	return (local_addr.s_addr == peer_addr.s_addr) ? TRUE : FALSE;
}

#ifdef _WIN32
static sb32 udp_port_from_fd(s32 udp_fd, u16 *out_port)
{
	struct sockaddr_storage ss;
	int len = (int)sizeof(ss);
	SOCKET sock = (SOCKET)(intptr_t)udp_fd;

	if (udp_fd < 0)
	{
		return FALSE;
	}
	if (getsockname(sock, (struct sockaddr *)&ss, &len) != 0)
	{
		return FALSE;
	}
	if (ss.ss_family != AF_INET)
	{
		return FALSE;
	}
	*out_port = ntohs(((struct sockaddr_in *)&ss)->sin_port);
	return TRUE;
}

static sb32 mmLanDetectBestRfc1918(u16 port, const char *want_if, char *best_ip, size_t best_ip_cap)
{
	(void)port;
	ULONG buf_len = 15000;
	PIP_ADAPTER_ADDRESSES addrs;
	ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
	PIP_ADAPTER_ADDRESSES cur;
	int best_score;
	sb32 ok;

	best_ip[0] = '\0';
	best_score = -1000;
	ok = FALSE;

	for (;;)
	{
		addrs = (PIP_ADAPTER_ADDRESSES)malloc(buf_len);
		if (addrs == NULL)
		{
			return FALSE;
		}
		if (GetAdaptersAddresses(AF_INET, flags, NULL, addrs, &buf_len) == ERROR_SUCCESS)
		{
			break;
		}
		free(addrs);
		if (GetLastError() != ERROR_BUFFER_OVERFLOW)
		{
#ifdef PORT
			port_log("SSB64 Automatch LAN detect: GetAdaptersAddresses failed\n");
#endif
			return FALSE;
		}
	}

	for (cur = addrs; cur != NULL; cur = cur->Next)
	{
		PIP_ADAPTER_UNICAST_ADDRESS ua;
		int sc;
		char ip_str[INET_ADDRSTRLEN];
		struct sockaddr_in *sin;

		if ((cur->IfType == IF_TYPE_SOFTWARE_LOOPBACK) || (cur->FriendlyName == NULL))
		{
			continue;
		}
		if ((want_if != NULL) && (want_if[0] != '\0'))
		{
			char ifname[64];
			int nw;

			nw = WideCharToMultiByte(CP_UTF8, 0, cur->FriendlyName, -1, ifname, (int)sizeof(ifname), NULL, NULL);
			if ((nw <= 0) || (strcmp(ifname, want_if) != 0))
			{
				continue;
			}
		}

		for (ua = cur->FirstUnicastAddress; ua != NULL; ua = ua->Next)
		{
			if ((ua->Address.lpSockaddr == NULL) || (ua->Address.lpSockaddr->sa_family != AF_INET))
			{
				continue;
			}
			sin = (struct sockaddr_in *)ua->Address.lpSockaddr;
			if (addr_is_rfc1918(&sin->sin_addr) == FALSE)
			{
				continue;
			}
			if (inet_ntop(AF_INET, &sin->sin_addr, ip_str, sizeof(ip_str)) == NULL)
			{
				continue;
			}
			sc = ifname_score("eth");
			if (sc > best_score)
			{
				best_score = sc;
				snprintf(best_ip, best_ip_cap, "%s", ip_str);
				ok = TRUE;
			}
			else if ((sc == best_score) && (best_ip[0] != '\0') && (strcmp(ip_str, best_ip) < 0))
			{
				snprintf(best_ip, best_ip_cap, "%s", ip_str);
				ok = TRUE;
			}
		}
	}

	free(addrs);
	return ok;
}
#else
static sb32 udp_port_from_fd(s32 udp_fd, u16 *out_port)
{
	struct sockaddr_storage ss;
	socklen_t len = (socklen_t)sizeof(ss);

	if (udp_fd < 0)
	{
		return FALSE;
	}
	if (getsockname(udp_fd, (struct sockaddr *)&ss, &len) != 0)
	{
		return FALSE;
	}
	if (ss.ss_family != AF_INET)
	{
		return FALSE;
	}
	*out_port = ntohs(((struct sockaddr_in *)&ss)->sin_port);
	return TRUE;
}

static sb32 mmLanDetectBestRfc1918(u16 port, const char *want_if, char *best_ip, size_t best_ip_cap)
{
	(void)port;
	struct ifaddrs *ifa_head;
	struct ifaddrs *ifa;
	int best_score;
	sb32 ok;

	best_ip[0] = '\0';
	best_score = -1000;
	ok = FALSE;

	if (getifaddrs(&ifa_head) != 0)
	{
#ifdef PORT
		port_log("SSB64 Automatch LAN detect: getifaddrs failed\n");
#endif
		return FALSE;
	}

	for (ifa = ifa_head; ifa != NULL; ifa = ifa->ifa_next)
	{
		struct sockaddr_in *sin;
		int sc;
		char ip_str[INET_ADDRSTRLEN];

		if ((ifa->ifa_addr == NULL) || (ifa->ifa_name == NULL))
		{
			continue;
		}
		if (ifa->ifa_addr->sa_family != AF_INET)
		{
			continue;
		}
		sin = (struct sockaddr_in *)ifa->ifa_addr;
		if ((sin->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) || (addr_is_rfc1918(&sin->sin_addr) == FALSE))
		{
			continue;
		}
		if ((want_if != NULL) && (want_if[0] != '\0') && (strcmp(ifa->ifa_name, want_if) != 0))
		{
			continue;
		}
		if (inet_ntop(AF_INET, &sin->sin_addr, ip_str, sizeof(ip_str)) == NULL)
		{
			continue;
		}
		sc = ifname_score(ifa->ifa_name);
		if (sc > best_score)
		{
			best_score = sc;
			snprintf(best_ip, best_ip_cap, "%s", ip_str);
			ok = TRUE;
		}
		else if ((sc == best_score) && (best_ip[0] != '\0') && (strcmp(ip_str, best_ip) < 0))
		{
			snprintf(best_ip, best_ip_cap, "%s", ip_str);
			ok = TRUE;
		}
	}

	freeifaddrs(ifa_head);
	return ok;
}
#endif

sb32 mmLanDetectEndpoint(char *buf, u32 bufsize, s32 udp_fd, const char *bind_spec_opt)
{
	const char *want_if;
	char best_ip[INET_ADDRSTRLEN];
	u16 port;

	want_if = getenv("SSB64_MATCHMAKING_LAN_INTERFACE");

	if (udp_port_from_fd(udp_fd, &port) == FALSE)
	{
		if ((bind_spec_opt == NULL) || (bind_spec_opt[0] == '\0') ||
		    (parse_bind_port(bind_spec_opt, &port) == FALSE))
		{
#ifdef PORT
			port_log("SSB64 Automatch LAN detect: no UDP port (getsockname/bind parse failed)\n");
#endif
			return FALSE;
		}
	}

	if (mmLanDetectBestRfc1918(port, want_if, best_ip, sizeof(best_ip)) == FALSE)
	{
#ifdef PORT
		port_log("SSB64 Automatch LAN detect: no RFC1918 IPv4 candidate (override with SSB64_MATCHMAKING_LAN_ENDPOINT)\n");
#endif
		return FALSE;
	}

	if (((int)snprintf(buf, (size_t)bufsize, "%s:%u", best_ip, (unsigned)port)) >= (int)bufsize)
	{
#ifdef PORT
		port_log("SSB64 Automatch LAN detect: buffer too small\n");
#endif
		return FALSE;
	}

#ifdef PORT
	port_log("SSB64 Automatch LAN detect: using %s (env overrides: SSB64_MATCHMAKING_LAN_ENDPOINT, SSB64_MATCHMAKING_LAN_INTERFACE)\n",
	         buf);
#endif
	return TRUE;
}

#endif /* PORT && SSB64_NETMENU */
