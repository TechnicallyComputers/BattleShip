#include <sys/netsync.h>

#include <ft/fighter.h>
#include <ft/ftdef.h>
#include <gm/gmdef.h>
#include <gm/gmcamera.h>
#include <it/item.h>
#include <mp/map.h>
#include <sc/scmanager.h>
#include <sc/sctypes.h>
#include <sys/objdef.h>
#include <sys/objman.h>
#include <sys/objman_gcport.h>
#include <sys/utils.h>
#include <wp/weapon.h>

#include <it/itmanager.h>

#ifdef PORT
#include <sys/netinput.h>
#include <sys/netpeer.h>

extern void port_log(const char *fmt, ...);

static u32 sSYNetSyncBattleGoSimTick = ~(u32)0;
#endif

/*
 * NetSync — cheap, partial-state checksums for diagnosing divergent gameplay.
 * When NetPeer logs `SSB64 NetSync:` lines, hashes should match across peers for the same input window.
 *
 * These are *helpers* — they do **not** initiate rollback (netrollback uses input history for that).
 */

static u32 syNetSyncFnvAccumulateU32(u32 hash, u32 value)
{
	hash ^= value;
	hash *= 16777619U;

	return hash;
}

static u32 syNetSyncHashF32(f32 value)
{
	union SYNetSyncF32Reinterpret
	{
		f32 fv;
		u32 uv;

	} reinterpret;

	reinterpret.fv = value;

	return reinterpret.uv;
}

#ifdef PORT
static u32 syNetSyncHashU8Array(const u8 *values, s32 count)
{
	u32 hash = 2166136261U;
	s32 i;

	if ((values == NULL) || (count <= 0))
	{
		return hash;
	}
	for (i = 0; i < count; i++)
	{
		hash = syNetSyncFnvAccumulateU32(hash, (u32)values[i]);
	}
	return hash;
}

static u32 syNetSyncHashU16Array(const u16 *values, s32 count)
{
	u32 hash = 2166136261U;
	s32 i;

	if ((values == NULL) || (count <= 0))
	{
		return hash;
	}
	for (i = 0; i < count; i++)
	{
		hash = syNetSyncFnvAccumulateU32(hash, (u32)values[i]);
	}
	return hash;
}
#endif

u32 syNetSyncHashFighterStructLight(const FTStruct *fp)
{
	u32 h;

	if (fp == NULL)
	{
		return 2166136261U;
	}
	h = 2166136261U;
	h = syNetSyncFnvAccumulateU32(h, (u32)fp->player);
	h = syNetSyncFnvAccumulateU32(h, (u32)fp->fkind);
	h = syNetSyncFnvAccumulateU32(h, (u32)fp->status_id);
	h = syNetSyncFnvAccumulateU32(h, (u32)fp->motion_id);
	h = syNetSyncFnvAccumulateU32(h, (u32)fp->percent_damage);
	h = syNetSyncFnvAccumulateU32(h, (u32)fp->stock_count);
	h = syNetSyncFnvAccumulateU32(h, (u32)fp->lr);
	h = syNetSyncFnvAccumulateU32(h, (u32)(fp->ga != FALSE));

	h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->physics.vel_air.x));
	h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->physics.vel_air.y));
	h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->physics.vel_air.z));
	h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->physics.vel_ground.x));
	h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->physics.vel_ground.z));
	h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->physics.vel_damage_ground));

	h = syNetSyncFnvAccumulateU32(h, (u32)fp->hitlag_tics);
	h = syNetSyncFnvAccumulateU32(h, (u32)fp->status_total_tics);
	if (fp->joints[nFTPartsJointTopN] != NULL)
	{
		h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->joints[nFTPartsJointTopN]->translate.vec.f.x));
		h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->joints[nFTPartsJointTopN]->translate.vec.f.y));
		h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->joints[nFTPartsJointTopN]->translate.vec.f.z));
	}
	h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->physics.vel_damage_air.x));
	h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->physics.vel_damage_air.y));
	h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->physics.vel_damage_air.z));

	h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->coll_data.pos_prev.x));
	h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->coll_data.pos_prev.y));
	h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->coll_data.pos_prev.z));
	return h;
}

/* Walk active fighter GObj list; fold selected scalars per player slot, then merge slots deterministically. */
u32 syNetSyncHashBattleFighters(void)
{
	GObj *fighter_gobj;
	u32 slot_hash[GMCOMMON_PLAYERS_MAX];
	s32 si;

	for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
	{
		slot_hash[si] = 2166136261U;
	}

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		u32 contribution;
		s32 slot;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			continue;
		}

		contribution = 2166136261U;

		contribution = syNetSyncFnvAccumulateU32(contribution, (u32)fp->player);
		contribution = syNetSyncFnvAccumulateU32(contribution, (u32)fp->fkind);
		contribution = syNetSyncFnvAccumulateU32(contribution, (u32)fp->status_id);
		contribution = syNetSyncFnvAccumulateU32(contribution, (u32)fp->motion_id);
		contribution = syNetSyncFnvAccumulateU32(contribution, (u32)fp->percent_damage);
		contribution = syNetSyncFnvAccumulateU32(contribution, (u32)fp->stock_count);
		contribution = syNetSyncFnvAccumulateU32(contribution, (u32)fp->lr);
		contribution = syNetSyncFnvAccumulateU32(contribution, (u32)(fp->ga != FALSE));

		contribution = syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->physics.vel_air.x));
		contribution = syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->physics.vel_air.y));
		contribution = syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->physics.vel_air.z));
		contribution = syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->physics.vel_ground.x));
		contribution = syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->physics.vel_ground.z));
		contribution = syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->physics.vel_damage_ground));

		contribution = syNetSyncFnvAccumulateU32(contribution, (u32)fp->hitlag_tics);
		contribution = syNetSyncFnvAccumulateU32(contribution, (u32)fp->status_total_tics);
		if (fp->joints[nFTPartsJointTopN] != NULL)
		{
			contribution =
				syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->joints[nFTPartsJointTopN]->translate.vec.f.x));
			contribution =
				syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->joints[nFTPartsJointTopN]->translate.vec.f.y));
			contribution =
				syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->joints[nFTPartsJointTopN]->translate.vec.f.z));
		}
		contribution = syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->physics.vel_damage_air.x));
		contribution = syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->physics.vel_damage_air.y));
		contribution = syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->physics.vel_damage_air.z));

		contribution = syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->coll_data.pos_prev.x));
		contribution = syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->coll_data.pos_prev.y));
		contribution = syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->coll_data.pos_prev.z));

		slot = fp->player;

		if ((slot >= 0) && (slot < GMCOMMON_PLAYERS_MAX))
		{
			slot_hash[slot] =
				syNetSyncFnvAccumulateU32(slot_hash[slot] ^ contribution, (u32)slot ^ 0x9E3779B9U);
		}
		else
		{
			slot_hash[0] = syNetSyncFnvAccumulateU32(slot_hash[0] ^ contribution, (u32)slot ^ 0x85EBCA77U);
		}
	}
	{
		u32 merged = 2166136261U;

		for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
		{
			merged = syNetSyncFnvAccumulateU32(merged ^ slot_hash[si], (u32)si);
		}
		return merged;
	}
}

#define SYNETSYNC_MAX_MP_YAKU 64

/*
 * Sample up to SYNETSYNC_MAX_MP_YAKU yakumono kinematic entries (stage moving pieces / hazards).
 * Intended as a canary for “map half of sim diverged”; not a full world hash.
 */
u32 syNetSyncHashMapCollisionKinematics(void)
{
	u32 hash;
	s32 i;
	s32 n;
	s32 cap;
	DObj *dobj;

	hash = 2166136261U;
	hash = syNetSyncFnvAccumulateU32(hash, (u32)gMPCollisionUpdateTic);
	n = gMPCollisionYakumonosNum;
	if (n < 0)
	{
		n = 0;
	}
	cap = (n > SYNETSYNC_MAX_MP_YAKU) ? SYNETSYNC_MAX_MP_YAKU : n;
	if ((gMPCollisionYakumonoDObjs == NULL) || (gMPCollisionSpeeds == NULL))
	{
		return hash;
	}
	for (i = 0; i < cap; i++)
	{
		dobj = gMPCollisionYakumonoDObjs->dobjs[i];
		if (dobj == NULL)
		{
			continue;
		}
		hash = syNetSyncFnvAccumulateU32(hash, (u32)dobj->user_data.s);
		hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(dobj->translate.vec.f.x));
		hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(dobj->translate.vec.f.y));
		hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(dobj->translate.vec.f.z));
		hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(gMPCollisionSpeeds[i].x));
		hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(gMPCollisionSpeeds[i].y));
		hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(gMPCollisionSpeeds[i].z));
	}
	return hash;
}

u32 syNetSyncHashGcRunAllTraversalFingerprint(void)
{
	return gcPortHashGcRunAllTraversalFingerprint();
}

u32 syNetSyncHashBattleFightersFull(void)
{
	GObj *fighter_gobj;
	u32 slot_hash[GMCOMMON_PLAYERS_MAX];
	s32 si;

	for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
	{
		slot_hash[si] = 2166136261U;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		u32 contribution;
		s32 slot;
		s32 ji;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			continue;
		}
		contribution = syNetSyncHashFighterStructLight(fp);
		contribution = syNetSyncFnvAccumulateU32(contribution, (u32)fp->shield_health);
		contribution = syNetSyncFnvAccumulateU32(contribution, (u32)fp->jumps_used);
		contribution = syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->physics.vel_jostle_x));
		contribution = syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->physics.vel_jostle_z));
		contribution = syNetSyncFnvAccumulateU32(contribution, (u32)fp->motion_attack_id);
		contribution = syNetSyncFnvAccumulateU32(contribution, (u32)fp->hitstatus);
		contribution = syNetSyncFnvAccumulateU32(contribution, (u32)fp->invincible_tics);
		contribution = syNetSyncFnvAccumulateU32(contribution, (u32)(fp->is_hitstun != FALSE));
		contribution = syNetSyncFnvAccumulateU32(contribution, (u32)(fp->is_shield != FALSE));
		for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
		{
			if (fp->joints[ji] != NULL)
			{
				contribution =
				    syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->joints[ji]->translate.vec.f.x));
				contribution =
				    syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->joints[ji]->translate.vec.f.y));
				contribution =
				    syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->joints[ji]->translate.vec.f.z));
			}
		}
		slot = fp->player;
		if ((slot >= 0) && (slot < GMCOMMON_PLAYERS_MAX))
		{
			slot_hash[slot] =
			    syNetSyncFnvAccumulateU32(slot_hash[slot] ^ contribution, (u32)slot ^ 0x9E3779B9U);
		}
	}
	{
		u32 merged = 2166136261U;

		for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
		{
			merged = syNetSyncFnvAccumulateU32(merged ^ slot_hash[si], (u32)si);
		}
		return merged;
	}
}

#ifdef PORT
void syNetSyncResetNetplayBattleClock(void)
{
	sSYNetSyncBattleGoSimTick = ~(u32)0;
}

void syNetSyncOnNetplayBattleGo(void)
{
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	if (sSYNetSyncBattleGoSimTick != ~(u32)0)
	{
		return;
	}
	sSYNetSyncBattleGoSimTick = syNetInputGetTick();
}

void syNetSyncReconcileBattleTimePassedForSimTick(u32 sim_tick)
{
	u32 derived;

	if ((gSCManagerBattleState == NULL) || (syNetPeerIsVSSessionActive() == FALSE))
	{
		return;
	}
	if (gSCManagerBattleState->game_status != nSCBattleGameStatusGo)
	{
		return;
	}
	if (sSYNetSyncBattleGoSimTick == ~(u32)0)
	{
		sSYNetSyncBattleGoSimTick = sim_tick;
	}
	derived = (sim_tick > sSYNetSyncBattleGoSimTick) ? (sim_tick - sSYNetSyncBattleGoSimTick) : 0U;
	gSCManagerBattleState->time_passed = derived;
}

void syNetSyncReconcileBattleTimePassedFromSimTick(void)
{
	syNetSyncReconcileBattleTimePassedForSimTick(syNetInputGetTick());
}
#endif

u32 syNetSyncHashRollbackWorld(void)
{
	u32 hash = 2166136261U;
	s32 pi;
	s32 i;
	s32 n;

	if (gSCManagerBattleState != NULL)
	{
		hash = syNetSyncFnvAccumulateU32(hash, (u32)gSCManagerBattleState->time_remain);
		hash = syNetSyncFnvAccumulateU32(hash, (u32)gSCManagerBattleState->time_passed);
		hash = syNetSyncFnvAccumulateU32(hash, (u32)gSCManagerBattleState->game_status);
		for (pi = 0; pi < GMCOMMON_PLAYERS_MAX; pi++)
		{
			hash = syNetSyncFnvAccumulateU32(hash, (u32)gSCManagerBattleState->players[pi].stock_count);
			hash = syNetSyncFnvAccumulateU32(hash, (u32)gSCManagerBattleState->players[pi].score);
			hash = syNetSyncFnvAccumulateU32(hash, (u32)gSCManagerBattleState->players[pi].falls);
			hash = syNetSyncFnvAccumulateU32(hash, (u32)gSCManagerBattleState->players[pi].stale_id);
		}
	}
	hash = syNetSyncFnvAccumulateU32(hash, (u32)gITManagerAppearActor.spawn_wait);
	hash = syNetSyncFnvAccumulateU32(hash, (u32)gITManagerAppearActor.weights.weights_sum);
	hash = syNetSyncFnvAccumulateU32(hash, (u32)gITManagerAppearActor.weights.valids_num);
	hash = syNetSyncFnvAccumulateU32(hash, (u32)gITManagerAppearActor.mapobjs_num);
	n = gITManagerAppearActor.mapobjs_num;
	if (n > nITKindEnumCount)
	{
		n = nITKindEnumCount;
	}
	if ((gITManagerAppearActor.mapobjs != NULL) && (n > 0))
	{
		for (i = 0; i < n; i++)
		{
			hash = syNetSyncFnvAccumulateU32(hash, (u32)gITManagerAppearActor.mapobjs[i]);
		}
	}
	hash = syNetSyncFnvAccumulateU32(hash, (u32)gITManagerRandomWeights.weights_sum);
	hash = syNetSyncFnvAccumulateU32(hash, (u32)gITManagerRandomWeights.valids_num);
	n = gITManagerRandomWeights.valids_num;
	if (n > nITKindEnumCount)
	{
		n = nITKindEnumCount;
	}
	if ((gITManagerRandomWeights.kinds != NULL) && (gITManagerRandomWeights.blocks != NULL) && (n > 0))
	{
		for (i = 0; i < n; i++)
		{
			hash = syNetSyncFnvAccumulateU32(hash, (u32)gITManagerRandomWeights.kinds[i]);
			hash = syNetSyncFnvAccumulateU32(hash, (u32)gITManagerRandomWeights.blocks[i]);
		}
	}
	return hash;
}

#ifdef PORT
void syNetSyncLogRollbackWorldDetail(const char *tag, u32 tick)
{
	s32 mapobjs_num;
	s32 random_valids_num;
	u32 mapobjs_hash;
	u32 random_kinds_hash;
	u32 random_blocks_hash;

	mapobjs_num = gITManagerAppearActor.mapobjs_num;
	if (mapobjs_num > nITKindEnumCount)
	{
		mapobjs_num = nITKindEnumCount;
	}
	random_valids_num = gITManagerRandomWeights.valids_num;
	if (random_valids_num > nITKindEnumCount)
	{
		random_valids_num = nITKindEnumCount;
	}
	mapobjs_hash = syNetSyncHashU8Array(gITManagerAppearActor.mapobjs, mapobjs_num);
	random_kinds_hash = syNetSyncHashU8Array(gITManagerRandomWeights.kinds, random_valids_num);
	random_blocks_hash = syNetSyncHashU16Array(gITManagerRandomWeights.blocks, random_valids_num);
	if (gSCManagerBattleState != NULL)
	{
		port_log(
		    "SSB64 NetSync: world_detail tag=%s tick=%u time_remain=%d time_passed=%d status=%d "
		    "p0=%d/%d/%d/%u p1=%d/%d/%d/%u p2=%d/%d/%d/%u p3=%d/%d/%d/%u "
		    "spawn_wait=%u appear_sum=%u appear_valid=%u mapobjs=%u map_hash=0x%08X random_sum=%u random_valid=%u random_kinds=0x%08X random_blocks=0x%08X world=0x%08X\n",
		    tag,
		    tick,
		    (int)gSCManagerBattleState->time_remain,
		    (int)gSCManagerBattleState->time_passed,
		    (int)gSCManagerBattleState->game_status,
		    (int)gSCManagerBattleState->players[0].stock_count,
		    (int)gSCManagerBattleState->players[0].score,
		    (int)gSCManagerBattleState->players[0].falls,
		    (unsigned int)gSCManagerBattleState->players[0].stale_id,
		    (int)gSCManagerBattleState->players[1].stock_count,
		    (int)gSCManagerBattleState->players[1].score,
		    (int)gSCManagerBattleState->players[1].falls,
		    (unsigned int)gSCManagerBattleState->players[1].stale_id,
		    (int)gSCManagerBattleState->players[2].stock_count,
		    (int)gSCManagerBattleState->players[2].score,
		    (int)gSCManagerBattleState->players[2].falls,
		    (unsigned int)gSCManagerBattleState->players[2].stale_id,
		    (int)gSCManagerBattleState->players[3].stock_count,
		    (int)gSCManagerBattleState->players[3].score,
		    (int)gSCManagerBattleState->players[3].falls,
		    (unsigned int)gSCManagerBattleState->players[3].stale_id,
		    (unsigned int)gITManagerAppearActor.spawn_wait,
		    (unsigned int)gITManagerAppearActor.weights.weights_sum,
		    (unsigned int)gITManagerAppearActor.weights.valids_num,
		    (unsigned int)gITManagerAppearActor.mapobjs_num,
		    mapobjs_hash,
		    (unsigned int)gITManagerRandomWeights.weights_sum,
		    (unsigned int)gITManagerRandomWeights.valids_num,
		    random_kinds_hash,
		    random_blocks_hash,
		    syNetSyncHashRollbackWorld());
	}
	else
	{
		port_log(
		    "SSB64 NetSync: world_detail tag=%s tick=%u battle=NULL spawn_wait=%u appear_sum=%u appear_valid=%u "
		    "mapobjs=%u map_hash=0x%08X random_sum=%u random_valid=%u random_kinds=0x%08X random_blocks=0x%08X world=0x%08X\n",
		    tag,
		    tick,
		    (unsigned int)gITManagerAppearActor.spawn_wait,
		    (unsigned int)gITManagerAppearActor.weights.weights_sum,
		    (unsigned int)gITManagerAppearActor.weights.valids_num,
		    (unsigned int)gITManagerAppearActor.mapobjs_num,
		    mapobjs_hash,
		    (unsigned int)gITManagerRandomWeights.weights_sum,
		    (unsigned int)gITManagerRandomWeights.valids_num,
		    random_kinds_hash,
		    random_blocks_hash,
		    syNetSyncHashRollbackWorld());
	}
}

void syNetSyncLogFighterDetail(const char *tag, u32 tick)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);
		DObj *topn;
		Vec3f top_pos;

		if (fp == NULL)
		{
			continue;
		}
		top_pos.x = top_pos.y = top_pos.z = 0.0F;
		topn = fp->joints[nFTPartsJointTopN];
		if (topn != NULL)
		{
			top_pos = topn->translate.vec.f;
		}
		port_log(
		    "SSB64 NetSync: fighter_detail tag=%s tick=%u slot=%d gobj=%u fkind=%d status=%d motion=%d dmg=%d shield=%d stock=%d lr=%d ga=%d jumps=%u hitlag=%u status_tics=%u "
		    "top=(0x%08X,0x%08X,0x%08X) pos_prev=(0x%08X,0x%08X,0x%08X) pos_diff=(0x%08X,0x%08X,0x%08X) "
		    "vel_air=(0x%08X,0x%08X,0x%08X) vel_dmg_air=(0x%08X,0x%08X,0x%08X) vel_ground=(0x%08X,0x%08X) vel_dmg_ground=0x%08X vel_jostle=(0x%08X,0x%08X) "
		    "coll_masks=%04X/%04X/%04X/%04X coll_tic=%u floor=%d ceil=%d lwall=%d rwall=%d cliff=%d ignore=%d floor_flags=0x%08X ceil_flags=0x%08X lwall_flags=0x%08X rwall_flags=0x%08X "
		    "flags atk=%u hitstun=%u fastfall=%u shield=%u cliff=%u catch=%u capture=%u hitstatus=%d inv=%d intan=%d dmgq=%d dmg_angle=%d dmg_lr=%d dmg_player=%d knock=0x%08X fhash=0x%08X\n",
		    tag,
		    tick,
		    (int)fp->player,
		    fighter_gobj->id,
		    (int)fp->fkind,
		    (int)fp->status_id,
		    (int)fp->motion_id,
		    (int)fp->percent_damage,
		    (int)fp->shield_health,
		    (int)fp->stock_count,
		    (int)fp->lr,
		    (int)(fp->ga != FALSE),
		    (unsigned int)fp->jumps_used,
		    (unsigned int)fp->hitlag_tics,
		    (unsigned int)fp->status_total_tics,
		    syNetSyncHashF32(top_pos.x),
		    syNetSyncHashF32(top_pos.y),
		    syNetSyncHashF32(top_pos.z),
		    syNetSyncHashF32(fp->coll_data.pos_prev.x),
		    syNetSyncHashF32(fp->coll_data.pos_prev.y),
		    syNetSyncHashF32(fp->coll_data.pos_prev.z),
		    syNetSyncHashF32(fp->coll_data.pos_diff.x),
		    syNetSyncHashF32(fp->coll_data.pos_diff.y),
		    syNetSyncHashF32(fp->coll_data.pos_diff.z),
		    syNetSyncHashF32(fp->physics.vel_air.x),
		    syNetSyncHashF32(fp->physics.vel_air.y),
		    syNetSyncHashF32(fp->physics.vel_air.z),
		    syNetSyncHashF32(fp->physics.vel_damage_air.x),
		    syNetSyncHashF32(fp->physics.vel_damage_air.y),
		    syNetSyncHashF32(fp->physics.vel_damage_air.z),
		    syNetSyncHashF32(fp->physics.vel_ground.x),
		    syNetSyncHashF32(fp->physics.vel_ground.z),
		    syNetSyncHashF32(fp->physics.vel_damage_ground),
		    syNetSyncHashF32(fp->physics.vel_jostle_x),
		    syNetSyncHashF32(fp->physics.vel_jostle_z),
		    (unsigned int)fp->coll_data.mask_prev,
		    (unsigned int)fp->coll_data.mask_curr,
		    (unsigned int)fp->coll_data.mask_unk,
		    (unsigned int)fp->coll_data.mask_stat,
		    (unsigned int)fp->coll_data.update_tic,
		    (int)fp->coll_data.floor_line_id,
		    (int)fp->coll_data.ceil_line_id,
		    (int)fp->coll_data.lwall_line_id,
		    (int)fp->coll_data.rwall_line_id,
		    (int)fp->coll_data.cliff_id,
		    (int)fp->coll_data.ignore_line_id,
		    (unsigned int)fp->coll_data.floor_flags,
		    (unsigned int)fp->coll_data.ceil_flags,
		    (unsigned int)fp->coll_data.lwall_flags,
		    (unsigned int)fp->coll_data.rwall_flags,
		    (unsigned int)(fp->is_attack_active != FALSE),
		    (unsigned int)(fp->is_hitstun != FALSE),
		    (unsigned int)(fp->is_fastfall != FALSE),
		    (unsigned int)(fp->is_shield != FALSE),
		    (unsigned int)(fp->is_cliff_hold != FALSE),
		    (unsigned int)(fp->is_catchstatus != FALSE),
		    (unsigned int)(fp->is_catch_or_capture != FALSE),
		    (int)fp->hitstatus,
		    (int)fp->invincible_tics,
		    (int)fp->intangible_tics,
		    (int)fp->damage_queue,
		    (int)fp->damage_angle,
		    (int)fp->damage_lr,
		    (int)fp->damage_player,
		    syNetSyncHashF32(fp->damage_knockback),
		    syNetSyncHashFighterStructLight(fp));
	}
}
#endif

u32 syNetSyncHashActiveItems(void)
{
	GObj *gobj;
	u32 hash = 2166136261U;

	for (gobj = gGCCommonLinks[nGCCommonLinkIDItem]; gobj != NULL; gobj = gobj->link_next)
	{
		DObj *dobj;
		ITStruct *ip = itGetStruct(gobj);
		u32 fold;

		if (ip == NULL)
		{
			continue;
		}
		fold = 2166136261U;
		fold = syNetSyncFnvAccumulateU32(fold, gobj->id);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->kind);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->lifetime);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->percent_damage);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->lr);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->player);
		fold = syNetSyncFnvAccumulateU32(fold, (ip->owner_gobj != NULL) ? (u32)ip->owner_gobj->id : 0U);
		dobj = DObjGetStruct(gobj);
		if (dobj != NULL)
		{
			Vec3f pos = dobj->translate.vec.f;

			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.x));
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.y));
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.z));
		}
		hash ^= fold;
		hash = syNetSyncFnvAccumulateU32(hash, 0xA5A5A5A5U);
	}
	return hash;
}

u32 syNetSyncHashActiveItemsForRollback(void)
{
	GObj *gobj;
	u32 hash = 2166136261U;

	for (gobj = gGCCommonLinks[nGCCommonLinkIDItem]; gobj != NULL; gobj = gobj->link_next)
	{
		DObj *dobj;
		ITStruct *ip = itGetStruct(gobj);
		u32 fold;

		if (ip == NULL)
		{
			continue;
		}
		fold = 2166136261U;
		fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->kind);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->type);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->lifetime);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->percent_damage);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->lr);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->player);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->team);
		fold = syNetSyncFnvAccumulateU32(fold, (ip->owner_gobj != NULL) ? (u32)ip->owner_gobj->id : 0U);
		dobj = DObjGetStruct(gobj);
		if (dobj != NULL)
		{
			Vec3f pos = dobj->translate.vec.f;

			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.x));
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.y));
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.z));
		}
		hash ^= fold;
		hash = syNetSyncFnvAccumulateU32(hash, 0xA5A5A5A5U);
	}
	return hash;
}

u32 syNetSyncHashActiveWeapons(void)
{
	GObj *gobj;
	u32 hash = 2166136261U;

	for (gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; gobj != NULL; gobj = gobj->link_next)
	{
		DObj *dobj;
		WPStruct *wp = wpGetStruct(gobj);
		u32 fold;

		if (wp == NULL)
		{
			continue;
		}
		fold = 2166136261U;
		fold = syNetSyncFnvAccumulateU32(fold, gobj->id);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)wp->kind);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)wp->lifetime);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)wp->group_id);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)wp->lr);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)wp->player);
		fold = syNetSyncFnvAccumulateU32(fold, (wp->owner_gobj != NULL) ? (u32)wp->owner_gobj->id : 0U);
		dobj = DObjGetStruct(gobj);
		if (dobj != NULL)
		{
			Vec3f pos = dobj->translate.vec.f;

			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.x));
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.y));
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.z));
		}
		hash ^= fold;
		hash = syNetSyncFnvAccumulateU32(hash, 0x5A5A5A5AU);
	}
	return hash;
}

u32 syNetSyncHashActiveWeaponsForRollback(void)
{
	GObj *gobj;
	u32 hash = 2166136261U;

	for (gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; gobj != NULL; gobj = gobj->link_next)
	{
		DObj *dobj;
		WPStruct *wp = wpGetStruct(gobj);
		u32 fold;

		if (wp == NULL)
		{
			continue;
		}
		fold = 2166136261U;
		fold = syNetSyncFnvAccumulateU32(fold, (u32)wp->kind);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)wp->lifetime);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)wp->group_id);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)wp->lr);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)wp->player);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)wp->team);
		fold = syNetSyncFnvAccumulateU32(fold, (wp->owner_gobj != NULL) ? (u32)wp->owner_gobj->id : 0U);
		dobj = DObjGetStruct(gobj);
		if (dobj != NULL)
		{
			Vec3f pos = dobj->translate.vec.f;

			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.x));
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.y));
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.z));
		}
		hash ^= fold;
		hash = syNetSyncFnvAccumulateU32(hash, 0x5A5A5A5AU);
	}
	return hash;
}

u32 syNetSyncHashRNGSeed(void)
{
	return syNetSyncFnvAccumulateU32(2166136261U, (u32)syUtilsRandSeed());
}

u32 syNetSyncHashGMCamera(void)
{
	u32 hash = 2166136261U;
	extern f32 gGMCameraPauseCameraEyeX;
	extern f32 gGMCameraPauseCameraEyeY;

	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(gGMCameraStruct.target_dist));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(gGMCameraStruct.fovy));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(gGMCameraStruct.pzoom_eye_x));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(gGMCameraStruct.pzoom_eye_y));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(gGMCameraPauseCameraEyeX));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(gGMCameraPauseCameraEyeY));
	return hash;
}

u32 syNetSyncHashFighterAnimationState(void)
{
	GObj *fighter_gobj;
	u32 hash = 2166136261U;
	s32 ji;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);
		u32 fold = 2166136261U;

		if (fp == NULL)
		{
			continue;
		}
		fold = syNetSyncFnvAccumulateU32(fold, fighter_gobj->id);
		fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(fighter_gobj->anim_frame));
		for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
		{
			if (fp->joints[ji] != NULL)
			{
				AObj *aobj;

				fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(fp->joints[ji]->anim_frame));
				fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(fp->joints[ji]->anim_wait));
				fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(fp->joints[ji]->anim_speed));
				for (aobj = fp->joints[ji]->aobj; aobj != NULL; aobj = aobj->next)
				{
					fold = syNetSyncFnvAccumulateU32(fold, (u32)aobj->track);
					fold = syNetSyncFnvAccumulateU32(fold, (u32)aobj->kind);
					fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(aobj->length_invert));
					fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(aobj->length));
					fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(aobj->value_base));
					fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(aobj->value_target));
					fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(aobj->rate_base));
					fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(aobj->rate_target));
				}
			}
		}
		hash ^= fold;
		hash = syNetSyncFnvAccumulateU32(hash, (u32)fp->player);
	}
	return hash;
}

u32 syNetSyncHashFighterAnimationStateForRollback(void)
{
	GObj *fighter_gobj;
	u32 hash = 2166136261U;
	s32 ji;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);
		u32 fold = 2166136261U;

		if (fp == NULL)
		{
			continue;
		}
		fold = syNetSyncFnvAccumulateU32(fold, fighter_gobj->id);
		fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(fighter_gobj->anim_frame));
		for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
		{
			if (fp->joints[ji] != NULL)
			{
				AObj *aobj;
				u32 aobj_steps;

				fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(fp->joints[ji]->anim_frame));
				fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(fp->joints[ji]->anim_wait));
				fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(fp->joints[ji]->anim_speed));
				for (aobj = fp->joints[ji]->aobj, aobj_steps = 0U;
				     (aobj != NULL) && (aobj_steps < (u32)SYNETROLLBACK_SNAPSHOT_AOBJ_CHAIN_MAX);
				     aobj = aobj->next, aobj_steps++)
				{
					fold = syNetSyncFnvAccumulateU32(fold, (u32)aobj->track);
					fold = syNetSyncFnvAccumulateU32(fold, (u32)aobj->kind);
					fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(aobj->length_invert));
					fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(aobj->length));
					fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(aobj->value_base));
					fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(aobj->value_target));
					fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(aobj->rate_base));
					fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(aobj->rate_target));
				}
			}
		}
		hash ^= fold;
		hash = syNetSyncFnvAccumulateU32(hash, (u32)fp->player);
	}
	return hash;
}
