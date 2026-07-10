#ifndef _SYNETDESYNCCLASSIFIER_H_
#define _SYNETDESYNCCLASSIFIER_H_

#include <PR/ultratypes.h>
#include <ssb_types.h>

#ifdef PORT
#include <sys/netpeer_frame_commit.h>
/*
 * Single-pass desync root classifier (INPUT / COMMIT / SNAPSHOT / SIM). Linux UDP implementation; Windows PORT builds
 * compile no-op stubs in `netdesyncclassifier.c`.
 *
 * Enable with `SSB64_NETPLAY_DESYNC_CLASSIFIER=1` (accumulate evidence during VS; print `SSB64 DESYNC REPORT` on
 * `syNetPeerStopVSSession`). `=2` additionally logs when the leading category would change (noisy).
 *
 * COMMIT is driven by cross-peer `SYNetFrameCommitToken` exchange (`SSB64_NETPLAY_FRAME_COMMIT_TOKEN`, default on).
 * Bootstrap `input_bind` / `battle_exec_sync` mismatches are logged as `bootstrap_bind_*` diagnostics only.
 * E/S/K admission bumps are `commit_delay_*` only.
 */
void syNetDesyncClassifierReset(void);
void syNetDesyncClassifierOnNetSyncValidation(u32 validation_tick, u32 hist_win_begin, u32 hist_win_len, u32 inp_all,
					      u32 fighter_hash, u32 map_hash, u32 late_frames, u32 seq_gaps_total);
void syNetDesyncClassifierOnAdmissionPath(u32 sim_tick, char path);
void syNetDesyncClassifierOnFrameIdentityMismatch(u32 tick);
void syNetDesyncClassifierOnFrameCommitTokenMismatch(u32 validation_tick, const SYNetFrameCommitToken *local,
						     const SYNetFrameCommitToken *peer);
void syNetDesyncClassifierOnFrameCommitValidationSent(u32 validation_tick, u32 validations_since_peer_reset);
void syNetDesyncClassifierOnFrameCommitPeerTokenReceived(u32 validation_tick);
void syNetDesyncClassifierOnLoadHashDrift(u32 tick);
void syNetDesyncClassifierOnPeerSnapshotDiverge(u32 load_tick);
void syNetDesyncClassifierOnRollbackInputMismatch(u32 mismatch_tick);
void syNetDesyncClassifierOnVerifyStrictUnchanged(u32 mismatch_tick);
void syNetDesyncClassifierEmitFrameCommitReportOnVsStop(void);
void syNetDesyncClassifierEmitReportOnVsStop(void);
#else
#define syNetDesyncClassifierReset() ((void)0)
#define syNetDesyncClassifierOnNetSyncValidation(a, b, c, d, e, f, g, h) ((void)0)
#define syNetDesyncClassifierOnAdmissionPath(a, b) ((void)0)
#define syNetDesyncClassifierOnFrameIdentityMismatch(a) ((void)0)
#define syNetDesyncClassifierOnFrameCommitTokenMismatch(a, b, c) ((void)0)
#define syNetDesyncClassifierOnFrameCommitValidationSent(a, b) ((void)0)
#define syNetDesyncClassifierOnFrameCommitPeerTokenReceived(a) ((void)0)
#define syNetDesyncClassifierOnLoadHashDrift(a) ((void)0)
#define syNetDesyncClassifierOnPeerSnapshotDiverge(a) ((void)0)
#define syNetDesyncClassifierOnRollbackInputMismatch(a) ((void)0)
#define syNetDesyncClassifierOnVerifyStrictUnchanged(a) ((void)0)
#define syNetDesyncClassifierEmitFrameCommitReportOnVsStop() ((void)0)
#define syNetDesyncClassifierEmitReportOnVsStop() ((void)0)
#endif /* PORT */

#endif /* _SYNETDESYNCCLASSIFIER_H_ */
