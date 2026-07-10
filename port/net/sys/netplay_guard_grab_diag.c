#include <sys/netplay_guard_grab_diag.h>

#if defined(PORT) && defined(SSB64_NETMENU)

#include <ft/fighter.h>
#include <ft/ftdef.h>
#include <ft/ftstatusvars.h>
#include <sc/scdef.h>
#include <sc/scmanager.h>
#include <sc/sctypes.h>
#include <sys/netinput.h>
#include <sys/netpeer.h>
#include <sys/netplay_sim_quantize.h>
#include <sys/netrollback.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void port_log(const char *fmt, ...);
extern char *getenv(const char *name);

static sb32 s_syNetplayGuardGrabDiagEnvCache = -999;
static u32 s_syNetplayGuardGrabDiagLogCount;

sb32 syNetplayGuardGrabDiagEnabled(void)
{
	const char *env;

	if (s_syNetplayGuardGrabDiagEnvCache != -999)
	{
		return s_syNetplayGuardGrabDiagEnvCache;
	}
	env = getenv("SSB64_NETPLAY_GUARD_GRAB_DIAG");
	s_syNetplayGuardGrabDiagEnvCache =
	    ((env != NULL) && (env[0] != '\0') && (strcmp(env, "0") != 0)) ? TRUE : FALSE;
	return s_syNetplayGuardGrabDiagEnvCache;
}

static sb32 syNetplayGuardGrabDiagVerbose(void)
{
	const char *env = getenv("SSB64_NETPLAY_GUARD_GRAB_DIAG_VERBOSE");

	return ((env != NULL) && (env[0] != '\0') && (strcmp(env, "0") != 0)) ? TRUE : FALSE;
}

static sb32 syNetplayGuardGrabDiagShouldLog(sb32 force)
{
	if (syNetplayGuardGrabDiagEnabled() == FALSE)
	{
		return FALSE;
	}
	if (force != FALSE)
	{
		return TRUE;
	}
	if (syNetplayGuardGrabDiagVerbose() != FALSE)
	{
		return TRUE;
	}
	if (s_syNetplayGuardGrabDiagLogCount >= 4096U)
	{
		return FALSE;
	}
	s_syNetplayGuardGrabDiagLogCount++;
	return TRUE;
}

static sb32 syNetplayGuardGrabDiagSceneIsOfflineMode(u16 scene)
{
	return (scene == nSCKind1PTrainingMode) || (scene == nSCKind1PGame);
}

static sb32 syNetplayGuardGrabDiagRollbackAnomaly(u16 scene)
{
	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return FALSE;
	}
	return syNetplayGuardGrabDiagSceneIsOfflineMode(scene);
}

static void syNetplayGuardGrabDiagLogCore(GObj *fighter_gobj, const char *event, const char *detail)
{
	FTStruct *fp;
	u16 scene;
	sb32 rollback;
	sb32 vs_active;
	sb32 resim;
	sb32 anomaly;

	if (syNetplayGuardGrabDiagShouldLog(FALSE) == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (fp->pkind != nFTPlayerKindMan))
	{
		return;
	}

	scene = gSCManagerSceneData.scene_curr;
	rollback = syNetplayRollbackSemanticsActive();
	vs_active = syNetPeerIsVSSessionActive();
	resim = syNetRollbackIsResimulating();
	anomaly = syNetplayGuardGrabDiagRollbackAnomaly(scene);

	port_log(
	    "SSB64 GuardGrabDiag: event=%s tick=%u scene=%u player=%d status=%d is_shield=%d release_lag=%d "
	    "is_release=%d shield_hp=%d hold=0x%04X tap=0x%04X rel=0x%04X rb=%d vs=%d resim=%d anomaly=%d %s\n",
	    event, (unsigned int)syNetInputGetTick(), (unsigned int)scene, (int)fp->player, (int)fp->status_id,
	    (int)(fp->is_shield != FALSE), (int)ftStatusVarsGuard(fp)->release_lag,
	    (int)(ftStatusVarsGuard(fp)->is_release != FALSE), (int)fp->shield_health,
	    (unsigned int)fp->input.pl.button_hold, (unsigned int)fp->input.pl.button_tap,
	    (unsigned int)fp->input.pl.button_release, (int)(rollback != FALSE), (int)(vs_active != FALSE),
	    (int)(resim != FALSE), (int)(anomaly != FALSE), (detail != NULL) ? detail : "");
}

void syNetplayGuardGrabDiagLogRInputEdge(GObj *fighter_gobj)
{
	FTStruct *fp;
	sb32 r_tap;
	sb32 r_release;
	sb32 r_held;

	if (syNetplayGuardGrabDiagEnabled() == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (fp->pkind != nFTPlayerKindMan))
	{
		return;
	}

	r_tap = (fp->input.pl.button_tap & R_TRIG) != 0;
	r_release = (fp->input.pl.button_release & R_TRIG) != 0;
	r_held = (fp->input.pl.button_hold & R_TRIG) != 0;
	if ((r_tap == FALSE) && (r_release == FALSE) && ((syNetplayGuardGrabDiagVerbose() == FALSE) || (r_held == FALSE)))
	{
		return;
	}

	syNetplayGuardGrabDiagLogCore(fighter_gobj, "r_edge", "");
}

void syNetplayGuardGrabDiagLogCatchAttempt(GObj *fighter_gobj, sb32 success, const char *reason)
{
	FTStruct *fp;
	sb32 force;
	const char *event;

	if (syNetplayGuardGrabDiagEnabled() == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (fp->pkind != nFTPlayerKindMan))
	{
		return;
	}

	force = (success != FALSE) ? TRUE : FALSE;
	if ((success == FALSE) && (fp->status_id == nFTCommonStatusWait) && (fp->is_shield != FALSE) &&
	    ((fp->input.pl.button_hold & (R_TRIG | fp->input.button_mask_z)) != 0))
	{
		force = TRUE;
	}
	if ((success == FALSE) && ((fp->input.pl.button_tap & R_TRIG) != 0))
	{
		force = TRUE;
	}
	if (syNetplayGuardGrabDiagShouldLog(force) == FALSE)
	{
		return;
	}

	event = (success != FALSE) ? "catch_ok" : "catch_miss";
	syNetplayGuardGrabDiagLogCore(fighter_gobj, event, (reason != NULL) ? reason : "?");
}

void syNetplayGuardGrabDiagLogGuardOn(GObj *fighter_gobj, const char *site)
{
	FTStruct *fp;
	sb32 force;

	if (syNetplayGuardGrabDiagEnabled() == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (fp->pkind != nFTPlayerKindMan))
	{
		return;
	}

	force = TRUE;
	if ((fp->input.pl.button_tap & fp->input.button_mask_a) != 0)
	{
		force = TRUE;
	}
	if ((fp->status_id == nFTCommonStatusWait) && ((fp->input.pl.button_tap & R_TRIG) != 0))
	{
		force = TRUE;
	}
	if (syNetplayGuardGrabDiagShouldLog(force) == FALSE)
	{
		return;
	}

	syNetplayGuardGrabDiagLogCore(fighter_gobj, "guard_on", (site != NULL) ? site : "?");
}

void syNetplayGuardGrabDiagLogGuardDropCatch(GObj *fighter_gobj, sb32 success, s32 status_id)
{
	FTStruct *fp;
	char detail[48];

	if (syNetplayGuardGrabDiagEnabled() == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (fp->pkind != nFTPlayerKindMan))
	{
		return;
	}
	if ((success == FALSE) && ((fp->input.pl.button_tap & R_TRIG) == 0) &&
	    ((fp->input.pl.button_tap & fp->input.button_mask_a) == 0))
	{
		return;
	}
	if (syNetplayGuardGrabDiagShouldLog(TRUE) == FALSE)
	{
		return;
	}
	(void)snprintf(detail, sizeof(detail), "status=%d success=%d", (int)status_id, (int)(success != FALSE));
	syNetplayGuardGrabDiagLogCore(fighter_gobj, (success != FALSE) ? "guarddrop_catch_ok" : "guarddrop_catch_miss",
	                               detail);
}

#else /* !(PORT && SSB64_NETMENU) */

sb32 syNetplayGuardGrabDiagEnabled(void)
{
	return FALSE;
}

void syNetplayGuardGrabDiagLogRInputEdge(GObj *fighter_gobj)
{
	(void)fighter_gobj;
}

void syNetplayGuardGrabDiagLogCatchAttempt(GObj *fighter_gobj, sb32 success, const char *reason)
{
	(void)fighter_gobj;
	(void)success;
	(void)reason;
}

void syNetplayGuardGrabDiagLogGuardOn(GObj *fighter_gobj, const char *site)
{
	(void)fighter_gobj;
	(void)site;
}

void syNetplayGuardGrabDiagLogGuardDropCatch(GObj *fighter_gobj, sb32 success, s32 status_id)
{
	(void)fighter_gobj;
	(void)success;
	(void)status_id;
}

#endif /* PORT && SSB64_NETMENU */
