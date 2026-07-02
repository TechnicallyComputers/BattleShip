#include <sys/netplay_resim_replay_hang_diag.h>

#if defined(PORT) && defined(SSB64_NETMENU)

#include <ft/fighter.h>
#include <ft/ftdef.h>
#include <sys/netinput.h>
#include <sys/netplay_fox_firefox_gate.h>
#include <sys/netrollback.h>
#include <sys/objdef.h>
#include <sys/objman.h>
#include <sys/objtypes.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void port_log(const char *fmt, ...);
extern char *getenv(const char *name);

void syNetRollbackResimReplayHangDiagExportEpisode(u32 *out_pending, u32 *out_load, u32 *out_mismatch,
                                                   u32 *out_target, u32 *out_next);

static sb32 s_syNetplayResimReplayHangDiagCache = -999;
static sb32 s_syNetplayResimReplayHangDiagVerboseCache = -999;

static s32 s_syNetplayResimReplayHangDiagIngressDepth;
static s32 s_syNetplayResimReplayHangDiagBattleSimDepth;
static u32 s_syNetplayResimReplayHangDiagReplayTick;
static char s_syNetplayResimReplayHangDiagLastCaller[48];

static u32 s_syNetplayResimReplayHangDiagLastGobjId;
static u8 s_syNetplayResimReplayHangDiagLastGobjLink;
static u8 s_syNetplayResimReplayHangDiagLastGobjKind;
static void (*s_syNetplayResimReplayHangDiagLastFuncRun)(GObj *);

static u8 s_syNetplayResimReplayHangDiagLastProcKind;
static u8 s_syNetplayResimReplayHangDiagLastProcPriority;
static u32 s_syNetplayResimReplayHangDiagLastProcParentId;
static void (*s_syNetplayResimReplayHangDiagLastProcFunc)(GObj *);
static void (*s_syNetplayResimReplayHangDiagLastProcFuncId)(GObj *);
static s32 s_syNetplayResimReplayHangDiagLastGcMesgValid;
static s32 s_syNetplayResimReplayHangDiagLastThreadState;

sb32 syNetplayResimReplayHangDiagEnabled(void)
{
	const char *env;

	if (s_syNetplayResimReplayHangDiagCache != -999)
	{
		return s_syNetplayResimReplayHangDiagCache;
	}
	env = getenv("SSB64_NETPLAY_RESIM_REPLAY_HANG_DIAG");
	s_syNetplayResimReplayHangDiagCache =
	    ((env != NULL) && (env[0] != '\0') && (strcmp(env, "0") != 0)) ? TRUE : FALSE;
	return s_syNetplayResimReplayHangDiagCache;
}

static sb32 syNetplayResimReplayHangDiagVerbose(void)
{
	const char *env;

	if (s_syNetplayResimReplayHangDiagVerboseCache != -999)
	{
		return s_syNetplayResimReplayHangDiagVerboseCache;
	}
	env = getenv("SSB64_NETPLAY_RESIM_REPLAY_HANG_DIAG_VERBOSE");
	s_syNetplayResimReplayHangDiagVerboseCache =
	    ((env != NULL) && (env[0] != '\0') && (strcmp(env, "0") != 0)) ? TRUE : FALSE;
	return s_syNetplayResimReplayHangDiagVerboseCache;
}

static void syNetplayResimReplayHangDiagStoreCaller(const char *caller_tag)
{
	if ((caller_tag == NULL) || (caller_tag[0] == '\0'))
	{
		s_syNetplayResimReplayHangDiagLastCaller[0] = '\0';
		return;
	}
	snprintf(s_syNetplayResimReplayHangDiagLastCaller, sizeof(s_syNetplayResimReplayHangDiagLastCaller), "%s",
	         caller_tag);
}

static void syNetplayResimReplayHangDiagLogFoxFirefoxContext(u32 tick, const char *phase)
{
	GObj *fighter_gobj;

	if (syNetplayFoxLiveHasFirefoxSynctestDeferScope() == FALSE)
	{
		return;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp == NULL) || (fp->fkind != nFTKindFox))
		{
			continue;
		}
		port_log(
		    "SSB64 Netplay: RESIM_REPLAY_HANG_DIAG tick=%u phase=%s fox player=%d status=%d motion=%d launch_delay=%d anim_frames=%d\n",
		    tick,
		    (phase != NULL) ? phase : "?",
		    (int)fp->player,
		    (int)fp->status_id,
		    (int)fp->motion_id,
		    (int)fp->status_vars.fox.specialhi.launch_delay,
		    (int)fp->status_vars.fox.specialhi.anim_frames);
	}
}

void syNetplayResimReplayHangDiagNotePacketIngressEnter(void)
{
	if (syNetplayResimReplayHangDiagEnabled() == FALSE)
	{
		return;
	}
	s_syNetplayResimReplayHangDiagIngressDepth++;
	if ((syNetplayResimReplayHangDiagVerbose() != FALSE) && (s_syNetplayResimReplayHangDiagIngressDepth == 1))
	{
		port_log("SSB64 Netplay: RESIM_REPLAY_HANG_DIAG packet_ingress_enter depth=%d sim=%u resim=%d\n",
		         s_syNetplayResimReplayHangDiagIngressDepth,
		         (unsigned int)syNetInputGetTick(),
		         (int)syNetRollbackIsResimulating());
	}
}

void syNetplayResimReplayHangDiagNotePacketIngressExit(void)
{
	if (syNetplayResimReplayHangDiagEnabled() == FALSE)
	{
		return;
	}
	if (s_syNetplayResimReplayHangDiagIngressDepth > 0)
	{
		s_syNetplayResimReplayHangDiagIngressDepth--;
	}
}

void syNetplayResimReplayHangDiagNoteReplayGateOpen(const char *caller_tag)
{
	u32 pending;
	u32 load_tick;
	u32 mismatch_tick;
	u32 target_tick;
	u32 next_tick;

	if (syNetplayResimReplayHangDiagEnabled() == FALSE)
	{
		return;
	}
	syNetplayResimReplayHangDiagStoreCaller(caller_tag);
	syNetRollbackResimReplayHangDiagExportEpisode(&pending, &load_tick, &mismatch_tick, &target_tick, &next_tick);
	port_log(
	    "SSB64 Netplay: RESIM_REPLAY_HANG_DIAG replay_gate_open caller=%s ingress=%d sim=%u pending=%u load=%u mismatch=%u target=%u next=%u resim=%d\n",
	    (caller_tag != NULL) ? caller_tag : "?",
	    s_syNetplayResimReplayHangDiagIngressDepth,
	    (unsigned int)syNetInputGetTick(),
	    pending,
	    load_tick,
	    mismatch_tick,
	    target_tick,
	    next_tick,
	    (int)syNetRollbackIsResimulating());
	syNetplayResimReplayHangDiagLogFoxFirefoxContext(syNetInputGetTick(), "replay_gate_open");
}

void syNetplayResimReplayHangDiagNoteReplayTickBegin(u32 tick, u32 ran_index, u32 tick_limit)
{
	if (syNetplayResimReplayHangDiagEnabled() == FALSE)
	{
		return;
	}
	s_syNetplayResimReplayHangDiagReplayTick = tick;
	port_log(
	    "SSB64 Netplay: RESIM_REPLAY_HANG_DIAG replay_tick_begin tick=%u ran=%u limit=%u ingress=%d battle_sim_depth=%d caller=%s\n",
	    tick,
	    ran_index,
	    tick_limit,
	    s_syNetplayResimReplayHangDiagIngressDepth,
	    s_syNetplayResimReplayHangDiagBattleSimDepth,
	    s_syNetplayResimReplayHangDiagLastCaller);
	syNetplayResimReplayHangDiagLogFoxFirefoxContext(tick, "replay_tick_begin");
}

void syNetplayResimReplayHangDiagNoteReplayTickEnd(u32 tick)
{
	if (syNetplayResimReplayHangDiagEnabled() == FALSE)
	{
		return;
	}
	if (syNetplayResimReplayHangDiagVerbose() != FALSE)
	{
		port_log("SSB64 Netplay: RESIM_REPLAY_HANG_DIAG replay_tick_end tick=%u ingress=%d\n", tick,
		         s_syNetplayResimReplayHangDiagIngressDepth);
	}
}

void syNetplayResimReplayHangDiagNoteBattleSimOnlyBegin(const char *caller_tag)
{
	if (syNetplayResimReplayHangDiagEnabled() == FALSE)
	{
		return;
	}
	s_syNetplayResimReplayHangDiagBattleSimDepth++;
	syNetplayResimReplayHangDiagStoreCaller(caller_tag);
	if ((syNetplayResimReplayHangDiagVerbose() != FALSE) || (s_syNetplayResimReplayHangDiagIngressDepth > 0))
	{
		port_log(
		    "SSB64 Netplay: RESIM_REPLAY_HANG_DIAG battle_sim_only_begin caller=%s tick=%u replay_tick=%u ingress=%d depth=%d gc_mesg_valid=%d\n",
		    (caller_tag != NULL) ? caller_tag : "?",
		    (unsigned int)syNetInputGetTick(),
		    s_syNetplayResimReplayHangDiagReplayTick,
		    s_syNetplayResimReplayHangDiagIngressDepth,
		    s_syNetplayResimReplayHangDiagBattleSimDepth,
		    (int)gGCMesgQueue.validCount);
	}
}

void syNetplayResimReplayHangDiagNoteBattleSimOnlyEnd(void)
{
	if (syNetplayResimReplayHangDiagEnabled() == FALSE)
	{
		return;
	}
	if (s_syNetplayResimReplayHangDiagBattleSimDepth > 0)
	{
		s_syNetplayResimReplayHangDiagBattleSimDepth--;
	}
}

void syNetplayResimReplayHangDiagNoteGcRunGObj(GObj *gobj)
{
	if ((syNetplayResimReplayHangDiagEnabled() == FALSE) || (gobj == NULL))
	{
		return;
	}
	s_syNetplayResimReplayHangDiagLastGobjId = gobj->id;
	s_syNetplayResimReplayHangDiagLastGobjLink = gobj->link_id;
	s_syNetplayResimReplayHangDiagLastGobjKind = gobj->obj_kind;
	s_syNetplayResimReplayHangDiagLastFuncRun = gobj->func_run;
	if (syNetplayResimReplayHangDiagVerbose() != FALSE)
	{
		port_log(
		    "SSB64 Netplay: RESIM_REPLAY_HANG_DIAG gc_run_gobj id=%u link=%u kind=%u func_run=%p tick=%u replay=%u\n",
		    gobj->id,
		    (unsigned int)gobj->link_id,
		    (unsigned int)gobj->obj_kind,
		    (void *)gobj->func_run,
		    (unsigned int)syNetInputGetTick(),
		    s_syNetplayResimReplayHangDiagReplayTick);
	}
}

void syNetplayResimReplayHangDiagNoteGcRunGObjProcessBegin(GObjProcess *gobjproc)
{
	GObj *parent;

	if ((syNetplayResimReplayHangDiagEnabled() == FALSE) || (gobjproc == NULL))
	{
		return;
	}
	parent = gobjproc->parent_gobj;
	s_syNetplayResimReplayHangDiagLastProcKind = gobjproc->kind;
	s_syNetplayResimReplayHangDiagLastProcPriority = (u8)gobjproc->priority;
	s_syNetplayResimReplayHangDiagLastProcParentId = (parent != NULL) ? parent->id : 0U;
	s_syNetplayResimReplayHangDiagLastProcFunc = gobjproc->exec.func;
	s_syNetplayResimReplayHangDiagLastProcFuncId = gobjproc->func_id;
	s_syNetplayResimReplayHangDiagLastGcMesgValid = gGCMesgQueue.validCount;
	if (gobjproc->kind == nGCProcessKindThread)
	{
		s_syNetplayResimReplayHangDiagLastThreadState = (s32)gobjproc->exec.gobjthread->thread.state;
	}
	else
	{
		s_syNetplayResimReplayHangDiagLastThreadState = -1;
	}
	if (syNetplayResimReplayHangDiagVerbose() != FALSE)
	{
		port_log(
		    "SSB64 Netplay: RESIM_REPLAY_HANG_DIAG gc_proc_begin parent_id=%u kind=%u pri=%u func=%p func_id=%p mesg_valid=%d tick=%u replay=%u\n",
		    s_syNetplayResimReplayHangDiagLastProcParentId,
		    (unsigned int)gobjproc->kind,
		    (unsigned int)gobjproc->priority,
		    (void *)gobjproc->exec.func,
		    (void *)gobjproc->func_id,
		    (int)gGCMesgQueue.validCount,
		    (unsigned int)syNetInputGetTick(),
		    s_syNetplayResimReplayHangDiagReplayTick);
	}
}

void syNetplayResimReplayHangDiagNoteGcRunGObjProcessThreadRecvWait(GObjProcess *gobjproc, s32 queue_valid)
{
	if (syNetplayResimReplayHangDiagEnabled() == FALSE)
	{
		return;
	}
	s_syNetplayResimReplayHangDiagLastGcMesgValid = queue_valid;
	if (queue_valid <= 0)
	{
		port_log(
		    "SSB64 Netplay: RESIM_REPLAY_HANG_DIAG gc_proc_thread_recv_wait parent_id=%u func_id=%p thread_state=%d mesg_valid=%d ingress=%d replay_tick=%u sim=%u\n",
		    (gobjproc != NULL && gobjproc->parent_gobj != NULL) ? gobjproc->parent_gobj->id : 0U,
		    (gobjproc != NULL) ? (void *)gobjproc->func_id : NULL,
		    s_syNetplayResimReplayHangDiagLastThreadState,
		    queue_valid,
		    s_syNetplayResimReplayHangDiagIngressDepth,
		    s_syNetplayResimReplayHangDiagReplayTick,
		    (unsigned int)syNetInputGetTick());
	}
}

void syNetplayResimReplayHangDiagNoteGcRunGObjProcessEnd(void)
{
	if (syNetplayResimReplayHangDiagEnabled() == FALSE)
	{
		return;
	}
	s_syNetplayResimReplayHangDiagLastProcKind = 0U;
	s_syNetplayResimReplayHangDiagLastThreadState = -1;
}

void syNetplayResimReplayHangDiagLogHangSnapshot(void)
{
	u32 pending;
	u32 load_tick;
	u32 mismatch_tick;
	u32 target_tick;
	u32 next_tick;

	if (syNetplayResimReplayHangDiagEnabled() == FALSE)
	{
		return;
	}
	syNetRollbackResimReplayHangDiagExportEpisode(&pending, &load_tick, &mismatch_tick, &target_tick, &next_tick);
	port_log(
	    "SSB64 Netplay: RESIM_REPLAY_HANG_DIAG hang_snapshot ingress=%d battle_sim_depth=%d caller=%s sim=%u replay_tick=%u pending=%u load=%u mismatch=%u target=%u next=%u resim=%d gc_status=%d\n",
	    s_syNetplayResimReplayHangDiagIngressDepth,
	    s_syNetplayResimReplayHangDiagBattleSimDepth,
	    s_syNetplayResimReplayHangDiagLastCaller,
	    (unsigned int)syNetInputGetTick(),
	    s_syNetplayResimReplayHangDiagReplayTick,
	    pending,
	    load_tick,
	    mismatch_tick,
	    target_tick,
	    next_tick,
	    (int)syNetRollbackIsResimulating(),
	    (int)dGCCurrentStatus);
	port_log(
	    "SSB64 Netplay: RESIM_REPLAY_HANG_DIAG hang_gobj id=%u link=%u kind=%u func_run=%p proc_kind=%u proc_pri=%u parent_id=%u proc_func=%p proc_func_id=%p thread_state=%d gc_mesg_valid=%d current_gobj=%p current_proc=%p\n",
	    s_syNetplayResimReplayHangDiagLastGobjId,
	    (unsigned int)s_syNetplayResimReplayHangDiagLastGobjLink,
	    (unsigned int)s_syNetplayResimReplayHangDiagLastGobjKind,
	    (void *)s_syNetplayResimReplayHangDiagLastFuncRun,
	    (unsigned int)s_syNetplayResimReplayHangDiagLastProcKind,
	    (unsigned int)s_syNetplayResimReplayHangDiagLastProcPriority,
	    s_syNetplayResimReplayHangDiagLastProcParentId,
	    (void *)s_syNetplayResimReplayHangDiagLastProcFunc,
	    (void *)s_syNetplayResimReplayHangDiagLastProcFuncId,
	    s_syNetplayResimReplayHangDiagLastThreadState,
	    s_syNetplayResimReplayHangDiagLastGcMesgValid,
	    (void *)gGCCurrentCommon,
	    (void *)gGCCurrentProcess);
	syNetplayResimReplayHangDiagLogFoxFirefoxContext(syNetInputGetTick(), "hang_snapshot");
}

#endif /* PORT && SSB64_NETMENU */
