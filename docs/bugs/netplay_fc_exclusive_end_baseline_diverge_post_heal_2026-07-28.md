# Netplay: exclusive-end peer baseline hard-kills after healed FC Commit

**Date:** 2026-07-28  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Seed:** `214064425` (Dream Land Ness ditto, Android guest / Linux host)  
**Related:** [stale baseline resync post heal](netplay_fc_stale_baseline_resync_post_heal_2026-07-28.md), [softlip understage wall](netplay_softlip_understage_wall_passthrough_2026-07-28.md)

## Symptom

```text
FC@765→777 Commit → Live (resolved_through=777)
[Android] RESIM_BASELINE_SEND load=776 figh=0x659374AC (other digests match Linux)
[Linux]   BASELINE_PREEMPTIVE_LIVE_CAP load=776 mismatch=777
          RESIM_BASELINE_ECHO hash_only figh=0xFF580FEC
          PEER_SNAPSHOT_DIVERGE — stopping VS session (load_tick 776)
[Android] PEER_SNAPSHOT_DIVERGE suppressed (figh stale aggregate) → continues
          then receives VS_SESSION_END from host
```

Prior fixes held: no invent/`dup_pending` hang; mid-span `ignore_stale_behind_resolved` fired for load=764.

## Root cause

1. Post-heal stale-baseline ignore used strict `(load_tick+1) < resolved_through`, so the **exclusive end** of the just-committed span (`load+1 == resolved_through`) still compared.
2. SoftLip / `topn` left aggregate figh disagreeing at that exclusive end while world/rng/map/cam/anim matched.
3. `StaleAggregateFighOnly` soft-continued on Android (slot/live refresh path) but returned FALSE on Live Linux → asymmetric hard kill.
4. Preemptive live-cap used the same strict `<`, so exclusive-end baselines still armed `BASELINE_PREEMPTIVE_LIVE_CAP`.

## Fix

`PORT && SSB64_NETMENU`:

1. **`ComparePeerBaselineToLocal` / `ArmPeerBaselineResync` / `PreemptiveBaselineCapIsStale`** — treat covered loads with inclusive exclusive end: `(load+1) <= resolved_through` / `mismatch <= resolved_through` while Live.
2. **`PeerBaselineDriftIsStaleAggregateFighOnly`** — if other partitions already match and `(load+1) <= resolved_through`, return TRUE (symmetric soft-continue even when slot/live refresh disagrees). Camera may also differ at covered exclusive end (seed `593462826` — see [figh+cam mid-resim](netplay_fc_exclusive_end_baseline_figh_cam_mid_resim_2026-07-28.md)).

Legitimate new FC onset after heal still uses loads with `load+1 > resolved_through`.

## Verification

Re-soak Dream Land Ness ditto through an FC heal that leaves exclusive-end figh skew:

- Prefer `ignore_stale_behind_resolved` / `BASELINE_PREEMPTIVE_LIVE_CAP_SKIP` / `FIGH_STALE_AGGREGATE_OK` over `PEER_SNAPSHOT_DIVERGE — stopping VS` at `load == resolved_through - 1`
- No host-only session end immediately after a Commit→Live with matching non-figh digests
