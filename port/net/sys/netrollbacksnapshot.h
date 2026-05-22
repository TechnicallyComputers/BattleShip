#ifndef _SYNETRB_SNAPSHOT_H_
#define _SYNETRB_SNAPSHOT_H_

/*
 * Typed rollback snapshot ring — gameplay closure for GGPO-style resim.
 * Snapshots are keyed only to completed authoritative sim ticks.
 */

#include <PR/ultratypes.h>
#include <ssb_types.h>

struct GObj;

#define SYNETRB_SNAPSHOT_RING_DEFAULT 64
#define SYNETRB_SNAPSHOT_RING_MAX     128
#define SYNETRB_SNAPSHOT_MAX_YAKU     64
#define SYNETRB_SNAPSHOT_MAX_ITEMS    32
#define SYNETRB_SNAPSHOT_MAX_WEAPONS  32
#define SYNETRB_SNAPSHOT_MAX_MAPOBJS  16

extern void syNetRbSnapshotInit(void);
extern void syNetRbSnapshotResetSession(void);
extern u32 syNetRbSnapshotRingCapacity(void);
/* Per-match depth from auto negotiation (clamped 1..SYNETRB_SNAPSHOT_RING_MAX). */
extern void syNetRbSnapshotSetRingFramesForSession(u32 frames);

extern sb32 syNetRbSnapshotSave(u32 completed_sim_tick);
extern sb32 syNetRbSnapshotSaveMarked(u32 completed_sim_tick, sb32 is_load_safe);
extern sb32 syNetRbSnapshotLoad(u32 completed_sim_tick);
#ifdef PORT
#include <ft/ftdef.h>
/* All-or-nothing load safety: capture live world before rollback load, restore on verify failure. */
extern sb32 syNetRbSnapshotCaptureLiveEmergency(void);
extern sb32 syNetRbSnapshotRestoreLiveEmergency(void);
/* Presentation sync + fighter-coupled weapon rebind before load-hash verify. */
extern void syNetRbSnapshotFinalizeLoad(u32 completed_sim_tick);
/* Rebind status procs after load verify (proc pointers are not hashed). */
extern void syNetRbSnapshotRebindAllFighters(void);
/* TRUE if any fighter link has catch_gobj or capture_gobj set (all slots). */
extern sb32 syNetRbSnapshotAnyFighterGrabCouplingActive(void);
/* TRUE if any item is held or any fighter has item_gobj set (all slots). */
extern sb32 syNetRbSnapshotAnyItemHoldCouplingActive(void);
/* Coupled-weapon rebind + weapon hit positions only (no figatree presentation sync). */
extern void syNetRbSnapshotFinalizeLoadCoupling(u32 completed_sim_tick);
/* Live mid-sim reacquire when fighter coupling pointer was cleared but weapon still exists. */
extern struct GObj *syNetRbSnapReacquireYoshiChargeEgg(struct GObj *fighter_gobj);
extern struct GObj *syNetRbSnapReacquireChargeShotForFP(FTStruct *fp);
/* Destroy duplicate/orphan charge eggs (attack_state Off); keep keep_egg_gobj if non-NULL. */
extern void syNetRbSnapCullYoshiChargeEggsForFighter(struct GObj *fighter_gobj, struct GObj *keep_egg_gobj);
/* Destroy duplicate/orphan Samus/Kirby-copy charge shots (is_release FALSE); keep if non-NULL. */
extern void syNetRbSnapCullSamusChargeShotsForFighter(struct GObj *fighter_gobj, struct GObj *keep_charge_gobj);
extern struct GObj *syNetRbSnapReacquireFireballForFighter(struct GObj *fighter_gobj);
extern sb32 syNetRbSnapFireballOwnedByFighter(struct GObj *fighter_gobj);
extern sb32 syNetRbSnapFireballNeedsSpawnAtHand(struct GObj *fighter_gobj, const Vec3f *spawn_pos);
extern struct GObj *syNetRbSnapReacquireFireballAtHand(struct GObj *fighter_gobj, const Vec3f *pos, f32 radius_sq);
extern void syNetRbSnapCullOwnedFireballsNearPose(struct GObj *fighter_gobj, struct GObj *keep_fireball_gobj,
                                                  const Vec3f *pos, f32 radius_sq);
extern void syNetRbSnapTrySpawnFireballFromAccessory(struct GObj *fighter_gobj);
/* Skip held-item spawn when rollback already restored a matching projectile at this pose. */
extern sb32 syNetRbSnapHeldItemWeaponNeedsSpawn(struct GObj *owner_gobj, s32 kind, const Vec3f *spawn_pos,
                                                const Vec3f *spawn_vel);
/* TRUE when periodic synctest must defer (intro wait, item hold/throw, fighter throw anim). */
extern sb32 syNetRbSnapshotSynctestShouldSkip(const char **reason_out);
/* `SSB64_NETPLAY_SNAPSHOT_FIGHTER_DIAG=1`: per-slot lines when load verify logs drift. */
extern void syNetRbSnapshotLogFighterLoadVerifyDiag(u32 tick, u32 live_f, u32 slot_f, u32 live_a, u32 slot_a);
/* `SSB64_NETPLAY_SNAPSHOT_FIGHTER_FIELD_DIFF=1`: named field lines when load verify figh drifts. */
extern void syNetRbSnapshotLogFighterFieldDiffOnLoadDrift(u32 tick);
extern void syNetRbSnapshotLogFighterFieldDiffAtTick(u32 tick, const char *tag);
#endif
/*
 * Figatree presentation sync only (no status entry / motion event replay on default path).
 * Prefer syNetRbSnapshotFinalizeLoad for rollback commit paths — it runs this plus coupled-weapon rebind.
 */
extern void syNetRbSnapshotSyncFighterPresentation(void);

/*
 * Collect active item GObjs (valid ITStruct), insertion-sorted by gobj->id.
 * out must hold at least max entries. Returns count stored; *truncated_out TRUE if link has more than max.
 */
extern s32 syNetRbEnumerateActiveItemsSorted(struct GObj **out, s32 max, sb32 *truncated_out);

#ifdef PORT
/* Subsystem hashes stored on the slot (for load verify / diagnostics). */
extern u32 syNetRbSnapshotGetSlotHashFighter(u32 tick);
extern u32 syNetRbSnapshotGetSlotHashWorld(u32 tick);
extern u32 syNetRbSnapshotGetSlotHashItem(u32 tick);
extern u32 syNetRbSnapshotGetSlotHashWeapon(u32 tick);
extern u32 syNetRbSnapshotGetSlotHashMap(u32 tick);
/* Stored yakumono slot count at save tick; -1 if slot invalid or map partition empty. */
extern s32 syNetRbSnapshotGetSlotMapYakumonoCount(u32 tick);
extern u32 syNetRbSnapshotGetSlotHashRng(u32 tick);
extern u32 syNetRbSnapshotGetSlotHashCamera(u32 tick);
extern u32 syNetRbSnapshotGetSlotHashAnimation(u32 tick);
extern sb32 syNetRbSnapshotGetStoredSubsystemHashes(u32 tick, u32 *figh, u32 *world, u32 *item, u32 *rng);
/* Ring slot published for tick (valid + tick match + save completed). */
extern sb32 syNetRbSnapshotIsTickCommitted(u32 tick);
/* Walk backward from tick down to min_tick inclusive; ~(u32)0 if none. */
extern u32 syNetRbSnapshotFindLatestValidTickAtOrBefore(u32 tick, u32 min_tick);
/* Same as above but only slots marked load-safe (no predicted-remote sim at that tick). */
extern u32 syNetRbSnapshotFindLatestLoadSafeTickAtOrBefore(u32 tick, u32 min_tick);
extern u32 syNetRbSnapshotGetLastLoadSafeTick(void);
extern void syNetRbSnapshotMarkLoadUnsafe(u32 tick);
#endif

#endif /* _SYNETRB_SNAPSHOT_H_ */
