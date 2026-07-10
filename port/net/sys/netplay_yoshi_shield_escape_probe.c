#include <sys/netplay_yoshi_shield_escape_probe.h>

#if defined(PORT) && defined(SSB64_NETMENU)

#include <ef/effect.h>
#include <ef/eftypes.h>
#include <ft/fighter.h>
#include <ft/ftcommon.h>
#include <ft/ftdef.h>
#include <ft/ftstatusvars.h>
#include <sys/netinput.h>
#include <sys/netpeer.h>
#include <sys/netplay_sim_quantize.h>
#include <sys/netrollback.h>
#include <sys/objman.h>
#include <sys/taskman.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void port_log(const char *fmt, ...);
extern char *getenv(const char *name);

static sb32 s_yoshi_shield_escape_probe_env = -999;
static u32 s_yoshi_shield_escape_probe_log_count;
static sb32 s_yoshi_shield_escape_probe_armed_logged = FALSE;

static void syNetplayYoshiShieldEscapeProbeLogArmedOnce(void)
{
	if (s_yoshi_shield_escape_probe_armed_logged != FALSE)
	{
		return;
	}
	s_yoshi_shield_escape_probe_armed_logged = TRUE;
	port_log("SSB64 YoshiShieldEscapeProbe: armed (SSB64_NETPLAY_YOSHI_SHIELD_ESCAPE_PROBE=1)\n");
}

sb32 syNetplayYoshiShieldEscapeProbeEnabled(void)
{
	const char *env;

	if (s_yoshi_shield_escape_probe_env != -999)
	{
		return (s_yoshi_shield_escape_probe_env != 0) ? TRUE : FALSE;
	}
	env = getenv("SSB64_NETPLAY_YOSHI_SHIELD_ESCAPE_PROBE");
	s_yoshi_shield_escape_probe_env =
	    ((env != NULL) && (env[0] != '\0') && (strcmp(env, "0") != 0)) ? 1 : 0;
	return (s_yoshi_shield_escape_probe_env != 0) ? TRUE : FALSE;
}

static sb32 syNetplayYoshiShieldEscapeProbeVerbose(void)
{
	const char *env = getenv("SSB64_NETPLAY_YOSHI_SHIELD_ESCAPE_PROBE_VERBOSE");

	return ((env != NULL) && (env[0] != '\0') && (strcmp(env, "0") != 0)) ? TRUE : FALSE;
}

static u32 syNetplayYoshiShieldEscapeProbeFrameBound(const char *name, u32 default_val)
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
	if (v > 600000L)
	{
		return 600000U;
	}
	return (u32)v;
}

static sb32 syNetplayYoshiShieldEscapeProbeFrameInWindow(u32 frame)
{
	u32 frame_min =
	    syNetplayYoshiShieldEscapeProbeFrameBound("SSB64_NETPLAY_YOSHI_SHIELD_ESCAPE_PROBE_FRAME_MIN", 0U);
	u32 frame_max = syNetplayYoshiShieldEscapeProbeFrameBound(
	    "SSB64_NETPLAY_YOSHI_SHIELD_ESCAPE_PROBE_FRAME_MAX", 600000U);

	if (frame < frame_min)
	{
		return FALSE;
	}
	if (frame > frame_max)
	{
		return FALSE;
	}
	return TRUE;
}

static sb32 syNetplayYoshiShieldEscapeProbeShouldLog(void)
{
	if (syNetplayYoshiShieldEscapeProbeEnabled() == FALSE)
	{
		return FALSE;
	}
	if (syNetplayYoshiShieldEscapeProbeVerbose() != FALSE)
	{
		return TRUE;
	}
	if (s_yoshi_shield_escape_probe_log_count >= 2048U)
	{
		return FALSE;
	}
	s_yoshi_shield_escape_probe_log_count++;
	return TRUE;
}

static sb32 syNetplayYoshiShieldEscapeProbeInScope(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	return ((fp->fkind == nFTKindYoshi) || (fp->fkind == nFTKindNYoshi)) ? TRUE : FALSE;
}

static sb32 syNetplayYoshiShieldEscapeProbeGobjLive(const GObj *gobj)
{
	if (gobj == NULL)
	{
		return FALSE;
	}
	/* GOBJ_PORT_EJECTED_SENTINEL — recycled slot; pointer may linger in union pool. */
	return (gobj->obj_kind != 0xFEU) ? TRUE : FALSE;
}

static void syNetplayYoshiShieldEscapeProbeCollectAttachedEffects(GObj *fighter_gobj, u32 *out_count,
                                                                  GObj **out_first_gobj, u32 *out_first_id,
                                                                  GObj **out_second_gobj, u32 *out_second_id)
{
	GObj *effect_gobj;
	u32 count = 0;

	*out_count = 0U;
	*out_first_gobj = NULL;
	*out_first_id = 0xFFFFFFFFU;
	*out_second_gobj = NULL;
	*out_second_id = 0xFFFFFFFFU;

	if (fighter_gobj == NULL)
	{
		return;
	}
	if (ftGetStruct(fighter_gobj)->is_effect_attach == FALSE)
	{
		return;
	}

	for (effect_gobj = gGCCommonLinks[nGCCommonLinkIDEffect]; effect_gobj != NULL;
	     effect_gobj = effect_gobj->link_next)
	{
		EFStruct *ep = efGetStruct(effect_gobj);

		if ((ep != NULL) && (ep->fighter_gobj == fighter_gobj))
		{
			if (count == 0U)
			{
				*out_first_gobj = effect_gobj;
				*out_first_id = effect_gobj->id;
			}
			else if (count == 1U)
			{
				*out_second_gobj = effect_gobj;
				*out_second_id = effect_gobj->id;
			}
			count++;
		}
	}
	*out_count = count;
}

static void syNetplayYoshiShieldEscapeProbeLogCore(GObj *fighter_gobj, const char *site, s32 escape_status_id,
                                                   s32 itemthrow_buffer_tics, sb32 adopt_attempted,
                                                   sb32 adopt_ok)
{
	FTStruct *fp;
	const u32 *pool_u32;
	GObj *guard_effect_gobj;
	GObj *live_first_gobj;
	GObj *live_second_gobj;
	u32 live_count;
	u32 live_first_id;
	u32 live_second_id;
	u32 frame;
	u32 sim_tick;
	u32 guard_effect_kind;
	sb32 guard_ptr_live;
	sb32 guard_ptr_matches_live;
	sb32 union_release_eq_itemthrow;
	sb32 union_stale_effect_ptr;
	char detail[96];

	if (syNetplayYoshiShieldEscapeProbeShouldLog() == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (syNetplayYoshiShieldEscapeProbeInScope(fp) == FALSE)
	{
		return;
	}

	syNetplayYoshiShieldEscapeProbeLogArmedOnce();

	frame = dSYTaskmanFrameCount;
	if (syNetplayYoshiShieldEscapeProbeFrameInWindow(frame) == FALSE)
	{
		return;
	}

	sim_tick = syNetInputGetTick();
	pool_u32 = (const u32 *)&fp->status_vars.common.guard;
	guard_effect_gobj = ftStatusVarsGuard(fp)->effect_gobj;
	guard_effect_kind = (guard_effect_gobj != NULL) ? (u32)guard_effect_gobj->obj_kind : 0xFFFFFFFFU;

	syNetplayYoshiShieldEscapeProbeCollectAttachedEffects(fighter_gobj, &live_count, &live_first_gobj,
	                                                      &live_first_id, &live_second_gobj, &live_second_id);

	guard_ptr_live = syNetplayYoshiShieldEscapeProbeGobjLive(guard_effect_gobj);
	guard_ptr_matches_live =
	    ((guard_effect_gobj != NULL) && (live_first_gobj != NULL) && (guard_effect_gobj == live_first_gobj)) ?
	        TRUE :
	        FALSE;
	union_release_eq_itemthrow =
	    (ftStatusVarsGuard(fp)->release_lag == ftStatusVarsEscape(fp)->itemthrow_buffer_tics) ? TRUE : FALSE;
	union_stale_effect_ptr =
	    ((guard_effect_gobj != NULL) && ((guard_ptr_live == FALSE) || (guard_ptr_matches_live == FALSE))) ? TRUE :
	                                                                                                        FALSE;

	detail[0] = '\0';
	if ((adopt_attempted != FALSE) || (adopt_ok != FALSE))
	{
		(void)snprintf(detail, sizeof(detail), " adopt_attempted=%d adopt_ok=%d", (int)(adopt_attempted != FALSE),
		               (int)(adopt_ok != FALSE));
	}
	if (escape_status_id >= 0)
	{
		char status_buf[32];
		size_t detail_len = strlen(detail);

		(void)snprintf(status_buf, sizeof(status_buf), " escape_status=%d itemthrow=%d", (int)escape_status_id,
		               (int)itemthrow_buffer_tics);
		if (detail_len < (sizeof(detail) - 1U))
		{
			(void)snprintf(detail + detail_len, sizeof(detail) - detail_len, "%s", status_buf);
		}
	}

	port_log(
	    "SSB64 YoshiShieldEscapeProbe: site=%s frame=%u sim_tick=%u player=%d fkind=%d status=%d motion=%d "
	    "is_shield=%d is_effect_attach=%d rb=%d vs=%d resim=%d "
	    "pool_u32=(0x%08x,0x%08x,0x%08x,0x%08x,0x%08x) "
	    "guard=(release_lag=%d decay=%d effect_gobj=%p is_release=%d) "
	    "escape=(itemthrow_buffer=%d) union_release_eq_itemthrow=%d "
	    "live_fx=(count=%u id0=%u gobj0=%p id1=%u gobj1=%p) "
	    "guard_effect_kind=0x%x guard_ptr_live=%d guard_ptr_matches_live=%d union_stale_effect_ptr=%d%s\n",
	    site, (unsigned int)frame, (unsigned int)sim_tick, (int)fp->player, (int)fp->fkind, (int)fp->status_id,
	    (int)fp->motion_id, (int)(fp->is_shield != FALSE), (int)(fp->is_effect_attach != FALSE),
	    (int)(syNetplayRollbackSemanticsActive() != FALSE), (int)(syNetPeerIsVSSessionActive() != FALSE),
	    (int)(syNetRollbackIsResimulating() != FALSE), (unsigned int)pool_u32[0], (unsigned int)pool_u32[1],
	    (unsigned int)pool_u32[2], (unsigned int)pool_u32[3], (unsigned int)pool_u32[4],
	    (int)ftStatusVarsGuard(fp)->release_lag, (int)ftStatusVarsGuard(fp)->shield_decay_wait,
	    (void *)guard_effect_gobj, (int)(ftStatusVarsGuard(fp)->is_release != FALSE),
	    (int)ftStatusVarsEscape(fp)->itemthrow_buffer_tics, (int)(union_release_eq_itemthrow != FALSE),
	    (unsigned int)live_count, (unsigned int)live_first_id, (void *)live_first_gobj, (unsigned int)live_second_id,
	    (void *)live_second_gobj, (unsigned int)guard_effect_kind, (int)(guard_ptr_live != FALSE), (int)(guard_ptr_matches_live != FALSE),
	    (int)(union_stale_effect_ptr != FALSE), detail);
}

void syNetplayYoshiShieldEscapeProbeLogEscapeSetStatus(GObj *fighter_gobj, s32 status_id, s32 itemthrow_buffer_tics,
                                                       const char *phase)
{
	if (syNetplayYoshiShieldEscapeProbeEnabled() == FALSE)
	{
		return;
	}
	syNetplayYoshiShieldEscapeProbeLogCore(fighter_gobj, phase, status_id, itemthrow_buffer_tics, FALSE, FALSE);
}

void syNetplayYoshiShieldEscapeProbeLogGuardFromEscape(GObj *fighter_gobj, const char *phase, sb32 adopt_attempted,
                                                       sb32 adopt_ok)
{
	if (syNetplayYoshiShieldEscapeProbeEnabled() == FALSE)
	{
		return;
	}
	syNetplayYoshiShieldEscapeProbeLogCore(fighter_gobj, phase, -1, -1, adopt_attempted, adopt_ok);
}

#else /* !(PORT && SSB64_NETMENU) */

sb32 syNetplayYoshiShieldEscapeProbeEnabled(void)
{
	return FALSE;
}

void syNetplayYoshiShieldEscapeProbeLogEscapeSetStatus(GObj *fighter_gobj, s32 status_id, s32 itemthrow_buffer_tics,
                                                       const char *phase)
{
	(void)fighter_gobj;
	(void)status_id;
	(void)itemthrow_buffer_tics;
	(void)phase;
}

void syNetplayYoshiShieldEscapeProbeLogGuardFromEscape(GObj *fighter_gobj, const char *phase, sb32 adopt_attempted,
                                                       sb32 adopt_ok)
{
	(void)fighter_gobj;
	(void)phase;
	(void)adopt_attempted;
	(void)adopt_ok;
}

#endif /* PORT && SSB64_NETMENU */
