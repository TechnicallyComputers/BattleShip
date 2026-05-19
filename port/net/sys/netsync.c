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

#include <stdlib.h>

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
	syNetSyncResetJointTranslateTraceSession();
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
#include <stdlib.h>
#include <string.h>

static int sSYNetSyncPeerDivergeDetailEnvCache = -999;

static sb32 sSYNetSyncJointTranslateTraceCache = -999;
static s32 sSYNetSyncJointTranslateTraceSlotFilter = -1;
static s32 sSYNetSyncJointTranslateTraceFkindFilter = -1;
static sb32 sSYNetSyncJointTranslateTraceTriggerFired = FALSE;
static u32 sSYNetSyncJointTranslateTracePrevTick = ~(u32)0;
static u32 sSYNetSyncJointTranslateTracePrevFigh = 0U;

static int syNetSyncEnvParseInt(const char *e, int default_val)
{
	long v;

	if ((e == NULL) || (e[0] == '\0'))
	{
		return default_val;
	}
	v = strtol(e, NULL, 10);
	return (int)v;
}

static void syNetSyncRefreshJointTranslateTraceEnv(void)
{
	const char *e;

	if (sSYNetSyncJointTranslateTraceCache != -999)
	{
		return;
	}
	sSYNetSyncJointTranslateTraceSlotFilter = -1;
	sSYNetSyncJointTranslateTraceFkindFilter = -1;
	e = getenv("SSB64_NETPLAY_JOINT_TRANSLATE_TRACE");
	sSYNetSyncJointTranslateTraceCache = (syNetSyncEnvParseInt(e, 0) != 0) ? 1 : 0;
	if (sSYNetSyncJointTranslateTraceCache == 0)
	{
		return;
	}
	e = getenv("SSB64_NETPLAY_JOINT_TRANSLATE_TRACE_SLOT");
	if ((e != NULL) && (e[0] != '\0'))
	{
		sSYNetSyncJointTranslateTraceSlotFilter = syNetSyncEnvParseInt(e, -1);
	}
	e = getenv("SSB64_NETPLAY_JOINT_TRANSLATE_TRACE_FKIND");
	if ((e != NULL) && (e[0] != '\0'))
	{
		sSYNetSyncJointTranslateTraceFkindFilter = syNetSyncEnvParseInt(e, -1);
	}
}

static sb32 syNetSyncJointTranslateTraceEnabled(void)
{
	syNetSyncRefreshJointTranslateTraceEnv();
	return (sSYNetSyncJointTranslateTraceCache != 0) ? TRUE : FALSE;
}

void syNetSyncResetJointTranslateTraceSession(void)
{
	sSYNetSyncJointTranslateTraceTriggerFired = FALSE;
	sSYNetSyncJointTranslateTracePrevTick = ~(u32)0;
	sSYNetSyncJointTranslateTracePrevFigh = 0U;
}

void syNetSyncRefreshJointTranslateTraceEnvCache(void)
{
	sSYNetSyncJointTranslateTraceCache = -999;
}

s32 syNetSyncPeerDivergeDetailEnabled(void)
{
	const char *e;
	int state_detail;

	if (sSYNetSyncPeerDivergeDetailEnvCache != -999)
	{
		return (sSYNetSyncPeerDivergeDetailEnvCache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_PEER_DIVERGE_DETAIL");
	if ((e != NULL) && (e[0] != '\0'))
	{
		sSYNetSyncPeerDivergeDetailEnvCache = (syNetSyncEnvParseInt(e, 0) != 0) ? 1 : 0;
		return (sSYNetSyncPeerDivergeDetailEnvCache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_STATE_DETAIL_DIAG");
	state_detail = syNetSyncEnvParseInt(e, 0);
	if (state_detail < 0)
	{
		state_detail = 0;
	}
	sSYNetSyncPeerDivergeDetailEnvCache = (state_detail >= 1) ? 1 : 0;
	return (sSYNetSyncPeerDivergeDetailEnvCache != 0) ? TRUE : FALSE;
}

void syNetSyncCollectRollbackWorldComponents(SYNetSyncRollbackWorldComponents *out)
{
	s32 pi;
	s32 i;
	s32 n;
	s32 mapobjs_num;
	s32 random_valids_num;

	if (out == NULL)
	{
		return;
	}
	memset(out, 0, sizeof(*out));
	out->hash_battle_time = 2166136261U;
	out->hash_battle_players = 2166136261U;
	out->hash_spawn_wait = 2166136261U;
	out->hash_appear_tables = 2166136261U;
	out->hash_random_tables = 2166136261U;
	if (gSCManagerBattleState != NULL)
	{
		out->time_remain = gSCManagerBattleState->time_remain;
		out->time_passed = gSCManagerBattleState->time_passed;
		out->game_status = gSCManagerBattleState->game_status;
		out->hash_battle_time =
		    syNetSyncFnvAccumulateU32(out->hash_battle_time, (u32)out->time_remain);
		out->hash_battle_time =
		    syNetSyncFnvAccumulateU32(out->hash_battle_time, (u32)out->time_passed);
		out->hash_battle_time =
		    syNetSyncFnvAccumulateU32(out->hash_battle_time, (u32)out->game_status);
		for (pi = 0; pi < GMCOMMON_PLAYERS_MAX; pi++)
		{
			out->stock_count[pi] = gSCManagerBattleState->players[pi].stock_count;
			out->score[pi] = gSCManagerBattleState->players[pi].score;
			out->falls[pi] = gSCManagerBattleState->players[pi].falls;
			out->stale_id[pi] = gSCManagerBattleState->players[pi].stale_id;
			out->hash_battle_players =
			    syNetSyncFnvAccumulateU32(out->hash_battle_players, (u32)out->stock_count[pi]);
			out->hash_battle_players =
			    syNetSyncFnvAccumulateU32(out->hash_battle_players, (u32)out->score[pi]);
			out->hash_battle_players =
			    syNetSyncFnvAccumulateU32(out->hash_battle_players, (u32)out->falls[pi]);
			out->hash_battle_players =
			    syNetSyncFnvAccumulateU32(out->hash_battle_players, out->stale_id[pi]);
		}
	}
	out->spawn_wait = (u32)gITManagerAppearActor.spawn_wait;
	out->appear_weights_sum = (u32)gITManagerAppearActor.weights.weights_sum;
	out->appear_valids_num = (u32)gITManagerAppearActor.weights.valids_num;
	out->mapobjs_num = (u32)gITManagerAppearActor.mapobjs_num;
	out->random_weights_sum = (u32)gITManagerRandomWeights.weights_sum;
	out->random_valids_num = (u32)gITManagerRandomWeights.valids_num;
	out->hash_spawn_wait = syNetSyncFnvAccumulateU32(2166136261U, out->spawn_wait);
	mapobjs_num = out->mapobjs_num;
	if (mapobjs_num > nITKindEnumCount)
	{
		mapobjs_num = nITKindEnumCount;
	}
	out->mapobjs_hash = syNetSyncHashU8Array(gITManagerAppearActor.mapobjs, mapobjs_num);
	random_valids_num = out->random_valids_num;
	if (random_valids_num > nITKindEnumCount)
	{
		random_valids_num = nITKindEnumCount;
	}
	out->random_kinds_hash = syNetSyncHashU8Array(gITManagerRandomWeights.kinds, random_valids_num);
	out->random_blocks_hash = syNetSyncHashU16Array(gITManagerRandomWeights.blocks, random_valids_num);
	out->hash_appear_tables = 2166136261U;
	out->hash_appear_tables =
	    syNetSyncFnvAccumulateU32(out->hash_appear_tables, out->appear_weights_sum);
	out->hash_appear_tables =
	    syNetSyncFnvAccumulateU32(out->hash_appear_tables, out->appear_valids_num);
	out->hash_appear_tables = syNetSyncFnvAccumulateU32(out->hash_appear_tables, out->mapobjs_num);
	n = mapobjs_num;
	if ((gITManagerAppearActor.mapobjs != NULL) && (n > 0))
	{
		for (i = 0; i < n; i++)
		{
			out->hash_appear_tables =
			    syNetSyncFnvAccumulateU32(out->hash_appear_tables,
						      (u32)gITManagerAppearActor.mapobjs[i]);
		}
	}
	out->hash_random_tables = 2166136261U;
	out->hash_random_tables =
	    syNetSyncFnvAccumulateU32(out->hash_random_tables, out->random_weights_sum);
	out->hash_random_tables =
	    syNetSyncFnvAccumulateU32(out->hash_random_tables, out->random_valids_num);
	n = random_valids_num;
	if ((gITManagerRandomWeights.kinds != NULL) && (gITManagerRandomWeights.blocks != NULL) && (n > 0))
	{
		for (i = 0; i < n; i++)
		{
			out->hash_random_tables =
			    syNetSyncFnvAccumulateU32(out->hash_random_tables,
						      (u32)gITManagerRandomWeights.kinds[i]);
			out->hash_random_tables =
			    syNetSyncFnvAccumulateU32(out->hash_random_tables,
						      (u32)gITManagerRandomWeights.blocks[i]);
		}
	}
	out->hash_combined = syNetSyncHashRollbackWorld();
}

static void syNetSyncLogWorldScalarDiff(const char *tag, u32 tick, const char *field, s32 peer_v, s32 local_v)
{
	if (peer_v != local_v)
	{
		port_log("SSB64 NetSync: PEER_DIVERGE_DIFF tag=%s tick=%u field=%s peer=%d local=%d\n", tag, tick, field,
			 (int)peer_v, (int)local_v);
	}
}

static void syNetSyncLogWorldU32Diff(const char *tag, u32 tick, const char *field, u32 peer_v, u32 local_v)
{
	if (peer_v != local_v)
	{
		port_log("SSB64 NetSync: PEER_DIVERGE_DIFF tag=%s tick=%u field=%s peer=%u local=%u\n", tag, tick, field,
			 (unsigned int)peer_v, (unsigned int)local_v);
	}
}

void syNetSyncLogWorldHashDiff(const char *tag, u32 tick, const SYNetSyncRollbackWorldComponents *peer,
			       const SYNetSyncRollbackWorldComponents *local)
{
	s32 pi;

	if ((local == NULL) || (syNetSyncPeerDivergeDetailEnabled() == FALSE))
	{
		return;
	}
	if (peer == NULL)
	{
		port_log(
		    "SSB64 NetSync: PEER_DIVERGE_DIFF tag=%s tick=%u world_local_breakdown time_remain=%d time_passed=%d status=%d spawn_wait=%u appear_sum=%u appear_valid=%u mapobjs=%u map_hash=0x%08X random_sum=%u random_valid=%u random_kinds=0x%08X random_blocks=0x%08X hash_time=0x%08X hash_players=0x%08X hash_spawn=0x%08X hash_appear=0x%08X hash_random=0x%08X world=0x%08X\n",
		    tag,
		    tick,
		    (int)local->time_remain,
		    (int)local->time_passed,
		    (int)local->game_status,
		    (unsigned int)local->spawn_wait,
		    (unsigned int)local->appear_weights_sum,
		    (unsigned int)local->appear_valids_num,
		    (unsigned int)local->mapobjs_num,
		    local->mapobjs_hash,
		    (unsigned int)local->random_weights_sum,
		    (unsigned int)local->random_valids_num,
		    local->random_kinds_hash,
		    local->random_blocks_hash,
		    local->hash_battle_time,
		    local->hash_battle_players,
		    local->hash_spawn_wait,
		    local->hash_appear_tables,
		    local->hash_random_tables,
		    local->hash_combined);
		for (pi = 0; pi < GMCOMMON_PLAYERS_MAX; pi++)
		{
			port_log(
			    "SSB64 NetSync: PEER_DIVERGE_DIFF tag=%s tick=%u player=%d stock=%d score=%d falls=%d stale_id=%u\n",
			    tag,
			    tick,
			    (int)pi,
			    (int)local->stock_count[pi],
			    (int)local->score[pi],
			    (int)local->falls[pi],
			    (unsigned int)local->stale_id[pi]);
		}
		return;
	}
	if (peer->hash_battle_time != local->hash_battle_time)
	{
		port_log("SSB64 NetSync: PEER_DIVERGE_DIFF tag=%s tick=%u partition=battle_time peer=0x%08X local=0x%08X\n",
			 tag,
			 tick,
			 peer->hash_battle_time,
			 local->hash_battle_time);
		syNetSyncLogWorldScalarDiff(tag, tick, "time_remain", peer->time_remain, local->time_remain);
		syNetSyncLogWorldScalarDiff(tag, tick, "time_passed", peer->time_passed, local->time_passed);
		syNetSyncLogWorldScalarDiff(tag, tick, "game_status", peer->game_status, local->game_status);
	}
	if (peer->hash_battle_players != local->hash_battle_players)
	{
		port_log(
		    "SSB64 NetSync: PEER_DIVERGE_DIFF tag=%s tick=%u partition=battle_players peer=0x%08X local=0x%08X\n",
		    tag,
		    tick,
		    peer->hash_battle_players,
		    local->hash_battle_players);
		for (pi = 0; pi < GMCOMMON_PLAYERS_MAX; pi++)
		{
			syNetSyncLogWorldScalarDiff(tag, tick, "stock_count", peer->stock_count[pi], local->stock_count[pi]);
			syNetSyncLogWorldScalarDiff(tag, tick, "score", peer->score[pi], local->score[pi]);
			syNetSyncLogWorldScalarDiff(tag, tick, "falls", peer->falls[pi], local->falls[pi]);
			if (peer->stale_id[pi] != local->stale_id[pi])
			{
				port_log(
				    "SSB64 NetSync: PEER_DIVERGE_DIFF tag=%s tick=%u field=stale_id player=%d peer=%u local=%u\n",
				    tag,
				    tick,
				    (int)pi,
				    (unsigned int)peer->stale_id[pi],
				    (unsigned int)local->stale_id[pi]);
			}
		}
	}
	if (peer->hash_spawn_wait != local->hash_spawn_wait)
	{
		port_log("SSB64 NetSync: PEER_DIVERGE_DIFF tag=%s tick=%u partition=spawn_wait peer=0x%08X local=0x%08X\n",
			 tag,
			 tick,
			 peer->hash_spawn_wait,
			 local->hash_spawn_wait);
		syNetSyncLogWorldU32Diff(tag, tick, "spawn_wait", peer->spawn_wait, local->spawn_wait);
	}
	if (peer->hash_appear_tables != local->hash_appear_tables)
	{
		port_log(
		    "SSB64 NetSync: PEER_DIVERGE_DIFF tag=%s tick=%u partition=appear_tables peer=0x%08X local=0x%08X\n",
		    tag,
		    tick,
		    peer->hash_appear_tables,
		    local->hash_appear_tables);
		syNetSyncLogWorldU32Diff(tag, tick, "appear_weights_sum", peer->appear_weights_sum,
					 local->appear_weights_sum);
		syNetSyncLogWorldU32Diff(tag, tick, "appear_valids_num", peer->appear_valids_num, local->appear_valids_num);
		syNetSyncLogWorldU32Diff(tag, tick, "mapobjs_num", peer->mapobjs_num, local->mapobjs_num);
		if (peer->mapobjs_hash != local->mapobjs_hash)
		{
			port_log(
			    "SSB64 NetSync: PEER_DIVERGE_DIFF tag=%s tick=%u field=mapobjs_hash peer=0x%08X local=0x%08X\n",
			    tag,
			    tick,
			    peer->mapobjs_hash,
			    local->mapobjs_hash);
		}
	}
	if (peer->hash_random_tables != local->hash_random_tables)
	{
		port_log(
		    "SSB64 NetSync: PEER_DIVERGE_DIFF tag=%s tick=%u partition=random_tables peer=0x%08X local=0x%08X\n",
		    tag,
		    tick,
		    peer->hash_random_tables,
		    local->hash_random_tables);
		syNetSyncLogWorldU32Diff(tag, tick, "random_weights_sum", peer->random_weights_sum,
					 local->random_weights_sum);
		syNetSyncLogWorldU32Diff(tag, tick, "random_valids_num", peer->random_valids_num,
					 local->random_valids_num);
		if (peer->random_kinds_hash != local->random_kinds_hash)
		{
			port_log(
			    "SSB64 NetSync: PEER_DIVERGE_DIFF tag=%s tick=%u field=random_kinds_hash peer=0x%08X local=0x%08X\n",
			    tag,
			    tick,
			    peer->random_kinds_hash,
			    local->random_kinds_hash);
		}
		if (peer->random_blocks_hash != local->random_blocks_hash)
		{
			port_log(
			    "SSB64 NetSync: PEER_DIVERGE_DIFF tag=%s tick=%u field=random_blocks_hash peer=0x%08X local=0x%08X\n",
			    tag,
			    tick,
			    peer->random_blocks_hash,
			    local->random_blocks_hash);
		}
	}
}

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

void syNetSyncLogFighterJointTranslateTrace(u32 tick)
{
	GObj *fighter_gobj;
	s32 ji;

	if (syNetSyncJointTranslateTraceEnabled() == FALSE)
	{
		return;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);
		DObj *joint;
		Vec3f tr;

		if (fp == NULL)
		{
			continue;
		}
		if ((sSYNetSyncJointTranslateTraceSlotFilter >= 0) &&
		    (fp->player != sSYNetSyncJointTranslateTraceSlotFilter))
		{
			continue;
		}
		if ((sSYNetSyncJointTranslateTraceFkindFilter >= 0) &&
		    (fp->fkind != sSYNetSyncJointTranslateTraceFkindFilter))
		{
			continue;
		}
		for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
		{
			joint = fp->joints[ji];
			if (joint == NULL)
			{
				continue;
			}
			tr = joint->translate.vec.f;
			port_log(
			    "SSB64 NetSync: joint_translate tick=%u slot=%d fkind=%d ji=%d "
			    "tx=0x%08X ty=0x%08X tz=0x%08X anim_frame=0x%08X anim_wait=0x%08X "
			    "motion_attack_id=%d hitstatus=%d inv=%d is_shield=%u is_hitstun=%u\n",
			    tick,
			    (int)fp->player,
			    (int)fp->fkind,
			    ji,
			    syNetSyncHashF32(tr.x),
			    syNetSyncHashF32(tr.y),
			    syNetSyncHashF32(tr.z),
			    syNetSyncHashF32(joint->anim_frame),
			    syNetSyncHashF32(joint->anim_wait),
			    (int)fp->motion_attack_id,
			    (int)fp->hitstatus,
			    (int)fp->invincible_tics,
			    (unsigned int)(fp->is_shield != FALSE),
			    (unsigned int)(fp->is_hitstun != FALSE));
		}
	}
}

void syNetSyncJointTranslateTraceOnFighStep(u32 tick, u32 figh)
{
	if (syNetSyncJointTranslateTraceEnabled() == FALSE)
	{
		return;
	}
	if (sSYNetSyncJointTranslateTraceTriggerFired == FALSE)
	{
		if ((sSYNetSyncJointTranslateTracePrevTick != ~(u32)0) && (figh != sSYNetSyncJointTranslateTracePrevFigh))
		{
			port_log(
			    "SSB64 NetSync: joint_translate_trigger tick=%u prev_tick=%u figh_old=0x%08X figh_new=0x%08X\n",
			    tick,
			    sSYNetSyncJointTranslateTracePrevTick,
			    sSYNetSyncJointTranslateTracePrevFigh,
			    figh);
			sSYNetSyncJointTranslateTraceTriggerFired = TRUE;
		}
	}
	syNetSyncLogFighterJointTranslateTrace(tick);
	sSYNetSyncJointTranslateTracePrevTick = tick;
	sSYNetSyncJointTranslateTracePrevFigh = figh;
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

static u32 syNetSyncFoldActiveItemGobj(GObj *gobj)
{
	DObj *dobj;
	ITStruct *ip;
	u32 fold;

	ip = itGetStruct(gobj);
	if (ip == NULL)
	{
		return 0U;
	}
	fold = 2166136261U;
	fold = syNetSyncFnvAccumulateU32(fold, gobj->id);
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
	return fold;
}

#define SYNET_SYNC_ITEM_HASH_SORT_MAX 48U

u32 syNetSyncHashActiveItemsForRollback(void)
{
	GObj *sorted[SYNET_SYNC_ITEM_HASH_SORT_MAX];
	GObj *gobj;
	u32 count;
	u32 hash;
	u32 i;
	u32 j;

	count = 0U;
	for (gobj = gGCCommonLinks[nGCCommonLinkIDItem]; gobj != NULL; gobj = gobj->link_next)
	{
		if (itGetStruct(gobj) == NULL)
		{
			continue;
		}
		if (count < SYNET_SYNC_ITEM_HASH_SORT_MAX)
		{
			sorted[count++] = gobj;
		}
	}
	for (i = 1U; i < count; i++)
	{
		GObj *key_gobj;
		u32 key_id;

		key_gobj = sorted[i];
		key_id = key_gobj->id;
		j = i;
		while ((j > 0U) && (sorted[j - 1U]->id > key_id))
		{
			sorted[j] = sorted[j - 1U];
			j--;
		}
		sorted[j] = key_gobj;
	}
	hash = 2166136261U;
	for (i = 0U; i < count; i++)
	{
		u32 fold;

		fold = syNetSyncFoldActiveItemGobj(sorted[i]);
		if (fold == 0U)
		{
			continue;
		}
		hash ^= fold;
		hash = syNetSyncFnvAccumulateU32(hash, 0xA5A5A5A5U);
	}
	return hash;
}

#ifdef PORT
extern char *getenv(const char *name);
extern int atoi(const char *s);

static sb32 syNetSyncItemHashTraceEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_ITEM_HASH_TRACE");
	s_env_cache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}

void syNetSyncLogItemHashWalkTrace(u32 sim_tick)
{
	GObj *sorted[SYNET_SYNC_ITEM_HASH_SORT_MAX];
	GObj *gobj;
	u32 count;
	u32 hash;
	u32 idx;
	u32 i;
	u32 j;

	if (syNetSyncItemHashTraceEnabled() == FALSE)
	{
		return;
	}
	count = 0U;
	for (gobj = gGCCommonLinks[nGCCommonLinkIDItem]; gobj != NULL; gobj = gobj->link_next)
	{
		if (itGetStruct(gobj) == NULL)
		{
			continue;
		}
		if (count < SYNET_SYNC_ITEM_HASH_SORT_MAX)
		{
			sorted[count++] = gobj;
		}
	}
	for (i = 1U; i < count; i++)
	{
		GObj *key_gobj;
		u32 key_id;

		key_gobj = sorted[i];
		key_id = key_gobj->id;
		j = i;
		while ((j > 0U) && (sorted[j - 1U]->id > key_id))
		{
			sorted[j] = sorted[j - 1U];
			j--;
		}
		sorted[j] = key_gobj;
	}
	hash = 2166136261U;
	idx = 0U;
	port_log("SSB64 NetSync: item_hash_walk begin sim_tick=%u live_sim=%u\n", sim_tick,
	         (unsigned int)syNetInputGetTick());
	for (i = 0U; i < count; i++)
	{
		ITStruct *ip;
		u32 fold;

		gobj = sorted[i];
		ip = itGetStruct(gobj);
		if (ip == NULL)
		{
			continue;
		}
		fold = syNetSyncFoldActiveItemGobj(gobj);
		hash ^= fold;
		hash = syNetSyncFnvAccumulateU32(hash, 0xA5A5A5A5U);
		port_log(
		    "SSB64 NetSync: item_hash_walk step=%u gobj_id=%u kind=%d type=%d fold=0x%08X hash=0x%08X\n",
		    idx,
		    (unsigned int)gobj->id,
		    (int)ip->kind,
		    (int)ip->type,
		    fold,
		    hash);
		idx++;
	}
	port_log("SSB64 NetSync: item_hash_walk end sim_tick=%u count=%u hash=0x%08X\n", sim_tick, idx, hash);
}
#endif

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
