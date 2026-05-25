#include "mm_turn.h"

#if defined(PORT) && defined(SSB64_NETMENU)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int atoi(const char *s);

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifdef PORT
extern void port_log(const char *fmt, ...);
#endif

#define STUN_MAGIC 0x2112A442U
#define MM_TURN_RECV_US 600000
#define MM_TURN_CHANNEL 0x4000U
#define MM_TURN_MAX_MSG 1600

#define STUN_ATTR_MAPPED_ADDRESS 0x0001U
#define STUN_ATTR_XOR_MAPPED_ADDRESS 0x0020U
#define STUN_ATTR_REALM 0x0014U
#define STUN_ATTR_NONCE 0x0015U
#define STUN_ATTR_USERNAME 0x0006U
#define STUN_ATTR_MESSAGE_INTEGRITY 0x0008U
#define STUN_ATTR_XOR_RELAYED_ADDRESS 0x0016U
#define STUN_ATTR_REQUESTED_TRANSPORT 0x0019U
#define STUN_ATTR_LIFETIME 0x000DU

#define STUN_METHOD_ALLOCATE 0x0003U
#define STUN_METHOD_CREATE_PERMISSION 0x0008U
#define STUN_METHOD_CHANNEL_BIND 0x0009U

#define STUN_CLASS_REQUEST 0x0000U
#define STUN_CLASS_SUCCESS 0x0100U
#define STUN_CLASS_ERROR 0x0110U

#define STUN_TRANSPORT_UDP 17U

/* Default coturn on match host — override via env until credentials are obscured. */
#define MM_TURN_DEFAULT_HOST "216.154.76.149"
#define MM_TURN_DEFAULT_PORT 3478
#define MM_TURN_DEFAULT_USER "netplay"
#define MM_TURN_DEFAULT_PASS "rCGKgDisoVJcdFRhltm3"

typedef struct MmTurnServer
{
	struct sockaddr_in addr;
} MmTurnServer;

static struct
{
	sb32 allocated;
	char relay_endpoint[128];
	struct sockaddr_in relay_addr;
	struct sockaddr_in server_addr;
	char realm[128];
	char nonce[256];
	char username[64];
	char password[96];
	u8 last_tx_id[12];
	u16 channel;
	struct sockaddr_in peer_addr;
	sb32 channel_bound;
} sMmTurn;

#ifdef _WIN32
typedef int mm_sock_len_t;
#else
typedef ssize_t mm_sock_len_t;
#endif

/* ---- minimal SHA1 + HMAC-SHA1 (RFC 2104 / RFC 3174) ---- */

typedef struct MmSha1Ctx
{
	u32 state[5];
	u32 count[2];
	u8 buffer[64];
} MmSha1Ctx;

static u32 mmSha1Rol(u32 v, u32 bits)
{
	return (v << bits) | (v >> (32U - bits));
}

static void mmSha1Transform(u32 state[5], const u8 block[64])
{
	u32 a = state[0];
	u32 b = state[1];
	u32 c = state[2];
	u32 d = state[3];
	u32 e = state[4];
	u32 w[80];
	u32 i;

	for (i = 0U; i < 16U; i++)
	{
		w[i] = ((u32)block[i * 4U] << 24) | ((u32)block[i * 4U + 1U] << 16) | ((u32)block[i * 4U + 2U] << 8) |
		       (u32)block[i * 4U + 3U];
	}
	for (i = 16U; i < 80U; i++)
	{
		w[i] = mmSha1Rol(w[i - 3U] ^ w[i - 8U] ^ w[i - 14U] ^ w[i - 16U], 1U);
	}
	for (i = 0U; i < 80U; i++)
	{
		u32 f;
		u32 k;

		if (i < 20U)
		{
			f = (b & c) | ((~b) & d);
			k = 0x5A827999U;
		}
		else if (i < 40U)
		{
			f = b ^ c ^ d;
			k = 0x6ED9EBA1U;
		}
		else if (i < 60U)
		{
			f = (b & c) | (b & d) | (c & d);
			k = 0x8F1BBCDCU;
		}
		else
		{
			f = b ^ c ^ d;
			k = 0xCA62C1D6U;
		}
		{
			u32 temp = mmSha1Rol(a, 5U) + f + e + k + w[i];

			e = d;
			d = c;
			c = mmSha1Rol(b, 30U);
			b = a;
			a = temp;
		}
	}
	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;
}

static void mmSha1Init(MmSha1Ctx *ctx)
{
	ctx->state[0] = 0x67452301U;
	ctx->state[1] = 0xEFCDAB89U;
	ctx->state[2] = 0x98BADCFEU;
	ctx->state[3] = 0x10325476U;
	ctx->state[4] = 0xC3D2E1F0U;
	ctx->count[0] = 0U;
	ctx->count[1] = 0U;
}

static void mmSha1Update(MmSha1Ctx *ctx, const u8 *data, size_t len)
{
	u32 i;
	u32 idx = (ctx->count[0] >> 3) & 63U;

	ctx->count[0] += (u32)(len << 3);
	if (ctx->count[0] < (u32)(len << 3))
	{
		ctx->count[1]++;
	}
	ctx->count[1] += (u32)(len >> 29);
	while (len > 0U)
	{
		u32 chunk = 64U - idx;

		if (chunk > len)
		{
			chunk = (u32)len;
		}
		memcpy(ctx->buffer + idx, data, chunk);
		idx += chunk;
		data += chunk;
		len -= chunk;
		if (idx == 64U)
		{
			mmSha1Transform(ctx->state, ctx->buffer);
			idx = 0U;
		}
	}
}

static void mmSha1Final(MmSha1Ctx *ctx, u8 digest[20])
{
	u8 final_count[8];
	u32 i;
	u8 c = 0x80U;

	for (i = 0U; i < 8U; i++)
	{
		final_count[i] = (u8)((ctx->count[(i >= 4U) ? 0U : 1U] >> ((3U - (i & 3U)) * 8)) & 0xFFU);
	}
	mmSha1Update(ctx, &c, 1U);
	while ((ctx->count[0] & 504U) != 448U)
	{
		c = 0U;
		mmSha1Update(ctx, &c, 1U);
	}
	mmSha1Update(ctx, final_count, 8U);
	for (i = 0U; i < 20U; i++)
	{
		digest[i] = (u8)((ctx->state[i >> 2] >> ((3U - (i & 3U)) * 8)) & 0xFFU);
	}
}

static void mmHmacSha1(const u8 *key, size_t key_len, const u8 *msg, size_t msg_len, u8 out[20])
{
	u8 kpad[64];
	u8 tk[20];
	u8 inner[64 + 256];
	u8 outer[64 + 20];
	MmSha1Ctx ctx;
	size_t i;

	memset(kpad, 0, sizeof(kpad));
	if (key_len > 64U)
	{
		mmSha1Init(&ctx);
		mmSha1Update(&ctx, key, key_len);
		mmSha1Final(&ctx, tk);
		key = tk;
		key_len = 20U;
	}
	memcpy(kpad, key, key_len);
	for (i = 0U; i < 64U; i++)
	{
		inner[i] = (u8)(kpad[i] ^ 0x36U);
		outer[i] = (u8)(kpad[i] ^ 0x5CU);
	}
	memcpy(inner + 64U, msg, msg_len);
	mmSha1Init(&ctx);
	mmSha1Update(&ctx, inner, 64U + msg_len);
	mmSha1Final(&ctx, out);
	memcpy(outer + 64U, out, 20U);
	mmSha1Init(&ctx);
	mmSha1Update(&ctx, outer, 64U + 20U);
	mmSha1Final(&ctx, out);
}

#if defined(SSB64_HAVE_OPENSSL)
#include <openssl/md5.h>
#endif

static void mmTurnMd5LongTermKey(const char *user, const char *realm, const char *pass, u8 out[16])
{
	char buf[384];

	snprintf(buf, sizeof(buf), "%s:%s:%s", user, realm, pass);
#if defined(SSB64_HAVE_OPENSSL)
	MD5((const unsigned char *)buf, strlen(buf), out);
#else
	{
		MmSha1Ctx ctx;
		u8 digest[20];

		/* Without OpenSSL, long-term TURN auth will not match coturn — build with OPENSSL. */
		mmSha1Init(&ctx);
		mmSha1Update(&ctx, (const u8 *)buf, strlen(buf));
		mmSha1Final(&ctx, digest);
		memcpy(out, digest, 16U);
	}
#endif
}

/* ---- STUN helpers ---- */

static void mmTurnFillTxId(u8 *tx_id)
{
	s32 i;

#ifdef _WIN32
	for (i = 0; i < 12; i++)
	{
		tx_id[i] = (u8)((u32)GetTickCount() ^ (u32)GetCurrentProcessId() ^ ((u32)i * 4099U));
	}
#else
	for (i = 0; i < 12; i++)
	{
		tx_id[i] = (u8)((u32)getpid() ^ ((u32)i * 4099U));
	}
#endif
}

static u32 mmTurnPad4(u32 len)
{
	return (len + 3U) & ~3U;
}

static u8 *mmTurnAppendAttr(u8 *cursor, u16 attr_type, const void *value, u16 value_len)
{
	u32 pad;
	u32 i;

	cursor[0] = (u8)((attr_type >> 8) & 0xFFU);
	cursor[1] = (u8)(attr_type & 0xFFU);
	cursor[2] = (u8)((value_len >> 8) & 0xFFU);
	cursor[3] = (u8)(value_len & 0xFFU);
	memcpy(cursor + 4U, value, value_len);
	pad = mmTurnPad4((u32)value_len);
	for (i = (u32)value_len; i < pad; i++)
	{
		cursor[4U + i] = 0U;
	}
	return cursor + 4U + pad;
}

static void mmTurnWriteHeader(u8 *msg, u16 method, u16 class_bits, const u8 *tx_id, u16 attr_len)
{
	u16 typ = (u16)(method | class_bits);

	msg[0] = (u8)((typ >> 8) & 0xFFU);
	msg[1] = (u8)(typ & 0xFFU);
	msg[2] = (u8)((attr_len >> 8) & 0xFFU);
	msg[3] = (u8)(attr_len & 0xFFU);
	msg[4] = (u8)((STUN_MAGIC >> 24) & 0xFFU);
	msg[5] = (u8)((STUN_MAGIC >> 16) & 0xFFU);
	msg[6] = (u8)((STUN_MAGIC >> 8) & 0xFFU);
	msg[7] = (u8)(STUN_MAGIC & 0xFFU);
	memcpy(msg + 8U, tx_id, 12U);
}

static void mmTurnAddMessageIntegrity(u8 *msg, u16 *io_attr_len, const u8 *key16)
{
	u16 total;
	u8 *mi_pos;
	u8 zero[20];

	total = (u16)(*io_attr_len + 24U);
	mi_pos = msg + 20U + *io_attr_len;
	mi_pos = mmTurnAppendAttr(mi_pos, STUN_ATTR_MESSAGE_INTEGRITY, zero, 20U);
	*io_attr_len = (u16)(*io_attr_len + 24U);
	msg[2] = (u8)((*io_attr_len >> 8) & 0xFFU);
	msg[3] = (u8)(*io_attr_len & 0xFFU);
	mmHmacSha1(key16, 16U, msg, (size_t)total, mi_pos + 4U);
}

static sb32 mmTurnResolveServer(MmTurnServer *out)
{
	const char *env;
	char host[128];
	char buf[256];
	char *colon;
	long port_l;
	struct addrinfo hints;
	struct addrinfo *res;

	env = getenv("SSB64_MATCHMAKING_TURN_SERVER");
	if ((env != NULL) && (env[0] != '\0'))
	{
		snprintf(buf, sizeof(buf), "%s", env);
	}
	else
	{
		snprintf(buf, sizeof(buf), "%s:%u", MM_TURN_DEFAULT_HOST, (unsigned int)MM_TURN_DEFAULT_PORT);
	}
	snprintf(host, sizeof(host), "%s", buf);
	colon = strrchr(host, ':');
	if (colon != NULL)
	{
		*colon = '\0';
		port_l = strtol(colon + 1, NULL, 10);
	}
	else
	{
		port_l = MM_TURN_DEFAULT_PORT;
	}
	if (port_l <= 0L || port_l >= 65536L)
	{
		port_l = MM_TURN_DEFAULT_PORT;
	}
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	if (getaddrinfo(host, NULL, &hints, &res) != 0 || res == NULL)
	{
		return FALSE;
	}
	memset(out, 0, sizeof(*out));
	memcpy(&out->addr, res->ai_addr, sizeof(out->addr));
	out->addr.sin_port = htons((u16)port_l);
	freeaddrinfo(res);
	return TRUE;
}

static void mmTurnLoadCredentials(void)
{
	const char *e;

	e = getenv("SSB64_MATCHMAKING_TURN_USER");
	snprintf(sMmTurn.username, sizeof(sMmTurn.username), "%s", (e != NULL && e[0] != '\0') ? e : MM_TURN_DEFAULT_USER);
	e = getenv("SSB64_MATCHMAKING_TURN_PASS");
	snprintf(sMmTurn.password, sizeof(sMmTurn.password), "%s", (e != NULL && e[0] != '\0') ? e : MM_TURN_DEFAULT_PASS);
}

static sb32 mmTurnSendRecv(s32 sock, const u8 *req, u16 req_len, u8 *resp, u16 resp_cap, mm_sock_len_t *out_len)
{
	fd_set rfds;
	struct timeval tv;
	mm_sock_len_t n;

#ifdef _WIN32
	if (sendto((SOCKET)(intptr_t)sock, (const char *)req, req_len, 0, (struct sockaddr *)&sMmTurn.server_addr,
	           sizeof(sMmTurn.server_addr)) != (int)req_len)
#else
	if (sendto(sock, req, req_len, 0, (struct sockaddr *)&sMmTurn.server_addr, sizeof(sMmTurn.server_addr)) !=
	    (ssize_t)req_len)
#endif
	{
		return FALSE;
	}
	tv.tv_sec = 0;
	tv.tv_usec = MM_TURN_RECV_US;
	FD_ZERO(&rfds);
	FD_SET(sock, &rfds);
#ifdef _WIN32
	n = select(0, &rfds, NULL, NULL, &tv);
#else
	n = select(sock + 1, &rfds, NULL, NULL, &tv);
#endif
	if ((n <= 0) || !FD_ISSET(sock, &rfds))
	{
		return FALSE;
	}
#ifdef _WIN32
	*out_len = recvfrom((SOCKET)(intptr_t)sock, (char *)resp, resp_cap, 0, NULL, NULL);
#else
	*out_len = recvfrom(sock, resp, resp_cap, MSG_DONTWAIT, NULL, NULL);
#endif
	return (*out_len >= 20) ? TRUE : FALSE;
}

static sb32 mmTurnParseAttr(const u8 *msg, mm_sock_len_t len, u16 want_type, u8 *out, u16 out_cap, u16 *out_len)
{
	u16 attr_len = (u16)(((u16)msg[2] << 8) | msg[3]);
	u32 pos = 20U;

	while (pos + 4U <= (u32)len && pos < 20U + (u32)attr_len)
	{
		u16 at = (u16)(((u16)msg[pos] << 8) | msg[pos + 1U]);
		u16 al = (u16)(((u16)msg[pos + 2U] << 8) | msg[pos + 3U]);
		u32 pad = mmTurnPad4((u32)al);

		if (at == want_type && al > 0U && (u32)al + 4U <= (u32)len - pos)
		{
			u16 copy = al;

			if (copy > out_cap)
			{
				copy = out_cap;
			}
			memcpy(out, msg + pos + 4U, copy);
			if (out_len != NULL)
			{
				*out_len = copy;
			}
			return TRUE;
		}
		pos += 4U + pad;
	}
	return FALSE;
}

static sb32 mmTurnParseXorRelayed(const u8 *msg, mm_sock_len_t len, struct sockaddr_in *out)
{
	u8 raw[8];
	u16 raw_len;
	u32 cookie;
	u32 i;

	if (mmTurnParseAttr(msg, len, STUN_ATTR_XOR_RELAYED_ADDRESS, raw, sizeof(raw), &raw_len) == FALSE || raw_len < 8U)
	{
		return FALSE;
	}
	cookie = STUN_MAGIC;
	memset(out, 0, sizeof(*out));
	out->sin_family = AF_INET;
	for (i = 0U; i < 4U; i++)
	{
		((u8 *)&out->sin_addr)[i] = (u8)(raw[4U + i] ^ ((cookie >> ((3U - i) * 8)) & 0xFFU));
	}
	{
		u16 port = (u16)(((u16)raw[2] << 8) | raw[3]);
		port ^= (u16)((cookie >> 16) & 0xFFFFU);
		out->sin_port = htons(port);
	}
	return TRUE;
}

static sb32 mmTurnCheckResponse(const u8 *msg, mm_sock_len_t len, u16 method, u16 class_mask)
{
	u16 typ;
	u32 mc;

	if (len < 20 || memcmp(msg + 8, sMmTurn.last_tx_id, 12) != 0)
	{
		return FALSE;
	}
	typ = (u16)(((u16)msg[0] << 8) | msg[1]);
	if ((typ & 0x3FFFU) != method)
	{
		return FALSE;
	}
	if ((typ & 0xC100U) != class_mask)
	{
		return FALSE;
	}
	mc = ((u32)msg[4] << 24) | ((u32)msg[5] << 16) | ((u32)msg[6] << 8) | msg[7];
	return (mc == STUN_MAGIC) ? TRUE : FALSE;
}

static sb32 mmTurnBuildAllocate(u8 *msg, u16 *out_len, sb32 with_auth)
{
	u8 *cursor = msg + 20U;
	u16 attr_len = 0U;
	u8 transport[4];
	u32 lifetime = htonl(600U);

	transport[0] = 0U;
	transport[1] = 0U;
	transport[2] = 0U;
	transport[3] = STUN_TRANSPORT_UDP;
	mmTurnFillTxId(sMmTurn.last_tx_id);
	mmTurnWriteHeader(msg, STUN_METHOD_ALLOCATE, STUN_CLASS_REQUEST, sMmTurn.last_tx_id, 0U);
	cursor = mmTurnAppendAttr(cursor, STUN_ATTR_REQUESTED_TRANSPORT, transport, 4U);
	attr_len = (u16)(cursor - msg - 20U);
	cursor = mmTurnAppendAttr(cursor, STUN_ATTR_LIFETIME, &lifetime, 4U);
	attr_len = (u16)(cursor - msg - 20U);
	if (with_auth != FALSE)
	{
		cursor = mmTurnAppendAttr(cursor, STUN_ATTR_USERNAME, sMmTurn.username, (u16)strlen(sMmTurn.username));
		attr_len = (u16)(cursor - msg - 20U);
		cursor = mmTurnAppendAttr(cursor, STUN_ATTR_REALM, sMmTurn.realm, (u16)strlen(sMmTurn.realm));
		attr_len = (u16)(cursor - msg - 20U);
		cursor = mmTurnAppendAttr(cursor, STUN_ATTR_NONCE, sMmTurn.nonce, (u16)strlen(sMmTurn.nonce));
		attr_len = (u16)(cursor - msg - 20U);
		{
			u8 key[16];

			mmTurnMd5LongTermKey(sMmTurn.username, sMmTurn.realm, sMmTurn.password, key);
			mmTurnAddMessageIntegrity(msg, &attr_len, key);
		}
	}
	msg[2] = (u8)((attr_len >> 8) & 0xFFU);
	msg[3] = (u8)(attr_len & 0xFFU);
	*out_len = (u16)(20U + attr_len);
	return TRUE;
}

static sb32 mmTurnBuildAuthedRequest(u8 *msg, u16 *out_len, u16 method, const u8 *extra, u16 extra_len)
{
	u8 *cursor = msg + 20U;
	u16 attr_len = 0U;
	u8 key[16];

	mmTurnFillTxId(sMmTurn.last_tx_id);
	mmTurnWriteHeader(msg, method, STUN_CLASS_REQUEST, sMmTurn.last_tx_id, 0U);
	if (extra != NULL && extra_len > 0U)
	{
		memcpy(cursor, extra, extra_len);
		attr_len = extra_len;
		cursor += extra_len;
	}
	cursor = mmTurnAppendAttr(cursor, STUN_ATTR_USERNAME, sMmTurn.username, (u16)strlen(sMmTurn.username));
	attr_len = (u16)(cursor - msg - 20U);
	cursor = mmTurnAppendAttr(cursor, STUN_ATTR_REALM, sMmTurn.realm, (u16)strlen(sMmTurn.realm));
	attr_len = (u16)(cursor - msg - 20U);
	cursor = mmTurnAppendAttr(cursor, STUN_ATTR_NONCE, sMmTurn.nonce, (u16)strlen(sMmTurn.nonce));
	attr_len = (u16)(cursor - msg - 20U);
	mmTurnMd5LongTermKey(sMmTurn.username, sMmTurn.realm, sMmTurn.password, key);
	mmTurnAddMessageIntegrity(msg, &attr_len, key);
	msg[2] = (u8)((attr_len >> 8) & 0xFFU);
	msg[3] = (u8)(attr_len & 0xFFU);
	*out_len = (u16)(20U + attr_len);
	return TRUE;
}

static sb32 mmTurnParseHostport(const char *hp, struct sockaddr_in *out)
{
	char buf[128];
	char *colon;
	long port_l;

	snprintf(buf, sizeof(buf), "%s", hp);
	colon = strrchr(buf, ':');
	if (colon == NULL)
	{
		return FALSE;
	}
	*colon = '\0';
	port_l = strtol(colon + 1, NULL, 10);
	if (port_l <= 0L || port_l >= 65536L)
	{
		return FALSE;
	}
	memset(out, 0, sizeof(*out));
	out->sin_family = AF_INET;
	if (inet_pton(AF_INET, buf, &out->sin_addr) != 1)
	{
		return FALSE;
	}
	out->sin_port = htons((u16)port_l);
	return TRUE;
}

sb32 mmTurnIsClientEnabled(void)
{
	const char *e;

	e = getenv("SSB64_NETPLAY_TURN_DISABLE");
	if ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0))
	{
		return FALSE;
	}
	return TRUE;
}

void mmTurnEndRelaySession(void)
{
	memset(&sMmTurn, 0, sizeof(sMmTurn));
}

sb32 mmTurnAllocateIpv4Relay(s32 udp_fd, MmTurnRelayResult *out)
{
	MmTurnServer server;
	u8 req[MM_TURN_MAX_MSG];
	u8 resp[MM_TURN_MAX_MSG];
	u16 req_len;
	mm_sock_len_t rlen;
	s32 attempt;
	char ip_buf[INET_ADDRSTRLEN];

	if ((udp_fd < 0) || (out == NULL) || (mmTurnIsClientEnabled() == FALSE))
	{
		return FALSE;
	}
	memset(out, 0, sizeof(*out));
	mmTurnEndRelaySession();
	mmTurnLoadCredentials();
	if (mmTurnResolveServer(&server) == FALSE)
	{
		return FALSE;
	}
	sMmTurn.server_addr = server.addr;
	for (attempt = 0; attempt < 3; attempt++)
	{
		if (mmTurnBuildAllocate(req, &req_len, FALSE) == FALSE)
		{
			continue;
		}
		if (mmTurnSendRecv(udp_fd, req, req_len, resp, sizeof(resp), &rlen) == FALSE)
		{
			continue;
		}
		if (mmTurnCheckResponse(resp, rlen, STUN_METHOD_ALLOCATE, STUN_CLASS_ERROR) != FALSE)
		{
			u16 rl;
			u16 nl;

			sMmTurn.realm[0] = '\0';
			sMmTurn.nonce[0] = '\0';
			(void)mmTurnParseAttr(resp, rlen, STUN_ATTR_REALM, (u8 *)sMmTurn.realm, sizeof(sMmTurn.realm) - 1U, &rl);
			(void)mmTurnParseAttr(resp, rlen, STUN_ATTR_NONCE, (u8 *)sMmTurn.nonce, sizeof(sMmTurn.nonce) - 1U, &nl);
			if (sMmTurn.realm[0] == '\0' || sMmTurn.nonce[0] == '\0')
			{
				continue;
			}
			if (mmTurnBuildAllocate(req, &req_len, TRUE) == FALSE)
			{
				continue;
			}
			if (mmTurnSendRecv(udp_fd, req, req_len, resp, sizeof(resp), &rlen) == FALSE)
			{
				continue;
			}
		}
		if (mmTurnCheckResponse(resp, rlen, STUN_METHOD_ALLOCATE, STUN_CLASS_SUCCESS) == FALSE)
		{
			continue;
		}
		if (mmTurnParseXorRelayed(resp, rlen, &sMmTurn.relay_addr) == FALSE)
		{
			continue;
		}
		if (inet_ntop(AF_INET, &sMmTurn.relay_addr.sin_addr, ip_buf, sizeof(ip_buf)) == NULL)
		{
			continue;
		}
		snprintf(sMmTurn.relay_endpoint, sizeof(sMmTurn.relay_endpoint), "%s:%u", ip_buf,
		         (unsigned int)ntohs(sMmTurn.relay_addr.sin_port));
		sMmTurn.allocated = TRUE;
		sMmTurn.channel = MM_TURN_CHANNEL;
		out->ok = TRUE;
		snprintf(out->relay_endpoint, sizeof(out->relay_endpoint), "%s", sMmTurn.relay_endpoint);
#ifdef PORT
		port_log("SSB64 Automatch TURN: relay=%s server=%s user=%s\n", sMmTurn.relay_endpoint, MM_TURN_DEFAULT_HOST,
		         sMmTurn.username);
#endif
		return TRUE;
	}
#ifdef PORT
	port_log("SSB64 Automatch TURN: allocate failed (coturn unreachable or bad credentials)\n");
#endif
	return FALSE;
}

static sb32 mmTurnXorPeerAddress(const struct sockaddr_in *peer, u8 out[8])
{
	u32 cookie = STUN_MAGIC;
	u16 port = ntohs(peer->sin_port) ^ (u16)((cookie >> 16) & 0xFFFFU);
	const u8 *ip = (const u8 *)&peer->sin_addr;

	out[0] = 0U;
	out[1] = 0U;
	out[2] = (u8)((port >> 8) & 0xFFU);
	out[3] = (u8)(port & 0xFFU);
	out[4] = (u8)(ip[0] ^ ((cookie >> 24) & 0xFFU));
	out[5] = (u8)(ip[1] ^ ((cookie >> 16) & 0xFFU));
	out[6] = (u8)(ip[2] ^ ((cookie >> 8) & 0xFFU));
	out[7] = (u8)(ip[3] ^ (cookie & 0xFFU));
	return TRUE;
}

sb32 mmTurnBeginRelayToPeer(s32 udp_fd, const char *peer_hostport)
{
	u8 req[MM_TURN_MAX_MSG];
	u8 resp[MM_TURN_MAX_MSG];
	u8 extra[32];
	u8 *cursor;
	u16 req_len;
	u16 extra_len;
	mm_sock_len_t rlen;
	u8 xpeer[8];
	u8 chbind[16];

	if ((udp_fd < 0) || (peer_hostport == NULL) || (peer_hostport[0] == '\0') || (sMmTurn.allocated == FALSE))
	{
		return FALSE;
	}
	if (mmTurnParseHostport(peer_hostport, &sMmTurn.peer_addr) == FALSE)
	{
		return FALSE;
	}
	mmTurnXorPeerAddress(&sMmTurn.peer_addr, xpeer);
	cursor = extra;
	/* XOR-PEER-ADDRESS 0x0012 */
	cursor[0] = 0x00U;
	cursor[1] = 0x12U;
	cursor[2] = 0x00U;
	cursor[3] = 0x08U;
	memcpy(cursor + 4U, xpeer, 8U);
	extra_len = 12U;
	if (mmTurnBuildAuthedRequest(req, &req_len, STUN_METHOD_CREATE_PERMISSION, extra, extra_len) == FALSE)
	{
		return FALSE;
	}
	if (mmTurnSendRecv(udp_fd, req, req_len, resp, sizeof(resp), &rlen) == FALSE ||
	    mmTurnCheckResponse(resp, rlen, STUN_METHOD_CREATE_PERMISSION, STUN_CLASS_SUCCESS) == FALSE)
	{
#ifdef PORT
		port_log("SSB64 Automatch TURN: CreatePermission failed peer=%s\n", peer_hostport);
#endif
		return FALSE;
	}
	chbind[0] = (u8)((MM_TURN_CHANNEL >> 8) & 0xFFU);
	chbind[1] = (u8)(MM_TURN_CHANNEL & 0xFFU);
	chbind[2] = 0x00U;
	chbind[3] = 0x08U;
	memcpy(chbind + 4U, xpeer, 8U);
	if (mmTurnBuildAuthedRequest(req, &req_len, STUN_METHOD_CHANNEL_BIND, chbind, 16U) == FALSE)
	{
		return FALSE;
	}
	if (mmTurnSendRecv(udp_fd, req, req_len, resp, sizeof(resp), &rlen) == FALSE ||
	    mmTurnCheckResponse(resp, rlen, STUN_METHOD_CHANNEL_BIND, STUN_CLASS_SUCCESS) == FALSE)
	{
#ifdef PORT
		port_log("SSB64 Automatch TURN: ChannelBind failed peer=%s\n", peer_hostport);
#endif
		return FALSE;
	}
	sMmTurn.channel_bound = TRUE;
#ifdef PORT
	port_log("SSB64 Automatch TURN: relay channel bound peer=%s ch=0x%04X\n", peer_hostport,
	         (unsigned int)sMmTurn.channel);
#endif
	return TRUE;
}

sb32 mmTurnRelaySend(s32 udp_fd, const u8 *payload, u32 len)
{
	u8 buf[MM_TURN_MAX_MSG];
	u16 total;

	if ((udp_fd < 0) || (payload == NULL) || (len == 0U) || (sMmTurn.channel_bound == FALSE))
	{
		return FALSE;
	}
	if (len + 4U > MM_TURN_MAX_MSG)
	{
		return FALSE;
	}
	buf[0] = (u8)((sMmTurn.channel >> 8) & 0xFFU);
	buf[1] = (u8)(sMmTurn.channel & 0xFFU);
	buf[2] = (u8)((len >> 8) & 0xFFU);
	buf[3] = (u8)(len & 0xFFU);
	memcpy(buf + 4U, payload, len);
	total = (u16)(len + 4U);
#ifdef _WIN32
	return (sendto((SOCKET)(intptr_t)udp_fd, (const char *)buf, total, 0, (struct sockaddr *)&sMmTurn.server_addr,
	               sizeof(sMmTurn.server_addr)) == (int)total)
	           ? TRUE
	           : FALSE;
#else
	return (sendto(udp_fd, buf, total, 0, (struct sockaddr *)&sMmTurn.server_addr, sizeof(sMmTurn.server_addr)) ==
	        (ssize_t)total)
	           ? TRUE
	           : FALSE;
#endif
}

sb32 mmTurnRelayUnwrap(const u8 *payload, s32 len, const void *from_addr, u8 *out, u32 out_cap, s32 *out_len)
{
	const struct sockaddr_in *from = (const struct sockaddr_in *)from_addr;
	u16 ch;
	u16 data_len;

	if ((payload == NULL) || (len < 4) || (from == NULL) || (out == NULL) || (out_len == NULL) ||
	    (sMmTurn.allocated == FALSE))
	{
		return FALSE;
	}
	if (from->sin_family != AF_INET || from->sin_addr.s_addr != sMmTurn.server_addr.sin_addr.s_addr ||
	    from->sin_port != sMmTurn.server_addr.sin_port)
	{
		return FALSE;
	}
	ch = (u16)(((u16)payload[0] << 8) | payload[1]);
	if (ch != sMmTurn.channel)
	{
		return FALSE;
	}
	data_len = (u16)(((u16)payload[2] << 8) | payload[3]);
	if ((u32)data_len + 4U > (u32)len || data_len > out_cap)
	{
		return FALSE;
	}
	memcpy(out, payload + 4, data_len);
	*out_len = (s32)data_len;
	return TRUE;
}

sb32 mmTurnRelaySessionActive(void)
{
	return (sMmTurn.channel_bound != FALSE) ? TRUE : FALSE;
}

const char *mmTurnGetRelayEndpoint(void)
{
	return (sMmTurn.relay_endpoint[0] != '\0') ? sMmTurn.relay_endpoint : NULL;
}

#endif /* PORT && SSB64_NETMENU */
