#include <sys/netplay_fallspecial_pass_diag.h>

#if defined(PORT) && defined(SSB64_NETMENU)

#include <ft/fighter.h>
#include <ft/ftchar/ftsamus/ftsamus.h>
#include <ft/ftcommon.h>
#include <ft/ftdef.h>
#include <ft/ftstatusvars.h>
#include <mp/map.h>
#include <sys/netinput.h>
#include <sys/netpeer.h>
#include <sys/netplay_fallspecial_pass_gate.h>
#include <sys/netplay_sim_quantize.h>
#include <sys/netrollback.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void port_log(const char *fmt, ...);
extern char *getenv(const char *name);

static sb32 s_syNetplayFallSpecialPassDiagEnvCache = -999;
static u32 s_syNetplayFallSpecialPassDiagLogCount;

sb32 syNetplayFallSpecialPassDiagEnabled(void)
{
	const char *env;

	if (s_syNetplayFallSpecialPassDiagEnvCache != -999)
	{
		return s_syNetplayFallSpecialPassDiagEnvCache;
	}
	env = getenv("SSB64_NETPLAY_FALLSPECIAL_PASS_DIAG");
	s_syNetplayFallSpecialPassDiagEnvCache =
	    ((env != NULL) && (env[0] != '\0') && (strcmp(env, "0") != 0)) ? TRUE : FALSE;
	return s_syNetplayFallSpecialPassDiagEnvCache;
}

static sb32 syNetplayFallSpecialPassDiagVerbose(void)
{
	const char *env = getenv("SSB64_NETPLAY_FALLSPECIAL_PASS_DIAG_VERBOSE");

	return ((env != NULL) && (env[0] != '\0') && (strcmp(env, "0") != 0)) ? TRUE : FALSE;
}

static u32 syNetplayFallSpecialPassDiagTickBound(const char *name, u32 default_val)
{
	const char *env = getenv(name);
	long v;

	if ((env == NULL) || (env[0] == '\0'))
	{
		return default_val;
	}
	v = strtol(env, NULL, 10);
	if (v < 0L)
	{
		return 0U;
	}
	if (v > 60000L)
	{
		return 60000U;
	}
	return (u32)v;
}

static sb32 syNetplayFallSpecialPassDiagTickInWindow(u32 tick)
{
	u32 tick_min = syNetplayFallSpecialPassDiagTickBound("SSB64_NETPLAY_FALLSPECIAL_PASS_DIAG_TICK_MIN", 0U);
	u32 tick_max = syNetplayFallSpecialPassDiagTickBound("SSB64_NETPLAY_FALLSPECIAL_PASS_DIAG_TICK_MAX", 60000U);

	if (tick < tick_min)
	{
		return FALSE;
	}
	if (tick > tick_max)
	{
		return FALSE;
	}
	return TRUE;
}

static sb32 syNetplayFallSpecialPassDiagShouldLog(sb32 force)
{
	if (syNetplayFallSpecialPassDiagEnabled() == FALSE)
	{
		return FALSE;
	}
	if (force != FALSE)
	{
		return TRUE;
	}
	if (syNetplayFallSpecialPassDiagVerbose() != FALSE)
	{
		return TRUE;
	}
	if (s_syNetplayFallSpecialPassDiagLogCount >= 4096U)
	{
		return FALSE;
	}
	s_syNetplayFallSpecialPassDiagLogCount++;
	return TRUE;
}

static sb32 syNetplayFallSpecialPassDiagIsKirbyCopySamus(const FTStruct *fp)
{
	if ((fp->fkind != nFTKindKirby) && (fp->fkind != nFTKindNKirby))
	{
		return FALSE;
	}
	return ((fp->passive_vars.kirby.copy_id == (s32)nFTKindSamus) ||
	        (fp->passive_vars.kirby.copy_id == (s32)nFTKindNSamus)) ?
	           TRUE :
	           FALSE;
}

static sb32 syNetplayFallSpecialPassDiagInScope(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	if (fp->pkind != nFTPlayerKindMan)
	{
		return FALSE;
	}
	if ((fp->fkind == nFTKindSamus) || (fp->fkind == nFTKindNSamus))
	{
		return TRUE;
	}
	if (syNetplayFallSpecialPassDiagIsKirbyCopySamus(fp) != FALSE)
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetplayFallSpecialPassDiagStatusInScope(const FTStruct *fp)
{
	if (syNetplayFallSpecialPassDiagInScope(fp) == FALSE)
	{
		return FALSE;
	}
	switch (fp->status_id)
	{
	case nFTSamusStatusSpecialAirHi:
	case nFTSamusStatusSpecialHi:
	case nFTCommonStatusFallSpecial:
	case nFTCommonStatusLandingFallSpecial:
		return TRUE;
	default:
		return FALSE;
	}
}

static const char *syNetplayFallSpecialPassDiagDenyReason(const FTStruct *fp, sb32 proc_pass_block, sb32 use_fallspecial_allow)
{
	sb32 pass_floor;
	s32 stick_y;

	if (proc_pass_block == FALSE)
	{
		return "none";
	}
	pass_floor = ((fp->coll_data.floor_flags & MAP_VERTEX_COLL_PASS) != 0) ? TRUE : FALSE;
	stick_y = fp->input.pl.stick_range.y;
	if (use_fallspecial_allow != FALSE)
	{
		if (ftStatusVarsFallSpecial(fp)->is_allow_pass == FALSE)
		{
			return "allow_pass0";
		}
		if (pass_floor == FALSE)
		{
			return "nopass_floor";
		}
		if (stick_y >= FTCOMMON_FALLSPECIAL_PASS_STICK_RANGE_MIN)
		{
			return "stick_high";
		}
		return "blocked";
	}
	if (pass_floor == FALSE)
	{
		return "nopass_floor";
	}
	if (stick_y >= FTSAMUS_SCREWATTACK_PASS_STICK_RANGE_MIN)
	{
		return "stick_high";
	}
	return "blocked";
}

static sb32 syNetplayFallSpecialPassDiagComputeProcPassBlock(GObj *fighter_gobj, const FTStruct *fp, sb32 use_fallspecial_allow)
{
	if (use_fallspecial_allow != FALSE)
	{
		syNetplayFallSpecialPassGateHardenAllowPass(fighter_gobj);
		if ((ftStatusVarsFallSpecial(fp)->is_allow_pass == FALSE) ||
		    !(fp->coll_data.floor_flags & MAP_VERTEX_COLL_PASS) ||
		    (fp->input.pl.stick_range.y >= FTCOMMON_FALLSPECIAL_PASS_STICK_RANGE_MIN))
		{
			return TRUE;
		}
		return FALSE;
	}
	if (!(fp->coll_data.floor_flags & MAP_VERTEX_COLL_PASS) ||
	    (fp->input.pl.stick_range.y >= FTSAMUS_SCREWATTACK_PASS_STICK_RANGE_MIN))
	{
		return TRUE;
	}
	return FALSE;
}

static void syNetplayFallSpecialPassDiagLogCore(GObj *fighter_gobj, const char *event, const char *site, sb32 proc_pass_block,
                                                sb32 use_fallspecial_allow, sb32 force)
{
	FTStruct *fp;
	u32 tick;
	sb32 pass_floor;
	sb32 allow_pass;
	const char *deny;

	if (syNetplayFallSpecialPassDiagShouldLog(force) == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (syNetplayFallSpecialPassDiagStatusInScope(fp) == FALSE))
	{
		return;
	}

	tick = syNetInputGetTick();
	if (syNetplayFallSpecialPassDiagTickInWindow(tick) == FALSE)
	{
		return;
	}

	pass_floor = ((fp->coll_data.floor_flags & MAP_VERTEX_COLL_PASS) != 0) ? TRUE : FALSE;
	allow_pass = (use_fallspecial_allow != FALSE) ? (ftStatusVarsFallSpecial(fp)->is_allow_pass != FALSE) : TRUE;
	deny = syNetplayFallSpecialPassDiagDenyReason(fp, proc_pass_block, use_fallspecial_allow);

	port_log(
	    "SSB64 FallSpecialPassDiag: event=%s site=%s tick=%u player=%d fkind=%d copy=%d status=%d motion=%d "
	    "anim=%.2f vel_y=%.2f stick_y=%d floor_flags=0x%02X pass_floor=%d allow_pass=%d block=%d deny=%s "
	    "mask=0x%04X rb=%d resim=%d rb_applied=%u\n",
	    (event != NULL) ? event : "?", (site != NULL) ? site : "?", tick, (int)fp->player, (int)fp->fkind,
	    (int)fp->passive_vars.kirby.copy_id, (int)fp->status_id, (int)fp->motion_id, fighter_gobj->anim_frame,
	    fp->physics.vel_air.y, (int)fp->input.pl.stick_range.y, (unsigned int)fp->coll_data.floor_flags,
	    (int)(pass_floor != FALSE), (int)(allow_pass != FALSE), (int)(proc_pass_block != FALSE), deny,
	    (unsigned int)fp->coll_data.mask_stat, (int)(syNetplayRollbackSemanticsActive() != FALSE),
	    (int)(syNetRollbackIsResimulating() != FALSE), (unsigned int)syNetRollbackGetAppliedResimCount());
}

void syNetplayFallSpecialPassDiagLogProcPass(GObj *fighter_gobj, const char *site, sb32 proc_pass_block)
{
	FTStruct *fp;
	sb32 force;
	sb32 use_fallspecial_allow;
	sb32 pass_floor;
	sb32 holding_down;

	if (syNetplayFallSpecialPassDiagEnabled() == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (syNetplayFallSpecialPassDiagStatusInScope(fp) == FALSE))
	{
		return;
	}

	use_fallspecial_allow = (fp->status_id == nFTCommonStatusFallSpecial) ? TRUE : FALSE;
	pass_floor = ((fp->coll_data.floor_flags & MAP_VERTEX_COLL_PASS) != 0) ? TRUE : FALSE;
	holding_down = (fp->input.pl.stick_range.y < FTCOMMON_FALLSPECIAL_PASS_STICK_RANGE_MIN) ? TRUE : FALSE;

	force = (proc_pass_block == FALSE) ? TRUE : FALSE;
	if ((pass_floor != FALSE) && (holding_down != FALSE))
	{
		force = TRUE;
	}
	if ((pass_floor != FALSE) && (use_fallspecial_allow != FALSE) &&
	    (ftStatusVarsFallSpecial(fp)->is_allow_pass == FALSE))
	{
		force = TRUE;
	}

	syNetplayFallSpecialPassDiagLogCore(fighter_gobj, "proc_pass", site, proc_pass_block, use_fallspecial_allow, force);
}

void syNetplayFallSpecialPassDiagLogFallSpecialEnter(GObj *fighter_gobj, const char *site)
{
	syNetplayFallSpecialPassDiagLogCore(fighter_gobj, "fallspecial_enter", site, TRUE, TRUE, TRUE);
}

void syNetplayFallSpecialPassDiagLogPassCliff(GObj *fighter_gobj, const char *site)
{
	FTStruct *fp;
	sb32 use_fallspecial_allow;
	sb32 proc_pass_block;

	if (syNetplayFallSpecialPassDiagEnabled() == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (syNetplayFallSpecialPassDiagStatusInScope(fp) == FALSE))
	{
		return;
	}
	use_fallspecial_allow = (fp->status_id == nFTCommonStatusFallSpecial) ? TRUE : FALSE;
	proc_pass_block = syNetplayFallSpecialPassDiagComputeProcPassBlock(fighter_gobj, fp, use_fallspecial_allow);
	syNetplayFallSpecialPassDiagLogCore(fighter_gobj, "pass_cliff", site, proc_pass_block, use_fallspecial_allow, TRUE);
}

#else /* !(PORT && SSB64_NETMENU) */

sb32 syNetplayFallSpecialPassDiagEnabled(void)
{
	return FALSE;
}

void syNetplayFallSpecialPassDiagLogProcPass(GObj *fighter_gobj, const char *site, sb32 proc_pass_block)
{
	(void)fighter_gobj;
	(void)site;
	(void)proc_pass_block;
}

void syNetplayFallSpecialPassDiagLogFallSpecialEnter(GObj *fighter_gobj, const char *site)
{
	(void)fighter_gobj;
	(void)site;
}

void syNetplayFallSpecialPassDiagLogPassCliff(GObj *fighter_gobj, const char *site)
{
	(void)fighter_gobj;
	(void)site;
}

#endif /* PORT && SSB64_NETMENU */
