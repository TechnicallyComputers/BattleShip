#ifndef SYS_NETFIGHTERPHASE_H
#define SYS_NETFIGHTERPHASE_H

#include <PR/ultratypes.h>

struct GObj;

/*
 * Per-slot fighter update phase tracing (input snapshot → pl copy → end of params).
 * Linux UDP VS when `SSB64_NETMENU` is enabled. Gated by env; see `docs/netplay_frame_composition.md`.
 */
void syNetFighterPhaseTraceGcRunAllBegin(void);
void syNetFighterPhaseOnInterruptVeryStart(struct GObj *fighter_gobj);
void syNetFighterPhaseOnInterruptAfterInputControl(struct GObj *fighter_gobj);
void syNetFighterPhaseOnParamsEnd(struct GObj *fighter_gobj);
void syNetFighterPhaseTraceEmitNetSyncLines(u32 validation_tick);

#endif /* SYS_NETFIGHTERPHASE_H */
