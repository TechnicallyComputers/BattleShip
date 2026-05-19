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

/* Rollback CSI subsystem hashes (canonical field sampling; PORT). */
extern u32 syNetSyncHashBattleFightersFull(void);
extern u32 syNetSyncHashRollbackWorld(void);
extern u32 syNetSyncHashActiveItems(void);
extern u32 syNetSyncHashActiveWeapons(void);
/*
 * Item / weapon fingerprints for rollback load-verify: **omit item/weapon GObj id** (respawn after snapshot
 * allocates fresh ids).Traversal order matches the common link walk. Includes item `type` so the fold aligns
 * with snapshot blobs better than the diagnostic hash.
 */
extern u32 syNetSyncHashActiveItemsForRollback(void);
extern u32 syNetSyncHashActiveWeaponsForRollback(void);
extern u32 syNetSyncHashRNGSeed(void);
extern u32 syNetSyncHashGMCamera(void);
extern u32 syNetSyncHashFighterAnimationState(void);
/*
 * Animation fingerprint constrained to rollback snapshot coverage: first SYNETROLLBACK_SNAPSHOT_AOBJ_CHAIN_MAX
 * AObj nodes per joint, same fields as `SYNetRbSnapAObjNodeBlob`. Must match `netrollbacksnapshot.c` capture/apply.
 */
#define SYNETROLLBACK_SNAPSHOT_AOBJ_CHAIN_MAX 6
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
/* Netplay: battle clock tied to authoritative sim tick (not wall-clock scheduler tics). */
extern void syNetSyncResetNetplayBattleClock(void);
extern void syNetSyncOnNetplayBattleGo(void);
extern void syNetSyncReconcileBattleTimePassedForSimTick(u32 sim_tick);
extern void syNetSyncReconcileBattleTimePassedFromSimTick(void);
/* `SSB64_NETPLAY_ITEM_HASH_TRACE=1`: log GObj walk order + per-item fold at hash-compute time. */
extern void syNetSyncLogItemHashWalkTrace(u32 sim_tick);
#endif

#endif /* _SYNETSYNC_H_ */
