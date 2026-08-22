# Netplay — resim double-steps the camera: hashed GMCamera drifts on the rolling-back peer

**Date:** 2026-08-22
**Build:** netmenu (`SSB64_NETMENU=ON`)
**Status:** FIX IMPLEMENTED — re-soak pending
**Soak:** Android guest ↔ Linux host, 2434 ticks, D=2, both players active simultaneously
**Related:** synctest camera yank (same class, fixed in `syNetRbSnapSynctestRecoverCameraIfInterestYanked`), [light exclusive-frontier poison](netplay_light_exclusive_frontier_poison_load_2026-07-28.md)

## Symptom

`PEER_SNAPSHOT_DIVERGE` at tick 2433 with `class=replay_determinism` — reported as a
gameplay desync. It was not one.

Comparing every tick both peers logged (2266 ticks):

| | mismatched ticks | shape |
|---|---|---|
| `figh` | 71 | isolated **single** ticks — mispredicts, healed by rollback |
| `cam` | **570** | **51 runs**, median 4 ticks, max 87 |

**All 51 camera runs start within 2 ticks of a resim. 100%.** Both peers first diverge
at tick 424, a rollback tick. The peer that rolls back more drifts more (Linux 219
resims vs Android 77).

Live `figh` at 2431–2434 was **identical on both peers** (2433 = `0x2DF56C00`). Only the
snapshot blobs differed, and each peer's local live-vs-blob comparison was sampled at a
different offset (Linux live `status_total_tics=35`, Android `34`, both blobs `33`). So
`class=replay_determinism` is a mislabel: the fighter sim never diverged.

Of 296 episodes across both peers, 295 are `earliest_authoritative_correction`; exactly
one is `peer_snapshot_diverge`. Camera drift was not generating rollbacks — it just
tripped the snapshot comparison once when the timing lined up.

## Root cause

`gcRunAll()` owns the camera: `gmCameraMakeDefaultCamera(..., gmCameraRunFuncCamera)`
registers the integrate as the camera GObj's process. During resim replay,
`scVSBattleFuncUpdateBattleSimOnly()` → `ifCommonBattleUpdateInterfaceAll()` →
`ifCommonBattleGoUpdateInterface()` → **`gcRunAll()`**, so the camera already advances
exactly once per replayed tick, same as forward sim.

On top of that, four sites ran an **additional** `gmCameraRunFuncCamera` whenever
`syNetplayRollbackSemanticsActive()`:

| site | when | extra integrates |
|---|---|---|
| `syNetRbSnapApplyCamera` | every snapshot load | +1 |
| `…RefreshIntroPresentationAfterForwardResimTick` | every replay tick | +1 × span |
| `…RefreshIntroPresentationAfterResimComplete` | resim complete | +1 |
| `syNetRbSnapshotResyncLiveFightersFromSlotForSim` | anchor probe / baseline echo | +1 |

A span-N episode therefore stepped the camera **N + 2 or more** times instead of N.
`gmCameraRunFuncCamera` mutates GMCamera scalars that **are** in `syNetSyncHashGMCamera`
(CObj is excluded; the struct is not), and only the rolling-back peer runs them — so the
hashed camera diverges, then slowly re-converges as the camera eases toward its target.
That is the 51 runs.

The intent was presentation ("integrate each forward replay tick so the viewport tracks
fighters"), but `gcRunAll` had already done exactly that.

This is the same failure the synctest path documents verbatim:

> Unconditionally running `gmCameraRunFuncCamera` afterward advances hashed GMCamera
> scalars by one integrate on the probing peer only (soak1 Android SYNCTEST@1713 →
> cam `0xC8F38248`→`0x95D46C4C` while Linux stayed).

That fix was never applied to the resim path.

## Fix

Drop `syNetplayRollbackSemanticsActive()` from all four conditions, keeping the
intro-countdown case (intro genuinely needs it — those paths run where interface
`gcRunAll` is deferred, so the integrate is the only camera advance).

`…AfterResimComplete` keeps its **CObj-only** restore from `target_tick` — CObj is
excluded from the hash, so pinning the end-of-replay view cannot move it — and simply
drops the integrate that followed.

Net effect: exactly one camera advance per replayed tick, matching forward sim, with no
hashed mutation unique to the rolling-back peer.

## Verification

- netmenu + offline Debug builds compile and link clean, no new warnings.
- No `syNetplayRollbackSemanticsActive()`-gated camera integrate remains in the snapshot
  layer (grep-verified 0).
- Re-soak should show camera mismatch runs collapse toward zero while `figh` behaviour is
  unchanged (isolated single-tick mispredicts, healed by rollback).

## Note

This removes a cross-peer state difference; it does not change episode volume. The
episode rate is driven by `D=2` provisioning — see
[`docs/netplay_rbe_sched_integration_2026-08-22.md`](../netplay_rbe_sched_integration_2026-08-21.md)
and the rbe shadow's `gap1_grace` finding.
