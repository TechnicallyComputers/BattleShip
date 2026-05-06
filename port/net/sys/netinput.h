#ifndef _SYNETINPUT_H_
#define _SYNETINPUT_H_

/*
 * NetInput — authoritative per-slot controller frames aligned to `sSYNetInputTick`.
 *
 * Pipeline (normal VS): scenes call `syNetInputFuncRead` once per sim step. On PORT it snapshots HID into an internal
 * latch and clears `gSYControllerDevices[]` before resolve. It resolves each player’s frame for the current tick
 * (local HID from the latch, replay, remote-confirmed ring, or prediction), publishes into `gSYControllerDevices[]`,
 * snapshots `SYNETINPUT_HISTORY_LENGTH` rings, then bumps `sSYNetInputTick` only after the battle start barrier
 * releases (`syNetPeerCheckStartBarrierReleased`), so pre-fight ticks stay frozen together.
 *
 * `SYNetInputSource` distinguishes how a slot is fed; NetPeer fills remote rings via `syNetInputSetRemoteInput`.
 * Published history (`syNetInputGetHistoryFrame`) is what rollback compares against wire copies.
 * Linux UDP: `syNetPeerPumpIngressBeforeInputRead()` runs at the start of `syNetInputFuncRead` so the remote ring
 * is fresh before resolve/publish for the current tick.
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
extern u32 syNetInputGetTick(void);        /* Monotonic sim index advanced in `syNetInputFuncRead` after barrier. */
extern void syNetInputSetTick(u32 tick);   /* Rollback resim rewinds this before synthetic `FuncRead` passes. */
extern void syNetInputSetSlotSource(s32 player, SYNetInputSource source);
extern SYNetInputSource syNetInputGetSlotSource(s32 player);
extern void syNetInputSetRemoteInput(s32 player, u32 tick, u16 buttons, s8 stick_x, s8 stick_y); /* NetPeer recv path fills remote ring. */
extern void syNetInputSetSavedInput(s32 player, u32 tick, u16 buttons, s8 stick_x, s8 stick_y);
extern sb32 syNetInputGetHistoryFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);
extern sb32 syNetInputGetPublishedFrame(s32 player, SYNetInputFrame *out_frame);
extern u32 syNetInputGetHistoryChecksum(s32 player, u32 tick_begin, u32 frame_count);
extern u32 syNetInputGetHistoryInputChecksum(u32 frame_count);
extern u32 syNetInputGetHistoryInputValueChecksumForPlayer(s32 player, u32 tick_begin, u32 frame_count);
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
 * First tick in [tick_begin, tick_begin+frame_count) where published history disagrees with remote ring on
 * sim inputs (tick/buttons/sticks) when presence differs or both sides valid — detects resolve/storage skew.
 * Returns FALSE if none. out_kind: 0=presence-only mismatch, 1=value mismatch.
 */
extern sb32 syNetInputDiagFindFirstPublishedRemoteMismatch(u32 tick_begin, u32 frame_count, s32 *out_player,
                                                           u32 *out_tick, u32 *out_kind);
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
extern void syNetInputFuncRead(void); /* Resolve → Publish all slots → optional replay capture → bump tick post-barrier. */
extern void syNetInputRollbackPrepareForResim(u32 resim_start_tick); /* Reseed last_published + remote prediction seed before resim loop. */
extern sb32 syNetInputGetRemoteHistoryFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);
/* Post-publish simulation input only: valid after syNetInputPublishFrame for this tick. NULL if player out of range. */
extern SYController *syNetInputGetSimController(s32 player);

extern void syNetInputExportPeerConnectStatus(s32 *out_last_tick, u8 *out_disconnected, s32 count);

#ifdef PORT
extern void syNetInputDebugXorPublishedHistoryButtons(s32 player, u32 tick, u16 xor_mask);
#endif

#endif /* _SYNETINPUT_H_ */
