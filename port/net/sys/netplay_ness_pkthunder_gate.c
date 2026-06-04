#include <sys/netplay_ness_pkthunder_gate.h>

#if defined(PORT) && defined(SSB64_NETMENU)

#include <ft/fighter.h>
#include <ft/ftchar/ftness/ftness.h>
#include <ft/ftchar/ftness/ftnessfunctions.h>
#include <ft/ftcommon/ftcommonfunctions.h>
#include <ft/ftdef.h>
#include <ft/ftstatusvars.h>
#include <ft/ftmain.h>
#include <gm/gmdef.h>
#include <mp/mpcommon.h>
#include <mp/mpdef.h>
#include <sys/netinput.h>
#include <sys/netplay_sim_quantize.h>
#include <sys/netrollback.h>
#include <sys/netrollbacksnapshot.h>
#include <sys/objman.h>
#include <stdlib.h>
#include <sys/vector.h>
#include <wp/weapon.h>
#include <wp/wpdef.h>
#include <wp/wpvars.h>
#include <wp/wpness/wpnesspkthunder.h>

extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);

static sb32 sSYNetplayNessPKThunderGateDiagCache = -999;

#define SY_NETPLAY_NESS_JIBAKU_STALL_TICKS 1

static s32 sSYNetplayNessJibakuLastAnimLength[GMCOMMON_PLAYERS_MAX];
static s32 sSYNetplayNessJibakuStallTicks[GMCOMMON_PLAYERS_MAX];
static u32 sSYNetplayNessThrowEntryTick[GMCOMMON_PLAYERS_MAX];
static u32 sSYNetplayNessHoldEntryTick[GMCOMMON_PLAYERS_MAX];
static s32 sSYNetplayNessHoldEntryDelay[GMCOMMON_PLAYERS_MAX];
static s32 sSYNetplayNessHoldEntryGravityDelay[GMCOMMON_PLAYERS_MAX];
static u32 sSYNetplayNessPkScopeEarliestLoadTick[GMCOMMON_PLAYERS_MAX];
static u32 sSYNetplayNessPkSessionEarliestLoadTick[GMCOMMON_PLAYERS_MAX];
static u32 sSYNetplayNessDeferPKCullUntilTick;
static s32 sSYNetplayNessDeferPKCullPlayer;
static u32 sSYNetplayNessPostCullFloorGraceUntilTick[GMCOMMON_PLAYERS_MAX];
static u32 sSYNetplayNessAirJibakuStartTick[GMCOMMON_PLAYERS_MAX];
static sb32 sSYNetplayNessGroundSnapBlockLogged[GMCOMMON_PLAYERS_MAX];
static sb32 sSYNetplayNessPosStaleLogged[GMCOMMON_PLAYERS_MAX];
static u32 sSYNetplayNessJibakuNotifyTick[GMCOMMON_PLAYERS_MAX];
/* NaN probe latch: 1 once a non-finite fighter field is seen, re-armed when finite again.
 * Keeps the finite->NaN transition to a single log line per occurrence. */
static u8 sSYNetplayNessFighterNaNState[GMCOMMON_PLAYERS_MAX];

#define SY_NETPLAY_NESS_PK_DEFER_CULL_TICKS 2U
#define SY_NETPLAY_NESS_PKTHUNDER_POS_STALE_DIST_SQ (128.0F * 128.0F)
#define SY_NETPLAY_NESS_PK_POST_CULL_FLOOR_GRACE_TICKS 2U
#define SY_NETPLAY_NESS_AIR_JIBAKU_LAUNCH_GUARD_TICKS 4U

static void syNetplayNessScheduleDeferPKTeardown(FTStruct *fp);
static s32 syNetplayNessCountLivePKThunderWeapons(void);
static void syNetplayNessLogJibakuWeaponState(FTStruct *fp);
static sb32 syNetplayNessFighterInPKThunderHoldScope(const FTStruct *fp);
static void syNetplayNessClearPKThunderPos(FTStruct *fp);
static void syNetplayNessGetPKThunderCouplingPos(GObj *fighter_gobj, FTStruct *fp, f32 *out_fx, f32 *out_fy,
                                                 f32 *out_ax, f32 *out_ay, f32 *out_hx, f32 *out_hy);
static void syNetplayNessWitnessPKThunderPosStale(GObj *fighter_gobj, FTStruct *fp, const char *site);
void syNetplayNessRefreshPKThunderPosFromHead(GObj *fighter_gobj, FTStruct *fp);
static void syNetplayNessClearPkScopeEarliestLoadTick(s32 player);
static void syNetplayNessNotePkScopeEarliestLoadTick(FTStruct *fp);
static u32 syNetplayNessEffectivePkScopeLoadTick(s32 player);
static void syNetplayNessArmAirJibakuFloorGrace(FTStruct *fp);
static void syNetplayNessMaintainPkScopeEarliestLoadTick(GObj *fighter_gobj, FTStruct *fp);
static void syNetplayNessResetJibakuStallState(s32 player);
static void syNetplayNessCullOrphanPKThunderKeepHead(GObj *fighter_gobj, FTStruct *fp);
static void syNetplayNessCullOrphanPKThunderForPlayer(s32 player);
static void syNetplayNessCanonicalizeCouplingFightersIfJibakuBurst(void);
static sb32 syNetplayNessFighterInPkThunderScopeStatus(s32 status_id);
sb32 syNetplayNessShouldDeferPKThunderTeardownForPlayer(s32 player);
sb32 syNetplayNessIsPKThunderGlobalDeferActive(void);
sb32 syNetplayNessShouldBlockAirJibakuGroundSnap(const FTStruct *fp);
static const char *syNetplayNessAirJibakuGroundSnapBlockReason(const FTStruct *fp);

static sb32 syNetplayNessPKThunderGateDiagEnabled(void)
{
	const char *env;

	if (sSYNetplayNessPKThunderGateDiagCache != -999)
	{
		return (sSYNetplayNessPKThunderGateDiagCache != 0) ? TRUE : FALSE;
	}
	env = getenv("SSB64_NETPLAY_NESS_PKTHUNDER_GATE_DIAG");
	sSYNetplayNessPKThunderGateDiagCache = ((env != NULL) && (env[0] != '\0') && (atoi(env) != 0)) ? 1 : 0;
	return (sSYNetplayNessPKThunderGateDiagCache != 0) ? TRUE : FALSE;
}

/* TRUE for Inf or NaN. Bit-pattern test avoids pulling <math.h>/isnan into this TU and
 * works regardless of the build's fast-math state (x != x can be optimized away). */
static sb32 syNetplayNessF32IsNonFinite(f32 v)
{
	union
	{
		f32 f;
		u32 u;
	} bits;

	bits.f = v;
	return (((bits.u >> 23) & 0xFFU) == 0xFFU) ? TRUE : FALSE;
}

/* Catch the first tick Ness's sim-driving floats go non-finite. The PK Thunder desync starts
 * with fighter translate / vel_air becoming NaN (different sign bit per ISA -> hash divergence);
 * this pins which field and which tick/site so we can guard the producing math. Diag-only. */
void syNetplayNessProbeFighterNaN(GObj *fighter_gobj, FTStruct *fp, const char *site)
{
	DObj *fighter_dobj;
	s32 pi;
	u32 bad;
	f32 tx;
	f32 ty;
	f32 tz;
	f32 vx;
	f32 vy;
	f32 vz;
	f32 jibaku_angle;
	f32 anchor_x;
	f32 anchor_y;

	if (syNetplayNessPKThunderGateDiagEnabled() == FALSE)
	{
		return;
	}
	if ((fighter_gobj == NULL) || (fp == NULL))
	{
		return;
	}
	if ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess))
	{
		return;
	}
	pi = fp->player;
	if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX))
	{
		return;
	}

	fighter_dobj = DObjGetStruct(fighter_gobj);
	tx = (fighter_dobj != NULL) ? fighter_dobj->translate.vec.f.x : 0.0F;
	ty = (fighter_dobj != NULL) ? fighter_dobj->translate.vec.f.y : 0.0F;
	tz = (fighter_dobj != NULL) ? fighter_dobj->translate.vec.f.z : 0.0F;
	vx = fp->physics.vel_air.x;
	vy = fp->physics.vel_air.y;
	vz = fp->physics.vel_air.z;
	jibaku_angle = fp->status_vars.ness.specialhi.pkjibaku_angle;
	anchor_x = fp->status_vars.ness.specialhi.pkthunder_pos.x;
	anchor_y = fp->status_vars.ness.specialhi.pkthunder_pos.y;

	bad = 0U;
	if (syNetplayNessF32IsNonFinite(tx) != FALSE)
	{
		bad |= (1U << 0);
	}
	if (syNetplayNessF32IsNonFinite(ty) != FALSE)
	{
		bad |= (1U << 1);
	}
	if (syNetplayNessF32IsNonFinite(tz) != FALSE)
	{
		bad |= (1U << 2);
	}
	if (syNetplayNessF32IsNonFinite(vx) != FALSE)
	{
		bad |= (1U << 3);
	}
	if (syNetplayNessF32IsNonFinite(vy) != FALSE)
	{
		bad |= (1U << 4);
	}
	if (syNetplayNessF32IsNonFinite(vz) != FALSE)
	{
		bad |= (1U << 5);
	}
	if (syNetplayNessF32IsNonFinite(jibaku_angle) != FALSE)
	{
		bad |= (1U << 6);
	}
	if (syNetplayNessF32IsNonFinite(anchor_x) != FALSE)
	{
		bad |= (1U << 7);
	}
	if (syNetplayNessF32IsNonFinite(anchor_y) != FALSE)
	{
		bad |= (1U << 8);
	}

	if (bad == 0U)
	{
		sSYNetplayNessFighterNaNState[pi] = 0U;
		return;
	}
	if (sSYNetplayNessFighterNaNState[pi] != 0U)
	{
		return;
	}
	sSYNetplayNessFighterNaNState[pi] = 1U;
	port_log(
	    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=fighter_nan player=%d status=%d site=%s "
	    "bad=0x%03x translate=(%f,%f,%f) vel_air=(%f,%f,%f) pkjibaku_angle=%f anchor=(%f,%f) resim=%d\n",
	    (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->status_id, site, (unsigned int)bad, tx, ty,
	    tz, vx, vy, vz, jibaku_angle, anchor_x, anchor_y, (int)(syNetRollbackIsResimulating() != FALSE));
}

static sb32 syNetplayNessF32Matches(f32 live, f32 expected)
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

sb32 syNetplayNessFighterInPKThunderLandingFallScope(const FTStruct *fp)
{
	f32 expected_drift;

	if ((fp == NULL) || ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess)))
	{
		return FALSE;
	}
	if ((fp->status_id != nFTCommonStatusFallSpecial) && (fp->status_id != nFTCommonStatusLandingFallSpecial))
	{
		return FALSE;
	}
	if (fp->attr == NULL)
	{
		return FALSE;
	}
	expected_drift = fp->attr->air_speed_max_x * FTNESS_PKTHUNDER_FALLSPECIAL_DRIFT;
	if ((syNetplayNessF32Matches(ftStatusVarsFallSpecial(fp)->drift, expected_drift) == FALSE) ||
	    (syNetplayNessF32Matches(ftStatusVarsFallSpecial(fp)->landing_lag, FTNESS_PKTHUNDER_LANDING_LAG) == FALSE))
	{
		return FALSE;
	}
	return TRUE;
}

sb32 syNetplayNessFighterInPKJibakuCatchUpScope(const FTStruct *fp)
{
	if ((fp == NULL) || ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess)))
	{
		return FALSE;
	}
	switch (fp->status_id)
	{
	case nFTNessStatusSpecialHiJibaku:
	case nFTNessStatusSpecialAirHiJibaku:
	case nFTNessStatusSpecialAirHiBound:
		return TRUE;

	default:
		break;
	}
	return FALSE;
}

static sb32 syNetplayNessFighterInPkThunderScopeStatus(s32 status_id)
{
	switch (status_id)
	{
	case nFTNessStatusSpecialHiStart:
	case nFTNessStatusSpecialHiHold:
	case nFTNessStatusSpecialAirHiStart:
	case nFTNessStatusSpecialAirHiHold:
	case nFTNessStatusSpecialHiJibaku:
	case nFTNessStatusSpecialAirHiJibaku:
	case nFTNessStatusSpecialAirHiBound:
		return TRUE;

	default:
		break;
	}
	return FALSE;
}

sb32 syNetplayNessFighterInFcResimDeferScope(s32 status_id)
{
	switch (status_id)
	{
	case nFTNessStatusSpecialHiStart:
	case nFTNessStatusSpecialHiHold:
	case nFTNessStatusSpecialHiEnd:
	case nFTNessStatusSpecialHiJibaku:
	case nFTNessStatusSpecialAirHiStart:
	case nFTNessStatusSpecialAirHiHold:
	case nFTNessStatusSpecialAirHiEnd:
	case nFTNessStatusSpecialAirHiJibaku:
	case nFTNessStatusSpecialAirHiBound:
		return TRUE;

	default:
		break;
	}
	return FALSE;
}

static void syNetplayNessClearPkScopeEarliestLoadTick(s32 player)
{
	if ((player >= 0) && (player < GMCOMMON_PLAYERS_MAX))
	{
		sSYNetplayNessPkScopeEarliestLoadTick[player] = 0U;
		sSYNetplayNessPkSessionEarliestLoadTick[player] = 0U;
		sSYNetplayNessAirJibakuStartTick[player] = 0U;
		sSYNetplayNessThrowEntryTick[player] = 0U;
	}
}

static void syNetplayNessNotePkSessionEarliestLoadTick(s32 player, u32 load_tick)
{
	if ((player < 0) || (player >= GMCOMMON_PLAYERS_MAX) || (load_tick == 0U))
	{
		return;
	}
	if ((sSYNetplayNessPkSessionEarliestLoadTick[player] == 0U) ||
	    (load_tick < sSYNetplayNessPkSessionEarliestLoadTick[player]))
	{
		sSYNetplayNessPkSessionEarliestLoadTick[player] = load_tick;
	}
}

static u32 syNetplayNessEffectivePkScopeLoadTick(s32 player)
{
	u32 scope_load;
	u32 session_load;

	if ((player < 0) || (player >= GMCOMMON_PLAYERS_MAX))
	{
		return 0U;
	}
	scope_load = sSYNetplayNessPkScopeEarliestLoadTick[player];
	session_load = sSYNetplayNessPkSessionEarliestLoadTick[player];
	if ((session_load != 0U) && ((scope_load == 0U) || (session_load < scope_load)))
	{
		return session_load;
	}
	return scope_load;
}

static void syNetplayNessArmAirJibakuFloorGrace(FTStruct *fp)
{
	s32 pi;
	u32 now_tick;
	u32 grace_until;

	if (fp == NULL)
	{
		return;
	}
	if (fp->status_id != nFTNessStatusSpecialAirHiJibaku)
	{
		return;
	}
	pi = fp->player;
	if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX))
	{
		return;
	}
	now_tick = syNetInputGetTick();
	sSYNetplayNessAirJibakuStartTick[pi] = now_tick;
	grace_until = now_tick + SY_NETPLAY_NESS_PK_DEFER_CULL_TICKS + SY_NETPLAY_NESS_PK_POST_CULL_FLOOR_GRACE_TICKS;
	if (grace_until > sSYNetplayNessPostCullFloorGraceUntilTick[pi])
	{
		sSYNetplayNessPostCullFloorGraceUntilTick[pi] = grace_until;
	}
}

static void syNetplayNessNotePkScopeEarliestLoadTick(FTStruct *fp)
{
	u32 tick;
	u32 scope_start;
	u32 load_tick;
	s32 pi;

	if ((fp == NULL) || (syNetplayNessFighterInPkThunderScopeStatus(fp->status_id) == FALSE))
	{
		return;
	}
	pi = fp->player;
	if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX))
	{
		return;
	}
	tick = syNetInputGetTick();
	if (fp->status_total_tics <= tick)
	{
		scope_start = tick - fp->status_total_tics;
	}
	else
	{
		scope_start = 0U;
	}
	load_tick = (scope_start > 0U) ? (scope_start - 1U) : 0U;
	if ((sSYNetplayNessPkScopeEarliestLoadTick[pi] == 0U) ||
	    (load_tick < sSYNetplayNessPkScopeEarliestLoadTick[pi]))
	{
		sSYNetplayNessPkScopeEarliestLoadTick[pi] = load_tick;
	}
	syNetplayNessNotePkSessionEarliestLoadTick(pi, load_tick);
}

static void syNetplayNessMaintainPkScopeEarliestLoadTick(GObj *fighter_gobj, FTStruct *fp)
{
	s32 pi;

	(void)fighter_gobj;
	if (fp == NULL)
	{
		return;
	}
	pi = fp->player;
	if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX))
	{
		return;
	}
	if (syNetplayNessFighterInPkThunderScopeStatus(fp->status_id) != FALSE)
	{
		syNetplayNessNotePkScopeEarliestLoadTick(fp);
		return;
	}
	if ((syNetplayNessShouldDeferPKThunderTeardownForPlayer(pi) != FALSE) ||
	    (syNetplayNessIsPKThunderGlobalDeferActive() != FALSE))
	{
		return;
	}
	switch (fp->status_id)
	{
	case nFTNessStatusSpecialHiEnd:
	case nFTNessStatusSpecialAirHiEnd:
		return;

	default:
		break;
	}
	syNetplayNessClearPkScopeEarliestLoadTick(pi);
}

sb32 syNetplayNessAnyLiveFighterInJibakuBurstScope(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp == NULL) || ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess)))
		{
			continue;
		}
		if ((fp->status_id == nFTNessStatusSpecialHiJibaku) || (fp->status_id == nFTNessStatusSpecialAirHiJibaku))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetplayNessAnyLiveFighterInPkThunderVolatileResimDeferScope(void)
{
	GObj *fighter_gobj;

	if (syNetplayNessIsPKThunderGlobalDeferActive() != FALSE)
	{
		return TRUE;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp == NULL) || ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess)))
		{
			continue;
		}
		if (syNetplayNessShouldDeferPKThunderTeardownForPlayer(fp->player) != FALSE)
		{
			return TRUE;
		}
		switch (fp->status_id)
		{
		case nFTNessStatusSpecialHiJibaku:
		case nFTNessStatusSpecialAirHiJibaku:
		case nFTNessStatusSpecialAirHiBound:
			return TRUE;

		default:
			break;
		}
	}
	return FALSE;
}

sb32 syNetplayNessAnyLiveFighterInFcStateRecoveryDeferScope(void)
{
	u32 now_tick;
	s32 pi;

	if (syNetplayNessAnyLiveFighterInPkThunderVolatileResimDeferScope() != FALSE)
	{
		return TRUE;
	}
	now_tick = syNetInputGetTick();
	for (pi = 0; pi < GMCOMMON_PLAYERS_MAX; pi++)
	{
		if ((sSYNetplayNessPostCullFloorGraceUntilTick[pi] != 0U) &&
		    (now_tick < sSYNetplayNessPostCullFloorGraceUntilTick[pi]))
		{
			return TRUE;
		}
		if ((sSYNetplayNessAirJibakuStartTick[pi] != 0U) &&
		    (now_tick < (sSYNetplayNessAirJibakuStartTick[pi] + SY_NETPLAY_NESS_AIR_JIBAKU_LAUNCH_GUARD_TICKS)))
		{
			return TRUE;
		}
	}
	return FALSE;
}

sb32 syNetplayNessAnyLiveFighterInFcResimDeferScope(void)
{
	GObj *fighter_gobj;
	u32 now_tick;
	s32 pi;

	if (syNetplayNessAnyLiveFighterInPkThunderVolatileResimDeferScope() != FALSE)
	{
		return TRUE;
	}
	now_tick = syNetInputGetTick();
	for (pi = 0; pi < GMCOMMON_PLAYERS_MAX; pi++)
	{
		if ((sSYNetplayNessPostCullFloorGraceUntilTick[pi] != 0U) &&
		    (now_tick < sSYNetplayNessPostCullFloorGraceUntilTick[pi]))
		{
			return TRUE;
		}
		if ((sSYNetplayNessAirJibakuStartTick[pi] != 0U) &&
		    (now_tick < (sSYNetplayNessAirJibakuStartTick[pi] + SY_NETPLAY_NESS_AIR_JIBAKU_LAUNCH_GUARD_TICKS)))
		{
			return TRUE;
		}
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp == NULL) || ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess)))
		{
			continue;
		}
		if (syNetplayNessFighterInFcResimDeferScope(fp->status_id) != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

sb32 syNetplayNessClampFcRecoveryLoadTick(u32 *io_load_tick, u32 *io_mismatch_tick)
{
	u32 min_load;
	s32 pi;

	if ((io_load_tick == NULL) || (io_mismatch_tick == NULL))
	{
		return FALSE;
	}
	min_load = 0U;
	for (pi = 0; pi < GMCOMMON_PLAYERS_MAX; pi++)
	{
		u32 scope_load = syNetplayNessEffectivePkScopeLoadTick(pi);

		if (scope_load == 0U)
		{
			continue;
		}
		if ((min_load == 0U) || (scope_load < min_load))
		{
			min_load = scope_load;
		}
	}
	if ((min_load == 0U) || (*io_load_tick >= min_load))
	{
		return FALSE;
	}
	if (syNetplayNessPKThunderGateDiagEnabled() != FALSE)
	{
		port_log(
		    "SSB64 Netplay: NESS_PKTHUNDER_GATE event=fc_recovery_load_clamp load=%u->%u mismatch=%u->%u\n",
		    *io_load_tick,
		    min_load,
		    *io_mismatch_tick,
		    (min_load + 1U));
	}
	*io_load_tick = min_load;
	if (*io_mismatch_tick <= min_load)
	{
		*io_mismatch_tick = min_load + 1U;
	}
	return TRUE;
}

void syNetplayNessResimReplayHardeningAfterLoadStep(void)
{
	GObj *fighter_gobj;

	syNetRbSnapshotRebindAllFighters();
	syNetplayNessSanitizeAllFightersAfterSlotApply();
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp == NULL) || ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess)))
		{
			continue;
		}
		if (syNetplayNessFighterInPKJibakuCatchUpScope(fp) != FALSE)
		{
			syNetRbSnapRebindNessPKJibakuProcs(fighter_gobj, fp);
			syNetplayNessCatchUpPKJibakuIfDue(fighter_gobj, fp);
		}
		else if (syNetplayFighterInNessPKThunderHoldSimScope(fp) != FALSE)
		{
			syNetplayCanonicalizeNessPKThunderHoldSimState(fighter_gobj);
			syNetplayNessReconcilePKThunderWeaponsAfterApply(fighter_gobj);
		}
		else if (syNetplayFighterInNessSpecialLwSimScope(fp) != FALSE)
		{
			syNetplayCanonicalizeNessSpecialLwSimState(fighter_gobj);
		}
	}
}

void syNetRbSnapRebindNessPKJibakuProcs(GObj *fighter_gobj, FTStruct *fp)
{
	if ((fighter_gobj == NULL) || (fp == NULL))
	{
		return;
	}
	if ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess))
	{
		return;
	}
	switch (fp->status_id)
	{
	case nFTNessStatusSpecialHiJibaku:
		fp->proc_update = ftNessSpecialHiJibakuProcUpdate;
		fp->proc_physics = ftNessSpecialHiJibakuProcPhysics;
		fp->proc_map = ftNessSpecialHiJibakuProcMap;
		fp->proc_damage = NULL;
		break;

	case nFTNessStatusSpecialAirHiJibaku:
		fp->proc_update = ftNessSpecialAirHiJibakuProcUpdate;
		fp->proc_physics = ftNessSpecialAirHiJibakuProcPhysics;
		fp->proc_map = ftNessSpecialAirHiJibakuProcMap;
		fp->proc_damage = NULL;
		break;

	case nFTNessStatusSpecialAirHiBound:
		fp->proc_update = ftNessSpecialAirHiJibakuBoundProcUpdate;
		fp->proc_map = ftNessSpecialAirHiJibakuBoundProcMap;
		break;

	default:
		break;
	}
}

/*
 * Ground/air jibaku ProcUpdate exits when anim_length hits 0 after decrement. Early jibaku with
 * anim_length=0 is a snapshot-scrub artifact (synctest restore), not a legitimate end frame.
 */
static sb32 syNetplayNessJibakuAnimLengthZeroIsLegitimate(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	if ((fp->status_id != nFTNessStatusSpecialHiJibaku) && (fp->status_id != nFTNessStatusSpecialAirHiJibaku))
	{
		return FALSE;
	}
	return (fp->status_total_tics >= (u32)FTNESS_PKJIBAKU_ANIM_LENGTH) ? TRUE : FALSE;
}

static void syNetplayNessClampResidualJibakuLaunchVelocity(FTStruct *fp)
{
	f32 vel_cap;

	if (fp == NULL)
	{
		return;
	}
	vel_cap = FTNESS_PKJIBAKU_VEL;
	if (fp->status_id == nFTNessStatusSpecialAirHiJibaku)
	{
		if (fp->physics.vel_air.x > vel_cap)
		{
			fp->physics.vel_air.x = vel_cap;
		}
		else if (fp->physics.vel_air.x < -vel_cap)
		{
			fp->physics.vel_air.x = -vel_cap;
		}
		if (fp->physics.vel_air.y > vel_cap)
		{
			fp->physics.vel_air.y = vel_cap;
		}
		else if (fp->physics.vel_air.y < -vel_cap)
		{
			fp->physics.vel_air.y = -vel_cap;
		}
	}
	else if (fp->status_id == nFTNessStatusSpecialHiJibaku)
	{
		if (fp->physics.vel_ground.x > vel_cap)
		{
			fp->physics.vel_ground.x = vel_cap;
		}
		else if (fp->physics.vel_ground.x < -vel_cap)
		{
			fp->physics.vel_ground.x = -vel_cap;
		}
		if (fp->physics.vel_air.x > vel_cap)
		{
			fp->physics.vel_air.x = vel_cap;
		}
		else if (fp->physics.vel_air.x < -vel_cap)
		{
			fp->physics.vel_air.x = -vel_cap;
		}
		if (fp->physics.vel_air.y > vel_cap)
		{
			fp->physics.vel_air.y = vel_cap;
		}
		else if (fp->physics.vel_air.y < -vel_cap)
		{
			fp->physics.vel_air.y = -vel_cap;
		}
	}
}

void syNetplayNessSanitizePKJibakuStatusVars(FTStruct *fp)
{
	s32 *anim_length;
	s32 was;

	if ((fp == NULL) || (syNetplayNessFighterInPKJibakuCatchUpScope(fp) == FALSE))
	{
		return;
	}
	anim_length = &fp->status_vars.ness.specialhi.pkjibaku_anim_length;
	if ((fp->status_id == nFTNessStatusSpecialHiJibaku) || (fp->status_id == nFTNessStatusSpecialAirHiJibaku))
	{
		if ((*anim_length <= 0) && (syNetplayNessJibakuAnimLengthZeroIsLegitimate(fp) == FALSE))
		{
			was = *anim_length;
			*anim_length = (s32)FTNESS_PKJIBAKU_ANIM_LENGTH;
			if (syNetplayNessPKThunderGateDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=anim_length_restore player=%d status=%d was=%d status_tics=%u\n",
				    (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->status_id, was,
				    (unsigned int)fp->status_total_tics);
			}
		}
	}
	if (*anim_length < 0)
	{
		*anim_length = 0;
	}
	else if (*anim_length > (s32)FTNESS_PKJIBAKU_ANIM_LENGTH)
	{
		*anim_length = (s32)FTNESS_PKJIBAKU_ANIM_LENGTH;
	}
}

static sb32 syNetplayNessFighterInPKThunderHoldStatus(s32 status_id)
{
	switch (status_id)
	{
	case nFTNessStatusSpecialHiHold:
	case nFTNessStatusSpecialAirHiHold:
		return TRUE;

	default:
		break;
	}
	return FALSE;
}

static sb32 syNetplayNessFighterInPKThunderThrowGravityScope(s32 status_id)
{
	switch (status_id)
	{
	case nFTNessStatusSpecialHiStart:
	case nFTNessStatusSpecialAirHiStart:
	case nFTNessStatusSpecialHiHold:
	case nFTNessStatusSpecialAirHiHold:
		return TRUE;

	default:
		break;
	}
	return FALSE;
}

static u32 syNetplayNessThrowFramesSinceEntry(const FTStruct *fp)
{
	s32 pi;
	u32 now_tick;

	if (fp == NULL)
	{
		return 0U;
	}
	pi = fp->player;
	if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX) || (sSYNetplayNessThrowEntryTick[pi] == 0U))
	{
		return 0U;
	}
	now_tick = syNetInputGetTick();
	if (now_tick >= sSYNetplayNessThrowEntryTick[pi])
	{
		return now_tick - sSYNetplayNessThrowEntryTick[pi];
	}
	return 0U;
}

static u32 syNetplayNessHoldFramesSinceEntry(const FTStruct *fp)
{
	s32 pi;
	u32 now_tick;

	if (fp == NULL)
	{
		return 0U;
	}
	pi = fp->player;
	if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX) || (sSYNetplayNessHoldEntryTick[pi] == 0U))
	{
		return 0U;
	}
	now_tick = syNetInputGetTick();
	if (now_tick >= sSYNetplayNessHoldEntryTick[pi])
	{
		return now_tick - sSYNetplayNessHoldEntryTick[pi];
	}
	return 0U;
}

static s32 syNetplayNessExpectedPkjibakuDelayFromTracking(const FTStruct *fp)
{
	s32 entry_delay;
	s32 expected;
	s32 pi;
	u32 hold_frames;

	if (fp == NULL)
	{
		return -1;
	}
	if (syNetplayNessFighterInPKThunderHoldStatus(fp->status_id) != FALSE)
	{
		pi = fp->player;
		if ((pi >= 0) && (pi < GMCOMMON_PLAYERS_MAX) && (sSYNetplayNessHoldEntryTick[pi] != 0U))
		{
			entry_delay = sSYNetplayNessHoldEntryDelay[pi];
			if (entry_delay >= 0)
			{
				hold_frames = syNetplayNessHoldFramesSinceEntry(fp);
				expected = entry_delay - (s32)hold_frames;
				return (expected > 0) ? expected : 0;
			}
		}
	}
	if (fp->status_total_tics >= (u32)FTNESS_PKJIBAKU_DELAY)
	{
		return 0;
	}
	return (s32)FTNESS_PKJIBAKU_DELAY - (s32)fp->status_total_tics;
}

static s32 syNetplayNessExpectedGravityDelayFromTracking(const FTStruct *fp)
{
	s32 entry_gravity;
	s32 expected;
	s32 hold_expected;
	s32 pi;
	s32 throw_expected;
	u32 hold_frames;
	u32 throw_frames;

	if (fp == NULL)
	{
		return -1;
	}
	pi = fp->player;
	if ((pi >= 0) && (pi < GMCOMMON_PLAYERS_MAX) && (sSYNetplayNessThrowEntryTick[pi] != 0U))
	{
		throw_frames = syNetplayNessThrowFramesSinceEntry(fp);
		throw_expected = (s32)FTNESS_PKTHUNDER_GRAVITY_DELAY - (s32)throw_frames;
		if (throw_expected < 0)
		{
			throw_expected = 0;
		}
		if (syNetplayNessFighterInPKThunderHoldStatus(fp->status_id) != FALSE)
		{
			if ((sSYNetplayNessHoldEntryTick[pi] != 0U) && (sSYNetplayNessHoldEntryGravityDelay[pi] >= 0))
			{
				entry_gravity = sSYNetplayNessHoldEntryGravityDelay[pi];
				hold_frames = syNetplayNessHoldFramesSinceEntry(fp);
				hold_expected = entry_gravity - (s32)hold_frames;
				if (hold_expected < 0)
				{
					hold_expected = 0;
				}
				return (throw_expected > hold_expected) ? throw_expected : hold_expected;
			}
		}
		return throw_expected;
	}
	if (syNetplayNessFighterInPKThunderHoldStatus(fp->status_id) != FALSE)
	{
		if ((pi >= 0) && (pi < GMCOMMON_PLAYERS_MAX) && (sSYNetplayNessHoldEntryTick[pi] != 0U))
		{
			entry_gravity = sSYNetplayNessHoldEntryGravityDelay[pi];
			if (entry_gravity >= 0)
			{
				hold_frames = syNetplayNessHoldFramesSinceEntry(fp);
				expected = entry_gravity - (s32)hold_frames;
				return (expected > 0) ? expected : 0;
			}
		}
	}
	if (fp->status_total_tics >= (u32)FTNESS_PKTHUNDER_GRAVITY_DELAY)
	{
		return 0;
	}
	return (s32)FTNESS_PKTHUNDER_GRAVITY_DELAY - (s32)fp->status_total_tics;
}

/*
 * Hold with delay=0 after grace expired is normal (jibaku imminent). Only early Hold with
 * delay=0 is a rollback-scrub artifact. Uses hold-entry tracking when available so Start
 * countdown preserved into Hold is not overwritten by hold-local status_total_tics.
 */
static sb32 syNetplayNessHoldDelayZeroIsLegitimate(const FTStruct *fp)
{
	s32 expected;

	if ((fp == NULL) || (syNetplayNessFighterInPKThunderHoldStatus(fp->status_id) == FALSE))
	{
		return FALSE;
	}
	expected = syNetplayNessExpectedPkjibakuDelayFromTracking(fp);
	if (expected >= 0)
	{
		return (expected == 0) ? TRUE : FALSE;
	}
	return (fp->status_total_tics >= (u32)FTNESS_PKJIBAKU_DELAY) ? TRUE : FALSE;
}

static void syNetplayNessSyncHoldEntryTracking(FTStruct *fp)
{
	s32 live_delay;
	s32 live_gravity;
	s32 pi;
	u32 hold_frames;
	u32 tick;

	if ((fp == NULL) || (syNetplayNessFighterInPKThunderHoldStatus(fp->status_id) == FALSE))
	{
		return;
	}
	pi = fp->player;
	if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX))
	{
		return;
	}
	tick = syNetInputGetTick();
	live_delay = fp->status_vars.ness.specialhi.pkjibaku_delay;
	live_gravity = fp->status_vars.ness.specialhi.pkthunder_gravity_delay;
	if (sSYNetplayNessHoldEntryTick[pi] == 0U)
	{
		if (fp->status_total_tics <= tick)
		{
			sSYNetplayNessHoldEntryTick[pi] = tick - fp->status_total_tics;
		}
		else
		{
			sSYNetplayNessHoldEntryTick[pi] = tick;
		}
	}
	hold_frames = syNetplayNessHoldFramesSinceEntry(fp);
	/* Reconstruct entry counters from live values so rollback blob-zero cannot poison tracking. */
	sSYNetplayNessHoldEntryDelay[pi] = live_delay + (s32)hold_frames;
	{
		s32 reconstructed_gravity;

		reconstructed_gravity = live_gravity + (s32)hold_frames;
		if ((sSYNetplayNessHoldEntryGravityDelay[pi] < 0) ||
		    (reconstructed_gravity > sSYNetplayNessHoldEntryGravityDelay[pi]))
		{
			sSYNetplayNessHoldEntryGravityDelay[pi] = reconstructed_gravity;
		}
	}
	syNetplayNessNotePkScopeEarliestLoadTick(fp);
}

static void syNetplayNessSanitizePKThunderGravityDelay(FTStruct *fp)
{
	s32 expected;
	s32 was;

	if ((fp == NULL) || (syNetplayNessFighterInPKThunderThrowGravityScope(fp->status_id) == FALSE))
	{
		return;
	}
	if (fp->status_vars.ness.specialhi.pkthunder_gravity_delay > FTNESS_PKTHUNDER_GRAVITY_DELAY)
	{
		fp->status_vars.ness.specialhi.pkthunder_gravity_delay = FTNESS_PKTHUNDER_GRAVITY_DELAY;
	}
	if (fp->status_vars.ness.specialhi.pkthunder_gravity_delay < 0)
	{
		fp->status_vars.ness.specialhi.pkthunder_gravity_delay = 0;
	}
	expected = syNetplayNessExpectedGravityDelayFromTracking(fp);
	if (expected < 0)
	{
		return;
	}
	if (fp->status_vars.ness.specialhi.pkthunder_gravity_delay >= expected)
	{
		return;
	}
	was = fp->status_vars.ness.specialhi.pkthunder_gravity_delay;
	fp->status_vars.ness.specialhi.pkthunder_gravity_delay = expected;
	if (syNetplayNessPKThunderGateDiagEnabled() != FALSE)
	{
		port_log(
		    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=sanitize_gravity player=%d status=%d was=%d now=%d expected=%d status_tics=%u resim=%d\n",
		    (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->status_id, was,
		    fp->status_vars.ness.specialhi.pkthunder_gravity_delay, expected,
		    (unsigned int)fp->status_total_tics, (int)(syNetRollbackIsResimulating() != FALSE));
	}
}

static void syNetplayNessSanitizePKThunderDelayIfZero(FTStruct *fp)
{
	s32 expected;
	s32 was;
	sb32 resim;

	if (fp->status_vars.ness.specialhi.pkjibaku_delay > 0)
	{
		return;
	}
	if (syNetplayNessHoldDelayZeroIsLegitimate(fp) != FALSE)
	{
		if (syNetplayNessPKThunderGateDiagEnabled() != FALSE)
		{
			resim = syNetRollbackIsResimulating();
			port_log(
			    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=sanitize_delay_skip player=%d status=%d reason=hold_grace_expired status_tics=%u resim=%d\n",
			    (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->status_id,
			    (unsigned int)fp->status_total_tics, (int)(resim != FALSE));
		}
		return;
	}
	expected = syNetplayNessExpectedPkjibakuDelayFromTracking(fp);
	if (expected < 1)
	{
		expected = 1;
	}
	was = fp->status_vars.ness.specialhi.pkjibaku_delay;
	fp->status_vars.ness.specialhi.pkjibaku_delay = expected;
	if (syNetplayNessPKThunderGateDiagEnabled() != FALSE)
	{
		resim = syNetRollbackIsResimulating();
		port_log(
		    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=sanitize_delay player=%d status=%d was=%d now=%d expected=%d status_tics=%u resim=%d\n",
		    (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->status_id, was,
		    (int)fp->status_vars.ness.specialhi.pkjibaku_delay, expected,
		    (unsigned int)fp->status_total_tics, (int)(resim != FALSE));
	}
}

sb32 syNetplayNessHoldJibakuCollideBlocked(const FTStruct *fp)
{
	(void)fp;
	return FALSE;
}

void syNetplayNessSanitizeAllFightersAfterSlotApply(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp == NULL) || ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess)))
		{
			continue;
		}
		if (syNetplayNessFighterInPKThunderHoldScope(fp) == FALSE)
		{
			continue;
		}
		syNetplayNessSanitizePKThunderThrowStatusVars(fp);
	}
}

void syNetplayNessSanitizePKThunderThrowStatusVars(FTStruct *fp)
{
	if ((fp == NULL) || ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess)))
	{
		return;
	}
	switch (fp->status_id)
	{
	case nFTNessStatusSpecialHiStart:
	case nFTNessStatusSpecialAirHiStart:
		syNetplayNessSanitizePKThunderDelayIfZero(fp);
		syNetplayNessSanitizePKThunderGravityDelay(fp);
		syNetplayNessNotePkScopeEarliestLoadTick(fp);
		break;

	case nFTNessStatusSpecialHiHold:
	case nFTNessStatusSpecialAirHiHold:
		syNetplayNessSanitizePKThunderDelayIfZero(fp);
		syNetplayNessSanitizePKThunderGravityDelay(fp);
		syNetplayNessSyncHoldEntryTracking(fp);
		break;

	default:
		break;
	}
}

void syNetplayNessSyncHoldEntryTrackingFromApply(FTStruct *fp)
{
	syNetplayNessSyncHoldEntryTracking(fp);
}

static void syNetplayNessClearPKThunderPos(FTStruct *fp)
{
	if (fp == NULL)
	{
		return;
	}
	fp->status_vars.ness.specialhi.pkthunder_pos.x = 0.0F;
	fp->status_vars.ness.specialhi.pkthunder_pos.y = 0.0F;
	fp->status_vars.ness.specialhi.pkthunder_pos.z = 0.0F;
}

static void syNetplayNessGetPKThunderCouplingPos(GObj *fighter_gobj, FTStruct *fp, f32 *out_fx, f32 *out_fy,
                                                 f32 *out_ax, f32 *out_ay, f32 *out_hx, f32 *out_hy)
{
	DObj *fighter_dobj;
	GObj *head_gobj;
	DObj *head_dobj;

	fighter_dobj = (fighter_gobj != NULL) ? DObjGetStruct(fighter_gobj) : NULL;
	head_gobj = (fp != NULL) ? fp->status_vars.ness.specialhi.pkthunder_gobj : NULL;
	head_dobj = (head_gobj != NULL) ? DObjGetStruct(head_gobj) : NULL;

	if (out_fx != NULL)
	{
		*out_fx = (fighter_dobj != NULL) ? fighter_dobj->translate.vec.f.x : 0.0F;
	}
	if (out_fy != NULL)
	{
		*out_fy = (fighter_dobj != NULL) ? fighter_dobj->translate.vec.f.y : 0.0F;
	}
	if (out_ax != NULL)
	{
		*out_ax = (fp != NULL) ? fp->status_vars.ness.specialhi.pkthunder_pos.x : 0.0F;
	}
	if (out_ay != NULL)
	{
		*out_ay = (fp != NULL) ? fp->status_vars.ness.specialhi.pkthunder_pos.y : 0.0F;
	}
	if (out_hx != NULL)
	{
		*out_hx = (head_dobj != NULL) ? head_dobj->translate.vec.f.x : 0.0F;
	}
	if (out_hy != NULL)
	{
		*out_hy = (head_dobj != NULL) ? head_dobj->translate.vec.f.y : 0.0F;
	}
}

static void syNetplayNessWitnessPKThunderPosStale(GObj *fighter_gobj, FTStruct *fp, const char *site)
{
	GObj *head_gobj;
	DObj *head_dobj;
	Vec3f anchor;
	f32 dx;
	f32 dy;
	f32 dist_sq;
	s32 pi;

	if ((syNetplayNessPKThunderGateDiagEnabled() == FALSE) || (fp == NULL) || (site == NULL))
	{
		return;
	}
	if (syNetplayNessFighterInPKThunderHoldScope(fp) == FALSE)
	{
		return;
	}
	pi = fp->player;
	if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX) || (sSYNetplayNessPosStaleLogged[pi] != FALSE))
	{
		return;
	}
	head_gobj = fp->status_vars.ness.specialhi.pkthunder_gobj;
	if ((head_gobj == NULL) || (wpGetStruct(head_gobj) == NULL))
	{
		head_gobj = syNetRbSnapReacquirePKThunderHeadForFighter(fighter_gobj);
	}
	if (head_gobj == NULL)
	{
		return;
	}
	head_dobj = DObjGetStruct(head_gobj);
	if (head_dobj == NULL)
	{
		return;
	}
	anchor = fp->status_vars.ness.specialhi.pkthunder_pos;
	dx = anchor.x - head_dobj->translate.vec.f.x;
	dy = anchor.y - head_dobj->translate.vec.f.y;
	dist_sq = (dx * dx) + (dy * dy);
	if (dist_sq <= SY_NETPLAY_NESS_PKTHUNDER_POS_STALE_DIST_SQ)
	{
		return;
	}
	sSYNetplayNessPosStaleLogged[pi] = TRUE;
	port_log(
	    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=pkthunder_pos_stale player=%d site=%s status=%d "
	    "anchor=(%f,%f) head=(%f,%f) delta=(%f,%f) resim=%d\n",
	    (unsigned int)syNetInputGetTick(), (int)pi, site, (int)fp->status_id, anchor.x, anchor.y,
	    head_dobj->translate.vec.f.x, head_dobj->translate.vec.f.y, dx, dy,
	    (int)(syNetRollbackIsResimulating() != FALSE));
}

void syNetplayNessSyncPKThunderPosDuringHold(GObj *fighter_gobj)
{
	FTStruct *fp;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (syNetplayNessFighterInPKThunderHoldScope(fp) == FALSE))
	{
		return;
	}
	syNetplayNessProbeFighterNaN(fighter_gobj, fp, "hold_tick");
	/* Rollback only: defer to CheckCollide for pkthunder_pos on the self-hit frame (offline never
	 * overwrites the anchor each Hold tick). */
	if ((fp->status_vars.ness.specialhi.pkjibaku_delay <= 0) &&
	    (fp->status_vars.ness.specialhi.pkthunder_end_delay <= 0) &&
	    ((fp->passive_vars.ness.is_thunder_destroy & TRUE) == FALSE))
	{
		return;
	}
	syNetplayNessWitnessPKThunderPosStale(fighter_gobj, fp, "hold_update");
	syNetplayNessRefreshPKThunderPosFromHead(fighter_gobj, fp);
}

void syNetplayNessPrepareHoldSelfHitCoupling(GObj *fighter_gobj)
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
	if ((fp == NULL) || (syNetplayNessFighterInPKThunderHoldScope(fp) == FALSE))
	{
		return;
	}
	if ((fp->status_vars.ness.specialhi.pkjibaku_delay > 0) ||
	    (fp->status_vars.ness.specialhi.pkthunder_end_delay > 0) ||
	    ((fp->passive_vars.ness.is_thunder_destroy & TRUE) != FALSE))
	{
		return;
	}
	syNetplayNessCullOrphanPKThunderKeepHead(fighter_gobj, fp);
}

void syNetplayNessNotifyThrowStarted(GObj *fighter_gobj, FTStruct *fp)
{
	s32 pi;

	if (fp == NULL)
	{
		return;
	}
	if ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess))
	{
		return;
	}
	syNetplayNessProbeFighterNaN(fighter_gobj, fp, "throw_start");
	syNetplayNessClearPKThunderPos(fp);
	pi = fp->player;
	if ((pi >= 0) && (pi < GMCOMMON_PLAYERS_MAX))
	{
		sSYNetplayNessThrowEntryTick[pi] = syNetInputGetTick();
		sSYNetplayNessHoldEntryTick[pi] = 0U;
		sSYNetplayNessHoldEntryDelay[pi] = -1;
		sSYNetplayNessHoldEntryGravityDelay[pi] = -1;
		sSYNetplayNessPosStaleLogged[pi] = FALSE;
		sSYNetplayNessJibakuNotifyTick[pi] = 0U;
		syNetplayNessNotePkScopeEarliestLoadTick(fp);
	}
}

void syNetplayNessNotifyHoldEntered(GObj *fighter_gobj, FTStruct *fp)
{
	s32 delay;
	s32 pi;

	if (fp == NULL)
	{
		return;
	}
	if ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess))
	{
		return;
	}
	syNetplayNessProbeFighterNaN(fighter_gobj, fp, "hold_enter");
	delay = fp->status_vars.ness.specialhi.pkjibaku_delay;
	pi = fp->player;
	if ((pi >= 0) && (pi < GMCOMMON_PLAYERS_MAX))
	{
		sSYNetplayNessHoldEntryTick[pi] = syNetInputGetTick();
		sSYNetplayNessHoldEntryDelay[pi] = delay;
		sSYNetplayNessHoldEntryGravityDelay[pi] = fp->status_vars.ness.specialhi.pkthunder_gravity_delay;
		sSYNetplayNessPosStaleLogged[pi] = FALSE;
		syNetplayNessNotePkScopeEarliestLoadTick(fp);
		if (sSYNetplayNessHoldEntryTick[pi] > 0U)
		{
			syNetplayNessNotePkSessionEarliestLoadTick(pi, sSYNetplayNessHoldEntryTick[pi] - 1U);
		}
	}
	syNetplayNessCullOrphanPKThunderKeepHead(fighter_gobj, fp);
	syNetplayNessRefreshPKThunderPosFromHead(fighter_gobj, fp);
	syNetplayNessWitnessPKThunderPosStale(fighter_gobj, fp, "hold_enter");
	if (syNetplayNessPKThunderGateDiagEnabled() != FALSE)
	{
		sb32 resim = syNetRollbackIsResimulating();
		f32 fighter_x;
		f32 fighter_y;
		f32 anchor_x;
		f32 anchor_y;
		f32 head_x;
		f32 head_y;

		syNetplayNessGetPKThunderCouplingPos(fighter_gobj, fp, &fighter_x, &fighter_y, &anchor_x, &anchor_y,
		                                     &head_x, &head_y);
		port_log(
		    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=hold_enter player=%d status=%d delay=%d gravity_delay=%d thund_destroy=%d wp_gobj=%p fighter=(%f,%f) anchor=(%f,%f) head=(%f,%f) resim=%d\n",
		    (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->status_id, delay,
		    fp->status_vars.ness.specialhi.pkthunder_gravity_delay,
		    (int)(fp->passive_vars.ness.is_thunder_destroy & TRUE),
		    (void *)fp->status_vars.ness.specialhi.pkthunder_gobj, fighter_x, fighter_y, anchor_x, anchor_y,
		    head_x, head_y, (int)(resim != FALSE));
	}
}

void syNetplayNessNotifyJibakuTriggered(GObj *fighter_gobj, FTStruct *fp, s32 from_status_id)
{
	u32 hold_frames;
	u32 now_tick;
	s32 delay_before;
	s32 pi;

	if (fp == NULL)
	{
		return;
	}
	if ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess))
	{
		return;
	}
	syNetplayNessProbeFighterNaN(fighter_gobj, fp, "jibaku");
	pi = fp->player;
	now_tick = syNetInputGetTick();
	if ((pi >= 0) && (pi < GMCOMMON_PLAYERS_MAX) && (sSYNetplayNessJibakuNotifyTick[pi] == now_tick))
	{
		return;
	}
	if ((pi >= 0) && (pi < GMCOMMON_PLAYERS_MAX))
	{
		sSYNetplayNessJibakuNotifyTick[pi] = now_tick;
	}
	delay_before = fp->status_vars.ness.specialhi.pkjibaku_delay;
	hold_frames = 0U;
	if ((pi >= 0) && (pi < GMCOMMON_PLAYERS_MAX) && (sSYNetplayNessHoldEntryTick[pi] != 0U))
	{
		if (now_tick >= sSYNetplayNessHoldEntryTick[pi])
		{
			hold_frames = now_tick - sSYNetplayNessHoldEntryTick[pi];
		}
	}
	if ((pi >= 0) && (pi < GMCOMMON_PLAYERS_MAX))
	{
		sSYNetplayNessGroundSnapBlockLogged[pi] = FALSE;
	}
	if (syNetplayNessPKThunderGateDiagEnabled() != FALSE)
	{
		sb32 resim = syNetRollbackIsResimulating();
		f32 fighter_x;
		f32 fighter_y;
		f32 anchor_x;
		f32 anchor_y;
		f32 head_x;
		f32 head_y;

		syNetplayNessGetPKThunderCouplingPos(fighter_gobj, fp, &fighter_x, &fighter_y, &anchor_x, &anchor_y,
		                                     &head_x, &head_y);
		port_log(
		    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=jibaku_trigger player=%d from_status=%d delay_before=%d hold_frames=%u hold_entry_delay=%d thund_destroy=%d wp_gobj=%p fighter=(%f,%f) anchor=(%f,%f) head=(%f,%f) resim=%d\n",
		    (unsigned int)syNetInputGetTick(), (int)fp->player, from_status_id, delay_before,
		    (unsigned int)hold_frames,
		    ((pi >= 0) && (pi < GMCOMMON_PLAYERS_MAX)) ? sSYNetplayNessHoldEntryDelay[pi] : -1,
		    (int)(fp->passive_vars.ness.is_thunder_destroy & TRUE),
		    (void *)fp->status_vars.ness.specialhi.pkthunder_gobj, fighter_x, fighter_y, anchor_x, anchor_y,
		    head_x, head_y, (int)(resim != FALSE));
	}
	if (fighter_gobj != NULL)
	{
		s32 weapons_before;
		s32 weapons_after;

		weapons_before = syNetplayNessCountLivePKThunderWeapons();
		syNetplayNessCullOrphanPKThunderKeepHead(fighter_gobj, fp);
		weapons_after = syNetplayNessCountLivePKThunderWeapons();
		if ((syNetplayNessPKThunderGateDiagEnabled() != FALSE) && (weapons_before != weapons_after))
		{
			port_log(
			    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=jibaku_pre_cull player=%d weapons_before=%d weapons_after=%d resim=%d\n",
			    (unsigned int)syNetInputGetTick(), (int)fp->player, weapons_before, weapons_after,
			    (int)(syNetRollbackIsResimulating() != FALSE));
		}
	}
	if ((pi >= 0) && (pi < GMCOMMON_PLAYERS_MAX))
	{
		sSYNetplayNessThrowEntryTick[pi] = 0U;
		sSYNetplayNessHoldEntryTick[pi] = 0U;
		sSYNetplayNessHoldEntryDelay[pi] = -1;
		sSYNetplayNessHoldEntryGravityDelay[pi] = -1;
	}
	syNetplayNessScheduleDeferPKTeardown(fp);
}

static sb32 syNetplayNessFighterInPKThunderHoldScope(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	switch (fp->status_id)
	{
	case nFTNessStatusSpecialHiStart:
	case nFTNessStatusSpecialHiHold:
	case nFTNessStatusSpecialAirHiStart:
	case nFTNessStatusSpecialAirHiHold:
		return TRUE;
	default:
		return FALSE;
	}
}

void syNetplayNessNotifyJibakuPhase(GObj *fighter_gobj, const char *phase)
{
	FTStruct *fp;

	if ((syNetplayNessPKThunderGateDiagEnabled() == FALSE) || (fighter_gobj == NULL) || (phase == NULL))
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp == NULL)
	{
		return;
	}
	port_log(
	    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=jibaku_phase player=%d phase=%s status=%d resim=%d\n",
	    (unsigned int)syNetInputGetTick(), (int)fp->player, phase, (int)fp->status_id,
	    (int)(syNetRollbackIsResimulating() != FALSE));
}

static void syNetplayNessCullOrphanPKThunderKeepHead(GObj *fighter_gobj, FTStruct *fp)
{
	GObj *weapon_gobj;
	GObj *keep_head;

	if ((fighter_gobj == NULL) || (fp == NULL))
	{
		return;
	}
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp = wpGetStruct(weapon_gobj);

		if ((wp == NULL) || (wp->player != fp->player))
		{
			continue;
		}
		if ((wp->kind != nWPKindPKThunderHead) && (wp->kind != nWPKindPKThunderTrail))
		{
			continue;
		}
		wp->owner_gobj = fighter_gobj;
		if (wp->kind == nWPKindPKThunderHead)
		{
			wp->weapon_vars.pkthunder.parent_gobj = fighter_gobj;
		}
	}
	keep_head = fp->status_vars.ness.specialhi.pkthunder_gobj;
	if ((keep_head == NULL) || (wpGetStruct(keep_head) == NULL) ||
	    (wpNessPKThunderGObjIsLiveWeapon(keep_head) == FALSE))
	{
		keep_head = syNetRbSnapReacquirePKThunderHeadForFighter(fighter_gobj);
		fp->status_vars.ness.specialhi.pkthunder_gobj = keep_head;
	}
	syNetRbSnapCullOwnedPKThunderForFighter(fighter_gobj, keep_head);
}

static void syNetplayNessCullOrphanPKThunderForPlayer(s32 player)
{
	GObj *fighter_gobj;

	if ((player < 0) || (player >= GMCOMMON_PLAYERS_MAX))
	{
		return;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp == NULL) || (fp->player != player) || ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess)))
		{
			continue;
		}
		syNetplayNessCullOrphanPKThunderKeepHead(fighter_gobj, fp);
		return;
	}
}

static void syNetplayNessCanonicalizeCouplingFightersIfJibakuBurst(void)
{
	GObj *fighter_gobj;

	if (syNetplayNessAnyLiveFighterInJibakuBurstScope() == FALSE)
	{
		return;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		if (ftGetStruct(fighter_gobj) != NULL)
		{
			syNetplayCanonicalizeFighterSimState(fighter_gobj);
		}
	}
}

static void syNetplayNessRefreshPKThunderPosImpl(GObj *fighter_gobj, FTStruct *fp, union FTStatusVars *status_vars,
                                                 sb32 update_live_coupling)
{
	GObj *pkthunder_gobj;
	DObj *head_dobj;
	WPStruct *wp;
	Vec3f pos_before;
	sb32 capture_only;

	if ((fighter_gobj == NULL) || (fp == NULL) || (status_vars == NULL))
	{
		return;
	}
	if ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess))
	{
		return;
	}
	if (syNetplayNessFighterInPKThunderHoldScope(fp) == FALSE)
	{
		return;
	}
	capture_only = (status_vars != &fp->status_vars) ? TRUE : FALSE;
	pkthunder_gobj = fp->status_vars.ness.specialhi.pkthunder_gobj;
	if ((pkthunder_gobj == NULL) || (wpGetStruct(pkthunder_gobj) == NULL))
	{
		pkthunder_gobj = syNetRbSnapReacquirePKThunderHeadForFighter(fighter_gobj);
		if ((update_live_coupling != FALSE) && (capture_only == FALSE))
		{
			fp->status_vars.ness.specialhi.pkthunder_gobj = pkthunder_gobj;
		}
	}
	if (pkthunder_gobj == NULL)
	{
		return;
	}
	wp = wpGetStruct(pkthunder_gobj);
	if ((wp == NULL) || (wp->kind != nWPKindPKThunderHead))
	{
		return;
	}
	head_dobj = DObjGetStruct(pkthunder_gobj);
	if (head_dobj == NULL)
	{
		return;
	}
	pos_before = status_vars->ness.specialhi.pkthunder_pos;
	status_vars->ness.specialhi.pkthunder_pos = head_dobj->translate.vec.f;
	syNetplayQuantizeNessPKThunderHoldStatusVars(fp, status_vars);
	if ((syNetplayNessPKThunderGateDiagEnabled() != FALSE) &&
	    ((pos_before.x != status_vars->ness.specialhi.pkthunder_pos.x) ||
	     (pos_before.y != status_vars->ness.specialhi.pkthunder_pos.y) ||
	     (pos_before.z != status_vars->ness.specialhi.pkthunder_pos.z)))
	{
		port_log(
		    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=pkthunder_pos_refresh player=%d status=%d "
		    "before=(%f,%f,%f) after=(%f,%f,%f) capture_only=%d resim=%d\n",
		    (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->status_id, pos_before.x, pos_before.y,
		    pos_before.z, status_vars->ness.specialhi.pkthunder_pos.x,
		    status_vars->ness.specialhi.pkthunder_pos.y, status_vars->ness.specialhi.pkthunder_pos.z,
		    (int)capture_only, (int)(syNetRollbackIsResimulating() != FALSE));
	}
}

void syNetplayNessRefreshPKThunderPosFromHead(GObj *fighter_gobj, FTStruct *fp)
{
	syNetplayNessRefreshPKThunderPosImpl(fighter_gobj, fp, &fp->status_vars, TRUE);
}

void syNetplayNessRefreshPKThunderPosInBlobFromHead(GObj *fighter_gobj, FTStruct *fp, union FTStatusVars *blob_status_vars)
{
	syNetplayNessRefreshPKThunderPosImpl(fighter_gobj, fp, blob_status_vars, FALSE);
}

void syNetplayNessRefreshPKThunderPosForJibakuLaunch(GObj *fighter_gobj, FTStruct *fp)
{
	GObj *pkthunder_gobj;
	DObj *head_dobj;
	DObj *fighter_dobj;
	WPStruct *wp;
	Vec3f pos_before;
	Vec3f head_pos;
	f32 anchor_head_dx;
	f32 anchor_head_dy;
	f32 anchor_head_dist_sq;
	f32 dist_x;
	f32 dist_y;

	if ((fighter_gobj == NULL) || (fp == NULL))
	{
		return;
	}
	if ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess))
	{
		return;
	}
	syNetplayNessProbeFighterNaN(fighter_gobj, fp, "jibaku_launch");
	syNetplayNessCullOrphanPKThunderKeepHead(fighter_gobj, fp);
	pos_before = fp->status_vars.ness.specialhi.pkthunder_pos;
	pkthunder_gobj = fp->status_vars.ness.specialhi.pkthunder_gobj;
	if ((pkthunder_gobj == NULL) || (wpGetStruct(pkthunder_gobj) == NULL))
	{
		pkthunder_gobj = syNetRbSnapReacquirePKThunderHeadForFighter(fighter_gobj);
		if (pkthunder_gobj != NULL)
		{
			fp->status_vars.ness.specialhi.pkthunder_gobj = pkthunder_gobj;
		}
	}
	if (pkthunder_gobj == NULL)
	{
		return;
	}
	wp = wpGetStruct(pkthunder_gobj);
	if ((wp == NULL) || (wp->kind != nWPKindPKThunderHead))
	{
		return;
	}
	head_dobj = DObjGetStruct(pkthunder_gobj);
	fighter_dobj = DObjGetStruct(fighter_gobj);
	if ((head_dobj == NULL) || (fighter_dobj == NULL))
	{
		return;
	}
	head_pos = head_dobj->translate.vec.f;
	anchor_head_dx = pos_before.x - head_pos.x;
	anchor_head_dy = pos_before.y - head_pos.y;
	anchor_head_dist_sq = (anchor_head_dx * anchor_head_dx) + (anchor_head_dy * anchor_head_dy);
	if (((pos_before.x != 0.0F) || (pos_before.y != 0.0F)) &&
	    (anchor_head_dist_sq <= SY_NETPLAY_NESS_PKTHUNDER_POS_STALE_DIST_SQ))
	{
		fp->status_vars.ness.specialhi.pkthunder_pos = pos_before;
	}
	else
	{
		fp->status_vars.ness.specialhi.pkthunder_pos = head_pos;
	}
	syNetplayQuantizeNessPKThunderHoldStatusVars(fp, &fp->status_vars);
	if (syNetplayNessPKThunderGateDiagEnabled() == FALSE)
	{
		return;
	}
	dist_x = fighter_dobj->translate.vec.f.x - fp->status_vars.ness.specialhi.pkthunder_pos.x;
	dist_y = (fighter_dobj->translate.vec.f.y + 150.0F) - fp->status_vars.ness.specialhi.pkthunder_pos.y;
	port_log(
	    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=jibaku_pos_refresh player=%d status=%d "
	    "before=(%f,%f,%f) after=(%f,%f,%f) dist=(%f,%f) resim=%d\n",
	    (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->status_id, pos_before.x, pos_before.y,
	    pos_before.z, fp->status_vars.ness.specialhi.pkthunder_pos.x,
	    fp->status_vars.ness.specialhi.pkthunder_pos.y, fp->status_vars.ness.specialhi.pkthunder_pos.z, dist_x,
	    dist_y, (int)(syNetRollbackIsResimulating() != FALSE));
}

void syNetplayNessReconcilePKThunderWeaponsAfterApply(GObj *fighter_gobj)
{
	FTStruct *fp;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess)))
	{
		return;
	}
	if (syNetplayNessFighterInPKThunderHoldScope(fp) == FALSE)
	{
		return;
	}
	syNetplayNessCullOrphanPKThunderKeepHead(fighter_gobj, fp);
	syNetplayNessSanitizePKThunderThrowStatusVars(fp);
}

static void syNetplayNessScheduleDeferPKTeardown(FTStruct *fp)
{
	u32 now_tick;
	u32 defer_until;

	if (fp == NULL)
	{
		return;
	}
	now_tick = syNetInputGetTick();
	defer_until = now_tick + SY_NETPLAY_NESS_PK_DEFER_CULL_TICKS;
	if (defer_until > sSYNetplayNessDeferPKCullUntilTick)
	{
		sSYNetplayNessDeferPKCullUntilTick = defer_until;
	}
	if ((fp->player >= 0) && (fp->player < GMCOMMON_PLAYERS_MAX))
	{
		sSYNetplayNessDeferPKCullPlayer = fp->player;
	}
}

sb32 syNetplayNessShouldDeferPKThunderTeardownForPlayer(s32 player)
{
	u32 now_tick;

	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return FALSE;
	}
	if ((player < 0) || (player >= GMCOMMON_PLAYERS_MAX))
	{
		return FALSE;
	}
	if (sSYNetplayNessDeferPKCullUntilTick == 0U)
	{
		return FALSE;
	}
	now_tick = syNetInputGetTick();
	if (now_tick >= sSYNetplayNessDeferPKCullUntilTick)
	{
		return FALSE;
	}
	return (player == sSYNetplayNessDeferPKCullPlayer) ? TRUE : FALSE;
}

sb32 syNetplayNessShouldDeferPKThunderHeadProcTeardown(WPStruct *wp)
{
	if (wp == NULL)
	{
		return FALSE;
	}
	return syNetplayNessShouldDeferPKThunderTeardownForPlayer((s32)wp->player);
}

sb32 syNetplayNessIsPKThunderGlobalDeferActive(void)
{
	u32 now_tick;

	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return FALSE;
	}
	if (sSYNetplayNessDeferPKCullUntilTick == 0U)
	{
		return FALSE;
	}
	now_tick = syNetInputGetTick();
	return (now_tick < sSYNetplayNessDeferPKCullUntilTick) ? TRUE : FALSE;
}

static const char *syNetplayNessAirJibakuGroundSnapBlockReason(const FTStruct *fp)
{
	s32 pi;
	u32 now_tick;

	if (fp == NULL)
	{
		return "null_fp";
	}
	if (fp->status_id != nFTNessStatusSpecialAirHiJibaku)
	{
		return "wrong_status";
	}
	if (syNetplayNessShouldDeferPKThunderTeardownForPlayer(fp->player) != FALSE)
	{
		return "defer_teardown";
	}
	pi = fp->player;
	if ((pi >= 0) && (pi < GMCOMMON_PLAYERS_MAX))
	{
		now_tick = syNetInputGetTick();
		if ((sSYNetplayNessPostCullFloorGraceUntilTick[pi] != 0U) &&
		    (now_tick < sSYNetplayNessPostCullFloorGraceUntilTick[pi]))
		{
			return "post_cull_grace";
		}
		if ((sSYNetplayNessAirJibakuStartTick[pi] != 0U) &&
		    (now_tick < (sSYNetplayNessAirJibakuStartTick[pi] + SY_NETPLAY_NESS_AIR_JIBAKU_LAUNCH_GUARD_TICKS)))
		{
			return "launch_guard";
		}
	}
	if (fp->ga == nMPKineticsGround)
	{
		return "already_ground";
	}
	if (fp->physics.vel_air.y > 0.0F)
	{
		return "ascending";
	}
	if ((fp->coll_data.floor_flags & MAP_VERTEX_COLL_PASS) && (fp->physics.vel_air.y <= 0.0F))
	{
		return "pass_floor_descent";
	}
	return NULL;
}

sb32 syNetplayNessShouldBlockAirJibakuGroundSnap(const FTStruct *fp)
{
	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return FALSE;
	}
	return (syNetplayNessAirJibakuGroundSnapBlockReason(fp) != NULL) ? TRUE : FALSE;
}

void syNetplayNessNotifyHoldEarlyExit(GObj *fighter_gobj, FTStruct *fp, const char *reason)
{
	u32 hold_frames;
	s32 pi;

	(void)fighter_gobj;
	if (fp == NULL)
	{
		return;
	}
	if ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess))
	{
		return;
	}
	if (syNetplayNessPKThunderGateDiagEnabled() == FALSE)
	{
		return;
	}
	hold_frames = 0U;
	pi = fp->player;
	if ((pi >= 0) && (pi < GMCOMMON_PLAYERS_MAX) && (sSYNetplayNessHoldEntryTick[pi] != 0U))
	{
		u32 now_tick = syNetInputGetTick();

		if (now_tick >= sSYNetplayNessHoldEntryTick[pi])
		{
			hold_frames = now_tick - sSYNetplayNessHoldEntryTick[pi];
		}
	}
	port_log(
	    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=hold_early_exit player=%d status=%d reason=%s delay=%d end_delay=%d hold_frames=%u thund_destroy=%d wp_gobj=%p resim=%d\n",
	    (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->status_id, (reason != NULL) ? reason : "?",
	    fp->status_vars.ness.specialhi.pkjibaku_delay, fp->status_vars.ness.specialhi.pkthunder_end_delay,
	    (unsigned int)hold_frames, (int)(fp->passive_vars.ness.is_thunder_destroy & TRUE),
	    (void *)fp->status_vars.ness.specialhi.pkthunder_gobj, (int)(syNetRollbackIsResimulating() != FALSE));
}

void syNetplayNessNotifyAirJibakuGroundSnapBlocked(GObj *fighter_gobj, FTStruct *fp)
{
	const char *reason;
	s32 pi;
	f32 pos_y;
	f32 vel_angle;

	if ((syNetplayNessPKThunderGateDiagEnabled() == FALSE) || (fighter_gobj == NULL) || (fp == NULL))
	{
		return;
	}
	reason = syNetplayNessAirJibakuGroundSnapBlockReason(fp);
	if (reason == NULL)
	{
		return;
	}
	pi = fp->player;
	if ((pi >= 0) && (pi < GMCOMMON_PLAYERS_MAX))
	{
		if (sSYNetplayNessGroundSnapBlockLogged[pi] != FALSE)
		{
			return;
		}
		sSYNetplayNessGroundSnapBlockLogged[pi] = TRUE;
	}
	pos_y = DObjGetStruct(fighter_gobj)->translate.vec.f.y;
	vel_angle = syVectorAngleDiff3D(&fp->coll_data.floor_angle, &fp->physics.vel_air);
	port_log(
	    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=air_jibaku_ground_snap_blocked player=%d reason=%s status=%d ga=%d pos_y=%f vel_air=%f,%f vel_angle=%f mask_curr=0x%X floor_flags=0x%X on_floor=%d\n",
	    (unsigned int)syNetInputGetTick(), (int)fp->player, reason, (int)fp->status_id, (int)fp->ga, pos_y,
	    fp->physics.vel_air.x, fp->physics.vel_air.y, vel_angle, (unsigned int)fp->coll_data.mask_curr,
	    (unsigned int)fp->coll_data.floor_flags, (int)(mpCommonCheckFighterOnFloor(fighter_gobj) != FALSE));
}

void syNetplayNessNotifyAirJibakuGroundSnap(GObj *fighter_gobj, FTStruct *fp, const char *source)
{
	f32 pos_y;
	f32 vel_angle;

	if ((syNetplayNessPKThunderGateDiagEnabled() == FALSE) || (fighter_gobj == NULL) || (fp == NULL))
	{
		return;
	}
	pos_y = DObjGetStruct(fighter_gobj)->translate.vec.f.y;
	vel_angle = syVectorAngleDiff3D(&fp->coll_data.floor_angle, &fp->physics.vel_air);
	port_log(
	    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=air_jibaku_ground_snap player=%d source=%s status=%d ga=%d pos_y=%f vel_air=%f,%f vel_angle=%f mask_curr=0x%X floor_flags=0x%X on_floor=%d\n",
	    (unsigned int)syNetInputGetTick(), (int)fp->player, (source != NULL) ? source : "?",
	    (int)fp->status_id, (int)fp->ga, pos_y, fp->physics.vel_air.x, fp->physics.vel_air.y, vel_angle,
	    (unsigned int)fp->coll_data.mask_curr, (unsigned int)fp->coll_data.floor_flags,
	    (int)(mpCommonCheckFighterOnFloor(fighter_gobj) != FALSE));
}

void syNetplayNessNotifyJibakuPostFinish(GObj *fighter_gobj)
{
	FTStruct *fp;

	if ((syNetplayNessPKThunderGateDiagEnabled() == FALSE) || (fighter_gobj == NULL))
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp == NULL)
	{
		return;
	}
	port_log(
	    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=jibaku_post_finish player=%d status=%d mask_curr=0x%X vel_air=%f,%f defer_teardown=%d resim=%d\n",
	    (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->status_id,
	    (unsigned int)fp->coll_data.mask_curr, fp->physics.vel_air.x, fp->physics.vel_air.y,
	    (int)(syNetplayNessShouldDeferPKThunderTeardownForPlayer(fp->player) != FALSE),
	    (int)(syNetRollbackIsResimulating() != FALSE));
}

static s32 syNetplayNessCountPKThunderHeadStatusForPlayer(s32 player, s32 *out_pkstatus)
{
	GObj *weapon_gobj;
	s32 count = 0;

	if (out_pkstatus != NULL)
	{
		*out_pkstatus = -1;
	}
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp = wpGetStruct(weapon_gobj);

		if ((wp == NULL) || (wp->player != player) || (wp->kind != nWPKindPKThunderHead))
		{
			continue;
		}
		count++;
		if (out_pkstatus != NULL)
		{
			*out_pkstatus = (s32)wp->weapon_vars.pkthunder.status;
		}
	}
	return count;
}

static void syNetplayNessLogJibakuWeaponState(FTStruct *fp)
{
	s32 weapons;
	s32 head_pkstatus;
	s32 head_count;

	if ((syNetplayNessPKThunderGateDiagEnabled() == FALSE) || (fp == NULL))
	{
		return;
	}
	weapons = syNetplayNessCountLivePKThunderWeapons();
	head_count = syNetplayNessCountPKThunderHeadStatusForPlayer((s32)fp->player, &head_pkstatus);
	port_log(
	    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=jibaku_weapon_state player=%d fighter_status=%d weapons=%d heads=%d head_pkstatus=%d cull_at_tick=%u resim=%d\n",
	    (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->status_id, weapons, head_count,
	    head_pkstatus, (unsigned int)sSYNetplayNessDeferPKCullUntilTick,
	    (int)(syNetRollbackIsResimulating() != FALSE));
}

static s32 syNetplayNessCountLivePKThunderWeapons(void)
{
	GObj *weapon_gobj;
	s32 count = 0;

	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp = wpGetStruct(weapon_gobj);

		if ((wp != NULL) &&
		    ((wp->kind == nWPKindPKThunderHead) || (wp->kind == nWPKindPKThunderTrail)))
		{
			count++;
		}
	}
	return count;
}

void syNetplayNessFinishJibakuTransition(GObj *fighter_gobj)
{
	FTStruct *fp;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess)))
	{
		return;
	}
	fp->is_effect_attach = FALSE;
	syNetplayNessScheduleDeferPKTeardown(fp);
	syNetplayNessArmAirJibakuFloorGrace(fp);
	syNetplayNessLogJibakuWeaponState(fp);
	syNetplayNessNotifyJibakuPhase(fighter_gobj, "finish");
	syNetplayNessNotifyJibakuPostFinish(fighter_gobj);
}

void syNetplayNessCatchUpPKJibakuIfDue(GObj *fighter_gobj, FTStruct *fp)
{
	s32 anim_length;

	if ((fighter_gobj == NULL) || (fp == NULL))
	{
		return;
	}
	if ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess))
	{
		return;
	}
	syNetplayNessSanitizePKJibakuStatusVars(fp);
	anim_length = fp->status_vars.ness.specialhi.pkjibaku_anim_length;

	if (fp->status_id == nFTNessStatusSpecialAirHiBound)
	{
		if (fighter_gobj->anim_frame <= 0.0F)
		{
			if (syNetplayNessPKThunderGateDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=bound_anim_end player=%d status=%d anim_frame=%f\n",
				    (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->status_id,
				    fighter_gobj->anim_frame);
			}
			ftCommonFallSpecialSetStatus(fighter_gobj, FTNESS_PKTHUNDER_FALLSPECIAL_DRIFT, FALSE, TRUE, TRUE,
			                           FTNESS_PKTHUNDER_LANDING_LAG, FALSE);
			syNetplayNessResetJibakuStallState(fp->player);
		}
		return;
	}

	if ((fp->status_id != nFTNessStatusSpecialHiJibaku) && (fp->status_id != nFTNessStatusSpecialAirHiJibaku))
	{
		return;
	}

	if (fp->status_id == nFTNessStatusSpecialHiJibaku)
	{
		s32 pi = fp->player;

		if ((pi >= 0) && (pi < GMCOMMON_PLAYERS_MAX))
		{
			if (anim_length == sSYNetplayNessJibakuLastAnimLength[pi])
			{
				sSYNetplayNessJibakuStallTicks[pi]++;
			}
			else
			{
				sSYNetplayNessJibakuStallTicks[pi] = 0;
			}
			sSYNetplayNessJibakuLastAnimLength[pi] = anim_length;

			if ((anim_length > 0) && (sSYNetplayNessJibakuStallTicks[pi] >= SY_NETPLAY_NESS_JIBAKU_STALL_TICKS))
			{
				fp->status_vars.ness.specialhi.pkjibaku_anim_length--;
				anim_length = fp->status_vars.ness.specialhi.pkjibaku_anim_length;
				sSYNetplayNessJibakuStallTicks[pi] = 0;
				sSYNetplayNessJibakuLastAnimLength[pi] = anim_length;
				if (syNetplayNessPKThunderGateDiagEnabled() != FALSE)
				{
					port_log(
					    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=jibaku_stall_tick player=%d status=%d anim_length=%d\n",
					    (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->status_id, anim_length);
				}
			}
		}
	}

	if (anim_length <= 0)
	{
		if (syNetplayNessPKThunderGateDiagEnabled() != FALSE)
		{
			port_log(
			    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=anim_length_zero player=%d status=%d anim_length=%d ga=%d status_tics=%u\n",
			    (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->status_id, anim_length,
			    (int)fp->ga, (unsigned int)fp->status_total_tics);
		}
		syNetplayNessClampResidualJibakuLaunchVelocity(fp);
		if (fp->status_id == nFTNessStatusSpecialAirHiJibaku)
		{
			ftNessSpecialAirHiEndSetStatus(fighter_gobj);
		}
		else
		{
			ftNessSpecialHiEndSetStatus(fighter_gobj);
		}
		syNetplayNessResetJibakuStallState(fp->player);
		return;
	}
}

static void syNetplayNessResetJibakuStallState(s32 player)
{
	if ((player >= 0) && (player < GMCOMMON_PLAYERS_MAX))
	{
		sSYNetplayNessJibakuLastAnimLength[player] = -1;
		sSYNetplayNessJibakuStallTicks[player] = 0;
	}
}

void syNetplayNessRunLiveJibakuCatchUpAll(void)
{
	GObj *fighter_gobj;
	u32 now_tick;
	s32 weapons_before;
	sb32 defer_active;

	now_tick = syNetInputGetTick();
	weapons_before = syNetplayNessCountLivePKThunderWeapons();
	defer_active =
	    (sSYNetplayNessDeferPKCullUntilTick != 0U) && (now_tick < sSYNetplayNessDeferPKCullUntilTick);

	if (defer_active != FALSE)
	{
		s32 weapons_after_partial;

		syNetplayNessCullOrphanPKThunderForPlayer(sSYNetplayNessDeferPKCullPlayer);
		weapons_after_partial = syNetplayNessCountLivePKThunderWeapons();
		if (syNetplayNessPKThunderGateDiagEnabled() != FALSE)
		{
			port_log(
			    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=jibaku_post_cull action=deferred weapons=%d weapons_after_partial=%d player=%d cull_at_tick=%u resim=%d\n",
			    (unsigned int)now_tick, weapons_before, weapons_after_partial, sSYNetplayNessDeferPKCullPlayer,
			    (unsigned int)sSYNetplayNessDeferPKCullUntilTick,
			    (int)(syNetRollbackIsResimulating() != FALSE));
		}
	}
	else
	{
		s32 weapons_after;
		sb32 had_pending_defer = (sSYNetplayNessDeferPKCullUntilTick != 0U) ? TRUE : FALSE;
		s32 defer_player = sSYNetplayNessDeferPKCullPlayer;

		syNetRbSnapCullAllOrphanPKThunderLive();
		syNetRbSnapPruneStaleNessPKWaveEffectsLive();
		weapons_after = syNetplayNessCountLivePKThunderWeapons();

		if ((syNetplayNessPKThunderGateDiagEnabled() != FALSE) &&
		    ((had_pending_defer != FALSE) || (weapons_before > 0) || (weapons_after > 0)))
		{
			port_log(
			    "SSB64 Netplay: NESS_PKTHUNDER_GATE tick=%u event=jibaku_post_cull action=cull weapons_before=%d weapons_after=%d player=%d resim=%d\n",
			    (unsigned int)now_tick, weapons_before, weapons_after, defer_player,
			    (int)(syNetRollbackIsResimulating() != FALSE));
		}
		if ((had_pending_defer != FALSE) && (defer_player >= 0) && (defer_player < GMCOMMON_PLAYERS_MAX))
		{
			u32 grace_until;

			grace_until = now_tick + SY_NETPLAY_NESS_PK_POST_CULL_FLOOR_GRACE_TICKS;
			if (grace_until > sSYNetplayNessPostCullFloorGraceUntilTick[defer_player])
			{
				sSYNetplayNessPostCullFloorGraceUntilTick[defer_player] = grace_until;
			}
		}
		sSYNetplayNessDeferPKCullUntilTick = 0U;
		sSYNetplayNessDeferPKCullPlayer = -1;
	}

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp == NULL) || ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess)))
		{
			continue;
		}
		syNetplayNessMaintainPkScopeEarliestLoadTick(fighter_gobj, fp);
		if (syNetplayNessFighterInPKJibakuCatchUpScope(fp) == FALSE)
		{
			if ((fp->player >= 0) && (fp->player < GMCOMMON_PLAYERS_MAX))
			{
				syNetplayNessResetJibakuStallState(fp->player);
			}
			continue;
		}
		syNetRbSnapRebindNessPKJibakuProcs(fighter_gobj, fp);
		syNetplayNessCatchUpPKJibakuIfDue(fighter_gobj, fp);
	}
	syNetplayNessCanonicalizeCouplingFightersIfJibakuBurst();
}

#endif /* PORT && SSB64_NETMENU */
