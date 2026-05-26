#include "mm_matchmaking.h"

#if defined(PORT) && defined(SSB64_NETMENU)

#include <macros.h>

#include <ssb64_paths_capi.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
/* Before curl/winsock2 pull in <windows.h> → <stralign.h> needs _wcsicmp from <wchar.h>. */
#include <wchar.h>
#endif

#include <curl/curl.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
/* decomp/src/netplay/include/stdlib.h shadows decomp/include before the system header; use include_next for POSIX. */
#ifdef _WIN32
#include <stdlib.h>
#else
#if defined(__GNUC__) || defined(__clang__)
#include_next <stdlib.h>
#else
#include <stdlib.h>
#endif
#endif
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <errno.h>
#include <sys/stat.h>
#include <windows.h>
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
static int mm_clock_gettime(int clk_id, struct timespec *ts)
{
	ULARGE_INTEGER uli;
	FILETIME ft;

	(void)clk_id;
	if (ts == NULL)
	{
		return -1;
	}
	GetSystemTimePreciseAsFileTime(&ft);
	uli.LowPart = ft.dwLowDateTime;
	uli.HighPart = ft.dwHighDateTime;
	uli.QuadPart -= 116444736000000000ULL;
	ts->tv_sec = (long)(uli.QuadPart / 10000000ULL);
	ts->tv_nsec = (long)((uli.QuadPart % 10000000ULL) * 100ULL);
	return 0;
}
#define clock_gettime(clk, ts) mm_clock_gettime((clk), (ts))
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef PORT
extern void port_log(const char *fmt, ...);
#endif

#ifndef MM_DEFAULT_BASE_URL
#define MM_DEFAULT_BASE_URL "https://netplay.technicallycomputers.ca"
#endif
/* HTTPS jobs queue: producer can outpace single worker under high RTT — keep headroom. */
#define MM_JOB_QUEUE_DEPTH 64
/* Completed events: may burst after slow requests; larger than job queue; never drop MATCHED/ERROR lightly. */
#define MM_DONE_QUEUE_DEPTH 128
#define MM_CRED_FILENAME "matchmaking.cred"

typedef enum MmJobKind
{
	MM_JOB_NONE = 0,
	MM_JOB_ENSURE_PLAYER,
	MM_JOB_JOIN_QUEUE,
	MM_JOB_HEARTBEAT,
	MM_JOB_POLL_MATCH,
	MM_JOB_CANCEL,
#if defined(SSB64_NETPLAY_ICE)
	MM_JOB_ICE_SIGNAL,
#endif
} MmJobKind;

typedef struct MmJob
{
	MmJobKind kind;
	sb32 verbose;
	char udp_endpoint[128];
	char lan_endpoint[128];
	sb32 has_lan_endpoint;
	char ticket_id[64];
	u8 fighter_kind;
	sb32 has_fighter_kind;
	sb32 heartbeat_has_endpoints;
	char turn_endpoint[128];
	sb32 has_turn_endpoint;
#if defined(SSB64_NETPLAY_ICE)
	char ice_sdp[4096];
	sb32 has_ice_sdp;
	char ice_candidate[280];
#endif
} MmJob;

typedef struct MmMemBuf
{
	char *data;
	size_t len;
} MmMemBuf;

static pthread_t sWorkerThread;
static pthread_mutex_t sMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t sDoneMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sCond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t sDoneNotFull = PTHREAD_COND_INITIALIZER;
static sb32 sWorkerRunning;
static sb32 sWorkerSpawned;
/* Mirrored for mmPushDone wait without holding sMutex (shutdown wake). */
static volatile sb32 sMmWorkerRunningForDoneWait;

static MmJob sJobQ[MM_JOB_QUEUE_DEPTH];
static u32 sJobHead;
static u32 sJobTail;
static u32 sJobCount;

static MmMatchResult sDoneQ[MM_DONE_QUEUE_DEPTH];
static u32 sDoneHead;
static u32 sDoneTail;
static u32 sDoneCount;

static char sBaseUrl[192];
static char sPlayerId[48];
static char sApiToken[192];

static sb32 mmPollKindIsCritical(MmPollKind k)
{
	switch (k)
	{
	case MM_POLL_MATCHED:
	case MM_POLL_ERROR:
	case MM_POLL_PLAYER_READY:
	case MM_POLL_QUEUED:
	case MM_POLL_CANCEL_OK:
		return TRUE;
	default:
		return FALSE;
	}
}

static void mmDoneEvictDisposableAtHeadLocked(void)
{
	while ((sDoneCount > 0U) && (sDoneQ[sDoneHead].kind == MM_POLL_HEARTBEAT_OK))
	{
		sDoneHead = (sDoneHead + 1U) % MM_DONE_QUEUE_DEPTH;
		sDoneCount--;
	}
}

/*
 * Reserve one slot for `incoming`. Evicts heartbeat completions first; critical results may wait
 * for the main thread to drain under sDoneMutex (worker releases mutex while waiting).
 */
static sb32 mmDoneQueueReserveSlotLocked(const MmMatchResult *incoming)
{
	struct timespec ts;

	for (;;)
	{
		mmDoneEvictDisposableAtHeadLocked();
		if (sDoneCount < MM_DONE_QUEUE_DEPTH)
		{
			return TRUE;
		}
		if (mmPollKindIsCritical(incoming->kind) == FALSE)
		{
			return FALSE;
		}
		if (sMmWorkerRunningForDoneWait == FALSE)
		{
#ifdef PORT
			port_log("SSB64 Matchmaking: shutdown: dropping critical done event (queue full)\n");
#endif
			return FALSE;
		}
		if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
		{
			(void)pthread_cond_wait(&sDoneNotFull, &sDoneMutex);
			continue;
		}
		ts.tv_nsec += 100 * 1000000L;
		if (ts.tv_nsec >= 1000000000L)
		{
			ts.tv_sec++;
			ts.tv_nsec -= 1000000000L;
		}
		(void)pthread_cond_timedwait(&sDoneNotFull, &sDoneMutex, &ts);
	}
}

static void mmPushDone(const MmMatchResult *r)
{
	if (r == NULL)
	{
		return;
	}
	pthread_mutex_lock(&sDoneMutex);
	if (mmDoneQueueReserveSlotLocked(r) == FALSE)
	{
		pthread_mutex_unlock(&sDoneMutex);
#ifdef PORT
		if (r->kind != MM_POLL_HEARTBEAT_OK)
		{
			port_log("SSB64 Matchmaking: dropped completed event kind=%d (overflow)\n", (int)r->kind);
		}
#endif
		return;
	}
	sDoneQ[sDoneTail] = *r;
	sDoneTail = (sDoneTail + 1U) % MM_DONE_QUEUE_DEPTH;
	sDoneCount++;
	pthread_mutex_unlock(&sDoneMutex);
}

static void mmPushDoneError(long http_status, const char *msg)
{
	MmMatchResult r;

#ifdef PORT
	port_log("SSB64 Matchmaking: error HTTP %ld %s\n", http_status, (msg != NULL) ? msg : "");
#endif
	memset(&r, 0, sizeof(r));
	r.kind = MM_POLL_ERROR;
	r.http_status = http_status;
	if (msg != NULL)
	{
		snprintf(r.error_message, sizeof(r.error_message), "%s", msg);
	}
	mmPushDone(&r);
}

static size_t mmCurlWriteMem(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t reals = size * nmemb;
	MmMemBuf *mem = (MmMemBuf*)userp;
	char *np = realloc(mem->data, mem->len + reals + 1U);

	if (np == NULL)
	{
		return 0;
	}
	mem->data = np;
	memcpy(mem->data + mem->len, contents, reals);
	mem->len += reals;
	mem->data[mem->len] = '\0';
	return reals;
}

static void mmMemBufFree(MmMemBuf *m)
{
	if (m->data != NULL)
	{
		free(m->data);
		m->data = NULL;
	}
	m->len = 0;
}

#ifdef _WIN32
static sb32 mmWinUtf8ToWide(const char *utf8, wchar_t *out, size_t out_wchars)
{
	int need;

	if ((utf8 == NULL) || (out == NULL) || (out_wchars == 0U))
	{
		return FALSE;
	}
	need = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, (int)out_wchars);
	return (need > 0) && ((size_t)need <= out_wchars);
}

static sb32 mmWinWideToUtf8(const wchar_t *wide, char *out, size_t out_bytes)
{
	int need;

	if ((wide == NULL) || (out == NULL) || (out_bytes == 0U))
	{
		return FALSE;
	}
	need = WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, (int)out_bytes, NULL, NULL);
	return (need > 0) && ((size_t)need <= out_bytes);
}
#endif

static sb32 mmFileReadable(const char *path)
{
	if ((path == NULL) || (path[0] == '\0'))
	{
		return FALSE;
	}
#ifdef _WIN32
	{
		wchar_t wpath[512];
		DWORD attr;

		if (mmWinUtf8ToWide(path, wpath, sizeof(wpath) / sizeof(wpath[0])) == FALSE)
		{
			return FALSE;
		}
		attr = GetFileAttributesW(wpath);
		if (attr == INVALID_FILE_ATTRIBUTES)
		{
			return FALSE;
		}
		if ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0U)
		{
			return FALSE;
		}
		return TRUE;
	}
#else
	{
		struct stat st;

		if (stat(path, &st) != 0)
		{
			return FALSE;
		}
		return S_ISREG(st.st_mode);
	}
#endif
}

static sb32 mmCaEnvUnset(const char *name)
{
	const char *v;

	if (name == NULL)
	{
		return TRUE;
	}
	v = getenv(name);
	return (v == NULL) || (v[0] == '\0');
}

static sb32 mmCaBundleAbsPath(const char *path, char *out, size_t cap)
{
#if defined(_WIN32)
	wchar_t wpath[512];
	wchar_t wout[512];
	DWORD n;

	if ((path == NULL) || (out == NULL) || (cap == 0U))
	{
		return FALSE;
	}
	if (mmWinUtf8ToWide(path, wpath, sizeof(wpath) / sizeof(wpath[0])) == FALSE)
	{
		goto fallback;
	}
	n = GetFullPathNameW(wpath, (DWORD)(sizeof(wout) / sizeof(wout[0])), wout, NULL);
	if ((n == 0U) || (n >= (DWORD)(sizeof(wout) / sizeof(wout[0]))))
	{
		goto fallback;
	}
	if (mmWinWideToUtf8(wout, out, cap) == FALSE)
	{
		return FALSE;
	}
	return TRUE;
fallback:
#else
	if (realpath(path, out) != NULL)
	{
		return TRUE;
	}
#endif
	if ((path != NULL) && (path[0] != '\0') && (strlen(path) + 1U <= cap))
	{
		memcpy(out, path, strlen(path) + 1U);
		return TRUE;
	}
	return FALSE;
}

static sb32 mmTryBundlePath(const char *path, char *out, size_t cap)
{
	char abs[512];

	if (mmFileReadable(path) == FALSE)
	{
		return FALSE;
	}
	if (mmCaBundleAbsPath(path, abs, sizeof(abs)) == FALSE)
	{
		return FALSE;
	}
	snprintf(out, cap, "%s", abs);
	return TRUE;
}

static sb32 mmResolveCaBundlePath(char *out, size_t cap)
{
	char base[384];
	char candidate[512];
	static const char *env_names[] = {
	    "SSB64_MATCHMAKING_CA_BUNDLE",
	    "CURL_CA_BUNDLE",
	    "SSL_CERT_FILE",
	};
	size_t ei;

	if ((out == NULL) || (cap == 0U))
	{
		return FALSE;
	}
	out[0] = '\0';

	for (ei = 0; ei < sizeof(env_names) / sizeof(env_names[0]); ei++)
	{
		const char *ca = getenv(env_names[ei]);

		if ((ca != NULL) && (ca[0] != '\0') && (mmTryBundlePath(ca, out, cap) != FALSE))
		{
			return TRUE;
		}
	}

	if (ssb64_RealAppBundlePathUtf8(base, sizeof(base)) == 0)
	{
		return FALSE;
	}

	snprintf(candidate, sizeof(candidate), "%s/ssl/cacert.pem", base);
	if (mmTryBundlePath(candidate, out, cap) != FALSE)
	{
		return TRUE;
	}
#ifdef _WIN32
	snprintf(candidate, sizeof(candidate), "%s\\ssl\\cacert.pem", base);
	if (mmTryBundlePath(candidate, out, cap) != FALSE)
	{
		return TRUE;
	}
#endif
	snprintf(candidate, sizeof(candidate), "%s/../share/BattleShip/ssl/cacert.pem", base);
	if (mmTryBundlePath(candidate, out, cap) != FALSE)
	{
		return TRUE;
	}
#ifdef _WIN32
	snprintf(candidate, sizeof(candidate), "%s\\..\\share\\BattleShip\\ssl\\cacert.pem", base);
	if (mmTryBundlePath(candidate, out, cap) != FALSE)
	{
		return TRUE;
	}
#endif
	return FALSE;
}

static void mmSetenvIfUnset(const char *name, const char *value)
{
	if ((name == NULL) || (value == NULL) || (value[0] == '\0') || (mmCaEnvUnset(name) == FALSE))
	{
		return;
	}
#if defined(_WIN32)
	{
		wchar_t wname[96];
		wchar_t wvalue[512];

		if (mmWinUtf8ToWide(name, wname, sizeof(wname) / sizeof(wname[0])) == FALSE)
		{
			return;
		}
		if (mmWinUtf8ToWide(value, wvalue, sizeof(wvalue) / sizeof(wvalue[0])) == FALSE)
		{
			return;
		}
		(void)SetEnvironmentVariableW(wname, wvalue);
	}
#else
	(void)setenv(name, value, 0);
#endif
}

static sb32 sCaBundleInitDone = FALSE;
static char sCaBundlePath[512];
static char *sCaBundleBlob = NULL;
static size_t sCaBundleBlobLen = 0U;
static struct curl_blob sCaBundleCurlBlob;

static sb32 mmLoadCaBundleFromPath(const char *path)
{
	char *buf = NULL;
	size_t blob_len = 0U;

#ifdef _WIN32
	{
		wchar_t wpath[512];
		HANDLE file;
		LARGE_INTEGER file_size;
		DWORD read_bytes;

		if ((path == NULL) || (path[0] == '\0'))
		{
			return FALSE;
		}
		if (mmWinUtf8ToWide(path, wpath, sizeof(wpath) / sizeof(wpath[0])) == FALSE)
		{
			return FALSE;
		}
		file = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE)
		{
			return FALSE;
		}
		if (GetFileSizeEx(file, &file_size) == 0)
		{
			CloseHandle(file);
			return FALSE;
		}
		if ((file_size.QuadPart <= 0LL) || (file_size.QuadPart > 8388608LL))
		{
			CloseHandle(file);
			return FALSE;
		}
		buf = (char *)malloc((size_t)file_size.QuadPart + 1U);
		if (buf == NULL)
		{
			CloseHandle(file);
			return FALSE;
		}
		if ((ReadFile(file, buf, (DWORD)file_size.QuadPart, &read_bytes, NULL) == 0) ||
		    (read_bytes != (DWORD)file_size.QuadPart))
		{
			free(buf);
			CloseHandle(file);
			return FALSE;
		}
		CloseHandle(file);
		buf[read_bytes] = '\0';
		blob_len = (size_t)read_bytes;
	}
#else
	{
		FILE *fp;
		long file_len;
		size_t got;

		fp = fopen(path, "rb");
		if (fp == NULL)
		{
			return FALSE;
		}
		if (fseek(fp, 0, SEEK_END) != 0)
		{
			fclose(fp);
			return FALSE;
		}
		file_len = ftell(fp);
		if ((file_len <= 0L) || (file_len > 8388608L))
		{
			fclose(fp);
			return FALSE;
		}
		if (fseek(fp, 0, SEEK_SET) != 0)
		{
			fclose(fp);
			return FALSE;
		}
		buf = (char *)malloc((size_t)file_len + 1U);
		if (buf == NULL)
		{
			fclose(fp);
			return FALSE;
		}
		got = fread(buf, 1U, (size_t)file_len, fp);
		fclose(fp);
		if (got != (size_t)file_len)
		{
			free(buf);
			return FALSE;
		}
		buf[got] = '\0';
		blob_len = got;
	}
#endif

	if (sCaBundleBlob != NULL)
	{
		free(sCaBundleBlob);
		sCaBundleBlob = NULL;
		sCaBundleBlobLen = 0U;
	}
	sCaBundleBlob = buf;
	sCaBundleBlobLen = blob_len;
	sCaBundleCurlBlob.data = sCaBundleBlob;
	sCaBundleCurlBlob.len = sCaBundleBlobLen;
	sCaBundleCurlBlob.flags = CURL_BLOB_NOCOPY;
	return TRUE;
}

static void mmMatchmakingInitPortableCaBundle(void)
{
	if (sCaBundleInitDone != FALSE)
	{
		return;
	}
	sCaBundleInitDone = TRUE;

	if (mmResolveCaBundlePath(sCaBundlePath, sizeof(sCaBundlePath)) == FALSE)
	{
#ifdef PORT
		port_log(
		    "SSB64 Matchmaking: WARNING no readable CA bundle (set SSB64_MATCHMAKING_CA_BUNDLE or place ssl/cacert.pem next to the exe)\n");
#endif
		return;
	}

	if (mmLoadCaBundleFromPath(sCaBundlePath) == FALSE)
	{
#ifdef PORT
		port_log("SSB64 Matchmaking: WARNING failed to load CA bundle from %s\n", sCaBundlePath);
#endif
		return;
	}

	mmSetenvIfUnset("SSB64_MATCHMAKING_CA_BUNDLE", sCaBundlePath);
	mmSetenvIfUnset("CURL_CA_BUNDLE", sCaBundlePath);
	mmSetenvIfUnset("SSL_CERT_FILE", sCaBundlePath);
#ifdef PORT
	port_log("SSB64 Matchmaking: CA bundle %s (%zu bytes, CURLOPT_CAINFO_BLOB)\n", sCaBundlePath,
	         sCaBundleBlobLen);
#endif
}

static void mmBaseUrlSetup(void)
{
	const char *env = getenv("SSB64_MATCHMAKING_BASE_URL");

	if ((env != NULL) && (env[0] != '\0'))
	{
		snprintf(sBaseUrl, sizeof(sBaseUrl), "%s", env);
	}
	else
	{
		snprintf(sBaseUrl, sizeof(sBaseUrl), "%s", MM_DEFAULT_BASE_URL);
	}
}

/* Portable builds bundle curl+OpenSSL but not the host CA store (Linux AppRun sets env; Windows loads PEM blob). */
static void mmCurlConfigureSsl(CURL *c)
{
	char bundle_path[512];
	const char *ca;

	if (c == NULL)
	{
		return;
	}

	curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);

	if ((sCaBundleBlob != NULL) && (sCaBundleBlobLen > 0U))
	{
		curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &sCaBundleCurlBlob);
		return;
	}

	if ((sCaBundlePath[0] != '\0') && (mmFileReadable(sCaBundlePath) != FALSE))
	{
		curl_easy_setopt(c, CURLOPT_CAINFO, sCaBundlePath);
		return;
	}

	if (mmResolveCaBundlePath(bundle_path, sizeof(bundle_path)) != FALSE)
	{
		curl_easy_setopt(c, CURLOPT_CAINFO, bundle_path);
		return;
	}

	ca = getenv("SSB64_MATCHMAKING_CA_BUNDLE");
	if ((ca == NULL) || (ca[0] == '\0'))
	{
		ca = getenv("CURL_CA_BUNDLE");
	}
	if ((ca == NULL) || (ca[0] == '\0'))
	{
		ca = getenv("SSL_CERT_FILE");
	}
	if ((ca != NULL) && (ca[0] != '\0'))
	{
		curl_easy_setopt(c, CURLOPT_CAINFO, ca);
		return;
	}

#ifdef PORT
	port_log("SSB64 Matchmaking: WARNING CA bundle not loaded — TLS verification may fail (OpenSSL curl)\n");
#endif
}

static void mmCredJoinPath(char *out, size_t cap, const char *dir, const char *filename)
{
	size_t dlen;

	if ((out == NULL) || (cap == 0U) || (dir == NULL) || (filename == NULL))
	{
		if ((out != NULL) && (cap > 0U))
		{
			out[0] = '\0';
		}
		return;
	}
	dlen = strlen(dir);
	if ((dlen > 0U) && ((dir[dlen - 1U] == '/') || (dir[dlen - 1U] == '\\')))
	{
		snprintf(out, cap, "%s%s", dir, filename);
	}
	else
	{
		snprintf(out, cap, "%s/%s", dir, filename);
	}
}

/* Same writable tree as ssb64.log (SDL pref path / Ship app directory). */
static void mmCredPath(char *out, size_t cap)
{
	char base[512];

	if ((ssb64_UserDataDirUtf8(base, sizeof(base)) != 0) && (base[0] != '\0'))
	{
		mmCredJoinPath(out, cap, base, MM_CRED_FILENAME);
		return;
	}
	snprintf(out, cap, "./%s", MM_CRED_FILENAME);
}

static sb32 mmCredPathLegacy(char *out, size_t cap)
{
#ifdef _WIN32
	const char *appdata = getenv("APPDATA");

	if ((appdata != NULL) && (appdata[0] != '\0'))
	{
		snprintf(out, cap, "%s\\ssb64\\%s", appdata, MM_CRED_FILENAME);
		return TRUE;
	}
#elif defined(__ANDROID__)
	char base[384];

	if ((ssb64_RealAppBundlePathUtf8(base, sizeof(base)) != 0) && (base[0] != '\0'))
	{
		mmCredJoinPath(out, cap, base, "ssb64/" MM_CRED_FILENAME);
		return TRUE;
	}
#else
	const char *xdg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");

	if ((xdg != NULL) && (xdg[0] != '\0'))
	{
		snprintf(out, cap, "%s/ssb64/%s", xdg, MM_CRED_FILENAME);
		return TRUE;
	}
	if ((home != NULL) && (home[0] != '\0'))
	{
		snprintf(out, cap, "%s/.config/ssb64/%s", home, MM_CRED_FILENAME);
		return TRUE;
	}
#endif
	(void)out;
	(void)cap;
	return FALSE;
}

static sb32 mmEnsureCredDir(const char *fullpath)
{
	char dir[512];
	const char *slash;
	size_t i;

	slash = strrchr(fullpath, '/');
#ifdef _WIN32
	{
		const char *bslash = strrchr(fullpath, '\\');

		if ((bslash != NULL) && ((slash == NULL) || (bslash > slash)))
		{
			slash = bslash;
		}
	}
#endif
	if (slash == NULL)
	{
		return TRUE;
	}
	if (((size_t)(slash - fullpath) + 1U) >= sizeof(dir))
	{
		return FALSE;
	}
	memcpy(dir, fullpath, (size_t)(slash - fullpath));
	dir[slash - fullpath] = '\0';
	for (i = 1; dir[i] != '\0'; i++)
	{
		if ((dir[i] == '/') || (dir[i] == '\\'))
		{
			dir[i] = '\0';
#ifdef _WIN32
			if (_mkdir(dir) != 0)
			{
				if (errno != EEXIST)
				{
					/* continue — parent may exist */
				}
			}
#else
			(void)mkdir(dir, 0700);
#endif
			dir[i] = slash[0];
		}
	}
#ifdef _WIN32
	if (_mkdir(dir) != 0)
#else
	if (mkdir(dir, 0700) != 0)
#endif
	{
		if (errno != EEXIST)
		{
#ifdef PORT
			port_log("SSB64 Matchmaking: mkdir errno=%d (%s)\n", errno, dir);
#endif
			return FALSE;
		}
	}
	return TRUE;
}

static void mmCredSave(void)
{
	FILE *fp;
	char path[512];

	if ((sPlayerId[0] == '\0') || (sApiToken[0] == '\0'))
	{
		return;
	}
	mmCredPath(path, sizeof(path));
	if (mmEnsureCredDir(path) == FALSE)
	{
		return;
	}
	fp = fopen(path, "w");
	if (fp == NULL)
	{
#ifdef PORT
		port_log("SSB64 Matchmaking: fopen cred errno=%d\n", errno);
#endif
		return;
	}
	fprintf(fp, "PLAYER_ID=%s\nAPI_TOKEN=%s\n", sPlayerId, sApiToken);
	fclose(fp);
}

static sb32 mmJsonCopyQuotedValue(const char *body, const char *key_name, char *out, size_t cap);
static long mmHttpsRequest(const char *method, const char *path_suffix, const char *json_body, sb32 verb,
                           char **resp_body_out);

static void mmCredClearMemory(void)
{
	sPlayerId[0] = '\0';
	sApiToken[0] = '\0';
}

static sb32 mmCredBackupCurrentFile(void)
{
	char path[512];
	char bak[560];
	FILE *in;
	FILE *out;
	char buf[256];
	size_t nread;

	mmCredPath(path, sizeof(path));
	in = fopen(path, "r");
	if (in == NULL)
	{
		return FALSE;
	}
	snprintf(bak, sizeof(bak), "%s.bak", path);
	out = fopen(bak, "w");
	if (out == NULL)
	{
		fclose(in);
#ifdef PORT
		port_log("SSB64 Matchmaking: cred backup fopen failed errno=%d\n", errno);
#endif
		return FALSE;
	}
	while ((nread = fread(buf, 1, sizeof(buf), in)) > 0U)
	{
		if (fwrite(buf, 1, nread, out) != nread)
		{
			fclose(in);
			fclose(out);
#ifdef PORT
			port_log("SSB64 Matchmaking: cred backup write failed\n");
#endif
			return FALSE;
		}
	}
	fclose(in);
	fclose(out);
#ifdef PORT
	port_log("SSB64 Matchmaking: backed up cred file -> %s\n", bak);
#endif
	return TRUE;
}

static sb32 mmCredShouldRepopulate(long http_code, const char *resp_body, sb32 not_found_means_stale)
{
	if ((http_code == 401) || (http_code == 403))
	{
		return TRUE;
	}
	if ((not_found_means_stale != FALSE) && (http_code == 404))
	{
		return TRUE;
	}
	if ((http_code == 400) && (resp_body != NULL))
	{
		if ((strstr(resp_body, "invalid player credentials") != NULL) ||
		    (strstr(resp_body, "invalid player") != NULL))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 mmCredRepopulate(sb32 verbose)
{
	char *resp;
	long hc;
	char pid[96];
	char tok[288];

	if ((sPlayerId[0] != '\0') || (sApiToken[0] != '\0'))
	{
		(void)mmCredBackupCurrentFile();
	}
	mmCredClearMemory();

	hc = mmHttpsRequest("POST", "/v1/players", "{}", verbose, &resp);
	if (((hc != 200) && (hc != 201)) || (resp == NULL))
	{
		if (resp != NULL)
		{
			free(resp);
		}
#ifdef PORT
		port_log("SSB64 Matchmaking: cred repopulate POST /v1/players failed HTTP %ld\n", hc);
#endif
		return FALSE;
	}
	if ((mmJsonCopyQuotedValue(resp, "player_id", pid, sizeof(pid)) == FALSE) ||
	    (mmJsonCopyQuotedValue(resp, "api_token", tok, sizeof(tok)) == FALSE))
	{
		free(resp);
#ifdef PORT
		port_log("SSB64 Matchmaking: cred repopulate JSON parse failed\n");
#endif
		return FALSE;
	}
	snprintf(sPlayerId, sizeof(sPlayerId), "%s", pid);
	snprintf(sApiToken, sizeof(sApiToken), "%s", tok);
	mmCredSave();
	free(resp);
#ifdef PORT
	port_log("SSB64 Matchmaking: repopulated player cred (new player_id %.8s...)\n", pid);
#endif
	return TRUE;
}

/* Heartbeat with unknown ticket: 404 means auth OK; 400/401 means stale cred file. */
static sb32 mmCredVerifyLoaded(sb32 verbose)
{
	char jbuf[256];
	char *resp;
	long hc;

	if ((sPlayerId[0] == '\0') || (sApiToken[0] == '\0'))
	{
		return FALSE;
	}
	snprintf(jbuf, sizeof(jbuf),
	         "{\"ticket_id\":\"00000000-0000-0000-0000-000000000000\",\"client_time_ms\":0,"
	         "\"last_server_rtt_ms\":0.0,\"jitter_ms\":0.0,\"loss_pct\":0.0}");
	hc = mmHttpsRequest("POST", "/v1/heartbeat", jbuf, verbose, &resp);
	if (hc == 404)
	{
		if (resp != NULL)
		{
			free(resp);
		}
		return TRUE;
	}
	if (mmCredShouldRepopulate(hc, resp, FALSE) != FALSE)
	{
		if (resp != NULL)
		{
			free(resp);
		}
#ifdef PORT
		port_log("SSB64 Matchmaking: cached cred rejected by server (HTTP %ld), repopulating\n", hc);
#endif
		return mmCredRepopulate(verbose);
	}
	if (resp != NULL)
	{
		free(resp);
	}
	return (hc == 200) ? TRUE : FALSE;
}

sb32 mmMatchmakingLoadCredentials(sb32 verbose)
{
	FILE *fp;
	char path[512];
	char loaded_from[512];
	char line[512];
	char key[96];
	char val[320];

	mmCredPath(path, sizeof(path));
	snprintf(loaded_from, sizeof(loaded_from), "%s", path);
	fp = fopen(path, "r");
	if (fp == NULL)
	{
		char legacy[512];

		if (mmCredPathLegacy(legacy, sizeof(legacy)) == FALSE)
		{
			return FALSE;
		}
		fp = fopen(legacy, "r");
		if (fp == NULL)
		{
			return FALSE;
		}
		snprintf(loaded_from, sizeof(loaded_from), "%s", legacy);
#ifdef PORT
		port_log("SSB64 Matchmaking: loaded cred from legacy path (will migrate)\n");
#endif
	}
	while (fgets(line, sizeof(line), fp) != NULL)
	{
		if (sscanf(line, " %95[^=]=%319s", key, val) != 2)
		{
			continue;
		}
		if (strcmp(key, "PLAYER_ID") == 0)
		{
			snprintf(sPlayerId, sizeof(sPlayerId), "%s", val);
		}
		else if (strcmp(key, "API_TOKEN") == 0)
		{
			snprintf(sApiToken, sizeof(sApiToken), "%s", val);
		}
	}
	fclose(fp);
	if ((sPlayerId[0] == '\0') || (sApiToken[0] == '\0'))
	{
		return FALSE;
	}
	if (strcmp(loaded_from, path) != 0)
	{
		mmCredSave();
	}
	if ((verbose != FALSE) && ((sPlayerId[0] != '\0')))
	{
#ifdef PORT
		port_log("SSB64 Matchmaking: loaded cred player_id prefix %.8s...\n", sPlayerId);
#endif
	}
	return TRUE;
}

static sb32 mmJsonCopyQuotedValue(const char *body, const char *key_name, char *out, size_t cap)
{
	char needle[80];
	const char *p;
	size_t wi;

	/* Require exact JSON key token (avoids matching "peer" inside "peer_player_id"). */
	snprintf(needle, sizeof(needle), "\"%s\":", key_name);
	p = strstr(body, needle);
	if (p == NULL)
	{
		return FALSE;
	}
	p += strlen(needle);
	while ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\n'))
	{
		p++;
	}
	if (*p != '"')
	{
		return FALSE;
	}
	p++;
	wi = 0;
	while ((p[wi] != '\0') && (p[wi] != '"') && (wi + 1 < cap))
	{
		out[wi] = p[wi];
		wi++;
	}
	out[wi] = '\0';
	return wi > 0U;
}

static sb32 mmScanSessionIdDigits(const char *body, u32 *out_sid)
{
	const char *k = strstr(body, "\"session_id\"");
	long v;

	if (k == NULL)
	{
		return FALSE;
	}
	k = strchr(k, ':');
	if (k == NULL)
	{
		return FALSE;
	}
	k++;
	while ((*k == ' ') || (*k == '\t'))
	{
		k++;
	}
	errno = 0;
	v = strtol(k, NULL, 10);
	if ((errno != 0) || (v <= 0L) || (v > 0x7FFFFFFFL))
	{
		return FALSE;
	}
	*out_sid = (u32)v;
	return TRUE;
}

static sb32 mmParseMatchedBodyInto(const char *body, MmMatchResult *r)
{
	memset(r, 0, sizeof(*r));

	if (mmScanSessionIdDigits(body, &r->session_id) == FALSE)
	{
		return FALSE;
	}
	if (mmJsonCopyQuotedValue(body, "peer", r->peer_hostport, sizeof(r->peer_hostport)) == FALSE)
	{
		return FALSE;
	}
	r->peer_lan_hostport[0] = '\0';
	if (mmJsonCopyQuotedValue(body, "peer_lan", r->peer_lan_hostport, sizeof(r->peer_lan_hostport)) == FALSE)
	{
		r->peer_lan_hostport[0] = '\0';
	}
	r->peer_turn_hostport[0] = '\0';
	if (mmJsonCopyQuotedValue(body, "peer_turn", r->peer_turn_hostport, sizeof(r->peer_turn_hostport)) == FALSE)
	{
		r->peer_turn_hostport[0] = '\0';
	}
#if defined(SSB64_NETPLAY_ICE)
	r->peer_ice_sdp[0] = '\0';
	(void)mmJsonCopyQuotedValue(body, "peer_ice_sdp", r->peer_ice_sdp, sizeof(r->peer_ice_sdp));
	r->has_pending_ice_candidate = FALSE;
#endif
#ifdef PORT
	if (r->peer_turn_hostport[0] != '\0')
	{
		port_log("SSB64 Automatch: match JSON peer_turn=%s\n", r->peer_turn_hostport);
	}
	if (r->peer_lan_hostport[0] == '\0')
	{
		port_log("SSB64 Automatch: match JSON has no peer_lan (opponent did not report lan_endpoint)\n");
	}
	else
	{
		port_log("SSB64 Automatch: match JSON peer_lan=%s\n", r->peer_lan_hostport);
	}
#endif
	if (strstr(body, "\"you_are_host\":true") != NULL)
	{
		r->you_are_host = TRUE;
	}
	else if (strstr(body, "\"you_are_host\":false") != NULL)
	{
		r->you_are_host = FALSE;
	}
	else
	{
		return FALSE;
	}
	(void)mmJsonCopyQuotedValue(body, "match_id", r->match_id, sizeof(r->match_id));
	(void)mmJsonCopyQuotedValue(body, "peer_player_id", r->peer_player_id, sizeof(r->peer_player_id));
	r->kind = MM_POLL_MATCHED;
	return TRUE;
}

static long mmHttpsRequest(const char *method, const char *path_suffix, const char *json_body, sb32 verb, char **resp_body_out)
{
	CURL *c;
	char url[448];
	char header_player[144];
	char header_auth[448];
	MmMemBuf chunk;
	struct curl_slist *hdrs;
	long http_code;
	CURLcode curl_code;

	memset(&chunk, 0, sizeof(chunk));
	hdrs = NULL;
	http_code = 0;
	curl_code = CURLE_OK;

	snprintf(url, sizeof(url), "%s%s", sBaseUrl, path_suffix);

	c = curl_easy_init();

	if (c == NULL)
	{
		return -1;
	}

	hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
	if ((sPlayerId[0] != '\0'))
	{
		snprintf(header_player, sizeof(header_player), "X-Player-Id: %s", sPlayerId);
		hdrs = curl_slist_append(hdrs, header_player);
	}
	if ((sApiToken[0] != '\0'))
	{
		snprintf(header_auth, sizeof(header_auth), "Authorization: Bearer %s", sApiToken);
		hdrs = curl_slist_append(hdrs, header_auth);
	}

	curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);

	if ((method != NULL) && (strcmp(method, "GET") == 0))
	{
		curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
	}
	else
	{
		curl_easy_setopt(c, CURLOPT_POST, 1L);
		if (json_body != NULL)
		{
			curl_easy_setopt(c, CURLOPT_POSTFIELDS, json_body);
		}
		else
		{
			curl_easy_setopt(c, CURLOPT_POSTFIELDS, "{}");
		}
	}

	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, mmCurlWriteMem);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &chunk);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
	mmCurlConfigureSsl(c);

	curl_code = curl_easy_perform(c);

	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
	curl_slist_free_all(hdrs);
	curl_easy_cleanup(c);

#ifdef PORT
	if ((curl_code != CURLE_OK) || (verb != FALSE))
	{
		if (curl_code != CURLE_OK)
		{
			port_log("SSB64 Matchmaking: %s %s curl error: %s (HTTP %ld)\n", method, url,
			         curl_easy_strerror(curl_code), http_code);
		}
		else if (verb != FALSE)
		{
			port_log("SSB64 Matchmaking: %s %s -> HTTP %ld\n", method, url, http_code);
		}
	}
#endif

	if (resp_body_out != NULL)
	{
		*resp_body_out = chunk.data;
	}
	else if (chunk.data != NULL)
	{
		free(chunk.data);
	}
	return http_code;
}

static void mmRunEnsure(sb32 verb)
{
	char *resp;
	long hc;
	char pid[96];
	char tok[288];

	mmMatchmakingLoadCredentials(FALSE);
	if ((sPlayerId[0] != '\0') && (sApiToken[0] != '\0'))
	{
		if (mmCredVerifyLoaded(verb) != FALSE)
		{
			MmMatchResult ok;

#ifdef PORT
			if (verb != FALSE)
			{
				port_log("SSB64 Matchmaking: reusing cached player credential\n");
			}
#endif
			memset(&ok, 0, sizeof(ok));
			ok.kind = MM_POLL_PLAYER_READY;
			mmPushDone(&ok);
			return;
		}
	}

	if (mmCredRepopulate(verb) != FALSE)
	{
		MmMatchResult ok;

		memset(&ok, 0, sizeof(ok));
		ok.kind = MM_POLL_PLAYER_READY;
		mmPushDone(&ok);
		return;
	}

	hc = mmHttpsRequest("POST", "/v1/players", "{}", verb, &resp);
	if (((hc != 200) && (hc != 201)) || (resp == NULL))
	{
		mmPushDoneError(hc, "POST /v1/players failed");
	}
	else
	{
		if ((mmJsonCopyQuotedValue(resp, "player_id", pid, sizeof(pid)) == FALSE) ||
		     (mmJsonCopyQuotedValue(resp, "api_token", tok, sizeof(tok)) == FALSE))
		{
			mmPushDoneError(hc, "POST /v1/players JSON parse failed");
		}
		else
		{
			MmMatchResult ok;

			snprintf(sPlayerId, sizeof(sPlayerId), "%s", pid);
			snprintf(sApiToken, sizeof(sApiToken), "%s", tok);
			mmCredSave();
#ifdef PORT
			if (verb != FALSE)
			{
				port_log("SSB64 Matchmaking: POST /players ok player_id %.8s...\n", pid);
			}
#endif
			memset(&ok, 0, sizeof(ok));
			ok.kind = MM_POLL_PLAYER_READY;
			mmPushDone(&ok);
		}
	}
	if (resp != NULL)
	{
		free(resp);
	}
}

static void mmJsonEscapeString(const char *in, char *out, size_t out_cap)
{
	size_t o = 0U;

	if ((in == NULL) || (out == NULL) || (out_cap < 2U))
	{
		return;
	}
	for (; in[0] != '\0' && (o + 2U) < out_cap; in++)
	{
		if ((in[0] == '"') || (in[0] == '\\'))
		{
			out[o++] = '\\';
		}
		if (in[0] == '\n')
		{
			out[o++] = '\\';
			out[o++] = 'n';
			continue;
		}
		if (in[0] == '\r')
		{
			continue;
		}
		out[o++] = in[0];
	}
	out[o] = '\0';
}

static void mmJsonInsertStringField(char *jbuf, size_t cap, const char *key, const char *val)
{
	char *end;
	char esc[4096];
	char frag[4352];
	size_t flen;

	if ((jbuf == NULL) || (key == NULL) || (val == NULL) || (val[0] == '\0'))
	{
		return;
	}
	mmJsonEscapeString(val, esc, sizeof(esc));
	end = strrchr(jbuf, '}');
	if (end == NULL)
	{
		return;
	}
	snprintf(frag, sizeof(frag), ",\"%s\":\"%s\"", key, esc);
	flen = strlen(frag);
	if ((size_t)(end - jbuf) + flen + strlen(end) + 1U >= cap)
	{
		return;
	}
	{
		size_t tail = strlen(end) + 1U;
		size_t off;

		for (off = 0U; off < tail; off++)
		{
			end[flen + off] = end[off];
		}
	}
	memcpy(end, frag, flen);
}

static void mmJsonInsertTurnEndpoint(char *jbuf, size_t cap, const char *turn_ep)
{
	char *end;
	char frag[192];
	size_t flen;

	if ((jbuf == NULL) || (turn_ep == NULL) || (turn_ep[0] == '\0'))
	{
		return;
	}
	end = strrchr(jbuf, '}');
	if (end == NULL)
	{
		return;
	}
	snprintf(frag, sizeof(frag), ",\"turn_endpoint\":\"%s\"", turn_ep);
	flen = strlen(frag);
	if ((size_t)(end - jbuf) + flen + strlen(end) + 1U >= cap)
	{
		return;
	}
	{
		size_t tail = strlen(end) + 1U;
		size_t off;

		for (off = 0U; off < tail; off++)
		{
			end[flen + off] = end[off];
		}
	}
	memcpy(end, frag, flen);
}

static void mmRunJoin(const MmJob *job)
{
	char jbuf[448];
	long hc;
	char *resp;

	if ((mmMatchmakingLoadCredentials(FALSE) == FALSE) || ((sApiToken[0] == '\0')))
	{
		mmPushDoneError(401, "no credentials — call ensure player first");
		return;
	}
	if (job->has_fighter_kind != FALSE)
	{
		if (job->has_lan_endpoint != FALSE)
		{
			snprintf(jbuf, sizeof(jbuf),
			         "{\"udp_endpoint\":\"%s\",\"lan_endpoint\":\"%s\",\"region\":\"na-east\",\"game_version\":\"0.1.0\",\"fighter_kind\":%u,"
			         "\"rtt_ms_server\":0.0,"
			         "\"jitter_ms\":0.0,\"loss_pct\":0.0,\"avg_fps\":60.0,\"fps_drops_per_min\":0.0}",
			         job->udp_endpoint, job->lan_endpoint, job->fighter_kind);
		}
		else
		{
			snprintf(
			    jbuf, sizeof(jbuf),
			    "{\"udp_endpoint\":\"%s\",\"region\":\"na-east\",\"game_version\":\"0.1.0\",\"fighter_kind\":%u,"
			    "\"rtt_ms_server\":0.0,"
			    "\"jitter_ms\":0.0,\"loss_pct\":0.0,\"avg_fps\":60.0,\"fps_drops_per_min\":0.0}",
			    job->udp_endpoint, job->fighter_kind);
		}
	}
	else
	{
		if (job->has_lan_endpoint != FALSE)
		{
			snprintf(jbuf, sizeof(jbuf),
			         "{\"udp_endpoint\":\"%s\",\"lan_endpoint\":\"%s\",\"region\":\"na-east\",\"game_version\":\"0.1.0\","
			         "\"rtt_ms_server\":0.0,"
			         "\"jitter_ms\":0.0,\"loss_pct\":0.0,\"avg_fps\":60.0,\"fps_drops_per_min\":0.0}",
			         job->udp_endpoint, job->lan_endpoint);
		}
		else
		{
			snprintf(jbuf, sizeof(jbuf),
			         "{\"udp_endpoint\":\"%s\",\"region\":\"na-east\",\"game_version\":\"0.1.0\","
			         "\"rtt_ms_server\":0.0,"
			         "\"jitter_ms\":0.0,\"loss_pct\":0.0,\"avg_fps\":60.0,\"fps_drops_per_min\":0.0}",
			         job->udp_endpoint);
		}
	}
	if (job->has_turn_endpoint != FALSE)
	{
		mmJsonInsertTurnEndpoint(jbuf, sizeof(jbuf), job->turn_endpoint);
	}
#if defined(SSB64_NETPLAY_ICE)
	if (job->has_ice_sdp != FALSE)
	{
		mmJsonInsertStringField(jbuf, sizeof(jbuf), "ice_sdp", job->ice_sdp);
	}
#endif
#ifdef PORT
	port_log("SSB64 Automatch: POST /v1/queue udp=%s lan=%s turn=%s\n", job->udp_endpoint,
	         (job->has_lan_endpoint != FALSE) ? job->lan_endpoint : "(none)",
	         (job->has_turn_endpoint != FALSE) ? job->turn_endpoint : "(none)");
#endif
	hc = mmHttpsRequest("POST", "/v1/queue", jbuf, job->verbose != FALSE, &resp);
	if (((hc != 200) || (resp == NULL)) && (mmCredShouldRepopulate(hc, resp, TRUE) != FALSE) &&
	    (mmCredRepopulate(job->verbose != FALSE) != FALSE))
	{
		if (resp != NULL)
		{
			free(resp);
			resp = NULL;
		}
		hc = mmHttpsRequest("POST", "/v1/queue", jbuf, job->verbose != FALSE, &resp);
	}
	if ((hc != 200) || (resp == NULL))
	{
		mmPushDoneError(hc, "POST /v1/queue failed");
	}
	else
	{
		MmMatchResult out;
		char tick[96];

		if (mmJsonCopyQuotedValue(resp, "ticket_id", tick, sizeof(tick)) == FALSE)
		{
			mmPushDoneError(hc, "queue ticket_id parse failed");
		}
		else
		{
			memset(&out, 0, sizeof(out));
			out.kind = MM_POLL_QUEUED;
			snprintf(out.ticket_id, sizeof(out.ticket_id), "%s", tick);
			mmPushDone(&out);
		}
	}
	if (resp != NULL)
	{
		free(resp);
	}
}

static void mmRunHeartbeat(const MmJob *job)
{
	char jbuf[512];
	long hc;
	char *resp;

	if (job->heartbeat_has_endpoints != FALSE)
	{
		if (job->has_lan_endpoint != FALSE)
		{
			snprintf(jbuf, sizeof(jbuf),
			         "{\"ticket_id\":\"%s\",\"client_time_ms\":0,\"last_server_rtt_ms\":0.0,\"jitter_ms\":0.0,"
			         "\"loss_pct\":0.0,\"udp_endpoint\":\"%s\",\"lan_endpoint\":\"%s\"}",
			         job->ticket_id, job->udp_endpoint, job->lan_endpoint);
		}
		else
		{
			snprintf(jbuf, sizeof(jbuf),
			         "{\"ticket_id\":\"%s\",\"client_time_ms\":0,\"last_server_rtt_ms\":0.0,\"jitter_ms\":0.0,"
			         "\"loss_pct\":0.0,\"udp_endpoint\":\"%s\"}",
			         job->ticket_id, job->udp_endpoint);
		}
	}
	else
	{
		snprintf(jbuf, sizeof(jbuf),
		         "{\"ticket_id\":\"%s\",\"client_time_ms\":0,\"last_server_rtt_ms\":0.0,\"jitter_ms\":0.0,\"loss_pct\":0.0}",
		         job->ticket_id);
	}
	if (job->has_turn_endpoint != FALSE)
	{
		mmJsonInsertTurnEndpoint(jbuf, sizeof(jbuf), job->turn_endpoint);
	}
	hc = mmHttpsRequest("POST", "/v1/heartbeat", jbuf, job->verbose != FALSE, &resp);
	if (hc == 404)
	{
		/* Ticket may already be matched; queue heartbeat is no longer valid. */
		MmMatchResult ok;

#ifdef PORT
		if (job->verbose != FALSE)
		{
			port_log("SSB64 Matchmaking: heartbeat 404 (ignored, likely matched)\n");
		}
#endif
		memset(&ok, 0, sizeof(ok));
		ok.kind = MM_POLL_HEARTBEAT_OK;
		mmPushDone(&ok);
	}
	else if (((hc != 200) || (resp == NULL)) && (mmCredShouldRepopulate(hc, resp, FALSE) != FALSE) &&
	         (mmCredRepopulate(job->verbose != FALSE) != FALSE))
	{
		if (resp != NULL)
		{
			free(resp);
			resp = NULL;
		}
		hc = mmHttpsRequest("POST", "/v1/heartbeat", jbuf, job->verbose != FALSE, &resp);
		if (hc == 404)
		{
			MmMatchResult ok;

			memset(&ok, 0, sizeof(ok));
			ok.kind = MM_POLL_HEARTBEAT_OK;
			mmPushDone(&ok);
		}
		else if ((hc != 200) || (resp == NULL))
		{
			mmPushDoneError(hc, "POST /v1/heartbeat failed after cred refresh");
		}
		else
		{
			MmMatchResult ok;

			memset(&ok, 0, sizeof(ok));
			ok.kind = MM_POLL_HEARTBEAT_OK;
			mmPushDone(&ok);
		}
	}
	else if ((hc != 200) || (resp == NULL))
	{
		mmPushDoneError(hc, "POST /v1/heartbeat failed");
	}
	else
	{
		MmMatchResult ok;

#ifdef PORT
		if (job->verbose != FALSE)
		{
			port_log("SSB64 Matchmaking: heartbeat OK\n");
		}
#endif
		memset(&ok, 0, sizeof(ok));
		ok.kind = MM_POLL_HEARTBEAT_OK;
		mmPushDone(&ok);
	}
	if (resp != NULL)
	{
		free(resp);
	}
}

#if defined(SSB64_NETPLAY_ICE)
static void mmParseIceSignalsFromBody(const char *body);
#endif

static void mmRunPoll(const MmJob *job)
{
	char url_path[288];
	long hc;
	char *resp;

	snprintf(url_path, sizeof(url_path), "/v1/match/%s", job->ticket_id);
	hc = mmHttpsRequest("GET", url_path, NULL, job->verbose != FALSE, &resp);

	if (((hc != 200) && (hc != 304)) || (resp == NULL))
	{
		mmPushDoneError(hc, "GET match poll failed");
	}
	else if (strstr(resp, "\"status\":\"queued\"") != NULL)
	{
#ifdef PORT
		if (job->verbose != FALSE)
		{
			port_log("SSB64 Matchmaking: still queued\n");
		}
#endif
	}
	else if (strstr(resp, "\"status\":\"matched\"") != NULL)
	{
		MmMatchResult r;

		if ((mmParseMatchedBodyInto(resp, &r)))
		{
			if ((r.kind == MM_POLL_MATCHED))
			{
#if defined(SSB64_NETPLAY_ICE)
				mmParseIceSignalsFromBody(resp);
#endif
				snprintf(r.ticket_id, sizeof(r.ticket_id), "%s", job->ticket_id);
				mmPushDone(&r);
			}
		}
		else
		{
			mmPushDoneError(hc, "match poll JSON parse failed");
		}
	}
	else
	{
		mmPushDoneError(hc, "unexpected poll payload");
	}
	if (resp != NULL)
	{
		free(resp);
	}
}

#if defined(SSB64_NETPLAY_ICE)
#define MM_ICE_SIGNAL_QUEUE 24

static char sMmIceSignalQ[MM_ICE_SIGNAL_QUEUE][280];
static u32 sMmIceSignalHead;
static u32 sMmIceSignalCount;

static void mmIceSignalQueueClear(void)
{
	sMmIceSignalHead = 0U;
	sMmIceSignalCount = 0U;
}

static void mmIceSignalQueuePush(const char *candidate)
{
	u32 tail;

	if ((candidate == NULL) || (candidate[0] == '\0'))
	{
		return;
	}
	if (sMmIceSignalCount >= MM_ICE_SIGNAL_QUEUE)
	{
		sMmIceSignalHead = (sMmIceSignalHead + 1U) % MM_ICE_SIGNAL_QUEUE;
		sMmIceSignalCount--;
	}
	tail = (sMmIceSignalHead + sMmIceSignalCount) % MM_ICE_SIGNAL_QUEUE;
	snprintf(sMmIceSignalQ[tail], sizeof(sMmIceSignalQ[tail]), "%s", candidate);
	sMmIceSignalCount++;
}

static void mmParseIceSignalsFromBody(const char *body)
{
	const char *p;
	const char *end;

	if (body == NULL)
	{
		return;
	}
	p = strstr(body, "\"ice_signals\"");
	if (p == NULL)
	{
		return;
	}
	p = strchr(p, '[');
	if (p == NULL)
	{
		return;
	}
	p++;
	while (*p != '\0' && *p != ']')
	{
		while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r')
		{
			p++;
		}
		if (*p != '"')
		{
			break;
		}
		p++;
		end = strchr(p, '"');
		if (end == NULL)
		{
			break;
		}
		{
			char tmp[280];
			size_t n = (size_t)(end - p);

			if (n >= sizeof(tmp))
			{
				n = sizeof(tmp) - 1U;
			}
			memcpy(tmp, p, n);
			tmp[n] = '\0';
			mmIceSignalQueuePush(tmp);
		}
		p = end + 1;
	}
}

sb32 mmMatchmakingPopIceCandidate(char *out, u32 out_cap)
{
	if ((out == NULL) || (out_cap < 8U) || (sMmIceSignalCount == 0U))
	{
		return FALSE;
	}
	snprintf(out, out_cap, "%s", sMmIceSignalQ[sMmIceSignalHead]);
	sMmIceSignalHead = (sMmIceSignalHead + 1U) % MM_ICE_SIGNAL_QUEUE;
	sMmIceSignalCount--;
	return TRUE;
}

static void mmRunIceSignal(const MmJob *job)
{
	char jbuf[512];
	long hc;
	char *resp;
	char url_path[288];

	if ((mmMatchmakingLoadCredentials(FALSE) == FALSE) || (sApiToken[0] == '\0'))
	{
		return;
	}
	snprintf(url_path, sizeof(url_path), "/v1/match/%s/ice", job->ticket_id);
	snprintf(jbuf, sizeof(jbuf), "{\"candidate\":\"%s\"}", job->ice_candidate);
	hc = mmHttpsRequest("POST", url_path, jbuf, job->verbose != FALSE, &resp);
	if (resp != NULL)
	{
		free(resp);
	}
	(void)hc;
}

static sb32 mmJsonCopyU32Field(const char *body, const char *key_name, u32 *out_val, u32 default_val)
{
	char needle[80];
	const char *p;
	long v;

	snprintf(needle, sizeof(needle), "\"%s\":", key_name);
	p = strstr(body, needle);
	if (p == NULL)
	{
		*out_val = default_val;
		return FALSE;
	}
	p += strlen(needle);
	while ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\n'))
	{
		p++;
	}
	errno = 0;
	v = strtol(p, NULL, 10);
	if ((errno != 0) || (v < 0L) || (v > 65535L))
	{
		*out_val = default_val;
		return FALSE;
	}
	*out_val = (u32)v;
	return TRUE;
}

sb32 mmMatchmakingFetchTurnCredentials(MmIceTurnBundle *out)
{
	long hc;
	char *resp;
	u32 port_tmp;

	if (out == NULL)
	{
		return FALSE;
	}
	memset(out, 0, sizeof(*out));
	out->stun_port = 3478U;
	out->turn_port = 3478U;
	out->turns_port = 5349U;

	if ((mmMatchmakingLoadCredentials(FALSE) == FALSE) || (sApiToken[0] == '\0'))
	{
		return FALSE;
	}
	hc = mmHttpsRequest("GET", "/v1/turn-credentials", NULL, FALSE, &resp);
	if ((hc != 200) || (resp == NULL))
	{
		if (resp != NULL)
		{
			free(resp);
		}
		return FALSE;
	}
	if ((mmJsonCopyQuotedValue(resp, "username", out->turn_user, sizeof(out->turn_user)) == FALSE) ||
	    (mmJsonCopyQuotedValue(resp, "password", out->turn_pass, sizeof(out->turn_pass)) == FALSE))
	{
		free(resp);
		return FALSE;
	}
	(void)mmJsonCopyQuotedValue(resp, "realm", out->realm, sizeof(out->realm));
	if (mmJsonCopyQuotedValue(resp, "stun_host", out->stun_host, sizeof(out->stun_host)) == FALSE)
	{
		(void)mmJsonCopyQuotedValue(resp, "turn_host", out->stun_host, sizeof(out->stun_host));
	}
	if (mmJsonCopyQuotedValue(resp, "turn_host", out->turn_host, sizeof(out->turn_host)) == FALSE)
	{
		snprintf(out->turn_host, sizeof(out->turn_host), "%s", out->stun_host);
	}
	(void)mmJsonCopyU32Field(resp, "stun_port", &port_tmp, 3478U);
	out->stun_port = (u16)port_tmp;
	(void)mmJsonCopyU32Field(resp, "turn_port", &port_tmp, 3478U);
	out->turn_port = (u16)port_tmp;
	(void)mmJsonCopyU32Field(resp, "turns_port", &port_tmp, 5349U);
	out->turns_port = (u16)port_tmp;
	free(resp);
	return TRUE;
}
#endif /* SSB64_NETPLAY_ICE */

static void mmRunCancel(const MmJob *job)
{
	char jbuf[192];
	long hc;
	char *resp;

	snprintf(jbuf, sizeof(jbuf), "{\"ticket_id\":\"%s\"}", job->ticket_id);
	hc = mmHttpsRequest("POST", "/v1/queue/cancel", jbuf, job->verbose != FALSE, &resp);

	if ((((hc >= 200) && (hc <= 299)) || (hc == 404)))
	{
		MmMatchResult ok;

		memset(&ok, 0, sizeof(ok));
		ok.kind = MM_POLL_CANCEL_OK;
		mmPushDone(&ok);
	}
	else if ((mmCredShouldRepopulate(hc, resp, FALSE) != FALSE) &&
	         (mmCredRepopulate(job->verbose != FALSE) != FALSE))
	{
		if (resp != NULL)
		{
			free(resp);
			resp = NULL;
		}
		hc = mmHttpsRequest("POST", "/v1/queue/cancel", jbuf, job->verbose != FALSE, &resp);
		if ((((hc >= 200) && (hc <= 299)) || (hc == 404)))
		{
			MmMatchResult ok;

			memset(&ok, 0, sizeof(ok));
			ok.kind = MM_POLL_CANCEL_OK;
			mmPushDone(&ok);
		}
		else
		{
			mmPushDoneError(hc, "cancel failed after cred refresh");
		}
	}
	else
	{
		mmPushDoneError(hc, "cancel failed");
	}
	if (resp != NULL)
	{
		free(resp);
	}
}

static sb32 mmJobQueueContainsPollForTicketLocked(const char *ticket_id)
{
	u32 i;
	u32 idx;

	if ((ticket_id == NULL) || (ticket_id[0] == '\0'))
	{
		return FALSE;
	}
	for (i = 0U; i < sJobCount; i++)
	{
		idx = (sJobHead + i) % MM_JOB_QUEUE_DEPTH;
		if (sJobQ[idx].kind != MM_JOB_POLL_MATCH)
		{
			continue;
		}
		if (strcmp(sJobQ[idx].ticket_id, ticket_id) == 0)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 mmJobQueueContainsHeartbeatForTicketLocked(const char *ticket_id)
{
	u32 i;
	u32 idx;

	if ((ticket_id == NULL) || (ticket_id[0] == '\0'))
	{
		return FALSE;
	}
	for (i = 0U; i < sJobCount; i++)
	{
		idx = (sJobHead + i) % MM_JOB_QUEUE_DEPTH;
		if (sJobQ[idx].kind != MM_JOB_HEARTBEAT)
		{
			continue;
		}
		if (strcmp(sJobQ[idx].ticket_id, ticket_id) == 0)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static void mmEnqueueLocked(const MmJob *jp)
{
	if (jp->kind == MM_JOB_POLL_MATCH)
	{
		if (mmJobQueueContainsPollForTicketLocked(jp->ticket_id) != FALSE)
		{
			return;
		}
	}
	if (jp->kind == MM_JOB_HEARTBEAT)
	{
		if (mmJobQueueContainsHeartbeatForTicketLocked(jp->ticket_id) != FALSE)
		{
			return;
		}
	}
	if (sJobCount >= MM_JOB_QUEUE_DEPTH)
	{
#ifdef PORT
		port_log("SSB64 Matchmaking: job queue overflow (drop kind=%d)\n", (int)jp->kind);
#endif
		return;
	}
	sJobQ[sJobTail] = *jp;
	sJobTail = (sJobTail + 1U) % MM_JOB_QUEUE_DEPTH;
	sJobCount++;
	pthread_cond_signal(&sCond);
}

static void mmJobWorkerLoop(void *unused_arg)
{
	(void)unused_arg;
	while (TRUE)
	{
		MmJob cur;

		memset(&cur, 0, sizeof(cur));
		pthread_mutex_lock(&sMutex);
		while ((sWorkerRunning != FALSE) && (sJobCount == 0U))
		{
			pthread_cond_wait(&sCond, &sMutex);
		}
		if ((sWorkerRunning == FALSE) && (sJobCount == 0U))
		{
			pthread_mutex_unlock(&sMutex);
			break;
		}
		cur = sJobQ[sJobHead];
		sJobHead = (sJobHead + 1U) % MM_JOB_QUEUE_DEPTH;
		sJobCount--;
		pthread_mutex_unlock(&sMutex);

		switch (cur.kind)
		{
		case MM_JOB_ENSURE_PLAYER:
			mmRunEnsure(cur.verbose);
			break;
		case MM_JOB_JOIN_QUEUE:
			mmRunJoin(&cur);
			break;
		case MM_JOB_HEARTBEAT:
			mmRunHeartbeat(&cur);
			break;
		case MM_JOB_POLL_MATCH:
			mmRunPoll(&cur);
			break;
		case MM_JOB_CANCEL:
			mmRunCancel(&cur);
			break;
#if defined(SSB64_NETPLAY_ICE)
		case MM_JOB_ICE_SIGNAL:
			mmRunIceSignal(&cur);
			break;
#endif
		default:
			break;
		}
	}
#ifdef PORT
	port_log("SSB64 Matchmaking: worker exited\n");
#endif
}

static void *mmWorkerPthreadThunk(void *p)
{
	(void)p;
	mmJobWorkerLoop(NULL);
	return NULL;
}

void mmMatchmakingStartup(void)
{
	mmBaseUrlSetup();
	if ((sWorkerSpawned != FALSE))
	{
		return;
	}

	mmMatchmakingInitPortableCaBundle();

	if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0)
	{
#ifdef PORT
		port_log("SSB64 Matchmaking: curl_global_init failed\n");
#endif
		return;
	}

	sWorkerRunning = TRUE;
	sWorkerSpawned = TRUE;
	sMmWorkerRunningForDoneWait = TRUE;
	if (pthread_create(&sWorkerThread, NULL, mmWorkerPthreadThunk, NULL) != 0)
	{
		sWorkerRunning = FALSE;
		sWorkerSpawned = FALSE;
		sMmWorkerRunningForDoneWait = FALSE;
#ifdef PORT
		port_log("SSB64 Matchmaking: pthread_create failed\n");
#endif
		curl_global_cleanup();
		return;
	}
#ifdef PORT
	port_log("SSB64 Matchmaking: worker thread online base=%s\n", sBaseUrl);
#endif
}

void mmMatchmakingShutdown(void)
{
	if ((sWorkerSpawned == FALSE))
	{
		return;
	}

	pthread_mutex_lock(&sMutex);
	sMmWorkerRunningForDoneWait = FALSE;
	sWorkerRunning = FALSE;
	pthread_cond_broadcast(&sCond);
	pthread_mutex_unlock(&sMutex);
	pthread_mutex_lock(&sDoneMutex);
	pthread_cond_broadcast(&sDoneNotFull);
	pthread_mutex_unlock(&sDoneMutex);
	(void)pthread_join(sWorkerThread, NULL);
	sWorkerSpawned = FALSE;

	curl_global_cleanup();
#ifdef PORT
	port_log("SSB64 Matchmaking: shutdown OK\n");
#endif
}

static void mmEnqueueSubmit(const MmJob *jp)
{
	if ((sWorkerSpawned == FALSE))
	{
#ifdef PORT
		port_log("SSB64 Matchmaking: Startup not called; dropping job kind=%d\n", (int)jp->kind);
#endif
		return;
	}

	pthread_mutex_lock(&sMutex);
	mmEnqueueLocked(jp);
	pthread_mutex_unlock(&sMutex);
}

void mmMatchmakingEnqueueEnsurePlayer(sb32 verbose)
{
	MmJob j;

	mmMatchmakingStartup();
	memset(&j, 0, sizeof(j));
	j.kind = MM_JOB_ENSURE_PLAYER;
	j.verbose = verbose;
	mmEnqueueSubmit(&j);
}

void mmMatchmakingEnqueueJoinQueue(sb32 verbose, const char *udp_endpoint, u8 fighter_kind, sb32 has_fkind,
                                   const char *lan_endpoint_opt)
{
	mmMatchmakingEnqueueJoinQueueEx(verbose, udp_endpoint, fighter_kind, has_fkind, lan_endpoint_opt, NULL);
}

void mmMatchmakingEnqueueJoinQueueEx(sb32 verbose, const char *udp_endpoint, u8 fighter_kind, sb32 has_fkind,
                                     const char *lan_endpoint_opt, const char *turn_endpoint_opt)
{
	MmJob j;

	mmMatchmakingStartup();
	memset(&j, 0, sizeof(j));
	j.kind = MM_JOB_JOIN_QUEUE;
	j.verbose = verbose;
	if (udp_endpoint != NULL)
	{
		snprintf(j.udp_endpoint, sizeof(j.udp_endpoint), "%s", udp_endpoint);
	}
	j.fighter_kind = fighter_kind;
	j.has_fighter_kind = has_fkind;
	j.has_lan_endpoint = FALSE;
	j.lan_endpoint[0] = '\0';
	if ((lan_endpoint_opt != NULL) && (lan_endpoint_opt[0] != '\0'))
	{
		snprintf(j.lan_endpoint, sizeof(j.lan_endpoint), "%s", lan_endpoint_opt);
		j.has_lan_endpoint = TRUE;
	}
	j.has_turn_endpoint = FALSE;
	j.turn_endpoint[0] = '\0';
	if ((turn_endpoint_opt != NULL) && (turn_endpoint_opt[0] != '\0'))
	{
		snprintf(j.turn_endpoint, sizeof(j.turn_endpoint), "%s", turn_endpoint_opt);
		j.has_turn_endpoint = TRUE;
	}
	mmEnqueueSubmit(&j);
}

void mmMatchmakingEnqueueHeartbeat(sb32 verbose, const char *ticket_id)
{
	mmMatchmakingEnqueueHeartbeatWithEndpointsEx(verbose, ticket_id, NULL, NULL, NULL);
}

void mmMatchmakingEnqueueHeartbeatWithEndpoints(sb32 verbose, const char *ticket_id, const char *udp_endpoint,
                                                const char *lan_endpoint_opt)
{
	mmMatchmakingEnqueueHeartbeatWithEndpointsEx(verbose, ticket_id, udp_endpoint, lan_endpoint_opt, NULL);
}

void mmMatchmakingEnqueueHeartbeatWithEndpointsEx(sb32 verbose, const char *ticket_id, const char *udp_endpoint,
                                                  const char *lan_endpoint_opt, const char *turn_endpoint_opt)
{
	MmJob j;

	memset(&j, 0, sizeof(j));
	j.kind = MM_JOB_HEARTBEAT;
	j.verbose = verbose;
	if (ticket_id != NULL)
	{
		snprintf(j.ticket_id, sizeof(j.ticket_id), "%s", ticket_id);
	}
	if ((udp_endpoint != NULL) && (udp_endpoint[0] != '\0'))
	{
		snprintf(j.udp_endpoint, sizeof(j.udp_endpoint), "%s", udp_endpoint);
		j.heartbeat_has_endpoints = TRUE;
	}
	j.has_lan_endpoint = FALSE;
	j.lan_endpoint[0] = '\0';
	if ((lan_endpoint_opt != NULL) && (lan_endpoint_opt[0] != '\0'))
	{
		snprintf(j.lan_endpoint, sizeof(j.lan_endpoint), "%s", lan_endpoint_opt);
		j.has_lan_endpoint = TRUE;
	}
	j.has_turn_endpoint = FALSE;
	j.turn_endpoint[0] = '\0';
	if ((turn_endpoint_opt != NULL) && (turn_endpoint_opt[0] != '\0'))
	{
		snprintf(j.turn_endpoint, sizeof(j.turn_endpoint), "%s", turn_endpoint_opt);
		j.has_turn_endpoint = TRUE;
	}
	mmEnqueueSubmit(&j);
}

void mmMatchmakingEnqueuePollMatch(sb32 verbose, const char *ticket_id)
{
	MmJob j;

	memset(&j, 0, sizeof(j));
	j.kind = MM_JOB_POLL_MATCH;
	j.verbose = verbose;
	if (ticket_id != NULL)
	{
		snprintf(j.ticket_id, sizeof(j.ticket_id), "%s", ticket_id);
	}
	mmEnqueueSubmit(&j);
}

void mmMatchmakingEnqueueCancel(sb32 verbose, const char *ticket_id)
{
	MmJob j;

	memset(&j, 0, sizeof(j));
	j.kind = MM_JOB_CANCEL;
	j.verbose = verbose;
	if (ticket_id != NULL)
	{
		snprintf(j.ticket_id, sizeof(j.ticket_id), "%s", ticket_id);
	}
	mmEnqueueSubmit(&j);
}

#if defined(SSB64_NETPLAY_ICE)
void mmMatchmakingEnqueueJoinQueueIce(sb32 verbose, const char *udp_endpoint, const char *ice_sdp, u8 fighter_kind,
                                      sb32 has_fkind, const char *lan_endpoint_opt)
{
	MmJob j;

	mmMatchmakingStartup();
	memset(&j, 0, sizeof(j));
	j.kind = MM_JOB_JOIN_QUEUE;
	j.verbose = verbose;
	if (udp_endpoint != NULL)
	{
		snprintf(j.udp_endpoint, sizeof(j.udp_endpoint), "%s", udp_endpoint);
	}
	j.fighter_kind = fighter_kind;
	j.has_fighter_kind = has_fkind;
	if ((lan_endpoint_opt != NULL) && (lan_endpoint_opt[0] != '\0'))
	{
		snprintf(j.lan_endpoint, sizeof(j.lan_endpoint), "%s", lan_endpoint_opt);
		j.has_lan_endpoint = TRUE;
	}
	if ((ice_sdp != NULL) && (ice_sdp[0] != '\0'))
	{
		snprintf(j.ice_sdp, sizeof(j.ice_sdp), "%s", ice_sdp);
		j.has_ice_sdp = TRUE;
	}
	mmEnqueueSubmit(&j);
}

void mmMatchmakingEnqueueIceSignal(sb32 verbose, const char *ticket_id, const char *candidate_sdp)
{
	MmJob j;

	memset(&j, 0, sizeof(j));
	j.kind = MM_JOB_ICE_SIGNAL;
	j.verbose = verbose;
	if (ticket_id != NULL)
	{
		snprintf(j.ticket_id, sizeof(j.ticket_id), "%s", ticket_id);
	}
	if ((candidate_sdp != NULL) && (candidate_sdp[0] != '\0'))
	{
		snprintf(j.ice_candidate, sizeof(j.ice_candidate), "%s", candidate_sdp);
	}
	mmEnqueueSubmit(&j);
}
#endif

sb32 mmMatchmakingDrainCompleted(MmMatchResult *out)
{
	sb32 ok;

	if (out == NULL)
	{
		return FALSE;
	}
	ok = FALSE;
	pthread_mutex_lock(&sDoneMutex);
	if (sDoneCount == 0U)
	{
		pthread_mutex_unlock(&sDoneMutex);
		return FALSE;
	}
	*out = sDoneQ[sDoneHead];
	sDoneHead = (sDoneHead + 1U) % MM_DONE_QUEUE_DEPTH;
	sDoneCount--;
	ok = TRUE;
	pthread_cond_broadcast(&sDoneNotFull);
	pthread_mutex_unlock(&sDoneMutex);
	return ok;
}

u32 mmMatchmakingApproxPendingJobs(void)
{
	u32 n;

	if (sWorkerSpawned == FALSE)
	{
		return 0U;
	}
	pthread_mutex_lock(&sMutex);
	n = sJobCount;
	pthread_mutex_unlock(&sMutex);
	return n;
}

#endif /* PORT && SSB64_NETMENU */
