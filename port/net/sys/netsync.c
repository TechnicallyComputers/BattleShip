#include <sys/netsync.h>

#include <sys/netrollbacksnapshot.h>

#include <ft/fighter.h>
#include <ft/ftdef.h>
#include <ft/ftchar/ftness/ftness.h>
#if defined(PORT) && defined(SSB64_NETMENU)
#include <ft/ftchar/ftkirby/ftkirby.h>
#endif
#include <gm/gmdef.h>
#include <gm/gmcamera.h>
#include <it/item.h>
#include <mp/map.h>
#include <sc/scmanager.h>
#include <sc/sctypes.h>
#include <sys/objdef.h>
#include <sys/objman.h>
#include <sys/objman_gcport.h>
#include <sys/objanim.h>
#include <sys/utils.h>
#include <wp/weapon.h>
#include <wp/wpdef.h>

#ifdef PORT
#include <ef/effect.h>
#include <ef/efmanager.h>
#include <gr/grdef.h>
#include <gr/grvars.h>
#include <gr/ground.h>
#include <gr/grcommon/gryoster.h>
#include <gr/grcommon/grsector.h>
#include <mp/map.h>
#include <mp/mpcollision.h>
#include <it/itfighter/itlinkbomb.h>
#include <sys/objtypes.h>
#endif

#include <it/itmanager.h>

#ifdef PORT
#include <sys/netinput.h>
#include <sys/netpeer.h>
#include <sys/netplay_sim_quantize.h>
#include <sys/netrollback.h>

#include <if/ifcommon.h>
#include <macros.h>
#include <sc/scdef.h>

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

extern void port_log(const char *fmt, ...);

static u32 sSYNetSyncBattleGoSimTick = ~(u32)0;
static sb32 sSYNetSyncItemHashTruncationLogged = FALSE;
static sb32 sSYNetSyncTimeUpTriggered = FALSE;
static sb32 sSYNetSyncBattleGoPending = FALSE;

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

#if defined(SSB64_NETMENU)
	reinterpret.fv = syNetplayQuantizeF32(value);
#else
	reinterpret.fv = value;
#endif

	return reinterpret.uv;
}

#if defined(SSB64_NETMENU)
static u32 syNetSyncHashF32ForRollback(f32 value)
{
	union SYNetSyncF32Reinterpret
	{
		f32 fv;
		u32 uv;

	} reinterpret;

	reinterpret.fv = syNetplayQuantizeF32ForRollbackHash(value);
	return reinterpret.uv;
}
#endif

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
	/*
	 * Cover scalars that the fighter sim state-machine reads but the original
	 * fhash_light fold missed. These were the exact fields that let the Mario
	 * WalkMiddle→KneeBend desync from session 4 (tick 519) and session 5
	 * (tick 577) slip past every FC checkpoint silently:
	 *
	 *   - tap_stick_x/y, hold_stick_x/y  drive smash / tap-jump / dash
	 *     detection (ftCommonKneeBendGetInputType*, ftCommonAttack4Check*,
	 *     ftCommonDamageSmashDI*). Without them in the fold, two peers with
	 *     diverged tap-frame counters hash identically.
	 *   - coll_data.pos  is read every tick by ground-collision / camera /
	 *     state-machine checks. Only pos_prev was hashed before, so a
	 *     1-tick-late drift in current pos was undetectable.
	 *   - anim_vel  is added into physics each tick and modifies grounded
	 *     position; an unhashed delta translates to silent positional drift.
	 *
	 * (See docs/bugs/netplay_tap_jump_local_cvar_desync_2026-05-25.md.)
	 */
	h = syNetSyncFnvAccumulateU32(h, (u32)fp->tap_stick_x);
	h = syNetSyncFnvAccumulateU32(h, (u32)fp->tap_stick_y);
	h = syNetSyncFnvAccumulateU32(h, (u32)fp->hold_stick_x);
	h = syNetSyncFnvAccumulateU32(h, (u32)fp->hold_stick_y);
	/*
	 * MPCollData has no .pos — the live world position is the indirected
	 * `*p_translate` vector (the fighter's TopN joint translation). Fold both
	 * pos_diff (per-tick movement delta) and the indirected p_translate so any
	 * sim/physics step divergence shows up in fhash_light immediately.
	 */
	h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->coll_data.pos_diff.x));
	h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->coll_data.pos_diff.y));
	h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->coll_data.pos_diff.z));
	if (fp->coll_data.p_translate != NULL)
	{
		h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->coll_data.p_translate->x));
		h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->coll_data.p_translate->y));
		h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->coll_data.p_translate->z));
	}
	h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->anim_vel.x));
	h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->anim_vel.y));
	h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->anim_vel.z));
	if (fp->status_id == nFTCommonStatusTwister)
	{
		GObj *tornado_gobj = ftStatusVarsTwister(fp)->tornado_gobj;
		DObj *tornado_dobj;

		h = syNetSyncFnvAccumulateU32(h, (u32)ftStatusVarsTwister(fp)->release_wait);
		h = syNetSyncFnvAccumulateU32(h, (tornado_gobj != NULL) ? (u32)tornado_gobj->id : 0U);
		h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->physics.vel_air.x));
		h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->physics.vel_air.y));
		h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->physics.vel_air.z));
		tornado_dobj = (tornado_gobj != NULL) ? DObjGetStruct(tornado_gobj) : NULL;
		if (tornado_dobj != NULL)
		{
			h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(tornado_dobj->translate.vec.f.x));
			h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(tornado_dobj->translate.vec.f.y));
			h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(tornado_dobj->translate.vec.f.z));
		}
	}
	if (fp->status_id == nFTCommonStatusTaruCann)
	{
		h = syNetSyncFnvAccumulateU32(h, (u32)ftStatusVarsTaruCann(fp)->release_wait);
		h = syNetSyncFnvAccumulateU32(h, (u32)ftStatusVarsTaruCann(fp)->shoot_wait);
	}
	if ((fp->status_id == nFTCommonStatusCaptureYoshi) || (fp->status_id == nFTCommonStatusYoshiEgg))
	{
		h = syNetSyncFnvAccumulateU32(h, (u32)fp->breakout_wait);
		h = syNetSyncFnvAccumulateU32(h, (u32)(u8)fp->breakout_lr);
		h = syNetSyncFnvAccumulateU32(h, (u32)(u8)fp->breakout_ud);
		h = syNetSyncFnvAccumulateU32(h, (u32)fp->motion_vars.flags.flag0);
		h = syNetSyncFnvAccumulateU32(h, (u32)ftStatusVarsCaptureYoshi(fp)->stage);
		h = syNetSyncFnvAccumulateU32(h, (u32)(u16)ftStatusVarsCaptureYoshi(fp)->breakout_wait);
		h = syNetSyncFnvAccumulateU32(h, (u32)(u8)ftStatusVarsCaptureYoshi(fp)->lr);
		h = syNetSyncFnvAccumulateU32(h, (u32)(ftStatusVarsCaptureYoshi(fp)->is_damagefloor != FALSE));
#ifdef PORT
		h = syNetSyncFnvAccumulateU32(h, syNetRbSnapHashCaptureYoshiEffectGobjId(fp));
#else
		h = syNetSyncFnvAccumulateU32(h, 0U);
#endif
	}
#if defined(PORT) && defined(SSB64_NETMENU)
	if ((fp->fkind == nFTKindKirby) && (fp->status_id >= nFTKirbyStatusSpecialLwStart) &&
	    (fp->status_id <= nFTKirbyStatusSpecialAirLwEnd))
	{
		h = syNetSyncFnvAccumulateU32(h, (u32)fp->status_vars.kirby.speciallw.duration);
		h = syNetSyncFnvAccumulateU32(h, (u32)(fp->is_damage_resist != FALSE));
		h = syNetSyncFnvAccumulateU32(h, (u32)fp->damage_resist);
	}
#endif
#if defined(PORT) && defined(SSB64_NETMENU)
	if ((gSCManagerBattleState != NULL) && (gSCManagerBattleState->gkind == nGRKindSector) &&
	    (gGRCommonStruct.sector.is_arwing_line_active != FALSE) &&
	    (gGRCommonStruct.sector.is_arwing_z_near != FALSE) && (fp->coll_data.floor_line_id == 1))
	{
		h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->coll_data.vel_speed.x));
		h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->coll_data.vel_speed.y));
		h = syNetSyncFnvAccumulateU32(h, syNetSyncHashF32(fp->coll_data.vel_speed.z));
		h = syNetSyncFnvAccumulateU32(h, (u32)fp->coll_data.floor_flags);
	}
#endif
	return h;
}

static u32 syNetSyncFoldFighterSlotFullContribution(const FTStruct *fp)
{
	u32 contribution;
	s32 ji;

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
			/* Fold joint rotate so the oracle catches facing desyncs (joints[TopN]->rotate.y == fp->lr*90deg
			   facing). Rotate is quantized in-sim every frame by gcPlayDObjAnimJoint's
			   syNetplayQuantizeDObjAnimPose hook (and again here), so this is cross-ISA safe like translate. */
			contribution =
			    syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->joints[ji]->rotate.vec.f.x));
			contribution =
			    syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->joints[ji]->rotate.vec.f.y));
			contribution =
			    syNetSyncFnvAccumulateU32(contribution, syNetSyncHashF32(fp->joints[ji]->rotate.vec.f.z));
		}
	}
	return contribution;
}

static u32 syNetSyncFoldFighterAnimJointContribution(DObj *joint, u32 fold)
{
	AObj *aobj;
	u32 aobj_steps;
	u32 chain_total;

	if (joint == NULL)
	{
		return fold;
	}
	fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(joint->anim_frame));
	fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(joint->anim_wait));
	fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(joint->anim_speed));
	chain_total = 0U;
	for (aobj = joint->aobj; aobj != NULL; aobj = aobj->next)
	{
		chain_total++;
	}
	fold = syNetSyncFnvAccumulateU32(fold, chain_total);
	for (aobj = joint->aobj, aobj_steps = 0U;
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
	return fold;
}

static u32 syNetSyncFoldFighterAnimRollback(const FTStruct *fp, GObj *fighter_gobj)
{
	u32 fold;
	s32 ji;

	if ((fp == NULL) || (fighter_gobj == NULL))
	{
		return 2166136261U;
	}
	fold = 2166136261U;
	fold = syNetSyncFnvAccumulateU32(fold, (u32)fp->status_id);
	fold = syNetSyncFnvAccumulateU32(fold, (u32)fp->motion_id);
	fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(fighter_gobj->anim_frame));
	for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
	{
		fold = syNetSyncFoldFighterAnimJointContribution(fp->joints[ji], fold);
	}
	return fold;
}

static u32 syNetSyncFoldAttackRecordSlots(const GMAttackRecord *records, s32 count, u32 fold)
{
	s32 i;

	if (records == NULL)
	{
		return fold;
	}
	for (i = 0; i < count; i++)
	{
		const GMAttackRecord *rec = &records[i];

		fold = syNetSyncFnvAccumulateU32(fold, (rec->victim_gobj != NULL) ? (u32)rec->victim_gobj->id : 0U);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)rec->victim_flags.is_interact_hurt);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)rec->victim_flags.is_interact_shield);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)rec->victim_flags.is_interact_reflect);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)rec->victim_flags.group_id);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)rec->victim_flags.timer_rehit);
	}
	return fold;
}

u32 syNetSyncHashFighterSlotFull(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return 2166136261U;
	}
	return syNetSyncFoldFighterSlotFullContribution(fp);
}

u32 syNetSyncHashFighterSlotAnim(const FTStruct *fp, GObj *fighter_gobj)
{
	return syNetSyncFoldFighterAnimRollback(fp, fighter_gobj);
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

#define SYNETSYNC_MAX_MP_YAKU SYNETRB_SNAPSHOT_MAX_YAKU

/*
 * Sample up to SYNETSYNC_MAX_MP_YAKU yakumono kinematic entries (stage moving pieces / hazards).
 * Intended as a canary for “map half of sim diverged”; not a full world hash.
 */
static u32 syNetSyncFoldMpCollisionBounds(void)
{
	u32 hash;
	const MPBounds *b;

	hash = 2166136261U;
	b = &gMPCollisionBounds.current;
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(b->top));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(b->bottom));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(b->left));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(b->right));
	b = &gMPCollisionBounds.diff;
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(b->top));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(b->bottom));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(b->left));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(b->right));
	return hash;
}

#if defined(SSB64_NETMENU)
static u32 syNetSyncFoldMpCollisionBoundsForRollback(void)
{
	u32 hash;
	const MPBounds *b;

	hash = 2166136261U;
	b = &gMPCollisionBounds.current;
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32ForRollback(b->top));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32ForRollback(b->bottom));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32ForRollback(b->left));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32ForRollback(b->right));
	b = &gMPCollisionBounds.diff;
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32ForRollback(b->top));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32ForRollback(b->bottom));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32ForRollback(b->left));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32ForRollback(b->right));
	return hash;
}
#endif

static u32 syNetSyncHashMapCollisionKinematicsCore(sb32 for_rollback_hash)
{
	u32 hash;
	s32 i;
	s32 n;
	s32 cap;
	DObj *dobj;

	hash = 2166136261U;
	hash = syNetSyncFnvAccumulateU32(hash, (u32)gMPCollisionUpdateTic);
#if defined(SSB64_NETMENU)
	if (for_rollback_hash != FALSE)
	{
		hash = syNetSyncFnvAccumulateU32(hash, syNetSyncFoldMpCollisionBoundsForRollback());
	}
	else
#endif
	{
		hash = syNetSyncFnvAccumulateU32(hash, syNetSyncFoldMpCollisionBounds());
	}
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
#if defined(SSB64_NETMENU)
		if (for_rollback_hash != FALSE)
		{
			hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32ForRollback(dobj->translate.vec.f.x));
			hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32ForRollback(dobj->translate.vec.f.y));
			hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32ForRollback(dobj->translate.vec.f.z));
			hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32ForRollback(gMPCollisionSpeeds[i].x));
			hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32ForRollback(gMPCollisionSpeeds[i].y));
			hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32ForRollback(gMPCollisionSpeeds[i].z));
		}
		else
#endif
		{
			hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(dobj->translate.vec.f.x));
			hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(dobj->translate.vec.f.y));
			hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(dobj->translate.vec.f.z));
			hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(gMPCollisionSpeeds[i].x));
			hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(gMPCollisionSpeeds[i].y));
			hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(gMPCollisionSpeeds[i].z));
		}
	}
	return hash;
}

u32 syNetSyncHashMapCollisionKinematics(void)
{
	return syNetSyncHashMapCollisionKinematicsCore(FALSE);
}

u32 syNetSyncHashMapCollisionKinematicsForRollback(void)
{
#if defined(SSB64_NETMENU)
	return syNetSyncHashMapCollisionKinematicsCore(TRUE);
#else
	return syNetSyncHashMapCollisionKinematicsCore(FALSE);
#endif
}

u32 syNetSyncHashGcRunAllTraversalFingerprint(void)
{
	return gcPortHashGcRunAllTraversalFingerprint();
}

#ifdef PORT
static sb32 syNetSyncHashGobjTranslateEnabled(void);
static u32 syNetSyncHashFighterGobjTranslate(const GObj *fighter_gobj);
#endif

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
		contribution = syNetSyncFoldFighterSlotFullContribution(fp);
		if (syNetSyncHashGobjTranslateEnabled() != FALSE)
		{
			contribution = syNetSyncFnvAccumulateU32(contribution, syNetSyncHashFighterGobjTranslate(fighter_gobj));
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
u32 syNetSyncNetplayEffectiveTimeLimitMinutes(void)
{
	if (gSCManagerBattleState == NULL)
	{
		return 0U;
	}
	return (u32)gSCManagerBattleState->time_limit;
}

void syNetSyncResetNetplayBattleClock(void)
{
	sSYNetSyncBattleGoSimTick = ~(u32)0;
	sSYNetSyncTimeUpTriggered = FALSE;
	sSYNetSyncBattleGoPending = FALSE;
	syNetSyncResetJointTranslateTraceSession();
	syNetSyncResetFhashLightMismatchTriggerSession();
}

void syNetSyncOnNetplayBattleGo(void)
{
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	/*
	 * Defer anchor to reconcile-time so we bind GO to the first accepted live sim step
	 * (never to a pre-GO / load / staging tick).
	 */
	sSYNetSyncBattleGoPending = TRUE;
	sSYNetSyncItemHashTruncationLogged = FALSE;
	sSYNetSyncTimeUpTriggered = FALSE;
}

static void syNetSyncReconcileBattleTimePassedCore(u32 sim_tick)
{
	u32 derived;
	u32 limit_min;
	u32 limit_tics;

	if ((gSCManagerBattleState == NULL) || (syNetPeerIsVSSessionActive() == FALSE))
	{
		return;
	}
	if (gSCManagerBattleState->game_status != nSCBattleGameStatusGo)
	{
		return;
	}
	/* Countdown starts only after GO (ifCommonAnnounceGoSetStatus → syNetSyncOnNetplayBattleGo). */
	if ((sSYNetSyncBattleGoPending != FALSE) ||
	    ((sSYNetSyncBattleGoSimTick == ~(u32)0) && (syNetRollbackIsResimulating() == FALSE)))
	{
		sSYNetSyncBattleGoSimTick = sim_tick;
		sSYNetSyncBattleGoPending = FALSE;
	}
	if (sSYNetSyncBattleGoSimTick == ~(u32)0)
	{
		return;
	}
	derived = (sim_tick > sSYNetSyncBattleGoSimTick) ? (sim_tick - sSYNetSyncBattleGoSimTick) : 0U;
	gSCManagerBattleState->time_passed = derived;

	if (ifCommonBattleUsesTimedStockLimit() == FALSE)
	{
		return;
	}
	limit_min = syNetSyncNetplayEffectiveTimeLimitMinutes();
	if ((limit_min == 0U) || (limit_min == (u32)SCBATTLE_TIMELIMIT_INFINITE))
	{
		return;
	}
	limit_tics = (u32)I_MIN_TO_TICS((s32)limit_min);
	if (limit_tics == 0U)
	{
		return;
	}
	if (derived >= limit_tics)
	{
		gSCManagerBattleState->time_remain = 0U;
		if ((gSCManagerBattleState->game_status == nSCBattleGameStatusGo) && (sSYNetSyncTimeUpTriggered == FALSE))
		{
			sSYNetSyncTimeUpTriggered = TRUE;
			ifCommonAnnounceTimeUpInitInterface();
		}
	}
	else
	{
		gSCManagerBattleState->time_remain = limit_tics - derived;
		sSYNetSyncTimeUpTriggered = FALSE;
	}
}

void syNetSyncReconcileBattleTimePassedForSimTick(u32 sim_tick)
{
	/* Live forward step only — not rollback load/resim replay of historical ticks. */
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	if (sim_tick != syNetInputGetTick())
	{
		return;
	}
	syNetSyncReconcileBattleTimePassedCore(sim_tick);
}

void syNetSyncReconcileBattleTimePassedForSnapshotSave(u32 completed_sim_tick)
{
	/* Persist clock fields for the tick being snapshotted; still skip during active resim. */
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	syNetSyncReconcileBattleTimePassedCore(completed_sim_tick);
}

void syNetSyncReconcileBattleTimePassedFromSimTick(void)
{
	syNetSyncReconcileBattleTimePassedForSimTick(syNetInputGetTick());
}
#endif

#ifdef PORT
static u32 syNetSyncFoldHyruleTwisterRollbackWorld(void)
{
	const GRCommonGroundVarsHyrule *hy;
	u32 hash;
	u32 gobj_id;

	if ((gSCManagerBattleState == NULL) || (gSCManagerBattleState->gkind != nGRKindHyrule))
	{
		return 2166136261U;
	}
	hy = &gGRCommonStruct.hyrule;
	hash = 2166136261U;
	hash = syNetSyncFnvAccumulateU32(hash, (u32)hy->twister_status);
	hash = syNetSyncFnvAccumulateU32(hash, (u32)hy->twister_wait);
	hash = syNetSyncFnvAccumulateU32(hash, (u32)hy->twister_speed_wait);
	hash = syNetSyncFnvAccumulateU32(hash, (u32)hy->twister_turn_wait);
	hash = syNetSyncFnvAccumulateU32(hash, (u32)hy->twister_line_id);
	gobj_id = (hy->twister_gobj != NULL) ? (u32)hy->twister_gobj->id : 0U;
	hash = syNetSyncFnvAccumulateU32(hash, gobj_id);
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(hy->twister_vel));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(hy->twister_leftedge_x));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(hy->twister_rightedge_x));
	if (hy->twister_gobj != NULL)
	{
		DObj *twister_dobj = DObjGetStruct(hy->twister_gobj);

		if (twister_dobj != NULL)
		{
			hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(twister_dobj->translate.vec.f.x));
			hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(twister_dobj->translate.vec.f.y));
			hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(twister_dobj->translate.vec.f.z));
		}
	}
	return hash;
}

/*
 * Yamabuki (Saffron) tower gate spawn timer + door collision were never folded into the cross-peer
 * world hash (unlike Hyrule). A divergent `monster_wait` (RNG drawn around the unsynced intro window)
 * then spawns the rooftop Pokemon at different ticks per peer and is only noticed once the item/eff
 * partitions drift. Fold the gate scalars so the first post-intro synctest catches the drift and
 * rolls back to re-converge the spawn schedule.
 */
static u32 syNetSyncFoldYamabukiGateRollbackWorld(void)
{
	const GRCommonGroundVarsYamabuki *ya;
	u32 hash;
	u32 gobj_id;
	f32 gate_anim_frame;
	f32 gate_anim_wait;

	if ((gSCManagerBattleState == NULL) || (gSCManagerBattleState->gkind != nGRKindYamabuki))
	{
		return 2166136261U;
	}
	ya = &gGRCommonStruct.yamabuki;
	gate_anim_frame = 0.0F;
	gate_anim_wait = 0.0F;
	if (ya->gate_gobj != NULL)
	{
		DObj *gate_dobj = DObjGetStruct(ya->gate_gobj);

		if (gate_dobj != NULL)
		{
			DObj *child = gate_dobj->child;

			if (child != NULL)
			{
				gate_anim_frame = child->anim_frame;
				gate_anim_wait = child->anim_wait;
			}
			else
			{
				gate_anim_frame = gate_dobj->anim_frame;
				gate_anim_wait = gate_dobj->anim_wait;
			}
		}
	}
#if defined(SSB64_NETMENU)
	gate_anim_frame = syNetplayQuantizeAnimScalar(gate_anim_frame);
	gate_anim_wait = syNetplayQuantizeAnimScalar(gate_anim_wait);
#endif
	hash = 2166136261U;
	hash = syNetSyncFnvAccumulateU32(hash, (u32)ya->gate_status);
	hash = syNetSyncFnvAccumulateU32(hash, (u32)ya->gate_noentry);
	hash = syNetSyncFnvAccumulateU32(hash, (u32)ya->monster_wait);
	hash = syNetSyncFnvAccumulateU32(hash, (u32)ya->gate_wait);
	hash = syNetSyncFnvAccumulateU32(hash, (u32)ya->monster_id_prev);
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(ya->gate_pos.x));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(ya->gate_pos.y));
	hash = syNetSyncFnvAccumulateU32(hash, (u32)ya->gate_anim_phase);
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(gate_anim_frame));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(gate_anim_wait));
	gobj_id = (ya->monster_gobj != NULL) ? (u32)ya->monster_gobj->id : 0U;
	hash = syNetSyncFnvAccumulateU32(hash, gobj_id);
	return hash;
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
#ifdef PORT
	{
		u32 hyrule_twister_hash = syNetSyncFoldHyruleTwisterRollbackWorld();
		u32 yamabuki_gate_hash = syNetSyncFoldYamabukiGateRollbackWorld();

		hash = syNetSyncFnvAccumulateU32(hash ^ hyrule_twister_hash, 0x48595255U);
		hash = syNetSyncFnvAccumulateU32(hash ^ yamabuki_gate_hash, 0x59414D42U);
	}
#endif
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

static sb32 sSYNetSyncFhashLightTriggerCache = -999;
static sb32 sSYNetSyncFhashLightTriggerFirstFired = FALSE;
static sb32 sSYNetSyncFhashLightTriggerSecondFired = FALSE;
static u32 sSYNetSyncFhashLightTriggerPrevTick = ~(u32)0;
static u32 sSYNetSyncFhashLightTriggerPrevFigh = 0U;
static u32 sSYNetSyncFhashLightTriggerPrevSlot[GMCOMMON_PLAYERS_MAX];
static int sSYNetSyncFhashLightTriggerSecondMinTick = -999999;

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

static sb32 syNetSyncHashGobjTranslateEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_HASH_GOBJ_TRANSLATE");
	s_env_cache = (syNetSyncEnvParseInt(e, 0) != 0) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}

static u32 syNetSyncHashFighterGobjTranslate(const GObj *fighter_gobj)
{
	const Vec3f *translate;

	if (fighter_gobj == NULL)
	{
		return 2166136261U;
	}
	translate = &DObjGetStruct(fighter_gobj)->translate.vec.f;
	return syNetSyncFnvAccumulateU32(
	    syNetSyncFnvAccumulateU32(syNetSyncHashF32(translate->x), syNetSyncHashF32(translate->y)),
	    syNetSyncHashF32(translate->z));
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

static sb32 syNetSyncFhashLightMismatchTriggerEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_FHASH_LIGHT_MISMATCH_TRIGGER");
	s_env_cache = ((e != NULL) && (e[0] != '\0') && (syNetSyncEnvParseInt(e, 0) != 0)) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}

static sb32 syNetSyncFhashLightMismatchTriggerTickInWindow(u32 tick)
{
	static int s_min_cache = -999999;
	static int s_max_cache = -999999;
	const char *e;
	s32 min_tick;
	s32 max_tick;

	if (s_min_cache == -999999)
	{
		min_tick = 0;
		max_tick = 0;
		e = getenv("SSB64_NETPLAY_FHASH_LIGHT_MISMATCH_TICK_MIN");
		if ((e != NULL) && (e[0] != '\0'))
		{
			min_tick = syNetSyncEnvParseInt(e, 0);
		}
		e = getenv("SSB64_NETPLAY_FHASH_LIGHT_MISMATCH_TICK_MAX");
		if ((e != NULL) && (e[0] != '\0'))
		{
			max_tick = syNetSyncEnvParseInt(e, 0);
		}
		if (max_tick <= 0)
		{
			s_min_cache = 0;
			s_max_cache = 0;
			return TRUE;
		}
		if (min_tick < 0)
		{
			min_tick = 0;
		}
		s_min_cache = min_tick;
		s_max_cache = max_tick;
	}
	if (s_max_cache == 0)
	{
		return TRUE;
	}
	return ((tick >= (u32)s_min_cache) && (tick <= (u32)s_max_cache)) ? TRUE : FALSE;
}

static s32 syNetSyncFhashLightMismatchTriggerSecondMinTick(void)
{
	const char *e;

	if (sSYNetSyncFhashLightTriggerSecondMinTick != -999999)
	{
		return sSYNetSyncFhashLightTriggerSecondMinTick;
	}
	e = getenv("SSB64_NETPLAY_FHASH_LIGHT_MISMATCH_TRIGGER_SECOND_MIN");
	if ((e != NULL) && (e[0] != '\0'))
	{
		sSYNetSyncFhashLightTriggerSecondMinTick = syNetSyncEnvParseInt(e, 473);
	}
	else
	{
		sSYNetSyncFhashLightTriggerSecondMinTick = 473;
	}
	return sSYNetSyncFhashLightTriggerSecondMinTick;
}

void syNetSyncResetFhashLightMismatchTriggerSession(void)
{
	sSYNetSyncFhashLightTriggerFirstFired = FALSE;
	sSYNetSyncFhashLightTriggerSecondFired = FALSE;
	sSYNetSyncFhashLightTriggerPrevTick = ~(u32)0;
	sSYNetSyncFhashLightTriggerPrevFigh = 0U;
}

static void syNetSyncFhashLightMismatchTriggerFire(u32 tick, u32 prev_tick, u32 cur_slot[GMCOMMON_PLAYERS_MAX],
						   const char *phase_tag)
{
	s32 si;

	for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
	{
		if (cur_slot[si] != sSYNetSyncFhashLightTriggerPrevSlot[si])
		{
			port_log(
			    "SSB64 NetSync: fhash_light_trigger phase=%s tick=%u prev_tick=%u player=%d fhash_light_old=0x%08X fhash_light_new=0x%08X\n",
			    phase_tag,
			    tick,
			    prev_tick,
			    (int)si,
			    sSYNetSyncFhashLightTriggerPrevSlot[si],
			    cur_slot[si]);
		}
	}
	syNetSyncLogFighterDetail("fhash_light_pre", prev_tick);
	syNetSyncLogFighterDetail("fhash_light_step", tick);
}

void syNetSyncFhashLightMismatchTriggerOnTick(u32 tick)
{
	u32 cur_slot[GMCOMMON_PLAYERS_MAX];
	u32 cur_figh;
	s32 si;
	s32 second_min;
	sb32 any_light_step;
	sb32 figh_step;
	sb32 want_first;
	sb32 want_second;

	if ((syNetSyncFhashLightMismatchTriggerEnabled() == FALSE) ||
	    (syNetSyncFhashLightMismatchTriggerTickInWindow(tick) == FALSE) || (syNetPeerIsVSSessionActive() == FALSE))
	{
		return;
	}
	if ((syNetRollbackIsResimulating() != FALSE) || (syNetRollbackGetAppliedResimCount() != 0U))
	{
		return;
	}
	if (tick == 0U)
	{
		return;
	}
	syNetSyncCollectFighterSlotHashes(cur_slot);
	cur_figh = syNetSyncHashBattleFighters();
	if ((sSYNetSyncFhashLightTriggerPrevTick != ~(u32)0) && (tick == (sSYNetSyncFhashLightTriggerPrevTick + 1U)))
	{
		any_light_step = FALSE;
		for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
		{
			if (cur_slot[si] != sSYNetSyncFhashLightTriggerPrevSlot[si])
			{
				any_light_step = TRUE;
				break;
			}
		}
		figh_step = (cur_figh != sSYNetSyncFhashLightTriggerPrevFigh) ? TRUE : FALSE;
		if ((any_light_step != FALSE) && (figh_step != FALSE))
		{
			second_min = syNetSyncFhashLightMismatchTriggerSecondMinTick();
			want_first = (sSYNetSyncFhashLightTriggerFirstFired == FALSE) ? TRUE : FALSE;
			want_second =
			    (want_first == FALSE) && (sSYNetSyncFhashLightTriggerSecondFired == FALSE) &&
			    ((s32)tick >= second_min) ? TRUE : FALSE;
			if ((want_first != FALSE) || (want_second != FALSE))
			{
				syNetSyncFhashLightMismatchTriggerFire(
				    tick, sSYNetSyncFhashLightTriggerPrevTick, cur_slot,
				    (want_first != FALSE) ? "first" : "second");
				if (want_first != FALSE)
				{
					sSYNetSyncFhashLightTriggerFirstFired = TRUE;
				}
				else
				{
					sSYNetSyncFhashLightTriggerSecondFired = TRUE;
				}
			}
		}
	}
	sSYNetSyncFhashLightTriggerPrevTick = tick;
	sSYNetSyncFhashLightTriggerPrevFigh = cur_figh;
	for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
	{
		sSYNetSyncFhashLightTriggerPrevSlot[si] = cur_slot[si];
	}
}

void syNetSyncCollectFighterSlotHashes(u32 out_slot_hash[GMCOMMON_PLAYERS_MAX])
{
	GObj *fighter_gobj;
	s32 si;

	if (out_slot_hash == NULL)
	{
		return;
	}
	for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
	{
		out_slot_hash[si] = 2166136261U;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		s32 slot;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			continue;
		}
		slot = fp->player;
		if ((slot >= 0) && (slot < GMCOMMON_PLAYERS_MAX))
		{
			out_slot_hash[slot] = syNetSyncHashFighterStructLight(fp);
		}
	}
}

static sb32 syNetSyncFighterSlotHashLogEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_FIGHTER_SLOT_HASH_LOG");
	s_env_cache = ((e != NULL) && (e[0] != '\0') && (syNetSyncEnvParseInt(e, 0) != 0)) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}

static sb32 syNetSyncFighterSlotHashLogTickInWindow(u32 tick)
{
	static int s_min_cache = -999999;
	static int s_max_cache = -999999;
	const char *e;
	s32 min_tick;
	s32 max_tick;

	if (s_min_cache == -999999)
	{
		min_tick = 0;
		max_tick = 0;
		e = getenv("SSB64_NETPLAY_FIGHTER_SLOT_HASH_TICK_MIN");
		if ((e != NULL) && (e[0] != '\0'))
		{
			min_tick = syNetSyncEnvParseInt(e, 0);
		}
		e = getenv("SSB64_NETPLAY_FIGHTER_SLOT_HASH_TICK_MAX");
		if ((e != NULL) && (e[0] != '\0'))
		{
			max_tick = syNetSyncEnvParseInt(e, 0);
		}
		if (max_tick <= 0)
		{
			max_tick = 0;
			s_min_cache = 0;
			s_max_cache = 0;
			return TRUE;
		}
		if (min_tick < 0)
		{
			min_tick = 0;
		}
		s_min_cache = min_tick;
		s_max_cache = max_tick;
	}
	if (s_max_cache == 0)
	{
		return TRUE;
	}
	return ((tick >= (u32)s_min_cache) && (tick <= (u32)s_max_cache)) ? TRUE : FALSE;
}

void syNetSyncLogFighterSlotHashes(u32 tick)
{
	GObj *fighter_gobj;

	if ((syNetSyncFighterSlotHashLogEnabled() == FALSE) || (syNetSyncFighterSlotHashLogTickInWindow(tick) == FALSE))
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

		if (fp == NULL)
		{
			continue;
		}
		port_log(
		    "SSB64 NetSync: fighter_slot_hash tick=%u player=%d fkind=%d status=%d motion=%d "
		    "fhash_light=0x%08X fhash_full=0x%08X anim_hash=0x%08X camera_mode=%u\n",
		    tick,
		    (int)fp->player,
		    (int)fp->fkind,
		    (int)fp->status_id,
		    (int)fp->motion_id,
		    syNetSyncHashFighterStructLight(fp),
		    syNetSyncHashFighterSlotFull(fp),
		    syNetSyncHashFighterSlotAnim(fp, fighter_gobj),
		    (unsigned int)fp->camera_mode);
	}
}

static sb32 syNetSyncPKThunderHoldDiagEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_PKTHUNDER_HOLD_DIAG");
	s_env_cache = ((e != NULL) && (e[0] != '\0') && (syNetSyncEnvParseInt(e, 0) != 0)) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}

static sb32 syNetSyncPKThunderHoldDiagTickInWindow(u32 tick)
{
	static int s_min_cache = -999999;
	static int s_max_cache = -999999;
	const char *e;
	s32 min_tick;
	s32 max_tick;

	if (s_min_cache == -999999)
	{
		min_tick = 0;
		max_tick = 0;
		e = getenv("SSB64_NETPLAY_PKTHUNDER_HOLD_DIAG_TICK_MIN");
		if ((e != NULL) && (e[0] != '\0'))
		{
			min_tick = syNetSyncEnvParseInt(e, 0);
		}
		e = getenv("SSB64_NETPLAY_PKTHUNDER_HOLD_DIAG_TICK_MAX");
		if ((e != NULL) && (e[0] != '\0'))
		{
			max_tick = syNetSyncEnvParseInt(e, 0);
		}
		if (max_tick <= 0)
		{
			max_tick = 0;
			s_min_cache = 0;
			s_max_cache = 0;
			return TRUE;
		}
		if (min_tick < 0)
		{
			min_tick = 0;
		}
		s_min_cache = min_tick;
		s_max_cache = max_tick;
	}
	if (s_max_cache == 0)
	{
		return TRUE;
	}
	return ((tick >= (u32)s_min_cache) && (tick <= (u32)s_max_cache)) ? TRUE : FALSE;
}

static sb32 syNetSyncFighterIsInPKThunderHold(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	return ((fp->fkind == nFTKindNess) &&
	        ((fp->status_id == nFTNessStatusSpecialHiHold) || (fp->status_id == nFTNessStatusSpecialAirHiHold))) ?
	           TRUE :
	           FALSE;
}

static s32 syNetSyncCountOwnedPKThunderWeapons(GObj *fighter_gobj)
{
	GObj *weapon_gobj;
	s32 count;

	count = 0;
	if (fighter_gobj == NULL)
	{
		return 0;
	}
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp = wpGetStruct(weapon_gobj);

		if ((wp != NULL) &&
		    ((wp->kind == nWPKindPKThunderHead) || (wp->kind == nWPKindPKThunderTrail)) &&
		    (wp->owner_gobj == fighter_gobj))
		{
			count++;
		}
	}
	return count;
}

void syNetSyncLogPKThunderHoldDiag(u32 tick)
{
	GObj *fighter_gobj;

	if ((syNetSyncPKThunderHoldDiagEnabled() == FALSE) || (syNetSyncPKThunderHoldDiagTickInWindow(tick) == FALSE))
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
		GObj *head_gobj;
		WPStruct *head_wp;
		DObj *top_joint;
		f32 top_x;
		f32 top_y;

		if ((fp == NULL) || (syNetSyncFighterIsInPKThunderHold(fp) == FALSE))
		{
			continue;
		}
		head_gobj = fp->status_vars.ness.specialhi.pkthunder_gobj;
		head_wp = (head_gobj != NULL) ? wpGetStruct(head_gobj) : NULL;
		top_joint = (fp->joints[nFTPartsJointTopN] != NULL) ? fp->joints[nFTPartsJointTopN] : NULL;
		top_x = (top_joint != NULL) ? top_joint->translate.vec.f.x : 0.0F;
		top_y = (top_joint != NULL) ? top_joint->translate.vec.f.y : 0.0F;
		port_log(
		    "SSB64 NetSync: pkthunder_hold tick=%u player=%d status=%d top=(0x%08X,0x%08X) vel_y=0x%08X "
		    "gravity_delay=%d end_delay=%d destroy=%d trail_id=%d wpn_count=%d coupled_id=%u head_angle=0x%08X "
		    "fhash=0x%08X wpn=0x%08X stick=(%d,%d)\n",
		    tick,
		    (int)fp->player,
		    (int)fp->status_id,
		    syNetSyncHashF32(top_x),
		    syNetSyncHashF32(top_y),
		    syNetSyncHashF32(fp->physics.vel_air.y),
		    (int)fp->status_vars.ness.specialhi.pkthunder_gravity_delay,
		    (int)fp->status_vars.ness.specialhi.pkthunder_end_delay,
		    (int)(fp->passive_vars.ness.is_thunder_destroy & TRUE),
		    (int)fp->passive_vars.ness.pkthunder_trail_id,
		    (int)syNetSyncCountOwnedPKThunderWeapons(fighter_gobj),
		    (head_gobj != NULL) ? (unsigned int)head_gobj->id : 0U,
		    (head_wp != NULL) ? syNetSyncHashF32(head_wp->weapon_vars.pkthunder.angle) : 0U,
		    syNetSyncHashFighterStructLight(fp),
		    syNetSyncHashActiveWeaponsForRollback(),
		    (int)fp->input.pl.stick_range.x,
		    (int)fp->input.pl.stick_range.y);
	}
}

void syNetSyncLogBaselineUniverseDiff(u32 load_tick, u32 peer_figh, u32 local_figh, u32 peer_world, u32 local_world,
				      u32 peer_rng, u32 local_rng)
{
	u32 hash_camera;
	GObj *fighter_gobj;
	s32 si;

	if (syNetSyncPeerDivergeDetailEnabled() == FALSE)
	{
		return;
	}
	hash_camera = syNetSyncHashGMCamera();
	port_log(
	    "SSB64 NetRollback: BASELINE_UNIVERSE_DIFF load_tick=%u peer figh=0x%08X local figh=0x%08X world=0x%08X rng=0x%08X cam=0x%08X\n",
	    load_tick,
	    peer_figh,
	    local_figh,
	    peer_world,
	    peer_rng,
	    hash_camera);
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if (fp == NULL)
		{
			continue;
		}
		si = fp->player;
		if ((si < 0) || (si >= GMCOMMON_PLAYERS_MAX))
		{
			continue;
		}
		port_log(
		    "SSB64 NetRollback: BASELINE_UNIVERSE_DIFF load_tick=%u player=%d status=%d motion=%d "
		    "fhash_light=0x%08X fhash_full=0x%08X anim_hash=0x%08X camera_mode=%u\n",
		    load_tick,
		    (int)si,
		    (int)fp->status_id,
		    (int)fp->motion_id,
		    syNetSyncHashFighterStructLight(fp),
		    syNetSyncHashFighterSlotFull(fp),
		    syNetSyncHashFighterSlotAnim(fp, fighter_gobj),
		    (unsigned int)fp->camera_mode);
	}
	syNetSyncLogFighterDetail("baseline_universe", load_tick);
	syNetRbSnapshotLogFighterFieldDiffAtTick(load_tick, "baseline_universe");
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

/*
 * Rollback load-verify item fold (must match snapshot blob semantics after apply).
 *
 * Included: kind, type, lifetime, percent_damage, lr, player, team, owner/damage/reflect gobj ids,
 *           hold/thrown/damage-all flags, attack_state, attack_records[], DObj translate x/y/z.
 *           Link bomb: multi, event_id, ga, physics vel_air, linkbomb item_vars, status nibble.
 *           Ground monsters: DObj anim_frame/wait, mobj texture_id; Hitokage flags/flame_spawn_wait/offset.
 *
 * Omitted: gobj->id — snapshot apply may eject/respawn via itManagerMakeItemSetupCommon and allocate
 * fresh GObj ids; folding the item's own id caused false LOAD_HASH_DRIFT after respawn. Apply still
 * matches blobs by gobj_id when ids are preserved (see docs/bugs/netrollback_item_weapon_gobj_id_verify).
 */
#ifdef PORT
static u8 syNetSyncLinkBombStatusFromLive(const ITStruct *ip)
{
	if (ip == NULL)
	{
		return 0xFFU;
	}
	if (ip->proc_update == itLinkBombExplodeProcUpdate)
	{
		return (u8)nITLinkBombStatusExplode;
	}
	if (ip->proc_update == itLinkBombHoldProcUpdate)
	{
		return (u8)nITLinkBombStatusHold;
	}
	if (ip->proc_update == itLinkBombDroppedProcUpdate)
	{
		return (u8)nITLinkBombStatusDropped;
	}
	if ((ip->proc_update == itLinkBombFallProcUpdate) && (ip->proc_map == itLinkBombThrownProcMap) &&
	    (ip->proc_hit == itLinkBombThrownProcHit))
	{
		return (u8)nITLinkBombStatusThrown;
	}
	if (ip->proc_update == itLinkBombWaitProcUpdate)
	{
		return (u8)nITLinkBombStatusWait;
	}
	if (ip->proc_update == itLinkBombFallProcUpdate)
	{
		return (u8)nITLinkBombStatusFall;
	}
	return 0xFFU;
}

static u32 syNetSyncFoldLinkBombItemExtras(const ITStruct *ip, u32 fold)
{
	u8 link_status;

	if (ip == NULL)
	{
		return fold;
	}
	fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->multi);
	fold = syNetSyncFnvAccumulateU32(fold, ip->event_id);
	fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->ga);
	fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(ip->physics.vel_air.x));
	fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(ip->physics.vel_air.y));
	fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(ip->physics.vel_air.z));
	fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->item_vars.linkbomb.unk_0x0);
	fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->item_vars.linkbomb.drop_update_wait);
	fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->item_vars.linkbomb.scale_id);
	fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->item_vars.linkbomb.scale_int);
	link_status = syNetSyncLinkBombStatusFromLive(ip);
	if (link_status <= (u8)nITLinkBombStatusExplode)
	{
		fold = syNetSyncFnvAccumulateU32(fold, (u32)link_status);
	}
	else
	{
		fold = syNetSyncFnvAccumulateU32(fold, 0xFFU);
	}
	return fold;
}

static u32 syNetSyncFoldGroundMonsterItemExtras(GObj *gobj, const ITStruct *ip, u32 fold)
{
	DObj *dobj;

	if ((gobj == NULL) || (ip == NULL))
	{
		return fold;
	}
	dobj = DObjGetStruct(gobj);
	if (dobj != NULL)
	{
		fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(dobj->anim_frame));
		fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(dobj->anim_wait));
		if (dobj->mobj != NULL)
		{
			fold = syNetSyncFnvAccumulateU32(fold, (u32)dobj->mobj->texture_id_curr);
		}
	}
	if (ip->kind == nITKindHitokage)
	{
		fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->item_vars.hitokage.flags);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->item_vars.hitokage.flame_spawn_wait);
		fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(ip->item_vars.hitokage.offset.x));
		fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(ip->item_vars.hitokage.offset.y));
		fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(ip->item_vars.hitokage.offset.z));
	}
	return fold;
}

static u32 syNetSyncFoldGBumperItemExtras(GObj *gobj, const ITStruct *ip, u32 fold)
{
	DObj *dobj;

	if ((gobj == NULL) || (ip == NULL))
	{
		return fold;
	}
	fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->multi);
	fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->item_vars.bumper.hit_anim_length);
	dobj = DObjGetStruct(gobj);
	if (dobj != NULL)
	{
		fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(dobj->scale.vec.f.x));
		fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(dobj->scale.vec.f.y));
		if (dobj->mobj != NULL)
		{
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(dobj->mobj->palette_id));
		}
	}
	return fold;
}
#endif

static u32 syNetSyncFoldActiveItemGobjForRollback(GObj *gobj)
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
	fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->kind);
	fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->type);
	fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->lifetime);
	fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->percent_damage);
	fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->lr);
	fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->player);
	fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->team);
	fold = syNetSyncFnvAccumulateU32(fold, (ip->owner_gobj != NULL) ? (u32)ip->owner_gobj->id : 0U);
	fold = syNetSyncFnvAccumulateU32(fold, (ip->damage_gobj != NULL) ? (u32)ip->damage_gobj->id : 0U);
	fold = syNetSyncFnvAccumulateU32(fold, (ip->reflect_gobj != NULL) ? (u32)ip->reflect_gobj->id : 0U);
	fold = syNetSyncFnvAccumulateU32(fold, ip->is_hold ? 1U : 0U);
	fold = syNetSyncFnvAccumulateU32(fold, ip->is_allow_pickup ? 1U : 0U);
	fold = syNetSyncFnvAccumulateU32(fold, ip->is_thrown ? 1U : 0U);
	fold = syNetSyncFnvAccumulateU32(fold, ip->is_damage_all ? 1U : 0U);
	fold = syNetSyncFnvAccumulateU32(fold, (u32)ip->attack_coll.attack_state);
	fold = syNetSyncFoldAttackRecordSlots(ip->attack_coll.attack_records, GMATTACKREC_NUM_MAX, fold);
	dobj = DObjGetStruct(gobj);
	if (dobj != NULL)
	{
		Vec3f pos = dobj->translate.vec.f;

		fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.x));
		fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.y));
		fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.z));
	}
#ifdef PORT
	if (ip->kind == nITKindLinkBomb)
	{
		fold = syNetSyncFoldLinkBombItemExtras(ip, fold);
	}
	else if (ip->kind == nITKindGBumper)
	{
		fold = syNetSyncFoldGBumperItemExtras(gobj, ip, fold);
	}
	else if ((ip->kind >= nITKindGroundMonsterStart) && (ip->kind <= nITKindGroundMonsterEnd))
	{
		fold = syNetSyncFoldGroundMonsterItemExtras(gobj, ip, fold);
	}
#endif
	return fold;
}

u32 syNetSyncHashActiveItemsForRollback(void)
{
	GObj *sorted[SYNET_SYNC_ITEM_HASH_SORT_MAX];
	s32 count;
	u32 hash;
	s32 i;
	sb32 truncated;

	truncated = FALSE;
	count = syNetRbEnumerateActiveItemsSorted(sorted, SYNET_SYNC_ITEM_HASH_SORT_MAX, &truncated);
#ifdef PORT
	if ((truncated != FALSE) && (sSYNetSyncItemHashTruncationLogged == FALSE))
	{
		port_log("SSB64 NetSync: item hash truncated at max=%d (snapshot cap; save will fail if overflow)\n",
		         SYNET_SYNC_ITEM_HASH_SORT_MAX);
		sSYNetSyncItemHashTruncationLogged = TRUE;
	}
#endif
	hash = 2166136261U;
	for (i = 0; i < count; i++)
	{
		u32 fold;

		fold = syNetSyncFoldActiveItemGobjForRollback(sorted[i]);
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

/* Item “opcode”: `ITStruct.type` selects procedural behavior / script step; atk_state + multi/ev/lifetime constrain it. */
static sb32 syNetSyncItemOpcodeTraceEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_ITEM_OPCODE_TRACE");
	s_env_cache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}

/* NULL-safe fingerprint of any code/data pointer (truncated XOR on LP64 — not symbolic). */
static u32 syNetSyncPointerFingerprintLow32(const void *p)
{
	uintptr_t u;

	u = (uintptr_t)p;
	if (sizeof(u) > sizeof(u32))
	{
		u ^= u >> 32;
	}
	return (u32)u;
}

/*
 * Raw IEEE-754 bits of an f32 with NO quantization — exposes the pre-quantize, sub-ULP
 * cross-ISA divergence that the %.5f decimal print and the quantized fold both hide.
 * Pair with syNetSyncHashF32 (post-quantize bits = what actually folds) to tell whether a
 * field diverges before quantization (and quantize fails to collapse it) or only in the print.
 */
static u32 syNetSyncF32RawBits(f32 value)
{
	union SYNetSyncF32RawReinterpret
	{
		f32 fv;
		u32 uv;

	} reinterpret;

	reinterpret.fv = value;
	return reinterpret.uv;
}

static void syNetSyncLogItemHashWalkBody(u32 sim_tick, u32 slot_item, u32 live_item, const char *reason)
{
	GObj *sorted[SYNET_SYNC_ITEM_HASH_SORT_MAX];
	GObj *gobj;
	s32 count;
	u32 hash;
	u32 idx;
	s32 i;
	sb32 truncated;

	truncated = FALSE;
	count = syNetRbEnumerateActiveItemsSorted(sorted, SYNET_SYNC_ITEM_HASH_SORT_MAX, &truncated);
	hash = 2166136261U;
	idx = 0U;
	port_log(
	    "SSB64 NetSync: item_hash_walk begin sim_tick=%u live_sim=%u cap=%d truncated=%d reason=%s slot_item=0x%08X live_item=0x%08X\n",
	    sim_tick,
	    (unsigned int)syNetInputGetTick(),
	    SYNET_SYNC_ITEM_HASH_SORT_MAX,
	    (int)truncated,
	    (reason != NULL) ? reason : "trace",
	    slot_item,
	    live_item);
	for (i = 0; i < count; i++)
	{
		ITStruct *ip;
		u32 fold;

		gobj = sorted[i];
		ip = itGetStruct(gobj);
		if (ip == NULL)
		{
			continue;
		}
		fold = syNetSyncFoldActiveItemGobjForRollback(gobj);
		hash ^= fold;
		hash = syNetSyncFnvAccumulateU32(hash, 0xA5A5A5A5U);
		if (syNetSyncItemOpcodeTraceEnabled())
		{
			/*
			 * `type` selects the active item procedural/script step (effective “opcode” for logs).
			 * Proc columns are XOR-folded uintptr low halves — compare same build; different ASLR still useful within one run.
			 */
			port_log(
			    "SSB64 NetSync: item_hash_walk step=%u gobj_id=%u kind=%d type=%d fold=0x%08X hash=0x%08X "
			    "atk_state=%d multi=%u event_id=%u lifetime=%d dmg_gid=%u own_gid=%u ref_gid=%u "
			    "hold=%u apick=%u thrwn=%u d_all=%u proc_up=%08X proc_map=%08X proc_hit=%08X proc_sd=%08X "
			    "proc_hop=%08X proc_so=%08X proc_rf=%08X proc_dmg=%08X proc_dead=%08X\n",
			    idx,
			    (unsigned int)gobj->id,
			    (int)ip->kind,
			    (int)ip->type,
			    fold,
			    hash,
			    (int)ip->attack_coll.attack_state,
			    (unsigned int)ip->multi,
			    (unsigned int)ip->event_id,
			    (int)ip->lifetime,
			    (unsigned int)((ip->damage_gobj != NULL) ? (u32)ip->damage_gobj->id : 0U),
			    (unsigned int)((ip->owner_gobj != NULL) ? (u32)ip->owner_gobj->id : 0U),
			    (unsigned int)((ip->reflect_gobj != NULL) ? (u32)ip->reflect_gobj->id : 0U),
			    (unsigned int)(ip->is_hold ? 1U : 0U),
			    (unsigned int)(ip->is_allow_pickup ? 1U : 0U),
			    (unsigned int)(ip->is_thrown ? 1U : 0U),
			    (unsigned int)(ip->is_damage_all ? 1U : 0U),
			    syNetSyncPointerFingerprintLow32((const void *)ip->proc_update),
			    syNetSyncPointerFingerprintLow32((const void *)ip->proc_map),
			    syNetSyncPointerFingerprintLow32((const void *)ip->proc_hit),
			    syNetSyncPointerFingerprintLow32((const void *)ip->proc_shield),
			    syNetSyncPointerFingerprintLow32((const void *)ip->proc_hop),
			    syNetSyncPointerFingerprintLow32((const void *)ip->proc_setoff),
			    syNetSyncPointerFingerprintLow32((const void *)ip->proc_reflector),
			    syNetSyncPointerFingerprintLow32((const void *)ip->proc_damage),
			    syNetSyncPointerFingerprintLow32((const void *)ip->proc_dead));
		}
		else
		{
			port_log(
			    "SSB64 NetSync: item_hash_walk step=%u gobj_id=%u kind=%d type=%d fold=0x%08X hash=0x%08X\n",
			    idx,
			    (unsigned int)gobj->id,
			    (int)ip->kind,
			    (int)ip->type,
			    fold,
			    hash);
		}
		idx++;
	}
	port_log("SSB64 NetSync: item_hash_walk end sim_tick=%u count=%u hash=0x%08X slot_item=0x%08X live_item=0x%08X\n",
	         sim_tick,
	         idx,
	         hash,
	         slot_item,
	         live_item);
}

void syNetSyncLogItemHashWalkTrace(u32 sim_tick)
{
	if (syNetSyncItemHashTraceEnabled() == FALSE)
	{
		return;
	}
	syNetSyncLogItemHashWalkBody(sim_tick, 0U, 0U, "env_trace");
}

void syNetSyncLogItemHashDriftDiag(u32 sim_tick, u32 slot_item, u32 live_item, const char *reason)
{
	syNetSyncLogItemHashWalkBody(sim_tick, slot_item, live_item, reason);
}

static sb32 syNetSyncItemHashFieldDiffEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_ITEM_HASH_FIELD_DIFF");
	s_env_cache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}

void syNetSyncLogItemFieldDiffDiag(u32 sim_tick, u32 slot_item, u32 live_item, const char *reason)
{
	GObj *sorted[SYNET_SYNC_ITEM_HASH_SORT_MAX];
	s32 count;
	u32 idx;
	s32 i;
	sb32 truncated;

	if (syNetSyncItemHashFieldDiffEnabled() == FALSE)
	{
		return;
	}
	truncated = FALSE;
	count = syNetRbEnumerateActiveItemsSorted(sorted, SYNET_SYNC_ITEM_HASH_SORT_MAX, &truncated);
	port_log(
	    "SSB64 NetSync: item_field_diff begin sim_tick=%u live_sim=%u reason=%s slot_item=0x%08X live_item=0x%08X count=%d truncated=%d\n",
	    sim_tick,
	    (unsigned int)syNetInputGetTick(),
	    (reason != NULL) ? reason : "trace",
	    slot_item,
	    live_item,
	    (int)count,
	    (int)truncated);
	idx = 0U;
	for (i = 0; i < count; i++)
	{
		ITStruct *ip;
		DObj *dobj;
		u32 fold;
		u8 link_status;

		ip = itGetStruct(sorted[i]);
		if (ip == NULL)
		{
			continue;
		}
		dobj = DObjGetStruct(sorted[i]);
		fold = syNetSyncFoldActiveItemGobjForRollback(sorted[i]);
		link_status = 0xFFU;
		if (ip->kind == nITKindLinkBomb)
		{
			link_status = syNetSyncLinkBombStatusFromLive(ip);
		}
		port_log(
		    "SSB64 NetSync: item_field_diff step=%u gobj_id=%u kind=%d type=%d player=%d multi=%u event_id=%u "
		    "lifetime=%d atk_state=%d hold=%u pos=(%.5f,%.5f,%.5f) vel=(%.5f,%.5f,%.5f) link_status=%u "
		    "lb_drop_wait=%d fold=0x%08X\n",
		    idx,
		    (unsigned int)sorted[i]->id,
		    (int)ip->kind,
		    (int)ip->type,
		    (int)ip->player,
		    (unsigned int)ip->multi,
		    (unsigned int)ip->event_id,
		    (int)ip->lifetime,
		    (int)ip->attack_coll.attack_state,
		    (unsigned int)(ip->is_hold ? 1U : 0U),
		    (dobj != NULL) ? (f64)syNetplayQuantizeF32(dobj->translate.vec.f.x) : 0.0,
		    (dobj != NULL) ? (f64)syNetplayQuantizeF32(dobj->translate.vec.f.y) : 0.0,
		    (dobj != NULL) ? (f64)syNetplayQuantizeF32(dobj->translate.vec.f.z) : 0.0,
		    (f64)syNetplayQuantizeF32(ip->physics.vel_air.x),
		    (f64)syNetplayQuantizeF32(ip->physics.vel_air.y),
		    (f64)syNetplayQuantizeF32(ip->physics.vel_air.z),
		    (unsigned int)link_status,
		    (ip->kind == nITKindLinkBomb) ? (int)ip->item_vars.linkbomb.drop_update_wait : -1,
		    fold);
		if (ip->kind == nITKindLinkBomb)
		{
			f32 px;
			f32 py;
			f32 pz;

			px = (dobj != NULL) ? dobj->translate.vec.f.x : 0.0F;
			py = (dobj != NULL) ? dobj->translate.vec.f.y : 0.0F;
			pz = (dobj != NULL) ? dobj->translate.vec.f.z : 0.0F;

			/*
			 * Raw vs quantized bit patterns for exactly the floats syNetSyncFoldLinkBombItemExtras
			 * folds (pos via base fold, vel_air via extras). Diff host vs guest line-by-line in a
			 * cross-ISA soak: the first *_q mismatch is the field whose hashed bits diverge; a *_raw
			 * mismatch with matching *_q means quantization absorbed it (not the desync source).
			 */
			port_log(
			    "SSB64 NetSync: item_fold_floats step=%u gobj_id=%u "
			    "px_raw=0x%08X py_raw=0x%08X pz_raw=0x%08X px_q=0x%08X py_q=0x%08X pz_q=0x%08X "
			    "vx_raw=0x%08X vy_raw=0x%08X vz_raw=0x%08X vx_q=0x%08X vy_q=0x%08X vz_q=0x%08X "
			    "multi=%u event_id=%u ga=%d drop_wait=%u scale_id=%u scale_int=%u unk0=%u link_status=%u\n",
			    idx,
			    (unsigned int)sorted[i]->id,
			    syNetSyncF32RawBits(px),
			    syNetSyncF32RawBits(py),
			    syNetSyncF32RawBits(pz),
			    syNetSyncHashF32(px),
			    syNetSyncHashF32(py),
			    syNetSyncHashF32(pz),
			    syNetSyncF32RawBits(ip->physics.vel_air.x),
			    syNetSyncF32RawBits(ip->physics.vel_air.y),
			    syNetSyncF32RawBits(ip->physics.vel_air.z),
			    syNetSyncHashF32(ip->physics.vel_air.x),
			    syNetSyncHashF32(ip->physics.vel_air.y),
			    syNetSyncHashF32(ip->physics.vel_air.z),
			    (unsigned int)ip->multi,
			    (unsigned int)ip->event_id,
			    (int)ip->ga,
			    (unsigned int)ip->item_vars.linkbomb.drop_update_wait,
			    (unsigned int)ip->item_vars.linkbomb.scale_id,
			    (unsigned int)ip->item_vars.linkbomb.scale_int,
			    (unsigned int)ip->item_vars.linkbomb.unk_0x0,
			    (unsigned int)link_status);
		}
		idx++;
	}
	port_log("SSB64 NetSync: item_field_diff end sim_tick=%u count=%u\n", sim_tick, idx);
}

static sb32 syNetSyncItemThrowWindowDiagEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
#if defined(SSB64_NETMENU)
	e = getenv("SSB64_NETPLAY_ITEM_THROW_WINDOW_DIAG");
	s_env_cache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
#else
	s_env_cache = 0;
#endif
	return (s_env_cache != 0) ? TRUE : FALSE;
}

void syNetSyncLogItemThrowWindowDiag(u32 sim_tick, const char *skip_reason)
{
	GObj *fighter_gobj;
	GObj *item_gobj;
	s32 item_count;
	u32 item_hash;

	if (syNetSyncItemThrowWindowDiagEnabled() == FALSE)
	{
		return;
	}
	if ((skip_reason == NULL) ||
	    ((strcmp(skip_reason, "fighter_throw") != 0) && (strcmp(skip_reason, "item_throw") != 0)))
	{
		return;
	}

	item_count = 0;
	for (item_gobj = gGCCommonLinks[nGCCommonLinkIDItem]; item_gobj != NULL; item_gobj = item_gobj->link_next)
	{
		if (itGetStruct(item_gobj) != NULL)
		{
			item_count++;
		}
	}
	item_hash = syNetSyncHashActiveItemsForRollback();

	port_log(
	    "SSB64 NetSync: item_throw_window tick=%u reason=%s live_sim=%u item=0x%08X item_count=%d\n",
	    sim_tick,
	    skip_reason,
	    (unsigned int)syNetInputGetTick(),
	    item_hash,
	    (int)item_count);

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);
		ITStruct *hip;
		u32 held_id;
		s32 joint_id;

		if (fp == NULL)
		{
			continue;
		}
		held_id = 0U;
		hip = NULL;
		joint_id = -1;
		if (fp->item_gobj != NULL)
		{
			held_id = fp->item_gobj->id;
			hip = itGetStruct(fp->item_gobj);
			joint_id = (hip != NULL && hip->weight == nITWeightHeavy) ? fp->attr->joint_itemheavy_id
			                                                          : fp->attr->joint_itemlight_id;
		}
		port_log(
		    "SSB64 NetSync: item_hold_coupling tick=%u player=%d fkind=%d status=%d motion=%d "
		    "item_gobj=%u joint_id=%d is_throw_item=%d throw_flag0=%u throw_vel_pct=%u throw_angle=%d "
		    "held_kind=%d held_hold=%u held_thrwn=%u\n",
		    sim_tick,
		    (int)fp->player,
		    (int)fp->fkind,
		    (int)fp->status_id,
		    (int)fp->motion_id,
		    (unsigned int)held_id,
		    (int)joint_id,
		    (int)fp->motion_vars.item_throw.is_throw_item,
		    (unsigned int)fp->motion_vars.flags.flag0,
		    (unsigned int)ftStatusVarsItemThrow(fp)->throw_vel,
		    (int)ftStatusVarsItemThrow(fp)->throw_angle,
		    (hip != NULL) ? (int)hip->kind : -1,
		    (hip != NULL) ? (unsigned int)(hip->is_hold ? 1U : 0U) : 0U,
		    (hip != NULL) ? (unsigned int)(hip->is_thrown ? 1U : 0U) : 0U);
	}

	if (syNetSyncItemHashFieldDiffEnabled() != FALSE)
	{
		syNetSyncLogItemFieldDiffDiag(sim_tick, item_hash, item_hash, "synctest_throw_window");
	}
	if (syNetSyncItemHashTraceEnabled() != FALSE)
	{
		syNetSyncLogItemHashDriftDiag(sim_tick, item_hash, item_hash, "synctest_throw_window");
	}
}

static sb32 syNetSyncYosterCloudDiagEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
#if defined(SSB64_NETMENU)
	e = getenv("SSB64_NETPLAY_YOSTER_CLOUD_DIAG");
	s_env_cache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
#else
	s_env_cache = 0;
#endif
	return (s_env_cache != 0) ? TRUE : FALSE;
}

static sb32 syNetSyncYosterCloudDiagVerbose(void)
{
	const char *e;

	e = getenv("SSB64_NETPLAY_YOSTER_CLOUD_DIAG_VERBOSE");
	return ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? TRUE : FALSE;
}

static sb32 syNetSyncYosterCloudDiagFighters(void)
{
	const char *e;

	e = getenv("SSB64_NETPLAY_YOSTER_CLOUD_DIAG_FIGHTERS");
	if (e == NULL)
	{
		return TRUE;
	}
	return ((e[0] != '\0') && (atoi(e) != 0)) ? TRUE : FALSE;
}

static u32 syNetSyncYosterCloudDiagTickBound(const char *name, u32 default_value)
{
	const char *e;
	int v;

	e = getenv(name);
	if ((e == NULL) || (e[0] == '\0'))
	{
		return default_value;
	}
	v = atoi(e);
	if (v < 0)
	{
		return default_value;
	}
	return (u32)v;
}

static u32 syNetSyncF32Bits(f32 value)
{
	u32 bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static sb32 syNetSyncYosterCloudAnimIdle(const GRYosterCloud *cloud)
{
	if ((cloud == NULL) || (cloud->dobj[0] == NULL))
	{
		return FALSE;
	}
	return grYosterCloudMatAnimIsIdle(cloud->dobj[0]->mobj);
}

static sb32 syNetSyncYosterCloudPressureGateOpen(const GRYosterCloud *cloud)
{
	MObj *mobj;

	if (cloud == NULL)
	{
		return FALSE;
	}
	mobj = ((cloud->dobj[0] != NULL) ? cloud->dobj[0]->mobj : NULL);
	return grYosterCloudPressureGateOpen(cloud, mobj);
}

static sb32 syNetSyncYosterCloudShouldLogSummary(const GRYosterCloud *cloud, sb32 stand, s32 cloud_id)
{
	if (syNetSyncYosterCloudDiagVerbose() != FALSE)
	{
		return TRUE;
	}
	if (grYosterCloudReestablishedThisTick(cloud_id) != FALSE)
	{
		return TRUE;
	}
	if (stand != FALSE)
	{
		return TRUE;
	}
	if (cloud->status != 0U)
	{
		return TRUE;
	}
	if (cloud->pressure > 0.0F)
	{
		return TRUE;
	}
	if (cloud->pressure_timer >= 0)
	{
		return TRUE;
	}
	if (cloud->evaporate_wait != 0U)
	{
		return TRUE;
	}
	if (syNetSyncYosterCloudPressureGateOpen(cloud) == FALSE)
	{
		return TRUE;
	}
	return FALSE;
}

void syNetSyncLogYosterCloudDiag(s32 cloud_id)
{
	const GRYosterCloud *cloud;
	u32 sim_tick;
	s32 exp_line;
	s32 stand;
	f32 translate_y;
	u32 anim_wait_bits;
	u32 map_head_bits;
	u32 matanim_bits;
	u32 anim_speed_bits;
	u32 gobj_bits;
	sb32 anim_idle;
	sb32 gate_open;
	sb32 dobj0_ok;
	sb32 mobj_ok;
	sb32 root_ok;
	sb32 root_child_ok;
	sb32 reest_this_tick;
	f32 root_x;
	f32 root_z;
	Vec3f spawn_translate;

	if (syNetSyncYosterCloudDiagEnabled() == FALSE)
	{
		return;
	}
	if ((gSCManagerBattleState == NULL) || (gSCManagerBattleState->gkind != nGRKindYoster))
	{
		return;
	}
	if ((cloud_id < 0) || (cloud_id >= (s32)ARRAY_COUNT(gGRCommonStruct.yoster.clouds)))
	{
		return;
	}
	sim_tick = syNetInputGetTick();
	if ((sim_tick < syNetSyncYosterCloudDiagTickBound("SSB64_NETPLAY_YOSTER_CLOUD_DIAG_TICK_MIN", 0U)) ||
	    (sim_tick > syNetSyncYosterCloudDiagTickBound("SSB64_NETPLAY_YOSTER_CLOUD_DIAG_TICK_MAX", 60000U)))
	{
		return;
	}

	cloud = &gGRCommonStruct.yoster.clouds[cloud_id];
	exp_line = (s32)dGRYosterCloudLineIDs[cloud_id];
	stand = grYosterCheckFighterCloudStand(cloud_id);
	anim_idle = syNetSyncYosterCloudAnimIdle(cloud);
	gate_open = syNetSyncYosterCloudPressureGateOpen(cloud);
	dobj0_ok = (cloud->dobj[0] != NULL) ? TRUE : FALSE;
	mobj_ok = ((cloud->dobj[0] != NULL) && (cloud->dobj[0]->mobj != NULL)) ? TRUE : FALSE;
	root_ok = FALSE;
	root_child_ok = FALSE;
	reest_this_tick = grYosterCloudReestablishedThisTick(cloud_id);
	root_x = 0.0F;
	root_z = 0.0F;
	spawn_translate.x = 0.0F;
	spawn_translate.y = 0.0F;
	spawn_translate.z = 0.0F;
	grYosterGetCloudSpawnTranslate(cloud_id, &spawn_translate);
	translate_y = 0.0F;
	anim_wait_bits = 0U;
	map_head_bits = 0U;
	matanim_bits = 0U;
	anim_speed_bits = 0U;
	if ((cloud->dobj[0] != NULL) && (cloud->dobj[0]->mobj != NULL))
	{
		MObj *mobj = cloud->dobj[0]->mobj;

		anim_wait_bits = syNetSyncF32Bits(mobj->anim_wait);
		anim_speed_bits = syNetSyncF32Bits(mobj->anim_speed);
		matanim_bits = (u32)(uintptr_t)mobj->matanim_joint.event32;
	}
	map_head_bits = (u32)(uintptr_t)gGRCommonStruct.yoster.map_head;
	gobj_bits = (u32)(uintptr_t)cloud->gobj;
	if (cloud->gobj != NULL)
	{
		DObj *root = DObjGetStruct(cloud->gobj);

		if (root != NULL)
		{
			root_ok = TRUE;
			if (root->child != NULL)
			{
				root_child_ok = TRUE;
			}
			root_x = root->translate.vec.f.x;
			translate_y = root->translate.vec.f.y;
			root_z = root->translate.vec.f.z;
		}
	}

	if (syNetSyncYosterCloudShouldLogSummary(cloud, stand, cloud_id) != FALSE)
	{
		port_log(
		    "SSB64 NetSync: yoster_cloud tick=%u cloud=%d status=%u anim_id=%d altitude=%.2f pressure=%.4f ptimer=%d evap=%u "
		    "line_act=%u stand=%d anim_idle=%d gate=%d root=%d root_child=%d reest=%d dobj0=%d mobj=%d gobj=0x%08X anim_wait=0x%08X anim_speed=0x%08X matanim=0x%08X map_head=0x%08X "
		    "root_x=%.2f translate_y=%.2f root_z=%.2f spawn_x=%.2f spawn_y=%.2f spawn_z=%.2f rb_resim=%d rb_applied=%u\n",
		    sim_tick,
		    (int)cloud_id,
		    (unsigned int)cloud->status,
		    (int)cloud->anim_id,
		    cloud->altitude,
		    cloud->pressure,
		    (int)cloud->pressure_timer,
		    (unsigned int)cloud->evaporate_wait,
		    (unsigned int)cloud->is_cloud_line_active,
		    (int)(stand != FALSE),
		    (int)(anim_idle != FALSE),
		    (int)(gate_open != FALSE),
		    (int)(root_ok != FALSE),
		    (int)(root_child_ok != FALSE),
		    (int)(reest_this_tick != FALSE),
		    (int)(dobj0_ok != FALSE),
		    (int)(mobj_ok != FALSE),
		    gobj_bits,
		    anim_wait_bits,
		    anim_speed_bits,
		    matanim_bits,
		    map_head_bits,
		    root_x,
		    translate_y,
		    root_z,
		    spawn_translate.x,
		    spawn_translate.y,
		    spawn_translate.z,
		    (int)(syNetRollbackIsResimulating() != FALSE),
		    (unsigned int)syNetRollbackGetAppliedResimCount());
	}

	if (syNetSyncYosterCloudDiagFighters() == FALSE)
	{
		return;
	}
	{
		GObj *fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter];

		while (fighter_gobj != NULL)
		{
			FTStruct *fp = ftGetStruct(fighter_gobj);

			if ((fp != NULL) && (fp->ga == nMPKineticsGround))
			{
				s32 floor_line = fp->coll_data.floor_line_id;
				s32 map_line = -1;
				sb32 match;

				if (floor_line != -2)
				{
					map_line = mpCollisionSetDObjNoID(floor_line);
				}
					match = ((floor_line != -2) && (map_line == exp_line)) ? TRUE : FALSE;
				if ((stand == FALSE) || (match == FALSE) || (syNetSyncYosterCloudDiagVerbose() != FALSE))
				{
					u32 top_y_bits = 0U;

					if ((fp->joints[nFTPartsJointTopN] != NULL))
					{
						top_y_bits = syNetSyncF32Bits(fp->joints[nFTPartsJointTopN]->translate.vec.f.y);
					}
					port_log(
					    "SSB64 NetSync: yoster_cloud_fighter tick=%u cloud=%d player=%d fkind=%d ga=%d "
					    "floor_line=%d map_line=%d exp_line=%d match=%d top_y=0x%08X\n",
					    sim_tick,
					    (int)cloud_id,
					    (int)fp->player,
					    (int)fp->fkind,
					    (int)fp->ga,
					    (int)floor_line,
					    (int)map_line,
					    (int)exp_line,
					    (int)(match != FALSE),
					    top_y_bits);
				}
			}
			fighter_gobj = fighter_gobj->link_next;
		}
	}
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
	GObj *sorted[SYNET_SYNC_WEAPON_HASH_SORT_MAX];
	s32 count;
	u32 hash;
	s32 i;
	sb32 truncated;

	truncated = FALSE;
	count = syNetRbEnumerateActiveWeaponsSorted(sorted, SYNET_SYNC_WEAPON_HASH_SORT_MAX, &truncated);
#ifdef PORT
	{
		static sb32 sSYNetSyncWeaponHashTruncationLogged = FALSE;

		if ((truncated != FALSE) && (sSYNetSyncWeaponHashTruncationLogged == FALSE))
		{
			port_log("SSB64 NetSync: weapon hash truncated at max=%d (snapshot cap; save will fail if overflow)\n",
			         SYNET_SYNC_WEAPON_HASH_SORT_MAX);
			sSYNetSyncWeaponHashTruncationLogged = TRUE;
		}
	}
#endif
	hash = 2166136261U;
	for (i = 0; i < count; i++)
	{
		GObj *gobj = sorted[i];
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
			Vec3f rot = dobj->rotate.vec.f;
			Vec3f scl = dobj->scale.vec.f;

			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.x));
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.y));
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.z));
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(rot.x));
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(rot.y));
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(rot.z));
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(scl.x));
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(scl.y));
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(scl.z));
		}
		fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(wp->physics.vel_air.x));
		fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(wp->physics.vel_air.y));
		fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(wp->physics.vel_air.z));
		fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(wp->physics.vel_ground));
		fold = syNetSyncFnvAccumulateU32(fold, (u32)wp->ga);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)wp->attack_coll.attack_state);
		fold = syNetSyncFoldAttackRecordSlots(wp->attack_coll.attack_records, GMATTACKREC_NUM_MAX, fold);
		if (wp->kind == nWPKindEggThrow)
		{
			fold = syNetSyncFnvAccumulateU32(fold, wp->weapon_vars.egg_throw.is_throw ? 1U : 0U);
			fold = syNetSyncFnvAccumulateU32(fold, wp->weapon_vars.egg_throw.is_spin ? 1U : 0U);
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(wp->weapon_vars.egg_throw.throw_force));
		}
		else if (wp->kind == nWPKindFireball)
		{
			fold = syNetSyncFnvAccumulateU32(fold, (u32)wp->weapon_vars.fireball.index);
		}
		else if (wp->kind == nWPKindChargeShot)
		{
			fold = syNetSyncFnvAccumulateU32(fold, wp->weapon_vars.charge_shot.is_release ? 1U : 0U);
			fold = syNetSyncFnvAccumulateU32(fold, wp->weapon_vars.charge_shot.is_full_charge ? 1U : 0U);
			fold = syNetSyncFnvAccumulateU32(fold, (u32)wp->weapon_vars.charge_shot.charge_size);
		}
		else if ((wp->kind == nWPKindThunderJoltAir) || (wp->kind == nWPKindThunderJoltGround))
		{
			fold = syNetSyncFnvAccumulateU32(fold, (u32)wp->weapon_vars.thunder_jolt.line_type);
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(wp->weapon_vars.thunder_jolt.rotate.x));
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(wp->weapon_vars.thunder_jolt.rotate.y));
		}
		else if (wp->kind == nWPKindThunderHead)
		{
			fold = syNetSyncFnvAccumulateU32(fold, (u32)wp->weapon_vars.thunder.thunder_state);
		}
		else if ((wp->kind == nWPKindPKThunderHead) || (wp->kind == nWPKindPKThunderTrail))
		{
			if (wp->kind == nWPKindPKThunderHead)
			{
				fold = syNetSyncFnvAccumulateU32(fold, (u32)wp->weapon_vars.pkthunder.status);
				fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(wp->weapon_vars.pkthunder.angle));
			}
			else
			{
				fold = syNetSyncFnvAccumulateU32(fold, (u32)wp->weapon_vars.pkthunder_trail.status);
				fold = syNetSyncFnvAccumulateU32(fold, (u32)wp->weapon_vars.pkthunder_trail.trail_id);
			}
		}
		hash ^= fold;
		hash = syNetSyncFnvAccumulateU32(hash, 0x5A5A5A5AU);
	}
	return hash;
}

#ifdef PORT
static sb32 syNetSyncFighterInRebirthScope(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	return ((fp->status_id >= nFTCommonStatusRebirthDown) && (fp->status_id <= nFTCommonStatusRebirthWait))
	           ? TRUE
	           : FALSE;
}

static sb32 syNetSyncFighterRebirthHaloLifecycleActive(const FTStruct *fp)
{
	return syNetSyncFighterInRebirthScope(fp);
}

static sb32 syNetSyncEffectIsRebirthHaloCoupling(const GObj *effect_gobj, const EFStruct *ep, const FTStruct *fp)
{
	DObj *dobj;

	if ((effect_gobj == NULL) || (ep == NULL) || (fp == NULL) || (ep->fighter_gobj == NULL))
	{
		return FALSE;
	}
	if (ep->proc_update != gcPlayAnimAll)
	{
		return FALSE;
	}
	dobj = DObjGetStruct(effect_gobj);
	return ((dobj != NULL) && (dobj->user_data.p == fp->joints[nFTPartsJointTopN])) ? TRUE : FALSE;
}

sb32 syNetSyncFoldSingleEffectGObj(struct GObj *gobj, u32 *fold_out)
{
	EFStruct *ep;
	DObj *dobj;
	u32 fold;
	sb32 rebirth_halo_effect;
	sb32 ness_pkwave_effect;
	sb32 ness_psychic_magnet_effect;
	sb32 ness_shock_effect;

	if ((gobj == NULL) || (fold_out == NULL))
	{
		return FALSE;
	}
	ep = efGetStruct(gobj);
	rebirth_halo_effect = FALSE;
	ness_pkwave_effect = FALSE;
	ness_psychic_magnet_effect = FALSE;
	ness_shock_effect = FALSE;
	if (ep == NULL)
	{
		if ((gobj->user_data.p == NULL) && (gobj->obj_kind == nGCCommonKindEffect))
		{
			fold = 2166136261U;
			fold = syNetSyncFnvAccumulateU32(fold, (u32)gobj->link_id);
			fold = syNetSyncFnvAccumulateU32(fold, (u32)gobj->obj_kind);
			fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(gobj->anim_frame));
			dobj = DObjGetStruct(gobj);
			if (dobj != NULL)
			{
				Vec3f pos = dobj->translate.vec.f;

				fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.x));
				fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.y));
				fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.z));
			}
			*fold_out = fold;
			return TRUE;
		}
		return FALSE;
	}
	fold = 2166136261U;
	fold = syNetSyncFnvAccumulateU32(fold, (u32)ep->bank_id);
	fold = syNetSyncFnvAccumulateU32(fold, (u32)syNetRbSnapEffectRespawnKindFromLive(gobj, ep));
	fold = syNetSyncFnvAccumulateU32(fold, (ep->fighter_gobj != NULL) ? (u32)ep->fighter_gobj->id : 0U);
	fold = syNetSyncFnvAccumulateU32(fold, (ep->is_pause_effect != FALSE) ? 1U : 0U);
	fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(gobj->anim_frame));
	if (ep->proc_update == efManagerFoxReflectorProcUpdate)
	{
		fold = syNetSyncFnvAccumulateU32(fold, (u32)ep->effect_vars.reflector.index);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)ep->effect_vars.reflector.status);
	}
	if (ep->proc_update == efManagerShieldProcUpdate)
	{
		fold = syNetSyncFnvAccumulateU32(fold, (u32)ep->effect_vars.shield.player);
		fold = syNetSyncFnvAccumulateU32(fold, (u32)(ep->effect_vars.shield.is_damage_shield != FALSE));
	}
	if ((ep->proc_update == gcPlayAnimAll) && (ep->fighter_gobj != NULL))
	{
		FTStruct *fp_halo;

		fp_halo = ftGetStruct(ep->fighter_gobj);
		if (fp_halo != NULL)
		{
			rebirth_halo_effect = syNetSyncEffectIsRebirthHaloCoupling(gobj, ep, fp_halo);
			if (rebirth_halo_effect != FALSE)
			{
				fold = syNetSyncFnvAccumulateU32(fold, (u32)fp_halo->status_id);
				fold = syNetSyncFnvAccumulateU32(fold, (fp_halo->is_rebirth != FALSE) ? 1U : 0U);
				fold = syNetSyncFnvAccumulateU32(fold, (u32)ftStatusVarsRebirth(fp_halo)->halo_despawn_wait);
				fold = syNetSyncFnvAccumulateU32(fold, (u32)ftStatusVarsRebirth(fp_halo)->halo_lower_wait);
			}
			else if (((fp_halo->fkind == nFTKindNess) || (fp_halo->fkind == nFTKindNNess)) &&
			         (DObjGetStruct(gobj) != NULL) &&
			         (DObjGetStruct(gobj)->user_data.p == fp_halo->joints[5]))
			{
				switch (fp_halo->status_id)
				{
				case nFTNessStatusSpecialHiStart:
				case nFTNessStatusSpecialHiHold:
				case nFTNessStatusSpecialHiEnd:
				case nFTNessStatusSpecialHiJibaku:
				case nFTNessStatusSpecialAirHiStart:
				case nFTNessStatusSpecialAirHiHold:
				case nFTNessStatusSpecialAirHiEnd:
				case nFTNessStatusSpecialAirHiBound:
				case nFTNessStatusSpecialAirHiJibaku:
					ness_pkwave_effect = TRUE;
					fold = syNetSyncFnvAccumulateU32(fold, (u32)fp_halo->status_id);
					fold = syNetSyncFnvAccumulateU32(fold, (fp_halo->is_effect_attach != FALSE) ? 1U : 0U);
					break;

				default:
					break;
				}
			}
			else if (syNetplayLiveEffectIsNessPsychicMagnet(gobj, ep) != FALSE)
			{
				ness_psychic_magnet_effect = TRUE;
				fold = syNetSyncFnvAccumulateU32(fold, (u32)fp_halo->status_id);
				fold = syNetSyncFnvAccumulateU32(fold, (fp_halo->is_effect_attach != FALSE) ? 1U : 0U);
				fold = syNetSyncFnvAccumulateU32(fold, (fp_halo->is_absorb != FALSE) ? 1U : 0U);
			}
		}
	}
	if (ep->proc_update == efManagerVelAddDestroyAnimEnd)
	{
		ness_shock_effect = TRUE;
	}
	fold = syNetSyncFnvAccumulateU32(fold, (u32)ep->effect_vars.quake.priority);
	dobj = DObjGetStruct(gobj);
	if ((dobj != NULL) && (rebirth_halo_effect == FALSE) && (ness_pkwave_effect == FALSE) &&
	    (ness_psychic_magnet_effect == FALSE) && (ness_shock_effect == FALSE))
	{
		Vec3f pos = dobj->translate.vec.f;

		fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.x));
		fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.y));
		fold = syNetSyncFnvAccumulateU32(fold, syNetSyncHashF32(pos.z));
	}
	*fold_out = fold;
	return TRUE;
}

u32 syNetSyncHashActiveEffectsForRollback(void)
{
	GObj *sorted[SYNET_SYNC_EFFECT_HASH_SORT_MAX];
	s32 count;
	u32 hash;
	s32 i;
	sb32 truncated;

	truncated = FALSE;
	count = syNetRbEnumerateActiveEffectsSorted(sorted, SYNET_SYNC_EFFECT_HASH_SORT_MAX, &truncated);
	{
		static sb32 sSYNetSyncEffectHashTruncationLogged = FALSE;

		if ((truncated != FALSE) && (sSYNetSyncEffectHashTruncationLogged == FALSE))
		{
			port_log("SSB64 NetSync: effect hash truncated at max=%d (snapshot cap; save will fail if overflow)\n",
			         SYNET_SYNC_EFFECT_HASH_SORT_MAX);
			sSYNetSyncEffectHashTruncationLogged = TRUE;
		}
	}
	hash = 2166136261U;
	for (i = 0; i < count; i++)
	{
		u32 fold;

		if (syNetSyncFoldSingleEffectGObj(sorted[i], &fold) != FALSE)
		{
			hash ^= fold;
			hash = syNetSyncFnvAccumulateU32(hash, 0xA5A5A5A5U);
		}
	}
	return hash;
}

static sb32 syNetSyncEffectFoldDiagEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_EFFECT_FOLD_DIAG");
	s_env_cache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}

/*
 * Per-effect breakdown of the eff rollback hash fold. Mirrors syNetSyncHashActiveEffectsForRollback
 * field-for-field so a save vs verify-load comparison pins the exact divergent component
 * (bank/respawn/parent id/anim_frame/pos/special). Self-gated on SSB64_NETPLAY_EFFECT_FOLD_DIAG.
 */
void syNetSyncLogActiveEffectsFoldDiag(const char *tag, u32 tick)
{
	GObj *sorted[SYNET_SYNC_EFFECT_HASH_SORT_MAX];
	s32 count;
	s32 i;
	sb32 truncated;

	if (syNetSyncEffectFoldDiagEnabled() == FALSE)
	{
		return;
	}
	truncated = FALSE;
	count = syNetRbEnumerateActiveEffectsSorted(sorted, SYNET_SYNC_EFFECT_HASH_SORT_MAX, &truncated);
	port_log("SSB64 NetSync: eff_fold_diag tag=%s tick=%u count=%d truncated=%d hash=0x%08X\n",
	         (tag != NULL) ? tag : "?", tick, (int)count, (int)truncated,
	         syNetSyncHashActiveEffectsForRollback());
	for (i = 0; i < count; i++)
	{
		GObj *gobj = sorted[i];
		EFStruct *ep;
		DObj *dobj;
		Vec3f pos;
		u32 respawn_kind;
		u32 parent_id;
		u32 shield_player;
		u32 shield_dmg;
		u32 special_tag;

		pos.x = 0.0F;
		pos.y = 0.0F;
		pos.z = 0.0F;
		ep = efGetStruct(gobj);
		dobj = DObjGetStruct(gobj);
		if (dobj != NULL)
		{
			pos = dobj->translate.vec.f;
		}
		if (ep == NULL)
		{
			port_log("SSB64 NetSync: eff_fold_diag tag=%s tick=%u idx=%d gobj_id=%u no_struct link_id=%u "
			         "obj_kind=%u anim_frame=0x%08X pos=(0x%08X,0x%08X,0x%08X)\n",
			         (tag != NULL) ? tag : "?", tick, (int)i, (unsigned int)gobj->id, (u32)gobj->link_id,
			         (u32)gobj->obj_kind, syNetSyncHashF32(gobj->anim_frame), syNetSyncHashF32(pos.x),
			         syNetSyncHashF32(pos.y), syNetSyncHashF32(pos.z));
			continue;
		}
		respawn_kind = (u32)syNetRbSnapEffectRespawnKindFromLive(gobj, ep);
		parent_id = (ep->fighter_gobj != NULL) ? (u32)ep->fighter_gobj->id : 0U;
		shield_player = 0xFFFFFFFFU;
		shield_dmg = 0U;
		special_tag = 0U;
		if (ep->proc_update == efManagerShieldProcUpdate)
		{
			shield_player = (u32)ep->effect_vars.shield.player;
			shield_dmg = (u32)(ep->effect_vars.shield.is_damage_shield != FALSE);
			special_tag = 1U;
		}
		else if (ep->proc_update == efManagerFoxReflectorProcUpdate)
		{
			special_tag = 2U;
		}
		port_log("SSB64 NetSync: eff_fold_diag tag=%s tick=%u idx=%d gobj_id=%u bank=%u respawn=%u parent_id=%u "
		         "is_pause=%u anim_frame=0x%08X quake_pri=%u shield_player=%d shield_dmg=%u special=%u "
		         "pos=(0x%08X,0x%08X,0x%08X)\n",
		         (tag != NULL) ? tag : "?", tick, (int)i, (unsigned int)gobj->id, (u32)ep->bank_id, respawn_kind,
		         parent_id, (ep->is_pause_effect != FALSE) ? 1U : 0U, syNetSyncHashF32(gobj->anim_frame),
		         (u32)ep->effect_vars.quake.priority, (s32)shield_player, shield_dmg, special_tag,
		         syNetSyncHashF32(pos.x), syNetSyncHashF32(pos.y), syNetSyncHashF32(pos.z));
	}
}
#endif

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
	hash = syNetSyncFnvAccumulateU32(hash, (u32)gGMCameraStruct.status_curr);
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(gGMCameraStruct.pzoom_dist));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(gGMCameraStruct.pfollow_dist));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(gGMCameraStruct.pfollow_eye_x));
	hash = syNetSyncFnvAccumulateU32(hash, syNetSyncHashF32(gGMCameraStruct.pfollow_eye_y));
	if (gGMCameraStruct.pzoom_fighter_gobj != NULL)
	{
		FTStruct *pzoom_fp = ftGetStruct(gGMCameraStruct.pzoom_fighter_gobj);

		if (pzoom_fp != NULL)
		{
			hash = syNetSyncFnvAccumulateU32(hash, (u32)pzoom_fp->player);
		}
	}
	if (gGMCameraStruct.pfollow_fighter_gobj != NULL)
	{
		FTStruct *pfollow_fp = ftGetStruct(gGMCameraStruct.pfollow_fighter_gobj);

		if (pfollow_fp != NULL)
		{
			hash = syNetSyncFnvAccumulateU32(hash, (u32)pfollow_fp->player);
		}
	}
	return hash;
}

/* Full-chain diagnostic fold (all AObj nodes; no rollback cap). See `syNetSyncHashFighterAnimationState`. */
static u32 syNetSyncFoldFighterAnimationStateDiagnostic(const FTStruct *fp, GObj *fighter_gobj)
{
	u32 fold = 2166136261U;
	s32 ji;

	if ((fp == NULL) || (fighter_gobj == NULL))
	{
		return fold;
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
	return fold;
}

u32 syNetSyncHashFighterAnimationState(void)
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
		FTStruct *fp = ftGetStruct(fighter_gobj);
		u32 fold;
		s32 slot;

		if (fp == NULL)
		{
			continue;
		}
		fold = syNetSyncFoldFighterAnimationStateDiagnostic(fp, fighter_gobj);
		slot = fp->player;

		if ((slot >= 0) && (slot < GMCOMMON_PLAYERS_MAX))
		{
			slot_hash[slot] =
			    syNetSyncFnvAccumulateU32(slot_hash[slot] ^ fold, (u32)slot ^ 0x9E3779B9U);
		}
		else
		{
			slot_hash[0] = syNetSyncFnvAccumulateU32(slot_hash[0] ^ fold, (u32)slot ^ 0x85EBCA77U);
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

u32 syNetSyncHashFighterAnimationStateForRollback(void)
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
		FTStruct *fp = ftGetStruct(fighter_gobj);
		u32 fold;
		s32 slot;

		if (fp == NULL)
		{
			continue;
		}
		fold = syNetSyncFoldFighterAnimRollback(fp, fighter_gobj);
		slot = fp->player;

		if ((slot >= 0) && (slot < GMCOMMON_PLAYERS_MAX))
		{
			slot_hash[slot] =
			    syNetSyncFnvAccumulateU32(slot_hash[slot] ^ fold, (u32)slot ^ 0x9E3779B9U);
		}
		else
		{
			slot_hash[0] = syNetSyncFnvAccumulateU32(slot_hash[0] ^ fold, (u32)slot ^ 0x85EBCA77U);
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
