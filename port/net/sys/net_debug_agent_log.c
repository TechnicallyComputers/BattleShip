#include <sys/net_debug_agent_log.h>

#include <stdio.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/time.h>
#endif

#define NET_DEBUG_AGENT_LOG_PATH "/home/alex/Documents/GitHub/BattleShip/.cursor/debug-3b7f79.log"

static long long net_debug_agent_log_timestamp_ms(void)
{
#ifdef _WIN32
	FILETIME ft;
	ULARGE_INTEGER uli;

	GetSystemTimeAsFileTime(&ft);
	uli.LowPart = ft.dwLowDateTime;
	uli.HighPart = ft.dwHighDateTime;
	uli.QuadPart -= 116444736000000000ULL;
	return (long long)(uli.QuadPart / 10000ULL);
#else
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (long long)tv.tv_sec * 1000LL + (long long)(tv.tv_usec / 1000);
#endif
}

void net_debug_agent_log_line(const char *hypothesis_id, const char *location, const char *message,
                              const char *data_json)
{
	FILE *f;
	long long ts;

	if ((hypothesis_id == NULL) || (location == NULL) || (message == NULL))
	{
		return;
	}
	ts = net_debug_agent_log_timestamp_ms();
	f = fopen(NET_DEBUG_AGENT_LOG_PATH, "a");
	if (f == NULL)
	{
		return;
	}
	fprintf(f,
	        "{\"sessionId\":\"3b7f79\",\"hypothesisId\":\"%s\",\"location\":\"%s\",\"message\":\"%s\",\"data\":%s,"
	        "\"timestamp\":%lld,\"runId\":\"pre-fix\"}\n",
	        hypothesis_id, location, message, (data_json != NULL) ? data_json : "{}", ts);
	fclose(f);
}
