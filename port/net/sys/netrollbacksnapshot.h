#ifndef _SYNETRB_SNAPSHOT_H_
#define _SYNETRB_SNAPSHOT_H_

/*
 * Typed rollback snapshot ring — gameplay closure for GGPO-style resim.
 * Snapshots are keyed only to completed authoritative sim ticks.
 */

#include <PR/ultratypes.h>
#include <ssb_types.h>

#define SYNETRB_SNAPSHOT_RING_DEFAULT 32
#define SYNETRB_SNAPSHOT_RING_MAX     64
#define SYNETRB_SNAPSHOT_MAX_YAKU     64
#define SYNETRB_SNAPSHOT_MAX_ITEMS    16
#define SYNETRB_SNAPSHOT_MAX_WEAPONS  32
#define SYNETRB_SNAPSHOT_MAX_MAPOBJS  16

extern void syNetRbSnapshotInit(void);
extern void syNetRbSnapshotResetSession(void);
extern u32 syNetRbSnapshotRingCapacity(void);

extern sb32 syNetRbSnapshotSave(u32 completed_sim_tick);
extern sb32 syNetRbSnapshotLoad(u32 completed_sim_tick);

/* After load+apply: drop cosmetic AObj/MObj leftovers on fighters/items/weapons. */
extern void syNetRbSnapshotAfterApplyCleanup(void);

#ifdef PORT
/* Subsystem hashes stored on the slot (for load verify / diagnostics). */
extern u32 syNetRbSnapshotGetSlotHashFighter(u32 tick);
extern u32 syNetRbSnapshotGetSlotHashWorld(u32 tick);
extern u32 syNetRbSnapshotGetSlotHashItem(u32 tick);
extern u32 syNetRbSnapshotGetSlotHashWeapon(u32 tick);
extern u32 syNetRbSnapshotGetSlotHashMap(u32 tick);
extern u32 syNetRbSnapshotGetSlotHashRng(u32 tick);
extern u32 syNetRbSnapshotGetSlotHashCamera(u32 tick);
extern u32 syNetRbSnapshotGetSlotHashAnimation(u32 tick);
#endif

#endif /* _SYNETRB_SNAPSHOT_H_ */
