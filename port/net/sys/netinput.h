#ifndef _SYNETINPUT_H_
#define _SYNETINPUT_H_

/*
 * NetInput — authoritative per-slot controller frames aligned to `sSYNetInputTick`.
 *
 * Pipeline (normal VS): scenes call `syNetInputFuncRead` once per sim step. On PORT it snapshots HID into an internal
 * latch (once per `sSYNetInputTick`; **before** stall-until-remote / skew pacing so the sample is keyed to the local sim
 * tick, not wall/network timing) and clears `gSYControllerDevices[]` before resolve. It resolves each player’s frame for the current tick
 * (local HID from the latch, replay, remote-confirmed ring, or prediction), publishes into `gSYControllerDevices[]`,
 * snapshots `SYNETINPUT_HISTORY_LENGTH` rings. `sSYNetInputTick` advances only after a full `scVSBattleFuncUpdate`
 * (`syNetInputAdvanceAuthoritativeSimTick`). FuncRead returns early when the battle execution gate is closed
 * (`syNetPeerCheckStartBarrierReleased`), so pre-fight ticks stay frozen together.
 *
 * GGPO-shaped rule: rollback resim replays local slots from published history for that tick (not fresh HID); live VS still
 * samples hardware once per tick before resolve.
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
#define SYNETINPUT_REPLAY_MAX_FRAMES 21600
#define SYNETINPUT_REPLAY_MAGIC 0x53534E52 // SSNR
#define SYNETINPUT_REPLAY_VERSION 2

typedef enum SYNetInputSource /* How Resolve picks this slot’s `(buttons, stick)` before Publish. */
{
	nSYNetInputSourceLocal,
	nSYNetInputSourceRemoteConfirmed,
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
extern u32 syNetInputGetTick(void); /* Monotonic sim index: advanced once per completed `scVSBattleFuncUpdate` (atomic with sim). */
extern void syNetInputSetTick(u32 tick);   /* Rollback resim rewinds this before synthetic `FuncRead` passes. */
extern void syNetInputAdvanceAuthoritativeSimTick(void); /* Call once after each full VS battle sim step (not from FuncRead). */
#if defined(PORT) && !defined(_WIN32)
/*
 * Fixed execution delay for strict-contract readiness (independent of `SSB64_NETPLAY_DELAY` / committed wire input delay).
 * Both peers should use the same value (`SSB64_NET_DELAY_FRAMES` or `SSB64_NETPLAY_INPUT_EXEC_DELAY_FRAMES`, clamped 0–4; default **0** = pre-patch strict probe).
 * Matchmaking may assign `g_NetInputDelayFrames` after session start.
 */
extern int g_NetInputDelayFrames;
/*
 * When TRUE (default), strict contract + missing remote ring uses last-input prediction (`syNetInputMakePredictedFrame`).
 * Disable with `SSB64_NETPLAY_INPUT_PREDICTION=0`.
 */
extern sb32 g_UseInputPrediction;
extern int syNetInputGetExecutionDelayFrames(void);
extern sb32 syNetInputGetUseInputPrediction(void);
/*
 * getenv `SSB64_NETPLAY_STRICT_INPUT_CONTRACT`: when **`1`**, Linux UDP VS uses a **strict authoritative input baseline**:
 * exec / skew / catch-up do not gate admission; `scVSBattleFuncUpdate` bypasses `syNetPeerCheckBattleExecutionReady` while VS+strict.
 * Remote ring readiness uses sim tick `max(0, tick - g_NetInputDelayFrames)` when delay > 0 (delay 0 probes `tick` unchanged).
 * Strict partial local publish uses that executable sim tick for frame labels when delay > 0. If `g_UseInputPrediction`, predicted
 * remotes are written into `sSYNetInputRemoteHistory` at the wire key for the current sim tick. Optional stuck bypass:
 * `SSB64_NETPLAY_STRICT_R_STUCK_FORCE_DIAG=1` with execution delay 0.
 */
extern sb32 syNetInputStrictInputContractEnabled(void);
/*
 * TRUE after `syNetInputFuncRead` took the strict remote-miss path: partial local publish for wire, scene suppress
 * (skew net slice), then early return. `scVSBattleFuncUpdate` is skipped that task iteration; tick does not advance.
 * If full `scene_update` were reached with this flag (should not happen), `scVSBattleFuncUpdate` only runs
 * `syNetPeerUpdate` so the battle sim is not executed twice for the same tick.
 */
extern sb32 syNetInputStrictContractSkippedPublishThisPass(void);
/* Cumulative FuncRead admission outcomes for active VS (non-resim): P=publish, E=!execution, S=stall, K=skew, R=strict remote missing. */
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
extern void syNetInputSetSavedInput(s32 player, u32 tick, u16 buttons, s8 stick_x, s8 stick_y);
extern sb32 syNetInputGetHistoryFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);
extern sb32 syNetInputGetPublishedFrame(s32 player, SYNetInputFrame *out_frame);
extern u32 syNetInputGetHistoryChecksum(s32 player, u32 tick_begin, u32 frame_count);
extern u32 syNetInputGetHistoryInputChecksum(u32 frame_count);
extern u32 syNetInputGetHistoryInputValueChecksumForPlayer(s32 player, u32 tick_begin, u32 frame_count);
extern u32 syNetInputGetRemoteHistoryValueChecksumForPlayer(s32 player, u32 tick_begin, u32 frame_count);
extern void syNetInputGetHistoryInputValueChecksumWindow(u32 tick_begin, u32 frame_count, u32 *out_checksums,
                                                       u32 *out_combined_checksum);
#ifdef PORT
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
 * getenv `SSB64_NETPLAY_ABORT_ON_INPUT_MISMATCH`: 0 = off. Bit 1: abort on first published-history vs remote-ring
 * mismatch in the NetSync validation window. Bit 2: abort when rollback finds a history vs remote mismatch before resim.
 * Use 3 for both. First tick in [tick_begin, tick_begin+frame_count) where published history disagrees with remote ring on
 * sim inputs (tick/buttons/sticks) when presence differs or both sides valid — detects resolve/storage skew.
 * Returns FALSE if none. out_kind: 0=presence-only mismatch, 1=value mismatch.
 */
extern s32 syNetInputGetAbortOnInputMismatchMask(void);
extern sb32 syNetInputDiagFindFirstPublishedRemoteMismatch(u32 tick_begin, u32 frame_count, s32 *out_player,
                                                           u32 *out_tick, u32 *out_kind);
extern void syNetInputLogDesyncNeedle(u32 validation_tick, u32 needle_tick, int trace_level);
/* Clear `last_confirmed` for NetPeer remote receive slots (call after bind / session slot wiring). */
extern void syNetInputClearRemoteSlotPredictionState(void);
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
extern void syNetInputPublishFrame(s32 player, SYNetInputFrame *frame);
/*
 * After `syNetInputFuncRead`, call once: returns TRUE if skew pacing held sim — taskman must skip `scene_update`
 * that tic so sim does not double-step while `sSYNetInputTick` stays unchanged.
 */
extern sb32 syNetInputTakeSuppressSceneUpdate(void);
#endif
extern void syNetInputRollbackPrepareForResim(u32 resim_start_tick); /* Reseed last_published + remote prediction seed before resim loop. */
extern sb32 syNetInputGetRemoteHistoryFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);
/* Post-publish simulation input only: valid after syNetInputPublishFrame for this tick. NULL if player out of range. */
extern SYController *syNetInputGetSimController(s32 player);

extern void syNetInputExportPeerConnectStatus(s32 *out_last_tick, u8 *out_disconnected, s32 count);

#ifdef PORT
extern void syNetInputDebugXorPublishedHistoryButtons(s32 player, u32 tick, u16 xor_mask);
#endif

#endif /* _SYNETINPUT_H_ */
