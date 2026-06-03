#include <sys/net_debug_agent_log.h>

#include <stdio.h>
#include <sys/time.h>

#define NET_DEBUG_AGENT_LOG_PATH "/home/alex/Documents/GitHub/BattleShip/.cursor/debug-3b7f79.log"

void net_debug_agent_log_line(const char *hypothesis_id, const char *location, const char *message,
                              const char *data_json)
{
	struct timeval tv;
	FILE *f;
	long long ts;

	if ((hypothesis_id == NULL) || (location == NULL) || (message == NULL))
	{
		return;
	}
	gettimeofday(&tv, NULL);
	ts = (long long)tv.tv_sec * 1000LL + (long long)(tv.tv_usec / 1000);
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
