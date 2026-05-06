#include <string.h>

#include "mm_stun.h"

#if defined(PORT) && defined(SSB64_NETMENU) && !defined(_WIN32)

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/random.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef PORT
extern void port_log(const char *fmt, ...);
#endif

static const struct
{
	const char *host;
	u16 port;
} kStunServers[] = {
    { "stun.l.google.com", 19302 },
    { "stun2.l.google.com", 19302 },
};

#define STUN_MAGIC 0x2112A442U

/* Parse XOR-MAPPED-ADDRESS (RFC 5389) inside a STUN Binding success response. */
static void mmStunParseXorMapped(const u8 *pkt, ssize_t total_len, struct sockaddr_in *out)
{
	u16 msg_len = (u16)((pkt[2] << 8) | pkt[3]);
	u32 pos;

	memset(out, 0, sizeof(*out));
	if ((total_len < 20) || (msg_len > 548) || ((ssize_t)(20 + msg_len) > total_len))
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
		if ((attr_type == 0x0020) && (attr_len >= 8)) /* XOR-MAPPED-ADDRESS */
		{
			if ((pkt[pos] == 0x00) && (pkt[pos + 1] == 0x01)) /* IPv4 */
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

sb32 mmStunGetReflexiveIpv4Endpoint(s32 udp_fd, char *buf, u32 bufsize)
{
	u32 s;
	ssize_t n;
	fd_set rfds;
	struct timeval tv;
	struct sockaddr_in stun_serv;
	u8 pkt[512];
	u8 tx_id[12];
	s32 i;

	if ((udp_fd < 0) || (buf == NULL) || (bufsize < 20U))
	{
		return FALSE;
	}
	for (s = 0; s < sizeof(kStunServers) / sizeof(kStunServers[0]); s++)
	{
		struct hostent *he = gethostbyname(kStunServers[s].host);

		memset(&stun_serv, 0, sizeof(stun_serv));
		stun_serv.sin_family = AF_INET;
		stun_serv.sin_port = htons(kStunServers[s].port);
		if ((he == NULL) || (he->h_length != (int)sizeof(struct in_addr)))
		{
			continue;
		}
		memcpy(&stun_serv.sin_addr, he->h_addr_list[0], sizeof(struct in_addr));
		pkt[0] = 0x00;
		pkt[1] = 0x01; /* Binding Request */
		pkt[2] = 0x00;
		pkt[3] = 0x00; /* length */
		pkt[4] = (STUN_MAGIC >> 24) & 0xFF;
		pkt[5] = (STUN_MAGIC >> 16) & 0xFF;
		pkt[6] = (STUN_MAGIC >> 8) & 0xFF;
		pkt[7] = STUN_MAGIC & 0xFF;
		if (getrandom(pkt + 8, 12U, 0) != 12)
		{
			for (i = 0; i < 12; i++)
			{
				pkt[(s32)(8 + i)] = (u8)((u32)getpid() ^ ((u32)i * 7919U));
			}
		}
		memcpy(tx_id, pkt + 8, 12);
		if (sendto(udp_fd, pkt, 20U, 0, (struct sockaddr*)&stun_serv, sizeof(stun_serv)) != 20)
		{
			continue;
		}
		while (TRUE)
		{
			tv.tv_sec = 0;
			tv.tv_usec = 400000;
			FD_ZERO(&rfds);
			FD_SET(udp_fd, &rfds);
			n = select(udp_fd + 1, &rfds, NULL, NULL, &tv);
			if ((n <= 0) || !FD_ISSET(udp_fd, &rfds))
			{
				break;
			}
			memset(pkt, 0, sizeof(pkt));
			n = recvfrom(udp_fd, pkt, sizeof(pkt), MSG_DONTWAIT, NULL, NULL);
			if ((n < 24) || (n >= (ssize_t)sizeof(pkt)))
			{
				continue;
			}
			/* Binding Success response, message type 0x0101 */
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
			{
				struct sockaddr_in mapped;

				mmStunParseXorMapped(pkt, n, &mapped);
				if (mapped.sin_family != 0)
				{
					char ip_buf[INET_ADDRSTRLEN];

					if (inet_ntop(AF_INET, &mapped.sin_addr, ip_buf, sizeof(ip_buf)) == NULL)
					{
						break;
					}
					snprintf(buf, bufsize, "%s:%u", ip_buf, ntohs(mapped.sin_port));
					return TRUE;
				}
			}
		}
	}
#ifdef PORT
	port_log("SSB64 Automatch STUN failed (fallback: set SSB64_MATCHMAKING_PUBLIC_ENDPOINT=h:p)\n");
#endif
	return FALSE;
}

#undef STUN_MAGIC

#endif /* PORT && SSB64_NETMENU && !_WIN32 */
