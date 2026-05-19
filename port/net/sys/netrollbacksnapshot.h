#ifndef _SYNETRB_SNAPSHOT_H_
#define _SYNETRB_SNAPSHOT_H_

/*
 * Typed rollback snapshot ring — gameplay closure for GGPO-style resim.
 * Snapshots are keyed only to completed authoritative sim ticks.
 */

#include <PR/ultratypes.h>
#include <ssb_types.h>

#define SYNETRB_SNAPSHOT_RING_DEFAULT 64
#define SYNETRB_SNAPSHOT_RING_MAX     128
#define SYNETRB_SNAPSHOT_MAX_YAKU     64
#define SYNETRB_SNAPSHOT_MAX_ITEMS    16
#define SYNETRB_SNAPSHOT_MAX_WEAPONS  32
#define SYNETRB_SNAPSHOT_MAX_MAPOBJS  16

extern void syNetRbSnapshotInit(void);
extern void syNetRbSnapshotResetSession(void);
extern u32 syNetRbSnapshotRingCapacity(void);
/* Per-match depth from auto negotiation (clamped 1..SYNETRB_SNAPSHOT_RING_MAX). */
extern void syNetRbSnapshotSetRingFramesForSession(u32 frames);

extern sb32 syNetRbSnapshotSave(u32 completed_sim_tick);
extern sb32 syNetRbSnapshotLoad(u32 completed_sim_tick);
#ifdef PORT
/* All-or-nothing load safety: capture live world before rollback load, restore on verify failure. */
extern sb32 syNetRbSnapshotCaptureLiveEmergency(void);
extern sb32 syNetRbSnapshotRestoreLiveEmergency(void);
#endif

/* After load+apply hook; animation AObj/MObj chains stay live so figatree playback survives rollback. */
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
extern sb32 syNetRbSnapshotGetStoredSubsystemHashes(u32 tick, u32 *figh, u32 *world, u32 *item, u32 *rng);
/* Ring slot published for tick (valid + tick match + save completed). */
extern sb32 syNetRbSnapshotIsTickCommitted(u32 tick);
/* Walk backward from tick down to min_tick inclusive; ~(u32)0 if none. */
extern u32 syNetRbSnapshotFindLatestValidTickAtOrBefore(u32 tick, u32 min_tick);
#endif

#endif /* _SYNETRB_SNAPSHOT_H_ */
