#include <string.h>

#include "mm_stun.h"

#if defined(PORT) && defined(SSB64_NETMENU)

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <wchar.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#if defined(__linux__) && !defined(__ANDROID__)
#include <sys/random.h>
#elif defined(__APPLE__)
void arc4random_buf(void *buf, size_t nbytes);
#elif defined(__ANDROID__)
#include <fcntl.h>
#endif
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifdef PORT
extern void port_log(const char *fmt, ...);
#endif

#define STUN_MAGIC 0x2112A442U
#define MM_STUN_MAX_SERVERS 10
#define MM_STUN_BINDING_PASSES 3 /* full rotations through the server list */
#define MM_STUN_RECV_US 400000

typedef struct MmStunServerEntry
{
	struct sockaddr_in addr;
	char host_label[64];
} MmStunServerEntry;

static const struct
{
	const char *host;
	u16 port;
} kStunServerDefaults[] = {
    { "stun.l.google.com", 19302 },
    { "stun1.l.google.com", 19302 },
    { "stun2.l.google.com", 19302 },
    { "stun3.l.google.com", 19302 },
    { "stun4.l.google.com", 19302 },
};

#if defined(__ANDROID__)
static sb32 mmStunTryDevUrandom(u8 *buf, size_t len)
{
	int fd;
	ssize_t n;

	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
	{
		return FALSE;
	}
	n = read(fd, buf, len);
	(void)close(fd);
	return (n == (ssize_t)len) ? TRUE : FALSE;
}
#endif

static void mmStunFillTxId(u8 *pkt)
{
	s32 i;

#ifdef _WIN32
	for (i = 0; i < 12; i++)
	{
		pkt[i] = (u8)((u32)GetTickCount() ^ (u32)GetCurrentProcessId() ^ ((u32)i * 7919U));
	}
#else
#if defined(__APPLE__)
	arc4random_buf(pkt, 12U);
	return;
#elif defined(__linux__) && !defined(__ANDROID__)
	if (getrandom(pkt, 12U, 0) == 12)
	{
		return;
	}
#elif defined(__ANDROID__)
	if (mmStunTryDevUrandom(pkt, 12U))
	{
		return;
	}
#endif
	for (i = 0; i < 12; i++)
	{
		pkt[i] = (u8)((u32)getpid() ^ ((u32)i * 7919U));
	}
#endif
}

#ifdef _WIN32
typedef int mm_sock_len_t;
#else
typedef ssize_t mm_sock_len_t;
#endif

static void mmStunParseXorMapped(const u8 *pkt, mm_sock_len_t total_len, struct sockaddr_in *out)
{
	u16 msg_len = (u16)((pkt[2] << 8) | pkt[3]);
	u32 pos;

	memset(out, 0, sizeof(*out));
	if ((total_len < 20) || (msg_len > 548) || ((mm_sock_len_t)(20 + msg_len) > total_len))
	{
		return;
	}
	pos = 20;
	while (pos + 4U <= (u32)(20 + msg_len))
	{
		u16 attr_type = (u16)((pkt[pos] << 8) | pkt[pos + 1]);
		u16 attr_len = (u16)((pkt[pos + 2] << 8) | pkt[pos + 3]);

		pos += 4;
		if (pos + attr_len > (u32)(20 + msg_len))
		{
			break;
		}
		if ((attr_type == 0x0020) && (attr_len >= 8))
		{
			if ((pkt[pos] == 0x00) && (pkt[pos + 1] == 0x01))
			{
				u16 xport = (u16)((pkt[pos + 2] << 8) | pkt[pos + 3]);
				u32 xaddr =
				    ((u32)pkt[pos + 4] << 24) | ((u32)pkt[pos + 5] << 16) | ((u32)pkt[pos + 6] << 8) | pkt[pos + 7];
				u16 port_host = (u16)(xport ^ (u16)(STUN_MAGIC >> 16));
				u32 addr_host = xaddr ^ STUN_MAGIC;

				out->sin_family = AF_INET;
				out->sin_port = htons(port_host);
				out->sin_addr.s_addr = htonl(addr_host);
				return;
			}
		}
		pos += attr_len + ((4U - (attr_len % 4U)) % 4U);
	}
}

static sb32 mmStunResolveHost(const char *host, u16 port, MmStunServerEntry *out)
{
	struct addrinfo hints;
	struct addrinfo *res;
	struct addrinfo *it;
	char port_buf[16];
	sb32 ok;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	snprintf(port_buf, sizeof(port_buf), "%u", (unsigned int)port);
	res = NULL;
	if (getaddrinfo(host, port_buf, &hints, &res) != 0)
	{
		return FALSE;
	}
	ok = FALSE;
	for (it = res; it != NULL; it = it->ai_next)
	{
		if ((it->ai_family == AF_INET) && (it->ai_addrlen >= (socklen_t)sizeof(struct sockaddr_in)))
		{
			memcpy(&out->addr, it->ai_addr, sizeof(out->addr));
			snprintf(out->host_label, sizeof(out->host_label), "%s", host);
			ok = TRUE;
			break;
		}
	}
	freeaddrinfo(res);
	return ok;
}

static void mmStunTrimToken(char *s)
{
	size_t len;
	size_t lead;
	size_t i;
	size_t end;

	if ((s == NULL) || (s[0] == '\0'))
	{
		return;
	}
	lead = 0U;
	while (s[lead] == ' ' || s[lead] == '\t')
	{
		lead++;
	}
	if (lead > 0U)
	{
		len = strlen(s + lead);
		for (i = 0U; i <= len; i++)
		{
			s[i] = s[lead + i];
		}
	}
	len = strlen(s);
	end = len;
	while (end > 0U &&
	       (s[end - 1U] == ' ' || s[end - 1U] == '\t' || s[end - 1U] == ',' || s[end - 1U] == '\r' ||
	        s[end - 1U] == '\n'))
	{
		end--;
	}
	s[end] = '\0';
}

static u32 mmStunLoadServers(MmStunServerEntry *servers, u32 max_servers)
{
	const char *env;
	char buf[512];
	u32 count;
	u32 i;

	count = 0U;
	env = getenv("SSB64_MATCHMAKING_STUN_SERVERS");
	if ((env != NULL) && (env[0] != '\0'))
	{
		snprintf(buf, sizeof(buf), "%s", env);
		for (i = 0U; buf[i] != '\0' && count < max_servers; i++)
		{
			char token[160];
			char *colon;
			char host[128];
			u32 start;
			u32 end;
			u32 port;
			long port_l;

			while (buf[i] == ',' || buf[i] == ' ' || buf[i] == '\t')
			{
				i++;
			}
			if (buf[i] == '\0')
			{
				break;
			}
			start = i;
			while (buf[i] != '\0' && buf[i] != ',')
			{
				i++;
			}
			end = i;
			if (end <= start || (end - start) >= sizeof(token))
			{
				continue;
			}
			memcpy(token, buf + start, end - start);
			token[end - start] = '\0';
			mmStunTrimToken(token);
			if (token[0] == '\0')
			{
				continue;
			}
			snprintf(host, sizeof(host), "%s", token);
			colon = strrchr(host, ':');
			port = 19302U;
			if (colon != NULL)
			{
				*colon = '\0';
				port_l = strtol(colon + 1, NULL, 10);
				if (port_l > 0L && port_l < 65536L)
				{
					port = (u32)port_l;
				}
			}
			if (mmStunResolveHost(host, (u16)port, &servers[count]) != FALSE)
			{
				count++;
			}
		}
	}
	if (count == 0U)
	{
		u32 i;

		for (i = 0; i < sizeof(kStunServerDefaults) / sizeof(kStunServerDefaults[0]) && count < max_servers; i++)
		{
			if (mmStunResolveHost(kStunServerDefaults[i].host, kStunServerDefaults[i].port, &servers[count]) != FALSE)
			{
				count++;
			}
		}
	}
	return count;
}

static sb32 mmStunBindingTry(s32 sock, const MmStunServerEntry *server, struct sockaddr_in *mapped_out)
{
	u8 pkt[512];
	u8 tx_id[12];
	mm_sock_len_t n;
	fd_set rfds;
	struct timeval tv;

	pkt[0] = 0x00;
	pkt[1] = 0x01;
	pkt[2] = 0x00;
	pkt[3] = 0x00;
	pkt[4] = (STUN_MAGIC >> 24) & 0xFF;
	pkt[5] = (STUN_MAGIC >> 16) & 0xFF;
	pkt[6] = (STUN_MAGIC >> 8) & 0xFF;
	pkt[7] = STUN_MAGIC & 0xFF;
	mmStunFillTxId(pkt + 8);
	memcpy(tx_id, pkt + 8, 12);
#ifdef _WIN32
	if (sendto((SOCKET)(intptr_t)sock, (const char *)pkt, 20, 0, (struct sockaddr *)&server->addr,
	           sizeof(server->addr)) != 20)
#else
	if (sendto(sock, pkt, 20, 0, (struct sockaddr *)&server->addr, sizeof(server->addr)) != 20)
#endif
	{
		return FALSE;
	}
	while (TRUE)
	{
		tv.tv_sec = 0;
		tv.tv_usec = MM_STUN_RECV_US;
		FD_ZERO(&rfds);
		FD_SET(sock, &rfds);
#ifdef _WIN32
		n = select(0, &rfds, NULL, NULL, &tv);
#else
		n = select(sock + 1, &rfds, NULL, NULL, &tv);
#endif
		if ((n <= 0) || !FD_ISSET(sock, &rfds))
		{
			break;
		}
		memset(pkt, 0, sizeof(pkt));
#ifdef _WIN32
		n = recvfrom((SOCKET)(intptr_t)sock, (char *)pkt, sizeof(pkt), 0, NULL, NULL);
#else
		n = recvfrom(sock, pkt, sizeof(pkt), MSG_DONTWAIT, NULL, NULL);
#endif
		if ((n < 24) || (n >= (mm_sock_len_t)sizeof(pkt)))
		{
			continue;
		}
		if ((pkt[0] != 0x01) || (pkt[1] != 0x01))
		{
			continue;
		}
		{
			u32 mc = ((u32)pkt[4] << 24) | ((u32)pkt[5] << 16) | ((u32)pkt[6] << 8) | pkt[7];

			if ((mc != STUN_MAGIC) || (memcmp(pkt + 8, tx_id, 12) != 0))
			{
				continue;
			}
		}
		mmStunParseXorMapped(pkt, n, mapped_out);
		if (mapped_out->sin_family != 0)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static void mmStunMappedToEndpoint(const struct sockaddr_in *mapped, char *buf, u32 bufsize)
{
	char ip_buf[INET_ADDRSTRLEN];

	if (inet_ntop(AF_INET, &mapped->sin_addr, ip_buf, sizeof(ip_buf)) == NULL)
	{
		buf[0] = '\0';
		return;
	}
	snprintf(buf, bufsize, "%s:%u", ip_buf, (unsigned int)ntohs(mapped->sin_port));
}

sb32 mmStunProbeIpv4Endpoint(s32 udp_fd, MmStunProbeResult *out)
{
	MmStunServerEntry servers[MM_STUN_MAX_SERVERS];
	struct sockaddr_in mapped_a;
	struct sockaddr_in mapped_b;
	u32 server_count;
	u32 i;
	sb32 first_ok;

	if ((udp_fd < 0) || (out == NULL))
	{
		return FALSE;
	}
	memset(out, 0, sizeof(*out));
	out->nat_hint = MM_STUN_NAT_UNKNOWN;
	server_count = mmStunLoadServers(servers, MM_STUN_MAX_SERVERS);
	if (server_count == 0U)
	{
#ifdef PORT
		port_log("SSB64 Automatch STUN: no servers resolved (set SSB64_MATCHMAKING_STUN_SERVERS)\n");
#endif
		return FALSE;
	}
	first_ok = FALSE;
	{
		u32 try;
		u32 max_tries;

		max_tries = MM_STUN_BINDING_PASSES * server_count;
		for (try = 0U; try < max_tries; try++)
		{
			struct sockaddr_in mapped;

			i = try % server_count;
			memset(&mapped, 0, sizeof(mapped));
			if (mmStunBindingTry(udp_fd, &servers[i], &mapped) == FALSE)
			{
				continue;
			}
			if (first_ok == FALSE)
			{
				mapped_a = mapped;
				mmStunMappedToEndpoint(&mapped, out->endpoint, (u32)sizeof(out->endpoint));
				out->ok = TRUE;
				first_ok = TRUE;
				continue;
			}
			mapped_b = mapped;
			if ((mapped_a.sin_addr.s_addr != mapped_b.sin_addr.s_addr) ||
			    (mapped_a.sin_port != mapped_b.sin_port))
			{
				out->nat_hint = MM_STUN_NAT_SYMMETRIC_SUSPECTED;
			}
			else
			{
				out->nat_hint = MM_STUN_NAT_LIKELY_CONE;
			}
			break;
		}
	}
	if (first_ok != FALSE)
	{
		if (out->nat_hint == MM_STUN_NAT_UNKNOWN)
		{
			out->nat_hint = MM_STUN_NAT_LIKELY_CONE;
		}
#ifdef PORT
		port_log("SSB64 Automatch STUN: reflexive=%s nat_hint=%d servers=%u\n", out->endpoint, (int)out->nat_hint,
		         (unsigned int)server_count);
#endif
		return TRUE;
	}
#ifdef PORT
	port_log("SSB64 Automatch STUN failed (fallback: set SSB64_MATCHMAKING_PUBLIC_ENDPOINT=h:p)\n");
#endif
	return FALSE;
}

sb32 mmStunGetReflexiveIpv4Endpoint(s32 udp_fd, char *buf, u32 bufsize)
{
	MmStunProbeResult probe;

	if ((buf == NULL) || (bufsize < 20U))
	{
		return FALSE;
	}
	if (mmStunProbeIpv4Endpoint(udp_fd, &probe) == FALSE)
	{
		return FALSE;
	}
	snprintf(buf, bufsize, "%s", probe.endpoint);
	return TRUE;
}

#endif /* PORT && SSB64_NETMENU */
