/*
 * OS-specific UDP socket helpers for netpeer (POSIX vs Winsock2).
 * Included only from netpeer.c on PORT builds.
 */
#ifndef _SYNETPEER_SOCKET_PLATFORM_H_
#define _SYNETPEER_SOCKET_PLATFORM_H_

#include <PR/ultratypes.h>
#include <ssb_types.h>

#include <stddef.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <wchar.h>
#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET syNetPeerOsSocket;
#define SY_NETPEER_OS_SOCKET_INVALID INVALID_SOCKET

#else /* !_WIN32 */

#include <errno.h>
#include <netinet/in.h>

typedef int syNetPeerOsSocket;
#define SY_NETPEER_OS_SOCKET_INVALID (-1)

#endif /* _WIN32 */

#ifdef __cplusplus
extern "C" {
#endif

/* Call once before any other socket API (idempotent). */
void syNetPeerSocketOsStartup(void);

sb32 syNetPeerOsSocketIsValid(syNetPeerOsSocket s);

syNetPeerOsSocket syNetPeerOsSocketCreateDgram(void);
void syNetPeerOsSocketDestroy(syNetPeerOsSocket *sock_ptr);

int syNetPeerOsSetsockoptReuseAddr(syNetPeerOsSocket s, int reuse_bool);
int syNetPeerOsSetsockoptRecvBuf(syNetPeerOsSocket s, int bytes);
int syNetPeerOsBind(syNetPeerOsSocket s, const struct sockaddr_in *addr);
int syNetPeerOsSetNonBlocking(syNetPeerOsSocket s);

/* Returns byte count, or -1 on error. On !would_block, caller should break recv loop. */
int syNetPeerOsRecvFrom(syNetPeerOsSocket s, void *buf, size_t len, sb32 *would_block_out);

/* Returns byte count, or -1 on error. */
int syNetPeerOsSendTo(syNetPeerOsSocket s, const void *buf, size_t len, const struct sockaddr_in *dst);

/* Wall ms since Unix epoch (UTC). Used for barrier / clock sync. */
u64 syNetPeerOsWallClockUnixMs(void);

/* Sleep roughly `usec` microseconds (best-effort). */
void syNetPeerOsSleepMicros(unsigned usec);

int syNetPeerOsSocketLastError(void);

#ifdef __cplusplus
}
#endif

#endif /* _SYNETPEER_SOCKET_PLATFORM_H_ */
