#include <sys/netplay_rebirth_gate.h>

#if defined(PORT) && defined(SSB64_NETMENU)

#include <sys/netinput.h>
#include <sys/netpeer.h>
#include <sys/netplay_sim_quantize.h>
#include <sys/netrollbacksnapshot.h>
#include <sys/netsync.h>
#include <sys/objman.h>

#include <ft/fighter.h>
#include <ft/ftanimend.h>
#include <ft/ftcommon.h>
#include <ft/ftcommon/ftcommonfunctions.h>
#include <ft/ftdef.h>
#include <ft/ftparam.h>
#include <ft/fttypes.h>
#include <sc/scene.h>

#include <ef/efmanager.h>

#include <stdlib.h>

extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);

static sb32 sSYNetplayRebirthGateDiagCache = -999;
static sb32 sSYNetplayRebirthSimDiagCache = -999;
static s32 sSYNetplayRebirthSimDiagTickMin = -999999;
static s32 sSYNetplayRebirthSimDiagTickMax = -999999;

static u32 syNetplayRebirthGateSimTick(void);

static u32 syNetplayRebirthSimDiagF32Bits(f32 value)
{
	union
	{
		f32 fv;
		u32 uv;
	} reinterpret;

	reinterpret.fv = value;
	return reinterpret.uv;
}

sb32 syNetplayRebirthGateDiagEnabled(void)
{
	const char *env;

	if (sSYNetplayRebirthGateDiagCache != -999)
	{
		return (sSYNetplayRebirthGateDiagCache != 0) ? TRUE : FALSE;
	}
	env = getenv("SSB64_NETPLAY_REBIRTH_GATE_DIAG");
	sSYNetplayRebirthGateDiagCache = ((env != NULL) && (env[0] != '\0') && (atoi(env) != 0)) ? 1 : 0;
	return (sSYNetplayRebirthGateDiagCache != 0) ? TRUE : FALSE;
}

static sb32 syNetplayRebirthFighterIsDeadStatus(s32 status_id)
{
	return ((status_id >= nFTCommonStatusDeadDown) && (status_id <= nFTCommonStatusDeadUpFall)) ? TRUE : FALSE;
}

sb32 syNetplayPlayerInDeadGhostStickAbsorbScope(s32 player)
{
	GObj *fighter_gobj;

	if ((syNetplayRollbackSemanticsActive() == FALSE) || (player < 0) || (player >= GMCOMMON_PLAYERS_MAX))
	{
		return FALSE;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp == NULL) || (fp->player != player))
		{
			continue;
		}
		/*
		 * Dead* only — do NOT key on is_ghost. RebirthWait sets is_ghost=TRUE but stick
		 * leave (GroundCheckInterrupt → Fall) is load-bearing; absorbing that REPLACE left
		 * the predictor at (0,0) while authority dropped (soak 1174892281 @4178–4180).
		 * See docs/bugs/netplay_rebirth_ghost_stick_absorb_leave_peer_2026-07-20.md.
		 */
		if (syNetplayRebirthFighterIsDeadStatus(fp->status_id) != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

void syNetplayRebirthGateLogLeaveStick(GObj *fighter_gobj, FTStruct *fp, const char *reason, s32 status_before)
{
	s32 sx;
	s32 sy;
	s32 halo_despawn;

	if ((fighter_gobj == NULL) || (fp == NULL) || (reason == NULL))
	{
		return;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	sx = (s32)fp->input.pl.stick_range.x;
	sy = (s32)fp->input.pl.stick_range.y;
	/* Only read rebirth overlay while it is live — after interrupt status may already be Fall. */
	halo_despawn = -1;
	if ((fp->status_id >= nFTCommonStatusRebirthDown) && (fp->status_id <= nFTCommonStatusRebirthWait))
	{
		halo_despawn = (s32)ftStatusVarsRebirth(fp)->halo_despawn_wait;
	}
	port_log(
	    "SSB64 Netplay: REBIRTH_LEAVE_STICK tick=%u player=%d reason=%s status_before=%d status_now=%d "
	    "sx=%d sy=%d halo_despawn=%d is_ghost=%u is_rebirth=%u\n",
	    syNetplayRebirthGateSimTick(),
	    (int)fp->player,
	    reason,
	    (int)status_before,
	    (int)fp->status_id,
	    (int)sx,
	    (int)sy,
	    (int)halo_despawn,
	    (unsigned int)(fp->is_ghost != FALSE),
	    (unsigned int)(fp->is_rebirth != FALSE));
}

static sb32 syNetplayRebirthFighterIsRebirthStatus(s32 status_id)
{
	return ((status_id >= nFTCommonStatusRebirthDown) && (status_id <= nFTCommonStatusRebirthWait)) ? TRUE : FALSE;
}

static u32 syNetplayRebirthGateSimTick(void)
{
	return syNetInputGetTick();
}

sb32 syNetplayRebirthSimDiagEnabled(void)
{
	const char *env;

	if (sSYNetplayRebirthSimDiagCache != -999)
	{
		return (sSYNetplayRebirthSimDiagCache != 0) ? TRUE : FALSE;
	}
	env = getenv("SSB64_NETPLAY_DEATH_REBIRTH_SIM_DIAG");
	sSYNetplayRebirthSimDiagCache = ((env != NULL) && (env[0] != '\0') && (atoi(env) != 0)) ? 1 : 0;
	return (sSYNetplayRebirthSimDiagCache != 0) ? TRUE : FALSE;
}

static sb32 syNetplayRebirthSimDiagTickInWindow(u32 tick)
{
	const char *env;
	s32 min_tick;
	s32 max_tick;

	if (sSYNetplayRebirthSimDiagTickMin == -999999)
	{
		min_tick = 0;
		max_tick = 0;
		env = getenv("SSB64_NETPLAY_DEATH_REBIRTH_SIM_DIAG_TICK_MIN");
		if ((env != NULL) && (env[0] != '\0'))
		{
			min_tick = atoi(env);
		}
		env = getenv("SSB64_NETPLAY_DEATH_REBIRTH_SIM_DIAG_TICK_MAX");
		if ((env != NULL) && (env[0] != '\0'))
		{
			max_tick = atoi(env);
		}
		if (max_tick <= 0)
		{
			sSYNetplayRebirthSimDiagTickMin = 0;
			sSYNetplayRebirthSimDiagTickMax = 0;
			return TRUE;
		}
		if (min_tick < 0)
		{
			min_tick = 0;
		}
		sSYNetplayRebirthSimDiagTickMin = min_tick;
		sSYNetplayRebirthSimDiagTickMax = max_tick;
	}
	if (sSYNetplayRebirthSimDiagTickMax == 0)
	{
		return TRUE;
	}
	return ((tick >= (u32)sSYNetplayRebirthSimDiagTickMin) && (tick <= (u32)sSYNetplayRebirthSimDiagTickMax)) ? TRUE
	                                                                                                       : FALSE;
}

void syNetplayRebirthSimDiagLogTick(u32 tick)
{
	GObj *fighter_gobj;

	if ((syNetplayRebirthSimDiagEnabled() == FALSE) || (syNetplayRebirthSimDiagTickInWindow(tick) == FALSE))
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
		FTStruct *fp;
		DObj *dobj;
		const char *scope;
		u32 gobj_tx;
		u32 gobj_ty;
		u32 gobj_tz;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			continue;
		}
		if ((syNetplayRebirthFighterIsDeadStatus(fp->status_id) == FALSE) &&
		    (syNetplayRebirthFighterIsRebirthStatus(fp->status_id) == FALSE))
		{
			continue;
		}
		scope = (syNetplayRebirthFighterIsRebirthStatus(fp->status_id) != FALSE) ? "rebirth" : "dead";
		gobj_tx = gobj_ty = gobj_tz = 0U;
		dobj = DObjGetStruct(fighter_gobj);
		if (dobj != NULL)
		{
			gobj_tx = syNetplayRebirthSimDiagF32Bits(dobj->translate.vec.f.x);
			gobj_ty = syNetplayRebirthSimDiagF32Bits(dobj->translate.vec.f.y);
			gobj_tz = syNetplayRebirthSimDiagF32Bits(dobj->translate.vec.f.z);
		}
		if (syNetplayRebirthFighterIsRebirthStatus(fp->status_id) != FALSE)
		{
			/*
			 * halo_effect_present: does the live effect pool actually hold the rebirth-halo (respawn
			 * platform) effect coupled to this fighter? is_effect_attach only records that one was made
			 * at RebirthDown; if a restore drops the halo effect the platform goes invisible while the
			 * fighter still behaves correctly. A 1->0 transition here names the tick the platform vanished.
			 */
			u32 halo_effect_present = (syNetRbSnapLiveFighterHasRebirthHalo(fighter_gobj) != FALSE) ? 1U : 0U;

			port_log(
			    "SSB64 Netplay: death_rebirth_sim tick=%u player=%d fkind=%d scope=%s status=%d motion=%d "
			    "motion_flag1=%d stock=%d hitlag=%u fhash_light=0x%08X fhash_full=0x%08X anim=0x%08X "
			    "gobj_tx=0x%08X gobj_ty=0x%08X gobj_tz=0x%08X vel_air_x=0x%08X vel_air_y=0x%08X vel_air_z=0x%08X "
			    "camera_mode=%u is_invisible=%u is_ghost=%u is_rebirth=%u is_effect_attach=%u halo_effect_present=%u "
			    "halo_lower=%d halo_despawn=%d halo_num=%d rebirth_pos_y=0x%08X rebirth_halo_y=0x%08X\n",
			    tick,
			    (int)fp->player,
			    (int)fp->fkind,
			    scope,
			    (int)fp->status_id,
			    (int)fp->motion_id,
			    (int)fp->motion_vars.flags.flag1,
			    (int)fp->stock_count,
			    (unsigned int)fp->hitlag_tics,
			    syNetSyncHashFighterStructLight(fp),
			    syNetSyncHashFighterSlotFull(fp),
			    syNetSyncHashFighterSlotAnim(fp, fighter_gobj),
			    gobj_tx,
			    gobj_ty,
			    gobj_tz,
			    syNetplayRebirthSimDiagF32Bits(fp->physics.vel_air.x),
			    syNetplayRebirthSimDiagF32Bits(fp->physics.vel_air.y),
			    syNetplayRebirthSimDiagF32Bits(fp->physics.vel_air.z),
			    (unsigned int)fp->camera_mode,
			    (unsigned int)(fp->is_invisible != FALSE),
			    (unsigned int)(fp->is_ghost != FALSE),
			    (unsigned int)(fp->is_rebirth != FALSE),
			    (unsigned int)(fp->is_effect_attach != FALSE),
			    halo_effect_present,
			    (int)ftStatusVarsRebirth(fp)->halo_lower_wait,
			    (int)ftStatusVarsRebirth(fp)->halo_despawn_wait,
			    (int)ftStatusVarsRebirth(fp)->halo_number,
			    syNetplayRebirthSimDiagF32Bits(ftStatusVarsRebirth(fp)->pos.y),
			    syNetplayRebirthSimDiagF32Bits(ftStatusVarsRebirth(fp)->halo_offset.y));
			syNetRbSnapDiagLogRebirthHaloPose("death_rebirth_sim", tick, fighter_gobj);
		}
		else
		{
			port_log(
			    "SSB64 Netplay: death_rebirth_sim tick=%u player=%d fkind=%d scope=%s status=%d motion=%d "
			    "motion_flag1=%d stock=%d hitlag=%u dead_wait=%d fhash_light=0x%08X fhash_full=0x%08X anim=0x%08X "
			    "gobj_tx=0x%08X gobj_ty=0x%08X gobj_tz=0x%08X vel_air_x=0x%08X vel_air_y=0x%08X vel_air_z=0x%08X "
			    "camera_mode=%u is_invisible=%u is_ghost=%u is_rebirth=%u\n",
			    tick,
			    (int)fp->player,
			    (int)fp->fkind,
			    scope,
			    (int)fp->status_id,
			    (int)fp->motion_id,
			    (int)fp->motion_vars.flags.flag1,
			    (int)fp->stock_count,
			    (unsigned int)fp->hitlag_tics,
			    (int)ftCommonDeadGetWait(fp),
			    syNetSyncHashFighterStructLight(fp),
			    syNetSyncHashFighterSlotFull(fp),
			    syNetSyncHashFighterSlotAnim(fp, fighter_gobj),
			    gobj_tx,
			    gobj_ty,
			    gobj_tz,
			    syNetplayRebirthSimDiagF32Bits(fp->physics.vel_air.x),
			    syNetplayRebirthSimDiagF32Bits(fp->physics.vel_air.y),
			    syNetplayRebirthSimDiagF32Bits(fp->physics.vel_air.z),
			    (unsigned int)fp->camera_mode,
			    (unsigned int)(fp->is_invisible != FALSE),
			    (unsigned int)(fp->is_ghost != FALSE),
			    (unsigned int)(fp->is_rebirth != FALSE));
		}
	}
}

void syNetplayRebirthGateLogDeadInit(GObj *fighter_gobj, FTStruct *fp)
{
	(void)fighter_gobj;

	if ((syNetplayRebirthGateDiagEnabled() == FALSE) || (fp == NULL))
	{
		return;
	}
	port_log(
	    "SSB64 Netplay: REBIRTH_GATE tick=%u event=dead_init player=%d status=%d stock=%d dead_wait=%d\n",
	    syNetplayRebirthGateSimTick(),
	    (int)fp->player,
	    (int)fp->status_id,
	    (int)fp->stock_count,
	    (int)ftCommonDeadGetWait(fp));
}

void syNetplayRebirthGateLogDeadWaitZero(GObj *fighter_gobj, FTStruct *fp)
{
	(void)fighter_gobj;

	if ((syNetplayRebirthGateDiagEnabled() == FALSE) || (fp == NULL))
	{
		return;
	}
	port_log(
	    "SSB64 Netplay: REBIRTH_GATE tick=%u event=dead_wait_zero player=%d status=%d stock=%d\n",
	    syNetplayRebirthGateSimTick(),
	    (int)fp->player,
	    (int)fp->status_id,
	    (int)fp->stock_count);
}

void syNetplayRebirthGateLogCheckRebirth(GObj *fighter_gobj, FTStruct *fp, const char *branch)
{
	(void)fighter_gobj;

	if ((syNetplayRebirthGateDiagEnabled() == FALSE) || (fp == NULL) || (branch == NULL))
	{
		return;
	}
	port_log(
	    "SSB64 Netplay: REBIRTH_GATE tick=%u event=check_rebirth player=%d branch=%s stock=%d\n",
	    syNetplayRebirthGateSimTick(),
	    (int)fp->player,
	    branch,
	    (int)fp->stock_count);
}

void syNetplayRebirthGateLogRebirthDownSetStatus(GObj *fighter_gobj, FTStruct *fp, s32 halo_number)
{
	(void)fighter_gobj;

	if ((syNetplayRebirthGateDiagEnabled() == FALSE) || (fp == NULL))
	{
		return;
	}
	port_log(
	    "SSB64 Netplay: REBIRTH_GATE tick=%u event=rebirth_down player=%d halo=%d is_rebirth=%d status=%d\n",
	    syNetplayRebirthGateSimTick(),
	    (int)fp->player,
	    (int)halo_number,
	    (int)(fp->is_rebirth != FALSE),
	    (int)fp->status_id);
}

void syNetplayRebirthSnapSyncBattleStock(FTStruct *fp)
{
	if ((fp == NULL) || (gSCManagerBattleState == NULL))
	{
		return;
	}
	if ((fp->player < 0) || (fp->player >= GMCOMMON_PLAYERS_MAX))
	{
		return;
	}
	gSCManagerBattleState->players[fp->player].stock_count = fp->stock_count;
}

void syNetplayRebirthSanitizeIsRebirthFlag(FTStruct *fp)
{
	if (fp == NULL)
	{
		return;
	}
	if (syNetplayRebirthFighterIsDeadStatus(fp->status_id) != FALSE)
	{
		fp->is_rebirth = FALSE;
	}
	else if (syNetplayRebirthFighterIsRebirthStatus(fp->status_id) != FALSE)
	{
		fp->is_rebirth = TRUE;
	}
}

sb32 syNetplayRebirthShouldForceSleepSetStatus(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	if (fp->stock_count != -1)
	{
		return FALSE;
	}
	if (fp->status_id == nFTCommonStatusSleep)
	{
		return FALSE;
	}
	return syNetplayRebirthFighterIsDeadStatus(fp->status_id);
}

void syNetplayRebirthApplyEliminationPresentation(GObj *fighter_gobj, FTStruct *fp)
{
	(void)fighter_gobj;

	if (fp == NULL)
	{
		return;
	}
	fp->is_invisible = TRUE;
	fp->is_shadow_hide = TRUE;
	fp->is_ghost = TRUE;
	fp->is_menu_ignore = TRUE;
	fp->camera_mode = nFTCameraModeGhost;
	fp->is_playertag_hide = TRUE;
}

void syNetplayRebirthCatchUpDeadGateIfDue(GObj *fighter_gobj, FTStruct *fp)
{
	if ((fighter_gobj == NULL) || (fp == NULL))
	{
		return;
	}
	if (syNetplayRebirthFighterIsDeadStatus(fp->status_id) == FALSE)
	{
		return;
	}
	if (ftCommonDeadGetWait(fp) > 0)
	{
		return;
	}
#if defined(PORT) && defined(SSB64_NETMENU)
	syNetplayRebirthGateLogDeadWaitZero(fighter_gobj, fp);
#endif
	ftCommonDeadCheckRebirth(fighter_gobj);
}

static void syNetplayRebirthGateLogLifecycleCatchUp(GObj *fighter_gobj, FTStruct *fp, const char *event)
{
	(void)fighter_gobj;

	if ((syNetplayRebirthGateDiagEnabled() == FALSE) || (fp == NULL) || (event == NULL))
	{
		return;
	}
	port_log(
	    "SSB64 Netplay: REBIRTH_GATE tick=%u event=%s player=%d status=%d halo_despawn=%d halo_lower=%d halo_num=%d\n",
	    syNetplayRebirthGateSimTick(),
	    event,
	    (int)fp->player,
	    (int)fp->status_id,
	    (int)ftStatusVarsRebirth(fp)->halo_despawn_wait,
	    (int)ftStatusVarsRebirth(fp)->halo_lower_wait,
	    (int)ftStatusVarsRebirth(fp)->halo_number);
}

/*
 * Forward sim can enter RebirthDown with lifecycle timers live but no halo GObj (MakeEffect failed
 * once, rollback skipped attach, etc.). Snapshot ensure only runs on load — mint the platform here.
 */
static void syNetplayRebirthEnsureLiveHaloIfDue(GObj *fighter_gobj, FTStruct *fp)
{
	GObj *effect_gobj;

	if ((fighter_gobj == NULL) || (fp == NULL))
	{
		return;
	}
	if (syNetplayRebirthFighterIsRebirthStatus(fp->status_id) == FALSE)
	{
		return;
	}
	if (syNetRbSnapLiveFighterHasRebirthHalo(fighter_gobj) != FALSE)
	{
		return;
	}
	{
		s32 reclaimed = 0;
		s32 ef_free_before = 0;
		s32 ef_free_after = 0;

		syNetRbSnapReclaimStaleEffectShellsForRebirthHalo(&reclaimed, &ef_free_before);
		effect_gobj = efManagerRebirthHaloMakeEffect(fighter_gobj, fp->attr->halo_size);
		ef_free_after = efManagerGetEffectStructFreeCount();
		if (effect_gobj != NULL)
		{
			fp->is_effect_attach = TRUE;
			if (syNetplayRebirthGateDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 Netplay: REBIRTH_GATE tick=%u event=halo_ensure player=%d status=%d gobj_id=%u reclaimed=%d ef_free=%d->%d\n",
				    syNetplayRebirthGateSimTick(),
				    (int)fp->player,
				    (int)fp->status_id,
				    (unsigned int)effect_gobj->id,
				    (int)reclaimed,
				    (int)ef_free_before,
				    (int)ef_free_after);
			}
		}
		else if (syNetplayRebirthGateDiagEnabled() != FALSE)
		{
			port_log(
			    "SSB64 Netplay: REBIRTH_GATE tick=%u event=halo_ensure_fail player=%d status=%d reclaimed=%d ef_free=%d->%d\n",
			    syNetplayRebirthGateSimTick(),
			    (int)fp->player,
			    (int)fp->status_id,
			    (int)reclaimed,
			    (int)ef_free_before,
			    (int)ef_free_after);
		}
	}
}

void syNetplayRebirthCatchUpLifecycleIfDue(GObj *fighter_gobj, FTStruct *fp)
{
	if ((fighter_gobj == NULL) || (fp == NULL))
	{
		return;
	}
	if (syNetplayRebirthFighterIsRebirthStatus(fp->status_id) == FALSE)
	{
		return;
	}
	if (fp->status_id == nFTCommonStatusRebirthDown)
	{
		if (ftStatusVarsRebirth(fp)->halo_despawn_wait ==
		    (FTCOMMON_REBIRTH_HALO_DESPAWN_WAIT - FTCOMMON_REBIRTH_HALO_STAND_WAIT))
		{
			syNetplayRebirthGateLogLifecycleCatchUp(fighter_gobj, fp, "rebirth_stand_catchup");
			ftCommonRebirthStandSetStatus(fighter_gobj);
		}
		return;
	}
	if (fp->status_id == nFTCommonStatusRebirthStand)
	{
		if (ftAnimEndCheckSetStatus(fighter_gobj, ftCommonRebirthWaitSetStatus) != FALSE)
		{
			syNetplayRebirthGateLogLifecycleCatchUp(fighter_gobj, fp, "rebirth_wait_catchup");
		}
		return;
	}
	if (fp->status_id == nFTCommonStatusRebirthWait)
	{
		if (ftStatusVarsRebirth(fp)->halo_despawn_wait == 0)
		{
			syNetplayRebirthGateLogLifecycleCatchUp(fighter_gobj, fp, "rebirth_fall_catchup");
			syNetplayRebirthGateLogLeaveStick(fighter_gobj, fp, "halo_timer_catchup", fp->status_id);
			ftParamSetTimedHitStatusInvincible(fp, FTCOMMON_REBIRTH_INVINCIBLE_FRAMES);
			ftCommonFallSetStatus(fighter_gobj);
		}
	}
}

void syNetplayRebirthCatchUpFightersTick(void)
{
	GObj *fighter_gobj;

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
		syNetplayRebirthCatchUpDeadGateIfDue(fighter_gobj, fp);
		syNetplayRebirthCatchUpLifecycleIfDue(fighter_gobj, fp);
		syNetplayRebirthEnsureLiveHaloIfDue(fighter_gobj, fp);
	}
}

#endif /* PORT && SSB64_NETMENU */
