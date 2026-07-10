#include <sys/netdesyncclassifier.h>

#if defined(PORT)

#include <sys/netinput.h>

#include <stdlib.h>
#include <string.h>

extern sb32 syNetPeerIsVSSessionActive(void);

extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);

typedef enum SYNetDesyncCat {
	SYNET_DESYNC_CAT_NONE = 0,
	SYNET_DESYNC_CAT_INPUT,
	SYNET_DESYNC_CAT_COMMIT,
	SYNET_DESYNC_CAT_SNAPSHOT,
	SYNET_DESYNC_CAT_SIM,
} SYNetDesyncCat;

typedef struct SYNetDesyncTrace {
	u32 first_input_values_tick;
	s32 first_input_values_player;
	u32 first_input_presence_tick;
	s32 first_input_presence_player;
	u32 first_transport_gap_tick;
	u32 last_seq_gaps_seen;
	u32 first_rollback_input_mismatch_tick;
	u32 first_commit_delay_E_tick;
	u32 first_commit_delay_S_tick;
	u32 first_commit_delay_K_tick;
	u32 first_bootstrap_bind_mismatch_tick;
	u32 first_commit_token_mismatch_validation_tick;
	SYNetFrameCommitToken last_commit_mismatch_local;
	SYNetFrameCommitToken last_commit_mismatch_peer;
	sb32 last_commit_delta_frame_id;
	sb32 last_commit_delta_input_digest;
	sb32 last_commit_delta_slot_binding;
	sb32 commit_mismatch_detail_valid;
	sb32 commit_token_starvation;
	u32 commit_token_starvation_tick;
	u32 first_load_hash_drift_tick;
	u32 first_peer_snapshot_diverge_tick;
	u32 first_verify_strict_tick;
	u32 last_validation_tick;
	u32 last_inp_all;
	u32 last_figh;
	u32 last_mph;
	sb32 any_evidence;
} SYNetDesyncTrace;

static SYNetDesyncTrace s_trace;
static int s_env_lvl = -999;
static SYNetDesyncCat s_last_leading_logged = SYNET_DESYNC_CAT_NONE;
static int s_frame_commit_wire_cache = -999;
static int s_frame_commit_starvation_thr_cache = -999;
static sb32 s_commit_starvation_latched;
static u32 s_commit_mismatch_classifier_log_count;

static int syNetDesyncClassifierGetLevel(void)
{
	const char *e;

	if (s_env_lvl != -999)
	{
		return s_env_lvl;
	}
	e = getenv("SSB64_NETPLAY_DESYNC_CLASSIFIER");
	s_env_lvl = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 0;
	if (s_env_lvl < 0)
	{
		s_env_lvl = 0;
	}
	if (s_env_lvl > 2)
	{
		s_env_lvl = 2;
	}
	return s_env_lvl;
}

static sb32 syNetDesyncClassifierActive(void)
{
	return (syNetDesyncClassifierGetLevel() >= 1) ? TRUE : FALSE;
}

static sb32 syNetDesyncFrameCommitWireEnabled(void)
{
	const char *e;
	int v;

	if (s_frame_commit_wire_cache != -999)
	{
		return (s_frame_commit_wire_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_FRAME_COMMIT_TOKEN");
	if ((e == NULL) || (e[0] == '\0'))
	{
		v = 1;
	}
	else
	{
		v = atoi(e);
		if (v < 0)
		{
			v = 0;
		}
		if (v > 1)
		{
			v = 1;
		}
	}
	s_frame_commit_wire_cache = v;
	return (v != 0) ? TRUE : FALSE;
}

static int syNetDesyncFrameCommitStarvationThreshold(void)
{
	const char *e;
	int t;

	if (s_frame_commit_starvation_thr_cache != -999)
	{
		return s_frame_commit_starvation_thr_cache;
	}
	e = getenv("SSB64_NETPLAY_FRAME_COMMIT_STARVATION");
	t = ((e != NULL) && (e[0] != '\0')) ? atoi(e) : 4;
	if (t < 1)
	{
		t = 1;
	}
	if (t > 64)
	{
		t = 64;
	}
	s_frame_commit_starvation_thr_cache = t;
	return t;
}

static void syNetDesyncClassifierMarkFirstU32(u32 *slot, u32 tick)
{
	if (tick == 0U)
	{
		return;
	}
	if (*slot == 0U || tick < *slot)
	{
		*slot = tick;
	}
}

static u32 syNetDesyncMinNonZero3(u32 a, u32 b, u32 c)
{
	u32 m = 0U;

	if (a != 0U && (m == 0U || a < m))
	{
		m = a;
	}
	if (b != 0U && (m == 0U || b < m))
	{
		m = b;
	}
	if (c != 0U && (m == 0U || c < m))
	{
		m = c;
	}
	return m;
}

static SYNetDesyncCat syNetDesyncClassify(void)
{
	u32 t_in;
	u32 t_commit_token;
	sb32 B_input;
	sb32 B_commit;
	sb32 B_snap;
	sb32 B_sim;
	SYNetDesyncCat cat;

	t_in = syNetDesyncMinNonZero3(s_trace.first_input_values_tick, s_trace.first_rollback_input_mismatch_tick,
				      s_trace.first_transport_gap_tick);
	t_commit_token = s_trace.first_commit_token_mismatch_validation_tick;

	B_input = (t_in != 0U) && ((t_commit_token == 0U) || (t_in <= t_commit_token));
	if (B_input != FALSE)
	{
		return SYNET_DESYNC_CAT_INPUT;
	}
	B_commit = (t_commit_token != 0U);
	if (B_commit != FALSE)
	{
		return SYNET_DESYNC_CAT_COMMIT;
	}
	B_snap = (s_trace.first_load_hash_drift_tick != 0U) || (s_trace.first_peer_snapshot_diverge_tick != 0U);
	if (B_snap != FALSE)
	{
		return SYNET_DESYNC_CAT_SNAPSHOT;
	}
	B_sim = (s_trace.first_verify_strict_tick != 0U);
	if (B_sim != FALSE)
	{
		return SYNET_DESYNC_CAT_SIM;
	}
	if (s_trace.commit_token_starvation != FALSE)
	{
		return SYNET_DESYNC_CAT_INPUT;
	}
	return SYNET_DESYNC_CAT_NONE;
}

static const char *syNetDesyncCatName(SYNetDesyncCat c)
{
	switch (c)
	{
	case SYNET_DESYNC_CAT_INPUT:
		return "INPUT";
	case SYNET_DESYNC_CAT_COMMIT:
		return "COMMIT";
	case SYNET_DESYNC_CAT_SNAPSHOT:
		return "SNAPSHOT";
	case SYNET_DESYNC_CAT_SIM:
		return "SIM";
	default:
		return "NO_DESYNC";
	}
}

static void syNetDesyncMaybeLogLeadingChange(void)
{
	SYNetDesyncCat now;

	if (syNetDesyncClassifierGetLevel() < 2)
	{
		return;
	}
	if (s_trace.any_evidence == FALSE)
	{
		return;
	}
	now = syNetDesyncClassify();
	if (now != s_last_leading_logged)
	{
		s_last_leading_logged = now;
		port_log("SSB64 DESYNC_CLASSIFIER: leading_category_now=%s validation_tick=%u\n", syNetDesyncCatName(now),
			 s_trace.last_validation_tick);
	}
}

void syNetDesyncClassifierReset(void)
{
	memset(&s_trace, 0, sizeof(s_trace));
	s_trace.first_input_values_player = -1;
	s_trace.first_input_presence_player = -1;
	s_last_leading_logged = SYNET_DESYNC_CAT_NONE;
	s_frame_commit_wire_cache = -999;
	s_frame_commit_starvation_thr_cache = -999;
	s_commit_starvation_latched = FALSE;
	s_commit_mismatch_classifier_log_count = 0U;
}

void syNetDesyncClassifierOnNetSyncValidation(u32 validation_tick, u32 hist_win_begin, u32 hist_win_len, u32 inp_all,
					     u32 fighter_hash, u32 map_hash, u32 late_frames, u32 seq_gaps_total)
{
	s32 mis_player;
	u32 mis_tick;
	u32 mis_kind;

	(void)late_frames;
	if (syNetDesyncClassifierActive() == FALSE)
	{
		return;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	s_trace.last_validation_tick = validation_tick;
	s_trace.last_inp_all = inp_all;
	s_trace.last_figh = fighter_hash;
	s_trace.last_mph = map_hash;
	s_trace.any_evidence = TRUE;

	if (seq_gaps_total > s_trace.last_seq_gaps_seen)
	{
		syNetDesyncClassifierMarkFirstU32(&s_trace.first_transport_gap_tick, validation_tick);
	}
	s_trace.last_seq_gaps_seen = seq_gaps_total;

	if (syNetInputDiagFindFirstPublishedRemoteMismatch(hist_win_begin, hist_win_len, &mis_player, &mis_tick, &mis_kind) !=
	    FALSE)
	{
		if (mis_kind != 0U)
		{
			if (s_trace.first_input_values_tick == 0U || mis_tick < s_trace.first_input_values_tick)
			{
				s_trace.first_input_values_tick = mis_tick;
				s_trace.first_input_values_player = mis_player;
			}
		}
		else
		{
			if (s_trace.first_input_presence_tick == 0U || mis_tick < s_trace.first_input_presence_tick)
			{
				s_trace.first_input_presence_tick = mis_tick;
				s_trace.first_input_presence_player = mis_player;
			}
		}
	}
	syNetDesyncMaybeLogLeadingChange();
}

void syNetDesyncClassifierOnAdmissionPath(u32 sim_tick, char path)
{
	if (syNetDesyncClassifierActive() == FALSE)
	{
		return;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	s_trace.any_evidence = TRUE;
	switch (path)
	{
	case 'E':
		syNetDesyncClassifierMarkFirstU32(&s_trace.first_commit_delay_E_tick, sim_tick);
		break;
	case 'S':
		syNetDesyncClassifierMarkFirstU32(&s_trace.first_commit_delay_S_tick, sim_tick);
		break;
	case 'K':
		syNetDesyncClassifierMarkFirstU32(&s_trace.first_commit_delay_K_tick, sim_tick);
		break;
	default:
		break;
	}
	syNetDesyncMaybeLogLeadingChange();
}

void syNetDesyncClassifierOnFrameIdentityMismatch(u32 tick)
{
	if (syNetDesyncClassifierActive() == FALSE)
	{
		return;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	s_trace.any_evidence = TRUE;
	syNetDesyncClassifierMarkFirstU32(&s_trace.first_bootstrap_bind_mismatch_tick, tick);
	syNetDesyncMaybeLogLeadingChange();
}

void syNetDesyncClassifierOnFrameCommitTokenMismatch(u32 validation_tick, const SYNetFrameCommitToken *local,
						   const SYNetFrameCommitToken *peer)
{
	sb32 df;
	sb32 di;
	sb32 ds;
	sb32 dt;

	if (syNetDesyncClassifierActive() == FALSE)
	{
		return;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	if (syNetFrameCommitTokensDesync(local, peer, &df, &di, &ds, &dt) == FALSE)
	{
		return;
	}
	s_trace.any_evidence = TRUE;
	syNetDesyncClassifierMarkFirstU32(&s_trace.first_commit_token_mismatch_validation_tick, validation_tick);
	s_trace.last_commit_mismatch_local = *local;
	s_trace.last_commit_mismatch_peer = *peer;
	s_trace.last_commit_delta_frame_id = df;
	s_trace.last_commit_delta_input_digest = di;
	s_trace.last_commit_delta_slot_binding = ds;
	s_trace.commit_mismatch_detail_valid = TRUE;
	if (syNetDesyncClassifierGetLevel() >= 1)
	{
		if (s_commit_mismatch_classifier_log_count < 16U)
		{
			s_commit_mismatch_classifier_log_count++;
			port_log(
			    "SSB64 DESYNC_CLASSIFIER: commit_token_mismatch validation=%u delta_frame_id=%d "
			    "delta_input_digest=%d delta_slot_binding=%d local inp=0x%08X bind=0x%08X | peer inp=0x%08X bind=0x%08X\n",
			    validation_tick, (int)df, (int)di, (int)ds, local->input_digest, local->slot_binding_hash,
			    peer->input_digest, peer->slot_binding_hash);
		}
	}
	syNetDesyncMaybeLogLeadingChange();
}

void syNetDesyncClassifierOnFrameCommitValidationSent(u32 validation_tick, u32 validations_since_peer_reset)
{
	(void)validation_tick;
	if (syNetDesyncFrameCommitWireEnabled() == FALSE)
	{
		return;
	}
	if (syNetDesyncClassifierActive() == FALSE)
	{
		return;
	}
	if (s_commit_starvation_latched != FALSE)
	{
		return;
	}
	if (validations_since_peer_reset >= (u32)syNetDesyncFrameCommitStarvationThreshold())
	{
		s_commit_starvation_latched = TRUE;
		s_trace.commit_token_starvation = TRUE;
		s_trace.commit_token_starvation_tick = validation_tick;
		s_trace.any_evidence = TRUE;
		syNetDesyncMaybeLogLeadingChange();
	}
}

void syNetDesyncClassifierOnFrameCommitPeerTokenReceived(u32 validation_tick)
{
	(void)validation_tick;
}

void syNetDesyncClassifierOnLoadHashDrift(u32 tick)
{
	if (syNetDesyncClassifierActive() == FALSE)
	{
		return;
	}
	s_trace.any_evidence = TRUE;
	syNetDesyncClassifierMarkFirstU32(&s_trace.first_load_hash_drift_tick, tick);
	syNetDesyncMaybeLogLeadingChange();
}

void syNetDesyncClassifierOnPeerSnapshotDiverge(u32 load_tick)
{
	if (syNetDesyncClassifierActive() == FALSE)
	{
		return;
	}
	s_trace.any_evidence = TRUE;
	syNetDesyncClassifierMarkFirstU32(&s_trace.first_peer_snapshot_diverge_tick, load_tick);
	syNetDesyncMaybeLogLeadingChange();
}

void syNetDesyncClassifierOnRollbackInputMismatch(u32 mismatch_tick)
{
	if (syNetDesyncClassifierActive() == FALSE)
	{
		return;
	}
	s_trace.any_evidence = TRUE;
	syNetDesyncClassifierMarkFirstU32(&s_trace.first_rollback_input_mismatch_tick, mismatch_tick);
	syNetDesyncMaybeLogLeadingChange();
}

void syNetDesyncClassifierOnVerifyStrictUnchanged(u32 mismatch_tick)
{
	if (syNetDesyncClassifierActive() == FALSE)
	{
		return;
	}
	s_trace.any_evidence = TRUE;
	syNetDesyncClassifierMarkFirstU32(&s_trace.first_verify_strict_tick, mismatch_tick);
	syNetDesyncMaybeLogLeadingChange();
}

extern void syNetPeerEmitFrameCommitDiagReport(void);

void syNetDesyncClassifierEmitFrameCommitReportOnVsStop(void)
{
	if (syNetDesyncFrameCommitWireEnabled() == FALSE)
	{
		return;
	}
	syNetPeerEmitFrameCommitDiagReport();
	port_log("SSB64 FRAME COMMIT REPORT\n");
	port_log("-------------------------\n");
	port_log("First mismatch validation_tick: %u\n", s_trace.first_commit_token_mismatch_validation_tick);
	port_log("Token starvation fallback (INPUT): %s tick=%u\n",
		 (s_trace.commit_token_starvation != FALSE) ? "yes" : "no", s_trace.commit_token_starvation_tick);
	if (s_trace.commit_mismatch_detail_valid != FALSE)
	{
		port_log(
		    "LOCAL_TOKEN: frame_id=%d input_digest=0x%08X slot_binding=0x%08X tick_anchor=%u\n",
		    (int)s_trace.last_commit_mismatch_local.frame_id, s_trace.last_commit_mismatch_local.input_digest,
		    s_trace.last_commit_mismatch_local.slot_binding_hash, s_trace.last_commit_mismatch_local.tick_anchor);
		port_log(
		    "PEER_TOKEN:  frame_id=%d input_digest=0x%08X slot_binding=0x%08X tick_anchor=%u\n",
		    (int)s_trace.last_commit_mismatch_peer.frame_id, s_trace.last_commit_mismatch_peer.input_digest,
		    s_trace.last_commit_mismatch_peer.slot_binding_hash, s_trace.last_commit_mismatch_peer.tick_anchor);
		port_log(
		    "DELTA: frame_id=%d input_digest=%d slot_binding=%d (slot_binding/tick_anchor logged only; not cross-peer compared)\n",
		    (int)s_trace.last_commit_delta_frame_id, (int)s_trace.last_commit_delta_input_digest,
		    (int)s_trace.last_commit_delta_slot_binding);
	}
	else
	{
		port_log("LOCAL_TOKEN / PEER_TOKEN: (no cross-peer token mismatch recorded this session)\n");
	}
}

void syNetDesyncClassifierEmitReportOnVsStop(void)
{
	SYNetDesyncCat cat;

	if (syNetDesyncClassifierGetLevel() < 1)
	{
		return;
	}
	cat = syNetDesyncClassify();
	port_log("SSB64 DESYNC REPORT\n");
	port_log("-------------------\n");
	port_log("Category: %s\n", syNetDesyncCatName(cat));
	port_log(
	    "Evidence ticks: input_values=%u (player=%d) input_presence=%u (player=%d) transport_gap=%u rollback_in=%u "
	    "commit_token_mismatch=%u bootstrap_bind=%u commit_delay_E=%u commit_delay_S=%u commit_delay_K=%u "
	    "load_hash_drift=%u peer_snapshot_diverge=%u verify_strict=%u last_val_tick=%u last_inp_all=0x%08X last_figh=0x%08X last_mph=0x%08X\n",
	    s_trace.first_input_values_tick, (int)s_trace.first_input_values_player, s_trace.first_input_presence_tick,
	    (int)s_trace.first_input_presence_player, s_trace.first_transport_gap_tick,
	    s_trace.first_rollback_input_mismatch_tick, s_trace.first_commit_token_mismatch_validation_tick,
	    s_trace.first_bootstrap_bind_mismatch_tick, s_trace.first_commit_delay_E_tick, s_trace.first_commit_delay_S_tick,
	    s_trace.first_commit_delay_K_tick, s_trace.first_load_hash_drift_tick,
	    s_trace.first_peer_snapshot_diverge_tick, s_trace.first_verify_strict_tick, s_trace.last_validation_tick,
	    s_trace.last_inp_all, s_trace.last_figh, s_trace.last_mph);
	port_log(
	    "Notes: COMMIT = first cross-peer SYNetFrameCommitToken mismatch (NetSync cadence). INPUT = value "
	    "pub_vs_remote, rollback mismatch, seq_gap, or commit-token starvation fallback. bootstrap_bind = "
	    "input_bind / battle_exec_sync only (diagnostic). commit_delay_* = E/S/K only. SNAPSHOT = LOAD_HASH_DRIFT. "
	    "SIM = VERIFY_STRICT.\n");
}

#endif /* defined(PORT) */
