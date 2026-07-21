#ifndef _SYNETINPUT_H_
#define _SYNETINPUT_H_

/*
 * NetInput — authoritative per-slot controller frames aligned to `sSYNetInputTick`.
 *
 * Pipeline (normal VS): scenes call `syNetInputFuncRead` once per sim step. On PORT+NETMENU it consumes one sample from a
 * wall-rate HID capture FIFO into an internal latch (once per `sSYNetInputTick`; **before** stall-until-remote so the sample
 * is keyed to the local sim tick) and clears `gSYControllerDevices[]` before resolve. The FIFO is polled on every FuncRead
 * while the same tick is held and on PortPushFrame sim-skips, preserving stick trajectory across rare R-holds (not peak-hold).
 * In phase-locked VS (NETMENU), local HID sampled at sim `t` is staged into a gameplay ring at `t` (feel delay 0) and a
 * send-lead ring at `t` plus provisional hold-last rows through `t+D` so wire `hr` can lead intro Wait; resolve consumes
 * the gameplay row for the current sim tick. Committed `D` remains wire send lead only (`wire = sim + D`). Replay,
 * remote-confirmed rings, and prediction fill the
 * other sources, then publish into `gSYControllerDevices[]`,
 * snapshots `SYNETINPUT_HISTORY_LENGTH` rings. `sSYNetInputTick` advances only after a full `scVSBattleFuncUpdate`
 * (`syNetInputAdvanceAuthoritativeSimTick`). Active UDP VS (PORT): `syNetTickCommitEvaluate` unifies exec-ready
 * (`syNetPeerCheckBattleExecutionReady`) with wire/stall/skew admission so FuncRead and battle sim agree on tick commit.
 *
 * GGPO-shaped rule: rollback resim replays local slots from published history for that tick (not fresh HID); live VS still
 * samples hardware once per tick before resolve (from the capture FIFO when NETMENU).
 *
 * **GGPO battle frame** (`syNetGgpoBattleFrame*`): mirrors `syNetInputGetTick()` — set together in `syNetInputSetTick` and
 * advanced only in `syNetInputAdvanceAuthoritativeSimTick()` at the end of each completed `scVSBattleFuncUpdate`.
 * Skew pacing may call `syNetInputFuncRead` without running `scVSBattleFuncUpdate`; neither advances.
 * Deterministic `syUtilsRandTime*` (when enabled) mixes `syNetInputGetTick()` with the PRNG seed, not wall time.
 *
 * `SYNetInputSource` distinguishes how a slot is fed; NetPeer fills remote rings via `syNetInputSetRemoteInput`.
 * Published history (`syNetInputGetHistoryFrame`) is what rollback compares against wire copies.
 *
 * Edge semantics (`button_tap` / `button_release`): derived in `syNetInputPublishFrame` from the current resolved
 * frame’s `buttons` vs the prior **published** sim tick (`frame->tick - 1`) in `sSYNetInputHistory` when present, not
 * from a transient slot shadow alone — so prediction/rollback cannot shift “pressed this frame” without changing the
 * stored per-tick hold stream. Optional trace: `SSB64_NETPLAY_INPUT_EDGE_DIAG` (see `netinput.c`).
 * Linux UDP: active VS sessions run `syNetPeerUpdateBattleGate()` at the start of `syNetInputFuncRead` (replacing the
 * pump-only path) so barrier + ingress + execution readiness are finalized **before** resolve/publish; inactive sessions
 * still use `syNetPeerPumpIngressBeforeInputRead()`.
 */

#include <PR/ultratypes.h>
#include <sys/controller.h>

#define SYNETINPUT_HISTORY_LENGTH 720
/* Full-match replay stream: 12 minutes @ 60 Hz (covers stock + sudden death). */
#define SYNETINPUT_REPLAY_MAX_FRAMES 43200
#define SYNETINPUT_REPLAY_MAGIC 0x53534E52 // SSNR
#define SYNETINPUT_REPLAY_VERSION 2

typedef enum SYNetInputSource /* How Resolve picks this slot’s `(buttons, stick)` before Publish. */
{
	nSYNetInputSourceLocal,
	nSYNetInputSourceRemoteConfirmed,
	nSYNetInputSourceRemoteGapFilled,
	nSYNetInputSourceRemotePredicted,
	nSYNetInputSourceSaved

} SYNetInputSource;

typedef struct SYNetInputFrame
{
	u32 tick;
	u16 buttons;
	s8 stick_x;
	s8 stick_y;
	u8 source;
	ub8 is_predicted;
	ub8 is_valid;

} SYNetInputFrame;

#if defined(PORT) && defined(SSB64_NETMENU)
/*
 * Remote authority ledger origin (Phase 1 of confirmed-authority contract).
 * Two writers only: wire (within sender auth frontier) and episode seal rows (win).
 * See docs/bugs/netplay_confirmed_publish_write_once_2026-07-12.md.
 */
#define SYNETINPUT_AUTH_LEDGER_ORIGIN_NONE 0U
#define SYNETINPUT_AUTH_LEDGER_ORIGIN_WIRE 1U
#define SYNETINPUT_AUTH_LEDGER_ORIGIN_SEAL 2U

/*
 * Provenance of a published History row (parallel ring — not part of SYNetInputFrame /
 * replay wire sizeof). Seal and freeze require gameplay-authoritative origins, not merely
 * Local && !predicted. See docs/bugs/netplay_history_provenance_2026-07-20.md.
 */
typedef enum SYNetInputHistoryProvenance
{
	nSYNetInputHistoryProvNone = 0,
	nSYNetInputHistoryProvPrediction = 1,
	nSYNetInputHistoryProvGameplay = 2,
	nSYNetInputHistoryProvLocalPublish = 3,
	nSYNetInputHistoryProvRemoteConfirmed = 4,
	nSYNetInputHistoryProvGapHold = 5,
	nSYNetInputHistoryProvLatch = 6

} SYNetInputHistoryProvenance;
#endif

typedef struct SYNetInputReplayMetadata /* Header written alongside recorded frame payloads (magic/version + rules). */
{
	u32 magic;
	u32 version;
	u32 scene_kind;
	u32 player_count;
	u32 stage_kind;
	u32 stocks;
	u32 time_limit;
	u32 item_switch;
	u32 item_toggles;
	u32 rng_seed;
	u8 game_type;
	u8 game_rules;
	u8 is_team_battle;
	u8 handicap;
	u8 is_team_attack;
	u8 is_stage_select;
	u8 damage_ratio;
	u8 item_appearance_rate;
	u8 is_not_teamshadows;
	u8 player_kinds[MAXCONTROLLERS];
	u8 fighter_kinds[MAXCONTROLLERS];
	u8 costumes[MAXCONTROLLERS];
	u8 teams[MAXCONTROLLERS];
	u8 handicaps[MAXCONTROLLERS];
	u8 levels[MAXCONTROLLERS];
	u8 shades[MAXCONTROLLERS];
	/* Netplay: sim *slots* (P1/P2 indices) for host vs guest humans in MATCH_CONFIG — not SDL/controller indices. */
	u8 netplay_sim_slot_host_hw;
	u8 netplay_sim_slot_client_hw;

} SYNetInputReplayMetadata;

extern void syNetInputReset(void);
extern void syNetInputStartVSSession(void); /* Calls Reset and reads netplay env (e.g. predict-neutral). */
#ifdef PORT
#if defined(SSB64_NETMENU)
/*
 * Solo training lab: same latch → delay-ring → publish path as net VS, without UDP peer.
 * `input_delay` is committed locally (use 0 to compare against online D>0). Ends with EndLocalLabSession.
 */
extern void syNetInputStartLocalLabSession(s32 local_player, u32 input_delay);
extern void syNetInputEndLocalLabSession(void);
extern sb32 syNetInputIsLocalLabActive(void);
extern s32 syNetInputGetLocalLabPlayer(void);
/*
 * `SSB64_STICK_SAMPLE_LOG=1`: one line per human fighter after each completed sim tick (training lab or net VS).
 * Logs sx/sy from published `gSYControllerDevices` plus `tap_stick_x` / `hold_stick_x` from FTStruct.
 */
extern void syNetInputMaybeLogStickSample(const char *mode);
/*
 * `SSB64_STICK_TAP_WITNESS=1`: after each completed sim tick, log anomalies linking published
 * `gSYControllerDevices` sticks to `FTStruct` tap counters / `input.pl` latch:
 * - `burned_dash` — `|sx_dev|>=56` and `tap_x>=3` (smash/dash window closed)
 * - `tap_max_held` — `|sx_dev|>=20` and `tap_x>=250` (near `FTINPUT_STICKBUFFER_TICS_MAX`)
 * - `device_pl_mismatch` — device stick != `fp->input.pl.stick_range` (device rewritten after ProcessInput)
 * Grep `STICK_TAP_WITNESS`.
 */
extern void syNetInputMaybeLogStickTapWitness(const char *mode);
/*
 * Wall-rate HID capture into a small FIFO (NETMENU). Call from PortPushFrame sim-skips and from
 * `syNetInputFuncRead` while the same sim tick is admission-held. Each accepted sim tick pops one
 * sample into the hardware latch — preserves stick trajectory across rare R-holds (not peak-hold).
 */
extern void syNetInputPollHardwareCaptureFifo(void);
#endif
/* Resets getenv caches used by netinput helpers; paired with `syNetPeerRefreshCachedNetplayEnvForNewMatch`. */
extern void syNetInputRefreshCachedNetplayEnvForNewMatch(void);
extern void syNetInputSetSessionIngressExtraPumpsOverride(s32 pumps);
extern void syNetInputSetSessionBundleRedundancyOverride(s32 redundancy);
extern void syNetInputClearSessionTransportOverrides(void);
#endif
extern u32 syNetInputGetTick(void); /* Monotonic sim index: advanced once per completed `scVSBattleFuncUpdate` (atomic with sim). */
extern void syNetInputSetTick(u32 tick);   /* Rollback resim rewinds this before synthetic `FuncRead` passes. */
extern sb32 syNetInputRollbackSimAdvanceAllowed(u32 next_sim_tick); /* Pure rollback cap: next_tick <= remote_sim + D + phase_lock. */
extern void syNetInputAdvanceAuthoritativeSimTick(void); /* Call once after each full VS battle sim step (not from FuncRead). */
#ifdef PORT
/*
 * Strict extra slack frames for `wire_cap` / `syNetPeerGetStrictRequiredWireTick` (`SSB64_NETPLAY_STRICT_SLACK_FRAMES`,
 * legacy aliases `SSB64_NET_DELAY_FRAMES` / `SSB64_NETPLAY_INPUT_EXEC_DELAY_FRAMES`, clamped 0–4; default 0).
 * **Independent** of `SSB64_NETPLAY_MATCH_INPUT_DELAY` (match delay sets committed `D` in netpeer only).
 * Both peers should use the same value. Matchmaking does not override this variable.
 */
extern int g_NetInputDelayFrames;
/*
 * When TRUE (default), strict contract + missing remote ring uses last-input prediction (`syNetInputMakePredictedFrame`).
 * Disable with `SSB64_NETPLAY_INPUT_PREDICTION=0`.
 */
extern sb32 g_UseInputPrediction;
extern int syNetInputGetStrictExtraSlack(void);
extern sb32 syNetInputGetUseInputPrediction(void);
/*
 * getenv `SSB64_NETPLAY_MATCH_INPUT_DELAY`: integer 0–99. When set, netpeer uses it for the **linked** committed wire
 * delay `D` (floor/ceiling / automatch). Strict slack (`g_NetInputDelayFrames`) is **not** derived from this; use
 * `SSB64_NETPLAY_STRICT_SLACK_FRAMES` separately. Returns -1 if unset.
 */
extern int syNetInputEnvGetMatchInputDelayOrNeg1(void);
/*
 * Authoritative wire admission (PORT): phase-locked wire admission is now the live VS path: exact `sim + D` remote-ring ownership, bounded
 * prediction from `syNetPeerEvaluateSharedCommitStep`, and partial local publish on stalls so outbound INPUT keeps flowing.
 */
extern int syNetInputGetInputContractTier(void);
/* Tier >= 1: wire-keyed authoritative admission + partial publish on miss. */
extern sb32 syNetInputAuthoritativeWireContractEnabled(void);
/* Always TRUE for live phase-locked VS. */
extern sb32 syNetInputStrictInputContractEnabled(void);
/*
 * Unified tick-commit (PORT VS): single admission policy for FuncRead wire admission vs battle sim / rollback scan.
 * FuncRead updates the FuncRead-phase cache each VS pass; consumers use `syNetTickCommitAllowsBattleSimFromLastFuncReadEvaluate`.
 */
typedef enum SYNetTickCommitPhase
{
	nSYNetTickCommitPhase_FuncReadExecGate,     /* Exec/bind/exec-sync + hard clock gate (before HID latch on E/W-hold). */
	nSYNetTickCommitPhase_FuncReadWireAdmission, /* Strict wire + legacy S/K; caller must have passed exec gate. */
	nSYNetTickCommitPhase_NetSlice              /* Skew net slice: exec+clock only (no wire re-check). */

} SYNetTickCommitPhase;

typedef struct SYNetTickCommitVerdict
{
	sb32 allow_full_input_publish;
	sb32 allow_battle_sim_step;
	sb32 suppress_scene_update;
	sb32 strict_partial_publish_local; /* TRUE: FuncRead must partial-publish local from latch (R/V paths). */
	sb32 strict_remote_stall_abort;    /* TRUE: strict R stall limit hit; FuncRead must not publish or advance. */
	char admission_letter;             /* P / E / W / R / V / S / K / A (strict abort) */

} SYNetTickCommitVerdict;

extern void syNetTickCommitEvaluate(u32 tick, SYNetTickCommitPhase phase, SYNetTickCommitVerdict *out);
extern sb32 syNetTickCommitAllowsBattleSimFromLastFuncReadEvaluate(void);
/*
 * Remote-ring presence by wire key (`SYNetInputFrame.tick` as staged by peer INPUT packets).
 * NETMENU: strict RemoteConfirmed only (provisional gap-fill does not count — shared-commit ring_ready).
 */
extern sb32 syNetInputHasRemoteInputForWireTick(s32 player, u32 wire_tick);
/*
 * Cached getenv for `SSB64_NETPLAY_STRICT_REMOTE_LEAD_BUFFER_TICKS` (reset in `syNetInputRefreshCachedNetplayEnvForNewMatch`).
 */
extern u32 syNetInputGetStrictRemoteLeadBufferTicks(void);
/*
 * TRUE after `syNetInputFuncRead` took the strict remote-miss path: partial local publish for wire, scene suppress
 * (skew net slice), then early return. `scVSBattleFuncUpdate` is skipped that task iteration; tick does not advance.
 * If full `scene_update` were reached with this flag (should not happen), `scVSBattleFuncUpdate` only runs
 * `syNetPeerUpdate` so the battle sim is not executed twice for the same tick.
 */
extern sb32 syNetInputStrictContractSkippedPublishThisPass(void);
/* `SSB64_NETPLAY_STRICT_STALL_DIAG` (0/1/2): strict R/V/W/S/K stall + net slice after rollback resim. */
extern void syNetInputMaybeLogStrictStallDiag(u32 tick, char admission_letter, sb32 suppress_scene, sb32 partial_publish,
                                              const char *phase_tag);
extern void syNetInputMaybeLogNetSliceDiag(u32 tick, sb32 allow_battle_sim, sb32 allow_net_update);
/* Cumulative FuncRead admission outcomes for active VS (non-resim): P=publish, E=exec hold, W=clock hold, S=stall, K=skew, R=strict remote missing. */
extern void syNetInputLogAdmissionStatsSummary(const char *tag, sb32 reset_counts_after);
#endif
#ifdef PORT
extern u32 syNetGgpoBattleFrameGet(void);        /* Same value as `syNetInputGetTick()` while in VS battle. */
extern void syNetGgpoBattleFrameAdvance(void);   /* No-op: frame advances in `syNetInputAdvanceAuthoritativeSimTick`. */
extern void syNetGgpoBattleFrameSet(u32 frame); /* Same as `syNetInputSetTick(frame)` (keeps frame counter aligned). */
#endif
extern void syNetInputSetSlotSource(s32 player, SYNetInputSource source);
extern SYNetInputSource syNetInputGetSlotSource(s32 player);
extern void syNetInputSetRemoteInput(s32 player, u32 tick, u16 buttons, s8 stick_x, s8 stick_y); /* NetPeer recv path fills remote ring. */
#ifdef PORT
extern sb32 syNetInputSetRemoteInputFromPacket(s32 player, u32 tick, u16 buttons, s8 stick_x, s8 stick_y,
                                               u32 packet_seq, u32 current_tick, s32 frame_index);
/* provisional=TRUE: wire tick above sender's auth frontier — store gap-filled, never RemoteConfirmed. */
extern sb32 syNetInputSetRemoteInputFromPacketEx(s32 player, u32 tick, u16 buttons, s8 stick_x, s8 stick_y,
                                                 u32 packet_seq, u32 current_tick, s32 frame_index, sb32 provisional);
#endif
extern void syNetInputSetSavedInput(s32 player, u32 tick, u16 buttons, s8 stick_x, s8 stick_y);
extern sb32 syNetInputGetHistoryFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);
extern sb32 syNetInputGetPublishedFrame(s32 player, SYNetInputFrame *out_frame);
extern u32 syNetInputGetHistoryChecksum(s32 player, u32 tick_begin, u32 frame_count);
extern u32 syNetInputAccumulateInputChecksum(u32 checksum, s32 player, SYNetInputFrame *frame);
extern u32 syNetInputGetHistoryInputChecksum(u32 frame_count);
extern u32 syNetInputGetHistoryInputValueChecksumForPlayer(s32 player, u32 tick_begin, u32 frame_count);
extern u32 syNetInputGetRemoteHistoryValueChecksumForPlayer(s32 player, u32 tick_begin, u32 frame_count);
extern void syNetInputGetHistoryInputValueChecksumWindow(u32 tick_begin, u32 frame_count, u32 *out_checksums,
                                                       u32 *out_combined_checksum);
#ifdef PORT
/* Cross-peer frame-commit input_digest: per-slot local authority + remote wire authority (not published-only). */
extern void syNetInputGetFrameCommitAuthorityChecksumWindow(u32 tick_begin, u32 frame_count, u32 *out_checksums,
							    u32 *out_combined_checksum);
/* Same folding as published-window checksum, over `sSYNetInputRemoteHistory` (wire-fed ring before resolve). */
extern void syNetInputGetRemoteHistoryValueChecksumWindow(u32 tick_begin, u32 frame_count, u32 *out_checksums,
                                                          u32 *out_combined_checksum);
/*
 * Diagnostic checksums: same windows as *ValueChecksumWindow but folds `source`, `is_predicted`, `is_valid`
 * per frame (FNV-style continuation after syNetInputAccumulateInputChecksum).
 */
extern void syNetInputGetHistoryInputDiagChecksumWindow(u32 tick_begin, u32 frame_count, u32 *out_checksums,
                                                        u32 *out_combined_checksum);
extern void syNetInputGetRemoteHistoryDiagChecksumWindow(u32 tick_begin, u32 frame_count, u32 *out_checksums,
                                                         u32 *out_combined_checksum);
/*
 * getenv `SSB64_NETPLAY_ABORT_ON_INPUT_MISMATCH`: 0 = off. Bit 1: trip on first published-history vs remote-ring
 * mismatch in the NetSync validation window. Bit 2: trip when rollback finds a history vs remote mismatch before resim.
 * Use 3 for both. First tick in [tick_begin, tick_begin+frame_count) where published history disagrees with remote ring on
 * sim inputs (tick/buttons/sticks) when presence differs or both sides valid — detects resolve/storage skew.
 * By default these paths **log only** (no process abort). getenv `SSB64_NETPLAY_ABORT_ON_INPUT_MISMATCH_FATAL`
 * (non-zero): also call abort() after logging so CI / bisect can hard-stop.
 * Returns FALSE if none. out_kind: 0=presence-only mismatch, 1=value mismatch.
 */
extern s32 syNetInputGetAbortOnInputMismatchMask(void);
extern sb32 syNetInputGetAbortOnInputMismatchFatal(void);
extern sb32 syNetInputDiagFindFirstPublishedRemoteMismatch(u32 tick_begin, u32 frame_count, s32 *out_player,
                                                           u32 *out_tick, u32 *out_kind);
/*
 * Subset of published-vs-remote-confirmed mismatches meant for operator logs: value mismatches on any slot, or
 * presence mismatches on remote human sim slots only (skips local-slot presence-only: no wire echo on same peer).
 */
extern sb32 syNetInputDiagFindFirstActionablePublishedRemoteMismatch(u32 tick_begin, u32 frame_count,
                                                                   s32 local_sim_slot, s32 extra_local_sim_slot,
                                                                   s32 *out_player, u32 *out_tick, u32 *out_kind);
/* One-shot ring snapshot after symmetric startup latch (tick 0..3 published vs remote-confirmed per slot). */
extern void syNetInputLogStartupInputBindingSnapshot(u32 agreed_tick);
extern void syNetInputLogDesyncNeedle(u32 validation_tick, u32 needle_tick, int trace_level);
/* Clear `last_confirmed` for NetPeer remote receive slots (call after bind / session slot wiring). */
extern void syNetInputClearRemoteSlotPredictionState(void);
/* Sticky per-sim-tick flag: TRUE when synchronize/publish used predicted remote input for that tick. */
extern void syNetInputNoteSimTickPredictedRemoteUsage(u32 sim_tick, const SYNetInputFrame *synced_frames);
extern sb32 syNetInputSimTickUsedPredictedRemote(u32 sim_tick);
/* Earliest sim tick in [from_tick, to_tick] where live sim consumed predicted remote input; ~(u32)0 if none. */
extern u32 syNetInputFindEarliestPredictedRemoteUsageInSpan(u32 from_tick, u32 to_tick);
/*
 * Earliest sim tick in [from_tick, to_tick] where any published human slot has non-neutral stick/buttons.
 * Symmetric when peers share input digests — prefer for FC input-agree reanchor over local predict flags.
 */
extern u32 syNetInputFindEarliestHumanNonNeutralInSpan(u32 from_tick, u32 to_tick);
/* Per-tick published vs sim-effective vs remote-confirmed row when SSB64_NETPLAY_DIVERGENCE_INPUT_LOG=1. */
extern void syNetInputMaybeLogDivergenceInputRow(u32 tick, const SYNetInputFrame *sim_consumed);
/* Bracketed fork bisect: SSB64_NETPLAY_INPUT_FORK_DIAG=1 (+ MIN/MAX sim ticks, default 515–530). */
extern sb32 syNetInputForkDiagWireInWindow(u32 wire_tick);
extern void syNetInputMaybeLogForkDiagIngressSlot(s32 player, u32 packet_seq, u32 wire_tick, u16 buttons, s8 stick_x,
                                                  s8 stick_y, u32 local_sim);
extern void syNetInputMaybeLogForkDiagSimRow(u32 tick, const SYNetInputFrame *sim_consumed);
#endif
extern void syNetInputSetRecordingEnabled(sb32 is_enabled);
extern sb32 syNetInputGetRecordingEnabled(void);
extern u32 syNetInputGetRecordedFrameCount(void);
extern void syNetInputClearReplayFrames(void);
extern sb32 syNetInputSetReplayFrame(s32 player, u32 tick, const SYNetInputFrame *frame);
extern sb32 syNetInputGetReplayFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);
extern u32 syNetInputGetReplayInputChecksum(void);
extern void syNetInputSetReplayMetadata(const SYNetInputReplayMetadata *metadata);
extern sb32 syNetInputGetReplayMetadata(SYNetInputReplayMetadata *out_metadata);
extern void syNetInputFuncRead(void); /* HID latch → synchronize all slots → publish → replay capture (tick++ is post-sim). */
#ifdef PORT
extern void syNetInputMakeLocalFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);
/* Live phase-locked VS: local hardware sampled at sim `t` is owned by future sim `t + D`; senders expose that ring here. */
extern sb32 syNetInputGetLocalDelayedFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);
extern void syNetInputPublishFrame(s32 player, SYNetInputFrame *frame);
/*
 * After `syNetInputFuncRead`, call once: returns TRUE if skew pacing held sim — taskman must skip `scene_update`
 * that tic so sim does not double-step while `sSYNetInputTick` stays unchanged.
 */
extern sb32 syNetInputTakeSuppressSceneUpdate(void);
#endif
extern void syNetInputRollbackPrepareForResim(u32 resim_start_tick); /* Reseed last_published + remote prediction seed before resim loop. */
#ifdef PORT
extern void syNetInputPublishSynchronizedTick(u32 tick); /* Resolve+publish only (pure rollback resim; no HID/network). */
extern void syNetInputRollbackReconcilePublishedFromRemote(u32 from_tick, u32 to_tick); /* Remote slots: wire-confirmed rows for [from,to). */
extern void syNetInputRollbackReconcilePublishedCommitWindow(u32 win_begin, u32 win_end); /* Stamp published before frame-commit digest. */
extern void syNetInputRollbackReconcileAfterResimCompleted(u32 mismatch_tick, u32 target_tick,
                                                         s32 correction_player); /* Post-resim published tail reconcile. */
extern void syNetInputRollbackResyncControllersAfterResim(u32 mismatch_tick,
                                                        u32 target_tick); /* Reseed last_published + republish remote after resim exit. */
extern sb32 syNetInputIsRemoteHumanSlot(s32 player); /* TRUE for opponent human sim slots (GGPO remote prediction/correction). */
extern void syNetInputRollbackReconcileResimSpan(u32 from_tick, u32 to_tick,
                                                  s32 correction_player); /* GGPO unified resim inputs: remote=wire, local=transmitted/per-tick published. */
/* Local authority: stamp published history from transmitted rows only (no published fallback). */
extern void syNetInputRollbackReconcilePeerSymmetricAuthority(s32 authority_slot, u32 from_tick, u32 to_tick);
/* Copy one reconciled frame into published history (episode FSM commit promote). */
extern void syNetInputStorePublishedHistoryFrame(s32 player, const SYNetInputFrame *frame);
#if defined(PORT) && defined(SSB64_NETMENU)
/*
 * Remote authority ledger (sim-tick keyed). Dual-write from wire confirm + seal commit;
 * TryGetRemoteConfirmed prefers ledger when present. Phase 1 — published ring still written.
 */
extern void syNetInputAuthorityLedgerCommitWire(s32 player, u32 sim_tick, const SYNetInputFrame *frame);
extern void syNetInputAuthorityLedgerCommitSeal(s32 player, u32 sim_tick, const SYNetInputFrame *frame);
extern sb32 syNetInputAuthorityLedgerTryGet(s32 player, u32 sim_tick, SYNetInputFrame *out_frame, u8 *out_origin);
/*
 * TRUE when predicting `sim_tick` would invent remote-human (0,0) after near-neutral
 * last_confirmed (no wire / soft-onset / last_nn). Shared-commit should cap the predict
 * window to ~D+1. Off during intro Wait / post-Go soft pacing.
 * See docs/bugs/netplay_zero_onset_predict_runway_peer_2026-07-20.md.
 */
extern sb32 syNetInputRemoteHumanZeroOnsetPredictRestrict(u32 sim_tick);
/*
 * SSB64_NETPLAY_STRICT_INPUT=1 — log-only input-authority witness. Enumerates confirmed-row
 * overwrites / fabricated confirms on the wire and published rings (migration to write-once
 * confirmed store). Summary flush + counter reset; called on VS session start.
 */
extern void syNetInputStrictWitnessLogMatchSummary(const char *when);
#endif
/* Episode seal: local-authority row from transmitted ring (wire source of truth), else non-predicted published. */
extern sb32 syNetInputCopyEpisodeLocalAuthoritySealFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);
extern sb32 syNetInputCopyEpisodeRemoteAuthoritySealFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);
extern sb32 syNetInputCopyEpisodeRemoteHumanSealFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);
extern void syNetInputPromoteRemoteHumanAuthorityPublished(s32 player, u32 tick);
extern void syNetInputPromoteAllRemoteHumanAuthoritySlots(u32 tick);
/* TRUE when every remote-human slot has strict wire-confirmed input for `sim_tick` (authoritative contract). */
extern sb32 syNetInputRemoteHumanWireReadyForSimTick(u32 sim_tick);
/* Pump ingress + rewrite remote `gSYControllerDevices` for `tick` immediately before battle sim (wire after FuncRead). */
extern sb32 syNetInputRepublishRemoteHumanControllersForTick(u32 tick);
extern u32 syNetInputFindEarliestRemoteAuthorityMismatch(s32 remote_slot, u32 from_tick, u32 to_tick);
/* All remote-human slots strict-confirmed at sim_tick AND published gameplay matches (snapshot load-safe promotion). */
extern sb32 syNetInputRemoteHumanPublishedMatchesConfirmedForSimTick(u32 sim_tick);
/* TRUE when episode FSM has sealed inputs and tick is inside the active span. */
extern sb32 syNetInputEpisodeSealedSpanBlocksPatch(u32 sim_tick);
/* Earliest t in [from,to) where published(slot,t) != transmitted(slot,t); ~(u32)0 if none. */
extern u32 syNetInputFindEarliestLocalAuthorityMismatch(s32 authority_slot, u32 from_tick, u32 to_tick);
/* Local authority: resolve transmitted → delay → HID latch; promote into published history for commit/seal. */
extern sb32 syNetInputResolveLocalAuthorityFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);
extern void syNetInputPromoteLocalAuthorityPublished(s32 player, u32 tick);
extern void syNetInputPromoteAllLocalAuthoritySlots(u32 tick);
extern void syNetInputMaybeLogFrameCommitLocalAuthorityDiag(u32 validation_tick, u32 win_begin);
extern void syNetInputMaybeLogFrameCommitSealLocalMismatch(u32 validation_tick, u32 win_begin, u32 win_end);
extern void syNetInputNoteTransmittedSimFrame(s32 player, const SYNetInputFrame *frame);
/* Read-only transmitted (wire-locked) row — egress append-only guard (never re-send changed gameplay). */
extern sb32 syNetInputTryGetTransmittedSimFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);
#if defined(SSB64_NETMENU)
/*
 * Feel-0: highest sim tick with a local gameplay sample. INPUT auth_wire_frontier must be derived
 * from this (not GetTick) so send-before-sample delay[sim] rows stay RemoteGapFilled until HID lands.
 * See docs/bugs/netplay_feel0_send_before_sample_release_skew_2026-07-13.md.
 */
extern u32 syNetInputGetLocalGameplayAuthSimTick(s32 player);
/* Gameplay or Transmitted row for bundle resend when published skipped neutral (release path). */
extern sb32 syNetInputTryGetLocalWireResendFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);
#endif
extern void syNetInputPatchPublishedFromRemoteConfirmed(s32 player, u32 wire_tick,
						      const SYNetInputFrame *confirmed);
/* Per-player published vs remote-confirmed mismatch summary for one NetSync validation window. */
extern void syNetInputLogPubVsRemoteWindowDiag(u32 validation_tick, u32 tick_begin, u32 frame_count,
                                               s32 local_sim_slot, s32 extra_local_sim_slot);
/*
 * TRUE when confirmed remote input differs enough from `old` to warrant GGPO rollback (buttons any change;
 * sticks when |delta| > deadband, neutral→non-neutral onset, horizontal sign flip, or large delta).
 * `correction_is_predicted`: use `SSB64_NETPLAY_GGPO_STICK_DEADBAND_PREDICT` (default 6) instead of
 * `SSB64_NETPLAY_GGPO_STICK_DEADBAND` (default 4).
 */
extern sb32 syNetInputGameplayCorrectionIsSignificantEx(const SYNetInputFrame *old, const SYNetInputFrame *new,
                                                      sb32 correction_is_predicted);
extern sb32 syNetInputGameplayCorrectionIsSignificant(const SYNetInputFrame *old, const SYNetInputFrame *new);
extern sb32 syNetInputShouldDeferPredictedAnalogCorrection(s32 player, u32 sim_tick, const SYNetInputFrame *published,
                                                         const SYNetInputFrame *remote);
#if defined(SSB64_NETMENU)
/*
 * Stick REPLACE → GGPO policy (feel-0 / late wire / pre-promote):
 * completed sim (`GetTick() > sim_tick`): buttons / release / non-micro stick → rewind;
 *   same-intent stick within COMPLETED_SIM_MICRO_DEADBAND (default 3) → Promote only;
 * release (analog → nearer/at neutral): always rewind (never onset-ahead defer);
 * else: confirmed-deadband significance (not predict-14), unless true onset-ahead defer.
 * `defer_published` may be NULL (falls back to `old_frame` for the defer check).
 */
extern sb32 syNetInputStickReplaceNeedsRewind(s32 player, u32 sim_tick, const SYNetInputFrame *old_frame,
                                              const SYNetInputFrame *wire, const SYNetInputFrame *defer_published);
/* Classify a published→wire delta for soak telemetry (static string). */
extern const char *syNetInputClassifyGgpoCorrection(const SYNetInputFrame *old_frame, const SYNetInputFrame *wire);
extern void syNetInputNoteGgpoCorrectionQueued(const SYNetInputFrame *old_frame, const SYNetInputFrame *wire);
extern void syNetInputLogGgpoCorrectionClassSummary(void);
#endif
/* TRUE when published vs remote sticks disagree with neutral vs analog (GGPO stick-mismatch recovery). */
extern sb32 syNetInputGgpoStickNeutralAnalogFlip(const SYNetInputFrame *published, const SYNetInputFrame *remote);
/* TRUE: patch published row only (skip GGPO resim) for isolated digital keyboard tap/release under delay. */
extern sb32 syNetInputShouldPatchDigitalTapWithoutRollback(s32 player, u32 sim_tick,
                                                            const SYNetInputFrame *published,
                                                            const SYNetInputFrame *remote);
#endif
extern sb32 syNetInputGetRemoteHistoryFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);
/* Post-publish simulation input only: valid after syNetInputPublishFrame for this tick. NULL if player out of range. */
extern SYController *syNetInputGetSimController(s32 player);

extern void syNetInputExportPeerConnectStatus(s32 *out_last_tick, u8 *out_disconnected, s32 count);

#ifdef PORT
extern void syNetInputDebugXorPublishedHistoryButtons(s32 player, u32 tick, u16 xor_mask);
extern void syNetInputRefreshPortHardwareUiLatch(void);
extern s32 syNetInputGetPortHardwareTapButtons(u32 buttons);
extern s32 syNetInputGetPortHardwareHoldButtons(u32 buttons);
extern s32 syNetInputGetPortHardwareStickUD(s8 range, sb32 up_or_down);
#endif

#endif /* _SYNETINPUT_H_ */
