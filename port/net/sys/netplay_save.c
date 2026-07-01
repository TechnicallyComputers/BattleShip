#include "netplay_save.h"

#if defined(PORT) && defined(SSB64_NETMENU)

#include <sc/scene.h>
#include <sc/scmanager.h>
#include <gr/ground.h>
#include <lb/lbbackup.h>

#include <ssb64_paths_capi.h>

#include "port_log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#define SSB64_NETPLAY_SAVE_FILENAME "ssb64_netplaysave.bin"
#define SSB64_NETPLAY_SAVE_MAGIC 0x53534E50u /* "SSNP" */
#define SSB64_NETPLAY_SAVE_VERSION 2u
#define SSB64_NETPLAY_SAVE_VERSION_V1 1u

typedef struct SSB64NetplaySaveV2
{
	u32 magic;
	u32 version;
	u32 checksum;
	u16 stage_ban_mask;
	u8 replay_save_enabled;
	u8 reserved;
} SSB64NetplaySaveV2;

typedef struct SSB64NetplaySaveV1
{
	u32 magic;
	u32 version;
	u32 checksum;
	u16 stage_ban_mask;
	u16 reserved;
} SSB64NetplaySaveV1;

static u32 syNetplaySaveChecksumPayload(const u8 *p, size_t n)
{
	u32 sum;
	size_t i;

	sum = 0U;
	for (i = 0; i < n; i++)
	{
		sum = (sum << 1) | (sum >> 31);
		sum ^= (u32)p[i];
	}
	return sum;
}

static u32 syNetplaySaveChecksumV1(const SSB64NetplaySaveV1 *blob)
{
	const u8 *p = (const u8 *)&blob->stage_ban_mask;

	return syNetplaySaveChecksumPayload(p, sizeof(blob->stage_ban_mask) + sizeof(blob->reserved));
}

static u32 syNetplaySaveChecksumV2(const SSB64NetplaySaveV2 *blob)
{
	const u8 *p = (const u8 *)&blob->stage_ban_mask;

	return syNetplaySaveChecksumPayload(p, sizeof(blob->stage_ban_mask) + sizeof(blob->replay_save_enabled) +
	                                           sizeof(blob->reserved));
}

static void syNetplaySaveJoinPath(char *out, size_t cap, const char *dir, const char *filename)
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

static void syNetplaySaveResolvePath(char *out, size_t cap)
{
	const char *override;

	if ((out == NULL) || (cap == 0U))
	{
		return;
	}
	out[0] = '\0';

	override = getenv("SSB64_NETPLAY_SAVE_PATH");
	if ((override != NULL) && (override[0] != '\0'))
	{
		snprintf(out, cap, "%s", override);
		return;
	}

	{
		char base[512];

		if ((ssb64_UserDataDirUtf8(base, sizeof(base)) != 0) && (base[0] != '\0'))
		{
			syNetplaySaveJoinPath(out, cap, base, SSB64_NETPLAY_SAVE_FILENAME);
			return;
		}
	}
	snprintf(out, cap, "./%s", SSB64_NETPLAY_SAVE_FILENAME);
}

const char *syNetplaySaveGetPath(void)
{
	static char sPath[512];

	syNetplaySaveResolvePath(sPath, sizeof(sPath));
	return sPath;
}

static sb32 syNetplaySaveEnsureParentDir(const char *fullpath)
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

static void syNetplaySaveApplyLoadedBlob(u16 stage_ban_mask, u8 replay_save_enabled)
{
	u16 mask;

	mask = mnVSNetLevelPrefsMapsSanitizeUserBanMask(stage_ban_mask);
	gSCManagerSceneData.vs_net_stage_ban_mask = mask;
	gSCManagerSceneData.vs_net_replay_save_enabled = (replay_save_enabled != 0U) ? (ub8)TRUE : (ub8)FALSE;
}

static sb32 syNetplaySaveReadBlob(SSB64NetplaySaveV2 *out_blob)
{
	FILE *fp;
	char path[512];
	SSB64NetplaySaveV1 blob_v1;
	SSB64NetplaySaveV2 blob;

	syNetplaySaveResolvePath(path, sizeof(path));
	fp = fopen(path, "rb");
	if (fp == NULL)
	{
		return FALSE;
	}
	if (fread(&blob_v1, 1, sizeof(blob_v1), fp) != sizeof(blob_v1))
	{
		fclose(fp);
		return FALSE;
	}
	fclose(fp);

	if (blob_v1.magic != SSB64_NETPLAY_SAVE_MAGIC)
	{
		return FALSE;
	}
	if (blob_v1.version == SSB64_NETPLAY_SAVE_VERSION_V1)
	{
		if (blob_v1.checksum != syNetplaySaveChecksumV1(&blob_v1))
		{
			port_log("SSB64 NetplaySave: v1 checksum mismatch (%s) — ignoring\n", path);
			return FALSE;
		}
		out_blob->magic = blob_v1.magic;
		out_blob->version = SSB64_NETPLAY_SAVE_VERSION;
		out_blob->stage_ban_mask = blob_v1.stage_ban_mask;
		out_blob->replay_save_enabled = 0U;
		out_blob->reserved = 0U;
		out_blob->checksum = syNetplaySaveChecksumV2(out_blob);
		return TRUE;
	}
	if (blob_v1.version != SSB64_NETPLAY_SAVE_VERSION)
	{
		return FALSE;
	}
	memcpy(&blob, &blob_v1, sizeof(blob));
	if (blob.checksum != syNetplaySaveChecksumV2(&blob))
	{
		port_log("SSB64 NetplaySave: checksum mismatch (%s) — ignoring\n", path);
		return FALSE;
	}
	*out_blob = blob;
	return TRUE;
}

static void syNetplaySaveWriteBlob(const SSB64NetplaySaveV2 *blob_in)
{
	FILE *fp;
	char path[512];
	SSB64NetplaySaveV2 blob;

	blob = *blob_in;
	blob.magic = SSB64_NETPLAY_SAVE_MAGIC;
	blob.version = SSB64_NETPLAY_SAVE_VERSION;
	blob.reserved = 0U;
	blob.checksum = syNetplaySaveChecksumV2(&blob);

	syNetplaySaveResolvePath(path, sizeof(path));
	if (syNetplaySaveEnsureParentDir(path) == FALSE)
	{
		port_log("SSB64 NetplaySave: could not create directory for %s\n", path);
		return;
	}

	fp = fopen(path, "wb");
	if (fp == NULL)
	{
		port_log("SSB64 NetplaySave: fopen(%s) failed errno=%d\n", path, errno);
		return;
	}
	if (fwrite(&blob, 1, sizeof(blob), fp) != sizeof(blob))
	{
		port_log("SSB64 NetplaySave: short write (%s)\n", path);
	}
	fclose(fp);
}

void syNetplaySaveLoad(void)
{
	SSB64NetplaySaveV2 blob;

	if (syNetplaySaveReadBlob(&blob) == FALSE)
	{
		gSCManagerSceneData.vs_net_replay_save_enabled = (ub8)FALSE;
		return;
	}
	syNetplaySaveApplyLoadedBlob(blob.stage_ban_mask, blob.replay_save_enabled);
}

void syNetplaySaveWriteStageBanMask(u16 user_mask)
{
	SSB64NetplaySaveV2 blob;
	u16 mask;

	mask = mnVSNetLevelPrefsMapsSanitizeUserBanMask(user_mask);
	gSCManagerSceneData.vs_net_stage_ban_mask = mask;

	if (syNetplaySaveReadBlob(&blob) == FALSE)
	{
		memset(&blob, 0, sizeof(blob));
	}
	blob.stage_ban_mask = mask;
	blob.replay_save_enabled = (gSCManagerSceneData.vs_net_replay_save_enabled != FALSE) ? 1U : 0U;
	syNetplaySaveWriteBlob(&blob);
}

sb32 syNetplaySaveGetReplaySaveEnabled(void)
{
	return (gSCManagerSceneData.vs_net_replay_save_enabled != FALSE) ? TRUE : FALSE;
}

void syNetplaySaveWriteReplaySaveEnabled(sb32 enabled)
{
	SSB64NetplaySaveV2 blob;

	if (syNetplaySaveReadBlob(&blob) == FALSE)
	{
		memset(&blob, 0, sizeof(blob));
	}
	gSCManagerSceneData.vs_net_replay_save_enabled = (enabled != FALSE) ? (ub8)TRUE : (ub8)FALSE;
	blob.replay_save_enabled = (enabled != FALSE) ? 1U : 0U;
	blob.stage_ban_mask = gSCManagerSceneData.vs_net_stage_ban_mask;
	syNetplaySaveWriteBlob(&blob);
}

#endif /* PORT && SSB64_NETMENU */
