# Automatch bootstrap stage/world desync at GO — 2026-05-29

**Status:** FIX SHIPPED (soak pending)  
**Symptom:** Cross-ISA automatch (e.g. Linux host + Android client): `world` hash diverges from tick 1; first `FRAME_COMMIT` validation @120 shows agreed inputs/RNG/fighter but mismatched `world` (item spawn tables). Sector Z (Fox stage) and other stages affected when metadata differs.

## Root cause

1. **Premature client `READY`:** Automatch client sent `READY` before receiving host `MATCH_CONFIG`. Host could exit its config loop on that `READY`, stop retransmitting `MATCH_CONFIG`, and proceed to `START` while the guest still had no staged host-authoritative `stage_kind` / `rng_seed`.

2. **Stage/RNG mix used `session_id`:** `syNetPeerComposeAutomatchMatchMetadata()` indexed the level pool with `seed_pick ^ session_id`. Match-scoped identity should use the shared **`match_id`** string from the matcher so stage/RNG are stable for the pair regardless of transport/session quirks.

3. **ICE path scene advance:** LAN/TURN bootstrap set `gSYNetPeerSuppressBootstrapSceneAdvance` until `scVSBattleStartBattle` commits metadata; ICE automatch did not, allowing earlier scene/metadata application timing skew vs LAN.

4. **Stale bootstrap struct:** Reset cleared staged/applied flags but left `sSYNetPeerBootstrapMetadata` bytes from a prior attempt.

## Fix

- `port/net/sys/netpeer.c`: remove early client `READY`; `syNetPeerHashBootstrapMatchIdU32()` for pool index and RNG mix; `memset` metadata on bootstrap reset; log composed/staged/dropped `MATCH_CONFIG`.
- `decomp/src/netplay/sc/sccommon/scautomatch.c`: ICE bootstrap wraps `gSYNetPeerSuppressBootstrapSceneAdvance` like LAN/TURN.

## Fox / Sector Z note

Intro `world` fork here is **bootstrap metadata** (spawn/item manager), not Fox Firefox or arwing rollback (see `netplay_fox_firefox_launch_gate_2026-05-28.md`, Sector ground blobs in `netrollbacksnapshot.c`). Re-test Sector Z after soak with matching `automatch MATCH_CONFIG staged` / `metadata composed` lines on both peers.

## Test plan

- Cross-ISA automatch soak: both logs show same `stage=` and `seed=` on `MATCH_CONFIG staged` (client) and `metadata composed` / `bootstrap host sent START` (host).
- `FRAME_COMMIT` @120: `world` agrees through intro.
- Sector Z + Hyrule from level-pref pool with overlapping ban masks.
