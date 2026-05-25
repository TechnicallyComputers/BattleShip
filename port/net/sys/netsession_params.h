#ifndef _SYNETSESSION_PARAMS_H_
#define _SYNETSESSION_PARAMS_H_

#include <PR/ultratypes.h>
#include <ssb_types.h>

#define SYNETSESSION_PARAMS_WIRE_VERSION 2U

#define SYNETSESSION_ROLLBACK_FLAG_ENABLED  0x01U
#define SYNETSESSION_ROLLBACK_FLAG_SYMMETRIC 0x02U

typedef struct SYNetSessionParams
{
	u32 version;
	u32 rtt_ms;
	u8 input_delay;
	u8 phase_lock_ticks;
	u8 bundle_redundancy;
	u8 ingress_extra_pumps;
	u8 delay_adaptive_headroom;
	u8 rollback_snapshot_frames;
	u8 rollback_resim_ticks_per_frame;
	u8 strict_ring_fuzz_ticks;
	u8 rollback_flags;

} SYNetSessionParams;

/* TRUE when unset or non-zero (default on for netmenu VS). */
extern sb32 syNetSessionParamsAutoNegotiationEnabled(void);
/* TRUE when committed delay must not be replaced by RTT policy (`MATCH_INPUT_DELAY` or `SSB64_NETPLAY_DELAY`). */
extern sb32 syNetSessionParamsManualDelayOverrideActive(void);

extern void syNetSessionParamsResetForNewMatch(void);
/* RTT-tier frame-commit / NetSync validation cadence (ticks); both peers must match. */
extern u32 syNetSessionParamsComputeFrameCommitValidationTicks(u32 rtt_ms);
extern void syNetSessionParamsComputeFromRttMs(u32 rtt_ms, SYNetSessionParams *out_params);
extern sb32 syNetSessionParamsAreNegotiated(void);
extern void syNetSessionParamsGetNegotiated(SYNetSessionParams *out_params);
extern u32 syNetSessionParamsGetNegotiatedRttMs(void);

/*
 * Host: apply proposed params locally and arm startup `INPUT_DELAY_SYNC` target.
 * Guest: apply params received on the wire (must match host proposal).
 */
extern void syNetSessionParamsApplyNegotiated(const SYNetSessionParams *params, const char *tag);

/* Effective per-match values (negotiated when auto is on, else env / defaults). */
extern u32 syNetSessionParamsGetEffectiveInputDelay(void);
extern u32 syNetSessionParamsGetEffectivePhaseLockTicks(void);
extern u32 syNetSessionParamsGetEffectiveBundleRedundancy(void);
extern u32 syNetSessionParamsGetEffectiveIngressExtraPumps(void);
extern u32 syNetSessionParamsGetEffectiveDelayCeil(void);
extern u32 syNetSessionParamsGetEffectiveRollbackSnapshotFrames(void);
extern u32 syNetSessionParamsGetEffectiveRollbackResimTicksPerFrame(void);
extern u32 syNetSessionParamsGetEffectiveStrictRingFuzzTicks(void);
extern u32 syNetSessionParamsGetEffectiveFrameCommitValidationTicks(void);
extern sb32 syNetSessionParamsRollbackEnabled(void);
extern sb32 syNetSessionParamsRollbackSymmetricEnabled(void);

#endif /* _SYNETSESSION_PARAMS_H_ */
