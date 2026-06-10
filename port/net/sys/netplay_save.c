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
#define SSB64_NETPLAY_SAVE_VERSION 1u

typedef struct SSB64NetplaySaveV1
{
	u32 magic;
	u32 version;
	u32 checksum;
	u16 stage_ban_mask;
	u16 reserved;
} SSB64NetplaySaveV1;

static u32 syNetplaySaveChecksumV1(const SSB64NetplaySaveV1 *blob)
{
	const u8 *p = (const u8 *)&blob->stage_ban_mask;
	size_t n = sizeof(blob->stage_ban_mask) + sizeof(blob->reserved);
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

void syNetplaySaveLoad(void)
{
	FILE *fp;
	char path[512];
	SSB64NetplaySaveV1 blob;
	u16 mask;

	syNetplaySaveResolvePath(path, sizeof(path));
	fp = fopen(path, "rb");
	if (fp == NULL)
	{
		return;
	}
	if (fread(&blob, 1, sizeof(blob), fp) != sizeof(blob))
	{
		fclose(fp);
		return;
	}
	fclose(fp);

	if ((blob.magic != SSB64_NETPLAY_SAVE_MAGIC) || (blob.version != SSB64_NETPLAY_SAVE_VERSION))
	{
		return;
	}
	if (blob.checksum != syNetplaySaveChecksumV1(&blob))
	{
		port_log("SSB64 NetplaySave: checksum mismatch (%s) — ignoring\n", path);
		return;
	}

	mask = mnVSNetLevelPrefsMapsSanitizeUserBanMask(blob.stage_ban_mask);
	gSCManagerSceneData.vs_net_stage_ban_mask = mask;
}

void syNetplaySaveWriteStageBanMask(u16 user_mask)
{
	FILE *fp;
	char path[512];
	SSB64NetplaySaveV1 blob;
	u16 mask;

	mask = mnVSNetLevelPrefsMapsSanitizeUserBanMask(user_mask);
	gSCManagerSceneData.vs_net_stage_ban_mask = mask;

	syNetplaySaveResolvePath(path, sizeof(path));
	if (syNetplaySaveEnsureParentDir(path) == FALSE)
	{
		port_log("SSB64 NetplaySave: could not create directory for %s\n", path);
		return;
	}

	blob.magic = SSB64_NETPLAY_SAVE_MAGIC;
	blob.version = SSB64_NETPLAY_SAVE_VERSION;
	blob.stage_ban_mask = mask;
	blob.reserved = 0U;
	blob.checksum = syNetplaySaveChecksumV1(&blob);

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

#endif /* PORT && SSB64_NETMENU */
