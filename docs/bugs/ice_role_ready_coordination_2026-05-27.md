# ICE role_ready server coordination (2026-05-27)

**Date:** 2026-05-27  
**Status:** Fix shipped  
**Area:** BattleShip-Server `ice_connect.rs`, `mm_matchmaking.c`, `mm_ice_automatch.c`

## Symptoms

- libjuice stderr: `ICE role conflict (both controlled)` during 2p WiFi automatch.
- Guest applied peer SDP and sent STUN checks while host was still in `MN_AM_POLL` (both peers briefly **controlled**).

## Root cause

ICE controlling role was set only at `BeginConnect` on each client. Match poll latency is asymmetric (guest often matches tens to hundreds of ticks before host), so the guest could start connectivity checks before the host promoted to **controlling**.

## Fix

**Server-mediated `role_ready` (edge-scoped, 4p-ready schema):**

- `IceConnectStore` registers one edge `"pair"` per 2p match; `sim_authority_player_id` = sim host.
- Host: `POST /v1/match/{ticket}/ice/role-ready` after `mmIceSetIceControlling(true)`.
- Poll: `ice_connect.edges[].peer_controlling_ready` on every `GET /v1/match/{ticket}` (including trickle polls).
- Guest: defer `mmIceApplyPeerIceSignaling` until `peer_controlling_ready`; apply from deferred buffer when trickle poll updates cache.

**Rollout:** clients require `ice_connect` in match JSON (fail fast if missing).

## Verification

- WiFi 2p automatch: guest log `deferring peer SDP` → `waiting for peer controlling role_ready` → `peer SDP applied after controlling role_ready`; no `both controlled` on stderr.
- Host log: `role=controlling` then role-ready POST; immediate peer SDP apply.
- Old server without `ice_connect`: guest abort `ICE server missing ice_connect`.

## See also

- [netplay_ice_session_coordination.md](../netplay_ice_session_coordination.md) — 4p edge graph + typed signals
- [ice_automatch_followups_2026-05-26.md](ice_automatch_followups_2026-05-26.md) — prior controlling/controlled defaults
