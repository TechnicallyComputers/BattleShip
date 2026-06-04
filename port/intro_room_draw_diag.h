#ifndef INTRO_ROOM_DRAW_DIAG_H
#define INTRO_ROOM_DRAW_DIAG_H

#include <ssb_types.h>

struct DObj;

#ifdef PORT

/* Intro opening-room draw visibility diagnostics (SSB64_DECOMP_DIAG=1).
 * Per-frame counters + camera snapshot for A/B NETMENU ON vs OFF.
 * Target frame: SSB64_INTRO_DRAW_DIAG_FRAME (default 58; use "auto" for first
 * background gSPDisplayList).
 *
 * PORT-only debug tooling — not netplay rollback policy. Frame transitions are
 * driven from syTaskman (not draw hooks) so a stale mvOpeningRoom BSS pointer
 * after scene eject cannot defer flush into a later VS/CSS draw. */

sb32 ssb64IntroRoomDrawDiagEnabled(void);

void ssb64IntroRoomDrawDiagOnTaskFrame(u32 frame);
void ssb64IntroRoomDrawDiagOnSceneReset(void);

void ssb64IntroRoomDrawDiagOnDObjMatrixPrep(struct DObj *dobj, s32 matrix_pushed, f32 scale_x);
void ssb64IntroRoomDrawDiagOnDisplayList(
    struct DObj *dobj,
    s32 list_id,
    s32 matrix_pushed,
    f32 scale_x);

#else

#define ssb64IntroRoomDrawDiagEnabled() FALSE
#define ssb64IntroRoomDrawDiagOnTaskFrame(frame) ((void)0)
#define ssb64IntroRoomDrawDiagOnSceneReset() ((void)0)
#define ssb64IntroRoomDrawDiagOnDObjMatrixPrep(dobj, matrix_pushed, scale_x) ((void)0)
#define ssb64IntroRoomDrawDiagOnDisplayList(dobj, list_id, matrix_pushed, scale_x) ((void)0)

#endif /* PORT */

#endif /* INTRO_ROOM_DRAW_DIAG_H */
