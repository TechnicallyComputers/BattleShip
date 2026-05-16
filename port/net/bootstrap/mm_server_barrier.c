#if defined(PORT) && defined(SSB64_NETMENU)

#include "bootstrap/mm_server_barrier.h"

#include <stdlib.h>

extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);

static sb32 mmServerBarrierEnvEnabled(void)
{
	char *e;

	e = getenv("SSB64_NETPLAY_SERVER_BOOTSTRAP");
	if ((e == NULL) || (e[0] == '\0'))
	{
		return FALSE;
	}
	if (atoi(e) != 0)
	{
		return TRUE;
	}
	return FALSE;
}

sb32 mmServerBarrierTryApplyHostSchedule(u64 *io_start_ms_raw, u64 *io_quantized_start_ms)
{
	static sb32 s_stub_logged;

	if (mmServerBarrierEnvEnabled() == FALSE)
	{
		return FALSE;
	}
	/* Stub: HTTPS bootstrap not wired; preserve local schedule (identical timings). */
	if (s_stub_logged == FALSE)
	{
		s_stub_logged = TRUE;
		port_log("SSB64 NetPeer: server bootstrap enabled (SSB64_NETPLAY_SERVER_BOOTSTRAP) but HTTPS contract stub — "
		         "using local barrier schedule\n");
	}
	(void)io_start_ms_raw;
	(void)io_quantized_start_ms;
	return FALSE;
}

#endif /* PORT && SSB64_NETMENU */
