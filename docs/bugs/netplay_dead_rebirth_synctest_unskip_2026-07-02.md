# Netplay: dead/rebirth synctest unskip

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)
**Date:** 2026-07-02
**Builds:** `build-netmenu` + `build-offline` link clean.

## Symptom

Soak2 session `1133245253` passed determinism after the ImpactWave cosmetic exclusion:

- `LOAD_HASH_DRIFT=0`
- `SYNCTEST_FAIL=0`
- `RESULT: PASS`

However, synctest still skipped large KO lifecycle windows:

- host: `rebirth` x166, `dead` x36, `rebirth_probe` x1
- guest: `rebirth` x168, `dead` x36, `rebirth_probe` x1

Those skips masked save/load coverage during the exact windows that still carry the highest rollback
risk: dead countdown, rebirth platform descent, and rebirth halo lifecycle.

## Root cause

The skips were originally defensive:

- `dead`: periodic synctest restore could step past vanilla's `dead.wait == 0` gate and miss
  `ftCommonDeadCheckRebirth`.
- `rebirth`: probe tick T vs T-1 differed in platform descent timers, causing a visible emergency
  restore snap.
- `rebirth_probe`: anchor walkback could land deeper inside the multi-tick KO window.

Those were temporary masks. The rollback load path now has explicit repair/catch-up paths:

- `syNetplayRebirthCatchUpDeadGateIfDue`
- `syNetplayRebirthCatchUpLifecycleIfDue`
- `syNetRbSnapRestoreRebirthFightersAfterFinalize`
- `syNetRbSnapEnsureRebirthHaloEffectsFromSlot`
- `syNetRbSnapPruneStaleRebirthHalos`
- faithful fighter blob capture for `gobj_translate`, control bits, and `dead_gate_wait`

With those paths in place, a dead/rebirth save/load failure should surface as a local
`SYNCTEST_FAIL` with field diagnostics, not be deferred through the whole KO window.

## Fix

Removed the live-scope synctest skips:

- `reason=dead`
- `reason=rebirth`

Removed the probe-tick skip:

- `reason=rebirth_probe`

Also removed the now-unused `syNetRbSnapFighterInDeadScope` /
`syNetRbSnapshotAnyFighterDeadScopeActive` helper path. Rebirth scope helpers remain because finalize,
halo, and verify-joint repair still use them.

## Verify

- `build-netmenu` `ssb64` target: links clean.
- `build-offline` `ssb64` target: links clean.
- Soak pending. Expected first failure, if any, should be a real `SYNCTEST_FAIL` in `figh` or `eff`
  around KO lifecycle state (`dead_gate_wait`, `dead.wait`, rebirth pose/control bits, or rebirth
  halo count/timers).
