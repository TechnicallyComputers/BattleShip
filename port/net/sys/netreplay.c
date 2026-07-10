#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/netreplay.h>

#include <ft/fighter.h>
#include <if/ifcommon.h>
#include <sc/scdef.h>
#include <sc/scmanager.h>
#include <sys/netinput.h>
#include <sys/netpeer.h>
#include <sys/utils.h>

#ifdef PORT
extern void port_log(const char *fmt, ...);
extern char *getenv(const char *name);
extern int atoi(const char *s);
#if defined(SSB64_NETMENU)
#include <ssb64_paths_capi.h>
#include <errno.h>
#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#else
#include <direct.h>
#include <windows.h>
#endif
#endif
#endif

/* Default safety ceiling matches SYNETINPUT_REPLAY_MAX_FRAMES (~12 min @ 60 Hz).
 * Automatch auto-save writes on match end (syNetReplayFinishVSSession), not mid-match. */
#define SYNETREPLAY_DEFAULT_RECORD_FRAMES SYNETINPUT_REPLAY_MAX_FRAMES

typedef struct SYNetReplayFileHeader
{
	u32 magic;
	u32 version;
	u32 metadata_size;
	u32 frame_size;
	u32 frame_count;
	u32 player_count;
	u32 input_checksum;

} SYNetReplayFileHeader;

const char *sSYNetReplayRecordPath;
const char *sSYNetReplayPlayPath;
#if defined(PORT) && defined(SSB64_NETMENU)
static char sSYNetReplayUserRecordPath[SYNETREPLAY_USER_PATH_MAX];
static char sSYNetReplayUserPlaybackPath[SYNETREPLAY_USER_PATH_MAX];
static sb32 sSYNetReplayIsUserPlaybackPending;
static sb32 sSYNetReplayIsUserPlaybackHalted;
#endif
u32 sSYNetReplayRecordFrameLimit = SYNETREPLAY_DEFAULT_RECORD_FRAMES;
u32 sSYNetReplayLoadedFrameCount;
u32 sSYNetReplayLoadedInputChecksum;
sb32 sSYNetReplayIsRecording;
sb32 sSYNetReplayIsRecordWritten;
sb32 sSYNetReplayIsPlaybackLoaded;
sb32 sSYNetReplayIsPlaybackActive;
sb32 sSYNetReplayIsPlaybackVerified;
SYNetInputReplayMetadata sSYNetReplayLoadedMetadata;
SYNetInputFrame sSYNetReplayLoadedFrames[MAXCONTROLLERS][SYNETINPUT_REPLAY_MAX_FRAMES];

void syNetReplayClearLoadedFrames(void)
{
	s32 player;
	s32 tick;

	sSYNetReplayLoadedFrameCount = 0;
	sSYNetReplayLoadedInputChecksum = 0;

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		for (tick = 0; tick < SYNETINPUT_REPLAY_MAX_FRAMES; tick++)
		{
			memset(&sSYNetReplayLoadedFrames[player][tick], 0, sizeof(SYNetInputFrame));
		}
	}
}

void syNetReplayCaptureBattleMetadata(SCBattleState *battle_state, SYNetInputReplayMetadata *metadata)
{
	s32 player;

	memset(metadata, 0, sizeof(SYNetInputReplayMetadata));

	metadata->magic = SYNETINPUT_REPLAY_MAGIC;
	metadata->version = SYNETINPUT_REPLAY_VERSION;
	metadata->scene_kind = nSCKindVSBattle;
	metadata->rng_seed = syUtilsRandSeed();

	if (battle_state == NULL)
	{
		return;
	}
	metadata->player_count = battle_state->pl_count + battle_state->cp_count;
	metadata->stage_kind = battle_state->gkind;
	metadata->stocks = battle_state->stocks;
	metadata->time_limit = battle_state->time_limit;
	metadata->item_switch = battle_state->item_appearance_rate;
	metadata->item_toggles = battle_state->item_toggles;
	metadata->game_type = battle_state->game_type;
	metadata->game_rules = battle_state->game_rules;
	metadata->is_team_battle = battle_state->is_team_battle;
	metadata->handicap = battle_state->handicap;
	metadata->is_team_attack = battle_state->is_team_attack;
	metadata->is_stage_select = battle_state->is_stage_select;
	metadata->damage_ratio = battle_state->damage_ratio;
	metadata->item_appearance_rate = battle_state->item_appearance_rate;
	metadata->is_not_teamshadows = battle_state->is_not_teamshadows;

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		metadata->player_kinds[player] = battle_state->players[player].pkind;
		metadata->fighter_kinds[player] = battle_state->players[player].fkind;
		metadata->costumes[player] = battle_state->players[player].costume;
		metadata->teams[player] = battle_state->players[player].team;
		metadata->handicaps[player] = battle_state->players[player].handicap;
		metadata->levels[player] = battle_state->players[player].level;
		metadata->shades[player] = battle_state->players[player].shade;
	}
	metadata->netplay_sim_slot_host_hw = 0U;
	metadata->netplay_sim_slot_client_hw = 1U;
}

void syNetReplayApplyBattleMetadata(const SYNetInputReplayMetadata *metadata)
{
	SCBattleState *battle_state = &gSCManagerTransferBattleState;
	s32 player;

	*battle_state = dSCManagerDefaultBattleState;
	battle_state->game_type = metadata->game_type;
	battle_state->gkind = metadata->stage_kind;
	battle_state->is_team_battle = metadata->is_team_battle;
	battle_state->game_rules = metadata->game_rules;
	battle_state->time_limit = metadata->time_limit;
	battle_state->stocks = metadata->stocks;
	battle_state->handicap = metadata->handicap;
	battle_state->is_team_attack = metadata->is_team_attack;
	battle_state->is_stage_select = FALSE;
	battle_state->damage_ratio = metadata->damage_ratio;
	battle_state->item_toggles = metadata->item_toggles;
	battle_state->item_appearance_rate = metadata->item_appearance_rate;
	battle_state->is_not_teamshadows = metadata->is_not_teamshadows;
	battle_state->pl_count = 0;
	battle_state->cp_count = 0;

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		battle_state->players[player].player = (metadata->is_team_battle != FALSE) ? metadata->teams[player] : player;
		battle_state->players[player].team = metadata->teams[player];
		battle_state->players[player].pkind = metadata->player_kinds[player];
		battle_state->players[player].fkind = metadata->fighter_kinds[player];
		battle_state->players[player].costume = metadata->costumes[player];
		battle_state->players[player].shade = metadata->shades[player];
		battle_state->players[player].handicap = metadata->handicaps[player];
		battle_state->players[player].level = metadata->levels[player];
		battle_state->players[player].tag = (metadata->player_kinds[player] == nFTPlayerKindMan) ? player : GMCOMMON_PLAYERS_MAX;
		battle_state->players[player].is_single_stockicon = (metadata->game_rules & SCBATTLE_GAMERULE_TIME) ? TRUE : FALSE;

		if (metadata->player_kinds[player] == nFTPlayerKindMan)
		{
			battle_state->players[player].color =
			(metadata->is_team_battle == FALSE) ? player : dIFCommonPlayerTeamColorIDs[metadata->teams[player]];
			battle_state->pl_count++;
		}
		else if (metadata->player_kinds[player] == nFTPlayerKindCom)
		{
			battle_state->players[player].color =
			(metadata->is_team_battle == FALSE) ? GMCOMMON_PLAYERS_MAX : dIFCommonPlayerTeamColorIDs[metadata->teams[player]];
			battle_state->cp_count++;
		}
	}
	gSCManagerSceneData.gkind = metadata->stage_kind;
}

static void syNetReplayStartRecordingInternal(const char *path, const SYNetInputReplayMetadata *metadata)
{
	if ((path == NULL) || (metadata == NULL))
	{
		return;
	}
	syNetInputClearReplayFrames();
	syNetInputSetReplayMetadata(metadata);
	syNetInputSetRecordingEnabled(TRUE);
	sSYNetReplayRecordPath = path;
	sSYNetReplayIsRecording = TRUE;
	sSYNetReplayIsRecordWritten = FALSE;

#ifdef PORT
	port_log("SSB64 Replay: recording start path=%s limit=%u stage=%u seed=%u players=%u\n", path,
	         sSYNetReplayRecordFrameLimit, metadata->stage_kind, metadata->rng_seed, metadata->player_count);
#endif
}

void syNetReplayInitDebugEnv(void)
{
#ifdef PORT
	const char *frame_limit_env;

	sSYNetReplayRecordPath = getenv("SSB64_REPLAY_RECORD");
	sSYNetReplayPlayPath = getenv("SSB64_REPLAY_PLAY");
	frame_limit_env = getenv("SSB64_REPLAY_RECORD_FRAMES");

	if (frame_limit_env != NULL)
	{
		s32 frame_limit = atoi(frame_limit_env);

		if ((frame_limit > 0) && (frame_limit <= (s32)SYNETINPUT_REPLAY_MAX_FRAMES))
		{
			sSYNetReplayRecordFrameLimit = (u32)frame_limit;
		}
	}
	if (sSYNetReplayPlayPath != NULL)
	{
		if (syNetReplayLoadDebugFile(sSYNetReplayPlayPath) != FALSE)
		{
			syNetReplayApplyBattleMetadata(&sSYNetReplayLoadedMetadata);
			syUtilsSetRandomSeed(sSYNetReplayLoadedMetadata.rng_seed);
#if defined(SSB64_NETMENU)
			syUtilsResetCosmeticRandomSeed(sSYNetReplayLoadedMetadata.rng_seed);
#endif
			gSCManagerSceneData.scene_prev = nSCKindVSMode;
			gSCManagerSceneData.scene_curr = nSCKindVSBattle;
		}
	}
#endif
}

void syNetReplayStartVSSession(SCBattleState *battle_state)
{
	SYNetInputReplayMetadata metadata;
	u32 tick;
	s32 player;

	if (sSYNetReplayIsPlaybackLoaded != FALSE)
	{
		syNetInputClearReplayFrames();
		syNetInputSetReplayMetadata(&sSYNetReplayLoadedMetadata);
		syUtilsSetRandomSeed(sSYNetReplayLoadedMetadata.rng_seed);
#if defined(SSB64_NETMENU)
		syUtilsResetCosmeticRandomSeed(sSYNetReplayLoadedMetadata.rng_seed);
#endif

		for (tick = 0; tick < sSYNetReplayLoadedFrameCount; tick++)
		{
			for (player = 0; player < MAXCONTROLLERS; player++)
			{
				syNetInputSetReplayFrame(player, tick, &sSYNetReplayLoadedFrames[player][tick]);
			}
		}
		for (player = 0; player < MAXCONTROLLERS; player++)
		{
			syNetInputSetSlotSource(player, nSYNetInputSourceSaved);
		}
		sSYNetReplayIsPlaybackActive = TRUE;
		sSYNetReplayIsPlaybackVerified = FALSE;
#if defined(SSB64_NETMENU)
		sSYNetReplayIsUserPlaybackPending = FALSE;
#endif

#ifdef PORT
		port_log("SSB64 Replay: playback start path=%s frames=%u checksum=0x%08X stage=%u seed=%u\n",
		         sSYNetReplayPlayPath, sSYNetReplayLoadedFrameCount, sSYNetReplayLoadedInputChecksum,
		         sSYNetReplayLoadedMetadata.stage_kind, sSYNetReplayLoadedMetadata.rng_seed);
#endif
		return;
	}
	if (sSYNetReplayRecordPath != NULL)
	{
		if (sSYNetReplayIsRecording == FALSE)
		{
			syNetReplayCaptureBattleMetadata(battle_state, &metadata);
			syNetReplayStartRecordingInternal(sSYNetReplayRecordPath, &metadata);
		}
	}
}

void syNetReplayUpdate(void)
{
	/* Recording is finalized on match end via syNetReplayFinishVSSession()
	 * (scVSBattleStartScene). Do not write mid-match when the safety ceiling
	 * is hit — new frames simply stop accepting past MAX; the file is still
	 * flushed when the battle ends. Optional SSB64_REPLAY_RECORD_FRAMES only
	 * shrinks the in-memory ceiling for debug captures. */
	if ((sSYNetReplayIsRecording != FALSE) && (sSYNetReplayIsRecordWritten == FALSE) &&
	    (syNetInputGetRecordedFrameCount() >= sSYNetReplayRecordFrameLimit) &&
	    (sSYNetReplayRecordFrameLimit < SYNETINPUT_REPLAY_MAX_FRAMES))
	{
		/* Debug-only early stop when env requested a short capture. */
		syNetReplayFinishVSSession();
	}
	if ((sSYNetReplayIsPlaybackActive != FALSE) && (sSYNetReplayIsPlaybackVerified == FALSE) &&
		(syNetInputGetTick() >= sSYNetReplayLoadedFrameCount))
	{
		u32 checksum = syNetInputGetHistoryInputChecksum(sSYNetReplayLoadedFrameCount);

#ifdef PORT
		port_log("SSB64 Replay: playback verify frames=%u expected=0x%08X actual=0x%08X result=%s\n",
		         sSYNetReplayLoadedFrameCount, sSYNetReplayLoadedInputChecksum, checksum,
		         (checksum == sSYNetReplayLoadedInputChecksum) ? "PASS" : "FAIL");
#endif
		sSYNetReplayIsPlaybackVerified = TRUE;
		sSYNetReplayIsPlaybackActive = FALSE;
	}
}

void syNetReplayFinishVSSession(void)
{
	if ((sSYNetReplayIsRecording != FALSE) && (sSYNetReplayIsRecordWritten == FALSE))
	{
		syNetReplayWriteDebugFile(sSYNetReplayRecordPath);
		syNetInputSetRecordingEnabled(FALSE);
		sSYNetReplayIsRecording = FALSE;
		sSYNetReplayIsRecordWritten = TRUE;
	}
}

sb32 syNetReplayWriteDebugFile(const char *path)
{
	SYNetReplayFileHeader header;
	SYNetInputReplayMetadata metadata;
	SYNetInputFrame frame;
	FILE *fp;
	u32 tick;
	s32 player;

	if ((path == NULL) || (syNetInputGetReplayMetadata(&metadata) == FALSE))
	{
		return FALSE;
	}
	fp = fopen(path, "wb");

	if (fp == NULL)
	{
#ifdef PORT
		port_log("SSB64 Replay: failed to open record path=%s\n", path);
#endif
		return FALSE;
	}
	header.magic = SYNETINPUT_REPLAY_MAGIC;
	header.version = SYNETINPUT_REPLAY_VERSION;
	header.metadata_size = sizeof(SYNetInputReplayMetadata);
	header.frame_size = sizeof(SYNetInputFrame);
	header.frame_count = syNetInputGetRecordedFrameCount();
	header.player_count = MAXCONTROLLERS;
	header.input_checksum = syNetInputGetReplayInputChecksum();

	fwrite(&header, sizeof(header), 1, fp);
	fwrite(&metadata, sizeof(metadata), 1, fp);

	for (tick = 0; tick < header.frame_count; tick++)
	{
		for (player = 0; player < MAXCONTROLLERS; player++)
		{
			if (syNetInputGetReplayFrame(player, tick, &frame) == FALSE)
			{
				memset(&frame, 0, sizeof(frame));
				frame.tick = tick;
			}
			fwrite(&frame, sizeof(frame), 1, fp);
		}
	}
	fclose(fp);

#ifdef PORT
	port_log("SSB64 Replay: wrote path=%s frames=%u checksum=0x%08X\n",
	         path, header.frame_count, header.input_checksum);
#endif
	return TRUE;
}

sb32 syNetReplayLoadDebugFile(const char *path)
{
	SYNetReplayFileHeader header;
	FILE *fp;
	u32 tick;
	s32 player;

	if (path == NULL)
	{
		return FALSE;
	}
	fp = fopen(path, "rb");

	if (fp == NULL)
	{
#ifdef PORT
		port_log("SSB64 Replay: failed to open playback path=%s\n", path);
#endif
		return FALSE;
	}
	if (fread(&header, sizeof(header), 1, fp) != 1)
	{
		fclose(fp);
		return FALSE;
	}
	if ((header.magic != SYNETINPUT_REPLAY_MAGIC) ||
		(header.version != SYNETINPUT_REPLAY_VERSION) ||
		(header.metadata_size != sizeof(SYNetInputReplayMetadata)) ||
		(header.frame_size != sizeof(SYNetInputFrame)) ||
		(header.frame_count > SYNETINPUT_REPLAY_MAX_FRAMES) ||
		(header.player_count != MAXCONTROLLERS))
	{
		fclose(fp);
		return FALSE;
	}
	if (fread(&sSYNetReplayLoadedMetadata, sizeof(sSYNetReplayLoadedMetadata), 1, fp) != 1)
	{
		fclose(fp);
		return FALSE;
	}
	syNetReplayClearLoadedFrames();
	sSYNetReplayLoadedFrameCount = header.frame_count;
	sSYNetReplayLoadedInputChecksum = header.input_checksum;

	for (tick = 0; tick < header.frame_count; tick++)
	{
		for (player = 0; player < MAXCONTROLLERS; player++)
		{
			if (fread(&sSYNetReplayLoadedFrames[player][tick], sizeof(SYNetInputFrame), 1, fp) != 1)
			{
				fclose(fp);
				return FALSE;
			}
		}
	}
	fclose(fp);
	sSYNetReplayIsPlaybackLoaded = TRUE;

#ifdef PORT
	port_log("SSB64 Replay: loaded path=%s frames=%u checksum=0x%08X stage=%u seed=%u\n",
	         path, sSYNetReplayLoadedFrameCount, sSYNetReplayLoadedInputChecksum,
	         sSYNetReplayLoadedMetadata.stage_kind, sSYNetReplayLoadedMetadata.rng_seed);
#endif
	return TRUE;
}

#if defined(PORT) && defined(SSB64_NETMENU)

static void syNetReplayStrCopyN(char *out, size_t cap, const char *src)
{
	size_t i;

	if ((out == NULL) || (cap == 0U))
	{
		return;
	}
	if (src == NULL)
	{
		out[0] = '\0';
		return;
	}
	for (i = 0U; (i < (cap - 1U)) && (src[i] != '\0'); i++)
	{
		out[i] = src[i];
	}
	out[i] = '\0';
}

static void syNetReplayJoinPath(char *out, size_t cap, const char *dir, const char *filename)
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

static sb32 syNetReplayEnsureDirRecursive(const char *fullpath)
{
	char dir[SYNETREPLAY_USER_PATH_MAX];
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
#else
			if (mkdir(dir, 0755) != 0)
#endif
			{
				if (errno != EEXIST)
				{
					return FALSE;
				}
			}
			dir[i] = '/';
		}
	}
#ifdef _WIN32
	return (_mkdir(dir) == 0) || (errno == EEXIST);
#else
	return (mkdir(dir, 0755) == 0) || (errno == EEXIST);
#endif
}

void syNetReplayResolveUserDir(char *out, size_t cap)
{
	char base[SYNETREPLAY_USER_PATH_MAX];

	if ((out == NULL) || (cap == 0U))
	{
		return;
	}
	out[0] = '\0';
	if ((ssb64_UserDataDirUtf8(base, sizeof(base)) != 0) && (base[0] != '\0'))
	{
		syNetReplayJoinPath(out, cap, base, SYNETREPLAY_USER_DIR_NAME "/");
		return;
	}
	snprintf(out, cap, "./%s/", SYNETREPLAY_USER_DIR_NAME);
}

sb32 syNetReplayEnsureUserDir(void)
{
	char dir[SYNETREPLAY_USER_PATH_MAX];

	syNetReplayResolveUserDir(dir, sizeof(dir));
	return syNetReplayEnsureDirRecursive(dir);
}

sb32 syNetReplayMakeTimestampFilename(char *out, size_t cap)
{
	time_t now;
	struct tm local_tm;
	struct tm *tm_out;

	if ((out == NULL) || (cap == 0U))
	{
		return FALSE;
	}
	now = time(NULL);
#ifdef _WIN32
	tm_out = (localtime_s(&local_tm, &now) == 0) ? &local_tm : NULL;
#else
	tm_out = localtime_r(&now, &local_tm);
#endif
	if (tm_out == NULL)
	{
		return FALSE;
	}
	if (strftime(out, cap, "%Y%m%d_%H%M%S" SYNETREPLAY_USER_FILE_EXT, tm_out) == 0U)
	{
		return FALSE;
	}
	return TRUE;
}

static sb32 syNetReplayIsReplayBasename(const char *name)
{
	size_t len;
	size_t ext_len;

	if (name == NULL)
	{
		return FALSE;
	}
	len = strlen(name);
	ext_len = strlen(SYNETREPLAY_USER_FILE_EXT);
	if ((len <= ext_len) || (len >= SYNETREPLAY_USER_FILENAME_MAX))
	{
		return FALSE;
	}
	return (strcmp(name + len - ext_len, SYNETREPLAY_USER_FILE_EXT) == 0) ? TRUE : FALSE;
}

static void syNetReplaySortNamesNewestFirst(char names[][SYNETREPLAY_USER_FILENAME_MAX], s32 count)
{
	s32 i;
	s32 j;
	char temp[SYNETREPLAY_USER_FILENAME_MAX];

	for (i = 0; i < (count - 1); i++)
	{
		for (j = i + 1; j < count; j++)
		{
			if (strcmp(names[i], names[j]) < 0)
			{
				memcpy(temp, names[i], sizeof(temp));
				memcpy(names[i], names[j], sizeof(temp));
				memcpy(names[j], temp, sizeof(temp));
			}
		}
	}
}

sb32 syNetReplayEnumerateUserFiles(char out_names[][SYNETREPLAY_USER_FILENAME_MAX], s32 max_count, s32 *out_count)
{
	char dir[SYNETREPLAY_USER_PATH_MAX];
	s32 count;

	if ((out_names == NULL) || (max_count <= 0) || (out_count == NULL))
	{
		return FALSE;
	}
	*out_count = 0;
	count = 0;
	syNetReplayResolveUserDir(dir, sizeof(dir));

#ifndef _WIN32
	{
		DIR *dp;
		struct dirent *entry;

		dp = opendir(dir);
		if (dp == NULL)
		{
			return TRUE;
		}
		while ((entry = readdir(dp)) != NULL)
		{
			if (syNetReplayIsReplayBasename(entry->d_name) == FALSE)
			{
				continue;
			}
			if (count >= max_count)
			{
				break;
			}
			syNetReplayStrCopyN(out_names[count], SYNETREPLAY_USER_FILENAME_MAX, entry->d_name);
			count++;
		}
		closedir(dp);
	}
#else
	{
		WIN32_FIND_DATAA find_data;
		HANDLE handle;
		char pattern[SYNETREPLAY_USER_PATH_MAX];

		snprintf(pattern, sizeof(pattern), "%s*" SYNETREPLAY_USER_FILE_EXT, dir);
		handle = FindFirstFileA(pattern, &find_data);
		if (handle != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (syNetReplayIsReplayBasename(find_data.cFileName) == FALSE)
				{
					continue;
				}
				if (count >= max_count)
				{
					break;
				}
				syNetReplayStrCopyN(out_names[count], SYNETREPLAY_USER_FILENAME_MAX, find_data.cFileName);
				count++;
			} while (FindNextFileA(handle, &find_data) != 0);
			FindClose(handle);
		}
	}
#endif

	syNetReplaySortNamesNewestFirst(out_names, count);
	*out_count = count;
	return TRUE;
}

sb32 syNetReplayResolveUserFilePath(const char *basename, char *out, size_t cap)
{
	char dir[SYNETREPLAY_USER_PATH_MAX];

	if ((basename == NULL) || (out == NULL) || (cap == 0U) || (syNetReplayIsReplayBasename(basename) == FALSE))
	{
		return FALSE;
	}
	syNetReplayResolveUserDir(dir, sizeof(dir));
	syNetReplayJoinPath(out, cap, dir, basename);
	return TRUE;
}

sb32 syNetReplayReadMetadataOnly(const char *path, SYNetInputReplayMetadata *out_metadata)
{
	SYNetReplayFileHeader header;
	FILE *fp;

	if ((path == NULL) || (out_metadata == NULL))
	{
		return FALSE;
	}
	fp = fopen(path, "rb");
	if (fp == NULL)
	{
		return FALSE;
	}
	if (fread(&header, sizeof(header), 1, fp) != 1)
	{
		fclose(fp);
		return FALSE;
	}
	if ((header.magic != SYNETINPUT_REPLAY_MAGIC) || (header.version != SYNETINPUT_REPLAY_VERSION) ||
	    (header.metadata_size != sizeof(SYNetInputReplayMetadata)))
	{
		fclose(fp);
		return FALSE;
	}
	if (fread(out_metadata, sizeof(*out_metadata), 1, fp) != 1)
	{
		fclose(fp);
		return FALSE;
	}
	fclose(fp);
	return TRUE;
}

sb32 syNetReplayBeginUserAutomatchRecording(const char *path)
{
	SYNetInputReplayMetadata metadata;

	if ((path == NULL) || (path[0] == '\0'))
	{
		return FALSE;
	}
	if (syNetPeerGetCommittedBootstrapMetadata(&metadata) == FALSE)
	{
		return FALSE;
	}
	syNetReplayStrCopyN(sSYNetReplayUserRecordPath, sizeof(sSYNetReplayUserRecordPath), path);
	syNetReplayStartRecordingInternal(sSYNetReplayUserRecordPath, &metadata);
	return TRUE;
}

sb32 syNetReplayBeginUserPlayback(const char *path)
{
	if ((path == NULL) || (path[0] == '\0'))
	{
		return FALSE;
	}
	syNetPeerEndVSSessionLocally();
	sSYNetReplayIsPlaybackLoaded = FALSE;
	sSYNetReplayIsUserPlaybackPending = FALSE;
	if (syNetReplayLoadDebugFile(path) == FALSE)
	{
		return FALSE;
	}
	syNetReplayStrCopyN(sSYNetReplayUserPlaybackPath, sizeof(sSYNetReplayUserPlaybackPath), path);
	syNetReplayApplyBattleMetadata(&sSYNetReplayLoadedMetadata);
	syUtilsSetRandomSeed(sSYNetReplayLoadedMetadata.rng_seed);
#if defined(SSB64_NETMENU)
	syUtilsResetCosmeticRandomSeed(sSYNetReplayLoadedMetadata.rng_seed);
#endif
	sSYNetReplayPlayPath = sSYNetReplayUserPlaybackPath;
	sSYNetReplayIsUserPlaybackPending = TRUE;
	gSCManagerSceneData.is_vs_replay_playback = (ub8)TRUE;
	gSCManagerSceneData.is_vs_automatch_battle = (ub8)FALSE;
	gSCManagerSceneData.vs_net_automatch_post_battle_scene = (u8)nSCKindVSReplays;
	return TRUE;
}

sb32 syNetReplayIsUserPlaybackPending(void)
{
	return sSYNetReplayIsUserPlaybackPending;
}

sb32 syNetReplayIsPlaybackLoaded(void)
{
	return sSYNetReplayIsPlaybackLoaded;
}

void syNetReplaySetUserPlaybackHalted(sb32 halted)
{
	sSYNetReplayIsUserPlaybackHalted = halted;
}

sb32 syNetReplayIsUserPlaybackHalted(void)
{
	return sSYNetReplayIsUserPlaybackHalted;
}

void syNetReplayAbortUserPlayback(void)
{
	syNetReplayFinishVSSession();
	syNetReplayClearLoadedFrames();
	sSYNetReplayIsPlaybackLoaded = FALSE;
	sSYNetReplayIsPlaybackActive = FALSE;
	sSYNetReplayIsPlaybackVerified = FALSE;
	sSYNetReplayIsUserPlaybackPending = FALSE;
	sSYNetReplayIsUserPlaybackHalted = FALSE;
	sSYNetReplayPlayPath = NULL;
	sSYNetReplayUserPlaybackPath[0] = '\0';
	syNetInputClearReplayFrames();
	gSCManagerSceneData.is_vs_replay_playback = (ub8)FALSE;
	gSCManagerSceneData.vs_net_automatch_post_battle_scene = (u8)0;
}

sb32 syNetReplayDeleteUserFile(const char *basename)
{
	char path[SYNETREPLAY_USER_PATH_MAX];

	if (syNetReplayResolveUserFilePath(basename, path, sizeof(path)) == FALSE)
	{
		return FALSE;
	}
#if defined(_WIN32)
	if (DeleteFileA(path) == FALSE)
	{
		return FALSE;
	}
#else
	if (remove(path) != 0)
	{
		return FALSE;
	}
#endif
#ifdef PORT
	port_log("SSB64 Replay: deleted path=%s\n", path);
#endif
	return TRUE;
}

sb32 syNetReplayDeleteAllUserFiles(void)
{
	char names[SYNETREPLAY_USER_MAX_FILES][SYNETREPLAY_USER_FILENAME_MAX];
	s32 count;
	s32 i;
	sb32 any_failed;

	if (syNetReplayEnumerateUserFiles(names, SYNETREPLAY_USER_MAX_FILES, &count) == FALSE)
	{
		return FALSE;
	}
	any_failed = FALSE;
	for (i = 0; i < count; i++)
	{
		if (syNetReplayDeleteUserFile(names[i]) == FALSE)
		{
			any_failed = TRUE;
		}
	}
	return (any_failed == FALSE) ? TRUE : FALSE;
}

#endif /* PORT && SSB64_NETMENU */
