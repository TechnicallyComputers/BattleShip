# Netplay — Ness ground jibaku propelled without hitboxes

**Date:** 2026-06-01  
**Status:** FIX SHIPPED (soak pending)  
**Log:** `netplay-session-trimmed-rollback.log` (synctest emergency restore during ground jibaku)

## Symptom

From standing on ground, Ness sometimes bursts forward extremely far after PK Thunder self-hit — **not** in jibaku animation, **no damage hitboxes**, like being propelled without jibaku state (status 230 `SpecialHiEnd` with launch velocity retained).

Gate diag signature (7 events, all status 231):

```
NESS_PKTHUNDER_GATE event=anim_length_zero player=0 status=231 anim_length=0 ga=0
```

Immediately followed by synctest emergency restore and status 230 with `atk0_y=0`.

## Root cause

`synNetplayNessCatchUpPKJibakuIfDue()` calls `ftNessSpecialHiEndSetStatus()` when `pkjibaku_anim_length <= 0` while status is still 231 (ground jibaku). On snapshot apply / synctest restore, the blob can have status=231 but `anim_length=0` from stale union overlay or mid-transition capture — not a legitimate end frame.

Catch-up treats this as expiry, exits to `SpecialHiEnd` (230) with jibaku launch momentum retained and hitboxes cleared.

Related union family: [`netplay_ness_pkthunder_instant_jibaku_2026-06-01.md`](netplay_ness_pkthunder_instant_jibaku_2026-06-01.md) (`pkjibaku_delay` scrub). Jibaku quantize covers angle/pos only, not `pkjibaku_anim_length`.

## Fix

1. **Apply/save sanitize** — `syNetplayNessSanitizePKJibakuStatusVars()` restores `FTNESS_PKJIBAKU_ANIM_LENGTH` when status is 231/236 and `anim_length <= 0` unless `status_total_tics >= FTNESS_PKJIBAKU_ANIM_LENGTH` (legitimate end).
2. **Catch-up** — sanitize runs before catch-up End path; early scrub no longer triggers premature `SpecialHiEnd`.
3. **Synctest defer** — `syNetplayNessAnyLiveFighterInJibakuBurstScope()` skips synctest while live fighter is in 231/236 (mirrors rebirth/dead defer).
4. **Safety clamp** — `syNetplayNessClampResidualJibakuLaunchVelocity()` caps residual velocity to `FTNESS_PKJIBAKU_VEL` on legitimate catch-up End.

## Files

- `port/net/sys/netplay_ness_pkthunder_gate.c/.h` — sanitize restore, velocity clamp, jibaku burst scope helper
- `port/net/sys/netrollbacksnapshot.c` — synctest skip `reason=ness_jibaku`

## Verification

Re-test ground PK Thunder jibaku from standing with `ROLLBACK_SYNCTEST=1`. With gate diag enabled, `anim_length_restore` may appear on apply; `anim_length_zero` during early jibaku should not transition to status 230 without full anim window.
