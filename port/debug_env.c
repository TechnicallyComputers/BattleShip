#include "debug_env.h"

#include "port_log.h"

#include <ssb64_paths_capi.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#define DEBUG_ENV_FILENAME "debug.env"
#define DEBUG_ENV_LINE_MAX 1024
#define DEBUG_ENV_NAME_MAX 128
#define DEBUG_ENV_VALUE_MAX 512
#define DEBUG_ENV_PATH_MAX 768

static int debugEnvNameAllowed(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    if (strncmp(name, "SSB64_", 6) == 0) {
        return 1;
    }
    if (strcmp(name, "CURL_CA_BUNDLE") == 0 || strcmp(name, "SSL_CERT_FILE") == 0) {
        return 1;
    }
    return 0;
}

static void debugEnvTrim(char *s)
{
    char *start;
    char *end;

    if (s == NULL) {
        return;
    }

    start = s;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }

    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
}

static void debugEnvApply(const char *name, const char *value)
{
    if (!debugEnvNameAllowed(name) || value == NULL) {
        return;
    }

#if defined(_WIN32)
    if (GetEnvironmentVariableA(name, NULL, 0) != 0) {
        return;
    }
    if (SetEnvironmentVariableA(name, value) == 0) {
        port_log("SSB64: debug.env: failed to set %s\n", name);
    }
#else
    if (getenv(name) != NULL) {
        return;
    }
    if (setenv(name, value, 0) != 0) {
        port_log("SSB64: debug.env: failed to set %s\n", name);
    }
#endif
}

/** Returns 1 if a var was applied, 0 otherwise (blank, comment, or skipped). */
static int debugEnvParseLine(char *line, int *out_applied)
{
    char *eq;
    char *name;
    char *value;

    debugEnvTrim(line);
    if (line[0] == '\0' || line[0] == '#') {
        return 0;
    }

    if (strncmp(line, "export ", 7) == 0) {
        memmove(line, line + 7, strlen(line + 7) + 1);
        debugEnvTrim(line);
        if (line[0] == '\0') {
            return 0;
        }
    }

    eq = strchr(line, '=');
    if (eq == NULL) {
        port_log("SSB64: debug.env: skip line (no '='): %.80s\n", line);
        return 0;
    }

    *eq = '\0';
    name = line;
    value = eq + 1;
    debugEnvTrim(name);
    debugEnvTrim(value);

    if (value[0] == '"' && value[strlen(value) - 1] == '"' && strlen(value) >= 2) {
        value[strlen(value) - 1] = '\0';
        value++;
    } else if (value[0] == '\'' && value[strlen(value) - 1] == '\'' && strlen(value) >= 2) {
        value[strlen(value) - 1] = '\0';
        value++;
    }

    if (!debugEnvNameAllowed(name)) {
        port_log("SSB64: debug.env: skip disallowed name: %s\n", name);
        return 0;
    }

    if (strlen(name) >= DEBUG_ENV_NAME_MAX || strlen(value) >= DEBUG_ENV_VALUE_MAX) {
        port_log("SSB64: debug.env: skip oversize entry: %s\n", name);
        return 0;
    }

    debugEnvApply(name, value);
    if (out_applied != NULL) {
        (*out_applied)++;
    }
    return 1; /* applied */
}

static int debugEnvBuildPath(char *out, size_t cap)
{
    char base[512];
    size_t len;
    const char *slash;

    if (ssb64_UserDataDirUtf8(base, sizeof(base)) == 0 || base[0] == '\0') {
        return 0;
    }

    len = strlen(base);
    slash = (len > 0 && (base[len - 1] == '/' || base[len - 1] == '\\')) ? "" : "/";
    if (snprintf(out, cap, "%s%s%s", base, slash, DEBUG_ENV_FILENAME) >= (int)cap) {
        return 0;
    }
    return 1;
}

void ssb64_load_debug_env_file(void)
{
    char path[DEBUG_ENV_PATH_MAX];
    FILE *fp;
    char line[DEBUG_ENV_LINE_MAX];
    int applied = 0;
    int skipped = 0;

    if (debugEnvBuildPath(path, sizeof(path)) == 0) {
        return;
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        return;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (debugEnvParseLine(line, &applied)) {
            continue;
        }
        debugEnvTrim(line);
        if (line[0] != '\0' && line[0] != '#') {
            skipped++;
        }
    }

    fclose(fp);
    port_log("SSB64: loaded debug.env (%d vars applied, %d lines skipped) from %s\n", applied,
             skipped, path);
}
