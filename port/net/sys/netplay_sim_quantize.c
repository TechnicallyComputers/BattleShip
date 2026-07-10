#include <sys/netplay_sim_quantize.h>
#include <sys/netplay_fox_firefox_gate.h>
#include <sys/netplay_ness_pkthunder_gate.h>
#include <sys/netplay_pikachu_quickattack_gate.h>
#include <sys/netinput.h>

#include <sys/netpeer.h>
#include <sys/netrollback.h>
#include <sys/netrollbacksnapshot.h>
#include <sys/obj.h>
#include <sys/objtypes.h>
#include <ft/fighter.h>
#include <ft/ftchar/ftpikachu/ftpikachu.h>
#include <mp/map.h>
#include <mp/mpdef.h>

#include <lb/lbcommon.h>

#include <ef/efmanager.h>
#include <ef/efvars.h>

#include <ft/ftchar/ftcaptain/ftcaptain.h>
#include <ft/ftchar/ftfox/ftfox.h>
#include <ft/ftchar/ftkirby/ftkirby.h>
#include <ft/ftchar/ftlink/ftlink.h>
#include <ft/ftchar/ftness/ftness.h>
#include <ft/ftchar/ftyoshi/ftyoshi.h>
#include <ft/ftdef.h>
#include <ft/ftstatusvars.h>
#include <it/item.h>
#include <wp/weapon.h>
#include <wp/wpdef.h>
#include <wp/wpvars.h>
#include <wp/wpness/wpnesspkthunder.h>

#include <sc/scdef.h>
#include <sc/scmanager.h>
#include <sc/sctypes.h>

#include <gr/grdef.h>
#include <gr/grcommon/grsector.h>
#include <sys/objman.h>
#include <sys/objdef.h>
#include <gm/gmcamera.h>

#include <ef/effect.h>
#include <sys/objanim.h>
#include <sys/objman.h>

#include <macros.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);

static sb32 sSYNetplaySimQuantizeEnvCache = -999;

static sb32 syNetplaySimQuantizeEnvEnabled(void)
{
	const char *env;

	if (sSYNetplaySimQuantizeEnvCache != -999)
	{
		return sSYNetplaySimQuantizeEnvCache;
	}
	env = getenv("SSB64_NETPLAY_SIM_F32_QUANTIZE");
	if (env == NULL)
	{
		sSYNetplaySimQuantizeEnvCache = TRUE;
		return TRUE;
	}
	sSYNetplaySimQuantizeEnvCache = (atoi(env) != 0) ? TRUE : FALSE;
	return sSYNetplaySimQuantizeEnvCache;
}

sb32 syNetplayRollbackSemanticsActive(void)
{
#if defined(PORT) && defined(SSB64_NETMENU)
	if (syNetPeerIsVSSessionActive() != FALSE)
	{
		return TRUE;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return TRUE;
	}
	return FALSE;
#else
	return FALSE;
#endif
}

sb32 syNetplayRollbackLiveForwardSimEligible(void)
{
#if defined(PORT) && defined(SSB64_NETMENU)
	SCBattleState *battle;

	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return FALSE;
	}
	battle = gSCManagerBattleState;
	if ((battle != NULL) && (battle->game_type == nSCBattleGameTypeTraining))
	{
		return FALSE;
	}
	return TRUE;
#else
	return FALSE;
#endif
}

sb32 syNetplaySimQuantizeActive(void)
{
#if defined(SSB64_NETMENU)
	if (syNetplaySimQuantizeEnvEnabled() == FALSE)
	{
		return FALSE;
	}
	return syNetplayRollbackSemanticsActive();
#else
	return FALSE;
#endif
}

static f32 syNetplayQuantizeF32Grid(f32 value)
{
	f64 scaled;
	f32 result;

	scaled = (f64)value * 65536.0;
	if (scaled >= 0.0)
	{
		scaled = floor(scaled + 0.5);
	}
	else
	{
		scaled = ceil(scaled - 0.5);
	}
	result = (f32)(scaled / 65536.0);
	return (result == 0.0F) ? 0.0F : result;
}

f32 syNetplayQuantizeF32(f32 value)
{
	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return value;
	}
	return syNetplayQuantizeF32Grid(value);
}

f32 syNetplayQuantizeF32ForRollbackHash(f32 value)
{
#if defined(SSB64_NETMENU)
	return syNetplayQuantizeF32Grid(value);
#else
	return value;
#endif
}

f32 syNetplayQuantizeAnimScalar(f32 value)
{
	if (value == AOBJ_ANIM_NULL)
	{
		return value;
	}
	return syNetplayQuantizeF32(value);
}

void syNetplayQuantizeVec3f(Vec3f *vec)
{
	if ((vec == NULL) || (syNetplaySimQuantizeActive() == FALSE))
	{
		return;
	}
	vec->x = syNetplayQuantizeF32(vec->x);
	vec->y = syNetplayQuantizeF32(vec->y);
	vec->z = syNetplayQuantizeF32(vec->z);
}

void syNetplayQuantizeVec3fInto(Vec3f *dst, const Vec3f *src)
{
	if ((dst == NULL) || (src == NULL))
	{
		return;
	}
	dst->x = syNetplayQuantizeF32(src->x);
	dst->y = syNetplayQuantizeF32(src->y);
	dst->z = syNetplayQuantizeF32(src->z);
}

void syNetplayQuantizeDObjAnimScalars(DObj *dobj)
{
	if ((dobj == NULL) || (syNetplaySimQuantizeActive() == FALSE))
	{
		return;
	}
	dobj->anim_frame = syNetplayQuantizeAnimScalar(dobj->anim_frame);
	dobj->anim_wait = syNetplayQuantizeAnimScalar(dobj->anim_wait);
	dobj->anim_speed = syNetplayQuantizeAnimScalar(dobj->anim_speed);
	if (dobj->parent_gobj != NULL)
	{
		dobj->parent_gobj->anim_frame = dobj->anim_frame;
	}
}

void syNetplayQuantizeDObjTranslate(DObj *dobj)
{
	if ((dobj == NULL) || (syNetplaySimQuantizeActive() == FALSE))
	{
		return;
	}
	syNetplayQuantizeVec3f(&dobj->translate.vec.f);
}

void syNetplayQuantizeDObjRotate(DObj *dobj)
{
	if ((dobj == NULL) || (syNetplaySimQuantizeActive() == FALSE))
	{
		return;
	}
	syNetplayQuantizeVec3f(&dobj->rotate.vec.f);
}

void syNetplayQuantizeDObjScale(DObj *dobj)
{
	if ((dobj == NULL) || (syNetplaySimQuantizeActive() == FALSE))
	{
		return;
	}
	syNetplayQuantizeVec3f(&dobj->scale.vec.f);
}

void syNetplayQuantizeDObjAnimPose(DObj *dobj)
{
	syNetplayQuantizeDObjTranslate(dobj);
	syNetplayQuantizeDObjRotate(dobj);
	syNetplayQuantizeDObjScale(dobj);
}

void syNetplayQuantizeDObjAObjChain(DObj *dobj)
{
	AObj *aobj;

	if ((dobj == NULL) || (syNetplaySimQuantizeActive() == FALSE))
	{
		return;
	}
	/*
	 * Mirror syNetRbSnapCaptureAObjNode/syNetRbSnapApplyAObjNode exactly: snapshot capture and
	 * apply round these six AObj fields to the shared grid, but live forward sim (gcParseDObjAnimJoint
	 * sets value/rate from the figatree stream, gcPlayDObjAnimJoint accumulates length += anim_speed)
	 * never does. Quantizing them here keeps the live chain bit-identical to a snapshot-restored chain,
	 * so a resim replay continues the same interpolation track and the cross-ISA anim hash stops forking.
	 */
	for (aobj = dobj->aobj; aobj != NULL; aobj = aobj->next)
	{
		aobj->length_invert = syNetplayQuantizeF32(aobj->length_invert);
		aobj->length = syNetplayQuantizeF32(aobj->length);
		aobj->value_base = syNetplayQuantizeF32(aobj->value_base);
		aobj->value_target = syNetplayQuantizeF32(aobj->value_target);
		aobj->rate_base = syNetplayQuantizeF32(aobj->rate_base);
		aobj->rate_target = syNetplayQuantizeF32(aobj->rate_target);
	}
}

sb32 syNetplayFighterInIntroSimScope(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	if ((gSCManagerBattleState != NULL) && (gSCManagerBattleState->game_status == nSCBattleGameStatusWait))
	{
		return TRUE;
	}
	return (fp->status_id == nFTCommonStatusEntry) || (fp->status_id == nFTCommonStatusEntryNull) ||
	       (fp->status_id == nFTCommonStatusWait) ? TRUE : FALSE;
}

static sb32 syNetplayFighterInAppearSimScope(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	return syNetRbSnapshotStatusInAppearPresentationScope(fp->fkind, fp->status_id);
}

static void syNetplayCanonicalizeFighterIntroJointPose(GObj *fighter_gobj)
{
	FTStruct *fp;
	DObj *root_dobj;
	s32 ji;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || ((syNetplayFighterInIntroSimScope(fp) == FALSE) &&
	                     (syNetplayFighterInAppearSimScope(fp) == FALSE)))
	{
		return;
	}
	root_dobj = DObjGetStruct(fighter_gobj);
	if (root_dobj != NULL)
	{
		syNetplayQuantizeVec3f(&root_dobj->rotate.vec.f);
		syNetplayQuantizeVec3f(&root_dobj->scale.vec.f);
	}
	for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
	{
		if (fp->joints[ji] != NULL)
		{
			syNetplayQuantizeVec3f(&fp->joints[ji]->rotate.vec.f);
			syNetplayQuantizeVec3f(&fp->joints[ji]->scale.vec.f);
		}
	}
}

sb32 syNetplaySectorArwingIntroMapScopeActive(void)
{
	GObj *fighter_gobj;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return FALSE;
	}
	if ((gSCManagerBattleState == NULL) || (gSCManagerBattleState->gkind != nGRKindSector))
	{
		return FALSE;
	}
	if (gSCManagerBattleState->game_status == nSCBattleGameStatusWait)
	{
		return TRUE;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp != NULL) && (syNetplayFighterInIntroSimScope(fp) != FALSE))
		{
			return TRUE;
		}
	}
	return FALSE;
}

void syNetplayCanonicalizeSectorArwingIntroMapPose(void)
{
	static u32 s_sector_intro_canon_tick = 0xFFFFFFFFU;
	u32 tick;

	if (syNetplaySectorArwingIntroMapScopeActive() == FALSE)
	{
		return;
	}
	tick = syNetInputGetTick();
	if (tick == s_sector_intro_canon_tick)
	{
		return;
	}
	s_sector_intro_canon_tick = tick;
	grSectorArwingCanonicalizeSimState();
}

static sb32 syNetplayFighterInRebirthScope(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	return ((fp->status_id >= nFTCommonStatusRebirthDown) && (fp->status_id <= nFTCommonStatusRebirthWait))
	           ? TRUE
	           : FALSE;
}

sb32 syNetplayFighterInNessPKJibakuSimScope(const FTStruct *fp)
{
	if ((fp == NULL) || ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess)))
	{
		return FALSE;
	}
	return (fp->status_id == nFTNessStatusSpecialHiJibaku) || (fp->status_id == nFTNessStatusSpecialAirHiJibaku) ||
	       (fp->status_id == nFTNessStatusSpecialAirHiBound) ? TRUE : FALSE;
}

sb32 syNetplayFighterInNessPKThunderHoldSimScope(const FTStruct *fp)
{
	if ((fp == NULL) || ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess)))
	{
		return FALSE;
	}
	switch (fp->status_id)
	{
	case nFTNessStatusSpecialHiStart:
	case nFTNessStatusSpecialHiHold:
	case nFTNessStatusSpecialHiEnd:
	case nFTNessStatusSpecialAirHiStart:
	case nFTNessStatusSpecialAirHiHold:
	case nFTNessStatusSpecialAirHiEnd:
		return TRUE;
	default:
		return FALSE;
	}
}

static void syNetplayQuantizeNessPKThunderPos(Vec3f *pkthunder_pos)
{
	syNetplayQuantizeVec3f(pkthunder_pos);
}

static void syNetplayCanonicalizeNessPKThunderHoldWeaponsForFighter(GObj *fighter_gobj)
{
	FTStruct *fp;
	GObj *head_gobj;
	WPStruct *head_wp;
	s32 ti;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (syNetplayFighterInNessPKThunderHoldSimScope(fp) == FALSE))
	{
		return;
	}
	head_gobj = fp->status_vars.ness.specialhi.pkthunder_gobj;
	if ((head_gobj == NULL) || (wpNessPKThunderGObjIsLiveWeapon(head_gobj) == FALSE))
	{
		return;
	}
	syNetplayCanonicalizeNessPKThunderWeaponSimState(head_gobj);
	head_wp = wpGetStruct(head_gobj);
	if (head_wp == NULL)
	{
		return;
	}
	for (ti = 0; ti < WPPKTHUNDER_PARTS_COUNT; ti++)
	{
		GObj *trail_gobj = head_wp->weapon_vars.pkthunder.trail_gobj[ti];

		if (wpNessPKThunderGObjIsLiveWeapon(trail_gobj) != FALSE)
		{
			syNetplayCanonicalizeNessPKThunderWeaponSimState(trail_gobj);
		}
	}
}

void syNetplayQuantizePikachuQuickAttackStatusVars(FTStruct *fp, union FTStatusVars *status_vars)
{
	ftPikachuSpecialHiStatusVars *specialhi;

	if ((fp == NULL) || (status_vars == NULL) || (syNetplaySimQuantizeActive() == FALSE) ||
	    ((fp->fkind != nFTKindPikachu) && (fp->fkind != nFTKindNPikachu)) ||
	    (syNetplayPikachuFighterInQuickAttackScope(fp->status_id) == FALSE))
	{
		return;
	}
	specialhi = &status_vars->pikachu.specialhi;
	specialhi->stick_range.x = (s32)syNetplayQuantizeF32((f32)specialhi->stick_range.x);
	specialhi->stick_range.y = (s32)syNetplayQuantizeF32((f32)specialhi->stick_range.y);
	specialhi->vel_x_bak = syNetplayQuantizeF32(specialhi->vel_x_bak);
	specialhi->vel_y_bak = syNetplayQuantizeF32(specialhi->vel_y_bak);
	specialhi->vel_ground_bak = syNetplayQuantizeF32(specialhi->vel_ground_bak);
}

void syNetplayQuantizePikachuQuickAttackLandingStatusVars(FTStruct *fp, union FTStatusVars *status_vars)
{
	ftCommonFallSpecialStatusVars *fallspecial;

	if ((fp == NULL) || (status_vars == NULL) || (syNetplaySimQuantizeActive() == FALSE) ||
	    (syNetplayPikachuFighterInQuickAttackLandingFallScope(fp) == FALSE))
	{
		return;
	}
	fallspecial = &status_vars->common.fallspecial;
	fallspecial->drift = syNetplayQuantizeF32(fallspecial->drift);
	fallspecial->landing_lag = syNetplayQuantizeF32(fallspecial->landing_lag);
}

void syNetplayCanonicalizePikachuQuickAttackSimState(GObj *fighter_gobj)
{
	FTStruct *fp;
	DObj *base_joint;
	sb32 in_qa_scope;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || ((fp->fkind != nFTKindPikachu) && (fp->fkind != nFTKindNPikachu)))
	{
		return;
	}
	in_qa_scope = (syNetplayPikachuFighterInQuickAttackScope(fp->status_id) != FALSE) ||
	              (syNetplayPikachuFighterInQuickAttackLandingFallScope(fp) != FALSE);
	if (in_qa_scope == FALSE)
	{
		return;
	}
	syNetplayQuantizePikachuQuickAttackStatusVars(fp, &fp->status_vars);
	syNetplayQuantizePikachuQuickAttackLandingStatusVars(fp, &fp->status_vars);
	if ((fp->coll_data.floor_flags & MAP_VERTEX_COLL_PASS) != 0U)
	{
		syNetplayQuantizeFighterPhysics(&fp->physics);
		syNetplayQuantizeMPCollData(&fp->coll_data);
		if (fp->joints[nFTPartsJointTopN] != NULL)
		{
			syNetplayQuantizeDObjTranslate(fp->joints[nFTPartsJointTopN]);
		}
	}
	if (syNetplayPikachuFighterInQuickAttackZipScope(fp->status_id) == FALSE)
	{
		return;
	}
	base_joint = fp->joints[FTPIKACHU_QUICKATTACK_BASE_JOINT];
	if (base_joint == NULL)
	{
		return;
	}
	base_joint->rotate.vec.f.x = syNetplayQuantizeF32(base_joint->rotate.vec.f.x);
	base_joint->scale.vec.f.x = syNetplayQuantizeF32(base_joint->scale.vec.f.x);
	base_joint->scale.vec.f.y = syNetplayQuantizeF32(base_joint->scale.vec.f.y);
	base_joint->scale.vec.f.z = syNetplayQuantizeF32(base_joint->scale.vec.f.z);
}

void syNetplayCanonicalizeFoxFirefoxSimState(GObj *fighter_gobj)
{
	FTStruct *fp;
	DObj *root_dobj;
	s32 ji;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (fp->fkind != nFTKindFox) || (syNetplayFoxFighterInResimPresentationScope(fp) == FALSE))
	{
		return;
	}
	root_dobj = DObjGetStruct(fighter_gobj);
	if (root_dobj != NULL)
	{
		syNetplayQuantizeVec3f(&root_dobj->rotate.vec.f);
		syNetplayQuantizeVec3f(&root_dobj->scale.vec.f);
	}
	for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
	{
		if (fp->joints[ji] != NULL)
		{
			syNetplayQuantizeVec3f(&fp->joints[ji]->rotate.vec.f);
			syNetplayQuantizeVec3f(&fp->joints[ji]->scale.vec.f);
			syNetplayQuantizeDObjTranslate(fp->joints[ji]);
		}
	}
	if (syNetplayFoxFighterInFirefoxSynctestDeferScope(fp->status_id) != FALSE)
	{
		syNetplayFoxSanitizeFirefoxStatusVars(fp);
		syNetplayQuantizeFighterPhysics(&fp->physics);
		syNetplayQuantizeMPCollData(&fp->coll_data);
	}
}

void syNetplayQuantizeNessPKJibakuStatusVars(FTStruct *fp, union FTStatusVars *status_vars)
{
	if ((fp == NULL) || (status_vars == NULL) || (syNetplaySimQuantizeActive() == FALSE) ||
	    (syNetplayFighterInNessPKJibakuSimScope(fp) == FALSE))
	{
		return;
	}
	status_vars->ness.specialhi.pkjibaku_angle =
	    syNetplayQuantizeF32(status_vars->ness.specialhi.pkjibaku_angle);
	syNetplayQuantizeNessPKThunderPos(&status_vars->ness.specialhi.pkthunder_pos);
}

void syNetplayQuantizeNessPKThunderHoldStatusVars(FTStruct *fp, union FTStatusVars *status_vars)
{
	if ((fp == NULL) || (status_vars == NULL) || (syNetplaySimQuantizeActive() == FALSE) ||
	    (syNetplayFighterInNessPKThunderHoldSimScope(fp) == FALSE))
	{
		return;
	}
	syNetplayQuantizeNessPKThunderPos(&status_vars->ness.specialhi.pkthunder_pos);
}

void syNetplayQuantizeNessPKThunderLandingStatusVars(FTStruct *fp, union FTStatusVars *status_vars)
{
	ftCommonFallSpecialStatusVars *fallspecial;

	if ((fp == NULL) || (status_vars == NULL) || (syNetplaySimQuantizeActive() == FALSE) ||
	    (syNetplayNessFighterInPKThunderLandingFallScope(fp) == FALSE))
	{
		return;
	}
	fallspecial = &status_vars->common.fallspecial;
	fallspecial->drift = syNetplayQuantizeF32(fallspecial->drift);
	fallspecial->landing_lag = syNetplayQuantizeF32(fallspecial->landing_lag);
}

void syNetplayQuantizeNessPKThunderHoldPassiveVars(FTStruct *fp, FTNessPassiveVars *passive)
{
	s32 ti;

	if ((fp == NULL) || (passive == NULL) || (syNetplaySimQuantizeActive() == FALSE) ||
	    (syNetplayFighterInNessPKThunderHoldSimScope(fp) == FALSE))
	{
		return;
	}
	for (ti = 0; ti < FTNESS_PKTHUNDER_TRAIL_POS_COUNT; ti++)
	{
		passive->pkthunder_trail_x[ti] = (s16)syNetplayQuantizeF32((f32)passive->pkthunder_trail_x[ti]);
		passive->pkthunder_trail_y[ti] = (s16)syNetplayQuantizeF32((f32)passive->pkthunder_trail_y[ti]);
	}
}

void syNetplayNessHardenPKJibakuAirVelFromAngle(GObj *fighter_gobj)
{
	FTStruct *fp;
	f32 angle;
	f32 mag;
	Vec3f *vel;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (syNetplayFighterInNessPKJibakuSimScope(fp) == FALSE))
	{
		return;
	}
	if (fp->status_id != nFTNessStatusSpecialAirHiJibaku)
	{
		return;
	}
	angle = syNetplayQuantizeF32(fp->status_vars.ness.specialhi.pkjibaku_angle);
	fp->status_vars.ness.specialhi.pkjibaku_angle = angle;
	vel = &fp->physics.vel_air;
	mag = lbCommonMag2D(vel);
	mag = syNetplayQuantizeF32(mag);
	vel->x = syNetplayQuantizeF32(__cosf(angle) * mag * fp->lr);
	vel->y = syNetplayQuantizeF32(__sinf(angle) * mag);
	vel->z = 0.0F;
}

void syNetplayCanonicalizeNessPKJibakuSimState(GObj *fighter_gobj)
{
	FTStruct *fp;
	DObj *pitch_joint;
	DObj *root_dobj;
	s32 ji;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (syNetplayFighterInNessPKJibakuSimScope(fp) == FALSE))
	{
		return;
	}
	fp->status_vars.ness.specialhi.pkjibaku_angle =
	    syNetplayQuantizeF32(fp->status_vars.ness.specialhi.pkjibaku_angle);
	syNetplayQuantizeNessPKThunderPos(&fp->status_vars.ness.specialhi.pkthunder_pos);
	syNetplayQuantizeFighterPhysics(&fp->physics);
	syNetplayQuantizeMPCollData(&fp->coll_data);
	root_dobj = DObjGetStruct(fighter_gobj);
	if (root_dobj != NULL)
	{
		syNetplayQuantizeDObjTranslate(root_dobj);
	}
	for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
	{
		if (fp->joints[ji] != NULL)
		{
			syNetplayQuantizeDObjTranslate(fp->joints[ji]);
			syNetplayQuantizeDObjRotate(fp->joints[ji]);
		}
	}
	pitch_joint = fp->joints[4];
	if (pitch_joint != NULL)
	{
		pitch_joint->rotate.vec.f.x = syNetplayQuantizeF32(pitch_joint->rotate.vec.f.x);
	}
}

void syNetplayCanonicalizeNessPKThunderWeaponSimState(GObj *weapon_gobj)
{
	WPStruct *wp;
	FTStruct *fp;
	DObj *dobj;
	f32 angle;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	if (weapon_gobj == NULL)
	{
		return;
	}
	wp = wpGetStruct(weapon_gobj);
	if ((wp == NULL) || (wp->owner_gobj == NULL))
	{
		return;
	}
	fp = ftGetStruct(wp->owner_gobj);
	if ((fp == NULL) || (syNetplayFighterInNessPKThunderHoldSimScope(fp) == FALSE))
	{
		return;
	}
	dobj = DObjGetStruct(weapon_gobj);
	if (wp->kind == nWPKindPKThunderHead)
	{
		angle = syNetplayQuantizeF32(wp->weapon_vars.pkthunder.angle);
		wp->weapon_vars.pkthunder.angle = angle;
		if ((fp->status_id == nFTNessStatusSpecialHiHold) || (fp->status_id == nFTNessStatusSpecialAirHiHold))
		{
			wp->physics.vel_air.x = syNetplayQuantizeF32(__cosf(angle) * WPPKTHUNDER_VEL);
			wp->physics.vel_air.y = syNetplayQuantizeF32(__sinf(angle) * WPPKTHUNDER_VEL);
			wp->physics.vel_air.z = 0.0F;
		}
		else
		{
			syNetplayQuantizeVec3f(&wp->physics.vel_air);
		}
		if (dobj != NULL)
		{
			syNetplayQuantizeDObjTranslate(dobj);
			syNetplayQuantizeDObjRotate(dobj);
		}
	}
	else if (wp->kind == nWPKindPKThunderTrail)
	{
		if (dobj != NULL)
		{
			syNetplayQuantizeDObjTranslate(dobj);
			syNetplayQuantizeDObjRotate(dobj);
		}
	}
}

static void syNetplayCanonicalizeNessPKThunderHoldFighterPose(GObj *fighter_gobj)
{
	FTStruct *fp;
	DObj *root_dobj;
	s32 ji;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (syNetplayFighterInNessPKThunderHoldSimScope(fp) == FALSE))
	{
		return;
	}
	syNetplayQuantizeFighterPhysics(&fp->physics);
	root_dobj = DObjGetStruct(fighter_gobj);
	if (root_dobj != NULL)
	{
		syNetplayQuantizeDObjAnimPose(root_dobj);
		fighter_gobj->anim_frame = syNetplayQuantizeAnimScalar(fighter_gobj->anim_frame);
		root_dobj->anim_frame = fighter_gobj->anim_frame;
		syNetplayQuantizeDObjAnimScalars(root_dobj);
	}
	for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
	{
		if (fp->joints[ji] != NULL)
		{
			syNetplayQuantizeDObjAnimPose(fp->joints[ji]);
			syNetplayQuantizeDObjAnimScalars(fp->joints[ji]);
		}
	}
}

void syNetplayCanonicalizeNessPKThunderHoldSimState(GObj *fighter_gobj)
{
	FTStruct *fp;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (syNetplayFighterInNessPKThunderHoldSimScope(fp) == FALSE))
	{
		return;
	}
	syNetplayQuantizeNessPKThunderPos(&fp->status_vars.ness.specialhi.pkthunder_pos);
	syNetplayQuantizeNessPKThunderHoldPassiveVars(fp, &fp->passive_vars.ness);
	syNetplayQuantizeFighterPhysics(&fp->physics);
	syNetplayQuantizeMPCollData(&fp->coll_data);
	syNetplayCanonicalizeNessPKThunderHoldFighterPose(fighter_gobj);
	syNetplayCanonicalizeNessPKThunderHoldWeaponsForFighter(fighter_gobj);
}

sb32 syNetplayFighterInNessPKWaveSimScope(const FTStruct *fp)
{
	if ((fp == NULL) || ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess)))
	{
		return FALSE;
	}
	if ((fp->status_id == nFTNessStatusSpecialHiStart) || (fp->status_id == nFTNessStatusSpecialHiHold) ||
	    (fp->status_id == nFTNessStatusSpecialAirHiStart) || (fp->status_id == nFTNessStatusSpecialAirHiHold))
	{
		return TRUE;
	}
	return FALSE;
}

sb32 syNetplayLiveEffectIsNessPKWave(const GObj *effect_gobj, const EFStruct *ep)
{
	FTStruct *fp;
	DObj *dobj;

	if ((effect_gobj == NULL) || (ep == NULL) || (ep->fighter_gobj == NULL) || (ep->proc_update != gcPlayAnimAll))
	{
		return FALSE;
	}
	fp = ftGetStruct(ep->fighter_gobj);
	if ((fp == NULL) || (syNetplayFighterInNessPKWaveSimScope(fp) == FALSE))
	{
		return FALSE;
	}
	dobj = DObjGetStruct((GObj *)effect_gobj);
	if ((dobj == NULL) || (fp->joints[5] == NULL))
	{
		return FALSE;
	}
	dobj->user_data.p = fp->joints[5];
	return TRUE;
}

sb32 syNetplayFighterInNessSpecialLwSimScope(const FTStruct *fp)
{
	if ((fp == NULL) || ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess)))
	{
		return FALSE;
	}
	if ((fp->status_id >= nFTNessStatusSpecialLwScopeStart) && (fp->status_id <= nFTNessStatusSpecialLwScopeEnd))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetplayFighterInNessPsychicMagnetSimScope(const FTStruct *fp)
{
	if (syNetplayFighterInNessSpecialLwSimScope(fp) == FALSE)
	{
		return FALSE;
	}
	switch (fp->status_id)
	{
	case nFTNessStatusSpecialLwHold:
	case nFTNessStatusSpecialAirLwHold:
	case nFTNessStatusSpecialLwHit:
	case nFTNessStatusSpecialAirLwHit:
		return TRUE;

	default:
		break;
	}
	return FALSE;
}

sb32 syNetplayLiveEffectIsNessPsychicMagnet(const GObj *effect_gobj, const EFStruct *ep)
{
	FTStruct *fp;
	DObj *dobj;

	if ((effect_gobj == NULL) || (ep == NULL) || (ep->fighter_gobj == NULL) || (ep->proc_update != gcPlayAnimAll))
	{
		return FALSE;
	}
	fp = ftGetStruct(ep->fighter_gobj);
	if ((fp == NULL) || (syNetplayFighterInNessPsychicMagnetSimScope(fp) == FALSE))
	{
		return FALSE;
	}
	dobj = DObjGetStruct((GObj *)effect_gobj);
	if ((dobj == NULL) || (fp->joints[nFTPartsJointTopN] == NULL))
	{
		return FALSE;
	}
	/* Repin TopN before identity check (PK wave repin path must not run on this shell). */
	dobj->user_data.p = fp->joints[nFTPartsJointTopN];
	return TRUE;
}

static sb32 syNetplayFighterHasLiveNessPsychicMagnet(GObj *fighter_gobj)
{
	s32 pass;

	if (fighter_gobj == NULL)
	{
		return FALSE;
	}
	for (pass = 0; pass < 2; pass++)
	{
		GObj *gobj;

		for (gobj = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		     gobj != NULL; gobj = gobj->link_next)
		{
			EFStruct *ep = efGetStruct(gobj);

			if ((syNetplayLiveEffectIsNessPsychicMagnet(gobj, ep) != FALSE) && (ep->fighter_gobj == fighter_gobj))
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

void syNetplayEnsureNessPsychicMagnetEffect(GObj *fighter_gobj)
{
	FTStruct *fp;

	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (syNetplayFighterInNessPsychicMagnetSimScope(fp) == FALSE))
	{
		return;
	}
	if (syNetplayFighterHasLiveNessPsychicMagnet(fighter_gobj) != FALSE)
	{
		return;
	}
	/*
	 * Vanilla InitVars only mints the bubble when is_effect_attach is FALSE. Rebirth halo / PK wave /
	 * snapshot restore can leave attach TRUE without a live magnet shell, so absorb gameplay runs
	 * (glow + sfx) but the PsychicMagnet GObj never spawns.
	 */
	if (efManagerNessPsychicMagnetMakeEffect(fighter_gobj) != NULL)
	{
		fp->is_effect_attach = TRUE;
	}
}

static DObj *syNetplayNessPsychicMagnetAnimDObj(DObj *root_dobj)
{
	if (root_dobj == NULL)
	{
		return NULL;
	}
	return (root_dobj->child != NULL) ? root_dobj->child : root_dobj;
}

static void syNetplayCanonicalizeNessPsychicMagnetEffectsForFighter(GObj *fighter_gobj)
{
	FTStruct *fp;
	s32 pass;
	GObj *gobj;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (syNetplayFighterInNessSpecialLwSimScope(fp) == FALSE))
	{
		return;
	}
	for (pass = 0; pass < 2; pass++)
	{
		for (gobj = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect]; gobj != NULL;
		     gobj = gobj->link_next)
		{
			EFStruct *ep;
			DObj *root_dobj;
			DObj *anim_dobj;

			ep = efGetStruct(gobj);
			if ((syNetplayLiveEffectIsNessPsychicMagnet(gobj, ep) == FALSE) || (ep->fighter_gobj != fighter_gobj))
			{
				continue;
			}
			root_dobj = DObjGetStruct(gobj);
			anim_dobj = syNetplayNessPsychicMagnetAnimDObj(root_dobj);
			if (anim_dobj == NULL)
			{
				continue;
			}
			/*
			 * AnimJoint lives on the child DObj (tree shell under TopN coupling). Quantize only the
			 * anim cursor/scalars on that node — not root translate/rotate: pose follows the fighter
			 * joint and mid-tick pose pinning froze the bubble on one frame (soak2 @405+).
			 */
			anim_dobj->anim_frame = syNetplayQuantizeAnimScalar(anim_dobj->anim_frame);
			gobj->anim_frame = anim_dobj->anim_frame;
			syNetplayQuantizeDObjAnimScalars(anim_dobj);
		}
	}
}

static void syNetplayCanonicalizeNessSpecialLwFighterPose(GObj *fighter_gobj)
{
	FTStruct *fp;
	DObj *root_dobj;
	s32 ji;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (syNetplayFighterInNessSpecialLwSimScope(fp) == FALSE))
	{
		return;
	}
	syNetplayQuantizeFighterPhysics(&fp->physics);
	root_dobj = DObjGetStruct(fighter_gobj);
	if (root_dobj != NULL)
	{
		syNetplayQuantizeDObjAnimPose(root_dobj);
		fighter_gobj->anim_frame = syNetplayQuantizeAnimScalar(fighter_gobj->anim_frame);
		root_dobj->anim_frame = fighter_gobj->anim_frame;
		syNetplayQuantizeDObjAnimScalars(root_dobj);
	}
	for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
	{
		if (fp->joints[ji] != NULL)
		{
			syNetplayQuantizeDObjAnimPose(fp->joints[ji]);
			syNetplayQuantizeDObjAnimScalars(fp->joints[ji]);
		}
	}
}

void syNetplayCanonicalizeNessSpecialLwProcUpdateState(GObj *fighter_gobj)
{
	FTStruct *fp;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (syNetplayFighterInNessSpecialLwSimScope(fp) == FALSE))
	{
		return;
	}
	/*
	 * Mid-tick (Hold/Hit ProcUpdate): fighter pose + camera only. Magnet effect anim runs via
	 * gcPlayAnimAll on the effect link after fighters; pinning effect pose/anim here froze the
	 * bubble loop. End-of-tick canonicalize applies magnet anim scalars once gcPlayAnimAll advances.
	 */
	syNetplayCanonicalizeNessSpecialLwFighterPose(fighter_gobj);
	syNetplayEnsureNessPsychicMagnetEffect(fighter_gobj);
	if (fp->is_absorb != FALSE)
	{
		syNetplayCanonicalizeGMCameraSimState();
	}
}

void syNetplayCanonicalizeNessSpecialLwSimState(GObj *fighter_gobj)
{
	FTStruct *fp;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (syNetplayFighterInNessSpecialLwSimScope(fp) == FALSE))
	{
		return;
	}
	syNetplayCanonicalizeNessSpecialLwFighterPose(fighter_gobj);
	syNetplayEnsureNessPsychicMagnetEffect(fighter_gobj);
	syNetplayCanonicalizeNessPsychicMagnetEffectsForFighter(fighter_gobj);
	if (fp->is_absorb != FALSE)
	{
		syNetplayCanonicalizeGMCameraSimState();
	}
}

void syNetplayCanonicalizeNessPKJibakuLaunchState(GObj *fighter_gobj)
{
	FTStruct *fp;
	f32 angle;
	f32 lr;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp == NULL)
	{
		return;
	}
	if ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess))
	{
		return;
	}
	if (fp->status_id == nFTNessStatusSpecialAirHiJibaku)
	{
		angle = syNetplayQuantizeF32(fp->status_vars.ness.specialhi.pkjibaku_angle);
		fp->status_vars.ness.specialhi.pkjibaku_angle = angle;
		syNetplayQuantizeNessPKThunderPos(&fp->status_vars.ness.specialhi.pkthunder_pos);
		lr = fp->lr;
		fp->physics.vel_air.x = syNetplayQuantizeF32(__cosf(angle) * FTNESS_PKJIBAKU_VEL * lr);
		fp->physics.vel_air.y = syNetplayQuantizeF32(__sinf(angle) * FTNESS_PKJIBAKU_VEL);
		syNetplayCanonicalizeNessPKJibakuSimState(fighter_gobj);
	}
	else if (fp->status_id == nFTNessStatusSpecialHiJibaku)
	{
		syNetplayQuantizeNessPKThunderPos(&fp->status_vars.ness.specialhi.pkthunder_pos);
		fp->physics.vel_ground.x = syNetplayQuantizeF32(fp->physics.vel_ground.x);
		syNetplayCanonicalizeNessPKJibakuSimState(fighter_gobj);
	} 
}

void syNetplayQuantizeFighterPhysics(struct FTPhysics *physics)
{
	if ((physics == NULL) || (syNetplaySimQuantizeActive() == FALSE))
	{
		return;
	}
	syNetplayQuantizeVec3f(&physics->vel_air);
	syNetplayQuantizeVec3f(&physics->vel_damage_air);
	syNetplayQuantizeVec3f(&physics->vel_ground);
	physics->vel_damage_ground = syNetplayQuantizeF32(physics->vel_damage_ground);
	physics->vel_jostle_x = syNetplayQuantizeF32(physics->vel_jostle_x);
	physics->vel_jostle_z = syNetplayQuantizeF32(physics->vel_jostle_z);
}

void syNetplayQuantizeMPCollData(MPCollData *coll)
{
	if ((coll == NULL) || (syNetplaySimQuantizeActive() == FALSE))
	{
		return;
	}
	syNetplayQuantizeVec3f(&coll->pos_prev);
	syNetplayQuantizeVec3f(&coll->pos_diff);
	syNetplayQuantizeVec3f(&coll->vel_speed);
	syNetplayQuantizeVec3f(&coll->vel_push);
	syNetplayQuantizeVec3f(&coll->line_coll_dist);
	coll->floor_dist = syNetplayQuantizeF32(coll->floor_dist);
	syNetplayQuantizeVec3f(&coll->floor_angle);
	syNetplayQuantizeVec3f(&coll->ceil_angle);
	syNetplayQuantizeVec3f(&coll->lwall_angle);
	syNetplayQuantizeVec3f(&coll->rwall_angle);
}

void syNetplayQuantizeFTAttackColl(FTAttackColl *attack_coll)
{
	if ((attack_coll == NULL) || (syNetplaySimQuantizeActive() == FALSE))
	{
		return;
	}
	if (attack_coll->attack_state == nGMAttackStateOff)
	{
		return;
	}
	syNetplayQuantizeVec3f(&attack_coll->pos_curr);
	if (attack_coll->attack_state != nGMAttackStateNew)
	{
		syNetplayQuantizeVec3f(&attack_coll->pos_prev);
	}
}

void syNetplayCanonicalizeFighterAttackCollPositions(GObj *fighter_gobj)
{
	FTStruct *fp;
	s32 i;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	if ((fighter_gobj == NULL) || (fighter_gobj->user_data.p == NULL) || (fighter_gobj->obj == NULL))
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	for (i = 0; i < (s32)ARRAY_COUNT(fp->attack_colls); i++)
	{
		syNetplayQuantizeFTAttackColl(&fp->attack_colls[i]);
	}
}

static sb32 syNetplayFighterInPassPlatformGroundCollScope(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	if ((fp->coll_data.floor_flags & MAP_VERTEX_COLL_PASS) == 0U)
	{
		return FALSE;
	}
	return (fp->ga == nMPKineticsGround) ? TRUE : FALSE;
}

static sb32 syNetplayFighterInKirbyCopyLinkSpecialNAnimEndScope(const FTStruct *fp)
{
	if ((fp == NULL) || (fp->fkind != nFTKindKirby))
	{
		return FALSE;
	}
	switch (fp->status_id)
	{
	case nFTKirbyStatusCopyLinkSpecialN:
	case nFTKirbyStatusCopyLinkSpecialNGet:
	case nFTKirbyStatusCopyLinkSpecialNEmpty:
	case nFTKirbyStatusCopyLinkSpecialAirN:
	case nFTKirbyStatusCopyLinkSpecialAirNReturn:
	case nFTKirbyStatusCopyLinkSpecialAirNEmpty:
		return TRUE;
	default:
		return FALSE;
	}
}

static sb32 syNetplayKirbyFighterInCopyLinkBoomerangScope(const FTStruct *fp)
{
	if ((fp == NULL) || (fp->fkind != nFTKindKirby))
	{
		return FALSE;
	}
	if (fp->passive_vars.kirby.copylink_boomerang_gobj != NULL)
	{
		return TRUE;
	}
	return syNetplayFighterInKirbyCopyLinkSpecialNAnimEndScope(fp);
}

static sb32 syNetplayKirbyFighterInCopyLinkCombatScope(const FTStruct *fp)
{
	if (syNetplayKirbyFighterInCopyLinkBoomerangScope(fp) != FALSE)
	{
		return TRUE;
	}
	if ((fp == NULL) || (fp->fkind != nFTKindKirby))
	{
		return FALSE;
	}
	if ((fp->status_id >= nFTKirbyStatusJumpAerialF1) && (fp->status_id <= nFTKirbyStatusJumpAerialF5))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetplayFighterInLinkSpecialNAnimEndScope(const FTStruct *fp)
{
	if ((fp == NULL) || ((fp->fkind != nFTKindLink) && (fp->fkind != nFTKindNLink)))
	{
		return FALSE;
	}
	switch (fp->status_id)
	{
	case nFTLinkStatusSpecialN:
	case nFTLinkStatusSpecialNGet:
	case nFTLinkStatusSpecialNEmpty:
	case nFTLinkStatusSpecialAirN:
	case nFTLinkStatusSpecialAirNReturn:
	case nFTLinkStatusSpecialAirNEmpty:
		return TRUE;
	default:
		return FALSE;
	}
}

static sb32 syNetplayFighterInAirborneDamageKnockbackScope(const FTStruct *fp)
{
	if ((fp == NULL) || (fp->ga != nMPKineticsAir))
	{
		return FALSE;
	}
	if ((fp->status_id >= nFTCommonStatusDamageFlyHi) && (fp->status_id <= nFTCommonStatusDamageFlyRoll))
	{
		return TRUE;
	}
	return (fp->status_id == nFTCommonStatusDamageFall) ? TRUE : FALSE;
}

#if defined(SSB64_NETMENU)
static u32 syNetplayKirbyCopyLinkFloatBits(f32 value)
{
	u32 bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static sb32 syNetplayKirbyCopyLinkTraceEnabled(void)
{
	static int sEnvCache = -999;
	char *e;

	if (sEnvCache == -999)
	{
		e = getenv("SSB64_NETPLAY_KIRBY_COPYLINK_TRACE");
		sEnvCache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	}
	return (sEnvCache != 0) ? TRUE : FALSE;
}

void syNetplayTraceKirbyCopyLinkBoomerangTick(u32 tick)
{
	GObj *fighter_gobj;

	if (syNetplayKirbyCopyLinkTraceEnabled() == FALSE)
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
		GObj *boomerang_gobj;
		WPStruct *wp;
		DObj *boomerang_dobj;
		Vec3f *topn;

		if (syNetplayKirbyFighterInCopyLinkCombatScope(fp) == FALSE)
		{
			continue;
		}
		boomerang_gobj = fp->passive_vars.kirby.copylink_boomerang_gobj;
		wp = (boomerang_gobj != NULL) ? wpGetStruct(boomerang_gobj) : NULL;
		boomerang_dobj = (boomerang_gobj != NULL) ? DObjGetStruct(boomerang_gobj) : NULL;
		topn = (fp->joints[nFTPartsJointTopN] != NULL) ? &fp->joints[nFTPartsJointTopN]->translate.vec.f : NULL;
		port_log(
		    "SSB64 NetSync: kirby_copylink tick=%u player=%d status=%d motion=%d topn_x=0x%08X topn_y=0x%08X "
		    "boomerang_id=%u boomerang_tx=0x%08X boomerang_ty=0x%08X vel_air_x=0x%08X vel_air_y=0x%08X\n",
		    tick,
		    (int)fp->player,
		    (int)fp->status_id,
		    (int)fp->motion_id,
		    (topn != NULL) ? syNetplayKirbyCopyLinkFloatBits(topn->x) : 0U,
		    (topn != NULL) ? syNetplayKirbyCopyLinkFloatBits(topn->y) : 0U,
		    (boomerang_gobj != NULL) ? (unsigned int)boomerang_gobj->id : 0U,
		    (boomerang_dobj != NULL) ? syNetplayKirbyCopyLinkFloatBits(boomerang_dobj->translate.vec.f.x) : 0U,
		    (boomerang_dobj != NULL) ? syNetplayKirbyCopyLinkFloatBits(boomerang_dobj->translate.vec.f.y) : 0U,
		    (wp != NULL) ? syNetplayKirbyCopyLinkFloatBits(wp->physics.vel_air.x) : 0U,
		    (wp != NULL) ? syNetplayKirbyCopyLinkFloatBits(wp->physics.vel_air.y) : 0U);
	}
}
#else
void syNetplayTraceKirbyCopyLinkBoomerangTick(u32 tick)
{
	(void)tick;
}
#endif /* SSB64_NETMENU */

sb32 syNetplayFighterInCaptainGroundKickGroundCollScope(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	if ((fp->fkind != nFTKindCaptain) && (fp->fkind != nFTKindNCaptain))
	{
		return FALSE;
	}
	if (fp->ga != nMPKineticsGround)
	{
		return FALSE;
	}
	switch (fp->status_id)
	{
	case nFTCaptainStatusSpecialLw:
	case nFTCaptainStatusSpecialLwLanding:
		return TRUE;
	default:
		return FALSE;
	}
}

static void syNetplayHardenCaptainGroundKickCollForFighter(GObj *fighter_gobj)
{
	FTStruct *fp;
	Vec3f *topn;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (syNetplayFighterInCaptainGroundKickGroundCollScope(fp) == FALSE)
	{
		return;
	}
	if (fp->joints[nFTPartsJointTopN] == NULL)
	{
		return;
	}
	topn = &fp->joints[nFTPartsJointTopN]->translate.vec.f;
	if (syNetplaySimQuantizeActive() != FALSE)
	{
		syNetplayQuantizeDObjTranslate(fp->joints[nFTPartsJointTopN]);
	}
	fp->coll_data.p_translate = topn;
	fp->coll_data.p_lr = &fp->lr;
	fp->coll_data.p_map_coll = &fp->coll_data.map_coll;
	fp->coll_data.pos_prev = *topn;
	fp->coll_data.pos_diff.x = 0.0F;
	fp->coll_data.pos_diff.y = 0.0F;
	fp->coll_data.pos_diff.z = 0.0F;
}

void syNetplayCanonicalizeCaptainGroundKickSimState(GObj *fighter_gobj)
{
	FTStruct *fp;
	DObj *topn_joint;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (syNetplayFighterInCaptainGroundKickGroundCollScope(fp) == FALSE)
	{
		return;
	}
	fp->physics.vel_ground.x = syNetplayQuantizeF32(fp->physics.vel_ground.x);
	fp->physics.vel_ground.y = syNetplayQuantizeF32(fp->physics.vel_ground.y);
	fp->physics.vel_ground.z = syNetplayQuantizeF32(fp->physics.vel_ground.z);
	fp->physics.vel_air.x = syNetplayQuantizeF32(fp->physics.vel_air.x);
	fp->physics.vel_air.y = syNetplayQuantizeF32(fp->physics.vel_air.y);
	fp->physics.vel_air.z = syNetplayQuantizeF32(fp->physics.vel_air.z);
	fp->status_vars.captain.speciallw.vel_scale =
	    syNetplayQuantizeF32(fp->status_vars.captain.speciallw.vel_scale);
	topn_joint = fp->joints[nFTPartsJointTopN];
	if (topn_joint != NULL)
	{
		topn_joint->rotate.vec.f.z = syNetplayQuantizeF32(topn_joint->rotate.vec.f.z);
	}
	syNetplayHardenCaptainGroundKickCollForFighter(fighter_gobj);
}

static void syNetplayHardenPassPlatformCollForFighter(GObj *fighter_gobj)
{
	FTStruct *fp;
	Vec3f *topn;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (syNetplayFighterInPassPlatformGroundCollScope(fp) == FALSE)
	{
		return;
	}
	if (fp->joints[nFTPartsJointTopN] == NULL)
	{
		return;
	}
	topn = &fp->joints[nFTPartsJointTopN]->translate.vec.f;
	if (syNetplaySimQuantizeActive() != FALSE)
	{
		syNetplayQuantizeDObjTranslate(fp->joints[nFTPartsJointTopN]);
	}
	fp->coll_data.p_translate = topn;
	fp->coll_data.p_lr = &fp->lr;
	fp->coll_data.p_map_coll = &fp->coll_data.map_coll;
	fp->coll_data.pos_prev = *topn;
	fp->coll_data.pos_diff.x = 0.0F;
	fp->coll_data.pos_diff.y = 0.0F;
	fp->coll_data.pos_diff.z = 0.0F;
}

static void syNetplayHardenAirborneDamageKnockbackCollForFighter(GObj *fighter_gobj)
{
	FTStruct *fp;
	Vec3f *topn;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (syNetplayFighterInAirborneDamageKnockbackScope(fp) == FALSE)
	{
		return;
	}
	if (fp->joints[nFTPartsJointTopN] == NULL)
	{
		return;
	}
	topn = &fp->joints[nFTPartsJointTopN]->translate.vec.f;
	if (syNetplaySimQuantizeActive() != FALSE)
	{
		syNetplayQuantizeDObjTranslate(fp->joints[nFTPartsJointTopN]);
	}
	fp->coll_data.p_translate = topn;
	fp->coll_data.p_lr = &fp->lr;
	fp->coll_data.p_map_coll = &fp->coll_data.map_coll;
	fp->coll_data.pos_prev = *topn;
	fp->coll_data.pos_diff.x = 0.0F;
	fp->coll_data.pos_diff.y = 0.0F;
	fp->coll_data.pos_diff.z = 0.0F;
}

#if defined(SSB64_NETMENU)
/*
 * ftAnimEndSetWait / GuardOff release wait for anim_frame <= 0. Cross-ISA float can leave one peer
 * above zero for extra ProcUpdate ticks despite syNetplayQuantizeAnimScalar (soak2 @950765188 GuardOff
 * status_total_tics fork; @371591666 Link SpecialN charge end fork → CopyLink hit knockback FC @600).
 * Snap near-zero frames to zero before ProcUpdate on known anim-end-wait statuses.
 */
static void syNetplaySnapAnimFrameToEndIfNearZero(GObj *fighter_gobj)
{
	DObj *root_dobj;
	f32 q_anim;
	const f32 release_grid = (1.0F / 65536.0F);

	if (fighter_gobj == NULL)
	{
		return;
	}
	q_anim = syNetplayQuantizeAnimScalar(fighter_gobj->anim_frame);
	if (q_anim <= release_grid)
	{
		fighter_gobj->anim_frame = 0.0F;
		root_dobj = DObjGetStruct(fighter_gobj);
		if (root_dobj != NULL)
		{
			root_dobj->anim_frame = 0.0F;
		}
	}
}

static void syNetplayCanonicalizeAnimEndWaitThreshold(GObj *fighter_gobj, FTStruct *fp)
{
	if ((fighter_gobj == NULL) || (fp == NULL))
	{
		return;
	}
	if (fp->status_id == nFTCommonStatusGuardOn)
	{
		if (ftStatusVarsGuard(fp)->is_release != FALSE)
		{
			syNetplaySnapAnimFrameToEndIfNearZero(fighter_gobj);
		}
		return;
	}
	if ((syNetplayFighterInLinkSpecialNAnimEndScope(fp) != FALSE) ||
	    (syNetplayFighterInKirbyCopyLinkSpecialNAnimEndScope(fp) != FALSE))
	{
		syNetplaySnapAnimFrameToEndIfNearZero(fighter_gobj);
		return;
	}
	/*
	 * Cliff windup/climb statuses gate on fighter_gobj->anim_frame <= 0.0F (same class as GuardOff /
	 * Link SpecialN charge end). Quantize grid can leave a tiny positive frame that never satisfies
	 * the check after synctest emergency restore re-pins mid-windup anim (~frame 30).
	 */
	if ((fp->status_id >= nFTCommonStatusCliffCatch) &&
	    (fp->status_id <= nFTCommonStatusCliffEscapeSlow2))
	{
		syNetplaySnapAnimFrameToEndIfNearZero(fighter_gobj);
	}
}
#endif

void syNetplayCanonicalizeFighterSimState(GObj *fighter_gobj)
{
	FTStruct *fp;
	DObj *root_dobj;
	s32 ji;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp == NULL)
	{
		return;
	}
	syNetplayQuantizeFighterPhysics(&fp->physics);
	syNetplayQuantizeMPCollData(&fp->coll_data);
	root_dobj = DObjGetStruct(fighter_gobj);
	if (root_dobj != NULL)
	{
		f32 gobj_anim_preserved;

		syNetplayQuantizeDObjTranslate(root_dobj);
		syNetplayQuantizeVec3f(&root_dobj->rotate.vec.f);
		gobj_anim_preserved = fighter_gobj->anim_frame;
		fighter_gobj->anim_frame = syNetplayQuantizeAnimScalar(fighter_gobj->anim_frame);
		root_dobj->anim_frame = fighter_gobj->anim_frame;
		for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
		{
			if (fp->joints[ji] != NULL)
			{
				/* Full pose (translate + rotate + scale), not translate only: child joint rotate
				 * is folded into fhash_full / the FC fighter_digest, but is grid-aligned in-sim only
				 * when gcPlayDObjAnimJoint plays that joint's track this frame. Joints that don't
				 * advance (DK heavy-throw/cargo carry, the Shouldered captive posed via coupling, any
				 * held pose) kept a raw cross-ISA value here, which leaked into the snapshot blob and
				 * forked the frame-commit digest. The intro/appear pass already does rotate+scale; do
				 * it for every scope. See docs/bugs/netplay_fighter_child_joint_rotate_quantize_2026-06-29.md. */
				syNetplayQuantizeDObjAnimPose(fp->joints[ji]);
				syNetplayQuantizeDObjAnimScalars(fp->joints[ji]);
			}
		}
		/* Joint loop propagates last joint anim_frame to parent_gobj; restore independent gobj cursor. */
		fighter_gobj->anim_frame = syNetplayQuantizeAnimScalar(gobj_anim_preserved);
		root_dobj->anim_frame = fighter_gobj->anim_frame;
	}
	else
	{
		for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
		{
			if (fp->joints[ji] != NULL)
			{
				/* Full pose (translate + rotate + scale): see rationale in the root_dobj branch above. */
				syNetplayQuantizeDObjAnimPose(fp->joints[ji]);
				syNetplayQuantizeDObjAnimScalars(fp->joints[ji]);
			}
		}
	}
	syNetplayCanonicalizeFighterAttackCollPositions(fighter_gobj);
	syNetplayCanonicalizeFighterIntroJointPose(fighter_gobj);
	syNetplayCanonicalizeRebirthFighterMapPose(fighter_gobj);
	syNetplayCanonicalizeNessPKJibakuSimState(fighter_gobj);
	syNetplayCanonicalizeNessPKThunderHoldSimState(fighter_gobj);
	syNetplayQuantizeNessPKThunderLandingStatusVars(fp, &fp->status_vars);
	syNetplayCanonicalizeNessSpecialLwSimState(fighter_gobj);
	syNetplayCanonicalizePikachuQuickAttackSimState(fighter_gobj);
	syNetplayCanonicalizeFoxFirefoxSimState(fighter_gobj);
	syNetplayCanonicalizeCaptainGroundKickSimState(fighter_gobj);
	if (syNetplayFighterInPassPlatformGroundCollScope(fp) != FALSE)
	{
		syNetplayHardenPassPlatformCollForFighter(fighter_gobj);
	}
	syNetplayHardenAirborneDamageKnockbackCollForFighter(fighter_gobj);
#if defined(SSB64_NETMENU)
	syNetplayCanonicalizeAnimEndWaitThreshold(fighter_gobj, fp);
#endif
}

void syNetplayQuantizeGMCameraState(GMCamera *camera, f32 *pause_eye_x, f32 *pause_eye_y)
{
	if ((camera == NULL) || (syNetplaySimQuantizeActive() == FALSE))
	{
		return;
	}
	camera->target_dist = syNetplayQuantizeF32(camera->target_dist);
	syNetplayQuantizeVec3f(&camera->vel_at);
	camera->fovy = syNetplayQuantizeF32(camera->fovy);
	camera->pzoom_eye_x = syNetplayQuantizeF32(camera->pzoom_eye_x);
	camera->pzoom_eye_y = syNetplayQuantizeF32(camera->pzoom_eye_y);
	camera->pzoom_dist = syNetplayQuantizeF32(camera->pzoom_dist);
	camera->pzoom_pan_scale = syNetplayQuantizeF32(camera->pzoom_pan_scale);
	camera->pzoom_fov = syNetplayQuantizeF32(camera->pzoom_fov);
	syNetplayQuantizeVec3f(&camera->zoom_origin_pos);
	syNetplayQuantizeVec3f(&camera->zoom_target_pos);
	camera->pfollow_eye_x = syNetplayQuantizeF32(camera->pfollow_eye_x);
	camera->pfollow_eye_y = syNetplayQuantizeF32(camera->pfollow_eye_y);
	camera->pfollow_dist = syNetplayQuantizeF32(camera->pfollow_dist);
	camera->pfollow_pan_scale = syNetplayQuantizeF32(camera->pfollow_pan_scale);
	camera->pfollow_fov = syNetplayQuantizeF32(camera->pfollow_fov);
	syNetplayQuantizeVec3f(&camera->vel_all);
	if (pause_eye_x != NULL)
	{
		*pause_eye_x = syNetplayQuantizeF32(*pause_eye_x);
	}
	if (pause_eye_y != NULL)
	{
		*pause_eye_y = syNetplayQuantizeF32(*pause_eye_y);
	}
}

void syNetplayCanonicalizeGMCameraSimState(void)
{
	CObj *cobj;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	syNetplayQuantizeGMCameraState(&gGMCameraStruct, &gGMCameraPauseCameraEyeX, &gGMCameraPauseCameraEyeY);
	if (gGMCameraGObj == NULL)
	{
		return;
	}
	cobj = CObjGetStruct(gGMCameraGObj);
	if (cobj == NULL)
	{
		return;
	}
	syNetplayQuantizeVec3f(&cobj->vec.eye);
	syNetplayQuantizeVec3f(&cobj->vec.at);
	syNetplayQuantizeVec3f(&cobj->vec.up);
	cobj->projection.persp.fovy = syNetplayQuantizeF32(cobj->projection.persp.fovy);
}

static void syNetplayQuantizeItemPhysics(ITStruct *ip)
{
	if ((ip == NULL) || (syNetplaySimQuantizeActive() == FALSE))
	{
		return;
	}
	ip->physics.vel_ground = syNetplayQuantizeF32(ip->physics.vel_ground);
	syNetplayQuantizeVec3f(&ip->physics.vel_air);
}

void syNetplayCanonicalizeItemSimState(GObj *item_gobj)
{
	ITStruct *ip;
	DObj *dobj;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	if ((item_gobj == NULL) || (item_gobj->user_data.p == NULL))
	{
		return;
	}
	ip = itGetStruct(item_gobj);
	if (ip == NULL)
	{
		return;
	}
	syNetplayQuantizeItemPhysics(ip);
	/*
	 * The item's folded position IS its root DObj translate (syNetSyncFoldItemState hashes
	 * dobj->translate.vec.f directly). Item forward-sim position integration is never quantized, so
	 * on a stage with a movable item that gets knocked by a hit (Peach's Castle GBumper, atk_state!=off)
	 * the per-tick cross-ISA f32 drift accumulates — observed ~5 units apart Linux↔Android — until
	 * frame_commit_item_diverge fires and forces an unrecoverable deep resim (perceived as a hard lock).
	 * Snap translate to the shared grid each accepted tick exactly as fighters do, so both peers integrate
	 * from identical inputs and the item hash stops forking. See
	 * docs/bugs/netplay_castle_bumper_item_resim_diverge_2026-06-28.md.
	 */
	dobj = DObjGetStruct(item_gobj);
	if (dobj != NULL)
	{
		syNetplayQuantizeDObjTranslate(dobj);
	}
}

void syNetplayCanonicalizeActiveItemsForNetplay(void)
{
	GObj *item_gobj;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	for (item_gobj = gGCCommonLinks[nGCCommonLinkIDItem]; item_gobj != NULL;
	     item_gobj = item_gobj->link_next)
	{
		syNetplayCanonicalizeItemSimState(item_gobj);
	}
}

static void syNetplayQuantizeWeaponPhysics(WPStruct *wp)
{
	if (wp == NULL)
	{
		return;
	}
	wp->physics.vel_ground = syNetplayQuantizeF32(wp->physics.vel_ground);
	syNetplayQuantizeVec3f(&wp->physics.vel_air);
}

void syNetplayCanonicalizeWeaponSimState(GObj *weapon_gobj)
{
	WPStruct *wp;
	DObj *dobj;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	if ((weapon_gobj == NULL) || (weapon_gobj->user_data.p == NULL))
	{
		return;
	}
	wp = wpGetStruct(weapon_gobj);
	if (wp == NULL)
	{
		return;
	}
	syNetplayQuantizeWeaponPhysics(wp);
	dobj = DObjGetStruct(weapon_gobj);
	if (dobj != NULL)
	{
		syNetplayQuantizeDObjTranslate(dobj);
		syNetplayQuantizeVec3f(&dobj->rotate.vec.f);
		syNetplayQuantizeVec3f(&dobj->scale.vec.f);
	}
}

void syNetplayCanonicalizeActiveWeaponsForNetplay(void)
{
	GObj *weapon_gobj;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		syNetplayCanonicalizeWeaponSimState(weapon_gobj);
	}
}

void syNetplayHardenPassPlatformCollBeforeSim(void)
{
	GObj *fighter_gobj;

#if defined(PORT) && defined(SSB64_NETMENU)
	if (syNetplayRollbackLiveForwardSimEligible() == FALSE)
	{
		return;
	}
#endif
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		syNetplayHardenPassPlatformCollForFighter(fighter_gobj);
	}
}

void syNetplayHardenAirborneDamageKnockbackCollBeforeSim(void)
{
	GObj *fighter_gobj;

#if defined(PORT) && defined(SSB64_NETMENU)
	if (syNetplayRollbackLiveForwardSimEligible() == FALSE)
	{
		return;
	}
#endif
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		syNetplayHardenAirborneDamageKnockbackCollForFighter(fighter_gobj);
	}
}

void syNetplayHardenCaptainGroundKickCollBeforeSim(void)
{
	GObj *fighter_gobj;

#if defined(PORT) && defined(SSB64_NETMENU)
	if (syNetplayRollbackLiveForwardSimEligible() == FALSE)
	{
		return;
	}
#endif
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		syNetplayHardenCaptainGroundKickCollForFighter(fighter_gobj);
	}
}

void syNetplayCanonicalizeActiveFightersForNetplay(void)
{
	GObj *fighter_gobj;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		syNetplayCanonicalizeFighterSimState(fighter_gobj);
	}
	syNetplayCanonicalizeActiveItemsForNetplay();
	syNetplayCanonicalizeActiveWeaponsForNetplay();
	syNetplayCanonicalizeGMCameraSimState();
}

void syNetplayRestoreRebirthStatusVars(FTStruct *fp, const union FTStatusVars *blob_status_vars)
{
	if ((fp == NULL) || (blob_status_vars == NULL) || (syNetplayFighterInRebirthScope(fp) == FALSE))
	{
		return;
	}
	*ftStatusVarsRebirth(fp) = blob_status_vars->common.rebirth;
}

void syNetplayRepairRebirthApexIfInverted(FTStruct *fp)
{
	f32 apex_y;
	f32 base_y;

	if (fp == NULL)
	{
		return;
	}
	if (syNetplayFighterInRebirthScope(fp) == FALSE)
	{
		return;
	}
	apex_y = ftStatusVarsRebirth(fp)->pos.y;
	base_y = ftStatusVarsRebirth(fp)->halo_offset.y;
	if (apex_y > base_y)
	{
		return;
	}
	if (gMPCollisionGroundData != NULL)
	{
		ftStatusVarsRebirth(fp)->pos.y = gMPCollisionGroundData->map_bound_top;
	}
}

void syNetplayCanonicalizeRebirthFighterMapPose(GObj *fighter_gobj)
{
	FTStruct *fp;
	DObj *dobj;
	sb32 vs_active;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (syNetplayFighterInRebirthScope(fp) == FALSE)
	{
		return;
	}
#if defined(SSB64_NETMENU)
	vs_active = syNetPeerIsVSSessionActive();
	if ((vs_active == FALSE) && (syNetplaySimQuantizeActive() == FALSE))
	{
		return;
	}
	if (vs_active != FALSE)
	{
		syNetplayRepairRebirthApexIfInverted(fp);
	}
#else
	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
#endif
	if (syNetplaySimQuantizeActive() != FALSE)
	{
		syNetplayQuantizeVec3f(&ftStatusVarsRebirth(fp)->pos);
		syNetplayQuantizeVec3f(&ftStatusVarsRebirth(fp)->halo_offset);
	}
	dobj = DObjGetStruct(fighter_gobj);
	if (dobj == NULL)
	{
		return;
	}
	/*
	 * Rebirth root Y is authoritative as captured pose. Vanilla forward sim derives RebirthDown Y in
	 * ftCommonRebirthCommonProcMap before reaching this hook; rollback load/verify paths can call this
	 * canonicalizer after the live rebirth union's halo_lower_wait has advanced past the loaded tick.
	 * Re-deriving here then writes a newer platform Y over the snapshot pose even if the union is later
	 * restored, producing figh-only LOAD_HASH_DRIFT. Quantize the restored/forward-derived pose instead.
	 * See docs/bugs/netplay_rebirth_wait_pose_derive_synctest_2026-07-02.md and
	 * docs/bugs/netplay_rebirth_down_pose_derive_load_cycle_2026-07-03.md.
	 */
	if (syNetplaySimQuantizeActive() != FALSE)
	{
		syNetplayQuantizeDObjTranslate(dobj);
		if (fp->joints[nFTPartsJointTopN] != NULL)
		{
			syNetplayQuantizeDObjTranslate(fp->joints[nFTPartsJointTopN]);
		}
	}
}

/*
 * Per-joint forward-vs-resim AObj trace (default off; SSB64_NETPLAY_AOBJ_LEG_TRACE=1).
 *
 * Ground-truth probe for the post-rollback joint-spin bug: dumps, for every active fighter joint,
 * the parsed AObj-chain digest (length/length_invert/value_base/value_target/rate_base/rate_target +
 * track/kind), a digest of the raw figatree EVENT32 stream bytes (the parser's source data — NOT folded
 * into the per-tick anim hash), the anim cursor scalars, and the rotate output. Tagged phase=fwd|resim
 * and keyed by stable dobj/event32 pointers so the same joint can be compared across the forward pass
 * and the resim replay of the same tick. Independent of SIM_F32_QUANTIZE so it works in a quantize-off
 * bisect. Bound the volume with SSB64_NETPLAY_AOBJ_LEG_TRACE_TICK_MIN / _TICK_MAX.
 *
 * Divergence localization (compare fwd vs resim line for the same tick+dobj):
 *   stream differs  -> raw figatree bytes mutated by restore (force un-halfswap) vs forward sim.
 *   chain differs, stream same -> parser produced a different chain from the same bytes (cursor/timing).
 *   rotate differs, chain same -> downstream pose write divergence.
 */
static u32 syNetplayAObjF32Bits(f32 v)
{
	u32 bits;

	memcpy(&bits, &v, sizeof(bits));
	return bits;
}

static sb32 sSYNetplayAObjLegTraceEnvCache = -999;
static u32 sSYNetplayAObjLegTraceTickMin = 0U;
static u32 sSYNetplayAObjLegTraceTickMax = 0xFFFFFFFFU;

static sb32 syNetplayAObjLegTraceEnabled(void)
{
	const char *env;

	if (sSYNetplayAObjLegTraceEnvCache != -999)
	{
		return sSYNetplayAObjLegTraceEnvCache;
	}
	env = getenv("SSB64_NETPLAY_AOBJ_LEG_TRACE");
	sSYNetplayAObjLegTraceEnvCache = ((env != NULL) && (atoi(env) != 0)) ? TRUE : FALSE;
	if (sSYNetplayAObjLegTraceEnvCache != FALSE)
	{
		const char *mn = getenv("SSB64_NETPLAY_AOBJ_LEG_TRACE_TICK_MIN");
		const char *mx = getenv("SSB64_NETPLAY_AOBJ_LEG_TRACE_TICK_MAX");

		if ((mn != NULL) && (mn[0] != '\0'))
		{
			sSYNetplayAObjLegTraceTickMin = (u32)atoi(mn);
		}
		if ((mx != NULL) && (mx[0] != '\0'))
		{
			sSYNetplayAObjLegTraceTickMax = (u32)atoi(mx);
		}
	}
	return sSYNetplayAObjLegTraceEnvCache;
}

static u32 syNetplayAObjStreamDigest(const void *event32)
{
	const u32 *ev = (const u32 *)event32;
	u32 h = 2166136261U;
	s32 i;

	if (ev == NULL)
	{
		return 0U;
	}
	for (i = 0; i < 16; i++)
	{
		h ^= ev[i];
		h *= 16777619U;
	}
	return h;
}

static u32 syNetplayAObjChainDigest(const DObj *dobj)
{
	const AObj *aobj;
	u32 h = 2166136261U;
	s32 n = 0;

	for (aobj = dobj->aobj; (aobj != NULL) && (n < 32); aobj = aobj->next, n++)
	{
		h ^= (u32)aobj->track;
		h *= 16777619U;
		h ^= (u32)aobj->kind;
		h *= 16777619U;
		h ^= syNetplayAObjF32Bits(aobj->length_invert);
		h *= 16777619U;
		h ^= syNetplayAObjF32Bits(aobj->length);
		h *= 16777619U;
		h ^= syNetplayAObjF32Bits(aobj->value_base);
		h *= 16777619U;
		h ^= syNetplayAObjF32Bits(aobj->value_target);
		h *= 16777619U;
		h ^= syNetplayAObjF32Bits(aobj->rate_base);
		h *= 16777619U;
		h ^= syNetplayAObjF32Bits(aobj->rate_target);
		h *= 16777619U;
	}
	return h;
}

/* seg=jnt: sim joint (fp->joints[]).  seg=vis: visual figatree DObj under TopN->child (the rendered
 * model the parser actually animates) — separate from the sim joints and the only place a reattach-
 * reloaded-but-not-renormalized stream surfaces.  idx is the joint index for jnt, a walk counter for vis. */
static void syNetplayTraceLogDObjAObj(u32 tick, sb32 is_resim, FTStruct *fp, const char *seg, s32 idx,
				      DObj *dobj)
{
	if (dobj == NULL)
	{
		return;
	}
	if ((dobj->aobj == NULL) && (dobj->anim_joint.event32 == NULL))
	{
		return;
	}
	port_log(
	    "SSB64 NetAObjTrace: tick=%u phase=%s seg=%s player=%d fkind=%d joint=%d dobj=%p ev=%p "
	    "af=%.9g aw=%.9g as=%.9g ra=%.9g rx=%.9g ry=%.9g rz=%.9g tx=%.9g ty=%.9g tz=%.9g "
	    "sx=%.9g sy=%.9g sz=%.9g chain=0x%08x stream=0x%08x\n",
	    tick, (is_resim != FALSE) ? "resim" : "fwd", seg, (s32)fp->player, (s32)fp->fkind, idx,
	    (void *)dobj, (void *)dobj->anim_joint.event32, (f64)dobj->anim_frame, (f64)dobj->anim_wait,
	    (f64)dobj->anim_speed, (f64)dobj->rotate.a, (f64)dobj->rotate.vec.f.x,
	    (f64)dobj->rotate.vec.f.y, (f64)dobj->rotate.vec.f.z, (f64)dobj->translate.vec.f.x,
	    (f64)dobj->translate.vec.f.y, (f64)dobj->translate.vec.f.z, (f64)dobj->scale.vec.f.x,
	    (f64)dobj->scale.vec.f.y, (f64)dobj->scale.vec.f.z, syNetplayAObjChainDigest(dobj),
	    syNetplayAObjStreamDigest(dobj->anim_joint.event32));
}

void syNetplayTraceActiveFighterAObj(u32 tick)
{
	GObj *fighter_gobj;
	sb32 is_resim;

	if (syNetplayAObjLegTraceEnabled() == FALSE)
	{
		return;
	}
	if ((tick < sSYNetplayAObjLegTraceTickMin) || (tick > sSYNetplayAObjLegTraceTickMax))
	{
		return;
	}
	is_resim = (syNetRollbackIsResimulating() != FALSE) ? TRUE : FALSE;
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);
		s32 ji;
		DObj *root_dobj;
		DObj *current_dobj;
		s32 vis_idx;

		if (fp == NULL)
		{
			continue;
		}
		for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
		{
			syNetplayTraceLogDObjAObj(tick, is_resim, fp, "jnt", ji, fp->joints[ji]);
		}

		/* Visual figatree tree (rendered model) — walked exactly like
		 * syNetRbSnapUnhalfswapFighterFigatreeDObjAnimStreams so vis indices align across logs. */
		if (fp->joints[nFTPartsJointTopN] == NULL)
		{
			continue;
		}
		root_dobj = fp->joints[nFTPartsJointTopN]->child;
		if (root_dobj == NULL)
		{
			continue;
		}
		vis_idx = 0;
		for (current_dobj = root_dobj; current_dobj != NULL;
		     current_dobj = lbCommonGetTreeDObjNextFromRoot(current_dobj, root_dobj))
		{
			syNetplayTraceLogDObjAObj(tick, is_resim, fp, "vis", vis_idx, current_dobj);
			vis_idx++;
		}
	}
}
