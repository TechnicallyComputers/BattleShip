#include "port_log.h"

#include <stdio.h>
#include <stdarg.h>

static FILE *sLogRegular = NULL;
static FILE *sLogDebug = NULL;
static port_log_sink_t sActiveSink = PORT_LOG_SINK_REGULAR;

static void portLogOpen(FILE **slot, const char *path)
{
    if (slot == NULL || path == NULL || *slot != NULL) {
        return;
    }
    *slot = fopen(path, "w");
}

void port_log_init_regular(const char *path)
{
    portLogOpen(&sLogRegular, path);
    sActiveSink = PORT_LOG_SINK_REGULAR;
}

void port_log_init_debug(const char *path)
{
    portLogOpen(&sLogDebug, path);
    sActiveSink = PORT_LOG_SINK_DEBUG;
}

void port_log_init(const char *path)
{
    port_log_init_regular(path);
}

void port_log_set_active(port_log_sink_t sink)
{
    sActiveSink = sink;
}

port_log_sink_t port_log_get_active(void)
{
    return sActiveSink;
}

void port_log_close(void)
{
    if (sLogRegular != NULL) {
        fclose(sLogRegular);
        sLogRegular = NULL;
    }
    if (sLogDebug != NULL) {
        fclose(sLogDebug);
        sLogDebug = NULL;
    }
}

int port_log_get_fd(void)
{
    FILE *fp = (sActiveSink == PORT_LOG_SINK_DEBUG) ? sLogDebug : sLogRegular;
    if (fp == NULL) {
        return -1;
    }
    return fileno(fp);
}

void port_log(const char *fmt, ...)
{
    FILE *fp = (sActiveSink == PORT_LOG_SINK_DEBUG) ? sLogDebug : sLogRegular;
    if (fp == NULL) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fflush(fp);
}
