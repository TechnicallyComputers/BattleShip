#include "mm_matchmaking.h"

#if defined(PORT) && defined(SSB64_NETMENU)

#include <macros.h>

#include <ssb64_paths_capi.h>

#include <curl/curl.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
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
#include <time.h>
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

static sb32 mmFileReadable(const char *path)
{
	struct stat st;

	if ((path == NULL) || (path[0] == '\0'))
	{
		return FALSE;
	}
	if (stat(path, &st) != 0)
	{
		return FALSE;
	}
#ifdef _WIN32
	return (st.st_mode & _S_IFREG) != 0;
#else
	return S_ISREG(st.st_mode);
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
#ifdef _WIN32
	if (_fullpath(out, path, cap) != NULL)
	{
		return TRUE;
	}
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
#ifdef _WIN32
	(void)_putenv_s(name, value);
#else
	(void)setenv(name, value, 0);
#endif
}

static sb32 sCaBundleInitDone = FALSE;
static char sCaBundlePath[512];

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

	mmSetenvIfUnset("SSB64_MATCHMAKING_CA_BUNDLE", sCaBundlePath);
	mmSetenvIfUnset("CURL_CA_BUNDLE", sCaBundlePath);
	mmSetenvIfUnset("SSL_CERT_FILE", sCaBundlePath);
#ifdef PORT
	port_log("SSB64 Matchmaking: CA bundle %s\n", sCaBundlePath);
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

/* Portable builds bundle curl+OpenSSL but not the host CA store (Linux AppRun sets env; Windows sets in mmMatchmakingInitPortableCaBundle). */
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
	port_log("SSB64 Matchmaking: WARNING CURLOPT_CAINFO not set — TLS verification may fail (OpenSSL curl)\n");
#endif
}

/* Persist matchmaking API token outside the install dir (port/net/stdlib.h shadows <stdlib.h>). */
static void mmCredPath(char *out, size_t cap)
{
#ifdef _WIN32
	const char *appdata = getenv("APPDATA");

	if ((appdata != NULL) && (appdata[0] != '\0'))
	{
		snprintf(out, cap, "%s\\ssb64\\%s", appdata, MM_CRED_FILENAME);
	}
	else
	{
		snprintf(out, cap, ".\\%s", MM_CRED_FILENAME);
	}
#else
	const char *xdg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");

	if ((xdg != NULL) && (xdg[0] != '\0'))
	{
		snprintf(out, cap, "%s/ssb64/%s", xdg, MM_CRED_FILENAME);
	}
	else if ((home != NULL) && (home[0] != '\0'))
	{
		snprintf(out, cap, "%s/.config/ssb64/%s", home, MM_CRED_FILENAME);
	}
	else
	{
		snprintf(out, cap, "./%s", MM_CRED_FILENAME);
	}
#endif
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

sb32 mmMatchmakingLoadCredentials(sb32 verbose)
{
	FILE *fp;
	char path[512];
	char line[512];
	char key[96];
	char val[320];

	mmCredPath(path, sizeof(path));
	fp = fopen(path, "r");
	if (fp == NULL)
	{
		return FALSE;
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
	if ((verbose != FALSE) && ((sPlayerId[0] != '\0')))
	{
#ifdef PORT
		port_log("SSB64 Matchmaking: loaded cred player_id prefix %.8s...\n", sPlayerId);
#endif
	}
	return (sPlayerId[0] != '\0') && (sApiToken[0] != '\0');
}

static sb32 mmJsonCopyQuotedValue(const char *body, const char *key_name, char *out, size_t cap)
{
	char needle[80];
	size_t lk;
	const char *p;
	size_t wi;

	snprintf(needle, sizeof(needle), "\"%s\"", key_name);
	p = strstr(body, needle);
	if (p == NULL)
	{
		return FALSE;
	}
	lk = strlen(key_name) + 2U;
	p += lk;
	while ((*p != '\0') && (*p != ':'))
	{
		p++;
	}
	if (*p != ':')
	{
		return FALSE;
	}
	p++;
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
	if ((sPlayerId[0] != '\0'))
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
	hc = mmHttpsRequest("POST", "/v1/queue", jbuf, job->verbose != FALSE, &resp);
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
	char jbuf[256];
	long hc;
	char *resp;

	snprintf(jbuf, sizeof(jbuf),
	         "{\"ticket_id\":\"%s\",\"client_time_ms\":0,\"last_server_rtt_ms\":0.0,\"jitter_ms\":0.0,\"loss_pct\":0.0}",
	         job->ticket_id);
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
	mmEnqueueSubmit(&j);
}

void mmMatchmakingEnqueueHeartbeat(sb32 verbose, const char *ticket_id)
{
	MmJob j;

	memset(&j, 0, sizeof(j));
	j.kind = MM_JOB_HEARTBEAT;
	j.verbose = verbose;
	if (ticket_id != NULL)
	{
		snprintf(j.ticket_id, sizeof(j.ticket_id), "%s", ticket_id);
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
