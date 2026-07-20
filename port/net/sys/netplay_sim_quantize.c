#include <sys/netplay_sim_quantize.h>
#include <sys/netplay_fox_firefox_gate.h>
#include <sys/netplay_ness_pkthunder_gate.h>
#include <sys/netplay_pikachu_quickattack_gate.h>
#include <sys/netinput.h>

#include <sys/netpeer.h>
#include <sys/netreplay.h>
#include <sys/netrollback.h>
#include <sys/netrollbacksnapshot.h>
#include <sys/obj.h>
#include <sys/objtypes.h>
#include <ft/fighter.h>
#include <ft/ftchar/ftpikachu/ftpikachu.h>
#include <gr/ground.h>
#include <mp/map.h>
#include <mp/mpdef.h>
#include <mp/mpprocess.h>
#include <sc/scene.h>

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
	/* Saved-input `.ssb64r` playback with SSB64_REPLAY_DIAGNOSTIC — no UDP peer. */
	if (syNetReplayIsDiagnosticPlaybackActive() != FALSE)
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
	/*
	 * AOBJ sentinels are huge negatives (F32_MIN, F32_MIN/2, F32_MIN/3). Grid-rounding them
	 * corrupts anim_wait retirement and can fork PlayAnim end detection cross-ISA.
	 */
	if ((value == AOBJ_ANIM_NULL) || (value == AOBJ_ANIM_CHANGED) || (value == AOBJ_ANIM_END))
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

f32 syNetplayAnimWaitAdd(f32 wait, f32 addend)
{
#if defined(SSB64_NETMENU)
	if (syNetplaySimQuantizeActive() != FALSE)
	{
		return syNetplayQuantizeAnimScalar(wait + addend);
	}
#endif
	return wait + addend;
}

void syNetplayAnimCountdownFixedPoint(f32 *anim_wait, f32 *anim_frame, f32 anim_speed)
{
#if defined(SSB64_NETMENU)
	f32 q_wait;
	f32 q_speed;
	f32 q_frame;
	s32 i_wait;
	s32 i_speed;

	if ((anim_wait == NULL) || (anim_frame == NULL))
	{
		return;
	}
	/*
	 * Always fixed-point under NETMENU (prior gcPlayDObjAnimJoint behavior). Do not gate on
	 * SimQuantizeActive — QuantizeAnimScalar is identity when the env is off; integer subtract
	 * still canonicalizes wait>0 continue vs end cross-ISA.
	 */
	q_wait = syNetplayQuantizeAnimScalar(*anim_wait);
	q_speed = syNetplayQuantizeAnimScalar(anim_speed);
	if (!(q_speed > 0.0F))
	{
		q_speed = 1.0F;
	}
	i_wait = (s32)floor((f64)q_wait * 65536.0 + 0.5);
	i_speed = (s32)floor((f64)q_speed * 65536.0 + 0.5);
	if (i_speed <= 0)
	{
		i_speed = 65536;
	}
	i_wait -= i_speed;
	*anim_wait = (f32)((f64)i_wait / 65536.0);
	q_frame = syNetplayQuantizeAnimScalar(*anim_frame + q_speed);
	*anim_frame = q_frame;
#else
	if ((anim_wait == NULL) || (anim_frame == NULL))
	{
		return;
	}
	*anim_wait -= anim_speed;
	*anim_frame += anim_speed;
#endif
}

void syNetplayAnimWaitCollapseLeftover(f32 *anim_wait)
{
#if defined(SSB64_NETMENU)
	const f32 leftover_eps = (256.0F / 65536.0F);

	if ((anim_wait == NULL) || (syNetplaySimQuantizeActive() == FALSE))
	{
		return;
	}
	if ((*anim_wait > 0.0F) && (*anim_wait <= leftover_eps))
	{
		*anim_wait = 0.0F;
	}
#else
	(void)anim_wait;
#endif
}

#if defined(SSB64_NETMENU)
/*
 * Called from gcPlayDObjAnimJoint immediately after wait-=speed. Collapse cross-ISA leftover
 * so `if (wait > 0) continue` agrees on both peers (soak 100749819 Turn→Wait @594 vs @595).
 */
void syNetplayQuantizeDObjAnimScalarsAfterPlayStep(DObj *dobj)
{
	syNetplayQuantizeDObjAnimScalars(dobj);
	if (dobj == NULL)
	{
		return;
	}
	syNetplayAnimWaitCollapseLeftover(&dobj->anim_wait);
}
#else
void syNetplayQuantizeDObjAnimScalarsAfterPlayStep(DObj *dobj)
{
	syNetplayQuantizeDObjAnimScalars(dobj);
}
#endif

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
	DObj *root_dobj;
	s32 ji;
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
	/*
	 * Always grid-snap physics / MPColl / TopN in QA scope (not only pass-platform). Zip travel
	 * and Start→Zip catch-up otherwise leave cross-ISA vel/pose forks that Ness jibaku already
	 * hardens via full sim-state canonicalize. See docs/bugs/netplay_pikachu_quickattack_canonicalize_2026-07-10.md.
	 */
	syNetplayQuantizeFighterPhysics(&fp->physics);
	syNetplayQuantizeMPCollData(&fp->coll_data);
	root_dobj = DObjGetStruct(fighter_gobj);
	if (root_dobj != NULL)
	{
		syNetplayQuantizeDObjTranslate(root_dobj);
	}
	if (fp->joints[nFTPartsJointTopN] != NULL)
	{
		syNetplayQuantizeDObjTranslate(fp->joints[nFTPartsJointTopN]);
	}
	if (syNetplayPikachuFighterInQuickAttackZipScope(fp->status_id) == FALSE)
	{
		return;
	}
	for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
	{
		if (fp->joints[ji] != NULL)
		{
			syNetplayQuantizeDObjTranslate(fp->joints[ji]);
			syNetplayQuantizeDObjRotate(fp->joints[ji]);
		}
	}
	base_joint = fp->joints[FTPIKACHU_QUICKATTACK_BASE_JOINT];
	if (base_joint == NULL)
	{
		return;
	}
	base_joint->rotate.vec.f.x = syNetplayQuantizeF32(base_joint->rotate.vec.f.x);
	base_joint->scale.vec.f.x = syNetplayQuantizeF32(FTPIKACHU_QUICKATTACK_SCALE_X);
	base_joint->scale.vec.f.y = syNetplayQuantizeF32(FTPIKACHU_QUICKATTACK_SCALE_Y);
	base_joint->scale.vec.f.z = syNetplayQuantizeF32(FTPIKACHU_QUICKATTACK_SCALE_Z);
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
	/*
	 * Full PK Thunder Hi status family, not just Start/Hold. The wave shell survives 1-2 ticks
	 * past hold exit (jibaku/end/bound); anim_frame is allowed to drift cross-ISA during hold
	 * (fold uses status_total_tics), so if the classifier drops out at the status transition the
	 * eff fold falls back to the drifted anim_frame and frame-commit forks with matching inputs
	 * (soak1 489290440 @3179-3180: jibaku @3179, eff-only FC diverge validation=3181 -> hang).
	 * Matches the snapshot layer, which already stamps respawn=NESS_PK_WAVE through jibaku scope.
	 * See docs/bugs/netplay_ness_pkwave_jibaku_eff_fold_dropout_2026-07-16.md.
	 */
	switch (fp->status_id)
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
		return TRUE;

	default:
		break;
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

/*
 * Coarse launch-dist grid (world units). 1/65536 quantize cannot absorb Hold head ULP
 * (~0.04–0.11 at soak1 FC@2534); 0.25 collapses that class while keeping aim feel.
 */
/*
 * 0.25 straddled soak 1540234570 FC@2404 (raw dy -13.85 vs -13.90 → -13.75/-14.00).
 * 1.0 reunifies that case and soak 1416106492 FC@2534; still tiny vs collide box.
 */
#define SYNETPLAY_JIBAKU_LAUNCH_DIST_GRID 1.0F

static f32 syNetplayQuantizeF32JibakuLaunchDistGrid(f32 value)
{
	f64 scaled;
	f32 result;

	if (SYNETPLAY_JIBAKU_LAUNCH_DIST_GRID <= 0.0F)
	{
		return value;
	}
	scaled = (f64)value / (f64)SYNETPLAY_JIBAKU_LAUNCH_DIST_GRID;
	if (scaled >= 0.0)
	{
		scaled = floor(scaled + 0.5);
	}
	else
	{
		scaled = ceil(scaled - 0.5);
	}
	result = (f32)(scaled * (f64)SYNETPLAY_JIBAKU_LAUNCH_DIST_GRID);
	return (result == 0.0F) ? 0.0F : result;
}

void syNetplayNessHardenPKJibakuLaunchAnchor(Vec3f *pkthunder_pos)
{
	if ((pkthunder_pos == NULL) || (syNetplaySimQuantizeActive() == FALSE))
	{
		return;
	}
	/*
	 * Coarse-snap the self-hit anchor before dist = fighter − pkthunder_pos.
	 * Dist-only 1.0u harden still straddles when Hold head Δy ≈ 0.34u puts raw
	 * dist_y on opposite sides of a half-unit midpoint (soak C FC@3732:
	 * −7.17 → −7 vs −7.51 → −8). Same grid as launch dist.
	 * See docs/bugs/netplay_ness_jibaku_launch_dist_hold_head_fc_2026-07-19.md.
	 */
	pkthunder_pos->x = syNetplayQuantizeF32JibakuLaunchDistGrid(pkthunder_pos->x);
	pkthunder_pos->y = syNetplayQuantizeF32JibakuLaunchDistGrid(pkthunder_pos->y);
	pkthunder_pos->z = syNetplayQuantizeF32JibakuLaunchDistGrid(pkthunder_pos->z);
}

void syNetplayNessHardenPKThunderHeadAfterMap(GObj *weapon_gobj)
{
	WPStruct *wp;
	FTStruct *fp;
	DObj *dobj;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	if (weapon_gobj == NULL)
	{
		return;
	}
	wp = wpGetStruct(weapon_gobj);
	if ((wp == NULL) || (wp->kind != nWPKindPKThunderHead) || (wp->owner_gobj == NULL))
	{
		return;
	}
	fp = ftGetStruct(wp->owner_gobj);
	if ((fp == NULL) || (syNetplayFighterInNessPKThunderHoldSimScope(fp) == FALSE))
	{
		return;
	}
	/*
	 * Fine QuantizeDObjTranslate after ProcUpdate is not enough: wpMap floor/wall
	 * AdjNew on CLIFF can still accumulate cross-ISA ULP until Hold→jibaku
	 * Refresh copies a forked head into pkthunder_pos (soak 128512323 @2043–2064).
	 * Same 1.0u grid as syNetplayNessHardenPKJibakuLaunchAnchor.
	 * See docs/bugs/netplay_pkthunder_hold_head_cliff_mplanding_jibaku_2026-07-19.md.
	 */
	dobj = DObjGetStruct(weapon_gobj);
	if (dobj == NULL)
	{
		return;
	}
	syNetplayNessHardenPKJibakuLaunchAnchor(&dobj->translate.vec.f);
}

void syNetplayNessHardenPKJibakuLaunchDist(f32 *dist_x, f32 *dist_y)
{
	if ((dist_x == NULL) || (dist_y == NULL))
	{
		return;
	}
	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	*dist_x = syNetplayQuantizeF32JibakuLaunchDistGrid(*dist_x);
	*dist_y = syNetplayQuantizeF32JibakuLaunchDistGrid(*dist_y);
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
	/*
	 * Dream Land soft platforms / ledge lines often carry MAP_VERTEX_COLL_CLIFF (0x8000) without
	 * PASS (0x4000). soak2 session 362259664 FC @600: Kirby LandingAirF→Wait on fflags=0x8000
	 * walked ~5u apart cross-ISA after matched AttackAirF fall; PASS-only harden never fired.
	 */
	if ((fp->coll_data.floor_flags & (MAP_VERTEX_COLL_PASS | MAP_VERTEX_COLL_CLIFF)) == 0U)
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

/*
 * Airborne over PASS|CLIFF soft floors: CliffFloorCeil MpLanding `branch=diff` accumulates
 * cross-ISA TopN X while grounded pass harden never fires (ga==Air).
 *
 * Originally status-gated to JumpAerial/Fall (+ Kirby F1–F5); soak1 `325987316` @720/@919 still
 * forked JumpB→JumpAerialF and Kirby CopyMario SpecialAirN (Ness) on PASS|CLIFF with inputs MATCH
 * because those statuses were outside the gate. Re-anchor is status-agnostic for any air fighter
 * on PASS|CLIFF: pos_prev := TopN, pos_diff := 0 at BeforeSim so the tick's collision segment is
 * exactly that tick's physics displacement (same class as grounded pass harden).
 * Note: ftMainProcPhysicsMap already snapshots pos_prev=TopN each tick; this harden mainly
 * covers snapshot capture/load + quantize. Live Cross-ISA TopN.x forks on CLIFF-only lips are
 * AdjNew wall-from-floor / FloorEdge / landing-edge (PASS skips wall-from-floor) — see
 * docs/bugs/netplay_airborne_cliff_lip_wall_from_floor_fc_drift_2026-07-13.md and
 * docs/bugs/netplay_airborne_cliff_lip_jumpaerial_fc_drift_2026-07-18.md.
 * See also docs/bugs/netplay_jumpaerial_pass_floor_fc_drift_2026-07-12.md and
 * docs/bugs/netplay_airborne_pass_cliff_coll_harden_fc_drift_2026-07-13.md.
 */
sb32 syNetplayFighterInJumpAerialPassCollScope(const FTStruct *fp)
{
	if ((fp == NULL) || (fp->ga != nMPKineticsAir))
	{
		return FALSE;
	}
	if ((fp->coll_data.floor_flags & (MAP_VERTEX_COLL_PASS | MAP_VERTEX_COLL_CLIFF)) == 0U)
	{
		return FALSE;
	}
	return TRUE;
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

sb32 syNetplayFighterInCaptainFalconDiveScope(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	if ((fp->fkind != nFTKindCaptain) && (fp->fkind != nFTKindNCaptain))
	{
		return FALSE;
	}
	switch (fp->status_id)
	{
	case nFTCaptainStatusSpecialHi:
	case nFTCaptainStatusSpecialHiCatch:
	case nFTCaptainStatusSpecialHiThrow:
	case nFTCaptainStatusSpecialAirHi:
		return TRUE;
	default:
		return FALSE;
	}
}

static void syNetplayHardenCaptainFalconDiveCollForFighter(GObj *fighter_gobj)
{
	FTStruct *fp;
	Vec3f *topn;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (syNetplayFighterInCaptainFalconDiveScope(fp) == FALSE)
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

void syNetplayCanonicalizeCaptainFalconDiveSimState(GObj *fighter_gobj)
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
	if (syNetplayFighterInCaptainFalconDiveScope(fp) == FALSE)
	{
		return;
	}
	fp->physics.vel_ground.x = syNetplayQuantizeF32(fp->physics.vel_ground.x);
	fp->physics.vel_ground.y = syNetplayQuantizeF32(fp->physics.vel_ground.y);
	fp->physics.vel_ground.z = syNetplayQuantizeF32(fp->physics.vel_ground.z);
	fp->physics.vel_air.x = syNetplayQuantizeF32(fp->physics.vel_air.x);
	fp->physics.vel_air.y = syNetplayQuantizeF32(fp->physics.vel_air.y);
	fp->physics.vel_air.z = syNetplayQuantizeF32(fp->physics.vel_air.z);
	fp->status_vars.captain.specialhi.vel.x = syNetplayQuantizeF32(fp->status_vars.captain.specialhi.vel.x);
	fp->status_vars.captain.specialhi.vel.y = syNetplayQuantizeF32(fp->status_vars.captain.specialhi.vel.y);
	fp->status_vars.captain.specialhi.vel.z = syNetplayQuantizeF32(fp->status_vars.captain.specialhi.vel.z);
	topn_joint = fp->joints[nFTPartsJointTopN];
	if (topn_joint != NULL)
	{
		syNetplayQuantizeDObjTranslate(topn_joint);
		topn_joint->rotate.vec.f.z = syNetplayQuantizeF32(topn_joint->rotate.vec.f.z);
	}
	syNetplayHardenCaptainFalconDiveCollForFighter(fighter_gobj);
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

static void syNetplayHardenJumpAerialPassCollForFighter(GObj *fighter_gobj)
{
	FTStruct *fp;
	Vec3f *topn;
	ftCommonJumpAerialStatusVars *jumpaerial;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (syNetplayFighterInJumpAerialPassCollScope(fp) == FALSE)
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
		/*
		 * soak1 1828471508: Ness JumpAerialF over Dream Land CLIFF lip forked TopN.x
		 * while Y matched. Ness/Yoshi double-jump feeds vel from TransN + status_vars
		 * vel_x/drift — quantize those with vel_air so BeforeSim/post-tick cannot keep
		 * raw cross-ISA drift into the next CliffFloorCeil segment.
		 */
		syNetplayQuantizeFighterPhysics(&fp->physics);
		if ((fp->status_id == nFTCommonStatusJumpAerialF) || (fp->status_id == nFTCommonStatusJumpAerialB))
		{
			jumpaerial = ftStatusVarsJumpAerial(fp);
			jumpaerial->drift = syNetplayQuantizeF32(jumpaerial->drift);
			jumpaerial->vel_x = syNetplayQuantizeF32(jumpaerial->vel_x);
		}
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
 * status_total_tics fork; @371591666 Link SpecialN charge end fork → CopyLink hit knockback FC @600;
 * soak2 @1440881291 Kirby Turn/Dash status_total_tics ±1 with inputs MATCH;
 * soak1 @900855595 Turn→WalkFast status_total_tics 2↔1 — anim_wait = speed+ULP skipped end one tick;
 * soak1 @100749819 Turn→Wait @594 vs @595 — post-subtract quantize leftover in (0,eps]).
 *
 * Hardening:
 * 1) BeforeSim / post-tick: gobj anim_frame near zero → 0 for ProcUpdate gates
 * 2) BeforeSim / post-tick: per-joint anim_wait in (0, speed+16/65536] → exactly speed
 * 3) In-play AfterPlayStep: post-subtract wait in (0, 16/65536] → 0 (fall into end path)
 */
void syNetplaySnapGobjAnimFrameToEndIfNearZero(GObj *gobj)
{
	DObj *root_dobj;
	f32 q_anim;
	/* Wider than one grid — ARM/x86 leftover after PlayAnim end can sit a few ULPs above 0. */
	const f32 release_grid = (256.0F / 65536.0F);

	if (gobj == NULL)
	{
		return;
	}
	q_anim = syNetplayQuantizeAnimScalar(gobj->anim_frame);
	if ((q_anim > 0.0F) && (q_anim <= release_grid))
	{
		gobj->anim_frame = 0.0F;
		root_dobj = DObjGetStruct(gobj);
		if (root_dobj != NULL)
		{
			root_dobj->anim_frame = 0.0F;
		}
	}
	else if (q_anim <= 0.0F)
	{
		/* Already non-positive (end leftover); pin exact 0 for ProcUpdate <= 0 gates. */
		if (q_anim > AOBJ_ANIM_END)
		{
			gobj->anim_frame = 0.0F;
			root_dobj = DObjGetStruct(gobj);
			if (root_dobj != NULL)
			{
				root_dobj->anim_frame = 0.0F;
			}
		}
	}
}

/*
 * Stage map GObjs (Whispy eyes/mouth) leave larger post-PlayAnim leftovers than fighters.
 * 256/65536 was enough for figatree wait gates but Dream Land Open→Blow still skewed one
 * tick cross-ISA (soak1 3628321978 @2399/@2400). Use 1/16 frame.
 */
void syNetplaySnapMapGobjAnimFrameToEndIfNearZero(GObj *gobj)
{
	DObj *root_dobj;
	f32 q_anim;
	const f32 release_grid = (4096.0F / 65536.0F);

	if (gobj == NULL)
	{
		return;
	}
	q_anim = syNetplayQuantizeAnimScalar(gobj->anim_frame);
	if ((q_anim > 0.0F) && (q_anim <= release_grid))
	{
		gobj->anim_frame = 0.0F;
		root_dobj = DObjGetStruct(gobj);
		if (root_dobj != NULL)
		{
			root_dobj->anim_frame = 0.0F;
		}
	}
	else if (q_anim <= 0.0F)
	{
		if (q_anim > AOBJ_ANIM_END)
		{
			gobj->anim_frame = 0.0F;
			root_dobj = DObjGetStruct(gobj);
			if (root_dobj != NULL)
			{
				root_dobj->anim_frame = 0.0F;
			}
		}
	}
}

static void syNetplaySnapAnimFrameToEndIfNearZero(GObj *fighter_gobj)
{
	syNetplaySnapGobjAnimFrameToEndIfNearZero(fighter_gobj);
}

sb32 syNetplayMapGobjAnimFrameEnded(GObj *gobj)
{
	if (gobj == NULL)
	{
		return TRUE;
	}
#if defined(SSB64_NETMENU)
	if (syNetplayRollbackSemanticsActive() != FALSE)
	{
		syNetplaySnapMapGobjAnimFrameToEndIfNearZero(gobj);
	}
#endif
	return (gobj->anim_frame <= 0.0F) ? TRUE : FALSE;
}

void syNetplayHardenPupupuWhispyMapAnimBeforeSim(void)
{
#if defined(SSB64_NETMENU)
	s32 i;

	if (syNetplayRollbackLiveForwardSimEligible() == FALSE)
	{
		return;
	}
	if ((gSCManagerBattleState == NULL) || (gSCManagerBattleState->gkind != nGRKindPupupu))
	{
		return;
	}
	for (i = 0; i < 4; i++)
	{
		syNetplaySnapMapGobjAnimFrameToEndIfNearZero(gGRCommonStruct.pupupu.map_gobj[i]);
	}
#endif
}

static void syNetplaySnapDObjAnimWaitIfAboutToEnd(DObj *dobj)
{
	f32 wait;
	f32 speed;
	f32 q_wait;
	f32 q_speed;
	/*
	 * Fixed-point play still needs a before-play snap when wait sits a few grid units above
	 * speed (figatree length accumulation). 256/65536 ≈ 1/256 frame.
	 */
	const f32 eps = (256.0F / 65536.0F);

	if (dobj == NULL)
	{
		return;
	}
	wait = dobj->anim_wait;
	/*
	 * Live countdown only. Sentinels are <= 0 (F32_MIN family); retired joints use AOBJ_ANIM_END.
	 */
	if (!(wait > 0.0F))
	{
		return;
	}
	speed = dobj->anim_speed;
	if (!(speed > 0.0F))
	{
		speed = 1.0F;
	}
	q_wait = syNetplayQuantizeAnimScalar(wait);
	q_speed = syNetplayQuantizeAnimScalar(speed);
	/*
	 * gcPlayDObjAnimJoint: wait -= speed; if (wait > 0) continue; else end.
	 * Only snap when wait is at/above one full step (speed … speed+eps]. Do not inflate
	 * tiny leftovers up to speed — that would delay end by a full frame.
	 */
	if ((q_wait >= q_speed) && (q_wait <= (q_speed + eps)))
	{
		dobj->anim_wait = q_speed;
	}
}

static void syNetplaySnapFighterAnimWaitsIfAboutToEnd(GObj *fighter_gobj, FTStruct *fp)
{
	DObj *root_dobj;
	s32 ji;

	if (fighter_gobj == NULL)
	{
		return;
	}
	root_dobj = DObjGetStruct(fighter_gobj);
	if (root_dobj != NULL)
	{
		syNetplaySnapDObjAnimWaitIfAboutToEnd(root_dobj);
	}
	if (fp == NULL)
	{
		return;
	}
	for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
	{
		if (fp->joints[ji] != NULL)
		{
			syNetplaySnapDObjAnimWaitIfAboutToEnd(fp->joints[ji]);
		}
	}
}

static sb32 syNetplayFighterInLocomotionAnimEndWaitScope(const FTStruct *fp)
{
	s32 status_id;

	if (fp == NULL)
	{
		return FALSE;
	}
	status_id = fp->status_id;
	/*
	 * Unconditional / ftAnimEndCheckSetStatus gates on anim_frame <= 0.0F.
	 * Ranges cover audit P0/P1 common families (docs/bugs/netplay_anim_end_harden_audit_2026-07-11.md).
	 *
	 * Turn / TurnRun are intentionally excluded: BeforeSim anim_frame / anim_wait snaps can
	 * desync figatree SetFlag1 from Turn ProcUpdate (`is_allow_turn_direction`), which blocks
	 * InvertLR Turn→Dash (dash-dance). See docs/bugs/netplay_turn_dash_allow_anim_harden_2026-07-12.md.
	 */
	if ((status_id == nFTCommonStatusWait) ||
	    ((status_id >= nFTCommonStatusWalkSlow) && (status_id <= nFTCommonStatusRunBrake)) ||
	    ((status_id >= nFTCommonStatusKneeBend) && (status_id <= nFTCommonStatusFallAerial)) ||
	    ((status_id >= nFTCommonStatusSquat) && (status_id <= nFTCommonStatusLandingHeavy)) ||
	    (status_id == nFTCommonStatusOttottoWait) || (status_id == nFTCommonStatusOttotto) ||
	    ((status_id >= nFTCommonStatusDamageStart) && (status_id <= nFTCommonStatusDamageFall)) ||
	    (status_id == nFTCommonStatusFallSpecial) || (status_id == nFTCommonStatusLandingFallSpecial) ||
	    ((status_id >= nFTCommonStatusDokanStart) && (status_id <= nFTCommonStatusDokanWalk)) ||
	    ((status_id >= nFTCommonStatusDownBounceD) && (status_id <= nFTCommonStatusPassive)) ||
	    ((status_id >= nFTCommonStatusLightGet) && (status_id <= nFTCommonStatusLiftTurn)) ||
	    ((status_id >= nFTCommonStatusLightThrowStart) &&
	     (status_id <= nFTCommonStatusFireFlowerShootAir)) ||
	    ((status_id >= nFTCommonStatusHammerStart) && (status_id <= nFTCommonStatusHammerEnd)) ||
	    (status_id == nFTCommonStatusGuardOff) ||
	    ((status_id >= nFTCommonStatusEscapeF) && (status_id <= nFTCommonStatusFuraSleep)) ||
	    ((status_id >= nFTCommonStatusCatch) && (status_id <= nFTCommonStatusThrownEnd)) ||
	    ((status_id >= nFTCommonStatusAttack11) && (status_id <= nFTCommonStatusLandingAirEnd)))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetplayFighterInKirbySpecialHiAnimEndScope(const FTStruct *fp)
{
	if ((fp == NULL) || (fp->fkind != nFTKindKirby))
	{
		return FALSE;
	}
	switch (fp->status_id)
	{
	case nFTKirbyStatusSpecialHi:
	case nFTKirbyStatusSpecialHiLanding:
	case nFTKirbyStatusSpecialAirHi:
	case nFTKirbyStatusSpecialAirHiFall:
		return TRUE;
	default:
		return FALSE;
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
			syNetplaySnapFighterAnimWaitsIfAboutToEnd(fighter_gobj, fp);
			syNetplaySnapAnimFrameToEndIfNearZero(fighter_gobj);
		}
		return;
	}
	if ((syNetplayFighterInLinkSpecialNAnimEndScope(fp) != FALSE) ||
	    (syNetplayFighterInKirbyCopyLinkSpecialNAnimEndScope(fp) != FALSE) ||
	    (syNetplayFighterInKirbySpecialHiAnimEndScope(fp) != FALSE))
	{
		syNetplaySnapFighterAnimWaitsIfAboutToEnd(fighter_gobj, fp);
		syNetplaySnapAnimFrameToEndIfNearZero(fighter_gobj);
		return;
	}
	if (syNetplayFighterInLocomotionAnimEndWaitScope(fp) != FALSE)
	{
		syNetplaySnapFighterAnimWaitsIfAboutToEnd(fighter_gobj, fp);
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
		syNetplaySnapFighterAnimWaitsIfAboutToEnd(fighter_gobj, fp);
		syNetplaySnapAnimFrameToEndIfNearZero(fighter_gobj);
	}
}

static void syNetplayHardenAnimEndWaitThresholdForFighter(GObj *fighter_gobj)
{
	FTStruct *fp;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp == NULL)
	{
		return;
	}
	syNetplayCanonicalizeAnimEndWaitThreshold(fighter_gobj, fp);
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
	syNetplayCanonicalizeCaptainFalconDiveSimState(fighter_gobj);
	if (syNetplayFighterInPassPlatformGroundCollScope(fp) != FALSE)
	{
		syNetplayHardenPassPlatformCollForFighter(fighter_gobj);
	}
	syNetplayHardenAirborneDamageKnockbackCollForFighter(fighter_gobj);
	syNetplayHardenJumpAerialPassCollForFighter(fighter_gobj);
#if defined(SSB64_NETMENU)
	syNetplayCanonicalizeAnimEndWaitThreshold(fighter_gobj, fp);
#endif
}

#if defined(SSB64_NETMENU)
void syNetplayHardenAnimEndWaitThresholdBeforeSim(void)
{
	GObj *fighter_gobj;

	if (syNetplayRollbackLiveForwardSimEligible() == FALSE)
	{
		return;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		syNetplayHardenAnimEndWaitThresholdForFighter(fighter_gobj);
	}
}

/*
 * InvertLR Turn entry pins ±1 here; Center pins 0. Restored by HardenTurnLrDash when the
 * live union slot is stomped (soak 1646535146 Android lr_dash -1→0 at allow frame).
 */
static s32 sSYNetplayTurnEntryLrDash[GMCOMMON_PLAYERS_MAX];
static sb32 sTurnDashWitnessCached = -999;

static sb32 syNetplayTurnDashWitnessEnabled(void)
{
	const char *e;

	if (sTurnDashWitnessCached == -999)
	{
		e = getenv("SSB64_TURN_DASH_WITNESS");
		sTurnDashWitnessCached = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	}
	return sTurnDashWitnessCached;
}

void syNetplayTurnNoteEntryLrDash(FTStruct *fp, s32 lr_dash)
{
	s32 pi;

	if (fp == NULL)
	{
		return;
	}
	pi = fp->player;
	if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX))
	{
		return;
	}
	sSYNetplayTurnEntryLrDash[pi] = lr_dash;
}

void syNetplayHardenTurnLrDash(FTStruct *fp)
{
	ftCommonTurnStatusVars *turn;
	s32 pi;
	s32 entry;
	s32 was;

	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return;
	}
	if ((fp == NULL) || (fp->status_id != nFTCommonStatusTurn))
	{
		return;
	}
	turn = ftStatusVarsTurn(fp);
	if (turn == NULL)
	{
		return;
	}
	pi = fp->player;
	if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX))
	{
		return;
	}
	entry = sSYNetplayTurnEntryLrDash[pi];
	/*
	 * Center turns intentionally keep lr_dash==0 (entry sticky 0). InvertLR / DashCheckTurn
	 * smash refresh pin ±1 — restore when union +0xC (attack4.unk_0xC) clears it.
	 */
	if ((entry == 0) || (turn->lr_dash == entry))
	{
		return;
	}
	was = turn->lr_dash;
	turn->lr_dash = entry;
	if (syNetplayTurnDashWitnessEnabled() != FALSE)
	{
		port_log(
		    "SSB64 TURN_DASH_WITNESS phase=harden_lr_dash tick=%u player=%d was=%d now=%d "
		    "entry=%d lr_turn=%d attacks4_buf=%d\n",
		    (unsigned)syNetInputGetTick(), (int)pi, was, turn->lr_dash, entry,
		    (int)turn->lr_turn, (int)turn->attacks4_buffer);
	}
}

void syNetplayMaybeLogTurnDashWitness(GObj *fighter_gobj, const char *phase, s32 flag1_before,
                                      sb32 did_dash)
{
	FTStruct *fp;
	ftCommonTurnStatusVars *turn;
	u32 tick;
	s32 entry;
	s32 pi;

	if (syNetplayTurnDashWitnessEnabled() == FALSE)
	{
		return;
	}
	if ((fighter_gobj == NULL) || (phase == NULL))
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (fp->status_id != nFTCommonStatusTurn))
	{
		return;
	}
	turn = ftStatusVarsTurn(fp);
	tick = syNetInputGetTick();
	pi = fp->player;
	entry = ((pi >= 0) && (pi < GMCOMMON_PLAYERS_MAX)) ? sSYNetplayTurnEntryLrDash[pi] : 0;
	port_log(
	    "SSB64 TURN_DASH_WITNESS phase=%s tick=%u player=%d flag1=%d allow=%d disable_sa=%d "
	    "lr_dash=%d entry_lr_dash=%d lr_turn=%d lr=%d sx=%d tap_x=%u attacks4_buf=%d "
	    "anim_frame=%.6f did_dash=%d\n",
	    phase, (unsigned)tick, (int)fp->player, (int)flag1_before,
	    (int)((turn != NULL) ? turn->is_allow_turn_direction : 0),
	    (int)((turn != NULL) ? turn->is_disable_sa_interrupts : 0),
	    (int)((turn != NULL) ? turn->lr_dash : 0), entry,
	    (int)((turn != NULL) ? turn->lr_turn : 0), (int)fp->lr,
	    (int)fp->input.pl.stick_range.x, (unsigned)fp->tap_stick_x,
	    (int)((turn != NULL) ? turn->attacks4_buffer : -1), (double)fighter_gobj->anim_frame,
	    (int)did_dash);
}

void syNetplayHardenTurnLrTurn(FTStruct *fp)
{
	ftCommonTurnStatusVars *turn;
	s32 repaired;
	s32 pi;

	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return;
	}
	if ((fp == NULL) || (fp->status_id != nFTCommonStatusTurn))
	{
		return;
	}
	turn = ftStatusVarsTurn(fp);
	if ((turn == NULL) || (turn->lr_turn != 0))
	{
		return;
	}
	/*
	 * InvertLR entry stores the same ±1 in lr_dash. Center turn has lr_dash==0; recover from
	 * facing (pre-flip: opposite; post-allow: facing is already the turn direction).
	 * Prefer entry sticky when live lr_dash was stomped (soak 1646535146).
	 */
	pi = fp->player;
	if (turn->lr_dash != 0)
	{
		repaired = turn->lr_dash;
	}
	else if ((pi >= 0) && (pi < GMCOMMON_PLAYERS_MAX) && (sSYNetplayTurnEntryLrDash[pi] != 0))
	{
		repaired = sSYNetplayTurnEntryLrDash[pi];
	}
	else if (turn->is_allow_turn_direction != FALSE)
	{
		repaired = fp->lr;
	}
	else
	{
		repaired = -fp->lr;
	}
	if (repaired == 0)
	{
		return;
	}
	turn->lr_turn = repaired;
}

static sb32 sJaVelWitnessCached = -999;

static sb32 syNetplayJaVelWitnessEnabled(void)
{
	const char *e;

	if (sJaVelWitnessCached == -999)
	{
		e = getenv("SSB64_JA_VEL_WITNESS");
		sJaVelWitnessCached = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	}
	return sJaVelWitnessCached;
}

/*
 * Soak 2120480047 @514: SoftLipPhase ja matched @513 then Android stick+friction from
 * 10.68 → 9.04 while Linux math behaved as ja_in=0 → −0.84. Name the ProcPhysics
 * pipeline before any ja_vel harden.
 */
void syNetplayMaybeLogJumpAerialJaVelWitness(GObj *fighter_gobj, f32 ja_in, sb32 used_decmax,
                                             f32 ja_out, f32 drift, f32 vel_composed)
{
	FTStruct *fp;
	u32 ja_in_bits;
	u32 ja_out_bits;
	u32 drift_bits;
	u32 vel_bits;
	u32 sticky;
	u32 fflags;
	s32 pi;

	if (syNetplayJaVelWitnessEnabled() == FALSE)
	{
		return;
	}
	if ((syNetplayRollbackSemanticsActive() == FALSE) || (fighter_gobj == NULL))
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || ((fp->status_id != nFTCommonStatusJumpAerialF) &&
	                     (fp->status_id != nFTCommonStatusJumpAerialB)))
	{
		return;
	}
	pi = fp->player;
	sticky = ((pi >= 0) && (pi < GMCOMMON_PLAYERS_MAX)) ? mpProcessNetplaySoftLipStickyGet(pi) : 0U;
	fflags = fp->coll_data.floor_flags;
	memcpy(&ja_in_bits, &ja_in, sizeof(ja_in_bits));
	memcpy(&ja_out_bits, &ja_out, sizeof(ja_out_bits));
	memcpy(&drift_bits, &drift, sizeof(drift_bits));
	memcpy(&vel_bits, &vel_composed, sizeof(vel_bits));
	port_log(
	    "SSB64 JA_VEL_WITNESS phase=exit tick=%u player=%d status=%d stick_x=%d stick_y=%d "
	    "ja_in=0x%08X decmax=%d ja_out=0x%08X drift=0x%08X vel=0x%08X sticky=0x%08X "
	    "fflags=0x%08X resim=%d\n",
	    (unsigned)syNetInputGetTick(), (int)pi, (int)fp->status_id,
	    (int)fp->input.pl.stick_range.x, (int)fp->input.pl.stick_range.y,
	    (unsigned)ja_in_bits, (int)(used_decmax != FALSE), (unsigned)ja_out_bits,
	    (unsigned)drift_bits, (unsigned)vel_bits, (unsigned)sticky, (unsigned)fflags,
	    (int)(syNetRollbackIsResimulating() != FALSE));
}
#else
void syNetplayHardenAnimEndWaitThresholdBeforeSim(void)
{
}

void syNetplayMaybeLogTurnDashWitness(GObj *fighter_gobj, const char *phase, s32 flag1_before,
                                      sb32 did_dash)
{
	(void)fighter_gobj;
	(void)phase;
	(void)flag1_before;
	(void)did_dash;
}

void syNetplayMaybeLogJumpAerialJaVelWitness(GObj *fighter_gobj, f32 ja_in, sb32 used_decmax,
                                             f32 ja_out, f32 drift, f32 vel_composed)
{
	(void)fighter_gobj;
	(void)ja_in;
	(void)used_decmax;
	(void)ja_out;
	(void)drift;
	(void)vel_composed;
}

void syNetplayHardenTurnLrTurn(FTStruct *fp)
{
	(void)fp;
}

void syNetplayTurnNoteEntryLrDash(FTStruct *fp, s32 lr_dash)
{
	(void)fp;
	(void)lr_dash;
}

void syNetplayHardenTurnLrDash(FTStruct *fp)
{
	(void)fp;
}
#endif

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

void syNetplayHardenJumpAerialPassCollBeforeSim(void)
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
		syNetplayHardenJumpAerialPassCollForFighter(fighter_gobj);
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
