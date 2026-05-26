#if defined(PORT) && defined(SSB64_NETMENU)

#include "bootstrap/mm_server_barrier.h"

#include <mm_matchmaking.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/netpeer.h>

extern void port_log(const char *fmt, ...);

static u32 sBarrierPingSeq;
static sb32 sBarrierFallbackLogged;

static sb32 mmServerBarrierEnvTruthy(const char *name)
{
	char *e;

	e = getenv(name);
	if ((e == NULL) || (e[0] == '\0'))
	{
		return FALSE;
	}
	return (e[0] == '1') ? TRUE : FALSE;
}

static sb32 mmServerBarrierEnvEnabled(void)
{
	return mmServerBarrierEnvTruthy("SSB64_NETPLAY_SERVER_BOOTSTRAP");
}

static sb32 mmServerBarrierVerbose(void)
{
	return mmServerBarrierEnvTruthy("SSB64_NETPLAY_SERVER_BOOTSTRAP_VERBOSE");
}

static sb32 mmServerBarrierPostPingInternal(sb32 verbose)
{
	char match_id[72];
	char ticket_id[72];
	char path[320];
	char body[512];
	char *resp;
	long hc;
	u64 deadline_ms;
	u64 epoch;
	sb32 complete;
	sb32 ok;

	if (mmServerBarrierEnvEnabled() == FALSE)
	{
		return FALSE;
	}
	if ((mmMatchmakingLoadCredentials(FALSE) == FALSE))
	{
		return FALSE;
	}
	if (syNetPeerGetAutomatchBootstrapContext(match_id, (u32)sizeof(match_id), ticket_id, (u32)sizeof(ticket_id)) ==
	    FALSE)
	{
		return FALSE;
	}
	snprintf(path, sizeof(path), "/v1/sessions/%s/bootstrap/ping", match_id);
	snprintf(body, sizeof(body), "{\"ticket_id\":\"%s\",\"client_ping_seq\":%u}", ticket_id,
	         (unsigned int)sBarrierPingSeq);
	sBarrierPingSeq++;
	hc = mmMatchmakingHttpsRequest("POST", path, body, verbose, &resp);
	if ((hc != 200) || (resp == NULL))
	{
		if ((sBarrierFallbackLogged == FALSE) || (verbose != FALSE))
		{
			port_log("SSB64 NetPeer: server bootstrap ping failed HTTP %ld (local barrier schedule)\n", hc);
			sBarrierFallbackLogged = TRUE;
		}
		if (resp != NULL)
		{
			free(resp);
		}
		return FALSE;
	}
	complete = FALSE;
	deadline_ms = 0U;
	epoch = 0U;
	(void)mmMatchmakingJsonCopyBoolField(resp, "contract_complete", &complete);
	(void)mmMatchmakingJsonCopyU64Field(resp, "barrier_deadline_unix_ms", &deadline_ms);
	(void)mmMatchmakingJsonCopyU64Field(resp, "bootstrap_epoch", &epoch);
	if (verbose != FALSE)
	{
		port_log(
		    "SSB64 NetPeer: server bootstrap ping ok complete=%d deadline_ms=%llu epoch=%llu\n",
		    (int)complete, (unsigned long long)deadline_ms, (unsigned long long)epoch);
	}
	free(resp);
	return complete;
}

void mmServerBarrierPostPing(void)
{
	(void)mmServerBarrierPostPingInternal(mmServerBarrierVerbose());
}

sb32 mmServerBarrierTryApplyHostSchedule(u64 *io_start_ms_raw, u64 *io_quantized_start_ms)
{
	char match_id[72];
	char ticket_id[72];
	char path[320];
	char body[512];
	char *resp;
	long hc;
	u64 deadline_ms;
	sb32 complete;
	sb32 verbose;

	if ((io_start_ms_raw == NULL) || (io_quantized_start_ms == NULL))
	{
		return FALSE;
	}
	if (mmServerBarrierEnvEnabled() == FALSE)
	{
		return FALSE;
	}
	verbose = mmServerBarrierVerbose();
	if ((mmMatchmakingLoadCredentials(FALSE) == FALSE))
	{
		if (sBarrierFallbackLogged == FALSE)
		{
			port_log("SSB64 NetPeer: server bootstrap enabled but matchmaking cred unavailable\n");
			sBarrierFallbackLogged = TRUE;
		}
		return FALSE;
	}
	if (syNetPeerGetAutomatchBootstrapContext(match_id, (u32)sizeof(match_id), ticket_id, (u32)sizeof(ticket_id)) ==
	    FALSE)
	{
		if (sBarrierFallbackLogged == FALSE)
		{
			port_log("SSB64 NetPeer: server bootstrap missing match_id/ticket (automatch context)\n");
			sBarrierFallbackLogged = TRUE;
		}
		return FALSE;
	}
	snprintf(path, sizeof(path), "/v1/sessions/%s/bootstrap/ping", match_id);
	snprintf(body, sizeof(body), "{\"ticket_id\":\"%s\",\"client_ping_seq\":%u}", ticket_id,
	         (unsigned int)sBarrierPingSeq);
	sBarrierPingSeq++;
	hc = mmMatchmakingHttpsRequest("POST", path, body, verbose, &resp);
	if ((hc != 200) || (resp == NULL))
	{
		if ((sBarrierFallbackLogged == FALSE) || (verbose != FALSE))
		{
			port_log("SSB64 NetPeer: server bootstrap apply failed HTTP %ld (keeping local schedule)\n", hc);
			sBarrierFallbackLogged = TRUE;
		}
		if (resp != NULL)
		{
			free(resp);
		}
		return FALSE;
	}
	complete = FALSE;
	deadline_ms = 0U;
	(void)mmMatchmakingJsonCopyBoolField(resp, "contract_complete", &complete);
	(void)mmMatchmakingJsonCopyU64Field(resp, "barrier_deadline_unix_ms", &deadline_ms);
	free(resp);
	if ((complete == FALSE) || (deadline_ms == 0U))
	{
		if (verbose != FALSE)
		{
			port_log("SSB64 NetPeer: server bootstrap contract incomplete (local schedule)\n");
		}
		return FALSE;
	}
	*io_start_ms_raw = deadline_ms;
	*io_quantized_start_ms = deadline_ms;
	port_log("SSB64 NetPeer: server bootstrap applied barrier_deadline_ms=%llu\n",
	         (unsigned long long)deadline_ms);
	sBarrierFallbackLogged = FALSE;
	return TRUE;
}

#endif /* PORT && SSB64_NETMENU */
