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
 * Rollback is **local input-timeline driven** (`netinput_timeline.c`). Peer symmetric notices default off
 * (`SSB64_NETPLAY_ROLLBACK_SYMMETRIC=1` for legacy coupled resim; `SSB64_NETPLAY_ROLLBACK_SYMMETRIC_DIAG=1` logs only).
 * Load failure restores a pre-load emergency snapshot and stops the VS session. See
 * `docs/netplay_rollback_refactor_contracts.md`.
 *
 * Ordering: `syNetRollbackAfterBattleUpdate` after battle sim; `syNetRollbackUpdate` from netpeer when not resimulating.
 */

#include <PR/ultratypes.h>
#include <ssb_types.h>

#include <sys/netsession_params.h>

/* Bounded backward search vs remote ring; must stay in sync with `netrollback.c`. */
#define SYNETROLLBACK_SCAN_WINDOW 256

extern void syNetRollbackInit(void); /* Parses rollback env knobs once at startup. */
extern void syNetRollbackApplySessionNegotiated(const SYNetSessionParams *params);
extern void syNetRollbackStartVSSession(void);
extern void syNetRollbackStopVSSession(void);
extern sb32 syNetRollbackIsActive(void);   /* Env enabled AND VS session flagged. */
extern sb32 syNetRollbackIsResimulating(void); /* TRUE while nested `syNetRollbackRunResim` loop executes. */

extern void syNetRollbackAfterBattleUpdate(void); /* Snapshot completed tick into ring (post-`scVSBattleFuncUpdate`). */
extern void syNetRollbackUpdate(void);            /* NetPeer: detect mismatch, load snapshot, resim forward. */

#ifdef PORT
extern void syNetRollbackDebugOnIncomingRemoteFrame(u32 *tick, u16 *buttons, s8 *stick_x, s8 *stick_y);
extern void syNetRollbackApplyPortSimPacing(u32 refresh_hz);
extern u32 syNetRollbackGetAppliedResimCount(void);
extern u32 syNetRollbackGetLoadFailCount(void);
/* Load snapshot for completed sim tick T (same ring index as SavePostTick(T)). */
extern sb32 syNetRollbackLoadSnapshotAfterCompletedTick(u32 completed_sim_tick);
/* TRUE after a predicted-input correction while rollback temporarily requires exact confirmed rows. */
extern sb32 syNetRollbackPredictionRecoveryRequiresConfirmed(u32 sim_tick);
/* Fill per-slot symmetric rollback ticks for INPUT peer_connect_status padding (-1 = none). */
extern void syNetRollbackExportPeerSymmetricNotify(s32 *out_tick_per_slot, s32 *out_target_tick_per_slot, s32 count);
/* Peer announced a correction on `slot` at `mismatch_tick` (24-bit wire); queue resim through `target_tick` (24-bit). */
extern void syNetRollbackOnPeerSymmetricRollbackNotify(s32 slot, u32 mismatch_tick, u32 target_tick);
extern void syNetRollbackOnPeerBaselineDigest(u32 load_tick, u32 figh, u32 world, u32 item, u32 rng, u32 anim,
					      u32 weapon, u32 map, u32 camera);
extern void syNetRollbackNotePeerBaselineDigestSent(void);
extern sb32 syNetRollbackTakePeerBaselineDigestForSend(u32 *out_load_tick, u32 *out_figh, u32 *out_world, u32 *out_item,
						     u32 *out_rng, u32 *out_anim, u32 *out_weapon, u32 *out_map,
						     u32 *out_camera);
#endif

#endif /* _SYNETROLLBACK_H_ */
