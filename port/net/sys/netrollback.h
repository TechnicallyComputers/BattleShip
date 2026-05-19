#ifndef _SYNETROLLBACK_H_
#define _SYNETROLLBACK_H_

/*
 * NetRollback — optional input-based rewind for P2P VS (PORT; enable with `SSB64_NETPLAY_ROLLBACK`).
 *
 * After each completed sim tick, stores a **typed** world snapshot in a ring (`SSB64_NETPLAY_ROLLBACK_SNAPSHOT_FRAMES`,
 * default 32). During NetPeer transport, if published history disagrees with confirmed remote gameplay input inside
 * `SYNETROLLBACK_SCAN_WINDOW`, loads the snapshot for `mismatch_tick - 1` and resimulates forward to the frontier.
 *
 * Input mismatch scan compares **gameplay fields only** (tick, buttons, stick_x, stick_y) when both published history
 * and remote ring rows exist; `source` / `is_predicted` / `is_valid` are diagnostic and must not alone trigger resim when
 * buttons/sticks match. Optional `SSB64_NETPLAY_ROLLBACK_MISMATCH_REMOTE_WITHOUT_PUBLISHED` also flags remote-without-published.
 *
 * Each save records subsystem hashes (fighter, world, item, weapon, map, rng, camera, animation). The stored
 * animation digest uses `syNetSyncHashFighterAnimationStateForRollback()`; item/weapon digests use
 * `syNetSyncHashActiveItemsForRollback()` / `syNetSyncHashActiveWeaponsForRollback()` (omit unstable item/weapon
 * GObj ids when respawn allocates fresh ids). After load+apply,
 * `SSB64_NETPLAY_ROLLBACK_LOAD_HASH_VERIFY` (default on) recomputes and logs `LOAD_HASH_DRIFT` on mismatch.
 *
 * Rollback is **local input-timeline driven** (`netinput_timeline.c`, validated at scan time). GGPO input
 * corrections coalesce into one deferred rewind (earliest mismatch, latest frontier) and honor the same debounce
 * as scan mismatches (`SSB64_NETPLAY_ROLLBACK_DEBOUNCE_FRAMES`, default 3). Peer symmetric rollback is coupled by
 * Symmetric follower resim is on by default with rollback (`SSB64_NETPLAY_ROLLBACK_SYMMETRIC=0` disables;
 * `SSB64_NETPLAY_ROLLBACK_SYMMETRIC_DIAG=1` log-only). `resim_rng_verify` logs each completed resim by default
 * (`SSB64_NETPLAY_RESIM_RNG_VERIFY=0` disables). Debounce: `SSB64_NETPLAY_ROLLBACK_DEBOUNCE_FRAMES`.
 * Soft load-hash drift after heavy rollback: `SSB64_NETPLAY_ROLLBACK_LOAD_HASH_SOFT=1`. Anim-only drift
 * (fighter/world/RNG/item/wpn/map/cam match) always soft-continues — figatree can advance during load before verify.
 * Symmetric episodes: wire-locked resim target, post-load baseline gate before forward sim, no snapshot
 * save during resim/episode cooldown. While resim pending, coordination transport still sends INPUT padding
 * and `ROLLBACK_SYNC` (type 24) plus receives baseline/sync. Out of scope: full snapshot exchange, pure
 * independent GGPO without symmetric notify, hard-fail on anim-only load drift. See
 * `docs/netplay_rollback_test_matrix.md`.
 * Load failure restores a pre-load emergency snapshot and stops the VS session. See
 * `docs/netplay_rollback_refactor_contracts.md`.
 *
 * Ordering: `syNetRollbackAfterBattleUpdate` after battle sim; `syNetRollbackUpdate` from netpeer each frame (including
 * resim-pending slices that pump ingress + `AdvanceResimBudget` only).
 */

#include <PR/ultratypes.h>
#include <ssb_types.h>

#include <sys/netsession_params.h>
#include <sys/netpeer_frame_commit.h>

/* Bounded backward search vs remote ring; must stay in sync with `netrollback.c`. */
#define SYNETROLLBACK_SCAN_WINDOW 256

extern void syNetRollbackInit(void); /* Parses rollback env knobs once at startup. */
extern void syNetRollbackApplySessionNegotiated(const SYNetSessionParams *params);
extern void syNetRollbackStartVSSession(void);
extern void syNetRollbackStopVSSession(void);
extern sb32 syNetRollbackIsActive(void);   /* Env enabled AND VS session flagged. */
extern sb32 syNetRollbackIsResimulating(void); /* TRUE while nested `syNetRollbackRunResim` loop executes. */
/* Max sim tick allowed for live forward commit during peer rollback epoch (~0 = no epoch cap). */
extern sb32 syNetRollbackGetLiveSimCap(u32 *out_max_live_sim, u32 *out_cap_source);
/* TRUE when live battle advance must wait (peer epoch / pacing); replay uses AdvanceResimBudget. */
extern sb32 syNetRollbackShouldBlockLiveBattleAdvance(u32 sim_tick);
extern u32 syNetRollbackGetEpochId(void);

extern void syNetRollbackAfterBattleUpdate(void); /* Snapshot completed tick into ring (post-`scVSBattleFuncUpdate`). */
extern void syNetRollbackUpdate(void);            /* NetPeer: detect mismatch, load snapshot, resim forward. */

#ifdef PORT
extern void syNetRollbackDebugOnIncomingRemoteFrame(u32 *tick, u16 *buttons, s8 *stick_x, s8 *stick_y);
extern void syNetRollbackApplyPortSimPacing(u32 refresh_hz);
extern u32 syNetRollbackGetAppliedResimCount(void);
extern u32 syNetRollbackGetLoadFailCount(void);
/* Load snapshot for completed sim tick T (same ring index as SavePostTick(T)). */
extern sb32 syNetRollbackLoadSnapshotAfterCompletedTick(u32 completed_sim_tick);
/* TRUE when `SSB64_NETPLAY_PREDICTION_RECOVERY=1` (debug); default off — significant mismatches use full resim. */
extern sb32 syNetRollbackPredictionRecoveryEnabled(void);
/* Default on: short confirmed-only window after neutral↔analog GGPO correction (`STICK_MISMATCH_RECOVERY=0` off). */
extern sb32 syNetRollbackStickMismatchRecoveryEnabled(void);
/* TRUE after a predicted-input correction while rollback temporarily requires exact confirmed rows. */
extern sb32 syNetRollbackPredictionRecoveryRequiresConfirmed(u32 sim_tick);
/* Extend confirmed-only remote input window after neutral→motion stick correction. */
extern void syNetRollbackArmPredictionRecoveryForStickMismatch(u32 sim_tick, u32 frontier_tick);
/* Fill per-slot symmetric rollback ticks for INPUT peer_connect_status padding (-1 = none). */
extern void syNetRollbackExportPeerSymmetricNotify(s32 *out_tick_per_slot, s32 *out_target_tick_per_slot, s32 count);
/* Peer announced a correction on `slot` at `mismatch_tick` (24-bit wire); queue resim through `target_tick` (24-bit). */
/* FALSE when notify is stale or already covered by pending/deferred symmetric rollback. */
extern sb32 syNetRollbackAcceptPeerSymmetricRollbackNotify(s32 slot, u32 mismatch_tick, u32 target_tick);
extern void syNetRollbackOnPeerSymmetricRollbackNotify(s32 slot, u32 mismatch_tick, u32 target_tick);
/* Queue one resim for a remote input correction that arrived during an active resim span. */
extern void syNetRollbackDeferRemoteInputCorrection(s32 player, u32 sim_tick);
/* GGPO-style: confirmed input corrects speculative remote input already simulated at `sim_tick`. */
extern void syNetRollbackRequestInputCorrection(s32 player, u32 sim_tick);
/* Host/local authority: retransmit revised gameplay for `sim_tick`; queue symmetric + local resim. */
extern void syNetRollbackNotifyLocalAuthorityTransmitRevision(s32 player, u32 sim_tick);
/* FALSE during post-resim debounce for ticks at/after last committed mismatch (quiet OOO patch only). */
extern sb32 syNetRollbackShouldQueueGgpoCorrection(u32 sim_tick);
extern void syNetRollbackOnPeerBaselineDigest(u32 load_tick, u32 figh, u32 world, u32 item, u32 rng, u32 anim,
					      u32 weapon, u32 map, u32 camera, const u32 *fighter_slot);
extern void syNetRollbackPumpResimBaselineIfAwaiting(void);
/* Cross-peer frame-commit state digest mismatch: queue rollback from start of validation window. */
extern void syNetRollbackOnPeerFrameCommitStateMismatch(u32 validation_tick, const SYNetFrameCommitToken *local,
						       const SYNetFrameCommitToken *peer);
extern void syNetRollbackNotePeerBaselineDigestSent(void);
extern sb32 syNetRollbackTakePeerBaselineDigestForSend(u32 *out_load_tick, u32 *out_figh, u32 *out_world, u32 *out_item,
						     u32 *out_rng, u32 *out_anim, u32 *out_weapon, u32 *out_map,
						     u32 *out_camera, u32 *out_fighter_slot, s32 fighter_slot_count);
#endif

#endif /* _SYNETROLLBACK_H_ */
