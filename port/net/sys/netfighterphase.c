#include <sys/netfighterphase.h>

#include <ft/fighter.h>
#include <ft/ftdef.h>
#include <sc/sctypes.h>
#include <sc/scmanager.h>
#include <sys/controller.h>
#include <sys/netinput.h>
#include <sys/netpeer.h>
#include <sys/netrollback.h>
#include <sys/netsync.h>

#include <stdlib.h>
#include <string.h>

extern int port_get_frame_count(void);

extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);

#define SYNETFIGHTER_PHASE_SLOTS MAXCONTROLLERS

typedef struct SYNetFighterPhaseSlotTrace
{
	u32 tick;
	u32 exec_order;
	u32 ctrl_hash_before;
	u32 hist_hash;
	u32 pl_hash_after;
	u32 st_hash_phase_a;
	u32 st_hash_phase_b;
	u32 st_hash_phase_c;
	u8 slot;
	u8 pkind;
	u8 phase_a_done;
	u8 phase_b_done;
	u8 phase_c_done;
	u8 valid;
} SYNetFighterPhaseSlotTrace;

static SYNetFighterPhaseSlotTrace s_slot[SYNETFIGHTER_PHASE_SLOTS];
static u32 s_capture_tick;
static u32 s_exec_counter;

static u32 syNetFighterPhaseFnv(u32 h, u32 v)
{
	h ^= v;
	h *= 16777619U;

	return h;
}

static u32 syNetFighterPhaseHashSyController(const SYController *c)
{
	u32 h;

	h = 2166136261U;
	if (c == NULL)
	{
		return h;
	}
	h = syNetFighterPhaseFnv(h, (u32)c->button_hold);
	h = syNetFighterPhaseFnv(h, (u32)c->button_tap);
	h = syNetFighterPhaseFnv(h, (u32)c->button_release);
	h = syNetFighterPhaseFnv(h, (u32)(u8)c->stick_range.x);
	h = syNetFighterPhaseFnv(h, (u32)(u8)c->stick_range.y);
	return h;
}

static u32 syNetFighterPhaseHashNetInputFrame(const SYNetInputFrame *f)
{
	u32 h;

	h = 2166136261U;
	if (f == NULL)
	{
		return h;
	}
	h = syNetFighterPhaseFnv(h, f->tick);
	h = syNetFighterPhaseFnv(h, (u32)f->buttons);
	h = syNetFighterPhaseFnv(h, (u32)(u8)f->stick_x);
	h = syNetFighterPhaseFnv(h, (u32)(u8)f->stick_y);
	h = syNetFighterPhaseFnv(h, (u32)f->source);
	h = syNetFighterPhaseFnv(h, (u32)f->is_predicted);
	h = syNetFighterPhaseFnv(h, (u32)f->is_valid);
	return h;
}

static u32 syNetFighterPhaseHashPl(const FTPlayerInput *pl)
{
	u32 h;

	h = 2166136261U;
	if (pl == NULL)
	{
		return h;
	}
	h = syNetFighterPhaseFnv(h, (u32)pl->button_hold);
	h = syNetFighterPhaseFnv(h, (u32)pl->button_tap);
	h = syNetFighterPhaseFnv(h, (u32)pl->button_release);
	h = syNetFighterPhaseFnv(h, (u32)(u8)pl->stick_range.x);
	h = syNetFighterPhaseFnv(h, (u32)(u8)pl->stick_range.y);
	return h;
}

static int syNetFighterPhaseGetTraceLevel(void)
{
	static int s_cache = -999;
	const char *e;

	if (s_cache != -999)
	{
		return s_cache;
	}
	e = getenv("SSB64_NETPLAY_FIGHTER_PHASE_TRACE");
	s_cache = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 0;
	return s_cache;
}

static int syNetFighterPhaseGetAssertLevel(void)
{
	static int s_cache = -999;
	const char *e;

	if (s_cache != -999)
	{
		return s_cache;
	}
	e = getenv("SSB64_NETPLAY_FIGHTER_PHASE_ASSERT");
	s_cache = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 0;
	return s_cache;
}

static sb32 syNetFighterPhaseShouldTrace(void)
{
	if (syNetFighterPhaseGetTraceLevel() <= 0)
	{
		return FALSE;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return FALSE;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return FALSE;
	}
	if (gSCManagerBattleState->game_status != nSCBattleGameStatusGo)
	{
		return FALSE;
	}
	return TRUE;
}

void syNetFighterPhaseTraceGcRunAllBegin(void)
{
	if (syNetFighterPhaseShouldTrace() == FALSE)
	{
		return;
	}
	(void)memset(s_slot, 0, sizeof(s_slot));
	s_exec_counter = 0U;
	s_capture_tick = syNetInputGetTick();
}

/*
 * Double-step witness (always on in netmenu VS, independent of the trace env).
 *
 * ftMainProcUpdateInterrupt increments status_total_tics once per authoritative tick when
 * control is enabled; this hook runs at its very top, so counting invocations here counts
 * exactly what the increment counts. Soaks 2026-08-31 showed a fighter's live state
 * coherently +2 then +3 ticks ahead of its own ring blob -- status_total_tics, the stick
 * latch counters, vel_air, and joint anims all together -- with every status transition
 * matching across peers and passes: the fighter is being STEPPED extra times relative to
 * the ring, outside any transition. This names the event at the instant it happens:
 * a second non-resim update of the same (player, authoritative tick).
 */
#define SYNETFIGHTER_DS_SLOTS 4
static u32 sDsLastTick[SYNETFIGHTER_DS_SLOTS];
static u32 sDsCountAtTick[SYNETFIGHTER_DS_SLOTS];
/*
 * Rollback-load generation. A load DISCARDS live state, so re-running an already-run tick
 * after a load is correct GGPO, not a double-step -- the first witness build counted
 * exactly those boundaries (32 hits, all at "POST_RESIM_LIVE sim==target" frames, soak
 * 2026-08-31) and said nothing about the real leak. Each load voids the per-slot counts;
 * only a second unflagged execution within ONE generation is pathological.
 */
static u32 sDsLoadGeneration;
static u32 sDsSlotGeneration[SYNETFIGHTER_DS_SLOTS];

void syNetFighterPhaseNoteRollbackLoad(void)
{
	sDsLoadGeneration++;
}

static u8 sDsFlagsAtTick[SYNETFIGHTER_DS_SLOTS]; /* bit0: seen resim=0, bit1: seen resim=1 */

static void syNetFighterPhaseDoubleStepWitness(FTStruct *fp)
{
	u32 tick;
	s32 slot;
	u8 flag;

	if ((fp == NULL) || (fp->pkind != nFTPlayerKindMan) || (fp->is_control_disable != FALSE))
	{
		return;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	slot = fp->player;
	if ((slot < 0) || (slot >= SYNETFIGHTER_DS_SLOTS))
	{
		return;
	}
	/*
	 * v3: count EVERY execution -- resim and live -- per (load generation, tick).
	 * Within one generation a tick's fighter update must run exactly once, whatever the
	 * flag: a load (gen bump) is the only thing that legitimizes re-execution. v1 counted
	 * correct GGPO re-execution (fixed by generations); v2 excluded resim=1 entirely and
	 * went silent while the drift continued at +1 per episode on BOTH players -- the
	 * signature of the resim walk executing the target tick flagged, then live re-running
	 * it unflagged, same generation, no load between. v3's flags field records which
	 * combination fired so the log names that case directly (flags=3 = one of each).
	 */
	flag = (syNetRollbackIsResimulating() != FALSE) ? 2U : 1U;
	tick = syNetInputGetTick();
	if ((sDsLastTick[slot] != tick) || (sDsSlotGeneration[slot] != sDsLoadGeneration))
	{
		sDsLastTick[slot] = tick;
		sDsSlotGeneration[slot] = sDsLoadGeneration;
		sDsCountAtTick[slot] = 1U;
		sDsFlagsAtTick[slot] = flag;
		return;
	}
	sDsCountAtTick[slot]++;
	sDsFlagsAtTick[slot] |= flag;
	if (sDsCountAtTick[slot] == 2U)
	{
		port_log("SSB64 NetFighterPhase: DOUBLE_STEP player=%d tick=%u status=%d total_tics=%u "
		         "rollback_active=%d gen=%u flags=%u frame=%d\n",
		         (int)fp->player, (unsigned int)tick, (int)fp->status_id,
		         (unsigned int)fp->status_total_tics, (int)syNetRollbackIsActive(),
		         (unsigned int)sDsLoadGeneration, (unsigned int)sDsFlagsAtTick[slot],
		         port_get_frame_count());
	}
}

void syNetFighterPhaseOnInterruptVeryStart(GObj *fighter_gobj)
{
	FTStruct *fp;
	s32 slot;
	SYNetInputFrame hist;
	u32 hh;
	SYNetFighterPhaseSlotTrace *t;

	syNetFighterPhaseDoubleStepWitness(ftGetStruct(fighter_gobj));
	if (syNetFighterPhaseShouldTrace() == FALSE)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp->pkind != nFTPlayerKindMan)
	{
		return;
	}
	slot = fp->player;
	if ((slot < 0) || (slot >= SYNETFIGHTER_PHASE_SLOTS))
	{
		return;
	}
	t = &s_slot[slot];
	(void)memset(t, 0, sizeof(*t));
	t->slot = (u8)slot;
	t->pkind = (u8)fp->pkind;
	t->tick = s_capture_tick;
	t->exec_order = s_exec_counter++;
	t->ctrl_hash_before = syNetFighterPhaseHashSyController(&gSYControllerDevices[slot]);
	t->st_hash_phase_a = syNetSyncHashFighterStructLight(fp);
	t->phase_a_done = 1U;
	if (syNetInputGetHistoryFrame(slot, s_capture_tick, &hist) != FALSE)
	{
		t->hist_hash = syNetFighterPhaseHashNetInputFrame(&hist);
	}
	else
	{
		t->hist_hash = 0U;
	}
	if (syNetFighterPhaseGetAssertLevel() >= 1)
	{
		if ((t->hist_hash != 0U) && (t->ctrl_hash_before != t->hist_hash))
		{
			port_log(
			    "SSB64 NetSync: ft_phase_assert tick=%u slot=%d ctrl=0x%08X hist=0x%08X (SYController vs history frame at gcRunAll tick)\n",
			    s_capture_tick, (int)slot, t->ctrl_hash_before, t->hist_hash);
		}
	}
	if (syNetFighterPhaseGetTraceLevel() >= 2)
	{
		if ((t->hist_hash != 0U) && (t->ctrl_hash_before != t->hist_hash))
		{
			port_log("SSB64 NetSync: ft_phase_warn tick=%u slot=%d ctrl_hist_mismatch (trace>=2 immediate)\n",
			         s_capture_tick, (int)slot);
		}
	}
}

void syNetFighterPhaseOnInterruptAfterInputControl(GObj *fighter_gobj)
{
	FTStruct *fp;
	s32 slot;
	SYNetFighterPhaseSlotTrace *t;

	if (syNetFighterPhaseShouldTrace() == FALSE)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp->pkind != nFTPlayerKindMan)
	{
		return;
	}
	slot = fp->player;
	if ((slot < 0) || (slot >= SYNETFIGHTER_PHASE_SLOTS))
	{
		return;
	}
	t = &s_slot[slot];
	if (t->phase_a_done == 0U)
	{
		return;
	}
	t->pl_hash_after = syNetFighterPhaseHashPl(&fp->input.pl);
	t->st_hash_phase_b = syNetSyncHashFighterStructLight(fp);
	t->phase_b_done = 1U;
}

void syNetFighterPhaseOnParamsEnd(GObj *fighter_gobj)
{
	FTStruct *fp;
	s32 slot;
	SYNetFighterPhaseSlotTrace *t;

	if (syNetFighterPhaseShouldTrace() == FALSE)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp->pkind != nFTPlayerKindMan)
	{
		return;
	}
	slot = fp->player;
	if ((slot < 0) || (slot >= SYNETFIGHTER_PHASE_SLOTS))
	{
		return;
	}
	t = &s_slot[slot];
	if (t->phase_a_done == 0U)
	{
		return;
	}
	t->st_hash_phase_c = syNetSyncHashFighterStructLight(fp);
	t->phase_c_done = 1U;
	t->valid = 1U;
}

void syNetFighterPhaseTraceEmitNetSyncLines(u32 validation_tick)
{
	s32 sl;

	if (syNetFighterPhaseGetTraceLevel() < 1)
	{
		return;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	for (sl = 0; sl < SYNETFIGHTER_PHASE_SLOTS; sl++)
	{
		const SYNetFighterPhaseSlotTrace *t = &s_slot[sl];

		if (t->valid == 0U)
		{
			continue;
		}
		if (t->pkind != (u8)nFTPlayerKindMan)
		{
			continue;
		}
		port_log(
		    "SSB64 NetSync: ft_phase tick=%u vtick=%u slot=%u ord=%u ctrl=0x%08X hist=0x%08X plB=0x%08X "
		    "stA=0x%08X stB=0x%08X stC=0x%08X hmatch=%d ab=%u%u%u\n",
		    t->tick,
		    validation_tick,
		    (unsigned int)t->slot,
		    t->exec_order,
		    t->ctrl_hash_before,
		    t->hist_hash,
		    t->pl_hash_after,
		    t->st_hash_phase_a,
		    t->st_hash_phase_b,
		    t->st_hash_phase_c,
		    (t->hist_hash != 0U && (t->ctrl_hash_before == t->hist_hash)) ? 1 : 0,
		    (unsigned int)t->phase_a_done,
		    (unsigned int)t->phase_b_done,
		    (unsigned int)t->phase_c_done);
	}
}
