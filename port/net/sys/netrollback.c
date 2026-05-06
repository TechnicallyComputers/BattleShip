#include <sys/netrollback.h>

#include <sys/netinput.h>
#include <sys/netpeer.h>
#include <sys/netsync.h>
#include <sys/objdef.h>
#include <sys/objman.h>
#include <sys/taskman.h>

#include <ft/fighter.h>
#include <ft/ftdef.h>
#include <gm/gmdef.h>
#include <mp/map.h>
#include <sys/controller.h>

/* May already be declared in taskman.h for PORT builds; duplicated here so parsers that see a
 * taskman.h variant without PORT-gated prototypes still diagnose calls under #ifdef PORT correctly. */
extern void syTaskmanSetIntervals(u16 update, u16 framedraw);

#ifdef PORT
extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);
#endif

extern void scVSBattleFuncUpdate(void);

/*
 * Ring storage: one `SYNetRollbackRingSlot` per `tick % SYNETROLLBACK_RING_LENGTH` (same span as input history).
 * Save runs **after** sim completes tick T-1 (see `AfterBattleUpdate`); load restores fighters + a subset of map
 * yakumono state so resim can diverge from an exact prior world point.
 */

#define SYNETROLLBACK_RING_LENGTH SYNETINPUT_HISTORY_LENGTH
#define SYNETROLLBACK_MAX_MP_YAKU 32

typedef struct SYNetRollbackYakuBlob
{
	Vec3f translate;
	Vec3f speed;
	s32 user_data_s;

} SYNetRollbackYakuBlob;

typedef struct SYNetRollbackFighterBlob
{
	sb32 is_valid;
	s32 player;
	s32 fkind;
	s32 status_id;
	s32 motion_id;
	s32 percent_damage;
	s32 stock_count;
	s32 lr;
	sb32 ga;
	Vec3f vel_air;
	Vec3f vel_ground;
	f32 vel_damage_ground;
	Vec3f pos_prev;
	Vec3f vel_damage_air;
	u32 hitlag_tics;
	Vec3f topn_translate;
	u32 status_total_tics;

} SYNetRollbackFighterBlob;

typedef struct SYNetRollbackRingSlot
{
	u32 tick;
	sb32 is_valid;
	u16 mp_collision_tic;
	s32 mp_yakumono_count;
	sb32 mp_yaku_captured;
	SYNetRollbackYakuBlob mp_yaku[SYNETROLLBACK_MAX_MP_YAKU];
	SYNetRollbackFighterBlob fighters[GMCOMMON_PLAYERS_MAX];

} SYNetRollbackRingSlot;

static SYNetRollbackRingSlot sSYNetRollbackRing[SYNETROLLBACK_RING_LENGTH];
static sb32 sSYNetRollbackModuleEnabled;
static sb32 sSYNetRollbackSessionActive;
static u32 sSYNetRollbackResimDepth;
static u32 sSYNetRollbackRollbackCount;
#ifdef PORT
static u32 sSYNetRollbackInjectTick;
static sb32 sSYNetRollbackInjectConsumed;
static u32 sSYNetRollbackLastVerifyHash;
static sb32 sSYNetRollbackForceMismatch;
static u32 sSYNetRollbackForceMismatchPendingTick;
static sb32 sSYNetRollbackMismatchDebug;
static sb32 sSYNetRollbackVerifyStrict;
static u32 sSYNetRollbackLoadFailCount;
static u32 sMismatchAsymLogsRemaining;
static s32 sSYNetRollbackForceMismatchPlayerSlot;
#endif

void syNetRollbackInit(void)
{
#ifdef PORT
	char *env_roll;
	char *env_inj;
	char *env_fm;
	char *env_md;
	char *env_vs;
	char *env_fmp;
	s32 fmp;

	sSYNetRollbackInjectTick = ~(u32)0;
	sSYNetRollbackInjectConsumed = FALSE;
	env_roll = getenv("SSB64_NETPLAY_ROLLBACK");
	sSYNetRollbackModuleEnabled = TRUE;
	if ((env_roll != NULL) && (atoi(env_roll) == 0))
	{
		sSYNetRollbackModuleEnabled = FALSE;
	}
	env_inj = getenv("SSB64_NETPLAY_ROLLBACK_INJECT_TICK");
	if ((env_inj != NULL) && (sSYNetRollbackModuleEnabled != FALSE))
	{
		s32 v = atoi(env_inj);

		if (v >= 0)
		{
			sSYNetRollbackInjectTick = (u32)v;
			port_log("SSB64 NetRollback: debug inject tamper at remote tick %u\n", sSYNetRollbackInjectTick);
		}
	}
	sSYNetRollbackForceMismatch = FALSE;
	sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
	env_fm = getenv("SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH");
	if ((env_fm != NULL) && (atoi(env_fm) != 0))
	{
		sSYNetRollbackForceMismatch = TRUE;
		if ((sSYNetRollbackInjectTick != ~(u32)0) && (sSYNetRollbackModuleEnabled != FALSE))
		{
			port_log(
			    "SSB64 NetRollback: FORCE_MISMATCH on wire tick %u (after staging: XOR published history if it equals remote)\n",
			    sSYNetRollbackInjectTick);
		}
		else if (sSYNetRollbackModuleEnabled != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: FORCE_MISMATCH set but SSB64_NETPLAY_ROLLBACK_INJECT_TICK missing — no one-shot scheduled\n");
		}
	}
	sSYNetRollbackMismatchDebug = FALSE;
	env_md = getenv("SSB64_NETPLAY_ROLLBACK_MISMATCH_DEBUG");
	if ((env_md != NULL) && (atoi(env_md) != 0))
	{
		sSYNetRollbackMismatchDebug = TRUE;
	}
	sSYNetRollbackVerifyStrict = FALSE;
	env_vs = getenv("SSB64_NETPLAY_ROLLBACK_VERIFY_STRICT");
	if ((env_vs != NULL) && (atoi(env_vs) != 0))
	{
		sSYNetRollbackVerifyStrict = TRUE;
	}
	sSYNetRollbackLoadFailCount = 0;
	sMismatchAsymLogsRemaining = 16;
	sSYNetRollbackForceMismatchPlayerSlot = -1;
	env_fmp = getenv("SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH_PLAYER");
	if ((env_fmp != NULL) && (env_fmp[0] != '\0'))
	{
		fmp = atoi(env_fmp);
		if ((fmp >= 0) && (fmp < MAXCONTROLLERS))
		{
			sSYNetRollbackForceMismatchPlayerSlot = fmp;
			port_log("SSB64 NetRollback: FORCE_MISMATCH player slot override=%d\n", fmp);
		}
	}
#else
	sSYNetRollbackModuleEnabled = FALSE;
#endif
	sSYNetRollbackSessionActive = FALSE;
	sSYNetRollbackResimDepth = 0;
	sSYNetRollbackRollbackCount = 0;
#ifdef PORT
	sSYNetRollbackLastVerifyHash = 0;
#endif
}

sb32 syNetRollbackIsActive(void)
{
	return (sSYNetRollbackModuleEnabled != FALSE) && (sSYNetRollbackSessionActive != FALSE);
}

sb32 syNetRollbackIsResimulating(void)
{
	return sSYNetRollbackResimDepth != 0;
}

void syNetRollbackStartVSSession(void)
{
	u32 i;

	if (sSYNetRollbackModuleEnabled == FALSE)
	{
		return;
	}
	for (i = 0; i < SYNETROLLBACK_RING_LENGTH; i++)
	{
		sSYNetRollbackRing[i].is_valid = FALSE;
		sSYNetRollbackRing[i].tick = ~(u32)0;
	}
	sSYNetRollbackSessionActive = TRUE;
	sSYNetRollbackResimDepth = 0;
	sSYNetRollbackRollbackCount = 0;
#ifdef PORT
	sSYNetRollbackInjectConsumed = FALSE;
	sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
	sSYNetRollbackLoadFailCount = 0;
	sMismatchAsymLogsRemaining = 16;
#endif
}

void syNetRollbackStopVSSession(void)
{
	sSYNetRollbackSessionActive = FALSE;
	sSYNetRollbackResimDepth = 0;
}

static SYNetRollbackRingSlot *syNetRollbackRingSlotForTick(u32 tick)
{
	return &sSYNetRollbackRing[tick % SYNETROLLBACK_RING_LENGTH];
}

/* Serialize live fighter fields we need to rebuild motion/damage continuity after a rewind. */
static void syNetRollbackCaptureFighters(SYNetRollbackRingSlot *slot)
{
	GObj *fighter_gobj;
	s32 si;

	for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
	{
		slot->fighters[si].is_valid = FALSE;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		s32 slot_index;

		fp = ftGetStruct(fighter_gobj);
		slot_index = fp->player;

		if ((slot_index >= 0) && (slot_index < GMCOMMON_PLAYERS_MAX))
		{
			SYNetRollbackFighterBlob *blob = &slot->fighters[slot_index];

			blob->is_valid = TRUE;
			blob->player = fp->player;
			blob->fkind = fp->fkind;
			blob->status_id = fp->status_id;
			blob->motion_id = fp->motion_id;
			blob->percent_damage = fp->percent_damage;
			blob->stock_count = fp->stock_count;
			blob->lr = fp->lr;
			blob->ga = fp->ga;
			blob->vel_air = fp->physics.vel_air;
			blob->vel_ground = fp->physics.vel_ground;
			blob->vel_damage_ground = fp->physics.vel_damage_ground;
			blob->pos_prev = fp->coll_data.pos_prev;
			blob->vel_damage_air = fp->physics.vel_damage_air;
			blob->hitlag_tics = fp->hitlag_tics;
			blob->status_total_tics = fp->status_total_tics;
			if (fp->joints[nFTPartsJointTopN] != NULL)
			{
				blob->topn_translate = fp->joints[nFTPartsJointTopN]->translate.vec.f;
			}
			else
			{
				blob->topn_translate.x = blob->topn_translate.y = blob->topn_translate.z = 0.0F;
			}
		}
	}
}

/* Overwrite currently loaded fighters with blob data; skips entries whose kind/slot no longer match (stale pointer guard). */
static void syNetRollbackApplyFighters(const SYNetRollbackRingSlot *slot)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		s32 slot_index;
		const SYNetRollbackFighterBlob *blob;

		fp = ftGetStruct(fighter_gobj);
		slot_index = fp->player;

		if ((slot_index < 0) || (slot_index >= GMCOMMON_PLAYERS_MAX))
		{
			continue;
		}
		blob = &slot->fighters[slot_index];

		if ((blob->is_valid == FALSE) || (blob->player != fp->player) || (blob->fkind != fp->fkind))
		{
			continue;
		}
		fp->status_id = blob->status_id;
		fp->motion_id = blob->motion_id;
		fp->percent_damage = blob->percent_damage;
		fp->stock_count = blob->stock_count;
		fp->lr = blob->lr;
		fp->ga = blob->ga;
		fp->physics.vel_air = blob->vel_air;
		fp->physics.vel_ground = blob->vel_ground;
		fp->physics.vel_damage_ground = blob->vel_damage_ground;
		fp->coll_data.pos_prev = blob->pos_prev;
		fp->physics.vel_damage_air = blob->vel_damage_air;
		fp->hitlag_tics = blob->hitlag_tics;
		fp->status_total_tics = blob->status_total_tics;
		if (fp->joints[nFTPartsJointTopN] != NULL)
		{
			fp->joints[nFTPartsJointTopN]->translate.vec.f = blob->topn_translate;
		}
	}
}

static void syNetRollbackCaptureMap(SYNetRollbackRingSlot *slot)
{
	s32 i;
	s32 n;
	s32 cap;
	DObj *dobj;

	slot->mp_yaku_captured = FALSE;
	slot->mp_collision_tic = 0;
	slot->mp_yakumono_count = 0;
	if ((gMPCollisionYakumonoDObjs == NULL) || (gMPCollisionSpeeds == NULL))
	{
		return;
	}
	n = gMPCollisionYakumonosNum;
	if (n < 0)
	{
		n = 0;
	}
	cap = (n > SYNETROLLBACK_MAX_MP_YAKU) ? SYNETROLLBACK_MAX_MP_YAKU : n;
	slot->mp_collision_tic = gMPCollisionUpdateTic;
	slot->mp_yakumono_count = cap;
	for (i = 0; i < cap; i++)
	{
		dobj = gMPCollisionYakumonoDObjs->dobjs[i];
		if (dobj == NULL)
		{
			slot->mp_yaku[i].translate.x = slot->mp_yaku[i].translate.y = slot->mp_yaku[i].translate.z = 0.0F;
			slot->mp_yaku[i].speed.x = slot->mp_yaku[i].speed.y = slot->mp_yaku[i].speed.z = 0.0F;
			slot->mp_yaku[i].user_data_s = 0;
			continue;
		}
		slot->mp_yaku[i].translate = dobj->translate.vec.f;
		slot->mp_yaku[i].speed = gMPCollisionSpeeds[i];
		slot->mp_yaku[i].user_data_s = dobj->user_data.s;
	}
	slot->mp_yaku_captured = TRUE;
}

static void syNetRollbackApplyMap(const SYNetRollbackRingSlot *slot)
{
	s32 i;
	s32 cap;
	s32 live_n;
	DObj *dobj;

	if (slot->mp_yaku_captured == FALSE)
	{
		return;
	}
	gMPCollisionUpdateTic = slot->mp_collision_tic;
	if ((gMPCollisionYakumonoDObjs == NULL) || (gMPCollisionSpeeds == NULL))
	{
		return;
	}
	live_n = gMPCollisionYakumonosNum;
	if (live_n < 0)
	{
		live_n = 0;
	}
	cap = slot->mp_yakumono_count;
	if (cap > live_n)
	{
		cap = live_n;
	}
	if (cap > SYNETROLLBACK_MAX_MP_YAKU)
	{
		cap = SYNETROLLBACK_MAX_MP_YAKU;
	}
	for (i = 0; i < cap; i++)
	{
		dobj = gMPCollisionYakumonoDObjs->dobjs[i];
		if (dobj == NULL)
		{
			continue;
		}
		dobj->translate.vec.f = slot->mp_yaku[i].translate;
		dobj->user_data.s = slot->mp_yaku[i].user_data_s;
		gMPCollisionSpeeds[i] = slot->mp_yaku[i].speed;
	}
}

/* Mark ring slot for completed sim tick `tick` and snapshot fighters + yakumono subset. */
static sb32 syNetRollbackSavePostTick(u32 tick)
{
	SYNetRollbackRingSlot *slot;

	if (syNetRollbackIsActive() == FALSE)
	{
		return FALSE;
	}
	slot = syNetRollbackRingSlotForTick(tick);
	slot->tick = tick;
	slot->is_valid = TRUE;
	syNetRollbackCaptureFighters(slot);
	syNetRollbackCaptureMap(slot);
	return TRUE;
}

/* Reconstruct world at end of tick `tick` (used when rewinding to just before mismatch). */
static sb32 syNetRollbackLoadPostTick(u32 tick)
{
	SYNetRollbackRingSlot *slot;

	slot = syNetRollbackRingSlotForTick(tick);

	if ((slot->is_valid == FALSE) || (slot->tick != tick))
	{
		return FALSE;
	}
	syNetRollbackApplyMap(slot);
	syNetRollbackApplyFighters(slot);
	return TRUE;
}

#ifdef PORT
sb32 syNetRollbackLoadSnapshotAfterCompletedTick(u32 completed_sim_tick)
{
	return syNetRollbackLoadPostTick(completed_sim_tick);
}
#endif

/* Once per frame after battle sim: persist the world that finished `(current_input_tick - 1)` while rollback is on. */
void syNetRollbackAfterBattleUpdate(void)
{
	u32 post_tick;

	if (syNetRollbackIsActive() == FALSE)
	{
		return;
	}
	if (syNetPeerCheckBattleExecutionReady() == FALSE)
	{
		return;
	}
	post_tick = syNetInputGetTick();

	/* First VS frame: post_tick==1 => save completed sim state for netinput tick 0. */
	if (post_tick == 0)
	{
		return;
	}
	syNetRollbackSavePostTick(post_tick - 1);
}

/* Walk backward from `frontier_tick` up to `SYNETROLLBACK_SCAN_WINDOW` comparing published vs remote history. */
static u32 syNetRollbackFindEarliestInputMismatch(u32 frontier_tick)
{
	SYNetInputFrame hist;
	SYNetInputFrame remote;
	u32 begin;
	u32 t;
	s32 ri;
	s32 remote_player;

	if (frontier_tick == 0)
	{
		return ~(u32)0;
	}
	begin = 0;
	if (frontier_tick > SYNETROLLBACK_SCAN_WINDOW)
	{
		begin = frontier_tick - SYNETROLLBACK_SCAN_WINDOW;
	}
	for (t = begin; t < frontier_tick; t++)
	{
		for (ri = 0; ri < syNetPeerGetRemoteHumanSlotCount(); ri++)
		{
			sb32 has_hist;
			sb32 has_remote;

			if (syNetPeerGetRemoteHumanSlotByIndex(ri, &remote_player) == FALSE)
			{
				continue;
			}
			if ((remote_player < 0) || (remote_player >= MAXCONTROLLERS))
			{
				continue;
			}
			has_hist = syNetInputGetHistoryFrame(remote_player, t, &hist);
			has_remote = syNetInputGetRemoteHistoryFrame(remote_player, t, &remote);
#ifdef PORT
			if ((sSYNetRollbackMismatchDebug != FALSE) && (has_hist != has_remote))
			{
				if (sMismatchAsymLogsRemaining > 0U)
				{
					port_log(
					    "SSB64 NetRollback: MISMATCH_DEBUG asym tick=%u slot=%d hist=%d remote=%d frontier=%u\n",
					    t,
					    remote_player,
					    (has_hist != FALSE) ? 1 : 0,
					    (has_remote != FALSE) ? 1 : 0,
					    frontier_tick);
					sMismatchAsymLogsRemaining--;
				}
			}
#endif
			if (has_hist == FALSE)
			{
				continue;
			}
			if (has_remote == FALSE)
			{
				continue;
			}
			if ((hist.buttons != remote.buttons) || (hist.stick_x != remote.stick_x) || (hist.stick_y != remote.stick_y))
			{
				return t;
			}
		}
	}
	return ~(u32)0;
}

#ifdef PORT
static s32 syNetRollbackResolveForceMismatchTargetPlayer(void)
{
	s32 want;
	s32 i;
	s32 slot;

	want = sSYNetRollbackForceMismatchPlayerSlot;
	if (want >= 0)
	{
		for (i = 0; i < syNetPeerGetRemoteHumanSlotCount(); i++)
		{
			if ((syNetPeerGetRemoteHumanSlotByIndex(i, &slot) != FALSE) && (slot == want))
			{
				return want;
			}
		}
		port_log(
		    "SSB64 NetRollback: FORCE_MISMATCH_PLAYER=%d not in remote receive slot list; using first remote slot\n",
		    want);
	}
	if (syNetPeerGetRemoteHumanSlotByIndex(0, &slot) != FALSE)
	{
		return slot;
	}
	return -1;
}

static void syNetRollbackDebugTryApplyPendingForceMismatch(void)
{
	s32 player;
	u32 tick;
	u32 frontier;
	SYNetInputFrame hist;
	SYNetInputFrame remote;

	if (sSYNetRollbackForceMismatch == FALSE)
	{
		return;
	}
	if (sSYNetRollbackForceMismatchPendingTick == ~(u32)0)
	{
		return;
	}
	if (sSYNetRollbackInjectConsumed != FALSE)
	{
		return;
	}
	player = syNetRollbackResolveForceMismatchTargetPlayer();
	if ((player < 0) || (player >= MAXCONTROLLERS))
	{
		return;
	}
	tick = sSYNetRollbackForceMismatchPendingTick;
	frontier = syNetInputGetTick();

	if (frontier <= tick)
	{
		return;
	}
	if (syNetInputGetHistoryFrame(player, tick, &hist) == FALSE)
	{
		if (frontier > tick + (u32)SYNETINPUT_HISTORY_LENGTH)
		{
			port_log(
			    "SSB64 NetRollback: FORCE_MISMATCH gave up: no published history at tick %u (frontier=%u)\n",
			    tick,
			    frontier);
			sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
			sSYNetRollbackInjectConsumed = TRUE;
		}
		return;
	}
	if (syNetInputGetRemoteHistoryFrame(player, tick, &remote) == FALSE)
	{
		port_log("SSB64 NetRollback: FORCE_MISMATCH gave up: no remote history at tick %u\n", tick);
		sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
		sSYNetRollbackInjectConsumed = TRUE;
		return;
	}
	if ((hist.buttons != remote.buttons) || (hist.stick_x != remote.stick_x) || (hist.stick_y != remote.stick_y))
	{
		port_log(
		    "SSB64 NetRollback: FORCE_MISMATCH detected published history already differs from remote at tick %u (no XOR)\n",
		    tick);
		sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
		sSYNetRollbackInjectConsumed = TRUE;
		return;
	}

	port_log(
	    "SSB64 NetRollback: FORCE_MISMATCH detected published history == remote at tick %u; XOR 0x1000 into published history only\n",
	    tick);
	syNetInputDebugXorPublishedHistoryButtons(player, tick, 0x1000);
	sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
	sSYNetRollbackInjectConsumed = TRUE;
}
#endif

/* Load snapshot at `mismatch_tick-1`, reseed netinput, then replay frames until we reach the live `target_tick`. */
static void syNetRollbackRunResim(u32 mismatch_tick, u32 target_tick)
{
	u32 t;

	if ((mismatch_tick >= target_tick) || (mismatch_tick == 0))
	{
		return;
	}
	if (syNetRollbackLoadPostTick(mismatch_tick - 1) == FALSE)
	{
#ifdef PORT
		port_log(
		    "SSB64 NetRollback: load post tick %u failed (need earlier snapshots; check delay/loss vs ring=%u scan=%u)\n",
		    mismatch_tick - 1,
		    (unsigned int)SYNETINPUT_HISTORY_LENGTH,
		    (unsigned int)SYNETROLLBACK_SCAN_WINDOW);
		sSYNetRollbackLoadFailCount++;
#endif
		return;
	}
	syNetInputRollbackPrepareForResim(mismatch_tick);

	sSYNetRollbackResimDepth++;
	for (t = mismatch_tick; t < target_tick; t++)
	{
		syNetInputSetTick(t);
		syNetInputFuncRead();
		scVSBattleFuncUpdate();
	}
	sSYNetRollbackResimDepth--;
}

/* Transport-time hook: if inputs diverge, resim (nested calls short-circuit via `IsResimulating`). */
void syNetRollbackUpdate(void)
{
	u32 frontier;
	u32 mismatch;
	u32 hash_after;
#ifdef PORT
	u32 hash_pre;
#endif

	if (syNetRollbackIsActive() == FALSE)
	{
		return;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	if (syNetPeerCheckBattleExecutionReady() == FALSE)
	{
		return;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
#ifdef PORT
	syNetRollbackDebugTryApplyPendingForceMismatch();
#endif
	frontier = syNetInputGetTick();
	mismatch = syNetRollbackFindEarliestInputMismatch(frontier);

	if (mismatch == ~(u32)0)
	{
		return;
	}
#ifdef PORT
	hash_pre = syNetSyncHashBattleFighters();
	port_log("SSB64 NetRollback: input mismatch at tick %u frontier=%u rollbacks=%u\n", mismatch, frontier,
	         sSYNetRollbackRollbackCount + 1);
#endif
	syNetRollbackRunResim(mismatch, frontier);
	sSYNetRollbackRollbackCount++;

	hash_after = syNetSyncHashBattleFighters();
#ifdef PORT
	port_log(
	    "SSB64 NetRollback: resim complete figh=0x%08X (pre_resim=0x%08X) mismatch_tick=%u rollbacks=%u\n",
	    hash_after,
	    hash_pre,
	    mismatch,
	    sSYNetRollbackRollbackCount);
	if ((sSYNetRollbackVerifyStrict != FALSE) && (hash_after == hash_pre))
	{
		port_log(
		    "SSB64 NetRollback: VERIFY_STRICT warning: figh unchanged after resim (mismatch_tick=%u frontier=%u)\n",
		    mismatch,
		    frontier);
	}
	if (sSYNetRollbackLastVerifyHash != 0)
	{
		port_log("SSB64 NetRollback: verify delta vs prior rollback figh ref=0x%08X\n", sSYNetRollbackLastVerifyHash);
	}
	sSYNetRollbackLastVerifyHash = hash_after;
#else
	(void)hash_after;
#endif
}

#ifdef PORT
u32 syNetRollbackGetAppliedResimCount(void)
{
	return sSYNetRollbackRollbackCount;
}

u32 syNetRollbackGetLoadFailCount(void)
{
	return sSYNetRollbackLoadFailCount;
}
#endif

void syNetRollbackDebugOnIncomingRemoteFrame(u32 *tick, u16 *buttons, s8 *stick_x, s8 *stick_y)
{
#ifdef PORT
	if (sSYNetRollbackInjectTick == ~(u32)0)
	{
		return;
	}
	if (sSYNetRollbackInjectConsumed != FALSE)
	{
		return;
	}
	if (*tick != sSYNetRollbackInjectTick)
	{
		return;
	}

	if (sSYNetRollbackForceMismatch != FALSE)
	{
		if (sSYNetRollbackForceMismatchPendingTick == *tick)
		{
			return;
		}
		port_log(
		    "SSB64 NetRollback: FORCE_MISMATCH armed at wire tick %u (patch published history after staging if it matches remote)\n",
		    *tick);
		sSYNetRollbackForceMismatchPendingTick = *tick;
		return;
	}

	*buttons ^= 0x1000;
	sSYNetRollbackInjectConsumed = TRUE;
	port_log("SSB64 NetRollback: injected button tamper at tick %u\n", *tick);
#else
	(void)tick;
	(void)buttons;
	(void)stick_x;
	(void)stick_y;
#endif
}

void syNetRollbackApplyPortSimPacing(u32 refresh_hz)
{
#ifdef PORT
	u32 sim_hz;
	char *hz_env;
	u32 K;

	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		syTaskmanSetIntervals(1, 1);
		return;
	}
	sim_hz = 60;
	hz_env = getenv("SSB64_NETPLAY_SIM_HZ");

	if ((hz_env != NULL) && (atoi(hz_env) > 0))
	{
		sim_hz = (u32)atoi(hz_env);
	}
	if (sim_hz < 1)
	{
		sim_hz = 1;
	}
	if (refresh_hz == 0)
	{
		refresh_hz = 60;
	}
	K = (refresh_hz + sim_hz / 2) / sim_hz;

	if (K < 1)
	{
		K = 1;
	}
	if (K > 16)
	{
		K = 16;
	}
	syTaskmanSetIntervals((u16)K, 1);
#else
	(void)refresh_hz;
#endif
}
