# Netplay — FC input-agree onset ring clamp

**Date:** 2026-07-19  
**Build:** netmenu (`SSB64_NETMENU=ON`), soak1 session `1410199591` seed `3284320887` (Android client ↔ Linux host)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptom

Paired soak: `FRAME_COMMIT_STATE_DIVERGE @3873` `figh` only, `inputs=MATCH`, then both peers:

```
FRAME_COMMIT_INPUT_AGREE_ONSET validation=3873 onset=3633 shared=3633 predicted=… scan_begin=3633
try_begin_fail stage=fc_storm_limit mismatch=3633 … resolved_through=3751
PEER_BASELINE_RESYNC_STORM load_tick=3632 … ring=128 — aborting resync loop
```

Session ends even though Ep11 had already corrected through `resolved_through=3751` and the live physics fork began at gut **3752** (still inside the 128-tick ring).

## Root cause

July 11 correctly prefers **shared** human non-neutral onset over local predicted-usage flags (`netplay_predict_fc_asymmetric_onset_2026-07-11.md`). That onset is bilateral when input digests match.

When the shared onset sits **outside the rollback ring**, Resolve cannot find a load-safe slot ≥ `min_load`, but the onset path still armed `mismatch_tick = onset` (3633 → load 3632). TryBegin then hit `fc_storm_limit` / `PEER_BASELINE_RESYNC_STORM`.

Predicted onset differed cross-peer (Android 3752 vs Linux 3633) — confirming it must stay diagnostic-only while shared is present.

## Fix

Under `PORT && SSB64_NETMENU`, input-agree FC onset recovery:

1. Keep shared-onset preference when `onset_load >= min_load` and Resolve succeeds.
2. Otherwise **fall forward** to the earliest bilaterally safe in-ring load:
   - shared correction frontier (`resolved_through` local∪peer) if load-safe in ring
   - else earliest load-safe in `[min_load, validation-1]`
   - else latest load-safe in ring (last resort)
3. Log `FRAME_COMMIT_INPUT_AGREE_ONSET_RING_CLAMP` / `FRAME_COMMIT_TRYBEGIN_RING_CLAMP`.
4. TryBegin safety net: if FC state recovery would still storm, apply the same clamp before abort.

Does **not** prefer predicted over shared (preserves July 11). Does **not** fix the underlying TopN.x sim fork (see soft-lip X diag soak).

## Verify

Re-soak Android↔Linux with a deep shared onset + late input-agree FC:

- Expect `ONSET_RING_CLAMP` with `clamped_load` near `resolved_through` / ring floor (not 3632)
- No `PEER_BASELINE_RESYNC_STORM` solely because onset was older than the ring
- Physics fork may still FC-diverge until the AdjNew writer is named (`SSB64_NETPLAY_SOFTLIP_X_DIAG=1`)

## Related

- [`netplay_predict_fc_asymmetric_onset_2026-07-11.md`](netplay_predict_fc_asymmetric_onset_2026-07-11.md) — shared onset preference
- [`netplay_shared_correction_frontier_2026-07-19.md`](netplay_shared_correction_frontier_2026-07-19.md) — `resolved_through`
- [`netrollback_peer_baseline_resync_storm_2026-05-18.md`](netrollback_peer_baseline_resync_storm_2026-05-18.md) — storm abort
- Soft-lip DamageFall TopN.x class: [`netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md`](netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md)
