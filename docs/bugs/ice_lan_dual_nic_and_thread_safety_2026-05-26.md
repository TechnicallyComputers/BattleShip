# ICE LAN dual-NIC alignment and thread safety (2026-05-26)

**Date:** 2026-05-26  
**Status:** Fix shipped  
**Area:** `mm_ice.c`, `mm_ice_automatch.c`, `mm_matchmaking.c`, `netpeer.c`, `scautomatch.c`

## Symptoms

- Intermittent **SIGABRT** on Android during `MN_AM_ICE_CONNECT` trickle polling (signal queue race, prior fix).
- **SIGABRT on Linux host** immediately after `staging_ready recv` when sending `STAGE_SCENE_GO`.
- LAN automatch: queue reported **`peer_lan=192.168.66.12`** while ICE nominated **`192.168.66.91`** (dual-NIC host).
- Host stderr: TURN **403/437** and STUN ufrag mismatch on rapid rematches.
- Guest stuck ~60s on staging after host crash; **`ICE: state=failed`**, `input_bind send_fail os_err=9`.
- **Post Phase-1 regression:** main thread hung ~75s at `mmIceStartGathering` → `pthread_mutex_lock` in `agent_gather_candidates`; watchdog SIGABRT (self-deadlock on non-recursive `sIceMutex` during synchronous gather callbacks).

## Root cause

1. **Juice API races:** `JUICE_CONCURRENCY_MODE_THREAD` callbacks used `sIceMutex`, but game-thread `juice_*` calls (`mmIceSend`, `juice_add_remote_candidate`, etc.) did not — heap corruption / abort under staging and trickle load.
2. **Dead `mmIceSignalQueueClear`:** trickle ring never cleared on automatch reset → stale signals / ufrag bleed across rematches.
3. **Relay filter bug:** `mmIceShouldAcceptRemoteCandidate` returned `TRUE` for all types when `allow_peer_host=1`, making LAN relay drop unreachable.
4. **Bind ignored:** `SSB64_MATCHMAKING_BIND` only parsed **port**; `juice_config.bind_address` unset → gather on all interfaces; `mmLanDetectEndpoint` picked a different NIC than ICE nomination.
5. **TURN on LAN:** unnecessary relay gather → coturn CreatePermission noise; not required for same-subnet host↔host.

## Fix

- **`sIceMutex` scope (Option B):** mutex protects port-owned recv/candidate queues and callback flags only. All `juice_*` calls run **without** holding `sIceMutex` (libjuice `conn_lock` serializes the agent). Holding the port mutex across `juice_gather_candidates` deadlocked when libjuice invoked `cb_candidate` / `cb_state_changed` synchronously on the same thread.
- **Async gather (2026-05-27):** `juice_gather_candidates` runs on a dedicated thread; `MN_AM_BIND` tick polls until gathering done. Game thread no longer blocks during STUN DNS / conn setup.
- **`mmIcePoll`:** drain candidate/gathering-done queues under mutex; invoke port callbacks after unlock (avoids ICE_CONNECT enqueue deadlocks).
- **LAN-direct:** skip STUN server (`config.stun_server_host = NULL`) when `lan_direct_gather`; queue WAN falls back to bind/LAN endpoint when no srflx.
- Export **`mmMatchmakingIceSignalsClear()`**; call on ICE/automatch reset and `MN_AM_ENTER`.
- **`mmIceParseBindHostFromSpec`** → `config.bind_address`; prefer **`SSB64_MATCHMAKING_LAN_ENDPOINT`** / bind host for `local_lan`.
- **`SSB64_MATCHMAKING_ICE_LAN_DIRECT`** (default on): skip TURN gather when `local_lan` known at player ready.
- Relay filter: drop peer **`typ=relay`** when shared LAN; tiered **`mmIceValidateSelectedRemotePath`** (subnet vs WAN register).
- **Path validation (2026-05-27):** shared-LAN check uses **`local_lan`** (our NIC on the LAN segment), not **`peer_lan`** (peer's phone IP). Previously rejected valid `.6`↔`.91` pairs when host had multiple NICs.
- **Trickle (2026-05-27):** defer **`remote gathering done`** until ICE connected + longer quiet period; drop late remote trickle after gathering done (no libjuice add); suppress outbound local trickle after connected.
- **`mmIceIsFailed()`** fast abort in `MN_AM_ENTER`; staging GO send bracket logs.
- Netpeer ICE transport unusable when failed (not only disconnected).

## Dual-NIC host setup (recommended)

Router port-forward (coturn, matchmaking HTTP) targets primary NIC (e.g. **192.168.66.3**).  
Isolated netplay UDP on secondary NIC (e.g. **192.168.66.91**):

```bash
export SSB64_MATCHMAKING_BIND=192.168.66.91:7778
export SSB64_MATCHMAKING_LAN_ENDPOINT=192.168.66.91:7778
# optional: export SSB64_MATCHMAKING_LAN_INTERFACE=eth1
```

## Verification

- LAN automatch Android guest + Linux host: ICE completes on **same host IP** in queue and nomination; host logs **`staging_go host send done`** without SIGABRT.
- Rapid rematches: no STUN ufrag cross-talk; guest **`ICE lost`** within seconds if host dies mid-staging.
