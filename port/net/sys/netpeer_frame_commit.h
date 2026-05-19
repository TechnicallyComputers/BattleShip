#ifndef _SYNETPEER_FRAME_COMMIT_H_
#define _SYNETPEER_FRAME_COMMIT_H_

#include <PR/ultratypes.h>
#include <ssb_types.h>

/*
 * Cross-peer frame commit token: compact digest of what this peer believes was committed for the NetSync
 * validation window ending at `validation_tick` (same cadence as `syNetPeerLogNetSyncValidation`).
 */
typedef struct SYNetFrameCommitToken
{
	s32 frame_id;
	u32 input_digest;
	u32 slot_binding_hash;
	u32 tick_anchor;
	/* Subsystem hashes at validation_tick (same functions as NetSync line). */
	u32 fighter_digest;
	u32 world_digest;
	u32 item_digest;
	u32 rng_digest;

} SYNetFrameCommitToken;

void syNetFrameCommitBuildToken(SYNetFrameCommitToken *out, u32 validation_tick, u32 hist_win_begin, u32 hist_win_len,
				s32 local_sim_slot, s32 remote_sim_slot, s32 extra_local_sim_slot, s32 peer_sender_count,
				const u8 *peer_sender_slots);
u32 syNetFrameCommitHashSlotBindings(s32 local_sim_slot, s32 remote_sim_slot, s32 extra_local_sim_slot,
				     s32 peer_sender_count, const u8 *peer_sender_slots);
/* TRUE if frame_id or input_digest disagree (cross-peer). slot_binding is logged via out_delta_slot_binding but not compared. */
sb32 syNetFrameCommitTokensDesync(const SYNetFrameCommitToken *a, const SYNetFrameCommitToken *b,
				  sb32 *out_delta_frame_id, sb32 *out_delta_input_digest, sb32 *out_delta_slot_binding,
				  sb32 *out_delta_tick_anchor);
/* TRUE when fighter/world/item/rng digests disagree (sim state desync at validation boundary). */
sb32 syNetFrameCommitStateDigestsDiverge(const SYNetFrameCommitToken *a, const SYNetFrameCommitToken *b);

#endif /* _SYNETPEER_FRAME_COMMIT_H_ */
