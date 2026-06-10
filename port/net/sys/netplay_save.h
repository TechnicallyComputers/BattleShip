#ifndef NETPLAY_SAVE_H
#define NETPLAY_SAVE_H

#include <ssb_types.h>

#if defined(PORT) && defined(SSB64_NETMENU)

/** Max user-selected stage bans (SSS slots 0-8; Random slot clears all). */
#define MN_VSNET_LEVEL_PREFS_MAX_USER_BANS 3u
#define MN_VSNET_LEVEL_PREFS_STAGE_SLOT_MASK ((u16)((1u << 9) - 1u))

/** Clamp user ban mask (slot mask, Inishie lock, max bans). Uses gSCManagerBackupData.unlock_mask. */
u16 mnVSNetLevelPrefsMapsSanitizeUserBanMask(u16 mask);

/** Load persisted netplay prefs into gSCManagerSceneData (after lbBackupIsSramValid). */
void syNetplaySaveLoad(void);

/** Persist current automatch stage ban mask (user bits only). */
void syNetplaySaveWriteStageBanMask(u16 user_mask);

const char *syNetplaySaveGetPath(void);

#endif /* PORT && SSB64_NETMENU */

#endif /* NETPLAY_SAVE_H */
