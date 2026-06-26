#include "port_log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

static FILE *sLogRegular = NULL;
static FILE *sLogDebug = NULL;
static char sLogRegularPath[512];
static char sLogDebugPath[512];
static port_log_sink_t sActiveSink = PORT_LOG_SINK_REGULAR;
#if defined(__ANDROID__)
static int sAndroidLogcatMirror = 0;
#endif

static void portLogStorePath(char *dst, size_t cap, const char *path)
{
    if (dst == NULL || cap == 0U) {
        return;
    }
    dst[0] = '\0';
    if (path != NULL && path[0] != '\0') {
        snprintf(dst, cap, "%s", path);
    }
}

static void portLogOpen(FILE **slot, const char *path, const char *mode)
{
    if (slot == NULL || path == NULL || *slot != NULL) {
        return;
    }
    *slot = fopen(path, mode);
}

#if defined(__ANDROID__)
static void portLogMirrorAndroidLogcat(const char *fmt, va_list ap)
{
    char line[1024];
    int n;

    if (fmt == NULL) {
        return;
    }
    n = vsnprintf(line, sizeof(line), fmt, ap);
    if (n < 0) {
        return;
    }
    if ((size_t)n >= sizeof(line)) {
        n = (int)sizeof(line) - 1;
        line[n] = '\0';
    }
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
        line[--n] = '\0';
    }
    if (n > 0) {
        __android_log_print(ANDROID_LOG_INFO, "ssb64", "%s", line);
    }
}
#endif

static void portLogEmit(FILE *fp, const char *fmt, va_list ap)
{
    if (fp == NULL || fmt == NULL) {
        return;
    }
    vfprintf(fp, fmt, ap);
    fflush(fp);
}

void port_log_init_regular(const char *path)
{
    portLogOpen(&sLogRegular, path, "w");
    portLogStorePath(sLogRegularPath, sizeof(sLogRegularPath), path);
    sActiveSink = PORT_LOG_SINK_REGULAR;
}

void port_log_init_regular_append(const char *path)
{
    portLogOpen(&sLogRegular, path, "a");
    portLogStorePath(sLogRegularPath, sizeof(sLogRegularPath), path);
}

void port_log_init_debug(const char *path)
{
    portLogOpen(&sLogDebug, path, "w");
    portLogStorePath(sLogDebugPath, sizeof(sLogDebugPath), path);
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

int port_log_debug_active(void)
{
    return (sActiveSink == PORT_LOG_SINK_DEBUG && sLogDebug != NULL) ? 1 : 0;
}

#if defined(__ANDROID__)
void port_log_set_android_logcat_mirror(int enable)
{
    sAndroidLogcatMirror = (enable != 0) ? 1 : 0;
}
#endif

void port_log_report_sinks(void)
{
    port_log("SSB64: port_log active_sink=%s\n",
             (sActiveSink == PORT_LOG_SINK_DEBUG) ? "debug" : "regular");
    if (sLogDebugPath[0] != '\0') {
        port_log("SSB64: port_log debug file %s (%s)\n", sLogDebugPath,
                 (sLogDebug != NULL) ? "open" : "FAILED");
    }
    if (sLogRegularPath[0] != '\0') {
        port_log("SSB64: port_log regular file %s (%s)\n", sLogRegularPath,
                 (sLogRegular != NULL) ? "open" : "FAILED");
    }
    if (port_log_debug_active() && sLogRegular != NULL) {
        port_log("SSB64: port_log mirroring connectivity lines to ssb64.log (append)\n");
    }
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
    sLogRegularPath[0] = '\0';
    sLogDebugPath[0] = '\0';
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
	if (sLogFile == NULL) return;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(sLogFile, fmt, ap);
	va_end(ap);
	/* fflush on every call costs seconds per frame on a slow drive when
	 * figatree watchdogs fire 28x per frame during a stuck APPEAR. Rely on
	 * stdio's buffer + OS-on-exit flush for normal logging; crash dumps
	 * have their own flush path. */
}
