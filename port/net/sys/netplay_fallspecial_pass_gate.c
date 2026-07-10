#include <sys/netplay_fallspecial_pass_gate.h>

#if defined(PORT) && defined(SSB64_NETMENU)

#include <ft/fighter.h>
#include <ft/ftdef.h>
#include <ft/ftstatusvars.h>
#include <sys/netplay_sim_quantize.h>

void syNetplayFallSpecialPassGateHardenAllowPass(GObj *fighter_gobj)
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
	if ((fp == NULL) || (fp->status_id != nFTCommonStatusFallSpecial))
	{
		return;
	}
	/* SSB64_NETMENU: stripped from offline builds. Runtime: active VS/resim only.
	 * Netplay rollback only: union stomp via squat.pass_wait / jumpaerial.vel_x at +4.
	 * See docs/bugs/netplay_fallspecial_pass_allow_stomp_2026-06-09.md. */
	ftStatusVarsFallSpecial(fp)->is_allow_pass = TRUE;
}

#endif /* PORT && SSB64_NETMENU */
