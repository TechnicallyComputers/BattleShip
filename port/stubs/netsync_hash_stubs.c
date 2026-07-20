/*
 * Offline (SSB64_NETMENU=OFF): netsync hash/reconcile stubs for port shell symbols.
 * Rollback snapshot TUs are not linked; decomp net blocks are compile-stripped.
 */
#include <sys/netsync.h>

#include <ft/ftdef.h>
#include <sys/objdef.h>

u32 syNetSyncHashFighterStructLight(const FTStruct *fp)
{
	(void)fp;
	return 0U;
}

u32 syNetSyncHashFighterSlotFull(const FTStruct *fp)
{
	(void)fp;
	return 0U;
}

u32 syNetSyncHashFighterSlotAnim(const FTStruct *fp, GObj *fighter_gobj)
{
	(void)fp;
	(void)fighter_gobj;
	return 0U;
}

void syNetSyncReconcileBattleTimePassedForSimTick(u32 sim_tick)
{
	(void)sim_tick;
}

void syNetSyncReconcileBattleTimePassedForSnapshotSave(u32 completed_sim_tick)
{
	(void)completed_sim_tick;
}

void syNetSyncResetNetplayBattleClock(void)
{
}

void syNetSyncLatchNetplayCountdownCreatedSimTick(u32 sim_tick)
{
	(void)sim_tick;
}

sb32 syNetSyncShouldDeferCountdownGoFromThread(void)
{
	return FALSE;
}

void syNetSyncTryApplyAuthoritativeNetplayGo(u32 sim_tick)
{
	(void)sim_tick;
}

void syNetSyncOnNetplayBattleGo(void)
{
}

void syNetSyncLogNetplayBattleGoApply(u32 sim_tick)
{
	(void)sim_tick;
}

u32 syNetSyncNetplayEffectiveTimeLimitMinutes(void)
{
	return 0U;
}

#ifndef SSB64_UPSTREAM_DECOMP_NET_SYS
u32 syNetSyncHashBattleFighters(void)
{
	return 0U;
}
#endif

u32 syNetSyncHashBattleFightersFull(void)
{
	return 0U;
}

u32 syNetSyncHashRollbackWorld(void)
{
	return 0U;
}

u32 syNetSyncHashActiveItemsForRollback(void)
{
	return 0U;
}

u32 syNetSyncHashActiveEffectsForRollback(void)
{
	return 0U;
}

u32 syNetSyncHashActiveWeaponsForRollback(void)
{
	return 0U;
}

u32 syNetSyncHashMapCollisionKinematics(void)
{
	return 0U;
}

u32 syNetSyncHashMapCollisionKinematicsForRollback(void)
{
	return 0U;
}

u32 syNetSyncHashRNGSeed(void)
{
	return 0U;
}

void syNetSyncRngTraceBeforeGameSeedStep(void)
{
}

void syNetSyncRngTraceAfterGameSeedStep(s32 seed_after, u32 caller_site)
{
	(void)seed_after;
	(void)caller_site;
}

void syNetSyncLogRngHashWalkTrace(u32 sim_tick)
{
	(void)sim_tick;
}

void syNetSyncLogRngHashDriftDiag(u32 sim_tick, u32 local_rng, u32 peer_rng, const char *reason)
{
	(void)sim_tick;
	(void)local_rng;
	(void)peer_rng;
	(void)reason;
}

u32 syNetSyncHashGMCamera(void)
{
	return 0U;
}

u32 syNetSyncHashFighterAnimationStateForRollback(void)
{
	return 0U;
}
