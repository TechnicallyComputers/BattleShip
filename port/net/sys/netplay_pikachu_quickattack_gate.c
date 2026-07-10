#include <sys/netplay_pikachu_quickattack_gate.h>
#include <sys/netrollbacksnapshot.h>

#if defined(PORT) && defined(SSB64_NETMENU)

#include <ft/fighter.h>
#include <ft/ftchar/ftpikachu/ftpikachu.h>
#include <ft/ftchar/ftpikachu/ftpikachufunctions.h>
#include <ft/ftcommon/ftcommonfunctions.h>
#include <ft/ftdef.h>
#include <mp/mpdef.h>
#include <sys/netplay_sim_quantize.h>
#include <sys/objman.h>
#include <stdlib.h>

extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);

static sb32 sSYNetplayPikachuQuickAttackGateDiagCache = -999;

static sb32 syNetplayPikachuFighterKindIsPikachu(s32 fkind)
{
	return (fkind == nFTKindPikachu) || (fkind == nFTKindNPikachu) ? TRUE : FALSE;
}

static sb32 syNetplayPikachuQuickAttackF32Matches(f32 live, f32 expected)
{
	f32 q_live;
	f32 q_expected;

	if (syNetplaySimQuantizeActive() != FALSE)
	{
		q_live = syNetplayQuantizeF32(live);
		q_expected = syNetplayQuantizeF32(expected);
	}
	else
	{
		q_live = live;
		q_expected = expected;
	}
	return (q_live == q_expected) ? TRUE : FALSE;
}

sb32 syNetplayPikachuQuickAttackGateDiagEnabled(void)
{
	const char *env;

	if (sSYNetplayPikachuQuickAttackGateDiagCache != -999)
	{
		return (sSYNetplayPikachuQuickAttackGateDiagCache != 0) ? TRUE : FALSE;
	}
	env = getenv("SSB64_NETPLAY_PIKACHU_QUICKATTACK_GATE_DIAG");
	sSYNetplayPikachuQuickAttackGateDiagCache = ((env != NULL) && (env[0] != '\0') && (atoi(env) != 0)) ? 1 : 0;
	return (sSYNetplayPikachuQuickAttackGateDiagCache != 0) ? TRUE : FALSE;
}

sb32 syNetplayPikachuFighterInQuickAttackScope(s32 status_id)
{
	if ((status_id >= nFTPikachuStatusSpecialHiStart) && (status_id <= nFTPikachuStatusSpecialAirHiEnd))
	{
		return TRUE;
	}
	return FALSE;
}

sb32 syNetplayPikachuFighterInQuickAttackZipScope(s32 status_id)
{
	return ((status_id == nFTPikachuStatusSpecialHi) || (status_id == nFTPikachuStatusSpecialAirHi)) ? TRUE : FALSE;
}

sb32 syNetplayPikachuFighterInQuickAttackStartScope(s32 status_id)
{
	return ((status_id == nFTPikachuStatusSpecialHiStart) || (status_id == nFTPikachuStatusSpecialAirHiStart))
	           ? TRUE
	           : FALSE;
}

sb32 syNetplayPikachuFighterInQuickAttackEndScope(s32 status_id)
{
	return ((status_id == nFTPikachuStatusSpecialHiEnd) || (status_id == nFTPikachuStatusSpecialAirHiEnd)) ? TRUE
	                                                                                                      : FALSE;
}

sb32 syNetplayPikachuFighterInQuickAttackLandingFallScope(const FTStruct *fp)
{
	if ((fp == NULL) || (syNetplayPikachuFighterKindIsPikachu(fp->fkind) == FALSE))
	{
		return FALSE;
	}
	if ((fp->status_id != nFTCommonStatusFallSpecial) && (fp->status_id != nFTCommonStatusLandingFallSpecial))
	{
		return FALSE;
	}
	if ((syNetplayPikachuQuickAttackF32Matches(ftStatusVarsFallSpecial(fp)->drift,
	                                         FTPIKACHU_QUICKATTACK_FALLSPECIAL_DRIFT) == FALSE) ||
	    (syNetplayPikachuQuickAttackF32Matches(ftStatusVarsFallSpecial(fp)->landing_lag,
	                                           FTPIKACHU_QUICKATTACK_LANDING_LAG) == FALSE))
	{
		return FALSE;
	}
	return TRUE;
}

sb32 syNetplayPikachuFighterInQuickAttackSynctestDeferScope(const FTStruct *fp)
{
	if ((fp == NULL) || (syNetplayPikachuFighterKindIsPikachu(fp->fkind) == FALSE))
	{
		return FALSE;
	}
	if (syNetplayPikachuFighterInQuickAttackScope(fp->status_id) != FALSE)
	{
		return TRUE;
	}
	return syNetplayPikachuFighterInQuickAttackLandingFallScope(fp);
}

sb32 syNetplayPikachuFighterInQuickAttackShockFxScope(const FTStruct *fp)
{
	if ((fp == NULL) || (syNetplayPikachuFighterKindIsPikachu(fp->fkind) == FALSE))
	{
		return FALSE;
	}
	return syNetplayPikachuFighterInQuickAttackScope(fp->status_id);
}

sb32 syNetplayPikachuLiveHasQuickAttackSynctestDeferScope(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp != NULL) && (syNetplayPikachuFighterInQuickAttackSynctestDeferScope(fp) != FALSE))
		{
			return TRUE;
		}
	}
	return FALSE;
}

void syNetplayPikachuSanitizeQuickAttackStatusVars(FTStruct *fp)
{
	s32 *anim_length;

	if ((fp == NULL) || (syNetplayPikachuFighterKindIsPikachu(fp->fkind) == FALSE) ||
	    (syNetplayPikachuFighterInQuickAttackScope(fp->status_id) == FALSE))
	{
		return;
	}
	anim_length = &fp->status_vars.pikachu.specialhi.anim_frames;
	if (syNetplayPikachuFighterInQuickAttackStartScope(fp->status_id) != FALSE)
	{
		if (*anim_length < 0)
		{
			*anim_length = 0;
		}
		else if (*anim_length > (s32)FTPIKACHU_QUICKATTACK_START_TIME)
		{
			*anim_length = (s32)FTPIKACHU_QUICKATTACK_START_TIME;
		}
	}
	else if (syNetplayPikachuFighterInQuickAttackZipScope(fp->status_id) != FALSE)
	{
		if (*anim_length < 0)
		{
			*anim_length = 0;
		}
		else if (*anim_length > (s32)FTPIKACHU_QUICKATTACK_ZIP_TIME)
		{
			*anim_length = (s32)FTPIKACHU_QUICKATTACK_ZIP_TIME;
		}
	}
	if (fp->status_vars.pikachu.specialhi.pass_timer < 0)
	{
		fp->status_vars.pikachu.specialhi.pass_timer = 0;
	}
	else if (fp->status_vars.pikachu.specialhi.pass_timer > (s32)FTPIKACHU_QUICKATTACK_PASS_BUFFER_MAX)
	{
		fp->status_vars.pikachu.specialhi.pass_timer = (s32)FTPIKACHU_QUICKATTACK_PASS_BUFFER_MAX;
	}
}

void syNetplayPikachuSanitizeAllFightersAfterSlotApply(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if (fp != NULL)
		{
			syNetplayPikachuSanitizeQuickAttackStatusVars(fp);
		}
	}
}

void syNetplayPikachuCatchUpQuickAttackIfDue(GObj *fighter_gobj, FTStruct *fp)
{
	if ((fighter_gobj == NULL) || (fp == NULL) || (syNetplayPikachuFighterKindIsPikachu(fp->fkind) == FALSE))
	{
		return;
	}
	syNetplayPikachuSanitizeQuickAttackStatusVars(fp);
	if (syNetplayPikachuFighterInQuickAttackStartScope(fp->status_id) != FALSE)
	{
		if (fp->status_vars.pikachu.specialhi.anim_frames > 0)
		{
			return;
		}
		if (syNetplayPikachuQuickAttackGateDiagEnabled() != FALSE)
		{
			port_log(
			    "SSB64 Netplay: PIKACHU_QUICKATTACK_GATE tick=? event=start_catchup player=%d status=%d anim_frames=%d\n",
			    (int)fp->player, (int)fp->status_id, (int)fp->status_vars.pikachu.specialhi.anim_frames);
		}
		if (fp->status_id == nFTPikachuStatusSpecialAirHiStart)
		{
			ftPikachuSpecialAirHiSetStatus(fighter_gobj);
		}
		else
		{
			ftPikachuSpecialHiSetStatus(fighter_gobj);
		}
		return;
	}
	if (syNetplayPikachuFighterInQuickAttackZipScope(fp->status_id) != FALSE)
	{
		if (fp->status_vars.pikachu.specialhi.anim_frames > 0)
		{
			return;
		}
		if (syNetplayPikachuQuickAttackGateDiagEnabled() != FALSE)
		{
			port_log(
			    "SSB64 Netplay: PIKACHU_QUICKATTACK_GATE tick=? event=zip_catchup player=%d status=%d anim_frames=%d\n",
			    (int)fp->player, (int)fp->status_id, (int)fp->status_vars.pikachu.specialhi.anim_frames);
		}
		if (fp->status_id == nFTPikachuStatusSpecialAirHi)
		{
			ftPikachuSpecialAirHiEndSetStatus(fighter_gobj);
		}
		else
		{
			ftPikachuSpecialHiEndSetStatus(fighter_gobj);
		}
		return;
	}
	if (syNetplayPikachuFighterInQuickAttackEndScope(fp->status_id) == FALSE)
	{
		return;
	}
	if ((fp->motion_vars.flags.flag1 == 1) || (fighter_gobj->anim_frame > 0.0F))
	{
		return;
	}
	if (syNetplayPikachuQuickAttackGateDiagEnabled() != FALSE)
	{
		port_log(
		    "SSB64 Netplay: PIKACHU_QUICKATTACK_GATE tick=? event=end_catchup player=%d status=%d anim_frame=%f flag1=%d\n",
		    (int)fp->player,
		    (int)fp->status_id,
		    (double)fighter_gobj->anim_frame,
		    (int)fp->motion_vars.flags.flag1);
	}
	if (fp->status_id == nFTPikachuStatusSpecialAirHiEnd)
	{
		ftCommonFallSpecialSetStatus(fighter_gobj, FTPIKACHU_QUICKATTACK_FALLSPECIAL_DRIFT, FALSE, TRUE, TRUE,
		                           FTPIKACHU_QUICKATTACK_LANDING_LAG, FALSE);
	}
	else
	{
		ftCommonWaitSetStatus(fighter_gobj);
	}
}

sb32 syNetplayPikachuClampFcRecoveryLoadTick(u32 *io_load_tick, u32 *io_mismatch_tick)
{
	u32 load_tick;

	if ((io_load_tick == NULL) || (io_mismatch_tick == NULL))
	{
		return FALSE;
	}
	load_tick = *io_load_tick;
	if (syNetRbSnapshotPikachuQuickAttackCatchUpPendingAtTick(load_tick) == FALSE)
	{
		return FALSE;
	}
	if ((load_tick + 1U) >= *io_mismatch_tick)
	{
		return FALSE;
	}
	if (syNetplayPikachuQuickAttackGateDiagEnabled() != FALSE)
	{
		port_log(
		    "SSB64 Netplay: PIKACHU_QUICKATTACK_GATE event=fc_recovery_load_clamp load=%u->%u mismatch=%u->%u\n",
		    load_tick,
		    load_tick + 1U,
		    *io_mismatch_tick,
		    load_tick + 2U);
	}
	*io_load_tick = load_tick + 1U;
	if (*io_mismatch_tick <= (load_tick + 1U))
	{
		*io_mismatch_tick = load_tick + 2U;
	}
	return TRUE;
}

void syNetplayPikachuCatchUpAllAfterLoadVerify(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp == NULL) || (syNetplayPikachuFighterKindIsPikachu(fp->fkind) == FALSE))
		{
			continue;
		}
		if ((syNetplayPikachuFighterInQuickAttackScope(fp->status_id) == FALSE) &&
		    (syNetplayPikachuFighterInQuickAttackEndScope(fp->status_id) == FALSE) &&
		    (syNetplayPikachuFighterInQuickAttackLandingFallScope(fp) == FALSE))
		{
			continue;
		}
		syNetplayPikachuCatchUpQuickAttackIfDue(fighter_gobj, fp);
	}
	syNetRbSnapshotRebindAllFighters();
}

#endif /* PORT && SSB64_NETMENU */
