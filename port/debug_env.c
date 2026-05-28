#include "debug_env.h"

#include "port_log.h"

#include <ssb64_paths_capi.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <errno.h>
#endif

#define DEBUG_ENV_FILENAME "debug.env"
#define DEBUG_ENV_LINE_MAX 1024
#define DEBUG_ENV_NAME_MAX 128
#define DEBUG_ENV_VALUE_MAX 512
#define DEBUG_ENV_PATH_MAX 768
#define DEBUG_ENV_LOG_VALUE_MAX 96

typedef enum DebugEnvApplyResult
{
	DEBUG_ENV_APPLY_DISALLOWED = 0,
	DEBUG_ENV_APPLY_SKIPPED_ALREADY,
	DEBUG_ENV_APPLY_SET_OK,
	DEBUG_ENV_APPLY_SET_FAILED,
} DebugEnvApplyResult;

static int debugEnvNameAllowed(const char *name)
{
	if (name == NULL || name[0] == '\0')
	{
		return 0;
	}
	if (strncmp(name, "SSB64_", 6) == 0)
	{
		return 1;
	}
	if (strcmp(name, "CURL_CA_BUNDLE") == 0 || strcmp(name, "SSL_CERT_FILE") == 0)
	{
		return 1;
	}
	return 0;
}

static void debugEnvTrim(char *s)
{
	char *start;
	char *end;

	if (s == NULL)
	{
		return;
	}

	start = s;
	while (*start != '\0' && isspace((unsigned char)*start))
	{
		start++;
	}
	if (start != s)
	{
		memmove(s, start, strlen(start) + 1);
	}

	end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
	{
		end--;
	}
	*end = '\0';
}

static DebugEnvApplyResult debugEnvApply(const char *name, const char *value)
{
	if (!debugEnvNameAllowed(name) || value == NULL)
	{
		return DEBUG_ENV_APPLY_DISALLOWED;
	}

#if defined(_WIN32)
	{
		char prev[DEBUG_ENV_LOG_VALUE_MAX];
		DWORD prev_len;

		prev_len = GetEnvironmentVariableA(name, prev, (DWORD)sizeof(prev));
		if (prev_len != 0U)
		{
			if (prev_len >= (DWORD)sizeof(prev))
			{
				snprintf(prev, sizeof(prev), "(len=%lu)", (unsigned long)prev_len);
			}
			port_log("SSB64: debug.env: skip %s (already in environment, was=%s)\n", name, prev);
			return DEBUG_ENV_APPLY_SKIPPED_ALREADY;
		}
		if (SetEnvironmentVariableA(name, value) == 0)
		{
			port_log("SSB64: debug.env: failed to set %s (Win32 error=%lu)\n", name,
			         (unsigned long)GetLastError());
			return DEBUG_ENV_APPLY_SET_FAILED;
		}
	}
#else
	{
		const char *existing;

		existing = getenv(name);
		if (existing != NULL)
		{
			port_log("SSB64: debug.env: skip %s (already in environment, was=%s)\n", name, existing);
			return DEBUG_ENV_APPLY_SKIPPED_ALREADY;
		}
		if (setenv(name, value, 0) != 0)
		{
			port_log("SSB64: debug.env: failed to set %s (errno=%d)\n", name, errno);
			return DEBUG_ENV_APPLY_SET_FAILED;
		}
	}
#endif
	{
		char val_buf[DEBUG_ENV_LOG_VALUE_MAX];

		snprintf(val_buf, sizeof(val_buf), "%s", value);
		port_log("SSB64: debug.env: set %s=%s\n", name, val_buf);
	}
	return DEBUG_ENV_APPLY_SET_OK;
}

/** Returns 1 if line was a parsed assignment, 0 for blank/comment. */
static int debugEnvParseLine(char *line, int *out_set, int *out_skipped_already, int *out_failed)
{
	char *eq;
	char *name;
	char *value;
	DebugEnvApplyResult ar;

	debugEnvTrim(line);
	if (line[0] == '\0' || line[0] == '#')
	{
		return 0;
	}

	if (strncmp(line, "export ", 7) == 0)
	{
		memmove(line, line + 7, strlen(line + 7) + 1);
		debugEnvTrim(line);
		if (line[0] == '\0')
		{
			return 0;
		}
	}

	eq = strchr(line, '=');
	if (eq == NULL)
	{
		port_log("SSB64: debug.env: skip line (no '='): %.80s\n", line);
		return 0;
	}

	*eq = '\0';
	name = line;
	value = eq + 1;
	debugEnvTrim(name);
	debugEnvTrim(value);

	if (value[0] == '"' && value[strlen(value) - 1] == '"' && strlen(value) >= 2)
	{
		value[strlen(value) - 1] = '\0';
		value++;
	}
	else if (value[0] == '\'' && value[strlen(value) - 1] == '\'' && strlen(value) >= 2)
	{
		value[strlen(value) - 1] = '\0';
		value++;
	}

	if (!debugEnvNameAllowed(name))
	{
		port_log("SSB64: debug.env: skip disallowed name: %s\n", name);
		return 0;
	}

	if (strlen(name) >= DEBUG_ENV_NAME_MAX || strlen(value) >= DEBUG_ENV_VALUE_MAX)
	{
		port_log("SSB64: debug.env: skip oversize entry: %s\n", name);
		return 0;
	}

	ar = debugEnvApply(name, value);
	switch (ar)
	{
	case DEBUG_ENV_APPLY_SET_OK:
		if (out_set != NULL)
		{
			(*out_set)++;
		}
		break;
	case DEBUG_ENV_APPLY_SKIPPED_ALREADY:
		if (out_skipped_already != NULL)
		{
			(*out_skipped_already)++;
		}
		break;
	case DEBUG_ENV_APPLY_SET_FAILED:
		if (out_failed != NULL)
		{
			(*out_failed)++;
		}
		break;
	default:
		break;
	}
	return 1;
}

static int debugEnvBuildPath(char *out, size_t cap)
{
	char base[512];
	size_t len;
	const char *slash;

	if (ssb64_UserDataDirUtf8(base, sizeof(base)) == 0 || base[0] == '\0')
	{
		return 0;
	}

	len = strlen(base);
	slash = (len > 0 && (base[len - 1] == '/' || base[len - 1] == '\\')) ? "" : "/";
	if (snprintf(out, cap, "%s%s%s", base, slash, DEBUG_ENV_FILENAME) >= (int)cap)
	{
		return 0;
	}
	return 1;
}

void ssb64_load_debug_env_file(void)
{
	char path[DEBUG_ENV_PATH_MAX];
	FILE *fp;
	char line[DEBUG_ENV_LINE_MAX];
	int set_count = 0;
	int skipped_already = 0;
	int failed = 0;
	int bad_lines = 0;
	int parsed_lines = 0;

	if (debugEnvBuildPath(path, sizeof(path)) == 0)
	{
		return;
	}

	fp = fopen(path, "r");
	if (fp == NULL)
	{
		return;
	}

	port_log("SSB64: loading debug.env from %s\n", path);

	while (fgets(line, sizeof(line), fp) != NULL)
	{
		if (debugEnvParseLine(line, &set_count, &skipped_already, &failed) != 0)
		{
			parsed_lines++;
			continue;
		}
		debugEnvTrim(line);
		if (line[0] != '\0' && line[0] != '#')
		{
			bad_lines++;
		}
	}

	fclose(fp);
	port_log(
	    "SSB64: debug.env done: %d set, %d skipped (already in environment), %d failed to set, %d malformed "
	    "lines (%d assignments parsed) — file does not override pre-set variables\n",
	    set_count, skipped_already, failed, bad_lines, parsed_lines);
}
