# ICE automatch cross-NAT gather + peer SDP (2026-05-27)

**Status:** FIX SHIPPED

## Symptoms

- LTE Android + LAN PC: matcher pairs quickly, then `juice_set_remote_description skipped (no a=ice-ufrag after filter)` on both sides; 60s ICE_CONNECT timeout.
- Linux host logged `LAN-direct gather (no STUN/TURN)` and queued host-only `ice_sdp`; LTE peer could not apply remote description after cross-NAT host filter.

## Root cause

1. Automatch enabled **LAN-direct gather** whenever RFC1918 existed (`ICE_LAN_DIRECT` default via `mmIceEnvLanDirectEnabled`), so the host never advertised srflx/relay in queue SDP.
2. Cross-NAT **host candidate filter** could leave peer SDP without session `a=ice-ufrag:` when session lines were stripped with malformed single-line SDPs.
3. **BeginConnect** continued into `ICE_CONNECT` after peer SDP apply failure.

## Fix

- Automatch: **full STUN/TURN gather by default**; skip only when `SSB64_MATCHMAKING_ICE_LAN_DIRECT=1` explicitly.
- Preserve `a=ice-ufrag` / `a=ice-pwd` before host strip; only treat `a=candidate:` lines as host filter targets.
- **Fail fast** on peer SDP apply (`BeginConnect` returns false → abort, no 60s zombie).
- Register **`turn_endpoint`** on queue when relay candidate gathered (`mmIceGetRelayHostport`).

## Verification

LTE + LAN automatch: Linux log shows STUN/TURN gather (not LAN-direct unless env=1); both sides `juice_set_remote_description` succeeds; ICE reaches completed or fails fast with clear reason.
