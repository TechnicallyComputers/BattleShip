#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Debug-mode NDJSON logger for agent session 3b7f79. */
void net_debug_agent_log_line(const char *hypothesis_id, const char *location, const char *message,
                              const char *data_json);

#ifdef __cplusplus
}
#endif
