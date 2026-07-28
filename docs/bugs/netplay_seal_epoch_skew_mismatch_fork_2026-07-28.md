# Netplay: seal epoch skew + mismatch fork → SEAL_ROWS_TIMEOUT

**Date:** 2026-07-28  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Seed:** `4025840110` (post escalate input-skew bypass)  
**Related:** [seal epoch skew identical span](netplay_seal_epoch_skew_identical_span_2026-07-26.md), [seal tuple fork](netplay_seal_tuple_fork_asymmetric_stall_2026-07-12.md), [FC escalate skew bypass](netplay_fc_escalate_input_skew_bypass_2026-07-28.md)

## Symptom

Escalate/FC recovery worked for ~2k ticks (`recovery_started≈9–11`, 0× `ONSET_UNRECOVERABLE`), then:

```text
[Linux]  initiator epoch=379 mismatch=1981 target=2011 load=1980  (FC escalate re-arm, deepen)
[Android] follower epoch=165 mismatch=2002 target=2011 load=2001  (PEER_SYMMETRIC_CLAMP 1981→2002)
Android: EPISODE_SEAL_ROWS_REJECT reason=stale_episode_tuple … pkt_mismatch=1981 active_mismatch=2002
Android: RESIM_SEAL_ROWS_TIMEOUT load_tick=2001 → VS stop
```

Same `target=2011`, overlapping absolute ticks, but different mismatch + independent epochs.

## Root cause

1. After a completed FC@1981→1992, Linux re-armed escalate FC with stale onset watermark **1981** and a deepened target **2011**.
2. Android joined via symmetric notify clamped to **2002**/2011 (resolved_through).
3. `COMPATIBLE_APPLY` (mismatch XOR target) required **identical epoch** — unlike identical-span `EpisodeTupleMatches`, which already allowed epoch skew.
4. Early-stashed Linux seals `(1981,2011)` were dropped on Android `FsmBegin(2002,2011)` because `ClearPendingExcept` kept only exact tuples.

## Fix

`netrollback_episode.c` (`PORT && SSB64_NETMENU`):

1. **`SealTupleCompatible`** helper — XOR same-target/same-mismatch fork.
2. **`ApplyCompatiblePeerSealRowsChunk`** — allow epoch skew while FSM active; log `EPOCH_SKEW_COMPATIBLE`; mark peer seal activity.
3. **Receive path** — try compatible apply/stash without requiring `pkt_epoch == active_epoch`.
4. **`ClearPendingExcept`** — retain XOR-compatible early stashes across Begin so flush can apply them.

## Verification

Re-soak Dream Land Ness ditto through repeated FC escalate:

- Prefer `EPOCH_SKEW_COMPATIBLE` / `COMPATIBLE_APPLY` over `stale_episode_tuple` storms when target matches and mismatch deepened
- No `RESIM_SEAL_ROWS_TIMEOUT` solely from epoch+mismatch fork with overlapping span

## Follow-on (2026-07-28)

Seed `3404332722`: epoch-skew compatible apply worked, but FC join still clamped `1205→1218` (`resolved_through`) → span 25 vs 12 → `SEAL_ROWS_EXHAUSTED missing_slots=0x2`. See [fc_join_keep_onset_resolved_clamp](netplay_fc_join_keep_onset_resolved_clamp_2026-07-28.md).
