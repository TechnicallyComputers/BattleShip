# Netplay baseline fighter_slots tick skew (Phase 42)

**Date:** 2026-06-10  
**Symptom:** Soak1 rematch (epoch 2, dual-appear @239) — Android completes resim; Linux deadlocks in intro after baseline slot-only mismatch and walkback to 238.

## Root cause

Resim baseline wire digests (figh/world/item/rng/anim/map/…) were sourced from **ring@load_tick**, but per-player `fighter_slots` in the baseline packet were sourced from **live** `syNetSyncCollectFighterSlotHashes()` at arm/send time.

During intro Appear, per-player `fhash_light` changes every sim tick. If sim advances past `load_tick` while awaiting peer baseline, live slot hashes reflect tick N+1 while the packet claims `load_tick=N`. Main subsystem digests still match (also ring-sourced); only `fighter_slots` diverge.

Soak1 match 2 example @load_tick=239:

```
RESIM_BASELINE_BISECT slot_div player=1
  peer_slot=0x6BC1B480  local_slot=0xC6CED593
```

Both peers agree `fhash_light=0xC6CED593` at tick 239 and `0x6BC1B480` at tick 240 — peer wire carried tick-240 slot hash against load_tick=239.

Linux then took `RESIM_BASELINE_MISMATCH → deeper restart @238 → rollback_epoch_hold / load_fail_hold` and froze intro until session end. Android happened to arm/compare at matching sim phase and completed.

## Fix

1. **Ring-sourced baseline slots** — `syNetRollbackArmResimBaselineAfterLoad()` now calls `syNetRbSnapshotCollectFighterSlotHashesAtTick(load_tick, …)` instead of live collection. Send path (`TakePeerBaselineDigestForSend`) unchanged; armed array is now ring-authoritative.

2. **Slot ring resync gate** — When main baseline digests and slot subsystem hashes match but armed `fighter_slots` differ from peer, compare peer slots against ring@load_tick. If they agree, re-arm local slots from ring and open replay gate (mirrors existing `RESIM_BASELINE_ANIM_ONLY` path). Prevents walkback deadlock when only local armed slots were live-skewed.

3. **Diagnostics** — `BASELINE_SLOT_RING_ARM` logs per-player ring vs live drift at arm; `RESIM_BASELINE_SLOT_RING_RESYNC` logs ring realignment before gate open.

## Files

- `port/net/sys/netrollbacksnapshot.c` — `syNetRbSnapshotCollectFighterSlotHashesAtTick()`
- `port/net/sys/netrollbacksnapshot.h` — export
- `port/net/sys/netrollback.c` — arm, compare recovery, last-peer slot cache

## Verification

Re-run soak1 rematch (two intro cycles, inject @240 on match 2). Expect both peers:

- `resim baseline digest matched load_tick=239`
- `resim replay gate open`
- `resim complete epoch=2`

No `RESIM_BASELINE_BISECT slot_div` on Linux when main digest matches.

Optional env: watch `BASELINE_SLOT_RING_ARM` / `RESIM_BASELINE_SLOT_RING_RESYNC` during intro baseline wait.
