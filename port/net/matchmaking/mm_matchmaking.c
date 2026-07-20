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
#if defined(__linux__)
#include <sys/prctl.h>
#endif
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
#include "port_log.h"
#if defined(SSB64_NETPLAY_ICE)
#include "mm_ice.h"
#include "mm_ice_automatch.h"
#include "mm_ice_reconnect.h"
#endif

static sb32 mmLogVerbose(void)
{
	return port_log_debug_active() ? TRUE : FALSE;
}

#define MM_VERBOSE(job_verb) (((job_verb) != FALSE) || (mmLogVerbose() != FALSE))
#endif

#ifndef MM_DEFAULT_BASE_URL
#define MM_DEFAULT_BASE_URL "https://netplay.technicallycomputers.ca"
#endif
/* HTTPS jobs queue: producer can outpace single worker under high RTT — keep headroom. */
#define MM_JOB_QUEUE_DEPTH 64
/* Completed events: may burst after slow requests; larger than job queue; never drop MATCHED/ERROR lightly. */
#define MM_DONE_QUEUE_DEPTH 128
#define MM_CRED_FILENAME "matchmaking.cred"
/* ICE local SDP in POST /v1/queue; 448-byte buffer silently dropped ice_sdp before 2026-05-25. */
#define MM_JOIN_JSON_CAP 9216

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
	MM_JOB_ICE_ROLE_READY,
	MM_JOB_ICE_PLAYER_READY,
	MM_JOB_ICE_RECONNECT_INIT,
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
	char ice_edge_id[16];
	u32 ice_connect_epoch;
	sb32 poll_trickle_only;
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
static volatile sb32 sWorkerPollMatchActive;
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
/** Last successful matchmaking HTTPS total time (ms), from CURLINFO_TOTAL_TIME. */
static double sLastHttpsRttMs;

static long long mmClientUnixTimeMs(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
	{
		return 0;
	}
	return ((long long)ts.tv_sec * 1000LL) + ((long long)ts.tv_nsec / 1000000LL);
}

static double mmLastHttpsRttMsOrZero(void)
{
	if ((sLastHttpsRttMs > 0.0) && (sLastHttpsRttMs < 2000.0))
	{
		return sLastHttpsRttMs;
	}
	return 0.0;
}

#if defined(SSB64_NETPLAY_ICE)
static MmIceTurnBundle sCachedTurnBundle;
static sb32 sCachedTurnValid;
#endif

static sb32 mmPollKindIsCritical(MmPollKind k)
{
	switch (k)
	{
	case MM_POLL_MATCHED:
	case MM_POLL_ERROR:
	case MM_POLL_PLAYER_READY:
#if defined(SSB64_NETPLAY_ICE)
	case MM_POLL_ICE_PLAYER_READY:
#endif
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

/* Same writable tree as ssb64.log / ssb64_save.bin (UserDataDir / externalFilesDir on Android). */
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
	         "{\"ticket_id\":\"00000000-0000-0000-0000-000000000000\",\"client_time_ms\":%lld,"
	         "\"last_server_rtt_ms\":%.1f,\"jitter_ms\":0.0,\"loss_pct\":0.0}",
	         (long long)mmClientUnixTimeMs(), mmLastHttpsRttMsOrZero());
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

		if (mmCredPathLegacy(legacy, sizeof(legacy)) != FALSE)
		{
			fp = fopen(legacy, "r");
			if (fp != NULL)
			{
				snprintf(loaded_from, sizeof(loaded_from), "%s", legacy);
#ifdef PORT
				port_log("SSB64 Matchmaking: loaded cred from legacy path (will migrate)\n");
#endif
			}
		}
		if (fp == NULL)
		{
			return FALSE;
		}
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

/* In-memory tokens from mmRunEnsure/mmCredRepopulate must work even when cred file I/O fails. */
static sb32 mmCredentialsEnsureReady(sb32 verbose)
{
	if ((sPlayerId[0] != '\0') && (sApiToken[0] != '\0'))
	{
		return TRUE;
	}
	(void)mmMatchmakingLoadCredentials(verbose);
	return ((sPlayerId[0] != '\0') && (sApiToken[0] != '\0')) ? TRUE : FALSE;
}

/* Decode JSON string contents (in points past opening "). Stops at unescaped closing quote. */
static size_t mmJsonDecodeQuotedString(const char *in, char *out, size_t cap)
{
	size_t o = 0U;

	if ((in == NULL) || (out == NULL) || (cap < 1U))
	{
		return 0U;
	}
	for (; in[0] != '\0'; in++)
	{
		unsigned char c;

		if (in[0] == '"')
		{
			break;
		}
		if (in[0] != '\\' || in[1] == '\0')
		{
			if (o + 1U >= cap)
			{
				return 0U;
			}
			out[o++] = in[0];
			continue;
		}
		in++;
		c = (unsigned char)in[0];
		if (c == (unsigned char)'n')
		{
			c = (unsigned char)'\n';
		}
		else if (c == (unsigned char)'r')
		{
			c = (unsigned char)'\r';
		}
		else if (c == (unsigned char)'t')
		{
			c = (unsigned char)'\t';
		}
		else if (c == (unsigned char)'u' && in[1] != '\0' && in[2] != '\0' && in[3] != '\0' && in[4] != '\0')
		{
			/* Minimal \uXXXX: keep ASCII when hex is 00xx. */
			unsigned int hex = 0U;
			int i;

			for (i = 1; i <= 4; i++)
			{
				unsigned char h = (unsigned char)in[i];

				hex <<= 4U;
				if (h >= (unsigned char)'0' && h <= (unsigned char)'9')
				{
					hex |= (unsigned int)(h - (unsigned char)'0');
				}
				else if (h >= (unsigned char)'a' && h <= (unsigned char)'f')
				{
					hex |= (unsigned int)(h - (unsigned char)'a' + 10U);
				}
				else if (h >= (unsigned char)'A' && h <= (unsigned char)'F')
				{
					hex |= (unsigned int)(h - (unsigned char)'A' + 10U);
				}
				else
				{
					hex = 0U;
					break;
				}
			}
			in += 4;
			c = (hex <= 0xFFU) ? (unsigned char)hex : (unsigned char)'?';
		}
		if (o + 1U >= cap)
		{
			return 0U;
		}
		out[o++] = (char)c;
	}
	out[o] = '\0';
	return o;
}

/* Advance past JSON string body (in past opening "); returns pointer after closing quote. */
static const char *mmJsonSkipPastQuotedString(const char *in)
{
	if (in == NULL)
	{
		return NULL;
	}
	for (; in[0] != '\0'; in++)
	{
		if (in[0] == '"')
		{
			return in + 1;
		}
		if (in[0] == '\\' && in[1] != '\0')
		{
			in++;
			if (in[0] == (char)'u' && in[1] != '\0' && in[2] != '\0' && in[3] != '\0' && in[4] != '\0')
			{
				in += 4U;
			}
		}
	}
	return in;
}

static sb32 mmJsonCopyQuotedValue(const char *body, const char *key_name, char *out, size_t cap)
{
	char needle[80];
	const char *p;
	size_t n;

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
	n = mmJsonDecodeQuotedString(p, out, cap);
	return n > 0U ? TRUE : FALSE;
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

static sb32 mmJsonCopyBoolField(const char *body, const char *key_name, sb32 *out_val);
#if defined(SSB64_NETPLAY_ICE)
static void mmParseIceConnectFromBody(const char *body, MmMatchResult *r);
#endif

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
	r->peer_reports_lan = (r->peer_lan_hostport[0] != '\0') ? TRUE : FALSE;
	(void)mmJsonCopyBoolField(body, "peer_reports_lan", &r->peer_reports_lan);
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
	if (r->peer_reports_lan == FALSE)
	{
		port_log("SSB64 Automatch: match JSON peer_reports_lan=false (opponent did not report lan_endpoint)\n");
	}
	else if (r->peer_lan_hostport[0] == '\0')
	{
		port_log("SSB64 Automatch: match JSON peer_reports_lan=true but peer_lan empty\n");
	}
	else
	{
		port_log("SSB64 Automatch: match JSON peer_lan=%s peer_reports_lan=true\n", r->peer_lan_hostport);
	}
#if defined(SSB64_NETPLAY_ICE)
	if (r->peer_ice_sdp[0] == '\0')
	{
		port_log("SSB64 Automatch: match JSON peer_ice_sdp empty (peer may have queued without ice_sdp)\n");
	}
	else
	{
		size_t ice_len = strlen(r->peer_ice_sdp);
		char ice_prefix[68];

		memset(ice_prefix, 0, sizeof(ice_prefix));
		snprintf(ice_prefix, sizeof(ice_prefix), "%.64s", r->peer_ice_sdp);
		port_log("SSB64 Automatch: match JSON peer_ice_sdp len=%zu prefix=%s has_ufrag=%d\n", ice_len, ice_prefix,
		         (int)mmIceSdpHasIceUfrag(r->peer_ice_sdp));
		if (mmIceSdpHasIceUfrag(r->peer_ice_sdp) == FALSE)
		{
			port_log("SSB64 Automatch: WARNING peer_ice_sdp missing a=ice-ufrag (opponent may have queued too early)\n");
		}
	}
#endif
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
#if defined(SSB64_NETPLAY_ICE)
	mmParseIceConnectFromBody(body, r);
#endif
	(void)mmJsonCopyQuotedValue(body, "match_id", r->match_id, sizeof(r->match_id));
	(void)mmJsonCopyQuotedValue(body, "peer_player_id", r->peer_player_id, sizeof(r->peer_player_id));
	r->kind = MM_POLL_MATCHED;
	return TRUE;
}

#define MM_HTTPS_DEFAULT_TIMEOUT_SEC 30L
#define MM_HTTPS_TRICKLE_TIMEOUT_SEC 8L

#if defined(_MSC_VER)
#define MM_THREAD_LOCAL __declspec(thread)
#else
#define MM_THREAD_LOCAL __thread
#endif

/*
 * Per-thread reusable curl handle. mmHttpsRequestInternal runs on BOTH the matchmaking
 * worker (SSB64MmWorker) and the game thread (sync ICE trickle / role-ready), so the handle
 * must be thread-local — a single shared CURL* is not thread-safe. Reusing one handle per
 * thread keeps the HTTPS connection alive (keep-alive) and the c-ares DNS cache warm across
 * the rapid queue-poll / ICE trickle burst, instead of curl_easy_init()+cleanup() per call.
 * That collapses the per-request DNS+TLS+socket open/close churn that maximizes the
 * bionic-fdsan fd-reuse window on Android (see docs/bugs/android_ice_connect_fdsan_2026-05-30.md
 * and android_matchmaking_cares_dns_fdsan_2026-06-26.md).
 */
static MM_THREAD_LOCAL CURL *sTlsCurlHandle;

static CURL *mmCurlThreadHandle(void)
{
	if (sTlsCurlHandle == NULL)
	{
		sTlsCurlHandle = curl_easy_init();
	}
	else
	{
		/* Clears options but preserves the connection cache + DNS cache for reuse. */
		curl_easy_reset(sTlsCurlHandle);
	}
	return sTlsCurlHandle;
}

static void mmCurlThreadHandleRelease(void)
{
	if (sTlsCurlHandle != NULL)
	{
		curl_easy_cleanup(sTlsCurlHandle);
		sTlsCurlHandle = NULL;
	}
}

static long mmHttpsRequestInternal(const char *method, const char *path_suffix, const char *json_body, sb32 verb,
                                   char **resp_body_out, sb32 ice_serialize, long curl_timeout_sec)
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

	c = mmCurlThreadHandle();

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
	curl_easy_setopt(c, CURLOPT_TIMEOUT, (curl_timeout_sec > 0L) ? curl_timeout_sec : MM_HTTPS_DEFAULT_TIMEOUT_SEC);
	mmCurlConfigureSsl(c);

#if defined(__ANDROID__)
	/*
	 * Android curl is built with c-ares (no threaded system resolver) so the matchmaking
	 * worker never calls Android getaddrinfo -> netd/DnsResolver Binder, whose Parcel-owned
	 * fds were double-closed under bionic fdsan (SIGABRT at matchmaking entry).
	 * c-ares cannot auto-discover DNS servers on Android (net.dns* sysprops gone since 8.0),
	 * so supply them explicitly. Overridable for restricted networks; a future hardening can
	 * use ares_library_init_android() to read the device's configured resolvers.
	 */
	{
		const char *dns_servers = getenv("SSB64_MATCHMAKING_DNS_SERVERS");

		if ((dns_servers == NULL) || (dns_servers[0] == '\0'))
		{
			dns_servers = "1.1.1.1,8.8.8.8,9.9.9.9";
		}
		(void)curl_easy_setopt(c, CURLOPT_DNS_SERVERS, dns_servers);
	}
#endif

	if (ice_serialize != FALSE)
	{
		mnVSNetAutomatchAMIceHttpsLockBeforeRequest();
	}
	curl_code = curl_easy_perform(c);
	if (ice_serialize != FALSE)
	{
		mnVSNetAutomatchAMIceHttpsUnlockAfterRequest();
	}

	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
	{
		double total_sec = 0.0;

		if ((curl_easy_getinfo(c, CURLINFO_TOTAL_TIME, &total_sec) == CURLE_OK) && (total_sec > 0.0))
		{
			sLastHttpsRttMs = total_sec * 1000.0;
		}
	}
	/*
	 * Keep the thread-local handle alive for connection reuse; only free the per-request
	 * header list. The next call resets the handle (mmCurlThreadHandle) before re-setting
	 * CURLOPT_HTTPHEADER, so the freed slist is never referenced by a later perform.
	 */
	curl_slist_free_all(hdrs);

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

static long mmHttpsRequest(const char *method, const char *path_suffix, const char *json_body, sb32 verb,
                           char **resp_body_out)
{
	sb32 ice_serialize;

	ice_serialize = TRUE;
#if defined(SSB64_NETPLAY_ICE)
	if (mmIceShouldSerializeMatchmakingHttps() == FALSE)
	{
		ice_serialize = FALSE;
	}
#endif
	return mmHttpsRequestInternal(method, path_suffix, json_body, verb, resp_body_out, ice_serialize, 0L);
}

#if defined(SSB64_NETPLAY_ICE)
static void mmRunEnsurePushPlayerReady(sb32 verb)
{
	MmMatchResult ok;

	sCachedTurnValid = (mmMatchmakingFetchTurnCredentials(&sCachedTurnBundle) != FALSE) ? TRUE : FALSE;
#ifdef PORT
	if (verb != FALSE)
	{
		if (sCachedTurnValid != FALSE)
		{
			port_log("SSB64 Matchmaking: TURN credentials prefetched on worker\n");
		}
		else
		{
			port_log("SSB64 Matchmaking: TURN prefetch failed (ICE init will retry)\n");
		}
	}
#endif
	memset(&ok, 0, sizeof(ok));
	ok.kind = MM_POLL_PLAYER_READY;
	mmPushDone(&ok);
}

static void mmRunIcePlayerReady(const MmJob *job)
{
	MmMatchResult ok;
	const char *bind;

	bind = job->udp_endpoint;
	if ((bind == NULL) || (bind[0] == '\0'))
	{
		mmPushDoneError(0, "ICE player ready missing bind spec");
		return;
	}
	if (mnVSNetAutomatchAMIceInitOnWorker(bind) == FALSE)
	{
		mmPushDoneError(0, "ICE player ready failed");
		return;
	}
	memset(&ok, 0, sizeof(ok));
	ok.kind = MM_POLL_ICE_PLAYER_READY;
	mmPushDone(&ok);
}

static void mmRunIceReconnectInit(const MmJob *job)
{
	sb32 ok;

	(void)job;
	ok = mmIceReconnectInitOnWorker();
	mmIceReconnectWorkerInitFinished(ok);
}
#else
static void mmRunEnsurePushPlayerReady(sb32 verb)
{
	MmMatchResult ok;

	(void)verb;
	memset(&ok, 0, sizeof(ok));
	ok.kind = MM_POLL_PLAYER_READY;
	mmPushDone(&ok);
}
#endif /* SSB64_NETPLAY_ICE */

static void mmRunEnsure(sb32 verb)
{
	char *resp;
	long hc;
	char pid[96];
	char tok[288];

	(void)mmCredentialsEnsureReady(FALSE);
	if ((sPlayerId[0] != '\0') && (sApiToken[0] != '\0'))
	{
		if (mmCredVerifyLoaded(verb) != FALSE)
		{
#ifdef PORT
			if (verb != FALSE)
			{
				port_log("SSB64 Matchmaking: reusing cached player credential\n");
			}
#endif
			mmRunEnsurePushPlayerReady(verb);
			return;
		}
	}

	if (mmCredRepopulate(verb) != FALSE)
	{
		mmRunEnsurePushPlayerReady(verb);
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
			snprintf(sPlayerId, sizeof(sPlayerId), "%s", pid);
			snprintf(sApiToken, sizeof(sApiToken), "%s", tok);
			mmCredSave();
#ifdef PORT
			if (verb != FALSE)
			{
				port_log("SSB64 Matchmaking: POST /players ok player_id %.8s...\n", pid);
			}
#endif
			mmRunEnsurePushPlayerReady(verb);
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

static sb32 mmJsonInsertStringField(char *jbuf, size_t cap, const char *key, const char *val)
{
	char *end;
	char esc[4096];
	char frag[4352];
	size_t flen;

	if ((jbuf == NULL) || (key == NULL) || (val == NULL) || (val[0] == '\0'))
	{
		return FALSE;
	}
	mmJsonEscapeString(val, esc, sizeof(esc));
	end = strrchr(jbuf, '}');
	if (end == NULL)
	{
		return FALSE;
	}
	snprintf(frag, sizeof(frag), ",\"%s\":\"%s\"", key, esc);
	flen = strlen(frag);
	if ((size_t)(end - jbuf) + flen + strlen(end) + 1U >= cap)
	{
		return FALSE;
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
	return TRUE;
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
	char jbuf[MM_JOIN_JSON_CAP];
	long hc;
	char *resp;
	double rtt_ms;

	if (mmCredentialsEnsureReady(FALSE) == FALSE)
	{
		mmPushDoneError(401, "no credentials — call ensure player first");
		return;
	}
	rtt_ms = mmLastHttpsRttMsOrZero();
	/* region "auto": server resolves from CF-IPCountry (or default). */
	if (job->has_fighter_kind != FALSE)
	{
		if (job->has_lan_endpoint != FALSE)
		{
			snprintf(jbuf, sizeof(jbuf),
			         "{\"udp_endpoint\":\"%s\",\"lan_endpoint\":\"%s\",\"region\":\"auto\",\"game_version\":\"0.1.0\",\"fighter_kind\":%u,"
			         "\"rtt_ms_server\":%.1f,"
			         "\"jitter_ms\":0.0,\"loss_pct\":0.0,\"avg_fps\":60.0,\"fps_drops_per_min\":0.0}",
			         job->udp_endpoint, job->lan_endpoint, job->fighter_kind, rtt_ms);
		}
		else
		{
			snprintf(
			    jbuf, sizeof(jbuf),
			    "{\"udp_endpoint\":\"%s\",\"region\":\"auto\",\"game_version\":\"0.1.0\",\"fighter_kind\":%u,"
			    "\"rtt_ms_server\":%.1f,"
			    "\"jitter_ms\":0.0,\"loss_pct\":0.0,\"avg_fps\":60.0,\"fps_drops_per_min\":0.0}",
			    job->udp_endpoint, job->fighter_kind, rtt_ms);
		}
	}
	else
	{
		if (job->has_lan_endpoint != FALSE)
		{
			snprintf(jbuf, sizeof(jbuf),
			         "{\"udp_endpoint\":\"%s\",\"lan_endpoint\":\"%s\",\"region\":\"auto\",\"game_version\":\"0.1.0\","
			         "\"rtt_ms_server\":%.1f,"
			         "\"jitter_ms\":0.0,\"loss_pct\":0.0,\"avg_fps\":60.0,\"fps_drops_per_min\":0.0}",
			         job->udp_endpoint, job->lan_endpoint, rtt_ms);
		}
		else
		{
			snprintf(jbuf, sizeof(jbuf),
			         "{\"udp_endpoint\":\"%s\",\"region\":\"auto\",\"game_version\":\"0.1.0\","
			         "\"rtt_ms_server\":%.1f,"
			         "\"jitter_ms\":0.0,\"loss_pct\":0.0,\"avg_fps\":60.0,\"fps_drops_per_min\":0.0}",
			         job->udp_endpoint, rtt_ms);
		}
	}
	if (job->has_turn_endpoint != FALSE)
	{
		mmJsonInsertTurnEndpoint(jbuf, sizeof(jbuf), job->turn_endpoint);
	}
#if defined(SSB64_NETPLAY_ICE)
	if (job->has_ice_sdp != FALSE)
	{
		if (mmJsonInsertStringField(jbuf, sizeof(jbuf), "ice_sdp", job->ice_sdp) == FALSE)
		{
#ifdef PORT
			port_log("SSB64 Automatch: POST /v1/queue ice_sdp insert FAILED len=%zu cap=%u\n",
			         strlen(job->ice_sdp), (unsigned int)sizeof(jbuf));
#endif
		}
	}
#endif
#ifdef PORT
	port_log("SSB64 Automatch: POST /v1/queue udp=%s lan=%s turn=%s ice=%s\n", job->udp_endpoint,
	         (job->has_lan_endpoint != FALSE) ? job->lan_endpoint : "(none)",
	         (job->has_turn_endpoint != FALSE) ? job->turn_endpoint : "(none)",
	         (job->has_ice_sdp != FALSE) ? "yes" : "no");
	if (job->has_ice_sdp != FALSE)
	{
		port_log("SSB64 Automatch: POST /v1/queue ice_sdp len=%zu in_json=%d\n", strlen(job->ice_sdp),
		         (strstr(jbuf, "\"ice_sdp\"") != NULL) ? 1 : 0);
	}
#endif
	hc = mmHttpsRequestInternal("POST", "/v1/queue", jbuf, MM_VERBOSE(job->verbose), &resp, FALSE, 0L);
	if (((hc != 200) || (resp == NULL)) && (mmCredShouldRepopulate(hc, resp, TRUE) != FALSE) &&
	    (mmCredRepopulate(MM_VERBOSE(job->verbose)) != FALSE))
	{
		if (resp != NULL)
		{
			free(resp);
			resp = NULL;
		}
		hc = mmHttpsRequestInternal("POST", "/v1/queue", jbuf, MM_VERBOSE(job->verbose), &resp, FALSE, 0L);
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
			char region[32];

			memset(&out, 0, sizeof(out));
			out.kind = MM_POLL_QUEUED;
			snprintf(out.ticket_id, sizeof(out.ticket_id), "%s", tick);
			if (mmJsonCopyQuotedValue(resp, "region", region, sizeof(region)) != FALSE)
			{
#ifdef PORT
				port_log("SSB64 Automatch: queued region=%s rtt_ms=%.1f\n", region, mmLastHttpsRttMsOrZero());
#endif
			}
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
	double rtt_ms;
	long long client_ms;

	rtt_ms = mmLastHttpsRttMsOrZero();
	client_ms = mmClientUnixTimeMs();
	if (job->heartbeat_has_endpoints != FALSE)
	{
		if (job->has_lan_endpoint != FALSE)
		{
			snprintf(jbuf, sizeof(jbuf),
			         "{\"ticket_id\":\"%s\",\"client_time_ms\":%lld,\"last_server_rtt_ms\":%.1f,\"jitter_ms\":0.0,"
			         "\"loss_pct\":0.0,\"udp_endpoint\":\"%s\",\"lan_endpoint\":\"%s\"}",
			         job->ticket_id, (long long)client_ms, rtt_ms, job->udp_endpoint, job->lan_endpoint);
		}
		else
		{
			snprintf(jbuf, sizeof(jbuf),
			         "{\"ticket_id\":\"%s\",\"client_time_ms\":%lld,\"last_server_rtt_ms\":%.1f,\"jitter_ms\":0.0,"
			         "\"loss_pct\":0.0,\"udp_endpoint\":\"%s\"}",
			         job->ticket_id, (long long)client_ms, rtt_ms, job->udp_endpoint);
		}
	}
	else
	{
		snprintf(jbuf, sizeof(jbuf),
		         "{\"ticket_id\":\"%s\",\"client_time_ms\":%lld,\"last_server_rtt_ms\":%.1f,\"jitter_ms\":0.0,\"loss_pct\":0.0}",
		         job->ticket_id, (long long)client_ms, rtt_ms);
	}
	if (job->has_turn_endpoint != FALSE)
	{
		mmJsonInsertTurnEndpoint(jbuf, sizeof(jbuf), job->turn_endpoint);
	}
	hc = mmHttpsRequest("POST", "/v1/heartbeat", jbuf, MM_VERBOSE(job->verbose), &resp);
	if (hc == 404)
	{
		/* Ticket may already be matched; queue heartbeat is no longer valid. */
		MmMatchResult ok;

#ifdef PORT
		if (MM_VERBOSE(job->verbose))
		{
			port_log("SSB64 Matchmaking: heartbeat 404 (ignored, likely matched)\n");
		}
#endif
		memset(&ok, 0, sizeof(ok));
		ok.kind = MM_POLL_HEARTBEAT_OK;
		mmPushDone(&ok);
	}
	else if (((hc != 200) || (resp == NULL)) && (mmCredShouldRepopulate(hc, resp, FALSE) != FALSE) &&
	         (mmCredRepopulate(MM_VERBOSE(job->verbose)) != FALSE))
	{
		if (resp != NULL)
		{
			free(resp);
			resp = NULL;
		}
		hc = mmHttpsRequest("POST", "/v1/heartbeat", jbuf, MM_VERBOSE(job->verbose), &resp);
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
		if (MM_VERBOSE(job->verbose))
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
static u32 mmIceSignalQueuedCount(void);
static void mmRunIceRoleReady(const MmJob *job);
static sb32 sMmIceConnectPresent;
static sb32 sMmIcePeerControllingReady;
static char sMmIceLocalRole[16];

static sb32 mmParsePeerControllingReadyFromBody(const char *body, sb32 *out_ready)
{
	const char *ice_block;
	const char *field;

	if ((body == NULL) || (out_ready == NULL))
	{
		return FALSE;
	}
	*out_ready = FALSE;
	ice_block = strstr(body, "\"ice_connect\"");
	if (ice_block == NULL)
	{
		return FALSE;
	}
	field = strstr(ice_block, "\"peer_controlling_ready\"");
	if (field == NULL)
	{
		return FALSE;
	}
	return mmJsonCopyBoolField(field, "peer_controlling_ready", out_ready);
}

static void mmParseIceConnectFromBody(const char *body, MmMatchResult *r)
{
	sb32 present;
	sb32 peer_ready;
	sb32 peer_ready_parsed;
	sb32 prev_ready;
	char local_role[16];

	present = FALSE;
	peer_ready = FALSE;
	peer_ready_parsed = FALSE;
	prev_ready = sMmIcePeerControllingReady;
	local_role[0] = '\0';
	if ((body != NULL) && (strstr(body, "\"ice_connect\"") != NULL))
	{
		present = TRUE;
		if (mmParsePeerControllingReadyFromBody(body, &peer_ready) != FALSE)
		{
			peer_ready_parsed = TRUE;
		}
		else
		{
			peer_ready = prev_ready;
		}
		if (strstr(body, "\"local_role\":\"controlling\"") != NULL)
		{
			snprintf(local_role, sizeof(local_role), "controlling");
		}
		else if (strstr(body, "\"local_role\":\"controlled\"") != NULL)
		{
			snprintf(local_role, sizeof(local_role), "controlled");
		}
	}
	sMmIceConnectPresent = present;
	if (peer_ready_parsed != FALSE)
	{
		sMmIcePeerControllingReady = peer_ready;
	}
#ifdef PORT
	if ((peer_ready_parsed != FALSE) && (peer_ready != prev_ready))
	{
		port_log("SSB64 ICE: ice_connect peer_controlling_ready=%d\n", (int)peer_ready);
	}
#endif
	if (local_role[0] != '\0')
	{
		snprintf(sMmIceLocalRole, sizeof(sMmIceLocalRole), "%s", local_role);
	}
	if (r != NULL)
	{
		r->ice_connect_present = present;
		r->ice_peer_controlling_ready = peer_ready;
		r->ice_local_role[0] = '\0';
		if (local_role[0] != '\0')
		{
			snprintf(r->ice_local_role, sizeof(r->ice_local_role), "%s", local_role);
		}
		snprintf(r->ice_edge_id, sizeof(r->ice_edge_id), "pair");
	}
}

void mmMatchmakingIceConnectCacheReset(void)
{
	sMmIceConnectPresent = FALSE;
	sMmIcePeerControllingReady = FALSE;
	sMmIceLocalRole[0] = '\0';
}

sb32 mmMatchmakingIceConnectPresent(void)
{
	return sMmIceConnectPresent;
}

sb32 mmMatchmakingIcePeerControllingReady(void)
{
	return sMmIcePeerControllingReady;
}
#endif

#if defined(SSB64_NETPLAY_ICE)
static void mmProcessIceTricklePollResponse(long hc, const char *resp, sb32 verbose)
{
	static u32 sTricklePollSeq;
	static u32 sTricklePollLastSignalCount;
	u32 prev_signal_count;

	if (resp == NULL)
	{
		return;
	}
	prev_signal_count = mmIceSignalQueuedCount();
	sTricklePollSeq++;
	mmParseIceSignalsFromBody(resp);
	mmParseIceConnectFromBody(resp, NULL);
#ifdef PORT
	if (mmLogVerbose() &&
	    (((sTricklePollSeq % 8U) == 1U) || (mmIceSignalQueuedCount() != sTricklePollLastSignalCount)))
	{
		u32 signal_count = mmIceSignalQueuedCount();

		port_log("SSB64 Matchmaking: ICE trickle poll #%u http=%ld signals=%u (+%u)\n", sTricklePollSeq, hc,
		         signal_count,
		         (signal_count > prev_signal_count) ? (signal_count - prev_signal_count) : 0U);
	}
	sTricklePollLastSignalCount = mmIceSignalQueuedCount();
#else
	(void)hc;
	(void)verbose;
#endif
}
#endif /* SSB64_NETPLAY_ICE */

u32 mmMatchmakingAdaptivePollEvery(u32 base_interval)
{
	u32 depth;

	depth = mmMatchmakingApproxPendingJobs();
	if (depth >= 12U)
	{
		return base_interval * 8U;
	}
	if (depth >= 8U)
	{
		return base_interval * 4U;
	}
	if (depth >= 4U)
	{
		return base_interval * 2U;
	}
	return base_interval;
}

static void mmRunPoll(const MmJob *job)
{
	char url_path[288];
	long hc;
	char *resp;

#if defined(SSB64_NETPLAY_ICE)
	if (mnVSNetAutomatchAMIceWorkerMatchPollBlocked(job->poll_trickle_only != FALSE) != FALSE)
	{
		return;
	}
#endif
	sWorkerPollMatchActive = TRUE;
	snprintf(url_path, sizeof(url_path), "/v1/match/%s", job->ticket_id);
#if defined(SSB64_NETPLAY_ICE)
	if (job->poll_trickle_only != FALSE)
	{
#ifdef PORT
		port_log("SSB64 Matchmaking: ICE trickle worker GET /v1/match/%s\n", job->ticket_id);
#endif
		hc = mmHttpsRequestInternal("GET", url_path, NULL, MM_VERBOSE(job->verbose), &resp,
		                            mmIceShouldSerializeMatchmakingHttps(), MM_HTTPS_TRICKLE_TIMEOUT_SEC);
	}
	else
#endif
	{
		hc = mmHttpsRequest("GET", url_path, NULL, MM_VERBOSE(job->verbose), &resp);
	}

	if (((hc != 200) && (hc != 304)) || (resp == NULL))
	{
#ifdef PORT
		if ((hc == 0L) || (hc >= 500L))
		{
			port_log("SSB64 Matchmaking: GET match poll transient failure http=%ld ticket=%.36s\n",
			         hc, (job->ticket_id[0] != '\0') ? job->ticket_id : "(empty)");
		}
#endif
		mmPushDoneError(hc, "GET match poll failed");
	}
	else if (strstr(resp, "\"status\":\"queued\"") != NULL)
	{
#if defined(SSB64_NETPLAY_ICE)
		if (job->poll_trickle_only == FALSE)
		{
			mnVSNetAutomatchAMQueuePollNoteStillQueued();
		}
#endif
#ifdef PORT
		if (MM_VERBOSE(job->verbose))
		{
			port_log("SSB64 Matchmaking: still queued\n");
		}
#endif
	}
	else if (strstr(resp, "\"status\":\"matched\"") != NULL)
	{
#if defined(SSB64_NETPLAY_ICE)
		if (job->poll_trickle_only != FALSE)
		{
			mmProcessIceTricklePollResponse(hc, resp, MM_VERBOSE(job->verbose));
			{
				MmMatchResult ok;

				memset(&ok, 0, sizeof(ok));
				ok.kind = MM_POLL_HEARTBEAT_OK;
				mmPushDone(&ok);
			}
		}
		else
#endif
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
	}
	else
	{
		mmPushDoneError(hc, "unexpected poll payload");
	}
	if (resp != NULL)
	{
		free(resp);
	}
	sWorkerPollMatchActive = FALSE;
}

#if defined(SSB64_NETPLAY_ICE)
#define MM_ICE_SIGNAL_QUEUE 24

static char sMmIceSignalQ[MM_ICE_SIGNAL_QUEUE][280];
static u32 sMmIceSignalHead;
static u32 sMmIceSignalCount;
/* Worker thread pushes from GET /v1/match trickle parse; game thread pops in ICE connect tick. */
static pthread_mutex_t sIceSignalMutex = PTHREAD_MUTEX_INITIALIZER;

static u32 mmIceSignalQueuedCount(void)
{
	u32 count;

	(void)pthread_mutex_lock(&sIceSignalMutex);
	count = sMmIceSignalCount;
	(void)pthread_mutex_unlock(&sIceSignalMutex);
	return count;
}

static void mmIceSignalQueueClear(void)
{
	(void)pthread_mutex_lock(&sIceSignalMutex);
	sMmIceSignalHead = 0U;
	sMmIceSignalCount = 0U;
	(void)pthread_mutex_unlock(&sIceSignalMutex);
}

static void mmIceSignalQueuePush(const char *candidate)
{
	u32 tail;

	if ((candidate == NULL) || (candidate[0] == '\0'))
	{
		return;
	}
	(void)pthread_mutex_lock(&sIceSignalMutex);
	if (sMmIceSignalCount >= MM_ICE_SIGNAL_QUEUE)
	{
		sMmIceSignalHead = (sMmIceSignalHead + 1U) % MM_ICE_SIGNAL_QUEUE;
		sMmIceSignalCount--;
	}
	tail = (sMmIceSignalHead + sMmIceSignalCount) % MM_ICE_SIGNAL_QUEUE;
	snprintf(sMmIceSignalQ[tail], sizeof(sMmIceSignalQ[tail]), "%s", candidate);
	sMmIceSignalCount++;
	(void)pthread_mutex_unlock(&sIceSignalMutex);
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
		{
			char tmp[280];
			const char *next;
			size_t n;

			n = mmJsonDecodeQuotedString(p, tmp, sizeof(tmp));
			if (n == 0U)
			{
				break;
			}
			mmIceSignalQueuePush(tmp);
			next = mmJsonSkipPastQuotedString(p);
			if (next == NULL || next == p)
			{
				break;
			}
			p = next;
		}
	}
}

sb32 mmMatchmakingPopIceCandidate(char *out, u32 out_cap)
{
	sb32 ok;

	if (out == NULL || out_cap < 8U)
	{
		return FALSE;
	}
	(void)pthread_mutex_lock(&sIceSignalMutex);
	if (sMmIceSignalCount == 0U)
	{
		(void)pthread_mutex_unlock(&sIceSignalMutex);
		return FALSE;
	}
	snprintf(out, out_cap, "%s", sMmIceSignalQ[sMmIceSignalHead]);
	sMmIceSignalHead = (sMmIceSignalHead + 1U) % MM_ICE_SIGNAL_QUEUE;
	sMmIceSignalCount--;
	ok = TRUE;
	(void)pthread_mutex_unlock(&sIceSignalMutex);
	return ok;
}

u32 mmMatchmakingIceSignalsQueuedCount(void)
{
	return mmIceSignalQueuedCount();
}

void mmMatchmakingIceSignalsClear(void)
{
	mmIceSignalQueueClear();
}

static void mmRunIceSignal(const MmJob *job)
{
	char jbuf[640];
	char esc[320];
	long hc;
	char *resp;
	char url_path[288];

	if (mmCredentialsEnsureReady(FALSE) == FALSE)
	{
		return;
	}
	snprintf(url_path, sizeof(url_path), "/v1/match/%s/ice", job->ticket_id);
	mmJsonEscapeString(job->ice_candidate, esc, sizeof(esc));
	snprintf(jbuf, sizeof(jbuf), "{\"candidate\":\"%s\"}", esc);
	hc = mmHttpsRequestInternal("POST", url_path, jbuf, MM_VERBOSE(job->verbose), &resp,
	                            mmIceShouldSerializeMatchmakingHttps(), 0L);
	if (resp != NULL)
	{
		free(resp);
	}
	(void)hc;
}

static void mmRunIceRoleReady(const MmJob *job)
{
	char jbuf[128];
	char esc_edge[32];
	long hc;
	char *resp;
	char url_path[320];
	const char *edge;

	if (mmCredentialsEnsureReady(FALSE) == FALSE)
	{
		return;
	}
	edge = (job->ice_edge_id[0] != '\0') ? job->ice_edge_id : "pair";
	snprintf(url_path, sizeof(url_path), "/v1/match/%s/ice/role-ready", job->ticket_id);
	mmJsonEscapeString(edge, esc_edge, sizeof(esc_edge));
	snprintf(jbuf, sizeof(jbuf), "{\"edge_id\":\"%s\",\"connect_epoch\":%u}", esc_edge,
	         (unsigned int)job->ice_connect_epoch);
	hc = mmHttpsRequestInternal("POST", url_path, jbuf, MM_VERBOSE(job->verbose), &resp, FALSE, 0L);
	if (resp != NULL)
	{
		free(resp);
	}
#ifdef PORT
	if (((hc < 200) || (hc >= 300)) || mmLogVerbose())
	{
		port_log("SSB64 Matchmaking: POST ice/role-ready http=%ld ticket=%.36s edge=%s\n", hc, job->ticket_id, edge);
	}
#endif
	(void)hc;
}

void mmMatchmakingPostIceRoleReadySync(const char *ticket_id, const char *edge_id, u32 connect_epoch)
{
	MmJob job;

	memset(&job, 0, sizeof(job));
	job.kind = MM_JOB_ICE_ROLE_READY;
	job.verbose = FALSE;
	job.ice_connect_epoch = (connect_epoch != 0U) ? connect_epoch : 1U;
	if (ticket_id != NULL)
	{
		snprintf(job.ticket_id, sizeof(job.ticket_id), "%s", ticket_id);
	}
	if ((edge_id != NULL) && (edge_id[0] != '\0'))
	{
		snprintf(job.ice_edge_id, sizeof(job.ice_edge_id), "%s", edge_id);
	}
	else
	{
		snprintf(job.ice_edge_id, sizeof(job.ice_edge_id), "pair");
	}
	mmRunIceRoleReady(&job);
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

	if (mmCredentialsEnsureReady(mmLogVerbose()) == FALSE)
	{
#ifdef PORT
		if (mmLogVerbose())
		{
			port_log("SSB64 Matchmaking: GET /v1/turn-credentials skipped (credentials not ready)\n");
		}
#endif
		return FALSE;
	}
	hc = mmHttpsRequest("GET", "/v1/turn-credentials", NULL, mmLogVerbose(), &resp);
	if ((hc != 200) || (resp == NULL))
	{
#ifdef PORT
		if (mmLogVerbose())
		{
			port_log("SSB64 Matchmaking: GET /v1/turn-credentials failed http=%ld%s\n", hc,
			         (resp == NULL) ? " (no body)" : "");
		}
#endif
		if (resp != NULL)
		{
			free(resp);
		}
		return FALSE;
	}
	if ((mmJsonCopyQuotedValue(resp, "username", out->turn_user, sizeof(out->turn_user)) == FALSE) ||
	    (mmJsonCopyQuotedValue(resp, "password", out->turn_pass, sizeof(out->turn_pass)) == FALSE))
	{
#ifdef PORT
		if (mmLogVerbose())
		{
			port_log("SSB64 Matchmaking: GET /v1/turn-credentials JSON parse failed (username/password)\n");
		}
#endif
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

sb32 mmMatchmakingTryGetCachedTurnCredentials(MmIceTurnBundle *out)
{
	if ((out == NULL) || (sCachedTurnValid == FALSE))
	{
		return FALSE;
	}
	*out = sCachedTurnBundle;
	return TRUE;
}
#endif /* SSB64_NETPLAY_ICE */

static void mmRunCancel(const MmJob *job)
{
	char jbuf[192];
	long hc;
	char *resp;

	snprintf(jbuf, sizeof(jbuf), "{\"ticket_id\":\"%s\"}", job->ticket_id);
	hc = mmHttpsRequest("POST", "/v1/queue/cancel", jbuf, MM_VERBOSE(job->verbose), &resp);

	if ((((hc >= 200) && (hc <= 299)) || (hc == 404)))
	{
		MmMatchResult ok;

		memset(&ok, 0, sizeof(ok));
		ok.kind = MM_POLL_CANCEL_OK;
		mmPushDone(&ok);
	}
	else if ((mmCredShouldRepopulate(hc, resp, FALSE) != FALSE) &&
	         (mmCredRepopulate(MM_VERBOSE(job->verbose)) != FALSE))
	{
		if (resp != NULL)
		{
			free(resp);
			resp = NULL;
		}
		hc = mmHttpsRequest("POST", "/v1/queue/cancel", jbuf, MM_VERBOSE(job->verbose), &resp);
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

void mmMatchmakingDropPendingPollMatchJobs(const char *ticket_id)
{
	u32 write_count;
	u32 i;

	if ((ticket_id == NULL) || (ticket_id[0] == '\0'))
	{
		return;
	}
	pthread_mutex_lock(&sMutex);
	write_count = 0U;
	for (i = 0U; i < sJobCount; i++)
	{
		u32 idx = (sJobHead + i) % MM_JOB_QUEUE_DEPTH;
		MmJob *jp = &sJobQ[idx];

		if ((jp->kind == MM_JOB_POLL_MATCH) && (strcmp(jp->ticket_id, ticket_id) == 0))
		{
			continue;
		}
		if (write_count != i)
		{
			sJobQ[(sJobHead + write_count) % MM_JOB_QUEUE_DEPTH] = sJobQ[idx];
		}
		write_count++;
	}
	sJobCount = write_count;
	sJobTail = (sJobHead + write_count) % MM_JOB_QUEUE_DEPTH;
	pthread_mutex_unlock(&sMutex);
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
			mmRunEnsure(MM_VERBOSE(cur.verbose));
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
		case MM_JOB_ICE_ROLE_READY:
			mmRunIceRoleReady(&cur);
			break;
		case MM_JOB_ICE_PLAYER_READY:
			mmRunIcePlayerReady(&cur);
			break;
		case MM_JOB_ICE_RECONNECT_INIT:
			mmRunIceReconnectInit(&cur);
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

static void mmWorkerSetThreadName(void)
{
#if defined(__linux__)
	(void)prctl(PR_SET_NAME, "SSB64MmWorker", 0, 0, 0);
#endif
}

static void *mmWorkerPthreadThunk(void *p)
{
	mmWorkerSetThreadName();
	(void)p;
	mmJobWorkerLoop(NULL);
	/* Tear down this thread's reused curl handle (and its keep-alive socket) before join. */
	mmCurlThreadHandleRelease();
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

#ifdef PORT
	{
		/*
		 * Confirm the resolver backend at runtime. On Android the c-ares async resolver
		 * (async_dns=1, c-ares=<ver>) is required to avoid the getaddrinfo->netd Binder
		 * Parcel-fd double-close under fdsan. async_dns=0 / c-ares=(none) means the build
		 * fell back to the threaded system resolver and the entry crash will recur.
		 */
		curl_version_info_data *vinfo = curl_version_info(CURLVERSION_NOW);
		const char *ares_ver =
		    ((vinfo != NULL) && (vinfo->ares != NULL) && (vinfo->ares[0] != '\0')) ? vinfo->ares : "(none)";
		int async_dns = ((vinfo != NULL) && ((vinfo->features & CURL_VERSION_ASYNCHDNS) != 0)) ? 1 : 0;
		port_log("SSB64 Matchmaking: curl %s ssl=%s async_dns=%d c-ares=%s\n",
		         (vinfo != NULL) ? vinfo->version : "?",
		         ((vinfo != NULL) && (vinfo->ssl_version != NULL)) ? vinfo->ssl_version : "?", async_dns,
		         ares_ver);
	}
#endif

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
#if defined(SSB64_NETPLAY_ICE)
	sCachedTurnValid = FALSE;
#endif

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
#if defined(SSB64_NETPLAY_ICE)
	j.poll_trickle_only = FALSE;
#endif
	if (ticket_id != NULL)
	{
		snprintf(j.ticket_id, sizeof(j.ticket_id), "%s", ticket_id);
	}
	mmEnqueueSubmit(&j);
}

#if defined(SSB64_NETPLAY_ICE)
void mmMatchmakingEnqueuePollIceTrickle(sb32 verbose, const char *ticket_id)
{
	MmJob j;

	/*
	 * Central gate: shared-LAN / CONNECTED / wall-clock throttle. Stops the
	 * ~30 Hz CONNECTING GET storm even if a caller bypasses ConnectTricklePollInterval.
	 */
	if (mnVSNetAutomatchAMIceConnectTrickleMayEnqueue() == FALSE)
	{
		return;
	}
	memset(&j, 0, sizeof(j));
	j.kind = MM_JOB_POLL_MATCH;
	j.verbose = verbose;
	j.poll_trickle_only = TRUE;
	if (ticket_id != NULL)
	{
		snprintf(j.ticket_id, sizeof(j.ticket_id), "%s", ticket_id);
	}
	mmEnqueueSubmit(&j);
	mnVSNetAutomatchAMIceConnectTrickleNoteEnqueued();
}

sb32 mmMatchmakingPollMatchIceTrickleSyncEx(const char *ticket_id, sb32 ice_serialize)
{
	char url_path[288];
	long hc;
	char *resp;

	if ((ticket_id == NULL) || (ticket_id[0] == '\0'))
	{
		return FALSE;
	}
	snprintf(url_path, sizeof(url_path), "/v1/match/%s", ticket_id);
	hc = mmHttpsRequestInternal("GET", url_path, NULL, FALSE, &resp, ice_serialize, MM_HTTPS_TRICKLE_TIMEOUT_SEC);
	if (((hc == 200) || (hc == 304)) && (resp != NULL))
	{
		if (strstr(resp, "\"status\":\"matched\"") != NULL)
		{
			mmProcessIceTricklePollResponse(hc, resp, FALSE);
		}
		free(resp);
		return TRUE;
	}
	if (resp != NULL)
	{
		free(resp);
	}
	return FALSE;
}

sb32 mmMatchmakingPollMatchIceTrickleSync(const char *ticket_id)
{
	return mmMatchmakingPollMatchIceTrickleSyncEx(ticket_id, mmIceShouldSerializeMatchmakingHttps());
}
#endif

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
                                      sb32 has_fkind, const char *lan_endpoint_opt, const char *turn_endpoint_opt)
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
	if ((turn_endpoint_opt != NULL) && (turn_endpoint_opt[0] != '\0'))
	{
		snprintf(j.turn_endpoint, sizeof(j.turn_endpoint), "%s", turn_endpoint_opt);
		j.has_turn_endpoint = TRUE;
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

void mmMatchmakingEnqueueIceRoleReady(sb32 verbose, const char *ticket_id, const char *edge_id, u32 connect_epoch)
{
	MmJob j;

	memset(&j, 0, sizeof(j));
	j.kind = MM_JOB_ICE_ROLE_READY;
	j.verbose = verbose;
	j.ice_connect_epoch = (connect_epoch != 0U) ? connect_epoch : 1U;
	if (ticket_id != NULL)
	{
		snprintf(j.ticket_id, sizeof(j.ticket_id), "%s", ticket_id);
	}
	if ((edge_id != NULL) && (edge_id[0] != '\0'))
	{
		snprintf(j.ice_edge_id, sizeof(j.ice_edge_id), "%s", edge_id);
	}
	else
	{
		snprintf(j.ice_edge_id, sizeof(j.ice_edge_id), "pair");
	}
	mmEnqueueSubmit(&j);
}

void mmMatchmakingEnqueueIcePlayerReady(sb32 verbose, const char *bind_spec)
{
	MmJob j;

	memset(&j, 0, sizeof(j));
	j.kind = MM_JOB_ICE_PLAYER_READY;
	j.verbose = verbose;
	if ((bind_spec != NULL) && (bind_spec[0] != '\0'))
	{
		snprintf(j.udp_endpoint, sizeof(j.udp_endpoint), "%s", bind_spec);
	}
	mmEnqueueSubmit(&j);
}

void mmMatchmakingEnqueueIceReconnectInit(sb32 verbose)
{
	MmJob j;

	memset(&j, 0, sizeof(j));
	j.kind = MM_JOB_ICE_RECONNECT_INIT;
	j.verbose = verbose;
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

sb32 mmMatchmakingPollMatchOutstanding(const char *ticket_id)
{
	sb32 queued;

	if (sWorkerPollMatchActive != FALSE)
	{
		return TRUE;
	}
	if ((ticket_id == NULL) || (ticket_id[0] == '\0'))
	{
		return FALSE;
	}
	pthread_mutex_lock(&sMutex);
	queued = mmJobQueueContainsPollForTicketLocked(ticket_id);
	pthread_mutex_unlock(&sMutex);
	return queued;
}

static sb32 mmJsonCopyU64Field(const char *body, const char *key_name, u64 *out_val)
{
	char needle[80];
	const char *p;
	u64 v;

	if ((body == NULL) || (key_name == NULL) || (out_val == NULL))
	{
		return FALSE;
	}
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
	if ((*p < '0') || (*p > '9'))
	{
		return FALSE;
	}
	v = 0U;
	while ((*p >= '0') && (*p <= '9'))
	{
		v = (v * 10U) + (u64)(*p - '0');
		p++;
	}
	*out_val = v;
	return TRUE;
}

static sb32 mmJsonCopyBoolField(const char *body, const char *key_name, sb32 *out_val)
{
	char needle[80];
	const char *p;

	if ((body == NULL) || (key_name == NULL) || (out_val == NULL))
	{
		return FALSE;
	}
	snprintf(needle, sizeof(needle), "\"%s\":", key_name);
	p = strstr(body, needle);
	if (p == NULL)
	{
		return FALSE;
	}
	p += strlen(needle);
	while ((*p == ' ') || (*p == '\t'))
	{
		p++;
	}
	if (strncmp(p, "true", 4) == 0)
	{
		*out_val = TRUE;
		return TRUE;
	}
	if (strncmp(p, "false", 5) == 0)
	{
		*out_val = FALSE;
		return TRUE;
	}
	return FALSE;
}

long mmMatchmakingHttpsRequest(const char *method, const char *path_suffix, const char *json_body, sb32 verbose,
                               char **resp_body_out)
{
	return mmHttpsRequest(method, path_suffix, json_body, verbose, resp_body_out);
}

sb32 mmMatchmakingJsonCopyQuotedValue(const char *body, const char *key_name, char *out, size_t cap)
{
	return mmJsonCopyQuotedValue(body, key_name, out, cap);
}

sb32 mmMatchmakingJsonCopyU64Field(const char *body, const char *key_name, u64 *out_val)
{
	return mmJsonCopyU64Field(body, key_name, out_val);
}

sb32 mmMatchmakingJsonCopyBoolField(const char *body, const char *key_name, sb32 *out_val)
{
	return mmJsonCopyBoolField(body, key_name, out_val);
}

sb32 mmMatchmakingGetLocalPlayerId(char *out, u32 out_cap)
{
	if ((out == NULL) || (out_cap < 8U))
	{
		return FALSE;
	}
	if (sPlayerId[0] == '\0')
	{
		return FALSE;
	}
	snprintf(out, out_cap, "%s", sPlayerId);
	return TRUE;
}

void mmMatchmakingPostMatchResultSync(const char *match_id, const char *winner_id, const char *loser_id,
                                      const char *reason)
{
	char url_path[320];
	char jbuf[512];
	char esc_w[160];
	char esc_l[160];
	char esc_r[64];
	long hc;
	char *resp;

	if ((match_id == NULL) || (winner_id == NULL) || (loser_id == NULL))
	{
		return;
	}
	if (mmCredentialsEnsureReady(FALSE) == FALSE)
	{
		return;
	}
	snprintf(url_path, sizeof(url_path), "/v1/match/%s/result", match_id);
	mmJsonEscapeString(winner_id, esc_w, sizeof(esc_w));
	mmJsonEscapeString(loser_id, esc_l, sizeof(esc_l));
	if ((reason != NULL) && (reason[0] != '\0'))
	{
		mmJsonEscapeString(reason, esc_r, sizeof(esc_r));
		snprintf(jbuf, sizeof(jbuf),
		         "{\"winner_player_id\":\"%s\",\"loser_player_id\":\"%s\",\"reason\":\"%s\"}", esc_w, esc_l, esc_r);
	}
	else
	{
		snprintf(jbuf, sizeof(jbuf), "{\"winner_player_id\":\"%s\",\"loser_player_id\":\"%s\"}", esc_w, esc_l);
	}
	hc = mmMatchmakingHttpsRequest("POST", url_path, jbuf, FALSE, &resp);
#ifdef PORT
	port_log("SSB64 Matchmaking: POST match result http=%ld match=%.36s reason=%s\n", hc, match_id,
	         (reason != NULL) ? reason : "normal");
#endif
	if (resp != NULL)
	{
		free(resp);
	}
	(void)hc;
}

sb32 mmMatchmakingFetchMatchOutcomeSync(const char *match_id, char *status_out, u32 status_cap, char *winner_out,
                                        u32 winner_cap, char *loser_out, u32 loser_cap, char *reason_out,
                                        u32 reason_cap)
{
	char url_path[320];
	long hc;
	char *resp;
	sb32 ok;

	if (match_id == NULL)
	{
		return FALSE;
	}
	if (mmCredentialsEnsureReady(FALSE) == FALSE)
	{
		return FALSE;
	}
	snprintf(url_path, sizeof(url_path), "/v1/match/%s/outcome", match_id);
	hc = mmMatchmakingHttpsRequest("GET", url_path, NULL, FALSE, &resp);
	ok = FALSE;
	if ((hc >= 200L) && (hc < 300L) && (resp != NULL))
	{
		if (status_out != NULL && status_cap > 0U)
		{
			(void)mmJsonCopyQuotedValue(resp, "status", status_out, status_cap);
		}
		if (winner_out != NULL && winner_cap > 0U)
		{
			(void)mmJsonCopyQuotedValue(resp, "winner_player_id", winner_out, winner_cap);
		}
		if (loser_out != NULL && loser_cap > 0U)
		{
			(void)mmJsonCopyQuotedValue(resp, "loser_player_id", loser_out, loser_cap);
		}
		if (reason_out != NULL && reason_cap > 0U)
		{
			(void)mmJsonCopyQuotedValue(resp, "reason", reason_out, reason_cap);
		}
		ok = TRUE;
	}
	if (resp != NULL)
	{
		free(resp);
	}
	return ok;
}

long mmMatchmakingPostIceRestartSync(const char *ticket_id, u32 connect_epoch)
{
	char url_path[320];
	char jbuf[96];
	long hc;
	char *resp;

	if ((ticket_id == NULL) || (ticket_id[0] == '\0'))
	{
		return 0L;
	}
	if (mmCredentialsEnsureReady(FALSE) == FALSE)
	{
		return 0L;
	}
	snprintf(url_path, sizeof(url_path), "/v1/match/%s/ice/restart", ticket_id);
	snprintf(jbuf, sizeof(jbuf), "{\"connect_epoch\":%u}", (unsigned int)connect_epoch);
	hc = mmMatchmakingHttpsRequest("POST", url_path, jbuf, FALSE, &resp);
#ifdef PORT
	port_log("SSB64 Matchmaking: POST ice/restart http=%ld ticket=%.36s epoch=%u\n", hc, ticket_id,
	         (unsigned int)connect_epoch);
#endif
	if (resp != NULL)
	{
		free(resp);
	}
	return hc;
}

#endif /* PORT && SSB64_NETMENU */
