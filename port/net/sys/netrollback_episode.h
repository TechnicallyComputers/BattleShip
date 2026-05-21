#ifndef _SYNETROLLBACK_EPISODE_H_
#define _SYNETROLLBACK_EPISODE_H_

#include <PR/ultratypes.h>
#include <ssb_types.h>

#include <sys/netinput.h>

#define SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN 64U
#define SYNETROLLBACK_EPISODE_REPLAY_LOG_MAX 64U
#define SYNETROLLBACK_EPISODE_EVENT_QUEUE_MAX 8U

/* Wire / chunk sizing for SYNETPEER_PACKET_EPISODE_SEAL_ROWS (must fit SYNETPEER_PACKET_RECV_MAX). */
/* Per-row: buttons(2) + sticks(2) + source + is_predicted + is_valid; tick from packet header. */
#define SYNETROLLBACK_EPISODE_SEAL_ROWS_WIRE_FRAME_BYTES 7U
#define SYNETROLLBACK_EPISODE_SEAL_ROWS_CHUNK_MAX 24U

typedef enum SYNetRollbackEpisodeFsmPhase
{
	nSYNetRollbackEpisodeFsmPhaseLive = 0,
	nSYNetRollbackEpisodeFsmPhaseSealInputs,
	nSYNetRollbackEpisodeFsmPhaseAwaitingBaseline,
	nSYNetRollbackEpisodeFsmPhaseReplay,
	nSYNetRollbackEpisodeFsmPhaseVerify,
	nSYNetRollbackEpisodeFsmPhaseCommit,
	nSYNetRollbackEpisodeFsmPhaseAbort
} SYNetRollbackEpisodeFsmPhase;

typedef enum SYNetRollbackEpisodeRole
{
	nSYNetRollbackEpisodeRoleInitiator = 0,
	nSYNetRollbackEpisodeRoleFollower
} SYNetRollbackEpisodeRole;

typedef enum SYNetRollbackEpisodeEventType
{
	nSYNetRollbackEpisodeEventNone = 0,
	nSYNetRollbackEpisodeEventInputMismatch,
	nSYNetRollbackEpisodeEventPeerSymmetric,
	nSYNetRollbackEpisodeEventStateDiverge,
	nSYNetRollbackEpisodeEventFrameCommit
} SYNetRollbackEpisodeEventType;

typedef struct SYNetRollbackEpisodeReplayLogEntry
{
	u32 tick;
	u32 input_digest;
	u32 figh;
	u32 item;
	u32 rng;
} SYNetRollbackEpisodeReplayLogEntry;

typedef struct SYNetRollbackEpisodeEvent
{
	SYNetRollbackEpisodeEventType type;
	s32 slot;
	u32 mismatch_tick;
	u32 target_tick;
	u32 load_tick;
	u32 epoch_id;
	sb32 follower_local_auth;
} SYNetRollbackEpisodeEvent;

typedef struct SYNetRollbackEpisodePostDigest
{
	u32 figh;
	u32 world;
	u32 item;
	u32 rng;
	u32 input_digest;
} SYNetRollbackEpisodePostDigest;

extern sb32 syNetRollbackEpisodeFsmEnabled(void);
extern void syNetRollbackEpisodeFsmSessionReset(void);

extern SYNetRollbackEpisodeFsmPhase syNetRollbackEpisodeFsmGetPhase(void);
extern sb32 syNetRollbackEpisodeFsmIsActive(void);
extern sb32 syNetRollbackEpisodeFsmIsResimulating(void);

extern u32 syNetRollbackEpisodeFsmGetEpochId(void);
extern u32 syNetRollbackEpisodeFsmGetMismatchTick(void);
extern u32 syNetRollbackEpisodeFsmGetLoadTick(void);
extern u32 syNetRollbackEpisodeFsmGetTargetTick(void);
extern s32 syNetRollbackEpisodeFsmGetCorrectedSlot(void);
extern sb32 syNetRollbackEpisodeFsmIsFromPeerNotify(void);

extern void syNetRollbackEpisodeFsmBegin(u32 epoch_id, u32 mismatch_tick, u32 load_tick, u32 target_tick,
					 s32 corrected_slot, sb32 initiator, sb32 from_peer_notify);
extern void syNetRollbackEpisodeFsmSetPhase(SYNetRollbackEpisodeFsmPhase phase);
extern void syNetRollbackEpisodeFsmSyncToLegacy(sb32 *out_resim_pending, sb32 *out_awaiting_baseline,
						sb32 *out_baseline_gate_open, u32 *out_mismatch, u32 *out_load,
						u32 *out_target, s32 *out_corrected, sb32 *out_from_peer_symmetric);

extern void syNetRollbackEpisodeSealInputs(u32 mismatch_tick, u32 target_tick, s32 correction_player);
/* Deeper baseline restart: update episode tuple, clear peer seal state, re-seal local rows. */
extern void syNetRollbackEpisodeResealForDeeperLoad(u32 load_tick, u32 mismatch_tick, u32 target_tick,
						    s32 correction_player);
extern sb32 syNetRollbackEpisodeInputsSealed(void);
extern sb32 syNetRollbackEpisodeTickInSealedSpan(u32 tick);
extern sb32 syNetRollbackEpisodeGetSealedFrame(s32 player, u32 tick, SYNetInputFrame *out_frame);

extern void syNetRollbackEpisodeReplayLogAppend(u32 tick, u32 input_digest, u32 figh, u32 item, u32 rng);
extern sb32 syNetRollbackEpisodeReplayLogGetPostDigest(SYNetRollbackEpisodePostDigest *out_digest);
extern u32 syNetRollbackEpisodeReplayLogTickInputDigest(u32 tick);
extern void syNetRollbackEpisodeReplayLogCheckInternalDiverge(void);

extern void syNetRollbackEpisodeCommitPromoteSealed(void);

extern sb32 syNetRollbackEpisodeFsmGetLiveSimCap(u32 *out_cap, u32 *out_cap_source);
extern void syNetRollbackEpisodeFsmSetPeerConvergence(u32 peer_target);
extern void syNetRollbackEpisodeFsmOnPostDiverge(void);
extern void syNetRollbackEpisodeFsmOnPostMatch(void);

extern void syNetRollbackEpisodeEnqueueEvent(const SYNetRollbackEpisodeEvent *event);
extern sb32 syNetRollbackEpisodeDrainNextEvent(SYNetRollbackEpisodeEvent *out_event);
extern sb32 syNetRollbackEpisodeHasPendingEvents(void);

extern sb32 syNetRollbackEpisodeFsmBaselineRequiresAnimMatch(void);

/* Bidirectional local-authority sealed rows (requires EPISODE_FSM). */
extern sb32 syNetRollbackEpisodeSealRowsExchangeEnabled(void);
extern void syNetRollbackEpisodeResetPeerSealRowsState(void);
extern void syNetRollbackEpisodeArmSealRowsSend(void);
/* Reset local seal-row send cursors while peer rows still missing (baseline-pump retransmit). */
extern void syNetRollbackEpisodePrepareSealRowsRetransmit(void);
extern s32 syNetRollbackEpisodeEnumerateLocalAuthoritySlots(s32 *out_slots, s32 max_slots, s32 *out_count);
extern s32 syNetRollbackEpisodeEnumerateRequiredPeerSealSlots(s32 *out_slots, s32 max_slots, s32 *out_count);
extern sb32 syNetRollbackEpisodeSlotIsLocalAuthority(s32 player);
extern sb32 syNetRollbackEpisodeSlotRequiresPeerSealRows(s32 player);
extern sb32 syNetRollbackEpisodeExportSealRowsChunk(s32 slot, u32 row_begin, u32 max_rows, SYNetInputFrame *out_frames,
						    u32 *out_row_count);
extern sb32 syNetRollbackEpisodeApplyPeerSealRowsChunk(u32 epoch_id, u32 mismatch_tick, u32 target_tick, s32 slot,
						       u32 row_begin, const SYNetInputFrame *rows, u32 row_count);
extern sb32 syNetRollbackEpisodePeerSealRowsComplete(s32 player);
extern sb32 syNetRollbackEpisodeAllPeerSealRowsComplete(void);
extern u32 syNetRollbackEpisodeGetMissingPeerSealSlotsMask(void);
extern sb32 syNetRollbackEpisodeTakeSealRowsChunkForSend(u32 *out_epoch_id, u32 *out_mismatch_tick, u32 *out_target_tick,
							 s32 *out_slot, u32 *out_row_begin, SYNetInputFrame *out_frames,
							 u32 max_frames, u32 *out_row_count);
extern void syNetRollbackEpisodeNoteSealRowsChunkSent(s32 slot, u32 row_begin, u32 row_count);
extern u32 syNetRollbackEpisodeComputeSpanInputDigest(u32 from_tick, u32 to_tick);
extern u32 syNetRollbackEpisodeComputeSlotSpanInputDigest(s32 player, u32 from_tick, u32 to_tick);
/* Snapshot span input digest once peer seal rows are complete (before forward replay). */
extern void syNetRollbackEpisodeFreezePostInputDigest(void);
extern sb32 syNetRollbackEpisodeGetFrozenPostInputDigest(u32 *out_input_digest);

#endif /* _SYNETROLLBACK_EPISODE_H_ */
