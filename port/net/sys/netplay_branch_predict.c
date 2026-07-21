#include <sys/netplay_branch_predict.h>

#if defined(PORT) && defined(SSB64_NETMENU)

#include <string.h>

#include <ft/fighter.h>
#include <sys/netinput.h>
#include <sys/netplay_sim_quantize.h>

extern void port_log(const char *fmt, ...);

typedef struct SYNetplayBranchEvalState
{
	sb32 active;
	sb32 speculative;
	s32 player;
	s32 status_id;
	u32 tick;
	const char *name;
	SYNetplayBranchInputClass cls;
	u8 hist_source;
	sb32 hist_pred;
	u32 snap_size;
	u8 snap[SYNETPLAY_BRANCH_SNAPSHOT_MAX];
} SYNetplayBranchEvalState;

static SYNetplayBranchEvalState sSYNetplayBranchEval;

static const char *syNetplayBranchInputClassName(SYNetplayBranchInputClass cls)
{
	switch (cls)
	{
	case nSYNetplayBranchInputLocal:
		return "local";
	case nSYNetplayBranchInputAuthoritative:
		return "authoritative";
	case nSYNetplayBranchInputPredicted:
		return "predicted";
	case nSYNetplayBranchInputUnknown:
	default:
		return "unknown";
	}
}

static const char *syNetplayBranchSourceName(u8 source)
{
	switch (source)
	{
	case nSYNetInputSourceLocal:
		return "Local";
	case nSYNetInputSourceRemoteConfirmed:
		return "RemoteConfirmed";
	case nSYNetInputSourceRemoteGapFilled:
		return "RemoteGapFilled";
	case nSYNetInputSourceRemotePredicted:
		return "RemotePredicted";
	case nSYNetInputSourceSaved:
		return "Saved";
	default:
		return "Other";
	}
}

static void syNetplayBranchEvalFillMeta(GObj *fighter_gobj, const char *transition_name)
{
	FTStruct *fp;
	SYNetInputFrame hist;

	sSYNetplayBranchEval.active = FALSE;
	sSYNetplayBranchEval.speculative = FALSE;
	sSYNetplayBranchEval.player = -1;
	sSYNetplayBranchEval.status_id = -1;
	sSYNetplayBranchEval.tick = 0U;
	sSYNetplayBranchEval.name = (transition_name != NULL) ? transition_name : "?";
	sSYNetplayBranchEval.cls = nSYNetplayBranchInputLocal;
	sSYNetplayBranchEval.hist_source = 0U;
	sSYNetplayBranchEval.hist_pred = FALSE;
	sSYNetplayBranchEval.snap_size = 0U;

	if ((fighter_gobj == NULL) || (syNetplayRollbackSemanticsActive() == FALSE))
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp == NULL)
	{
		return;
	}

	sSYNetplayBranchEval.active = TRUE;
	sSYNetplayBranchEval.player = fp->player;
	sSYNetplayBranchEval.status_id = fp->status_id;
	sSYNetplayBranchEval.tick = syNetInputGetTick();
	sSYNetplayBranchEval.cls = syNetplayBranchClassifyDrivingInput(fp->player);
	if (syNetInputGetHistoryFrame(fp->player, sSYNetplayBranchEval.tick, &hist) != FALSE)
	{
		sSYNetplayBranchEval.hist_source = hist.source;
		sSYNetplayBranchEval.hist_pred = (hist.is_predicted != FALSE) ? TRUE : FALSE;
	}
	sSYNetplayBranchEval.speculative = (sSYNetplayBranchEval.cls == nSYNetplayBranchInputPredicted) ? TRUE : FALSE;
}

SYNetplayBranchInputClass syNetplayBranchClassifyDrivingInput(s32 player)
{
	SYNetInputFrame hist;
	u32 tick;

	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return nSYNetplayBranchInputLocal;
	}
	if (syNetInputIsRemoteHumanSlot(player) == FALSE)
	{
		return nSYNetplayBranchInputLocal;
	}

	tick = syNetInputGetTick();
	if (syNetInputGetHistoryFrame(player, tick, &hist) == FALSE)
	{
		return nSYNetplayBranchInputUnknown;
	}
	if (hist.is_valid == FALSE)
	{
		return nSYNetplayBranchInputUnknown;
	}
	if ((hist.is_predicted != FALSE) || (hist.source == nSYNetInputSourceRemotePredicted))
	{
		return nSYNetplayBranchInputPredicted;
	}
	return nSYNetplayBranchInputAuthoritative;
}

sb32 syNetplayBranchEvalBegin(GObj *fighter_gobj, const char *transition_name, const void *preimage,
                              u32 size)
{
	syNetplayBranchEvalFillMeta(fighter_gobj, transition_name);

	if ((sSYNetplayBranchEval.speculative != FALSE) && (preimage != NULL) && (size > 0U))
	{
		if (size > SYNETPLAY_BRANCH_SNAPSHOT_MAX)
		{
			size = SYNETPLAY_BRANCH_SNAPSHOT_MAX;
		}
		memcpy(sSYNetplayBranchEval.snap, preimage, (size_t)size);
		sSYNetplayBranchEval.snap_size = size;
	}

	if ((sSYNetplayBranchEval.active != FALSE) && (sSYNetplayBranchEval.speculative != FALSE))
	{
		port_log(
		    "SSB64 BRANCH_PREDICTED_INPUT tick=%u player=%d status=%d transition=%s "
		    "input=%s hist_source=%s hist_pred=%d phase=begin snap_size=%u\n",
		    (unsigned)sSYNetplayBranchEval.tick, (int)sSYNetplayBranchEval.player,
		    (int)sSYNetplayBranchEval.status_id, sSYNetplayBranchEval.name,
		    syNetplayBranchInputClassName(sSYNetplayBranchEval.cls),
		    syNetplayBranchSourceName(sSYNetplayBranchEval.hist_source),
		    (int)sSYNetplayBranchEval.hist_pred, (unsigned)sSYNetplayBranchEval.snap_size);
	}

	return sSYNetplayBranchEval.speculative;
}

sb32 syNetplayBranchEvalResolve(GObj *fighter_gobj, sb32 wants_branch, SYNetplayBranchRestoreFn restore_fn)
{
	sb32 may_commit;
	const char *name;
	u32 tick;
	s32 player;
	s32 status_id;
	SYNetplayBranchInputClass cls;
	u8 hist_source;
	sb32 hist_pred;
	sb32 speculative;
	sb32 active;

	active = sSYNetplayBranchEval.active;
	speculative = sSYNetplayBranchEval.speculative;
	name = sSYNetplayBranchEval.name;
	tick = sSYNetplayBranchEval.tick;
	player = sSYNetplayBranchEval.player;
	status_id = sSYNetplayBranchEval.status_id;
	cls = sSYNetplayBranchEval.cls;
	hist_source = sSYNetplayBranchEval.hist_source;
	hist_pred = sSYNetplayBranchEval.hist_pred;

	if (active == FALSE)
	{
		/*
		 * Begin was a no-op (offline path / null gobj). Preserve vanilla wants_branch;
		 * clear residual state.
		 */
		memset(&sSYNetplayBranchEval, 0, sizeof(sSYNetplayBranchEval));
		return wants_branch;
	}

	if (speculative != FALSE)
	{
		if ((restore_fn != NULL) && (sSYNetplayBranchEval.snap_size > 0U))
		{
			restore_fn(fighter_gobj, sSYNetplayBranchEval.snap, sSYNetplayBranchEval.snap_size);
			port_log(
			    "SSB64 BRANCH_DISCARD_SIDE_EFFECTS tick=%u player=%d status=%d transition=%s "
			    "input=%s hist_source=%s hist_pred=%d snap_size=%u wants_branch=%d\n",
			    (unsigned)tick, (int)player, (int)status_id, name, syNetplayBranchInputClassName(cls),
			    syNetplayBranchSourceName(hist_source), (int)hist_pred,
			    (unsigned)sSYNetplayBranchEval.snap_size, (int)wants_branch);
		}

		port_log(
		    "SSB64 BRANCH_PREDICTED_INPUT tick=%u player=%d status=%d transition=%s "
		    "input=%s hist_source=%s hist_pred=%d wants_branch=%d decision=%s\n",
		    (unsigned)tick, (int)player, (int)status_id, name, syNetplayBranchInputClassName(cls),
		    syNetplayBranchSourceName(hist_source), (int)hist_pred, (int)wants_branch,
		    (wants_branch != FALSE) ? "defer" : "no_branch");

		if (wants_branch != FALSE)
		{
			port_log(
			    "SSB64 BRANCH_DEFERRED tick=%u player=%d status=%d transition=%s "
			    "input=%s hist_source=%s hist_pred=%d decision=defer\n",
			    (unsigned)tick, (int)player, (int)status_id, name, syNetplayBranchInputClassName(cls),
			    syNetplayBranchSourceName(hist_source), (int)hist_pred);
		}

		memset(&sSYNetplayBranchEval, 0, sizeof(sSYNetplayBranchEval));
		return FALSE;
	}

	may_commit = wants_branch;
	if (may_commit != FALSE)
	{
		port_log(
		    "SSB64 BRANCH_COMMITTED tick=%u player=%d status=%d transition=%s "
		    "input=%s hist_source=%s hist_pred=%d decision=commit\n",
		    (unsigned)tick, (int)player, (int)status_id, name, syNetplayBranchInputClassName(cls),
		    syNetplayBranchSourceName(hist_source), (int)hist_pred);
	}
	else if (cls == nSYNetplayBranchInputPredicted)
	{
		/* Unreachable: predicted sets speculative. Kept for clarity. */
	}

	memset(&sSYNetplayBranchEval, 0, sizeof(sSYNetplayBranchEval));
	return may_commit;
}

sb32 syNetplayBranchSensitiveMayCommit(GObj *fighter_gobj, const char *transition_name, sb32 wants_branch)
{
	syNetplayBranchEvalBegin(fighter_gobj, transition_name, NULL, 0U);
	return syNetplayBranchEvalResolve(fighter_gobj, wants_branch, NULL);
}

void syNetplayBranchTurnDashCapture(GObj *fighter_gobj, SYNetplayBranchTurnDashSnap *snap)
{
	FTStruct *fp;
	ftCommonTurnStatusVars *turn;

	if (snap == NULL)
	{
		return;
	}
	snap->lr_dash = 0;
	snap->attacks4_buffer = 0;
	snap->entry_lr_dash = 0;
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp == NULL)
	{
		return;
	}
	turn = ftStatusVarsTurn(fp);
	if (turn != NULL)
	{
		snap->lr_dash = turn->lr_dash;
		snap->attacks4_buffer = turn->attacks4_buffer;
	}
	snap->entry_lr_dash = syNetplayTurnGetEntryLrDash(fp);
}

void syNetplayBranchTurnDashApply(GObj *fighter_gobj, const SYNetplayBranchTurnDashSnap *snap)
{
	FTStruct *fp;
	ftCommonTurnStatusVars *turn;

	if ((fighter_gobj == NULL) || (snap == NULL))
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp == NULL)
	{
		return;
	}
	turn = ftStatusVarsTurn(fp);
	if (turn != NULL)
	{
		turn->lr_dash = snap->lr_dash;
		turn->attacks4_buffer = snap->attacks4_buffer;
	}
	syNetplayTurnNoteEntryLrDash(fp, snap->entry_lr_dash);
}

void syNetplayBranchTurnDashRestoreFn(GObj *fighter_gobj, const void *preimage, u32 size)
{
	if ((preimage == NULL) || (size < sizeof(SYNetplayBranchTurnDashSnap)))
	{
		return;
	}
	syNetplayBranchTurnDashApply(fighter_gobj, (const SYNetplayBranchTurnDashSnap *)preimage);
}

sb32 syNetplayBranchTurnDashEvalBegin(GObj *fighter_gobj)
{
	SYNetplayBranchTurnDashSnap snap;

	syNetplayBranchTurnDashCapture(fighter_gobj, &snap);
	return syNetplayBranchEvalBegin(fighter_gobj, "turn_allow_dash", &snap, sizeof(snap));
}

sb32 syNetplayBranchTurnDashEvalResolve(GObj *fighter_gobj, sb32 wants_branch)
{
	SYNetplayBranchTurnDashSnap live;
	sb32 restore = FALSE;

	/*
	 * Skip restore/log when DashCheckTurn was a no-op (preimage unchanged). Still
	 * run Resolve so predicted status commit stays blocked and diagnostics fire.
	 */
	if ((sSYNetplayBranchEval.active != FALSE) && (sSYNetplayBranchEval.speculative != FALSE) &&
	    (sSYNetplayBranchEval.snap_size == sizeof(SYNetplayBranchTurnDashSnap)))
	{
		syNetplayBranchTurnDashCapture(fighter_gobj, &live);
		if (memcmp(&live, sSYNetplayBranchEval.snap, sizeof(live)) != 0)
		{
			restore = TRUE;
		}
	}
	else if ((sSYNetplayBranchEval.active != FALSE) && (sSYNetplayBranchEval.speculative != FALSE))
	{
		restore = TRUE;
	}

	return syNetplayBranchEvalResolve(fighter_gobj, wants_branch,
	                                  (restore != FALSE) ? syNetplayBranchTurnDashRestoreFn : NULL);
}

#endif /* PORT && SSB64_NETMENU */
