#include <sys/netpeer_socket_platform.h>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <wchar.h>
#include <windows.h>

static int s_wsa_started;
static LARGE_INTEGER s_win_monotonic_freq;
static int s_win_monotonic_freq_init;

void syNetPeerSocketOsStartup(void)
{
	WSADATA wsa;

	if (s_wsa_started != 0)
	{
		return;
	}
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		return;
	}
	s_wsa_started = 1;
}

sb32 syNetPeerOsSocketIsValid(syNetPeerOsSocket s)
{
	return (s != SY_NETPEER_OS_SOCKET_INVALID) ? TRUE : FALSE;
}

syNetPeerOsSocket syNetPeerOsSocketCreateDgram(void)
{
	syNetPeerOsSocket s;

	syNetPeerSocketOsStartup();
	s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	return s;
}

void syNetPeerOsSocketDestroy(syNetPeerOsSocket *sock_ptr)
{
	if ((sock_ptr == NULL) || (*sock_ptr == SY_NETPEER_OS_SOCKET_INVALID))
	{
		return;
	}
	closesocket(*sock_ptr);
	*sock_ptr = SY_NETPEER_OS_SOCKET_INVALID;
}

int syNetPeerOsSetsockoptReuseAddr(syNetPeerOsSocket s, int reuse_bool)
{
	int v = reuse_bool ? 1 : 0;

	return setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&v, (int)sizeof(v));
}

int syNetPeerOsSetsockoptRecvBuf(syNetPeerOsSocket s, int bytes)
{
	return setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char *)&bytes, (int)sizeof(bytes));
}

void syNetPeerOsSleepMicros(unsigned usec)
{
	/* Windows Sleep() is ~1 ms granularity; sub-ms polls round up to 1 ms. */
	if (usec == 0U)
	{
		return;
	}
	if (usec < 1000U)
	{
		Sleep(1U);
		return;
	}
	Sleep((DWORD)(usec / 1000U));
}

int syNetPeerOsBind(syNetPeerOsSocket s, const struct sockaddr_in *addr)
{
	return bind(s, (const struct sockaddr *)addr, (int)sizeof(*addr));
}

int syNetPeerOsSetNonBlocking(syNetPeerOsSocket s)
{
	u_long mode = 1UL;

	return ioctlsocket(s, FIONBIO, &mode);
}

int syNetPeerOsRecvFrom(syNetPeerOsSocket s, void *buf, size_t len, sb32 *would_block_out)
{
	int r;

	if (would_block_out != NULL)
	{
		*would_block_out = FALSE;
	}
	r = recvfrom(s, (char *)buf, (int)len, 0, NULL, NULL);
	if (r == SOCKET_ERROR)
	{
		int e = WSAGetLastError();

		if ((e == WSAEWOULDBLOCK) && (would_block_out != NULL))
		{
			*would_block_out = TRUE;
		}
		return -1;
	}
	return r;
}

int syNetPeerOsSendTo(syNetPeerOsSocket s, const void *buf, size_t len, const struct sockaddr_in *dst)
{
	int r;

	r = sendto(s, (const char *)buf, (int)len, 0, (const struct sockaddr *)dst, (int)sizeof(*dst));
	if (r == SOCKET_ERROR)
	{
		return -1;
	}
	return r;
}

u64 syNetPeerOsWallClockUnixMs(void)
{
	FILETIME ft;
	ULARGE_INTEGER uli;

	GetSystemTimeAsFileTime(&ft);
	uli.LowPart = ft.dwLowDateTime;
	uli.HighPart = ft.dwHighDateTime;
	/* 100-ns intervals since 1601-01-01 UTC → Unix ms */
	return (u64)((uli.QuadPart - 116444736000000000ULL) / 10000ULL);
}

u64 syNetPeerOsMonotonicMs(void)
{
	LARGE_INTEGER counter;

	if (s_win_monotonic_freq_init == 0)
	{
		if (QueryPerformanceFrequency(&s_win_monotonic_freq) == 0)
		{
			s_win_monotonic_freq.QuadPart = 0;
		}
		s_win_monotonic_freq_init = 1;
	}
	if (s_win_monotonic_freq.QuadPart == 0)
	{
		return (u64)GetTickCount64();
	}
	if (QueryPerformanceCounter(&counter) == 0)
	{
		return (u64)GetTickCount64();
	}
	return (u64)((counter.QuadPart * 1000ULL) / (u64)s_win_monotonic_freq.QuadPart);
}

int syNetPeerOsSocketLastError(void)
{
	return (int)WSAGetLastError();
}

#else /* !_WIN32 */

#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

void syNetPeerSocketOsStartup(void)
{
}

sb32 syNetPeerOsSocketIsValid(syNetPeerOsSocket s)
{
	return (s >= 0) ? TRUE : FALSE;
}

syNetPeerOsSocket syNetPeerOsSocketCreateDgram(void)
{
	syNetPeerSocketOsStartup();
	return socket(AF_INET, SOCK_DGRAM, 0);
}

void syNetPeerOsSocketDestroy(syNetPeerOsSocket *sock_ptr)
{
	if ((sock_ptr == NULL) || (*sock_ptr < 0))
	{
		return;
	}
	close(*sock_ptr);
	*sock_ptr = SY_NETPEER_OS_SOCKET_INVALID;
}

int syNetPeerOsSetsockoptReuseAddr(syNetPeerOsSocket s, int reuse_bool)
{
	int v = reuse_bool;

	return setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &v, (socklen_t)sizeof(v));
}

int syNetPeerOsSetsockoptRecvBuf(syNetPeerOsSocket s, int bytes)
{
	return setsockopt(s, SOL_SOCKET, SO_RCVBUF, &bytes, (socklen_t)sizeof(bytes));
}

int syNetPeerOsBind(syNetPeerOsSocket s, const struct sockaddr_in *addr)
{
	return bind(s, (const struct sockaddr *)addr, (socklen_t)sizeof(*addr));
}

int syNetPeerOsSetNonBlocking(syNetPeerOsSocket s)
{
	int flags;

	flags = fcntl(s, F_GETFL, 0);
	if (flags < 0)
	{
		return -1;
	}
	return fcntl(s, F_SETFL, flags | O_NONBLOCK);
}

int syNetPeerOsRecvFrom(syNetPeerOsSocket s, void *buf, size_t len, sb32 *would_block_out)
{
	ssize_t r;

	if (would_block_out != NULL)
	{
		*would_block_out = FALSE;
	}
	r = recvfrom(s, buf, len, 0, NULL, NULL);
	if (r < 0)
	{
		if (((errno == EAGAIN) || (errno == EWOULDBLOCK)) && (would_block_out != NULL))
		{
			*would_block_out = TRUE;
		}
		return -1;
	}
	return (int)r;
}

int syNetPeerOsSendTo(syNetPeerOsSocket s, const void *buf, size_t len, const struct sockaddr_in *dst)
{
	ssize_t r;

	r = sendto(s, buf, len, 0, (const struct sockaddr *)dst, (socklen_t)sizeof(*dst));
	if (r < 0)
	{
		return -1;
	}
	return (int)r;
}

u64 syNetPeerOsWallClockUnixMs(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
	{
		return 0ULL;
	}
	return (u64)ts.tv_sec * 1000ULL + (u64)(ts.tv_nsec / 1000000L);
}

u64 syNetPeerOsMonotonicMs(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
	{
		return 0ULL;
	}
	return (u64)ts.tv_sec * 1000ULL + (u64)(ts.tv_nsec / 1000000L);
}

int syNetPeerOsSocketLastError(void)
{
	return errno;
}

void syNetPeerOsSleepMicros(unsigned usec)
{
	if (usec == 0U)
	{
		return;
	}
	usleep(usec);
}

#endif /* _WIN32 */
