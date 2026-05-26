#pragma once

/**
 * port_log.h — Unified crash-safe logging for the SSB64 PC port.
 *
 * All port logging goes through port_log(). Output is written to the active
 * sink with immediate fflush after every write. On Android, regular launches use
 * ssb64.log; explicit debug sessions use ssb64-debug.log only.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum port_log_sink {
    PORT_LOG_SINK_REGULAR = 0,
    PORT_LOG_SINK_DEBUG = 1,
} port_log_sink_t;

/* Open ssb64.log (truncate). Desktop default; Android normal launch. */
void port_log_init_regular(const char *path);

/* Open ssb64-debug.log (truncate). Android debug session only. */
void port_log_init_debug(const char *path);

/* Legacy single-file init (same as port_log_init_regular). */
void port_log_init(const char *path);

void port_log_set_active(port_log_sink_t sink);

port_log_sink_t port_log_get_active(void);

void port_log_close(void);

int port_log_get_fd(void);

#ifdef __GNUC__
__attribute__((format(printf, 1, 2)))
#endif
void port_log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif
