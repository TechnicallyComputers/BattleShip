#include "common.h"

#if defined(PORT) && defined(SSB64_NETMENU)

#if !defined(_WIN32)

#include <reloc_data.h>
#include <sc/scene.h>
#include <sys/netpeer.h>
#include <sys/taskman.h>
#include <sys/video.h>
#include <sys/audio.h>
#include <sys/controller.h>
#include <ef/efmanager.h>
#include <ef/efparticle.h>

extern void mnVSNetAutomatchFuncLights(Gfx **dls);
extern void mnVSNetAutomatchLoadWallpaperRelocFiles(void);
extern void mnVSNetAutomatchMakeWallpaper(void);
extern void mnVSNetAutomatchMakeWallpaperCamera(void);

extern void mnVSNetAutomatchAMStartSearch(void);
extern void mnVSNetAutomatchMatchmakingTick(void);
extern void mnVSNetAutomatchAMFinalizeVsLoad(void);
extern sb32 mnVSNetAutomatchAMConsumeStagingHandshake(void);
extern sb32 mnVSNetAutomatchAMIsError(void);
extern void mnVSNetAutomatchAMStagingReturnToAutomatch(void);

static void mnVSNetMatchStagingFuncRun(GObj *gobj)
{
	(void)gobj;

	mnVSNetAutomatchMatchmakingTick();

	if (mnVSNetAutomatchAMIsError() != FALSE)
	{
		mnVSNetAutomatchAMStagingReturnToAutomatch();
		return;
	}

	if (mnVSNetAutomatchAMConsumeStagingHandshake() != FALSE)
	{
		syNetPeerStartVSSession();
		/* Always transition to VS immediately; barrier/bind/exec-sync run under VSBattle + taskman freeze. */
		mnVSNetAutomatchAMFinalizeVsLoad();
		return;
	}
}

static void mnVSNetMatchStagingFuncStart(void)
{
	mnVSNetAutomatchLoadWallpaperRelocFiles();

	gcMakeGObjSPAfter(nGCCommonKindPlayerSelect, mnVSNetMatchStagingFuncRun, 15, GOBJ_PRIORITY_DEFAULT);
	gcMakeDefaultCameraGObj(16, GOBJ_PRIORITY_DEFAULT, 100, COBJ_FLAG_ZBUFFER, GPACK_RGBA8888(0x00, 0x00, 0x00, 0x00));
	efParticleInitAll();
	efManagerInitEffects();
	mnVSNetAutomatchMakeWallpaperCamera();
	mnVSNetAutomatchMakeWallpaper();
	scSubsysFighterSetLightParams(45.0F, 45.0F, 0xFF, 0xFF, 0xFF, 0xFF);

	mnVSNetAutomatchAMStartSearch();

	if (gSCManagerSceneData.scene_prev != nSCKindMaps)
	{
		syAudioPlayBGM(0, nSYAudioBGMBattleSelect);
	}
}

SYVideoSetup dMNVSNetMatchStagingVideoSetup = SYVIDEO_SETUP_DEFAULT();

SYTaskmanSetup dMNVSNetMatchStagingTaskmanSetup =
{
    {
        0,
        gcRunAll,
        gcDrawAll,
        &ovl27_BSS_END,
        0,
        1,
        2,
        sizeof(Gfx) * 2375,
        sizeof(Gfx) * 64,
        0,
        0,
        0x8000,
        2,
        0x8000,
        mnVSNetAutomatchFuncLights,
        syControllerFuncRead,
    },

    0,
    sizeof(u64) * 32,
    0,
    0,
    0,
    0,
    sizeof(GObj),
    0,
    dLBCommonFuncMatrixList,
    NULL,
    0,
    0,
    0,
    sizeof(DObj),
    0,
    sizeof(SObj),
    0,
    sizeof(CObj),

    mnVSNetMatchStagingFuncStart
};

void mnVSNetMatchStagingStartScene(void)
{
	dMNVSNetMatchStagingVideoSetup.zbuffer = SYVIDEO_ZBUFFER_START(320, 240, 0, 10, u16);
	syVideoInit(&dMNVSNetMatchStagingVideoSetup);

	dMNVSNetMatchStagingTaskmanSetup.scene_setup.arena_size = (size_t)((uintptr_t)&ovl1_VRAM - (uintptr_t)&ovl27_BSS_END);
	syTaskmanStartTask(&dMNVSNetMatchStagingTaskmanSetup);
}

#else /* _WIN32 */

void mnVSNetMatchStagingStartScene(void)
{
}

#endif /* _WIN32 */

#endif /* PORT && SSB64_NETMENU */
