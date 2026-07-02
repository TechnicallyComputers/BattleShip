#include <sys/netplay_fox_firefox_gate.h>

#if defined(PORT) && defined(SSB64_NETMENU)

#include <ft/fighter.h>
#include <ft/ftchar/ftfox/ftfox.h>
#include <ft/ftchar/ftfox/ftfoxfunctions.h>
#include <ft/ftdef.h>
#include <mp/mpdef.h>
#include <sys/netinput.h>
#include <sys/netplay_sim_quantize.h>
#include <sys/netrollback.h>
#include <sys/netrollbacksnapshot.h>
#include <sys/objman.h>
#include <stdlib.h>

extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);

static sb32 sSYNetplayFoxFirefoxGateDiagCache = -999;

/*
 * Per-player last-seen state for the AfterLoadVerify catch-up checks. Diagnostic only: lets a
 * soak trace show, for a given player, how many sim ticks elapsed and how much the gate field
 * moved between two consecutive catch-up evaluations. If the same sim tick shows up more than
 * once with the countdown field already lower than last time, the countdown is being advanced
 * by something other than one forward-sim ProcUpdate call per elapsed tick.
 */
static s32 sSYNetplayFoxFirefoxGateLastCheckTick[2] = {-1, -1};
static s32 sSYNetplayFoxFirefoxGateLastCheckValue[2] = {0, 0};

/*
 * Firefox travel-span tracer state (diag-only). One record per human slot; measures the reported
 * "Firefox cuts off early" by counting real forward-sim anim_frames decrements and the wire-tick
 * span between travel entry and the travel->End transition, plus which path fired the end.
 */
typedef struct SYNetplayFoxTravelSpan
{
	sb32 active;
	s32 entry_tick;
	s32 entry_frames;
	s32 sim_decrements;
	s32 last_decrement_tick;
} SYNetplayFoxTravelSpan;

static SYNetplayFoxTravelSpan sSYNetplayFoxTravelSpan[2];
/* Latched by whoever triggers the End transition just before ftFox*EndSetStatus: 0 unknown / 1 sim / 2 gate. */
static s32 sSYNetplayFoxTravelEndPathPending[2] = {0, 0};

static s32 syNetplayFoxTravelSpanPlayer(const FTStruct *fp)
{
	s32 player;

	if (fp == NULL)
	{
		return -1;
	}
	player = (s32)fp->player;
	if ((player < 0) || (player >= 2))
	{
		return -1;
	}
	return player;
}

sb32 syNetplayFoxFirefoxGateDiagEnabled(void)
{
	const char *env;

	if (sSYNetplayFoxFirefoxGateDiagCache != -999)
	{
		return (sSYNetplayFoxFirefoxGateDiagCache != 0) ? TRUE : FALSE;
	}
	env = getenv("SSB64_NETPLAY_FOX_FIREFOX_GATE_DIAG");
	sSYNetplayFoxFirefoxGateDiagCache = ((env != NULL) && (env[0] != '\0') && (atoi(env) != 0)) ? 1 : 0;
	return (sSYNetplayFoxFirefoxGateDiagCache != 0) ? TRUE : FALSE;
}

sb32 syNetplayFoxFighterInFirefoxStartScope(s32 status_id)
{
	return ((status_id == nFTFoxStatusSpecialHiStart) || (status_id == nFTFoxStatusSpecialAirHiStart)) ? TRUE : FALSE;
}

sb32 syNetplayFoxFighterInFirefoxHoldScope(s32 status_id)
{
	return ((status_id == nFTFoxStatusSpecialHiHold) || (status_id == nFTFoxStatusSpecialAirHiHold)) ? TRUE : FALSE;
}

sb32 syNetplayFoxFighterInFirefoxTravelScope(s32 status_id)
{
	return ((status_id == nFTFoxStatusSpecialHi) || (status_id == nFTFoxStatusSpecialAirHi)) ? TRUE : FALSE;
}

sb32 syNetplayFoxFighterInFirefoxSynctestDeferScope(s32 status_id)
{
	if ((status_id >= nFTFoxStatusSpecialHiStart) && (status_id <= nFTFoxStatusSpecialAirHiBound))
	{
		return TRUE;
	}
	return FALSE;
}

sb32 syNetplayFoxFighterInSpecialNScope(s32 status_id)
{
	return ((status_id == nFTFoxStatusSpecialN) || (status_id == nFTFoxStatusSpecialAirN)) ? TRUE : FALSE;
}

sb32 syNetplayFoxFighterInAppearScope(s32 fkind, s32 status_id)
{
	if (fkind != nFTKindFox)
	{
		return FALSE;
	}
	return syNetRbSnapshotStatusInAppearPresentationScope(fkind, status_id);
}

sb32 syNetplayFoxFighterInResimPresentationScope(const FTStruct *fp)
{
	if ((fp == NULL) || (fp->fkind != nFTKindFox))
	{
		return FALSE;
	}
	if (syNetplayFoxFighterInAppearScope(fp->fkind, fp->status_id) != FALSE)
	{
		return TRUE;
	}
	if (syNetplayFoxFighterInSpecialNScope(fp->status_id) != FALSE)
	{
		return TRUE;
	}
	return syNetplayFoxFighterInFirefoxSynctestDeferScope(fp->status_id);
}

sb32 syNetplayFoxLiveHasResimPresentationScope(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp != NULL) && (syNetplayFoxFighterInResimPresentationScope(fp) != FALSE))
		{
			return TRUE;
		}
	}
	return FALSE;
}

sb32 syNetplayFoxLiveHasFirefoxSynctestDeferScope(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp != NULL) && (fp->fkind == nFTKindFox) &&
		    (syNetplayFoxFighterInFirefoxSynctestDeferScope(fp->status_id) != FALSE))
		{
			return TRUE;
		}
	}
	return FALSE;
}

void syNetplayFoxSanitizeFirefoxStatusVars(FTStruct *fp)
{
	ftFoxSpecialHiStatusVars *specialhi;
	sb32 in_hold;
	sb32 in_travel;

	if ((fp == NULL) || (fp->fkind != nFTKindFox) ||
	    (syNetplayFoxFighterInFirefoxSynctestDeferScope(fp->status_id) == FALSE))
	{
		return;
	}
	/*
	 * Start (SpecialHiStart/AirHiStart) uses different status_vars overlay bytes than Hold/Travel.
	 * Clamping launch_delay/anim_frames while Start is live stomps the Start overlay, changes figh
	 * without a status transition, and can make load-hash verify pass then TryDeeperLoadBeforeResim
	 * see slot/live drift (soak2 @518 resim during Firefox startup). Only sanitize fields for the
	 * overlay that is actually live. See docs/bugs/netplay_fox_firefox_start_resim_drift_2026-07-01.md.
	 */
	if (syNetplayFoxFighterInFirefoxStartScope(fp->status_id) != FALSE)
	{
		return;
	}
	in_hold = syNetplayFoxFighterInFirefoxHoldScope(fp->status_id);
	in_travel = syNetplayFoxFighterInFirefoxTravelScope(fp->status_id);
	if ((in_hold == FALSE) && (in_travel == FALSE))
	{
		return;
	}
	specialhi = &fp->status_vars.fox.specialhi;
	if (in_hold != FALSE)
	{
		if (specialhi->launch_delay < 0)
		{
			specialhi->launch_delay = 0;
		}
	}
	if (in_travel != FALSE)
	{
		if (specialhi->anim_frames < 0)
		{
			if (syNetplayFoxFirefoxGateDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 Netplay: FOX_FIREFOX_GATE tick=%u event=sanitize_anim_frames player=%d status=%d before=%d after=0 resim=%d\n",
				    syNetInputGetTick(), (int)fp->player, (int)fp->status_id,
				    (int)specialhi->anim_frames, (int)(syNetRollbackIsResimulating() != FALSE));
			}
			specialhi->anim_frames = 0;
		}
		if (specialhi->decelerate_wait < 0)
		{
			specialhi->decelerate_wait = 0;
		}
		if (syNetplaySimQuantizeActive() != FALSE)
		{
			specialhi->angle = syNetplayQuantizeF32(specialhi->angle);
		}
	}
}

/*
 * Diagnostic only: logs every AfterLoadVerify evaluation of a gate countdown field for this
 * player (not just the ones that trigger a forced transition), tagged with the delta from the
 * previous evaluation for the same player. `dtick=0 dvalue<0` means this exact sim tick was
 * evaluated more than once and the field dropped between those evaluations without the sim tick
 * advancing — the signature of the field being decremented by something other than one
 * forward-sim ProcUpdate call per elapsed tick.
 */
static void syNetplayFoxFirefoxGateLogCheck(const char *event, const FTStruct *fp, s32 value)
{
	u32 tick;
	s32 player;
	s32 dtick;
	s32 dvalue;

	if (syNetplayFoxFirefoxGateDiagEnabled() == FALSE)
	{
		return;
	}
	tick = syNetInputGetTick();
	player = (s32)fp->player;
	if ((player < 0) || (player >= 2))
	{
		return;
	}
	dtick = (sSYNetplayFoxFirefoxGateLastCheckTick[player] < 0) ? 0
	        : ((s32)tick - sSYNetplayFoxFirefoxGateLastCheckTick[player]);
	dvalue = (sSYNetplayFoxFirefoxGateLastCheckTick[player] < 0) ? 0
	         : (value - sSYNetplayFoxFirefoxGateLastCheckValue[player]);
	port_log("SSB64 Netplay: FOX_FIREFOX_GATE tick=%u event=%s player=%d status=%d value=%d dtick=%d dvalue=%d\n",
	         tick, event, (int)player, (int)fp->status_id, (int)value, (int)dtick, (int)dvalue);
	sSYNetplayFoxFirefoxGateLastCheckTick[player] = (s32)tick;
	sSYNetplayFoxFirefoxGateLastCheckValue[player] = value;
}

void syNetplayFoxFirefoxGateLogFieldDecrement(GObj *fighter_gobj, FTStruct *fp, const char *field, s32 before, s32 after)
{
	(void)fighter_gobj;
	if ((fp == NULL) || (field == NULL) || (syNetplayFoxFirefoxGateDiagEnabled() == FALSE))
	{
		return;
	}
	port_log("SSB64 Netplay: FOX_FIREFOX_GATE tick=%u event=proc_decrement player=%d status=%d field=%s before=%d after=%d\n",
	         syNetInputGetTick(), (int)fp->player, (int)fp->status_id, field, (int)before, (int)after);
}

void syNetplayFoxFirefoxTravelSpanOnInit(FTStruct *fp)
{
	s32 player;

	player = syNetplayFoxTravelSpanPlayer(fp);
	if (player < 0)
	{
		return;
	}
	sSYNetplayFoxTravelSpan[player].active = TRUE;
	sSYNetplayFoxTravelSpan[player].entry_tick = (s32)syNetInputGetTick();
	sSYNetplayFoxTravelSpan[player].entry_frames = fp->status_vars.fox.specialhi.anim_frames;
	sSYNetplayFoxTravelSpan[player].sim_decrements = 0;
	sSYNetplayFoxTravelSpan[player].last_decrement_tick = -1;
	sSYNetplayFoxTravelEndPathPending[player] = 0;
	if (syNetplayFoxFirefoxGateDiagEnabled() != FALSE)
	{
		port_log(
		    "SSB64 Netplay: FOX_FIREFOX_GATE tick=%u event=travel_span_init player=%d status=%d entry_frames=%d expected=%d resim=%d\n",
		    syNetInputGetTick(), (int)player, (int)fp->status_id,
		    (int)fp->status_vars.fox.specialhi.anim_frames, (int)FTFOX_FIREFOX_TRAVEL_TIME,
		    (int)(syNetRollbackIsResimulating() != FALSE));
	}
}

void syNetplayFoxFirefoxTravelSpanOnSimDecrement(FTStruct *fp)
{
	s32 player;
	s32 tick;
	s32 dtick;

	player = syNetplayFoxTravelSpanPlayer(fp);
	if (player < 0)
	{
		return;
	}
	tick = (s32)syNetInputGetTick();
	if (sSYNetplayFoxTravelSpan[player].active == FALSE)
	{
		/* Decrement seen without a tracked entry (span began before trace armed): back-fill entry. */
		sSYNetplayFoxTravelSpan[player].active = TRUE;
		sSYNetplayFoxTravelSpan[player].entry_tick = tick;
		sSYNetplayFoxTravelSpan[player].entry_frames = fp->status_vars.fox.specialhi.anim_frames + 1;
		sSYNetplayFoxTravelSpan[player].sim_decrements = 0;
		sSYNetplayFoxTravelSpan[player].last_decrement_tick = -1;
		sSYNetplayFoxTravelEndPathPending[player] = 0;
	}
	dtick = (sSYNetplayFoxTravelSpan[player].last_decrement_tick < 0)
	            ? 0
	            : (tick - sSYNetplayFoxTravelSpan[player].last_decrement_tick);
	sSYNetplayFoxTravelSpan[player].sim_decrements++;
	sSYNetplayFoxTravelSpan[player].last_decrement_tick = tick;
	if (syNetplayFoxFirefoxGateDiagEnabled() != FALSE)
	{
		port_log(
		    "SSB64 Netplay: FOX_FIREFOX_GATE tick=%u event=proc_decrement player=%d status=%d field=anim_frames after=%d count=%d dtick=%d resim=%d\n",
		    (unsigned int)tick, (int)player, (int)fp->status_id,
		    (int)fp->status_vars.fox.specialhi.anim_frames, (int)sSYNetplayFoxTravelSpan[player].sim_decrements,
		    (int)dtick, (int)(syNetRollbackIsResimulating() != FALSE));
	}
}

void syNetplayFoxFirefoxTravelSpanNoteEndPath(FTStruct *fp, sb32 from_gate)
{
	s32 player;

	player = syNetplayFoxTravelSpanPlayer(fp);
	if (player < 0)
	{
		return;
	}
	sSYNetplayFoxTravelEndPathPending[player] = (from_gate != FALSE) ? 2 : 1;
}

void syNetplayFoxFirefoxTravelSpanOnEnd(FTStruct *fp)
{
	s32 player;
	s32 end_tick;
	s32 span_ticks;
	s32 path;
	const char *path_str;

	player = syNetplayFoxTravelSpanPlayer(fp);
	if (player < 0)
	{
		return;
	}
	if (sSYNetplayFoxTravelSpan[player].active == FALSE)
	{
		return;
	}
	end_tick = (s32)syNetInputGetTick();
	span_ticks = end_tick - sSYNetplayFoxTravelSpan[player].entry_tick;
	path = sSYNetplayFoxTravelEndPathPending[player];
	path_str = (path == 2) ? "gate" : ((path == 1) ? "sim" : "other");
	if (syNetplayFoxFirefoxGateDiagEnabled() != FALSE)
	{
		port_log(
		    "SSB64 Netplay: FOX_FIREFOX_GATE tick=%u event=travel_span_end player=%d status=%d path=%s entry_tick=%d end_tick=%d span_ticks=%d sim_decrements=%d entry_frames=%d anim_frames=%d expected=%d lost_ticks=%d lost_decrements=%d resim=%d\n",
		    (unsigned int)end_tick, (int)player, (int)fp->status_id, path_str,
		    (int)sSYNetplayFoxTravelSpan[player].entry_tick, (int)end_tick, (int)span_ticks,
		    (int)sSYNetplayFoxTravelSpan[player].sim_decrements, (int)sSYNetplayFoxTravelSpan[player].entry_frames,
		    (int)fp->status_vars.fox.specialhi.anim_frames, (int)FTFOX_FIREFOX_TRAVEL_TIME,
		    (int)(FTFOX_FIREFOX_TRAVEL_TIME - span_ticks),
		    (int)(FTFOX_FIREFOX_TRAVEL_TIME - sSYNetplayFoxTravelSpan[player].sim_decrements),
		    (int)(syNetRollbackIsResimulating() != FALSE));
	}
	sSYNetplayFoxTravelSpan[player].active = FALSE;
	sSYNetplayFoxTravelEndPathPending[player] = 0;
}

void syNetplayFoxCatchUpFirefoxLaunchIfDue(GObj *fighter_gobj, FTStruct *fp)
{
	if ((fighter_gobj == NULL) || (fp == NULL) || (fp->fkind != nFTKindFox))
	{
		return;
	}
	syNetplayFoxSanitizeFirefoxStatusVars(fp);
	if (syNetplayFoxFighterInFirefoxHoldScope(fp->status_id) == FALSE)
	{
		return;
	}
	syNetplayFoxFirefoxGateLogCheck("launch_check", fp, (s32)fp->status_vars.fox.specialhi.launch_delay);
	if (fp->status_vars.fox.specialhi.launch_delay > 0)
	{
		return;
	}
	if (syNetplayFoxFirefoxGateDiagEnabled() != FALSE)
	{
		port_log("SSB64 Netplay: FOX_FIREFOX_GATE tick=%u event=launch_delay_zero player=%d status=%d launch_delay=%d\n",
		         syNetInputGetTick(), (int)fp->player, (int)fp->status_id, (int)fp->status_vars.fox.specialhi.launch_delay);
	}
	if (fp->ga == nMPKineticsAir)
	{
		ftFoxSpecialAirHiSetStatusFromGround(fighter_gobj);
	}
	else
	{
		ftFoxSpecialHiDecideSetStatus(fighter_gobj);
	}
}

void syNetplayFoxCatchUpFirefoxEndIfDue(GObj *fighter_gobj, FTStruct *fp)
{
	if ((fighter_gobj == NULL) || (fp == NULL) || (fp->fkind != nFTKindFox))
	{
		return;
	}
	syNetplayFoxSanitizeFirefoxStatusVars(fp);
	if (syNetplayFoxFighterInFirefoxTravelScope(fp->status_id) == FALSE)
	{
		return;
	}
	syNetplayFoxFirefoxGateLogCheck("end_check", fp, (s32)fp->status_vars.fox.specialhi.anim_frames);
	if (fp->status_vars.fox.specialhi.anim_frames > 0)
	{
		return;
	}
	if (syNetplayFoxFirefoxGateDiagEnabled() != FALSE)
	{
		port_log("SSB64 Netplay: FOX_FIREFOX_GATE tick=%u event=anim_frames_zero player=%d status=%d anim_frames=%d\n",
		         syNetInputGetTick(), (int)fp->player, (int)fp->status_id, (int)fp->status_vars.fox.specialhi.anim_frames);
	}
	syNetplayFoxFirefoxTravelSpanNoteEndPath(fp, TRUE);
	if (fp->ga == nMPKineticsAir)
	{
		ftFoxSpecialAirHiEndSetStatus(fighter_gobj);
	}
	else
	{
		ftFoxSpecialHiEndSetStatus(fighter_gobj);
	}
}

void syNetplayFoxCatchUpAllAfterLoadVerify(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp == NULL) || (fp->fkind != nFTKindFox))
		{
			continue;
		}
		if (syNetplayFoxFighterInFirefoxSynctestDeferScope(fp->status_id) == FALSE)
		{
			continue;
		}
		/* Hold/Travel gate catch-up only — Start must advance via forward-sim anim-end. */
		if (syNetplayFoxFighterInFirefoxStartScope(fp->status_id) != FALSE)
		{
			continue;
		}
		syNetplayFoxCatchUpFirefoxLaunchIfDue(fighter_gobj, fp);
		syNetplayFoxCatchUpFirefoxEndIfDue(fighter_gobj, fp);
	}
	syNetRbSnapshotRebindAllFighters();
}

#endif /* PORT && SSB64_NETMENU */
