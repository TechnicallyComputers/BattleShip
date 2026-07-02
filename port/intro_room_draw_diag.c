/* Intro opening-room draw visibility diagnostics for SSB64_DECOMP_DIAG A/B.
 * See port/intro_room_draw_diag.h. */

#include "intro_room_draw_diag.h"

#ifdef PORT

/* decomp/include/stdlib.h shadows libc — declare host libc helpers explicitly. */
extern char *getenv(const char *name);
extern int strcmp(const char *s1, const char *s2);
extern long strtol(const char *nptr, char **endptr, int base);

#include "port_log.h"

#include <sys/obj.h>
#include <sys/objdef.h>
#include <sys/taskman.h>

/* mvOpeningRoom BSS — linked from decomp movie scene. Pointers are NOT cleared on
 * gcEjectGObj (see mvOpeningRoomEjectCameraGObjs / mvOpeningRoomEjectRoomGObjs). */
extern GObj *sMVOpeningRoomGObj;
extern GObj *sMVOpeningRoomMainCameraGObj;
extern GObj *sMVOpeningRoomOutsideGObj;
extern GObj *sMVOpeningRoomSunlightGObj;
extern GObj *sMVOpeningRoomOutsideHazeGObj;
extern GObj *sMVOpeningRoomSpotlightGObj;
extern GObj *sMVOpeningRoomDeskGObj;
extern GObj *sMVOpeningRoomLogoGObj;
extern GObj *gGCCurrentCamera;

typedef enum IntroRoomDrawDiagCategory
{
    INTRO_ROOM_DRAW_DIAG_CAT_NONE = 0,
    INTRO_ROOM_DRAW_DIAG_CAT_BACKGROUND,
    INTRO_ROOM_DRAW_DIAG_CAT_OUTSIDE,
    INTRO_ROOM_DRAW_DIAG_CAT_SUNLIGHT,
    INTRO_ROOM_DRAW_DIAG_CAT_HAZE,
    INTRO_ROOM_DRAW_DIAG_CAT_SPOTLIGHT,
    INTRO_ROOM_DRAW_DIAG_CAT_DESK,
    INTRO_ROOM_DRAW_DIAG_CAT_LOGO,
    INTRO_ROOM_DRAW_DIAG_CAT_OTHER_OPENING,
    INTRO_ROOM_DRAW_DIAG_CAT_COUNT
} IntroRoomDrawDiagCategory;

typedef struct IntroRoomDrawDiagCounters
{
    u32 matrix_prep;
    u32 display_list;
} IntroRoomDrawDiagCounters;

static sb32 sIntroRoomDrawDiagEnabled = -1;
static u32 sIntroRoomDrawDiagFrameActive;
static u32 sIntroRoomDrawDiagTargetFrame;
static sb32 sIntroRoomDrawDiagTargetResolved;
static sb32 sIntroRoomDrawDiagUseAutoFrame;
static sb32 sIntroRoomDrawDiagAutoFrameCaptured;
static sb32 sIntroRoomDrawDiagTargetFlushed;
static sb32 sIntroRoomDrawDiagCameraLogged;
static IntroRoomDrawDiagCounters sIntroRoomDrawDiagCounters[INTRO_ROOM_DRAW_DIAG_CAT_COUNT];
static s32 sIntroRoomDrawDiagBackgroundSamples;

static sb32 ssb64IntroRoomDrawDiagParseEnabled(void)
{
    const char *env = getenv("SSB64_DECOMP_DIAG");

    return (env != NULL) && (env[0] != '\0') && (strcmp(env, "0") != 0);
}

static u32 ssb64IntroRoomDrawDiagParseTargetFrame(void)
{
    const char *env = getenv("SSB64_INTRO_DRAW_DIAG_FRAME");

    if (env == NULL || env[0] == '\0')
    {
        return 58u;
    }
    if (strcmp(env, "auto") == 0)
    {
        sIntroRoomDrawDiagUseAutoFrame = TRUE;
        return 0u;
    }
    return (u32)strtol(env, NULL, 10);
}

static void ssb64IntroRoomDrawDiagResolveTarget(void)
{
    if (sIntroRoomDrawDiagTargetResolved != FALSE)
    {
        return;
    }
    sIntroRoomDrawDiagTargetFrame = ssb64IntroRoomDrawDiagParseTargetFrame();
    sIntroRoomDrawDiagTargetResolved = TRUE;
}

sb32 ssb64IntroRoomDrawDiagEnabled(void)
{
    if (sIntroRoomDrawDiagEnabled < 0)
    {
        sIntroRoomDrawDiagEnabled = ssb64IntroRoomDrawDiagParseEnabled();
        ssb64IntroRoomDrawDiagResolveTarget();
    }
    return sIntroRoomDrawDiagEnabled;
}

/* PORT wrapper: mvOpeningRoom globals survive scene eject; validate obj_kind + payload. */
static sb32 ssb64IntroRoomDrawDiagGObjLive(GObj *gobj, u8 expected_obj_kind)
{
    if (gobj == NULL || gobj->obj == NULL)
    {
        return FALSE;
    }
    return (gobj->obj_kind == expected_obj_kind);
}

static sb32 ssb64IntroRoomDrawDiagSceneLive(void)
{
    return ssb64IntroRoomDrawDiagGObjLive(sMVOpeningRoomGObj, nGCCommonAppendDObj);
}

static IntroRoomDrawDiagCategory ssb64IntroRoomDrawDiagCategoryForGObj(GObj *gobj)
{
    if (gobj == NULL)
    {
        return INTRO_ROOM_DRAW_DIAG_CAT_NONE;
    }
    if (gobj == sMVOpeningRoomGObj)
    {
        return INTRO_ROOM_DRAW_DIAG_CAT_BACKGROUND;
    }
    if (gobj == sMVOpeningRoomOutsideGObj)
    {
        return INTRO_ROOM_DRAW_DIAG_CAT_OUTSIDE;
    }
    if (gobj == sMVOpeningRoomSunlightGObj)
    {
        return INTRO_ROOM_DRAW_DIAG_CAT_SUNLIGHT;
    }
    if (gobj == sMVOpeningRoomOutsideHazeGObj)
    {
        return INTRO_ROOM_DRAW_DIAG_CAT_HAZE;
    }
    if (gobj == sMVOpeningRoomSpotlightGObj)
    {
        return INTRO_ROOM_DRAW_DIAG_CAT_SPOTLIGHT;
    }
    if (gobj == sMVOpeningRoomDeskGObj)
    {
        return INTRO_ROOM_DRAW_DIAG_CAT_DESK;
    }
    if (gobj == sMVOpeningRoomLogoGObj)
    {
        return INTRO_ROOM_DRAW_DIAG_CAT_LOGO;
    }
    if (ssb64IntroRoomDrawDiagSceneLive() != FALSE)
    {
        return INTRO_ROOM_DRAW_DIAG_CAT_OTHER_OPENING;
    }
    return INTRO_ROOM_DRAW_DIAG_CAT_NONE;
}

static const char *ssb64IntroRoomDrawDiagCategoryName(IntroRoomDrawDiagCategory cat)
{
    switch (cat)
    {
    case INTRO_ROOM_DRAW_DIAG_CAT_BACKGROUND:
        return "background";
    case INTRO_ROOM_DRAW_DIAG_CAT_OUTSIDE:
        return "outside";
    case INTRO_ROOM_DRAW_DIAG_CAT_SUNLIGHT:
        return "sunlight";
    case INTRO_ROOM_DRAW_DIAG_CAT_HAZE:
        return "haze";
    case INTRO_ROOM_DRAW_DIAG_CAT_SPOTLIGHT:
        return "spotlight";
    case INTRO_ROOM_DRAW_DIAG_CAT_DESK:
        return "desk";
    case INTRO_ROOM_DRAW_DIAG_CAT_LOGO:
        return "logo";
    case INTRO_ROOM_DRAW_DIAG_CAT_OTHER_OPENING:
        return "opening-other";
    default:
        return "none";
    }
}

static void ssb64IntroRoomDrawDiagResetCounters(void)
{
    s32 i;

    for (i = 0; i < INTRO_ROOM_DRAW_DIAG_CAT_COUNT; i++)
    {
        sIntroRoomDrawDiagCounters[i].matrix_prep = 0;
        sIntroRoomDrawDiagCounters[i].display_list = 0;
    }
    sIntroRoomDrawDiagBackgroundSamples = 0;
    sIntroRoomDrawDiagCameraLogged = FALSE;
}

static void ssb64IntroRoomDrawDiagLogCamera(void)
{
    CObj *main_cobj;
    CObj *curr_cobj;

    if (sIntroRoomDrawDiagCameraLogged != FALSE)
    {
        return;
    }
    sIntroRoomDrawDiagCameraLogged = TRUE;

    if (ssb64IntroRoomDrawDiagGObjLive(sMVOpeningRoomMainCameraGObj, nGCCommonAppendCamera) != FALSE)
    {
        main_cobj = CObjGetStruct(sMVOpeningRoomMainCameraGObj);
        if (main_cobj != NULL)
        {
            port_log(
                "SSB64: introDrawDiag camera main gobj=%p eye=(%.3f,%.3f,%.3f) at=(%.3f,%.3f,%.3f) "
                "fovy=%.3f aspect=%.3f near=%.3f far=%.3f scale=%.3f frame=%u\n",
                sMVOpeningRoomMainCameraGObj,
                main_cobj->vec.eye.x,
                main_cobj->vec.eye.y,
                main_cobj->vec.eye.z,
                main_cobj->vec.at.x,
                main_cobj->vec.at.y,
                main_cobj->vec.at.z,
                main_cobj->projection.persp.fovy,
                main_cobj->projection.persp.aspect,
                main_cobj->projection.persp.near,
                main_cobj->projection.persp.far,
                main_cobj->projection.persp.scale,
                (unsigned)dSYTaskmanFrameCount);
        }
    }
    if (ssb64IntroRoomDrawDiagGObjLive(gGCCurrentCamera, nGCCommonAppendCamera) != FALSE)
    {
        curr_cobj = CObjGetStruct(gGCCurrentCamera);
        if (curr_cobj != NULL)
        {
            port_log(
                "SSB64: introDrawDiag camera current gobj=%p eye=(%.3f,%.3f,%.3f) at=(%.3f,%.3f,%.3f) "
                "fovy=%.3f aspect=%.3f near=%.3f far=%.3f scale=%.3f frame=%u\n",
                gGCCurrentCamera,
                curr_cobj->vec.eye.x,
                curr_cobj->vec.eye.y,
                curr_cobj->vec.eye.z,
                curr_cobj->vec.at.x,
                curr_cobj->vec.at.y,
                curr_cobj->vec.at.z,
                curr_cobj->projection.persp.fovy,
                curr_cobj->projection.persp.aspect,
                curr_cobj->projection.persp.near,
                curr_cobj->projection.persp.far,
                curr_cobj->projection.persp.scale,
                (unsigned)dSYTaskmanFrameCount);
        }
    }
}

static void ssb64IntroRoomDrawDiagFlush(u32 frame)
{
    s32 i;

    port_log("SSB64: introDrawDiag flush frame=%u opening_room_live=%d background_gobj=%p\n",
        (unsigned)frame,
        ssb64IntroRoomDrawDiagSceneLive(),
        (void *)sMVOpeningRoomGObj);

    ssb64IntroRoomDrawDiagLogCamera();

    for (i = 0; i < INTRO_ROOM_DRAW_DIAG_CAT_COUNT; i++)
    {
        if (sIntroRoomDrawDiagCounters[i].matrix_prep != 0
            || sIntroRoomDrawDiagCounters[i].display_list != 0)
        {
            port_log(
                "SSB64: introDrawDiag counts frame=%u cat=%s matrix_prep=%u display_list=%u\n",
                (unsigned)frame,
                ssb64IntroRoomDrawDiagCategoryName((IntroRoomDrawDiagCategory)i),
                sIntroRoomDrawDiagCounters[i].matrix_prep,
                sIntroRoomDrawDiagCounters[i].display_list);
        }
    }
}

static sb32 ssb64IntroRoomDrawDiagIsTargetFrame(u32 frame)
{
    ssb64IntroRoomDrawDiagResolveTarget();

    if (sIntroRoomDrawDiagUseAutoFrame != FALSE)
    {
        return (sIntroRoomDrawDiagAutoFrameCaptured != FALSE)
            && (frame == sIntroRoomDrawDiagTargetFrame);
    }
    return (frame == sIntroRoomDrawDiagTargetFrame);
}

static sb32 ssb64IntroRoomDrawDiagCanRecord(void)
{
    if (ssb64IntroRoomDrawDiagEnabled() == FALSE || sIntroRoomDrawDiagTargetFlushed != FALSE)
    {
        return FALSE;
    }
    if (ssb64IntroRoomDrawDiagSceneLive() == FALSE)
    {
        return FALSE;
    }
    return (sIntroRoomDrawDiagFrameActive == dSYTaskmanFrameCount);
}

static void ssb64IntroRoomDrawDiagMaybeCaptureAutoFrame(IntroRoomDrawDiagCategory cat)
{
    if (sIntroRoomDrawDiagUseAutoFrame == FALSE || sIntroRoomDrawDiagAutoFrameCaptured != FALSE)
    {
        return;
    }
    if (cat != INTRO_ROOM_DRAW_DIAG_CAT_BACKGROUND)
    {
        return;
    }
    sIntroRoomDrawDiagTargetFrame = dSYTaskmanFrameCount;
    sIntroRoomDrawDiagAutoFrameCaptured = TRUE;
    port_log(
        "SSB64: introDrawDiag auto target frame=%u (first background display_list)\n",
        (unsigned)sIntroRoomDrawDiagTargetFrame);
}

static void ssb64IntroRoomDrawDiagLogBackgroundSample(
    DObj *dobj,
    s32 matrix_pushed,
    f32 scale_x,
    s32 list_id)
{
    if (sIntroRoomDrawDiagBackgroundSamples >= 8)
    {
        return;
    }
    sIntroRoomDrawDiagBackgroundSamples++;
    port_log(
        "SSB64: introDrawDiag background sample[%d] frame=%u dobj=%p translate=(%.3f,%.3f,%.3f) "
        "rotate=(%.3f,%.3f,%.3f) scale=(%.3f,%.3f,%.3f) matrix_pushed=%d scale_x=%.6f list_id=%d "
        "flags=0x%02x mobj=%p dl_link=%p\n",
        sIntroRoomDrawDiagBackgroundSamples - 1,
        (unsigned)dSYTaskmanFrameCount,
        (void *)dobj,
        dobj->translate.vec.f.x,
        dobj->translate.vec.f.y,
        dobj->translate.vec.f.z,
        dobj->rotate.vec.f.x,
        dobj->rotate.vec.f.y,
        dobj->rotate.vec.f.z,
        dobj->scale.vec.f.x,
        dobj->scale.vec.f.y,
        dobj->scale.vec.f.z,
        matrix_pushed,
        scale_x,
        list_id,
        (unsigned)dobj->flags,
        (void *)dobj->mobj,
        (void *)dobj->dl_link);
}

void ssb64IntroRoomDrawDiagOnTaskFrame(u32 frame)
{
    if (ssb64IntroRoomDrawDiagEnabled() == FALSE || sIntroRoomDrawDiagTargetFlushed != FALSE)
    {
        return;
    }
    if (sIntroRoomDrawDiagFrameActive == frame)
    {
        return;
    }
    if (sIntroRoomDrawDiagFrameActive != 0
        && ssb64IntroRoomDrawDiagIsTargetFrame(sIntroRoomDrawDiagFrameActive) != FALSE
        && ssb64IntroRoomDrawDiagSceneLive() != FALSE)
    {
        ssb64IntroRoomDrawDiagFlush(sIntroRoomDrawDiagFrameActive);
        sIntroRoomDrawDiagTargetFlushed = TRUE;
        sIntroRoomDrawDiagFrameActive = frame;
        ssb64IntroRoomDrawDiagResetCounters();
        return;
    }
    sIntroRoomDrawDiagFrameActive = frame;
    ssb64IntroRoomDrawDiagResetCounters();
}

void ssb64IntroRoomDrawDiagOnSceneReset(void)
{
    if (ssb64IntroRoomDrawDiagEnabled() == FALSE)
    {
        return;
    }
    /* Taskman epoch reset — drop stale frame bucket without flush (opening-room BSS may dangle). */
    sIntroRoomDrawDiagFrameActive = 0;
    ssb64IntroRoomDrawDiagResetCounters();
}

static void ssb64IntroRoomDrawDiagRecordMatrixPrep(DObj *dobj, s32 matrix_pushed, f32 scale_x)
{
    GObj *gobj;
    IntroRoomDrawDiagCategory cat;

    if (dobj == NULL || ssb64IntroRoomDrawDiagCanRecord() == FALSE)
    {
        return;
    }

    gobj = dobj->parent_gobj;
    cat = ssb64IntroRoomDrawDiagCategoryForGObj(gobj);
    if (cat == INTRO_ROOM_DRAW_DIAG_CAT_NONE)
    {
        return;
    }

    sIntroRoomDrawDiagCounters[cat].matrix_prep++;
    (void)matrix_pushed;
    (void)scale_x;
}

static void ssb64IntroRoomDrawDiagRecordDisplayList(
    DObj *dobj,
    s32 list_id,
    s32 matrix_pushed,
    f32 scale_x)
{
    GObj *gobj;
    IntroRoomDrawDiagCategory cat;

    if (dobj == NULL || ssb64IntroRoomDrawDiagCanRecord() == FALSE)
    {
        return;
    }

    gobj = dobj->parent_gobj;
    cat = ssb64IntroRoomDrawDiagCategoryForGObj(gobj);
    if (cat == INTRO_ROOM_DRAW_DIAG_CAT_NONE)
    {
        return;
    }

    sIntroRoomDrawDiagCounters[cat].display_list++;

    if (cat == INTRO_ROOM_DRAW_DIAG_CAT_BACKGROUND)
    {
        ssb64IntroRoomDrawDiagMaybeCaptureAutoFrame(cat);
        if (ssb64IntroRoomDrawDiagIsTargetFrame(dSYTaskmanFrameCount) != FALSE)
        {
            ssb64IntroRoomDrawDiagLogBackgroundSample(dobj, matrix_pushed, scale_x, list_id);
        }
    }
}

void ssb64IntroRoomDrawDiagOnDObjMatrixPrep(DObj *dobj, s32 matrix_pushed, f32 scale_x)
{
    ssb64IntroRoomDrawDiagRecordMatrixPrep(dobj, matrix_pushed, scale_x);
}

void ssb64IntroRoomDrawDiagOnDisplayList(DObj *dobj, s32 list_id, s32 matrix_pushed, f32 scale_x)
{
    ssb64IntroRoomDrawDiagRecordDisplayList(dobj, list_id, matrix_pushed, scale_x);
}

#endif /* PORT */
