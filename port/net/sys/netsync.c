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

u32 syNetSyncHashRollbackWorld(void)
{
	u32 hash = 2166136261U;
	s32 pi;

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
	return hash;
}

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
				fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(fp->joints[ji]->anim_frame));
				fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(fp->joints[ji]->anim_wait));
				fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(fp->joints[ji]->anim_speed));
			}
		}
		hash ^= fold;
		hash = syNetSyncFnvAccumulateU32(hash, (u32)fp->player);
	}
	return hash;
}
