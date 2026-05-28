# ICE shared-LAN relay SDP strip and abort bind ordering (2026-05-27)

**Date:** 2026-05-27  
**Status:** Fix shipped  
**Area:** `mm_ice.c`, `mm_ice_automatch.c`, `scautomatch.c`

## Symptoms

- **WiFi automatch (Linux host + Android guest, same subnet):** ICE completed with **`selected remote typ=relay`** despite `allow_peer_host=1` and valid `peer_lan`. Path validation correctly rejected the relay nomination → session abort.
- Immediately after abort: **`bind failed err=98`** (`EADDRINUSE`) in `syNetPeerNotifyAutomatchBootstrapPeerAbort` — ICE socket still held the bind port because **`mnVSNetAutomatchAMIceNotifyPeerAbort` ran before `mmIceShutdown`**.

## Root cause

1. **Relay in peer SDP:** Trickle relay was dropped when `allow_peer_host=1`, but **`typ=relay` lines embedded in the peer's initial SDP were passed through** `juice_set_remote_description`. libjuice could nominate relay even on a shared LAN where host↔host should win.
2. **Abort ordering:** Bootstrap abort notify opens a second UDP socket on the same bind spec while libjuice still owns the ICE bind.

## Fix

- **`mmIceStripRelayCandidatesFromSdp`:** when `allow_peer_host=1`, strip remote **`typ=relay`** lines from peer SDP before `juice_set_remote_description` (mirror of cross-NAT host strip).
- **`mmIceShouldSignalLocalCandidate`:** do not signal local relay when shared LAN (symmetric policy).
- **`ICE_LAN_RELAY_SETTLE_TICKS` (60):** if libjuice still completes on relay briefly, wait for host-pair re-nomination before path validation abort.
- **`scautomatch.c` ICE_CONNECT failure paths:** call **`mmIceShutdown()` before `mnVSNetAutomatchAMIceNotifyPeerAbort()`** so bootstrap abort UDP can bind.

## Out of scope

- Hash drift / frame-commit state recovery / intro-wait resim guards (separate diagnostics).

## Verification

- Shared-LAN automatch: logs show relay stripped from peer SDP when applicable; ICE completes on host or srflx, not relay.
- ICE connect failure / bootstrap failure: peer abort notify succeeds without `EADDRINUSE`; no second bind on occupied port.
