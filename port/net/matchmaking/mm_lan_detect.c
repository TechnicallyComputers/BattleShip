#include <string.h>

#include "mm_lan_detect.h"

#if defined(PORT) && defined(SSB64_NETMENU) && !defined(_WIN32)

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

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

sb32 mmLanDetectEndpoint(char *buf, u32 bufsize, s32 udp_fd, const char *bind_spec_opt)
{
	struct ifaddrs *ifa_head;
	struct ifaddrs *ifa;
	const char *want_if;
	int best_score;
	char best_ip[INET_ADDRSTRLEN];
	u16 port;

	best_ip[0] = '\0';
	best_score = -1000;
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
			snprintf(best_ip, sizeof(best_ip), "%s", ip_str);
		}
		else if ((sc == best_score) && (best_ip[0] != '\0') && (strcmp(ip_str, best_ip) < 0))
		{
			snprintf(best_ip, sizeof(best_ip), "%s", ip_str);
		}
	}

	freeifaddrs(ifa_head);

	if (best_ip[0] == '\0')
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

#endif /* PORT && SSB64_NETMENU && !_WIN32 */
