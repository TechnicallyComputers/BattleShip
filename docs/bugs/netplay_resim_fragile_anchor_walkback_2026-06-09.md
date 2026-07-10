# Netplay resim fragile anchor walkback — 2026-06-09

**Status:** FIX SHIPPED (soak pending)

## Symptom

Live forward sim stays valid, but rollback resim that loads a ring slot inside a synctest-fragile window (`LOAD_HASH_DRIFT`, baseline timeout, or `RESIM_POST_DIVERGE` with matching input digests) could hard-stop the VS session. Synctest skip probes already avoided false failures on the same ticks during diagnostic probes only.

## Root cause

Fragile-tick detection lived only in `syNetRbSnapshotSynctestShouldSkipProbeTick()` (synctest path). Production resim used `mismatch - 1` as load anchor with no walkback; verify failure on those ticks called session stop.

## Fix

| Component | Change |
|-----------|--------|
| `syNetRbSnapshotIsLoadAnchorFragile` | Reuses synctest probe scopes for load tick and load+1 |
| `syNetRbSnapshotResolveLoadAnchorAvoidingFragile` | Walk back via load-safe / valid slots; mark skipped ticks unsafe |
| `syNetRollbackApplyLoadAnchorFragileWalkback` | Called from `ResolveLoadTickForSnapshot` before every resim load |
| `syNetRollbackTryLoadPostTickWithFidelityWalkback` | Begin-resim load + retry loop on verify fail (no session stop in resim context) |
| `syNetRollbackMaybeResimAnchorProbe` | Always runs at resim begin; mismatch triggers deeper reload loop |
| `RESIM_POST` snapshot_fidelity | Deeper restart from walked-back load when replay inp matches |

## Soak pass criteria

- Rollback through Kirby JumpAerial, guard release lag, Whispy post-blow, QA travel, etc.: expect `RESIM_ANCHOR_FRAGILE_WALKBACK` / `RESIM_LOAD_FIDELITY_RETRY` / `RESIM_ANCHOR_PROBE_WALKBACK` instead of session stop.
- No `LOAD_HASH_DRIFT — stopping VS session` during resim on known fragile scopes when a deeper load-safe anchor exists within 16 ticks.
