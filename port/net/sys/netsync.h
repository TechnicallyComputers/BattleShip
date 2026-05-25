#ifndef _SYNETSYNC_H_
#define _SYNETSYNC_H_

/*
 * Deterministic gameplay fingerprint helpers for netplay debugging — **not** authoritative desync repair.
 *
 * These hashes sample a narrow slice of fighter/map state so two machines can compare `port_log` lines
 * after the same confirmed input window. They intentionally ignore most of the ROM; expanding coverage
 * belongs in dedicated investigations, not silent hot paths.
 */
#include <PR/ultratypes.h>

#include <gm/gmdef.h>
#include <sys/netrollbacksnapshot.h>

/* Item rollback hash uses the same active-item cap as snapshot blobs (no silent hash-only extras). */
#define SYNET_SYNC_ITEM_HASH_SORT_MAX SYNETRB_SNAPSHOT_MAX_ITEMS
#define SYNET_SYNC_WEAPON_HASH_SORT_MAX SYNETRB_SNAPSHOT_MAX_WEAPONS

struct FTStruct;

/* XOR-FNV style fold of per-fighter contributions (order independent over fighter list). */
extern u32 syNetSyncHashBattleFighters(void);
/* Map collision / kinematic sentinel hash for broad “world moved” diagnostics. */
extern u32 syNetSyncHashMapCollisionKinematics(void);
/*
 * `gcRunAll`-shaped traversal fingerprint (common links + process queue order). Cross-peer-stable fields only;
 * see `gcPortHashGcRunAllTraversalFingerprint` / `docs/netplay_frame_composition.md`.
 */
extern u32 syNetSyncHashGcRunAllTraversalFingerprint(void);
/* Narrow per-fighter fingerprint for phase tracing (subset of battle fighter hash). */
extern u32 syNetSyncHashFighterStructLight(const struct FTStruct *fp);
/* Per-slot Full hash contribution (matches one fighter fold inside `syNetSyncHashBattleFightersFull`). */
extern u32 syNetSyncHashFighterSlotFull(const struct FTStruct *fp);
/* Per-slot rollback animation fold (matches one fighter in `syNetSyncHashFighterAnimationStateForRollback`). */
extern u32 syNetSyncHashFighterSlotAnim(const struct FTStruct *fp, struct GObj *fighter_gobj);

/* Rollback CSI subsystem hashes (canonical field sampling; PORT). */
extern u32 syNetSyncHashBattleFightersFull(void);
extern u32 syNetSyncHashRollbackWorld(void);
extern u32 syNetSyncHashActiveItems(void);
extern u32 syNetSyncHashActiveWeapons(void);
/*
 * Item / weapon fingerprints for rollback load-verify: **omit item/weapon GObj id** (respawn after snapshot
 * allocates fresh ids). Item walk uses syNetRbEnumerateActiveItemsSorted (same cap/order as snapshot blobs).
 * Includes item `type` so the fold aligns with snapshot blobs better than the diagnostic hash.
 */
extern u32 syNetSyncHashActiveItemsForRollback(void);
extern u32 syNetSyncHashActiveWeaponsForRollback(void);
extern u32 syNetSyncHashRNGSeed(void);
extern u32 syNetSyncHashGMCamera(void);
/*
 * Diagnostic animation fingerprint: full AObj walk per joint (no 16-node cap). Merges fighter folds by player slot like
 * `syNetSyncHashBattleFighters` — **not** used for rollback (`syNetSyncHashFighterAnimationStateForRollback`).
 */
extern u32 syNetSyncHashFighterAnimationState(void);
/*
 * Animation fingerprint constrained to rollback snapshot coverage: first SYNETROLLBACK_SNAPSHOT_AOBJ_CHAIN_MAX
 * AObj nodes per joint, same fields as `SYNetRbSnapAObjNodeBlob`. Must match `netrollbacksnapshot.c` capture/apply.
 *
 * Fold is per fighter slot (player index); slots merge in fixed 0..GMCOMMON_PLAYERS_MAX-1 order like
 * `syNetSyncHashBattleFighters`, not fighter common-link traversal order — cross-peer-stable when per-slot folds match.
 */
/* Yoshi grab/throw (and similar) can reach 9+ AObj nodes per joint; 8 truncated resim (joint4/6 @ tick 569). */
#define SYNETROLLBACK_SNAPSHOT_AOBJ_CHAIN_MAX 16
extern u32 syNetSyncHashFighterAnimationStateForRollback(void);
#ifdef PORT
/* Rollback-world hash partitions (see syNetSyncHashRollbackWorld). */
typedef struct SYNetSyncRollbackWorldComponents
{
	u32 hash_battle_time;
	u32 hash_battle_players;
	u32 hash_spawn_wait;
	u32 hash_appear_tables;
	u32 hash_random_tables;
	u32 hash_combined;
	s32 time_remain;
	s32 time_passed;
	s32 game_status;
	s32 stock_count[GMCOMMON_PLAYERS_MAX];
	s32 score[GMCOMMON_PLAYERS_MAX];
	s32 falls[GMCOMMON_PLAYERS_MAX];
	u32 stale_id[GMCOMMON_PLAYERS_MAX];
	u32 spawn_wait;
	u32 appear_weights_sum;
	u32 appear_valids_num;
	u32 mapobjs_num;
	u32 mapobjs_hash;
	u32 random_weights_sum;
	u32 random_valids_num;
	u32 random_kinds_hash;
	u32 random_blocks_hash;
} SYNetSyncRollbackWorldComponents;

extern void syNetSyncCollectRollbackWorldComponents(SYNetSyncRollbackWorldComponents *out);
extern void syNetSyncLogWorldHashDiff(const char *tag, u32 tick, const SYNetSyncRollbackWorldComponents *peer,
				      const SYNetSyncRollbackWorldComponents *local);
extern s32 syNetSyncPeerDivergeDetailEnabled(void);
extern void syNetSyncLogRollbackWorldDetail(const char *tag, u32 tick);
extern void syNetSyncLogFighterDetail(const char *tag, u32 tick);
/* `SSB64_NETPLAY_JOINT_TRANSLATE_TRACE=1`: per-joint translate + Full-only scalars each forward-sim tick. */
extern void syNetSyncResetJointTranslateTraceSession(void);
extern void syNetSyncRefreshJointTranslateTraceEnvCache(void);
extern void syNetSyncLogFighterJointTranslateTrace(u32 tick);
extern void syNetSyncJointTranslateTraceOnFighStep(u32 tick, u32 figh);
/* First per-slot `fhash_light` step on live forward sim: bookmark + fighter_detail at prev_tick and tick. */
extern void syNetSyncResetFhashLightMismatchTriggerSession(void);
extern void syNetSyncFhashLightMismatchTriggerOnTick(u32 tick);
/* Netplay: battle clock tied to authoritative sim tick (not wall-clock scheduler tics). */
extern void syNetSyncResetNetplayBattleClock(void);
extern void syNetSyncOnNetplayBattleGo(void);
extern void syNetSyncReconcileBattleTimePassedForSimTick(u32 sim_tick);
extern void syNetSyncReconcileBattleTimePassedFromSimTick(void);
/* `SSB64_NETPLAY_ITEM_HASH_TRACE=1`: log GObj walk order + per-item fold at hash-compute time. */
extern void syNetSyncLogItemHashWalkTrace(u32 sim_tick);
/* Per-player `syNetSyncHashFighterStructLight` at `sim_state_tick` (`SSB64_NETPLAY_FIGHTER_SLOT_HASH_LOG`). */
extern void syNetSyncCollectFighterSlotHashes(u32 out_slot_hash[GMCOMMON_PLAYERS_MAX]);
extern void syNetSyncLogFighterSlotHashes(u32 tick);
/* Ness PK Thunder hold: per-tick coupling/physics (`SSB64_NETPLAY_PKTHUNDER_HOLD_DIAG=1`). */
extern void syNetSyncLogPKThunderHoldDiag(u32 tick);
/* Field-level baseline mismatch when world/rng/item agree but figh differs. */
extern void syNetSyncLogBaselineUniverseDiff(u32 load_tick, u32 peer_figh, u32 local_figh, u32 peer_world,
					     u32 local_world, u32 peer_rng, u32 local_rng);
#endif

#endif /* _SYNETSYNC_H_ */
