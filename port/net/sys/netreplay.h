#ifndef _SYNETREPLAY_H_
#define _SYNETREPLAY_H_

#include <PR/ultratypes.h>
#include <sc/scene.h>
#include <sys/netinput.h>

#define SYNETREPLAY_USER_DIR_NAME "replays"
#define SYNETREPLAY_USER_FILE_EXT ".ssb64r"
#define SYNETREPLAY_USER_MAX_FILES 64
#define SYNETREPLAY_USER_FILENAME_MAX 48
#define SYNETREPLAY_USER_PATH_MAX 512

extern void syNetReplayInitDebugEnv(void);
extern void syNetReplayCaptureBattleMetadata(SCBattleState *battle_state, SYNetInputReplayMetadata *metadata);
extern void syNetReplayApplyBattleMetadata(const SYNetInputReplayMetadata *metadata);
extern void syNetReplayStartVSSession(SCBattleState *battle_state);
extern void syNetReplayUpdate(void);
extern void syNetReplayFinishVSSession(void);
extern sb32 syNetReplayWriteDebugFile(const char *path);
extern sb32 syNetReplayLoadDebugFile(const char *path);

#if defined(PORT) && defined(SSB64_NETMENU)
extern void syNetReplayResolveUserDir(char *out, size_t cap);
extern sb32 syNetReplayEnsureUserDir(void);
extern sb32 syNetReplayMakeTimestampFilename(char *out, size_t cap);
extern sb32 syNetReplayEnumerateUserFiles(char out_names[][SYNETREPLAY_USER_FILENAME_MAX], s32 max_count,
                                          s32 *out_count);
extern sb32 syNetReplayResolveUserFilePath(const char *basename, char *out, size_t cap);
extern sb32 syNetReplayReadMetadataOnly(const char *path, SYNetInputReplayMetadata *out_metadata);
extern sb32 syNetReplayBeginUserAutomatchRecording(const char *path);
extern sb32 syNetReplayBeginUserPlayback(const char *path);
extern sb32 syNetReplayIsUserPlaybackPending(void);
extern sb32 syNetReplayIsPlaybackLoaded(void);
extern void syNetReplaySetUserPlaybackHalted(sb32 halted);
extern sb32 syNetReplayIsUserPlaybackHalted(void);
extern void syNetReplayAbortUserPlayback(void);
extern sb32 syNetReplayDeleteUserFile(const char *basename);
#endif

#endif /* _SYNETREPLAY_H_ */
