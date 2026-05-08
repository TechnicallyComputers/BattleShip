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

} SYNetFrameCommitToken;

void syNetFrameCommitBuildToken(SYNetFrameCommitToken *out, u32 validation_tick, u32 hist_win_begin, u32 hist_win_len,
				s32 local_sim_slot, s32 remote_sim_slot, s32 extra_local_sim_slot, s32 peer_sender_count,
				const u8 *peer_sender_slots);
u32 syNetFrameCommitHashSlotBindings(s32 local_sim_slot, s32 remote_sim_slot, s32 extra_local_sim_slot,
				     s32 peer_sender_count, const u8 *peer_sender_slots);
/* TRUE if any compared field disagrees (cross-peer structural desync for this validation). */
sb32 syNetFrameCommitTokensDesync(const SYNetFrameCommitToken *a, const SYNetFrameCommitToken *b,
				  sb32 *out_delta_frame_id, sb32 *out_delta_input_digest, sb32 *out_delta_slot_binding,
				  sb32 *out_delta_tick_anchor);

#endif /* _SYNETPEER_FRAME_COMMIT_H_ */
