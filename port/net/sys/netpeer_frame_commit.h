#ifndef _SYNETPEER_FRAME_COMMIT_H_
#define _SYNETPEER_FRAME_COMMIT_H_

#include <PR/ultratypes.h>
#include <ssb_types.h>

/*
 * Cross-peer frame commit token: compact digest of what this peer believes was committed for the NetSync
 * validation window ending at `validation_tick` (same cadence as `syNetPeerLogNetSyncValidation`).
 */
#define SYNET_FRAME_COMMIT_FIGHTER_SLOTS 4

typedef struct SYNetFrameCommitFighterDiag
{
	u32 valid;
	u32 fkind;
	u32 status_id;
	u32 motion_id;
	u32 status_total_tics;
	u32 hitlag_tics;
	u32 ga;
	u32 is_attack_active;
	u32 damage_queue;
	u32 topn_tx;
	u32 topn_ty;
	u32 coll_pos_diff_x;
	u32 coll_pos_diff_y;
	u32 vel_damage_air_x;
	u32 vel_damage_air_y;
	u32 fox_anim_frames;
	/* Folded into fhash_light; omitted from early diag → silent FC slot diverge (soak 699967527). */
	u32 tap_stick_x;
	u32 tap_stick_y;
	u32 hold_stick_x;
	u32 hold_stick_y;

} SYNetFrameCommitFighterDiag;

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
	u32 effect_digest;
	/* Per-slot commit diagnostics; light slot hashes name the diverging player, fields name likely fork source. */
	u32 fighter_slot_digest[SYNET_FRAME_COMMIT_FIGHTER_SLOTS];
	SYNetFrameCommitFighterDiag fighter_diag[SYNET_FRAME_COMMIT_FIGHTER_SLOTS];

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
/* TRUE when fighter/world/item/rng/effect digests disagree (sim state desync at validation boundary). */
sb32 syNetFrameCommitStateDigestsDiverge(const SYNetFrameCommitToken *a, const SYNetFrameCommitToken *b);
/*
 * TRUE when only item_digest differs while fighter/world/rng/effect agree (cross-OS item hash drift;
 * gameplay partitions matched — safe to treat as cosmetic for frame-commit authority).
 */
sb32 syNetFrameCommitItemOnlyCosmeticDiverge(const SYNetFrameCommitToken *a, const SYNetFrameCommitToken *b);
/*
 * TRUE when only fighter_digest differs while world/item/rng/effect and every fighter_slot_digest
 * agree cross-peer (stale ring hash_fighter; per-slot blobs are authoritative).
 */
sb32 syNetFrameCommitFighOnlyStaleRingDiverge(const SYNetFrameCommitToken *a, const SYNetFrameCommitToken *b);
/*
 * TRUE when frame-commit tokens agree on state digests but rollback snapshot at validation_tick-1 does not
 * match local token digests, or (fallback) live sim is more than one tick past validation and disagrees.
 */
sb32 syNetFrameCommitLiveHashGuardTripped(const SYNetFrameCommitToken *local, const SYNetFrameCommitToken *peer,
					  u32 validation_tick, u32 *out_diag_figh, u32 *out_diag_world);

#endif /* _SYNETPEER_FRAME_COMMIT_H_ */
