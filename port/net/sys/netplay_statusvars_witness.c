#include <sys/netplay_statusvars_witness.h>

#if defined(PORT) && defined(SSB64_NETMENU)

#include <ft/fighter.h>
#include <ft/ftchar/ftfox/ftfox.h>
#include <ft/ftdef.h>
#include <ft/ftstatusvars.h>

#include <stdlib.h>
#include <string.h>

extern void port_log(const char *fmt, ...);
extern u32 syNetInputGetTick(void);
extern sb32 syNetRollbackIsResimulating(void);

static u32 syNetplayStatusVarsWitnessF32Bits(f32 v)
{
    union
    {
        f32 f;
        u32 u;
    } bits;

    bits.f = v;
    return bits.u;
}

/* Non-finite (Inf or NaN): IEEE-754 all-ones exponent. Bit-pattern test only — never
 * a float compare, so it is deterministic across ISAs and survives -ffast-math. */
static sb32 syNetplayStatusVarsWitnessF32NonFinite(f32 v)
{
    return (((syNetplayStatusVarsWitnessF32Bits(v) >> 23) & 0xFFU) == 0xFFU) ? TRUE : FALSE;
}

#define SSB64_NETPLAY_STATUSVARS_OWNERSHIP_TABLE_SIZE 512

static sb32 s_statusvars_witness_enabled = -1;
static sb32 s_statusvars_witness_armed_logged = FALSE;
static s32 s_statusvars_witness_damage_init_depth = 0;
static FTStatusVarsOverlay s_statusvars_last_tag[4];
static FTStatusVarsOverlay s_statusvars_ownership_table[SSB64_NETPLAY_STATUSVARS_OWNERSHIP_TABLE_SIZE];

static const char *syNetplayStatusVarsWitnessOverlayName(FTStatusVarsOverlay overlay)
{
    switch (overlay)
    {
    case nFTStatusVarsOverlayDead:
        return "dead";
    case nFTStatusVarsOverlayRebirth:
        return "rebirth";
    case nFTStatusVarsOverlaySleep:
        return "sleep";
    case nFTStatusVarsOverlayEntry:
        return "entry";
    case nFTStatusVarsOverlayTurn:
        return "turn";
    case nFTStatusVarsOverlayKneeBend:
        return "kneebend";
    case nFTStatusVarsOverlayJumpAerial:
        return "jumpaerial";
    case nFTStatusVarsOverlayDamage:
        return "damage";
    case nFTStatusVarsOverlaySquat:
        return "squat";
    case nFTStatusVarsOverlayDokan:
        return "dokan";
    case nFTStatusVarsOverlayLanding:
        return "landing";
    case nFTStatusVarsOverlayFallSpecial:
        return "fallspecial";
    case nFTStatusVarsOverlayTwister:
        return "twister";
    case nFTStatusVarsOverlayTaruCann:
        return "tarucann";
    case nFTStatusVarsOverlayDownWait:
        return "downwait";
    case nFTStatusVarsOverlayDownBounce:
        return "downbounce";
    case nFTStatusVarsOverlayRebound:
        return "rebound";
    case nFTStatusVarsOverlayCliffWait:
        return "cliffwait";
    case nFTStatusVarsOverlayCliffMotion:
        return "cliffmotion";
    case nFTStatusVarsOverlayLift:
        return "lift";
    case nFTStatusVarsOverlayItemThrow:
        return "itemthrow";
    case nFTStatusVarsOverlayItemSwing:
        return "itemswing";
    case nFTStatusVarsOverlayFireFlower:
        return "fireflower";
    case nFTStatusVarsOverlayHammer:
        return "hammer";
    case nFTStatusVarsOverlayGuard:
        return "guard";
    case nFTStatusVarsOverlayEscape:
        return "escape";
    case nFTStatusVarsOverlayCatchMain:
        return "catchmain";
    case nFTStatusVarsOverlayCatchWait:
        return "catchwait";
    case nFTStatusVarsOverlayCapture:
        return "capture";
    case nFTStatusVarsOverlayThrown:
        return "thrown";
    case nFTStatusVarsOverlayCaptureKirby:
        return "capturekirby";
    case nFTStatusVarsOverlayCaptureYoshi:
        return "captureyoshi";
    case nFTStatusVarsOverlayCaptureCaptain:
        return "capturecaptain";
    case nFTStatusVarsOverlayThrowF:
        return "throwf";
    case nFTStatusVarsOverlayThrowFF:
        return "throwff";
    case nFTStatusVarsOverlayThrowFDamage:
        return "throwfdamage";
    case nFTStatusVarsOverlayAttack1:
        return "attack1";
    case nFTStatusVarsOverlayAttack100:
        return "attack100";
    case nFTStatusVarsOverlayAttackLw3:
        return "attacklw3";
    case nFTStatusVarsOverlayAttack4:
        return "attack4";
    case nFTStatusVarsOverlayAttackAir:
        return "attackair";
    case nFTStatusVarsOverlayFoxSpecialHi:
        return "fox_specialhi";
    default:
        return "none";
    }
}

static sb32 syNetplayStatusVarsWitnessEnabled(void)
{
    const char *env;

    if (s_statusvars_witness_enabled != -1)
    {
        return (s_statusvars_witness_enabled != 0) ? TRUE : FALSE;
    }
    env = getenv("SSB64_NETPLAY_STATUSVARS_WITNESS");
    s_statusvars_witness_enabled =
        ((env != NULL) && (env[0] != '\0') && (strcmp(env, "0") != 0)) ? 1 : 0;
    return (s_statusvars_witness_enabled != 0) ? TRUE : FALSE;
}

static void syNetplayStatusVarsWitnessFillRange(s32 status_start, s32 status_end, FTStatusVarsOverlay overlay)
{
    s32 status_id;

    if (status_start < 0)
    {
        status_start = 0;
    }
    if (status_end >= SSB64_NETPLAY_STATUSVARS_OWNERSHIP_TABLE_SIZE)
    {
        status_end = SSB64_NETPLAY_STATUSVARS_OWNERSHIP_TABLE_SIZE - 1;
    }
    for (status_id = status_start; status_id <= status_end; status_id++)
    {
        s_statusvars_ownership_table[status_id] = overlay;
    }
}

static void syNetplayStatusVarsWitnessInitOwnershipTable(void)
{
    static sb32 initialized = FALSE;
    s32 i;

    if (initialized != FALSE)
    {
        return;
    }
    initialized = TRUE;

    for (i = 0; i < SSB64_NETPLAY_STATUSVARS_OWNERSHIP_TABLE_SIZE; i++)
    {
        s_statusvars_ownership_table[i] = nFTStatusVarsOverlayNone;
    }

    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusDeadDown, nFTCommonStatusDeadUpFall, nFTStatusVarsOverlayDead);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusSleep, nFTCommonStatusSleep, nFTStatusVarsOverlaySleep);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusEntry, nFTCommonStatusEntryNull, nFTStatusVarsOverlayEntry);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusRebirthDown, nFTCommonStatusRebirthWait, nFTStatusVarsOverlayRebirth);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusTurn, nFTCommonStatusTurnRun, nFTStatusVarsOverlayTurn);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusKneeBend, nFTCommonStatusGuardKneeBend, nFTStatusVarsOverlayKneeBend);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusJumpAerialF, nFTCommonStatusJumpAerialB, nFTStatusVarsOverlayJumpAerial);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusSquat, nFTCommonStatusSquatRv, nFTStatusVarsOverlaySquat);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusLandingLight, nFTCommonStatusLandingHeavy, nFTStatusVarsOverlayLanding);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusDamageStart, nFTCommonStatusDamageEnd, nFTStatusVarsOverlayDamage);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusFallSpecial, nFTCommonStatusLandingFallSpecial, nFTStatusVarsOverlayFallSpecial);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusTwister, nFTCommonStatusTwister, nFTStatusVarsOverlayTwister);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusTaruCann, nFTCommonStatusTaruCann, nFTStatusVarsOverlayTaruCann);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusDokanStart, nFTCommonStatusDokanWalk, nFTStatusVarsOverlayDokan);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusDownWaitD, nFTCommonStatusDownWaitU, nFTStatusVarsOverlayDownWait);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusDownBounceD, nFTCommonStatusDownBounceU, nFTStatusVarsOverlayDownBounce);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusReboundWait, nFTCommonStatusRebound, nFTStatusVarsOverlayRebound);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusCliffWait, nFTCommonStatusCliffWait, nFTStatusVarsOverlayCliffWait);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusCliffQuick, nFTCommonStatusCliffEscapeSlow2, nFTStatusVarsOverlayCliffMotion);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusLiftWait, nFTCommonStatusLiftTurn, nFTStatusVarsOverlayLift);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusLightThrowStart, nFTCommonStatusHeavyThrowEnd, nFTStatusVarsOverlayItemThrow);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusItemSwingStart, nFTCommonStatusItemSwingEnd, nFTStatusVarsOverlayItemSwing);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusFireFlowerShoot, nFTCommonStatusFireFlowerShootAir, nFTStatusVarsOverlayFireFlower);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusHammerStart, nFTCommonStatusHammerEnd, nFTStatusVarsOverlayHammer);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusGuardStart, nFTCommonStatusGuardEnd, nFTStatusVarsOverlayGuard);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusEscapeF, nFTCommonStatusEscapeB, nFTStatusVarsOverlayEscape);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusCatch, nFTCommonStatusCatchPull, nFTStatusVarsOverlayCatchMain);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusCatchWait, nFTCommonStatusCatchWait, nFTStatusVarsOverlayCatchWait);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusThrowF, nFTCommonStatusThrowB, nFTStatusVarsOverlayThrowF);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusCapturePulled, nFTCommonStatusCaptureWait, nFTStatusVarsOverlayCapture);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusCaptureKirby, nFTCommonStatusCaptureWaitKirby, nFTStatusVarsOverlayCaptureKirby);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusCaptureYoshi, nFTCommonStatusYoshiEgg, nFTStatusVarsOverlayCaptureYoshi);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusCaptureCaptain, nFTCommonStatusCaptureCaptain, nFTStatusVarsOverlayCaptureCaptain);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusThrownStart, nFTCommonStatusThrownEnd, nFTStatusVarsOverlayThrown);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusThrownKirbyStar, nFTCommonStatusThrownCopyStar, nFTStatusVarsOverlayThrown);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusAttack11, nFTCommonStatusAttackDash, nFTStatusVarsOverlayAttack1);
    syNetplayStatusVarsWitnessFillRange(nFTCommonStatusAttackAirStart, nFTCommonStatusAttackAirEnd, nFTStatusVarsOverlayAttackAir);
    syNetplayStatusVarsWitnessFillRange(nFTFoxStatusSpecialHiStart, nFTFoxStatusSpecialAirHi, nFTStatusVarsOverlayFoxSpecialHi);
}

static FTStatusVarsOverlay syNetplayStatusVarsWitnessExpectedOverlay(const FTStruct *fp)
{
    FTStatusVarsOverlay expected;

    syNetplayStatusVarsWitnessInitOwnershipTable();

    if ((fp->status_id >= 0) && (fp->status_id < SSB64_NETPLAY_STATUSVARS_OWNERSHIP_TABLE_SIZE))
    {
        expected = s_statusvars_ownership_table[fp->status_id];
        if (expected != nFTStatusVarsOverlayNone)
        {
            return expected;
        }
    }

    /*
     * Appear / Entry camera paths reuse entry overlay before actionable sim statuses.
     * Do not tag Wait+ as entry solely from camera_mode — intro countdown runs in Wait
     * while camera_mode may still be Entry (soak1: false stomps on Turn/Squat/Dash).
     */
    if ((fp->camera_mode == nFTCameraModeEntry) || (fp->camera_mode == nFTCameraModeExplain))
    {
        if (fp->status_id < nFTCommonStatusWait)
        {
            return nFTStatusVarsOverlayEntry;
        }
    }

    return nFTStatusVarsOverlayNone;
}

static sb32 syNetplayStatusVarsWitnessIsAllowedCrossOverlay(const FTStruct *fp, FTStatusVarsOverlay accessed,
                                                            FTStatusVarsOverlay expected)
{
    if (s_statusvars_witness_damage_init_depth > 0)
    {
        if (accessed == nFTStatusVarsOverlayDamage)
        {
            return TRUE;
        }
    }
    if (accessed == nFTStatusVarsOverlayDamage)
    {
        if (expected == nFTStatusVarsOverlayThrown)
        {
            return TRUE;
        }
    }
    if ((accessed == nFTStatusVarsOverlayEntry) && (fp != NULL))
    {
        if ((fp->camera_mode == nFTCameraModeEntry) || (fp->camera_mode == nFTCameraModeExplain))
        {
            return TRUE;
        }
    }
    if ((accessed == nFTStatusVarsOverlayTurn) && (fp != NULL))
    {
        if ((fp->status_id >= nFTCommonStatusWait) && (fp->status_id <= nFTCommonStatusOttotto))
        {
            return TRUE;
        }
    }
    return FALSE;
}

void syNetplayStatusVarsWitnessEnterDamageInit(void)
{
    s_statusvars_witness_damage_init_depth++;
}

void syNetplayStatusVarsWitnessLeaveDamageInit(void)
{
    if (s_statusvars_witness_damage_init_depth > 0)
    {
        s_statusvars_witness_damage_init_depth--;
    }
}

static void syNetplayStatusVarsWitnessLogArmedOnce(void)
{
    if (s_statusvars_witness_armed_logged != FALSE)
    {
        return;
    }
    s_statusvars_witness_armed_logged = TRUE;
    port_log("SSB64 NetStatusVars: witness armed (SSB64_NETPLAY_STATUSVARS_WITNESS=1)\n");
}

static void syNetplayStatusVarsWitnessCheckIntegrity(const FTStruct *fp, FTStatusVarsOverlay overlay, FTStatusVarsOverlay expected)
{
    /*
     * Read status_vars.common.* directly — never ftStatusVars*() accessors here.
     * Accessors call syNetplayStatusVarsWitnessNoteAccess → CheckIntegrity again (infinite recursion / SIGSEGV).
     */
    if ((overlay == nFTStatusVarsOverlayEntry) || (expected == nFTStatusVarsOverlayEntry))
    {
        if ((fp->hit_lr == -1) || (fp->hit_lr == 1))
        {
            if (fp->status_vars.common.entry.lr != fp->hit_lr)
            {
                port_log(
                    "SSB64 NetStatusVars: corrupt entry_lr tick=%u player=%d fkind=%d status_id=%d entry_lr=%d hit_lr=%d fp_lr=%d\n",
                    (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->fkind, (int)fp->status_id,
                    (int)fp->status_vars.common.entry.lr, (int)fp->hit_lr, (int)fp->lr);
            }
        }
    }

    if (fp->status_id == nFTCommonStatusCatchWait)
    {
        s32 throw_wait = fp->status_vars.common.catchwait.throw_wait;
        s32 shuffle = fp->shuffle_tics;
        s32 drift = throw_wait - shuffle;

        if ((shuffle > 0) && (drift != 0) && (drift != 1))
        {
            port_log(
                "SSB64 NetStatusVars: corrupt catchwait tick=%u player=%d fkind=%d status_id=%d throw_wait=%d shuffle_tics=%d\n",
                (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->fkind, (int)fp->status_id,
                (int)throw_wait, (int)shuffle);
        }
    }

    if ((fp->status_id >= nFTCommonStatusDeadDown) && (fp->status_id <= nFTCommonStatusDeadUpFall))
    {
        if (fp->status_vars.common.dead.wait != fp->dead_gate_wait)
        {
            port_log(
                "SSB64 NetStatusVars: corrupt dead_gate tick=%u player=%d fkind=%d status_id=%d union_wait=%d dead_gate_wait=%d\n",
                (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->fkind, (int)fp->status_id,
                (int)fp->status_vars.common.dead.wait, (int)fp->dead_gate_wait);
        }
    }

    if ((overlay == nFTStatusVarsOverlayKneeBend) &&
        ((fp->status_id == nFTCommonStatusKneeBend) || (fp->status_id == nFTCommonStatusGuardKneeBend)) &&
        (fp->attr != NULL))
    {
        f32 anim_frame = fp->status_vars.common.kneebend.anim_frame;
        f32 anim_length = fp->attr->kneebend_anim_length;

        if ((anim_frame < -1.0F) || (anim_frame > (anim_length + 30.0F)))
        {
            port_log(
                "SSB64 NetStatusVars: corrupt kneebend_stuck tick=%u player=%d fkind=%d status_id=%d anim_frame=%.2f anim_length=%.2f\n",
                (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->fkind, (int)fp->status_id,
                anim_frame, anim_length);
        }
    }

    if ((overlay == nFTStatusVarsOverlayJumpAerial) ||
        ((fp->status_id >= nFTCommonStatusJumpAerialF) && (fp->status_id <= nFTCommonStatusJumpAerialB)))
    {
        f32 drift = fp->status_vars.common.jumpaerial.drift;
        f32 vel_x = fp->status_vars.common.jumpaerial.vel_x;
        f32 vair_x = fp->physics.vel_air.x;
        f32 vair_y = fp->physics.vel_air.y;

        if ((syNetplayStatusVarsWitnessF32NonFinite(drift) != FALSE) ||
            (syNetplayStatusVarsWitnessF32NonFinite(vel_x) != FALSE) ||
            (syNetplayStatusVarsWitnessF32NonFinite(vair_x) != FALSE) ||
            (syNetplayStatusVarsWitnessF32NonFinite(vair_y) != FALSE))
        {
            /* Accessor fires multiple times per tick (ProcPhysics reads the overlay 4x);
             * dedup to one line per player per tick. Raw bit patterns expose the cross-ISA
             * +nan (0x7FC00000) vs -nan (0xFFC00000) divergence that drives the desync. */
            static u32 jumpaerial_last_tick[4] = { 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU };
            s32 player = fp->player;
            u32 tick = syNetInputGetTick();

            if ((player >= 0) && (player < 4) && (jumpaerial_last_tick[player] != tick))
            {
                jumpaerial_last_tick[player] = tick;
                port_log(
                    "SSB64 NetStatusVars: corrupt jumpaerial tick=%u player=%d fkind=%d status_id=%d "
                    "drift=0x%08x vel_x=0x%08x vel_air=(0x%08x,0x%08x) turn_tics=%d resim=%d\n",
                    (unsigned int)tick, (int)player, (int)fp->fkind, (int)fp->status_id,
                    (unsigned int)syNetplayStatusVarsWitnessF32Bits(drift),
                    (unsigned int)syNetplayStatusVarsWitnessF32Bits(vel_x),
                    (unsigned int)syNetplayStatusVarsWitnessF32Bits(vair_x),
                    (unsigned int)syNetplayStatusVarsWitnessF32Bits(vair_y),
                    (int)fp->status_vars.common.jumpaerial.turn_tics,
                    (int)(syNetRollbackIsResimulating() != FALSE));
            }
        }
    }
}

void syNetplayStatusVarsWitnessNoteAccess(const FTStruct *fp, FTStatusVarsOverlay overlay)
{
    FTStatusVarsOverlay expected;
    s32 player;

    if (fp == NULL)
    {
        return;
    }

    player = fp->player;
    if ((player < 0) || (player >= 4))
    {
        return;
    }

    s_statusvars_last_tag[player] = overlay;

    if (syNetplayStatusVarsWitnessEnabled() == FALSE)
    {
        return;
    }

    syNetplayStatusVarsWitnessLogArmedOnce();

    expected = syNetplayStatusVarsWitnessExpectedOverlay(fp);
    syNetplayStatusVarsWitnessCheckIntegrity(fp, overlay, expected);

    if (expected == nFTStatusVarsOverlayNone)
    {
        return;
    }
    if (expected == overlay)
    {
        return;
    }
    if (syNetplayStatusVarsWitnessIsAllowedCrossOverlay(fp, overlay, expected) != FALSE)
    {
        return;
    }

    port_log(
        "SSB64 NetStatusVars: witness stomp tick=%u player=%d fkind=%d status_id=%d accessed=%s expected=%s "
        "hit_lr=%d shuffle_tics=%d fp_lr=%d\n",
        (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->fkind, (int)fp->status_id,
        syNetplayStatusVarsWitnessOverlayName(overlay), syNetplayStatusVarsWitnessOverlayName(expected),
        (int)fp->hit_lr, (int)fp->shuffle_tics, (int)fp->lr);
}

/*
 * Logged once at each aerial-jump SetStatus boundary (env-gated, read-only). Captures the
 * forward-sim writer's vel_air + jumpaerial overlay the instant the double jump is armed —
 * the frame the cross-ISA NaN first appears. Compare host vs guest bit patterns: matching
 * finite values here with a later "corrupt jumpaerial" line localizes the NaN to a tick the
 * per-tick integrity check already covers; a divergent entry pins it to the SetStatus path.
 */
void syNetplayStatusVarsWitnessProbeJumpAerialEntry(const FTStruct *fp)
{
    if (fp == NULL)
    {
        return;
    }
    if (syNetplayStatusVarsWitnessEnabled() == FALSE)
    {
        return;
    }

    syNetplayStatusVarsWitnessLogArmedOnce();

    port_log(
        "SSB64 NetStatusVars: jumpaerial entry tick=%u player=%d fkind=%d status_id=%d "
        "drift=0x%08x vel_x=0x%08x vel_air=(0x%08x,0x%08x) turn_tics=%d jumps_used=%d resim=%d\n",
        (unsigned int)syNetInputGetTick(), (int)fp->player, (int)fp->fkind, (int)fp->status_id,
        (unsigned int)syNetplayStatusVarsWitnessF32Bits(fp->status_vars.common.jumpaerial.drift),
        (unsigned int)syNetplayStatusVarsWitnessF32Bits(fp->status_vars.common.jumpaerial.vel_x),
        (unsigned int)syNetplayStatusVarsWitnessF32Bits(fp->physics.vel_air.x),
        (unsigned int)syNetplayStatusVarsWitnessF32Bits(fp->physics.vel_air.y),
        (int)fp->status_vars.common.jumpaerial.turn_tics, (int)fp->jumps_used,
        (int)(syNetRollbackIsResimulating() != FALSE));
}

/*
 * Dumps the inputs to ftPhysicsGetAirVelTransN() the first tick any of its outputs
 * (drift/vel_air.y/vel_air.z) goes non-finite. The Ness/Yoshi double-jump NaN is born here:
 *   drift = anim_vel_z*cos - anim_vel_y*sin ; vy = anim_vel_z*sin + anim_vel_y*cos
 * where cos/sin = __cosf/__sinf(transn rot_z), and anim_vel_* derive from the TransN joint
 * translate minus fp->anim_vel times the TopN joint scale. Logging raw bit patterns localizes
 * whether the non-finite source is rot_z (-> trig NaN), a joint transform, or anim_vel — and
 * exposes the cross-ISA +nan/-nan sign split. Read-only, env-gated, one line per player per tick.
 */
void syNetplayStatusVarsWitnessProbeAirVelTransN(const FTStruct *fp, const DObj *transn_joint,
                                                 const DObj *topn_joint, f32 out_drift, f32 out_vy,
                                                 f32 out_vz, f32 cos_v, f32 sin_v)
{
    s32 player;
    u32 tick;
    static u32 airveltransn_last_tick[4] = { 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU };

    if ((fp == NULL) || (transn_joint == NULL) || (topn_joint == NULL))
    {
        return;
    }
    if (syNetplayStatusVarsWitnessEnabled() == FALSE)
    {
        return;
    }
    if ((syNetplayStatusVarsWitnessF32NonFinite(out_drift) == FALSE) &&
        (syNetplayStatusVarsWitnessF32NonFinite(out_vy) == FALSE) &&
        (syNetplayStatusVarsWitnessF32NonFinite(out_vz) == FALSE))
    {
        return;
    }

    player = fp->player;
    tick = syNetInputGetTick();
    if ((player < 0) || (player >= 4) || (airveltransn_last_tick[player] == tick))
    {
        return;
    }
    airveltransn_last_tick[player] = tick;

    syNetplayStatusVarsWitnessLogArmedOnce();

    port_log(
        "SSB64 NetStatusVars: airveltransn_nan tick=%u player=%d fkind=%d status_id=%d "
        "out=(drift=0x%08x,vy=0x%08x,vz=0x%08x) rot_z=0x%08x transn_t=(0x%08x,0x%08x,0x%08x) "
        "topn_s=(0x%08x,0x%08x,0x%08x) anim_vel=(0x%08x,0x%08x,0x%08x) cos=0x%08x sin=0x%08x lr=%d resim=%d\n",
        (unsigned int)tick, (int)player, (int)fp->fkind, (int)fp->status_id,
        (unsigned int)syNetplayStatusVarsWitnessF32Bits(out_drift),
        (unsigned int)syNetplayStatusVarsWitnessF32Bits(out_vy),
        (unsigned int)syNetplayStatusVarsWitnessF32Bits(out_vz),
        (unsigned int)syNetplayStatusVarsWitnessF32Bits(transn_joint->rotate.vec.f.z),
        (unsigned int)syNetplayStatusVarsWitnessF32Bits(transn_joint->translate.vec.f.x),
        (unsigned int)syNetplayStatusVarsWitnessF32Bits(transn_joint->translate.vec.f.y),
        (unsigned int)syNetplayStatusVarsWitnessF32Bits(transn_joint->translate.vec.f.z),
        (unsigned int)syNetplayStatusVarsWitnessF32Bits(topn_joint->scale.vec.f.x),
        (unsigned int)syNetplayStatusVarsWitnessF32Bits(topn_joint->scale.vec.f.y),
        (unsigned int)syNetplayStatusVarsWitnessF32Bits(topn_joint->scale.vec.f.z),
        (unsigned int)syNetplayStatusVarsWitnessF32Bits(fp->anim_vel.x),
        (unsigned int)syNetplayStatusVarsWitnessF32Bits(fp->anim_vel.y),
        (unsigned int)syNetplayStatusVarsWitnessF32Bits(fp->anim_vel.z),
        (unsigned int)syNetplayStatusVarsWitnessF32Bits(cos_v),
        (unsigned int)syNetplayStatusVarsWitnessF32Bits(sin_v), (int)fp->lr,
        (int)(syNetRollbackIsResimulating() != FALSE));
}

#endif
