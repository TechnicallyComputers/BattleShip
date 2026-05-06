#include "mm_matchmaking.h"

#if defined(PORT) && defined(SSB64_NETMENU) && !defined(_WIN32)

#include <macros.h>

#include <curl/curl.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef PORT
extern void port_log(const char *fmt, ...);
#endif

#ifndef MM_DEFAULT_BASE_URL
#define MM_DEFAULT_BASE_URL "http://216.154.76.149:8899"
#endif
#define MM_QUEUE_DEPTH 16
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
static sb32 sWorkerRunning;
static sb32 sWorkerSpawned;

static MmJob sJobQ[MM_QUEUE_DEPTH];
static u32 sJobHead;
static u32 sJobTail;
static u32 sJobCount;

static MmMatchResult sDoneQ[MM_QUEUE_DEPTH];
static u32 sDoneHead;
static u32 sDoneTail;
static u32 sDoneCount;

static char sBaseUrl[192];
static char sPlayerId[48];
static char sApiToken[192];

static void mmPushDone(const MmMatchResult *r)
{
	pthread_mutex_lock(&sDoneMutex);
	if (sDoneCount >= MM_QUEUE_DEPTH)
	{
#ifdef PORT
		port_log("SSB64 Matchmaking: dropped completed event (overflow)\n");
#endif
		pthread_mutex_unlock(&sDoneMutex);
		return;
	}
	sDoneQ[sDoneTail] = *r;
	sDoneTail = (sDoneTail + 1U) % MM_QUEUE_DEPTH;
	sDoneCount++;
	pthread_mutex_unlock(&sDoneMutex);
}

static void mmPushDoneError(long http_status, const char *msg)
{
	MmMatchResult r;

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

static void mmCredPath(char *out, size_t cap)
{
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
}

static sb32 mmEnsureCredDir(const char *fullpath)
{
	char dir[512];
	const char *slash;
	size_t i;

	slash = strrchr(fullpath, '/');
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
		if (dir[i] == '/')
		{
			dir[i] = '\0';
			(void)mkdir(dir, 0700);
			dir[i] = '/';
		}
	}
	if (mkdir(dir, 0700) != 0)
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

	memset(&chunk, 0, sizeof(chunk));
	hdrs = NULL;
	http_code = 0;

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

	(void)curl_easy_perform(c);

	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
	curl_slist_free_all(hdrs);
	curl_easy_cleanup(c);

#ifdef PORT
	if (verb != FALSE)
	{
		port_log("SSB64 Matchmaking: %s %s -> HTTP %ld\n", method, url, http_code);
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

static void mmEnqueueLocked(const MmJob *jp)
{
	if (sJobCount >= MM_QUEUE_DEPTH)
	{
#ifdef PORT
		port_log("SSB64 Matchmaking: job queue overflow (drop)\n");
#endif
		return;
	}
	sJobQ[sJobTail] = *jp;
	sJobTail = (sJobTail + 1U) % MM_QUEUE_DEPTH;
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
		sJobHead = (sJobHead + 1U) % MM_QUEUE_DEPTH;
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

	if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0)
	{
#ifdef PORT
		port_log("SSB64 Matchmaking: curl_global_init failed\n");
#endif
		return;
	}

	sWorkerRunning = TRUE;
	sWorkerSpawned = TRUE;
	if (pthread_create(&sWorkerThread, NULL, mmWorkerPthreadThunk, NULL) != 0)
	{
		sWorkerRunning = FALSE;
		sWorkerSpawned = FALSE;
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
	sWorkerRunning = FALSE;
	pthread_cond_broadcast(&sCond);
	pthread_mutex_unlock(&sMutex);
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
	sDoneHead = (sDoneHead + 1U) % MM_QUEUE_DEPTH;
	sDoneCount--;
	ok = TRUE;
	pthread_mutex_unlock(&sDoneMutex);
	return ok;
}

#endif /* PORT && SSB64_NETMENU && !_WIN32 */
