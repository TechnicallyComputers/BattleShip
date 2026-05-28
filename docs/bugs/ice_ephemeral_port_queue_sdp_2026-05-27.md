# ICE ephemeral port queue SDP regression (2026-05-27)

**Date:** 2026-05-27  
**Status:** Fix shipped  
**Area:** `mm_ice_automatch.c`, `mm_ice.c`, `mm_lan_detect.c`, `scautomatch.c`

## Symptoms

- Instant return to character select on automatch search (both Android and Linux).
- `POST /v1/queue` → **HTTP 400** (server requires `ice_sdp` with `a=ice-ufrag:`).
- Logs: `ice_sdp_len=20`, `WARNING join queue ice_sdp missing a=ice-ufrag`, `omitted local host candidate(s) from signaling SDP (no local LAN)`.
- `Automatch LAN detect: using 192.168.66.x:0` (invalid port hint before gather).

## Root cause

Ephemeral bind (`0.0.0.0:0`) left `sIceLocalLan` empty until after gather. At `IcePlayerReady`, `mmIceSetCandidatePolicy(..., signal_local_host=0)` was locked for the session. On bind tick, `mmIceFilterHostFromSignalingSdp` stripped all host candidates from LAN-direct SDP (only `a=ice-options` remained). Matchmaking rejected the queue body.

Secondary: `mmIceGetLocalHostHostport` returned the first gathered host (e.g. public `25.x` on Android, `.91` on dual-NIC host) instead of the preferred RFC1918 interface.

## Fix

1. **Bind tick order:** refresh `local_lan` from gather → update candidate policy (signal local host when LAN-direct / `local_lan` known) → build queue SDP → strip host lines only for cross-NAT → post-filter ufrag gate.
2. **Player ready:** defer strict `signal_local_host=0` when `likely_lan`; set provisional policy for LAN-direct gather.
3. **Queue hard gate:** do not `EnqueueJoinQueueIce` unless `ice_sdp` contains `a=ice-ufrag:`.
4. **RFC1918 host pick:** `mmLanPickBestHostportFromCandidates` + upgraded `mmIceGetLocalHostHostport`.
5. **WAN vs LAN:** queue `udp_endpoint` prefers srflx, then RFC1918 host bind (not carrier/public relay IP).
6. **ICE completed settle:** retry `GetSelectedPath` ~30 ticks before path-validation abort; cache remote for bootstrap.

## Verification

- LAN soak: queue HTTP 200, `ice_sdp` length >> 20 with ufrag, ICE connect proceeds.
- No `host:0` in queue registration after ephemeral bind.
