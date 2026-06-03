# Netplay — Ness PK Thunder resim sanitize_delay false positive

**Date:** 2026-06-01  
**Status:** Fix shipped (soak pending)  
**Symptom:** Cross-ISA rollback session diverged at validation ~1814: item hash agreed, fighter + effect hashes split. Trim log flagged SUSPECT instant jibaku during resim replay.

## Root cause

1. **Resim sanitize over-correction:** On rollback load at tick 1694 Ness was legitimately mid-Hold with `pkjibaku_delay=0` (grace expired after 30+ Hold frames). `syNetplayNessSanitizePKThunderThrowStatusVars()` treated all zero delays as scrub artifacts and reset delay to 30, re-arming jibaku grace during resim and desyncing fighter state vs the peer that skipped the bad sanitize.

2. **Resim diag false positive:** `hold_enter` only fires on live HoldInit; resim replay loads Hold blobs directly. `jibaku_trigger` during resim therefore logged `hold_frames=0` even when live sim had ~51 frames of Hold — trim script SUSPECT logic misread this as instant jibaku.

3. **Effect hash drift (secondary):** Post-jibaku fighters could retain `is_effect_attach=TRUE` from Start/Hold blobs while no live PK wave effect existed (jibaku/end scope). Stale attach flag contributed to effect-hash divergence after resim.

## Fix

1. **`syNetplayNessHoldDelayZeroIsLegitimate()`:** On Hold with `status_total_tics >= FTNESS_PKJIBAKU_DELAY`, delay=0 is normal grace expiry — skip sanitize (`sanitize_delay_skip` diag event).

2. **`syNetplayNessSyncHoldEntryTracking()`:** On Hold apply, seed hold-entry tick from `tick - status_total_tics` so resim replay reports accurate `hold_frames`.

3. **`resim=` field** on all NESS_PKTHUNDER_GATE diag lines via `syNetRollbackIsResimulating()`.

4. **Hold ground/air switch:** `syNetplayNessHoldSwitchRefreshDelay()` re-arms early scrubbed delay and syncs tracking without duplicate `hold_enter` spam.

5. **Apply-post:** Clear stale `is_effect_attach` when fighter is past Start/Hold PK scope and no live PK wave effect exists.

## Files

- `port/net/sys/netplay_ness_pkthunder_gate.c/.h`
- `decomp/src/ft/ftchar/ftness/ftnessspecialhi.c`
- `port/net/sys/netrollbacksnapshot.c` — apply-post effect attach clear
- `scripts/netplay-trim-logs.py` — ignore resim jibaku SUSPECT rows

## Verification

Re-run cross-ISA netplay with `SSB64_NETPLAY_NESS_PKTHUNDER_GATE_DIAG=1`. After rollback load mid-late Hold, expect `sanitize_delay_skip reason=hold_grace_expired` instead of `sanitize_delay was=0 now=30`. Resim `jibaku_trigger` rows should show plausible `hold_frames` and trim script should not flag them as SUSPECT.
