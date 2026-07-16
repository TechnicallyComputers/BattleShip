# Netplay: Pupupu Open→Blow mouth leftover → 4-tick phase skew → map+figh PEER_SNAPSHOT_DIVERGE — 2026-07-15

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-15  
**Session:** `1827128554` seed `3677322234` (Android client ↔ Linux host, Dream Land)

## Symptom

scan-drift PASS (no `LOAD_HASH_DRIFT` / FC inputs-MATCH diverge). Host (Android) STABLE soft recovery; guest (Linux) UNSTABLE `PEER_SNAPSHOT_DIVERGE` @load ~4444. Seven stick resims earlier; session ends.

Fail-closed partitions at load **4444**:

| Partition | Match? |
|-----------|--------|
| figh / map | **diverge** |
| world / rng / item / wpn / anim / cam | match |

Trigger was incidental: `LEDGER_REFRESH` @4445 → GGPO → baseline of an **already forked** universe.

## Root cause

Whispy **Open→Blow** onset skewed by **4 ticks**:

| Tick | Android | Linux |
|------|---------|-------|
| 4342 | Open | Open |
| **4343** | Open | **Blow** `wind_dur=293` |
| 4344–4346 | Open | Blow |
| **4347** | **Blow** `wind_dur=293` | Blow `wind_dur=289` |

Blink matched throughout (including a matched negative blink window then reseed). RNG stayed matched (one wind roll each, interleaved) — so no FC `diverged=rng`. Asymmetric wind push still forked `ground_fold` + fighter positions → baseline deepen exhaust → `PEER_SNAPSHOT_DIVERGE`.

`grPupupuWhispyUpdateOpen` still gated Blow on **mouth `map_gobj[1]` anim end**. Stick GGPOs @4307–4315 during Open left ISA-sensitive mouth leftovers that are **not** in the Pupupu ground blob; post-load refresh intentionally does not snap mouth (would fire Blow immediately). Prior widen-snap (`4096/65536`) only helps near-zero — insufficient for multi-tick leftover skew.

Same class as [`netplay_pupupu_whispy_open_blow_rng_fc_2026-07-12.md`](netplay_pupupu_whispy_open_blow_rng_fc_2026-07-12.md), but 4-tick skew without an FC rng fork.

## Fix

Under `syNetplayRollbackSemanticsActive()`:

1. **Tick-authoritative Open→Blow** — do not gate on mouth leftover. When Open mouth `PlayAnim` runs, arm `whispy_wind_wait` from `ceil(map_gobj[1]->anim_frame)` (field is already in the ground snapshot; unused during Open in vanilla because Wait consumed it to 0).
2. **`UpdateOpen`** decrements that wait and fires Blow at 0 (same Blow body / gameplay `wind_duration` roll as vanilla).
3. Pending `mouth_status == Open` returns early until `UpdateGObjAnims` arms the wait.
4. Offline / non-rollback netmenu keeps the anim-end gate.

## Verify

Re-soak Dream Land through stick GGPO during Whispy Open into Blow:

- Matched Blow onset tick across peers (`pupupu_ground` status / `wind_dur`)
- No map+figh `PEER_SNAPSHOT_DIVERGE` deepen chain after Blow
- During Open, `whispy_wind_wait` counts down Open remaining (not Wait idle 0)
