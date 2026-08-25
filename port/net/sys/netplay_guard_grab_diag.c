/* dladdr / Dl_info are behind __USE_GNU; the feature macro must precede the first
 * libc header. Used only to name the caller of a grab-range SetStatus. */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

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
	int guard_release_lag;
	int guard_is_release;

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

	/*
	 * guard.release_lag / .is_release are only meaningful while the guard overlay is live.
	 * Reading them unconditionally made this diagnostic the dominant witness "stomp" source
	 * (soak 2026-08-22: 368 of 408 stomps came from here, drowning any real writer) and
	 * printed aliased bytes from whatever overlay was actually live — the same tick showed
	 * release_lag=178 on one peer and -2 on the other for an idle Wait fighter. Reads cannot
	 * corrupt state, but they can mislead a reader and they bury the signal. Report -1 when
	 * the overlay is not live instead.
	 */
	if ((fp->is_shield != FALSE) ||
	    ((fp->status_id >= nFTCommonStatusGuardStart) && (fp->status_id <= nFTCommonStatusGuardEnd)))
	{
		guard_release_lag = (int)ftStatusVarsGuard(fp)->release_lag;
		guard_is_release = (int)(ftStatusVarsGuard(fp)->is_release != FALSE);
	}
	else
	{
		guard_release_lag = -1;
		guard_is_release = -1;
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
	    (int)(fp->is_shield != FALSE), guard_release_lag, guard_is_release, (int)fp->shield_health,
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

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

/*
 * Grab-range SetStatus trace.
 *
 * Soak 2026-08-25 narrowed the surviving grab failure to a status transition, not the
 * anim-end edge: both peers enter 167 CatchPull at the same tick with matching figh, then
 * the predicting peer is back at 166 Catch on the very next tick, so
 * ftCommonCatchPullProcUpdate never runs. Something calls SetStatus on the grabber during
 * replay that does not happen on the input owner; nothing logged says who.
 *
 * Caller resolved in-process with dladdr (normal sim path, not a signal handler) because
 * the binary is PIE and a raw return address cannot be rebased from the log alone.
 */
static const char *syNetplayGuardGrabDiagCallerName(const void *caller, unsigned long *out_off)
{
	*out_off = 0UL;
	if (caller == NULL)
	{
		return "?";
	}
#if !defined(_WIN32)
	{
		Dl_info info;

		memset(&info, 0, sizeof(info));
		if ((dladdr((void *)(uintptr_t)caller, &info) != 0) && (info.dli_sname != NULL) &&
		    (info.dli_saddr != NULL))
		{
			*out_off = (unsigned long)((const char *)caller - (const char *)info.dli_saddr);
			return info.dli_sname;
		}
	}
#endif
	return "?";
}

void syNetplayGuardGrabDiagLogSetStatus(GObj *fighter_gobj, s32 from_status, s32 to_status,
                                        const void *caller)
{
	FTStruct *fp;
	unsigned long off = 0UL;

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
	/* Only transitions touching the grab/capture band, either side. */
	if (((from_status < 166) || (from_status > 172)) && ((to_status < 166) || (to_status > 172)))
	{
		return;
	}
	/* Not through ShouldLog(): its dedup would hide the repeated replay passes. */
	port_log(
	    "SSB64 GuardGrabDiag: event=grab_setstatus tick=%u player=%d from=%d to=%d caller=%s+0x%lx resim=%d\n",
	    (unsigned int)syNetInputGetTick(), (int)fp->player, (int)from_status, (int)to_status,
	    syNetplayGuardGrabDiagCallerName(caller, &off), off,
	    (int)(syNetRollbackIsResimulating() != FALSE));
}

void syNetplayGuardGrabDiagLogCatchPullAnimEnd(GObj *fighter_gobj, sb32 anim_end, f32 anim_frame,
                                               GObj *catch_gobj)
{
	FTStruct *fp;
	FTStruct *victim_fp;
	int victim_flag;

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
	victim_fp = (catch_gobj != NULL) ? ftGetStruct(catch_gobj) : NULL;
	victim_flag = (victim_fp != NULL) ? (int)(ftStatusVarsCapture(victim_fp)->is_goto_pulled_wait != FALSE) : -1;

	/*
	 * Deliberately NOT routed through syNetplayGuardGrabDiagShouldLog(): that dedups repeats,
	 * and the point here is to see this edge on every pass including resim replay.
	 */
	port_log(
	    "SSB64 GuardGrabDiag: event=catchpull_animend tick=%u player=%d status=%d anim_frame=%.4f "
	    "anim_end=%d catch_gobj=%d victim_player=%d victim_status=%d goto_pulled_wait=%d resim=%d\n",
	    (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->status_id, (double)anim_frame,
	    (int)(anim_end != FALSE), (catch_gobj != NULL) ? 1 : 0,
	    (victim_fp != NULL) ? (int)victim_fp->player : -1,
	    (victim_fp != NULL) ? (int)victim_fp->status_id : -1, victim_flag,
	    (int)(syNetRollbackIsResimulating() != FALSE));
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

void syNetplayGuardGrabDiagLogSetStatus(GObj *fighter_gobj, s32 from_status, s32 to_status,
                                        const void *caller)
{
	(void)fighter_gobj;
	(void)from_status;
	(void)to_status;
	(void)caller;
}

void syNetplayGuardGrabDiagLogCatchPullAnimEnd(GObj *fighter_gobj, sb32 anim_end, f32 anim_frame,
                                               GObj *catch_gobj)
{
	(void)fighter_gobj;
	(void)anim_end;
	(void)anim_frame;
	(void)catch_gobj;
}


void syNetplayGuardGrabDiagLogGuardDropCatch(GObj *fighter_gobj, sb32 success, s32 status_id)
{
	(void)fighter_gobj;
	(void)success;
	(void)status_id;
}

#endif /* PORT && SSB64_NETMENU */
