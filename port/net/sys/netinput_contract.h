#ifndef _SYNETINPUT_CONTRACT_H_
#define _SYNETINPUT_CONTRACT_H_

/*
 * Portable GGPO-style input replace contract — pure decision core.
 *
 * Standalone TU: stdint.h only. No engine includes, no rings, no env reads, no
 * logging, no fighter/status knowledge. Game- and engine-specific behavior enters
 * only through SYNetInputContractParams (numeric thresholds) and
 * SYNetInputContractHostGates (optional host callbacks, queried lazily in
 * decision order).
 *
 * Contract + soak-derived invariants: docs/netplay_input_contract_portable.md.
 * Export target: recomp-net rollback mode (rename prefix, keep semantics).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SYNetInputContractFrame
{
	uint32_t tick;
	uint16_t buttons;
	int8_t stick_x;
	int8_t stick_y;
	uint8_t is_predicted;
} SYNetInputContractFrame;

typedef struct SYNetInputContractParams
{
	/* Runway significance deadband for confirmed corrections (default 12). */
	uint32_t confirmed_deadband;
	/* Runway significance deadband for predicted corrections (default 14). */
	uint32_t predict_deadband;
	/* Completed-sim confirmed same-intent noise floor, Promote-only (default 3; 0 disables). */
	uint32_t micro_deadband;
	/* Completed-sim confirmed same-intent mag drift, Promote-only (default 12; must be >= micro). */
	uint32_t continuity_deadband;
	/* Looks-analog magnitude floor: both axes at/below => not analog (default 12). */
	uint32_t analog_min_mag;
	/* Per-axis magnitude at/above which sign conflicts break same-intent (default 8). */
	uint32_t same_intent_min_active;
	/* Predicted same-intent tolerance tail in significance (default 14). */
	uint32_t same_intent_tolerance;
	/* X facing-sign flip rewind threshold in significance (default 4). */
	uint32_t onset_facing_thresh;
	/* Per-axis delta that is always significant (default 40). */
	uint32_t onset_large_delta;
	/* Smash-class |x| threshold for the dash-gate X disagree proxy (default 56; <=0 disables). */
	int32_t dash_gate_min;
	/* Digital keyboard encoding: full deflection magnitude (default 85; 0 disables). */
	int8_t digital_axis_mag;
} SYNetInputContractParams;

/*
 * Host gates: all callbacks optional (NULL = portable default = gate never fires).
 * Queried lazily — a gate is only called when every portable condition ahead of it
 * in the decision order already holds, so implementations may log / count / peek
 * rings freely without changing side-effect order vs an inline implementation.
 */
typedef struct SYNetInputContractHostGates
{
	void *ctx;
	/* Equal frames, published predicted: pending host branch ticket forces rewind
	 * (e.g. BRANCH_DEFERRED Turn-vs-Dash with identical sticks). */
	uint8_t (*equal_predicted_force_rewind)(void *ctx);
	/* Buttons-equal stick delta that cannot affect the hashed sim (e.g. Dead*):
	 * promote wire silently instead of opening an episode. */
	uint8_t (*absorb_stick_replace)(void *ctx);
	/* Completed-sim same-intent: host move-protect promote (may exceed deadbands). */
	uint8_t (*protect_promote)(void *ctx, int32_t dx, int32_t dy, uint32_t micro_deadband,
	                           uint8_t old_predicted);
	/* Completed-sim same-intent: host blocks all deadband promotes (fragile aim window). */
	uint8_t (*block_deadband_promote)(void *ctx);
	/* Predicted row within continuity deadband: peer state watermark already agreed
	 * past this tick (frame-commit / master-hash confirm). Fail closed when NULL. */
	uint8_t (*hash_confirm_promote)(void *ctx);
	/* Runway predicted onset-ahead: host defers the correction until wire settles. */
	uint8_t (*defer_predicted_correction)(void *ctx);
} SYNetInputContractHostGates;

typedef enum SYNetInputContractDecision
{
	nSYNetInputContractRewind = 0,
	nSYNetInputContractRewindEqualDeferred,
	nSYNetInputContractPromoteEqual,
	nSYNetInputContractPromoteAbsorb,
	nSYNetInputContractPromoteProtect,
	nSYNetInputContractPromoteMicro,
	nSYNetInputContractPromoteContinuity,
	nSYNetInputContractPromoteHashConfirm,
	nSYNetInputContractPromoteDefer,
	nSYNetInputContractPromoteInsignificant
} SYNetInputContractDecision;

typedef enum SYNetInputContractCorrectionClass
{
	nSYNetInputContractClassButton = 0,
	nSYNetInputContractClassRelease,
	nSYNetInputContractClassOnsetFromZero,
	nSYNetInputContractClassMicroStick,
	nSYNetInputContractClassSameIntentContinuity,
	nSYNetInputContractClassRealStick
} SYNetInputContractCorrectionClass;

/* Fill BattleShip-current defaults (see docs/netplay_input_contract_portable.md table). */
void syNetInputContractParamsInitDefaults(SYNetInputContractParams *params);

/* 1 when the decision means "queue a rollback", 0 for every promote class. */
static inline uint8_t syNetInputContractDecisionIsRewind(SYNetInputContractDecision decision)
{
	return ((decision == nSYNetInputContractRewind) ||
	        (decision == nSYNetInputContractRewindEqualDeferred))
	           ? (uint8_t)1
	           : (uint8_t)0;
}

uint8_t syNetInputContractStickLooksAnalog(int8_t stick_x, int8_t stick_y,
                                           const SYNetInputContractParams *params);
uint8_t syNetInputContractStickSameAnalogIntent(int8_t ax, int8_t ay, int8_t bx, int8_t by,
                                                const SYNetInputContractParams *params);
uint8_t syNetInputContractStickDashGateDisagreeX(int8_t hold_x, int8_t wire_x,
                                                 const SYNetInputContractParams *params);

/* Analog -> neutral, or clearly shedding magnitude (confirmed_deadband slack). */
uint8_t syNetInputContractStickReplaceIsRelease(const SYNetInputContractFrame *old_frame,
                                                const SYNetInputContractFrame *wire,
                                                const SYNetInputContractParams *params);

/* Runway significance: buttons / onset-from-neutral / facing flip / large delta / deadband. */
uint8_t syNetInputContractCorrectionIsSignificant(const SYNetInputContractFrame *old_frame,
                                                  const SYNetInputContractFrame *new_frame,
                                                  uint8_t correction_is_predicted,
                                                  const SYNetInputContractParams *params);

/* Telemetry classification of a queued correction (no host gates involved). */
SYNetInputContractCorrectionClass
syNetInputContractClassifyCorrection(const SYNetInputContractFrame *old_frame,
                                     const SYNetInputContractFrame *wire,
                                     const SYNetInputContractParams *params);

/*
 * Master decision: published row vs authoritative/late wire row at one tick.
 * completed_sim: sim has already advanced past this tick (sim_now > tick).
 * published/wire must be non-NULL; gates may be NULL (all-portable defaults).
 * Decision order: docs/netplay_input_contract_portable.md.
 */
SYNetInputContractDecision
syNetInputContractStickReplaceDecide(const SYNetInputContractFrame *published,
                                     const SYNetInputContractFrame *wire, uint8_t completed_sim,
                                     const SYNetInputContractParams *params,
                                     const SYNetInputContractHostGates *gates);

#ifdef __cplusplus
}
#endif

#endif /* _SYNETINPUT_CONTRACT_H_ */
