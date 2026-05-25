/*
 * Offline (SSB64_NETMENU=OFF): netrollbacksnapshot.c is linked for syNetRbSnap* coupling
 * helpers used by PORT decomp TUs, but netsync.c is not. Provide no-op hash/reconcile stubs
 * for snapshot save paths (unused when rollback is inactive).
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

u32 syNetSyncHashRNGSeed(void)
{
	return 0U;
}

u32 syNetSyncHashGMCamera(void)
{
	return 0U;
}

u32 syNetSyncHashFighterAnimationStateForRollback(void)
{
	return 0U;
}
