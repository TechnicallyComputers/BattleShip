# Netplay: exclusive-end mid-resim figh+camera baseline hard-kill

**Date:** 2026-07-28  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Seed:** `593462826` (Dream Land Ness ditto, Android guest / Linux host)  
**Related:** [exclusive-end baseline diverge post heal](netplay_fc_exclusive_end_baseline_diverge_post_heal_2026-07-28.md), [stale baseline resync post heal](netplay_fc_stale_baseline_resync_post_heal_2026-07-28.md), [healed frontier survive deepen](netplay_fc_healed_frontier_survive_deepen_2026-07-28.md)

## Symptom

```text
FC@811 inputs_agree=1 SoftLip topn_tx → Commit 801→812
short episode 813→815 Commit → Live (resolved_through=815)
[Android] resim begin 814→817 (local_initiator) after Commit
          RESIM_BASELINE_RECV load=814 peer figh+cam diverge (world/rng/map/anim match)
          deepen exhaust → PEER_SNAPSHOT_DIVERGE — stopping VS (load_tick 814)
[Linux]   LIVE_CAP_SKIP / ignore_stale_behind_resolved load=814 resolved=815
          receives VS_SESSION_END from guest
```

Prior exclusive-end Live ignore and figh-only stale-aggregate held earlier in the same soak (`LIVE_CAP_SKIP` @765/800, `FIGH_STALE_AGGREGATE_OK` @811).

## Root cause

1. Live `ignore_stale_behind_resolved` does not apply while `ResimPending` — Android re-opened a span whose load was the just-committed exclusive end.
2. Peer baseline at that load disagreed on **figh and camera**; world/rng/map/anim matched.
3. `StaleAggregateFighOnly` required camera equality before the exclusive-end soft-continue branch, so deepen-exhaust hard-killed on Android while Live Linux ignored the same baseline.

## Fix

`PORT && SSB64_NETMENU` — `PeerBaselineDriftIsStaleAggregateFighOnly`:

- Require non-cam gameplay digests (world/item/rng/anim/weapon/map/effect).
- If `(load+1) <= resolved_through`, return TRUE even when camera differs (cosmetic at covered exclusive end).
- Non-covered paths still require camera match before live/slot refresh branches.

## Verification

Re-soak Dream Land Ness ditto through FC heal + short post-Commit resim at exclusive end:

- Prefer `PEER_SNAPSHOT_DIVERGE suppressed (figh stale aggregate)` / `FIGH_STALE_AGGREGATE_OK` over session stop when load is covered and only figh±cam disagree
- Linux Live `ignore_stale` / `LIVE_CAP_SKIP` at the same load should not be paired with guest hard kill
