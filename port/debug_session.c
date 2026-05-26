#include "debug_session.h"

#include <stdio.h>
#include <string.h>

#define DEBUG_SESSION_FILENAME ".battleship_debug_session"
#define DEBUG_SESSION_LINE_MAX 64

static void trim(char *s)
{
    char *start;
    char *end;

    if (s == NULL || s[0] == '\0') {
        return;
    }
    start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        end--;
    }
    *end = '\0';
}

ssb64_debug_session_kind ssb64_consume_debug_session(const char *user_dir)
{
    char path[512];
    char line[DEBUG_SESSION_LINE_MAX];
    FILE *fp;
    size_t len;
    const char *slash;
    ssb64_debug_session_kind kind = SSB64_DEBUG_SESSION_NONE;

    if (user_dir == NULL || user_dir[0] == '\0') {
        return SSB64_DEBUG_SESSION_NONE;
    }

    len = strlen(user_dir);
    slash = (len > 0 && (user_dir[len - 1] == '/' || user_dir[len - 1] == '\\')) ? "" : "/";
    if (snprintf(path, sizeof(path), "%s%s%s", user_dir, slash, DEBUG_SESSION_FILENAME) >= (int)sizeof(path)) {
        return SSB64_DEBUG_SESSION_NONE;
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        return SSB64_DEBUG_SESSION_NONE;
    }

    line[0] = '\0';
    if (fgets(line, sizeof(line), fp) != NULL) {
        trim(line);
        if (strcmp(line, "log_only") == 0) {
            kind = SSB64_DEBUG_SESSION_LOG_ONLY;
        } else if (strcmp(line, "env") == 0) {
            kind = SSB64_DEBUG_SESSION_ENV;
        }
    }
    fclose(fp);
    remove(path);
    return kind;
}
