/*
 * Portable GGPO-style input replace contract — pure decision core.
 *
 * Behavior mirrors the soak-hardened policy previously inlined in
 * port/net/sys/netinput.c (syNetInputStickReplaceNeedsRewind and helpers).
 * Every branch here is load-bearing against a soak failure documented in
 * docs/netplay_input_contract_portable.md — do not relax without a new soak.
 *
 * Keep this TU free of engine includes so it can be exported verbatim
 * (recomp-net rollback mode).
 */

#include <sys/netinput_contract.h>

#include <stddef.h>

#define SYNETINPUT_CONTRACT_TRUE ((uint8_t)1)
#define SYNETINPUT_CONTRACT_FALSE ((uint8_t)0)

void syNetInputContractParamsInitDefaults(SYNetInputContractParams *params)
{
	if (params == NULL)
	{
		return;
	}
	params->confirmed_deadband = 12U;
	params->predict_deadband = 14U;
	params->micro_deadband = 3U;
	params->continuity_deadband = 12U;
	params->analog_min_mag = 12U;
	params->same_intent_min_active = 8U;
	params->same_intent_tolerance = 14U;
	params->onset_facing_thresh = 4U;
	params->onset_large_delta = 40U;
	params->dash_gate_min = 56;
	params->digital_axis_mag = 85;
}

static int32_t syNetInputContractAbsS8Diff(int8_t a, int8_t b)
{
	int32_t d;

	d = (int32_t)a - (int32_t)b;
	if (d < 0)
	{
		return -d;
	}
	return d;
}

static int32_t syNetInputContractStickSign(int8_t axis)
{
	if (axis > 0)
	{
		return 1;
	}
	if (axis < 0)
	{
		return -1;
	}
	return 0;
}

/* Digital keyboard encoding: full stick deflection on one axis. */
static uint8_t syNetInputContractStickAxisIsDigital(int8_t axis, const SYNetInputContractParams *params)
{
	if (params->digital_axis_mag == 0)
	{
		return SYNETINPUT_CONTRACT_FALSE;
	}
	return ((axis == params->digital_axis_mag) || (axis == (int8_t)-params->digital_axis_mag))
	           ? SYNETINPUT_CONTRACT_TRUE
	           : SYNETINPUT_CONTRACT_FALSE;
}

static uint8_t syNetInputContractSticksNearNeutral(const SYNetInputContractFrame *frame, uint32_t deadband)
{
	if (frame == NULL)
	{
		return SYNETINPUT_CONTRACT_FALSE;
	}
	return ((syNetInputContractAbsS8Diff(frame->stick_x, 0) <= (int32_t)deadband) &&
	        (syNetInputContractAbsS8Diff(frame->stick_y, 0) <= (int32_t)deadband))
	           ? SYNETINPUT_CONTRACT_TRUE
	           : SYNETINPUT_CONTRACT_FALSE;
}

uint8_t syNetInputContractStickLooksAnalog(int8_t stick_x, int8_t stick_y,
                                           const SYNetInputContractParams *params)
{
	if ((syNetInputContractAbsS8Diff(stick_x, 0) <= (int32_t)params->analog_min_mag) &&
	    (syNetInputContractAbsS8Diff(stick_y, 0) <= (int32_t)params->analog_min_mag))
	{
		return SYNETINPUT_CONTRACT_FALSE;
	}
	if ((syNetInputContractStickAxisIsDigital(stick_x, params) != SYNETINPUT_CONTRACT_FALSE) ||
	    (syNetInputContractStickAxisIsDigital(stick_y, params) != SYNETINPUT_CONTRACT_FALSE))
	{
		return SYNETINPUT_CONTRACT_FALSE;
	}
	return SYNETINPUT_CONTRACT_TRUE;
}

uint8_t syNetInputContractStickSameAnalogIntent(int8_t ax, int8_t ay, int8_t bx, int8_t by,
                                                const SYNetInputContractParams *params)
{
	int32_t min_active;

	if ((syNetInputContractStickLooksAnalog(ax, ay, params) == SYNETINPUT_CONTRACT_FALSE) ||
	    (syNetInputContractStickLooksAnalog(bx, by, params) == SYNETINPUT_CONTRACT_FALSE))
	{
		return SYNETINPUT_CONTRACT_FALSE;
	}
	min_active = (int32_t)params->same_intent_min_active;
	/*
	 * Use >= min_active so |s|==min_active participates in sign checks. Strict > let
	 * (61,8)->(61,-9) count as same-intent through a Y flip (soak 932522105 @1069).
	 */
	if ((syNetInputContractAbsS8Diff(ax, 0) >= min_active) ||
	    (syNetInputContractAbsS8Diff(bx, 0) >= min_active))
	{
		if ((syNetInputContractAbsS8Diff(ax, 0) >= min_active) &&
		    (syNetInputContractAbsS8Diff(bx, 0) >= min_active) &&
		    (syNetInputContractStickSign(ax) != syNetInputContractStickSign(bx)))
		{
			return SYNETINPUT_CONTRACT_FALSE;
		}
	}
	if ((syNetInputContractAbsS8Diff(ay, 0) >= min_active) ||
	    (syNetInputContractAbsS8Diff(by, 0) >= min_active))
	{
		if ((syNetInputContractAbsS8Diff(ay, 0) >= min_active) &&
		    (syNetInputContractAbsS8Diff(by, 0) >= min_active) &&
		    (syNetInputContractStickSign(ay) != syNetInputContractStickSign(by)))
		{
			return SYNETINPUT_CONTRACT_FALSE;
		}
	}
	return SYNETINPUT_CONTRACT_TRUE;
}

static uint8_t syNetInputContractStickDashGateActiveX(int8_t stick_x, const SYNetInputContractParams *params)
{
	return (syNetInputContractAbsS8Diff(stick_x, 0) >= params->dash_gate_min) ? SYNETINPUT_CONTRACT_TRUE
	                                                                          : SYNETINPUT_CONTRACT_FALSE;
}

/*
 * Turn->Dash product proxy: |sx| crosses the smash threshold and/or X sign flips while
 * either side is smash-class. Disabled when dash_gate_min <= 0.
 */
uint8_t syNetInputContractStickDashGateDisagreeX(int8_t hold_x, int8_t wire_x,
                                                 const SYNetInputContractParams *params)
{
	uint8_t hold_dash;
	uint8_t wire_dash;
	int32_t hold_sign;
	int32_t wire_sign;

	if (params->dash_gate_min <= 0)
	{
		return SYNETINPUT_CONTRACT_FALSE;
	}
	hold_dash = syNetInputContractStickDashGateActiveX(hold_x, params);
	wire_dash = syNetInputContractStickDashGateActiveX(wire_x, params);
	if (hold_dash != wire_dash)
	{
		return SYNETINPUT_CONTRACT_TRUE;
	}
	hold_sign = syNetInputContractStickSign(hold_x);
	wire_sign = syNetInputContractStickSign(wire_x);
	if ((hold_dash != SYNETINPUT_CONTRACT_FALSE) && (hold_sign != 0) && (wire_sign != 0) &&
	    (hold_sign != wire_sign))
	{
		return SYNETINPUT_CONTRACT_TRUE;
	}
	return SYNETINPUT_CONTRACT_FALSE;
}

/*
 * Release (published analog -> wire nearer/at neutral) must never be treated as an
 * onset-ahead defer, and always rewinds on the completed-sim path.
 */
uint8_t syNetInputContractStickReplaceIsRelease(const SYNetInputContractFrame *old_frame,
                                                const SYNetInputContractFrame *wire,
                                                const SYNetInputContractParams *params)
{
	uint32_t confirmed_db;
	int32_t old_mag;
	int32_t wire_mag;

	if ((old_frame == NULL) || (wire == NULL))
	{
		return SYNETINPUT_CONTRACT_FALSE;
	}
	confirmed_db = params->confirmed_deadband;
	if (syNetInputContractSticksNearNeutral(old_frame, confirmed_db) != SYNETINPUT_CONTRACT_FALSE)
	{
		return SYNETINPUT_CONTRACT_FALSE;
	}
	if (syNetInputContractSticksNearNeutral(wire, confirmed_db) != SYNETINPUT_CONTRACT_FALSE)
	{
		return SYNETINPUT_CONTRACT_TRUE;
	}
	old_mag = syNetInputContractAbsS8Diff(old_frame->stick_x, 0);
	if (syNetInputContractAbsS8Diff(old_frame->stick_y, 0) > old_mag)
	{
		old_mag = syNetInputContractAbsS8Diff(old_frame->stick_y, 0);
	}
	wire_mag = syNetInputContractAbsS8Diff(wire->stick_x, 0);
	if (syNetInputContractAbsS8Diff(wire->stick_y, 0) > wire_mag)
	{
		wire_mag = syNetInputContractAbsS8Diff(wire->stick_y, 0);
	}
	/* Clearly shedding magnitude (not a same-intent ramp up). */
	return ((wire_mag + (int32_t)confirmed_db) < old_mag) ? SYNETINPUT_CONTRACT_TRUE
	                                                      : SYNETINPUT_CONTRACT_FALSE;
}

/*
 * Predicted +/-digital on one axis vs remote neutral/partial on that axis — heuristic
 * promotion artifact, not a committed digital jump. Suppresses oversized GGPO resim
 * when promotion already slipped through.
 */
static uint8_t syNetInputContractFalseDigitalHeuristicMismatch(const SYNetInputContractFrame *published,
                                                               const SYNetInputContractFrame *remote,
                                                               const SYNetInputContractParams *params)
{
	int32_t weak_thresh;

	if ((published == NULL) || (remote == NULL))
	{
		return SYNETINPUT_CONTRACT_FALSE;
	}
	weak_thresh = 25;
	if ((syNetInputContractStickAxisIsDigital(published->stick_y, params) != SYNETINPUT_CONTRACT_FALSE) &&
	    (syNetInputContractStickAxisIsDigital(remote->stick_y, params) == SYNETINPUT_CONTRACT_FALSE) &&
	    (syNetInputContractAbsS8Diff(published->stick_x, 0) <= 14) &&
	    (syNetInputContractAbsS8Diff(remote->stick_x, 0) <= 14) &&
	    (syNetInputContractAbsS8Diff(remote->stick_y, 0) <= weak_thresh))
	{
		return SYNETINPUT_CONTRACT_TRUE;
	}
	if ((syNetInputContractStickAxisIsDigital(published->stick_x, params) != SYNETINPUT_CONTRACT_FALSE) &&
	    (syNetInputContractStickAxisIsDigital(remote->stick_x, params) == SYNETINPUT_CONTRACT_FALSE) &&
	    (syNetInputContractAbsS8Diff(published->stick_y, 0) <= 14) &&
	    (syNetInputContractAbsS8Diff(remote->stick_y, 0) <= 14) &&
	    (syNetInputContractAbsS8Diff(remote->stick_x, 0) <= weak_thresh))
	{
		return SYNETINPUT_CONTRACT_TRUE;
	}
	return SYNETINPUT_CONTRACT_FALSE;
}

uint8_t syNetInputContractCorrectionIsSignificant(const SYNetInputContractFrame *old_frame,
                                                  const SYNetInputContractFrame *new_frame,
                                                  uint8_t correction_is_predicted,
                                                  const SYNetInputContractParams *params)
{
	uint32_t deadband;
	uint32_t facing_thresh;
	uint32_t large_delta;

	if ((old_frame == NULL) || (new_frame == NULL))
	{
		return SYNETINPUT_CONTRACT_TRUE;
	}
	if (old_frame->buttons != new_frame->buttons)
	{
		return SYNETINPUT_CONTRACT_TRUE;
	}
	if ((correction_is_predicted != SYNETINPUT_CONTRACT_FALSE) &&
	    (syNetInputContractFalseDigitalHeuristicMismatch(old_frame, new_frame, params) !=
	     SYNETINPUT_CONTRACT_FALSE))
	{
		return SYNETINPUT_CONTRACT_FALSE;
	}
	if (correction_is_predicted != SYNETINPUT_CONTRACT_FALSE)
	{
		deadband = params->predict_deadband;
	}
	else
	{
		deadband = params->confirmed_deadband;
	}
	facing_thresh = params->onset_facing_thresh;
	large_delta = params->onset_large_delta;
	if (syNetInputContractSticksNearNeutral(old_frame, deadband) != SYNETINPUT_CONTRACT_FALSE)
	{
		if (syNetInputContractSticksNearNeutral(new_frame, deadband) == SYNETINPUT_CONTRACT_FALSE)
		{
			return SYNETINPUT_CONTRACT_TRUE;
		}
		if ((syNetInputContractAbsS8Diff(new_frame->stick_x, 0) > 25) ||
		    (syNetInputContractAbsS8Diff(new_frame->stick_y, 0) > 25))
		{
			return SYNETINPUT_CONTRACT_TRUE;
		}
	}
	if ((syNetInputContractAbsS8Diff(old_frame->stick_x, 0) > (int32_t)facing_thresh) &&
	    (syNetInputContractAbsS8Diff(new_frame->stick_x, 0) > (int32_t)facing_thresh) &&
	    (syNetInputContractStickSign(old_frame->stick_x) != syNetInputContractStickSign(new_frame->stick_x)))
	{
		return SYNETINPUT_CONTRACT_TRUE;
	}
	if (syNetInputContractAbsS8Diff(old_frame->stick_x, new_frame->stick_x) > (int32_t)large_delta)
	{
		return SYNETINPUT_CONTRACT_TRUE;
	}
	if (syNetInputContractAbsS8Diff(old_frame->stick_y, new_frame->stick_y) > (int32_t)large_delta)
	{
		return SYNETINPUT_CONTRACT_TRUE;
	}
	if (deadband == 0U)
	{
		return ((old_frame->stick_x != new_frame->stick_x) || (old_frame->stick_y != new_frame->stick_y))
		           ? SYNETINPUT_CONTRACT_TRUE
		           : SYNETINPUT_CONTRACT_FALSE;
	}
	if (syNetInputContractAbsS8Diff(old_frame->stick_x, new_frame->stick_x) > (int32_t)deadband)
	{
		return SYNETINPUT_CONTRACT_TRUE;
	}
	if (syNetInputContractAbsS8Diff(old_frame->stick_y, new_frame->stick_y) > (int32_t)deadband)
	{
		return SYNETINPUT_CONTRACT_TRUE;
	}
	if ((correction_is_predicted != SYNETINPUT_CONTRACT_FALSE) &&
	    (syNetInputContractSticksNearNeutral(old_frame, deadband) == SYNETINPUT_CONTRACT_FALSE) &&
	    (syNetInputContractSticksNearNeutral(new_frame, deadband) == SYNETINPUT_CONTRACT_FALSE) &&
	    (syNetInputContractStickSameAnalogIntent(old_frame->stick_x, old_frame->stick_y,
	                                             new_frame->stick_x, new_frame->stick_y, params) !=
	     SYNETINPUT_CONTRACT_FALSE))
	{
		uint32_t same_intent_tol;

		same_intent_tol = params->same_intent_tolerance;
		if ((syNetInputContractAbsS8Diff(old_frame->stick_x, new_frame->stick_x) <=
		     (int32_t)same_intent_tol) &&
		    (syNetInputContractAbsS8Diff(old_frame->stick_y, new_frame->stick_y) <=
		     (int32_t)same_intent_tol))
		{
			return SYNETINPUT_CONTRACT_FALSE;
		}
	}
	return SYNETINPUT_CONTRACT_FALSE;
}

SYNetInputContractCorrectionClass
syNetInputContractClassifyCorrection(const SYNetInputContractFrame *old_frame,
                                     const SYNetInputContractFrame *wire,
                                     const SYNetInputContractParams *params)
{
	uint32_t micro_db;
	uint32_t continuity_db;
	int32_t dx;
	int32_t dy;

	if (old_frame->buttons != wire->buttons)
	{
		return nSYNetInputContractClassButton;
	}
	if (syNetInputContractStickReplaceIsRelease(old_frame, wire, params) != SYNETINPUT_CONTRACT_FALSE)
	{
		return nSYNetInputContractClassRelease;
	}
	if ((syNetInputContractSticksNearNeutral(old_frame, params->predict_deadband) !=
	     SYNETINPUT_CONTRACT_FALSE) &&
	    (syNetInputContractSticksNearNeutral(wire, params->predict_deadband) == SYNETINPUT_CONTRACT_FALSE))
	{
		return nSYNetInputContractClassOnsetFromZero;
	}
	micro_db = params->micro_deadband;
	continuity_db = params->continuity_deadband;
	dx = syNetInputContractAbsS8Diff(old_frame->stick_x, wire->stick_x);
	dy = syNetInputContractAbsS8Diff(old_frame->stick_y, wire->stick_y);
	if ((syNetInputContractStickLooksAnalog(old_frame->stick_x, old_frame->stick_y, params) !=
	     SYNETINPUT_CONTRACT_FALSE) &&
	    (syNetInputContractStickLooksAnalog(wire->stick_x, wire->stick_y, params) !=
	     SYNETINPUT_CONTRACT_FALSE) &&
	    (syNetInputContractStickSameAnalogIntent(old_frame->stick_x, old_frame->stick_y, wire->stick_x,
	                                             wire->stick_y, params) != SYNETINPUT_CONTRACT_FALSE))
	{
		if ((micro_db > 0U) && (dx <= (int32_t)micro_db) && (dy <= (int32_t)micro_db))
		{
			return nSYNetInputContractClassMicroStick;
		}
		if ((continuity_db > micro_db) && (dx <= (int32_t)continuity_db) && (dy <= (int32_t)continuity_db))
		{
			return nSYNetInputContractClassSameIntentContinuity;
		}
	}
	return nSYNetInputContractClassRealStick;
}

static uint8_t syNetInputContractFrameGameplayEquals(const SYNetInputContractFrame *a,
                                                     const SYNetInputContractFrame *b)
{
	return ((a->tick == b->tick) && (a->buttons == b->buttons) && (a->stick_x == b->stick_x) &&
	        (a->stick_y == b->stick_y))
	           ? SYNETINPUT_CONTRACT_TRUE
	           : SYNETINPUT_CONTRACT_FALSE;
}

SYNetInputContractDecision
syNetInputContractStickReplaceDecide(const SYNetInputContractFrame *published,
                                     const SYNetInputContractFrame *wire, uint8_t completed_sim,
                                     const SYNetInputContractParams *params,
                                     const SYNetInputContractHostGates *gates)
{
	void *gate_ctx;

	gate_ctx = (gates != NULL) ? gates->ctx : NULL;
	if (syNetInputContractFrameGameplayEquals(published, wire) != SYNETINPUT_CONTRACT_FALSE)
	{
		/*
		 * Same sticks can still hide a host branch miss (e.g. BRANCH_DEFERRED: Turn
		 * stayed while owner Dashed). Predicted rows must rewind when the host holds
		 * a deferred ticket for this tick.
		 */
		if ((published->is_predicted != SYNETINPUT_CONTRACT_FALSE) && (gates != NULL) &&
		    (gates->equal_predicted_force_rewind != NULL) &&
		    (gates->equal_predicted_force_rewind(gate_ctx) != SYNETINPUT_CONTRACT_FALSE))
		{
			return nSYNetInputContractRewindEqualDeferred;
		}
		return nSYNetInputContractPromoteEqual;
	}
	/*
	 * Buttons-equal stick delta the host knows cannot affect the hashed sim (e.g.
	 * Dead* KO drift): promote wire silently instead of burning a resim that only
	 * diverges cosmetic RNG.
	 */
	if ((published->buttons == wire->buttons) && (gates != NULL) &&
	    (gates->absorb_stick_replace != NULL) &&
	    (gates->absorb_stick_replace(gate_ctx) != SYNETINPUT_CONTRACT_FALSE))
	{
		return nSYNetInputContractPromoteAbsorb;
	}
	if (completed_sim != SYNETINPUT_CONTRACT_FALSE)
	{
		uint32_t micro_db;
		uint32_t continuity_db;
		int32_t dx;
		int32_t dy;

		if (published->buttons != wire->buttons)
		{
			return nSYNetInputContractRewind;
		}
		if (syNetInputContractStickReplaceIsRelease(published, wire, params) !=
		    SYNETINPUT_CONTRACT_FALSE)
		{
			return nSYNetInputContractRewind;
		}
		micro_db = params->micro_deadband;
		continuity_db = params->continuity_deadband;
		dx = syNetInputContractAbsS8Diff(published->stick_x, wire->stick_x);
		dy = syNetInputContractAbsS8Diff(published->stick_y, wire->stick_y);
		/*
		 * Same-intent Promote-only window. Never promote when the dash-gate X proxy
		 * disagrees (smash threshold cross / sign flip) — status products fork.
		 */
		if ((syNetInputContractStickLooksAnalog(published->stick_x, published->stick_y, params) !=
		     SYNETINPUT_CONTRACT_FALSE) &&
		    (syNetInputContractStickLooksAnalog(wire->stick_x, wire->stick_y, params) !=
		     SYNETINPUT_CONTRACT_FALSE) &&
		    (syNetInputContractStickSameAnalogIntent(published->stick_x, published->stick_y,
		                                             wire->stick_x, wire->stick_y, params) !=
		     SYNETINPUT_CONTRACT_FALSE) &&
		    (syNetInputContractStickDashGateDisagreeX(published->stick_x, wire->stick_x, params) ==
		     SYNETINPUT_CONTRACT_FALSE))
		{
			/* Host move-protect (may exceed deadbands; e.g. baked jibaku launch). */
			if ((gates != NULL) && (gates->protect_promote != NULL) &&
			    (gates->protect_promote(gate_ctx, dx, dy, micro_db, published->is_predicted) !=
			     SYNETINPUT_CONTRACT_FALSE))
			{
				return nSYNetInputContractPromoteProtect;
			}
			/* Fragile aim windows: host blocks every deadband promote below. */
			if ((gates == NULL) || (gates->block_deadband_promote == NULL) ||
			    (gates->block_deadband_promote(gate_ctx) == SYNETINPUT_CONTRACT_FALSE))
			{
				if (published->is_predicted == SYNETINPUT_CONTRACT_FALSE)
				{
					if ((micro_db > 0U) && (dx <= (int32_t)micro_db) &&
					    (dy <= (int32_t)micro_db))
					{
						return nSYNetInputContractPromoteMicro;
					}
					if ((continuity_db > micro_db) && (dx <= (int32_t)continuity_db) &&
					    (dy <= (int32_t)continuity_db))
					{
						return nSYNetInputContractPromoteContinuity;
					}
				}
				else if ((dx <= (int32_t)continuity_db) && (dy <= (int32_t)continuity_db) &&
				         (gates != NULL) && (gates->hash_confirm_promote != NULL) &&
				         (gates->hash_confirm_promote(gate_ctx) != SYNETINPUT_CONTRACT_FALSE))
				{
					/*
					 * Predicted rows never get a bare deadband promote (soak
					 * 740113729) — only a peer state watermark past this tick.
					 */
					return nSYNetInputContractPromoteHashConfirm;
				}
			}
		}
		/* Any remaining stick/button gameplay delta on completed sim rewinds. */
		return nSYNetInputContractRewind;
	}
	/*
	 * Runway: analog -> neutral / shedding magnitude is never onset-ahead — always
	 * rewind before considering a defer.
	 */
	if (syNetInputContractStickReplaceIsRelease(published, wire, params) != SYNETINPUT_CONTRACT_FALSE)
	{
		return nSYNetInputContractRewind;
	}
	if ((gates != NULL) && (gates->defer_predicted_correction != NULL) &&
	    (gates->defer_predicted_correction(gate_ctx) != SYNETINPUT_CONTRACT_FALSE))
	{
		return nSYNetInputContractPromoteDefer;
	}
	if (syNetInputContractCorrectionIsSignificant(published, wire, SYNETINPUT_CONTRACT_FALSE, params) !=
	    SYNETINPUT_CONTRACT_FALSE)
	{
		return nSYNetInputContractRewind;
	}
	return nSYNetInputContractPromoteInsignificant;
}
