# Provisional gap-fill counted as ring_ready → R-stall instead of prediction

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-12  
**Seed:** `414290157` (slow lockstep; replay matched, FC `state_diverge=0`)

## Symptom

After wire 8/9 auth-frontier + authority ledger Phase 1/2, stick soaks stayed correct but admission skewed hard to lockstep: **P≈61% / R≈39%**, ~2.6 wall frames per sim tick. No GGPO (inputs arrived “on time” as provisional), so prediction never exercised.

## Root cause

Auth-frontier / feel-0 runway stores frames above the sender’s `auth_wire_frontier` as **`RemoteGapFilled`** (`wire_provisional`). That correctly:

- raises `hr` for pacing
- is not strict-confirmed (real INPUT replaces freely; write-once does not protect it)

But `syNetInputHasRemoteInputForWireTick` used `FrameIsRemoteConfirmed`, which **includes gap-fill**. Shared commit then took the confirmed path:

1. `EvaluateSharedCommitStep` → `ring_ready=TRUE` → `uses_prediction=FALSE`
2. FuncRead → `need_confirmed_wire=TRUE` → `syNetInputRemoteHumanWireReadyForSimTick` (strict / ledger only)
3. Gap-fill alone → WireReady fails → **R-stall**

Provisional raised `hr` while simultaneously **disabling** the phase_lock prediction escape.

## Fix

Under `PORT && SSB64_NETMENU`, `syNetInputHasRemoteInputForWireTick` requires **`RemoteConfirmed` only** (`FrameIsRemoteStrictConfirmed`). Gap-fill still updates `hr` via StoreFrame; shared commit falls through to `uses_prediction` when the phase_lock window allows.

Call sites: shared-commit `RemoteInputsPresentForWireTick`, strict ring fuzz in `IsRemoteInputReadyForSimTickEx`, reconnect transport arm, delay_sync_diag.

## Verify

Re-soak Android ↔ Linux stick mash:

- Admission **R%** should drop vs seed `414290157`; more **P** / predict advances when wire is only provisional
- Expect possible **GGPO** on provisional→real stick REPLACE (desired)
- Replay / FC should still match when inputs agree

Related: `netplay_confirmed_publish_write_once_2026-07-12.md` (wire 8/9 + ledger), `docs/netplay_phase_lock.md`.
