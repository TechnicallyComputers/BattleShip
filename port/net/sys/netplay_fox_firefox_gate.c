#include <sys/netplay_fox_firefox_gate.h>

#if defined(PORT) && defined(SSB64_NETMENU)

#include <ft/fighter.h>
#include <ft/ftchar/ftfox/ftfox.h>
#include <ft/ftchar/ftfox/ftfoxfunctions.h>
#include <ft/ftdef.h>
#include <mp/mpdef.h>
#include <sys/objman.h>
#include <stdlib.h>

extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);

static sb32 sSYNetplayFoxFirefoxGateDiagCache = -999;

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

void syNetplayFoxCatchUpFirefoxLaunchIfDue(GObj *fighter_gobj, FTStruct *fp)
{
	if ((fighter_gobj == NULL) || (fp == NULL) || (fp->fkind != nFTKindFox))
	{
		return;
	}
	if (syNetplayFoxFighterInFirefoxHoldScope(fp->status_id) == FALSE)
	{
		return;
	}
	if (fp->status_vars.fox.specialhi.launch_delay > 0)
	{
		return;
	}
	if (syNetplayFoxFirefoxGateDiagEnabled() != FALSE)
	{
		port_log("SSB64 Netplay: FOX_FIREFOX_GATE tick=? event=launch_delay_zero player=%d status=%d launch_delay=%d\n",
		         (int)fp->player, (int)fp->status_id, (int)fp->status_vars.fox.specialhi.launch_delay);
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
	if (syNetplayFoxFighterInFirefoxTravelScope(fp->status_id) == FALSE)
	{
		return;
	}
	if (fp->status_vars.fox.specialhi.anim_frames > 0)
	{
		return;
	}
	if (syNetplayFoxFirefoxGateDiagEnabled() != FALSE)
	{
		port_log("SSB64 Netplay: FOX_FIREFOX_GATE tick=? event=anim_frames_zero player=%d status=%d anim_frames=%d\n",
		         (int)fp->player, (int)fp->status_id, (int)fp->status_vars.fox.specialhi.anim_frames);
	}
	if (fp->ga == nMPKineticsAir)
	{
		ftFoxSpecialAirHiEndSetStatus(fighter_gobj);
	}
	else
	{
		ftFoxSpecialHiEndSetStatus(fighter_gobj);
	}
}

#endif /* PORT && SSB64_NETMENU */
