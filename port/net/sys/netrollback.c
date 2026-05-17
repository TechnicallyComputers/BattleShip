#include <sys/netrollback.h>

#include <sys/netinput.h>
#include <sys/netpeer.h>
#include <sys/netrollbacksnapshot.h>
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
#include <stdlib.h>
#include <sys/netdesyncclassifier.h>

extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);
#endif

extern void scVSBattleFuncUpdate(void);

/*
 * Rollback control: input mismatch detection + resim pacing.
 * Typed world snapshots live in netrollbacksnapshot.c (ring sized by
 * SSB64_NETPLAY_ROLLBACK_SNAPSHOT_FRAMES, independent of input history).
 */
static sb32 sSYNetRollbackModuleEnabled;
static sb32 sSYNetRollbackSessionActive;
static u32 sSYNetRollbackResimDepth;
static u32 sSYNetRollbackRollbackCount;
static sb32 sSYNetRollbackMismatchRemoteWithoutPublished;
#ifdef PORT
static u32 sSYNetRollbackInjectTick;
static sb32 sSYNetRollbackInjectConsumed;
static u32 sSYNetRollbackLastVerifyHash;
static sb32 sSYNetRollbackForceMismatch;
static u32 sSYNetRollbackForceMismatchPendingTick;
static sb32 sSYNetRollbackMismatchDebug;
static sb32 sSYNetRollbackVerifyStrict;
static sb32 sSYNetRollbackLoadHashVerify;
static u32 sSYNetRollbackLoadFailCount;
static u32 sMismatchAsymLogsRemaining;
static s32 sSYNetRollbackForceMismatchPlayerSlot;
static u32 sSYNetRollbackResimTicksPerFrame;
static sb32 sSYNetRollbackResimPending;
static u32 sSYNetRollbackResimMismatchTick;
static u32 sSYNetRollbackResimTargetTick;
static u32 sSYNetRollbackResimNextTick;
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
	sSYNetRollbackLoadHashVerify = TRUE;
	{
		const char *env_lh;

		env_lh = getenv("SSB64_NETPLAY_ROLLBACK_LOAD_HASH_VERIFY");
		if ((env_lh != NULL) && (env_lh[0] != '\0') && (atoi(env_lh) == 0))
		{
			sSYNetRollbackLoadHashVerify = FALSE;
		}
	}
	sSYNetRollbackMismatchRemoteWithoutPublished = FALSE;
	{
		const char *env_rp;

		env_rp = getenv("SSB64_NETPLAY_ROLLBACK_MISMATCH_REMOTE_WITHOUT_PUBLISHED");
		if ((env_rp != NULL) && (env_rp[0] != '\0') && (atoi(env_rp) != 0))
		{
			sSYNetRollbackMismatchRemoteWithoutPublished = TRUE;
		}
	}
	sSYNetRollbackLoadFailCount = 0;
	sMismatchAsymLogsRemaining = 16;
	sSYNetRollbackForceMismatchPlayerSlot = -1;
	sSYNetRollbackResimTicksPerFrame = 4U;
	{
		const char *env_rt;

		env_rt = getenv("SSB64_NETPLAY_ROLLBACK_RESIM_TICKS_PER_FRAME");
		if ((env_rt != NULL) && (env_rt[0] != '\0'))
		{
			s32 v = atoi(env_rt);

			if (v >= 1)
			{
				sSYNetRollbackResimTicksPerFrame = (u32)v;
			}
		}
		if (sSYNetRollbackResimTicksPerFrame > 32U)
		{
			sSYNetRollbackResimTicksPerFrame = 32U;
		}
	}
	sSYNetRollbackResimPending = FALSE;
	sSYNetRollbackResimMismatchTick = ~(u32)0;
	sSYNetRollbackResimTargetTick = ~(u32)0;
	sSYNetRollbackResimNextTick = ~(u32)0;
	syNetRbSnapshotInit();
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
	sSYNetRollbackMismatchRemoteWithoutPublished = FALSE;
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
#ifdef PORT
	if (sSYNetRollbackResimPending != FALSE)
	{
		return TRUE;
	}
#endif
	return sSYNetRollbackResimDepth != 0;
}

void syNetRollbackStartVSSession(void)
{
	if (sSYNetRollbackModuleEnabled == FALSE)
	{
		return;
	}
	syNetRbSnapshotResetSession();
	sSYNetRollbackSessionActive = TRUE;
	sSYNetRollbackResimDepth = 0;
	sSYNetRollbackRollbackCount = 0;
#ifdef PORT
	sSYNetRollbackInjectConsumed = FALSE;
	sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
	sSYNetRollbackLoadFailCount = 0;
	sMismatchAsymLogsRemaining = 16;
	sSYNetRollbackResimPending = FALSE;
#endif
}

void syNetRollbackStopVSSession(void)
{
	sSYNetRollbackSessionActive = FALSE;
	sSYNetRollbackResimDepth = 0;
#ifdef PORT
	sSYNetRollbackResimPending = FALSE;
#endif
}

static sb32 syNetRollbackSavePostTick(u32 tick)
{
	if (syNetRollbackIsActive() == FALSE)
	{
		return FALSE;
	}
	return syNetRbSnapshotSave(tick);
}

static void syNetRollbackVerifyLoadedSlot(u32 tick)
{
#ifdef PORT
	u32 live_f;
	u32 live_w;
	u32 live_i;
	u32 live_wp;
	u32 live_m;
	u32 live_r;
	u32 live_c;
	u32 live_a;

	if (sSYNetRollbackLoadHashVerify == FALSE)
	{
		return;
	}
	live_f = syNetSyncHashBattleFightersFull();
	live_w = syNetSyncHashRollbackWorld();
	live_i = syNetSyncHashActiveItems();
	live_wp = syNetSyncHashActiveWeapons();
	live_m = syNetSyncHashMapCollisionKinematics();
	live_r = syNetSyncHashRNGSeed();
	live_c = syNetSyncHashGMCamera();
	live_a = syNetSyncHashFighterAnimationState();
	if ((live_f != syNetRbSnapshotGetSlotHashFighter(tick)) || (live_w != syNetRbSnapshotGetSlotHashWorld(tick)) ||
	    (live_i != syNetRbSnapshotGetSlotHashItem(tick)) || (live_wp != syNetRbSnapshotGetSlotHashWeapon(tick)) ||
	    (live_m != syNetRbSnapshotGetSlotHashMap(tick)) || (live_r != syNetRbSnapshotGetSlotHashRng(tick)) ||
	    (live_c != syNetRbSnapshotGetSlotHashCamera(tick)) || (live_a != syNetRbSnapshotGetSlotHashAnimation(tick)))
	{
		port_log(
		    "SSB64 NetRollback: LOAD_HASH_DRIFT tick=%u figh=0x%08X/0x%08X world=0x%08X/0x%08X item=0x%08X/0x%08X "
		    "wpn=0x%08X/0x%08X map=0x%08X/0x%08X rng=0x%08X/0x%08X cam=0x%08X/0x%08X anim=0x%08X/0x%08X\n",
		    tick,
		    syNetRbSnapshotGetSlotHashFighter(tick),
		    live_f,
		    syNetRbSnapshotGetSlotHashWorld(tick),
		    live_w,
		    syNetRbSnapshotGetSlotHashItem(tick),
		    live_i,
		    syNetRbSnapshotGetSlotHashWeapon(tick),
		    live_wp,
		    syNetRbSnapshotGetSlotHashMap(tick),
		    live_m,
		    syNetRbSnapshotGetSlotHashRng(tick),
		    live_r,
		    syNetRbSnapshotGetSlotHashCamera(tick),
		    live_c,
		    syNetRbSnapshotGetSlotHashAnimation(tick),
		    live_a);
		syNetDesyncClassifierOnLoadHashDrift(tick);
	}
#else
	(void)tick;
#endif
}

static sb32 syNetRollbackLoadPostTick(u32 tick)
{
	if (syNetRbSnapshotLoad(tick) == FALSE)
	{
		return FALSE;
	}
	syNetRollbackVerifyLoadedSlot(tick);
	return TRUE;
}

#ifdef PORT
sb32 syNetRollbackLoadSnapshotAfterCompletedTick(u32 completed_sim_tick)
{
	return syNetRollbackLoadPostTick(completed_sim_tick);
}
#endif

/* Once per frame after battle sim: persist the world that finished `syNetInputGetTick()` (completed sim index). */
void syNetRollbackAfterBattleUpdate(void)
{
	u32 completed_tick;

	if (syNetRollbackIsActive() == FALSE)
	{
		return;
	}
	completed_tick = syNetInputGetTick();
	syNetRollbackSavePostTick(completed_tick);
}

/*
 * Input mismatch search uses **netinput sim tick** indices only (`syNetInputGetHistoryFrame` / `GetRemoteHistoryFrame`),
 * not taskman update/frame counts. `frontier_tick` is exclusive: `syNetInputGetTick() + 1` after the latest completed sim step
 * (`syNetRollbackUpdate`), so tick `frontier_tick - 1` is the newest completed index included in the scan.
 *
 * Compares full `SYNetInputFrame` fields (tick, buttons, sticks, source, predicted, valid) when both rows exist, and
 * treats **remote ring present without published history** as a mismatch (wire ahead of local commit).
 */
/* TRUE when both rows exist and disagree on any committed field (aligned with NetInput desync diag value checks). */
static sb32 syNetRollbackHistRemoteValueMismatch(const SYNetInputFrame *hist, const SYNetInputFrame *remote)
{
	if ((hist->tick != remote->tick) || (hist->buttons != remote->buttons) || (hist->stick_x != remote->stick_x) ||
	    (hist->stick_y != remote->stick_y) || (hist->source != remote->source) ||
	    (hist->is_predicted != remote->is_predicted) || (hist->is_valid != remote->is_valid))
	{
		return TRUE;
	}
	return FALSE;
}

/* Walk backward from `frontier_tick` up to `SYNETROLLBACK_SCAN_WINDOW` comparing published vs remote history. */
static u32 syNetRollbackFindEarliestInputMismatch(u32 frontier_tick, s32 *out_mismatch_player)
{
	SYNetInputFrame hist;
	SYNetInputFrame remote;
	u32 begin;
	u32 t;
	s32 ri;
	s32 remote_player;

	if (out_mismatch_player != NULL)
	{
		*out_mismatch_player = -1;
	}
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
				if ((has_remote != FALSE) && (sSYNetRollbackMismatchRemoteWithoutPublished != FALSE))
				{
					if (out_mismatch_player != NULL)
					{
						*out_mismatch_player = remote_player;
					}
					return t;
				}
				continue;
			}
			if (has_remote == FALSE)
			{
				continue;
			}
			if (syNetRollbackHistRemoteValueMismatch(&hist, &remote) != FALSE)
			{
				if (out_mismatch_player != NULL)
				{
					*out_mismatch_player = remote_player;
				}
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
	if (syNetRollbackHistRemoteValueMismatch(&hist, &remote) != FALSE)
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

static sb32 syNetRollbackBeginResim(u32 mismatch_tick, u32 target_tick)
{
	if ((mismatch_tick >= target_tick) || (mismatch_tick == 0))
	{
		return FALSE;
	}
	if (syNetRollbackLoadPostTick(mismatch_tick - 1) == FALSE)
	{
#ifdef PORT
		port_log(
		    "SSB64 NetRollback: load post tick %u failed (need earlier snapshots; ring=%u scan=%u)\n",
		    mismatch_tick - 1,
		    (unsigned int)syNetRbSnapshotRingCapacity(),
		    (unsigned int)SYNETROLLBACK_SCAN_WINDOW);
		sSYNetRollbackLoadFailCount++;
#endif
		return FALSE;
	}
	syNetInputRollbackPrepareForResim(mismatch_tick);
#ifdef PORT
	port_log(
	    "SSB64 NetRollback: resim begin mismatch_tick=%u target_tick=%u span=%u budget=%u/frame\n",
	    mismatch_tick,
	    target_tick,
	    (unsigned int)(target_tick - mismatch_tick),
	    (unsigned int)sSYNetRollbackResimTicksPerFrame);
	sSYNetRollbackResimPending = TRUE;
	sSYNetRollbackResimMismatchTick = mismatch_tick;
	sSYNetRollbackResimTargetTick = target_tick;
	sSYNetRollbackResimNextTick = mismatch_tick;
	sSYNetRollbackResimDepth = 1U;
#endif
	return TRUE;
}

static void syNetRollbackAdvanceResimBudget(void)
{
#ifdef PORT
	u32 ran;
	u32 t;

	if (sSYNetRollbackResimPending == FALSE)
	{
		return;
	}
	ran = 0;
	t = sSYNetRollbackResimNextTick;
	while ((t < sSYNetRollbackResimTargetTick) && (ran < sSYNetRollbackResimTicksPerFrame))
	{
		syNetInputSetTick(t);
		syNetInputFuncRead();
		scVSBattleFuncUpdate();
		{
			char *rt = getenv("SSB64_NETPLAY_RESIM_TICK_TRACE");

			if ((rt != NULL) && (rt[0] != '\0') && (atoi(rt) != 0))
			{
				port_log(
				    "SSB64 NetRollback: resim_tick t=%u figh=0x%08X mph=0x%08X\n",
				    t,
				    syNetSyncHashBattleFightersFull(),
				    syNetSyncHashMapCollisionKinematics());
			}
		}
		t++;
		ran++;
	}
	sSYNetRollbackResimNextTick = t;
	if (t >= sSYNetRollbackResimTargetTick)
	{
		sSYNetRollbackResimPending = FALSE;
		sSYNetRollbackResimDepth = 0;
	}
#else
	(void)0;
#endif
}

/* Transport-time hook: if inputs diverge, resim (nested calls short-circuit via `IsResimulating`). */
void syNetRollbackUpdate(void)
{
	u32 frontier;
	u32 mismatch;
	u32 hash_after;
#ifdef PORT
	u32 hash_pre;
	SYNetInputFrame mismatch_hist;
	SYNetInputFrame mismatch_remote;
	s32 mismatch_player;
#endif

	if (syNetRollbackIsActive() == FALSE)
	{
		return;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
#ifdef PORT
	if (sSYNetRollbackResimPending != FALSE)
	{
		syNetRollbackAdvanceResimBudget();
		return;
	}
#endif
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
#ifdef PORT
	if (syNetTickCommitAllowsBattleSimFromLastFuncReadEvaluate() == FALSE)
	{
		return;
	}
#else
	if (syNetPeerCheckBattleExecutionReady() == FALSE)
	{
		return;
	}
#endif
#ifdef PORT
	syNetRollbackDebugTryApplyPendingForceMismatch();
#endif
	frontier = syNetInputGetTick();
	if (frontier < ~(u32)0)
	{
		frontier++;
	}
#ifdef PORT
	mismatch_player = -1;
	mismatch = syNetRollbackFindEarliestInputMismatch(frontier, &mismatch_player);
#else
	mismatch = syNetRollbackFindEarliestInputMismatch(frontier, NULL);
#endif

	if (mismatch == ~(u32)0)
	{
#ifdef PORT
		{
			char *sd = getenv("SSB64_NETPLAY_ROLLBACK_SCAN_DIAG");

			if ((sd != NULL) && (sd[0] != '\0') && (atoi(sd) != 0))
			{
				static u32 sLastRollbackCleanLogFrontier = ~(u32)0;
				u32 fr = frontier;

				if ((sLastRollbackCleanLogFrontier == ~(u32)0) || (fr >= sLastRollbackCleanLogFrontier + 120U))
				{
					port_log(
					    "SSB64 NetRollback: input scan clean frontier=%u (no mismatch in rollback scan window)\n",
					    fr);
					sLastRollbackCleanLogFrontier = fr;
				}
			}
		}
#endif
		return;
	}
#ifdef PORT
	hash_pre = syNetSyncHashBattleFightersFull();
	port_log(
	    "SSB64 NetRollback: input mismatch at tick %u frontier=%u sim_slot=%d rollbacks=%u\n",
	    mismatch,
	    frontier,
	    (int)mismatch_player,
	    sSYNetRollbackRollbackCount + 1);
	syNetDesyncClassifierOnRollbackInputMismatch(mismatch);
	if ((mismatch_player >= 0) && (mismatch_player < MAXCONTROLLERS))
	{
		if ((syNetInputGetHistoryFrame(mismatch_player, mismatch, &mismatch_hist) != FALSE) &&
		    (syNetInputGetRemoteHistoryFrame(mismatch_player, mismatch, &mismatch_remote) != FALSE))
		{
			port_log(
			    "SSB64 NetRollback: INPUT_MISMATCH_DETAIL tick=%u slot=%d hist t=%u btn=0x%04X sx=%d sy=%d src=%u "
			    "pred=%u valid=%u | ring t=%u btn=0x%04X sx=%d sy=%d src=%u pred=%u valid=%u\n",
			    mismatch,
			    (int)mismatch_player,
			    (unsigned int)mismatch_hist.tick,
			    (unsigned int)mismatch_hist.buttons,
			    mismatch_hist.stick_x,
			    mismatch_hist.stick_y,
			    (unsigned int)mismatch_hist.source,
			    (unsigned int)mismatch_hist.is_predicted,
			    (unsigned int)mismatch_hist.is_valid,
			    (unsigned int)mismatch_remote.tick,
			    (unsigned int)mismatch_remote.buttons,
			    mismatch_remote.stick_x,
			    mismatch_remote.stick_y,
			    (unsigned int)mismatch_remote.source,
			    (unsigned int)mismatch_remote.is_predicted,
			    (unsigned int)mismatch_remote.is_valid);
		}
		else
		{
			port_log(
			    "SSB64 NetRollback: INPUT_MISMATCH_DETAIL tick=%u slot=%d (missing hist or ring row for detail)\n",
			    mismatch,
			    (int)mismatch_player);
		}
	}
		if ((syNetInputGetAbortOnInputMismatchMask() & 2) != 0)
		{
			port_log(
			    "SSB64 NetRollback: ABORT_ON_INPUT_MISMATCH (bit2) before resim tick=%u frontier=%u slot=%d — %s\n",
			    mismatch,
			    frontier,
			    (int)mismatch_player,
			    (syNetInputGetAbortOnInputMismatchFatal() != FALSE)
			        ? "hard-abort (SSB64_NETPLAY_ABORT_ON_INPUT_MISMATCH_FATAL)"
			        : "soft (unset mask or set SSB64_NETPLAY_ABORT_ON_INPUT_MISMATCH_FATAL=1 to abort)");
			if (syNetPeerShouldHardAbortOnNetplayInputMismatch() != FALSE)
			{
				abort();
			}
			else if (syNetInputGetAbortOnInputMismatchFatal() != FALSE)
			{
				port_log(
				    "SSB64 NetRollback: ABORT_ON_INPUT_MISMATCH_FATAL skipped (sync pipeline phase=%d; hard-abort only "
				    "in Running)\n",
				    (int)syNetPeerGetSyncPipelinePhase());
			}
		}
#endif
	if (syNetRollbackBeginResim(mismatch, frontier) == FALSE)
	{
		return;
	}
	sSYNetRollbackRollbackCount++;
	syNetRollbackAdvanceResimBudget();
	if (sSYNetRollbackResimPending != FALSE)
	{
		return;
	}

	hash_after = syNetSyncHashBattleFightersFull();
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
#ifdef PORT
		syNetDesyncClassifierOnVerifyStrictUnchanged(mismatch);
#endif
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
