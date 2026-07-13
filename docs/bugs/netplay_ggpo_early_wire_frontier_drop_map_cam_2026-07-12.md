# Stick storm: early-wire GGPO drop + opaque SYNCTEST during resim → map/cam diverge

**Date:** 2026-07-12  
**Session:** `1309587627` seed `2425991472` (Android client ↔ Linux host, Dream Land)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

Idle through ~1831 with Whispy blow cycles: **15× SYNCTEST_OK**, FC clean. Stick input ~1944 opens a GGPO storm; `SYNCTEST_FAIL` @1952/1954 mid-storm; session dies on `PEER_SNAPSHOT_DIVERGE` @1959 with **matched figh/world/rng/anim** and **map+cam only** disagree.

## Root cause

1. **Early-wire frontier drop:** `QueueDeferredInputCorrectionEx` still rejected `sim_tick > frontier` when live had not reached the wire tick (`deferred_queue_drop mismatch=1962 frontier=1961 sim=1960`). REPLACE promoted without arming deferred for that span.
2. **Opaque SYNCTEST mid-resim:** Synctest probe ran while resim pending / rewriting slots → `SYNCTEST_FAIL` with no `LOAD_HASH_DRIFT` (load/verify failed without partition breadcrumb).
3. **Map+cam miss map-only path:** `PeerBaselineDriftIsGameplayOnlyMap` required camera match, so map+cam forks skipped `PEER_BASELINE_MAP_DRIFT` resync and hard-stopped while fighters already agreed. Camera is cosmetic; map (Pupupu `ground_fold`) remains fail-closed after deeper exhaust.

## Fix

1. Early wire within phase_lock: clamp mismatch to `live_sim`, widen `target_override` to cover wire tick (`CORRECTION_CLAMP_EARLY_WIRE`).
2. `SYNCTEST_SKIP reason=resim_in_flight` while resim/deferred active; `SYNCTEST_FAIL` logs `emergency_ok` / `load_ok`.
3. `GameplayOnlyMap` ignores camera so map+cam still arms map resync/deeper (Pupupu fail-closed on deeper exhaust unchanged).

## Verify

Re-soak Dream Land: idle+Whispy OK; stick after idle should show `CORRECTION_CLAMP_EARLY_WIRE` instead of `deferred_queue_drop` for near-frontier wire; no `SYNCTEST_FAIL` during active resim; map forks log `PEER_BASELINE_MAP_DRIFT` before any diverge kill.
