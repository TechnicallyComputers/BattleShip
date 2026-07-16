# Baseline state deepen exhaust: fail closed on hard gameplay diverge

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-15  
**Session:** soak1 `1643097122` seed `1889905460` (Android client ↔ Linux host)

## Symptom

State-vs-input routing worked (`universe state diverge … → deeper` / `→ state deepen`). After deeper budget (`DEEPER_MAX=2`) exhaust at load ~2305:

- P0 JumpAerial status 22 on CLIFF `0x8000`; rng/world matched at first deepen; **figh + map** already disagreed.
- Recovery then `ArmPeerBaselineResync` / fallthrough and kept simulating forked → messy `PEER_SNAPSHOT_DIVERGE @2311` with world/map/figh all split.
- Drift scan PASS (no FC). Physics CLIFF JumpAerial + Whispy map/`ground_fold` seed remains open.

## Root cause

[`netplay_baseline_universe_state_vs_input_routing_2026-07-13.md`](netplay_baseline_universe_state_vs_input_routing_2026-07-13.md) correctly sends **inputs-agree-through-load** forks to deeper load instead of GGPO. Map-only deeper exhaust already fail-closed ([`netplay_pupupu_ground_fold_whispy_anim_2026-07-12.md`](netplay_pupupu_ground_fold_whispy_anim_2026-07-12.md)), but **figh/map (or world/item/rng) hard diverge** after state-deepen exhaust still armed peer baseline resync and continued — same asymmetrical “keep playing forked” class.

Unilateral deepen targets (`local_deeper` vs peer-announced load) also walked each peer back on different ticks (2307→2305 vs 2305→2303).

## Fix (`port/net/sys/netrollback.c`)

1. **Negotiated deepen load** — `restart_load = min(local_deeper, peer_ann)` when peer already announced an earlier outcome tick.
2. **`syNetRollbackTryFailClosedAfterStateDeepenExhaust`** — after deepen budget spent (or restart unavailable), if not input-poisoned at load:
   - map-only → `PEER_SNAPSHOT_DIVERGE` (existing policy, centralized);
   - hard gameplay diverge (figh / world / item / rng / map) → `PEER_SNAPSHOT_DIVERGE` immediately;
   - soft/cosmetic-only → still allow `ArmPeerBaselineResync`.
3. Skip `ArmPeerBaselineResync` when fail-closed consumed the mismatch.

## Verify

Re-soak similar Dream Land cliff JumpAerial / Whispy window:

- Expect `universe state diverge … → deeper` then either converge or `state deepen exhausted … PEER_SNAPSHOT_DIVERGE` / `map-only deeper exhausted …` — **not** multi-tick play-through with forked world after exhaust.
- Input-poisoned loads still GGPO / arm resync paths (fail-closed helper returns FALSE).

### Still open

- Physics / snapshot fidelity for CLIFF JumpAerial + Pupupu map stream (why figh/map forked with inputs matched).

Related: [`netplay_baseline_universe_state_vs_input_routing_2026-07-13.md`](netplay_baseline_universe_state_vs_input_routing_2026-07-13.md), [`netplay_pupupu_ground_fold_whispy_anim_2026-07-12.md`](netplay_pupupu_ground_fold_whispy_anim_2026-07-12.md), [`netplay_mmicepoll_cand_copy_stack_overflow_2026-07-12.md`](netplay_mmicepoll_cand_copy_stack_overflow_2026-07-12.md).
