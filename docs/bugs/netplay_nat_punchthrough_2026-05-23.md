# Netplay NAT punchthrough (socket reuse + STUN hardening)

**Date:** 2026-05-23  
**Symptom:** Automatch pairs with correct cross-matched `peer` endpoints but bootstrap reports `sent=0 recv=0` until timeout; queue-time STUN mapping invalidated when bootstrap opened a new UDP socket.

## Root cause

1. **Socket lifecycle:** `syNetPeerConfigureUdpForAutomatch` and `syNetPeerRunBootstrap` previously closed/reopened the UDP socket between queue join, reflexive attempt, and LAN retry, destroying the NAT mapping learned at queue STUN time.
2. **Stale registry:** Server `udp_endpoint` from join was not refreshed when the client's reflexive port changed before match.
3. **STUN:** Single-server, single-attempt probe gave no symmetric-NAT hint.

## Fix

- **Phase 0:** Reuse queue socket for bootstrap (`reuse_existing_socket`); `syNetPeerResetAutomatchBootstrapTransportState` no longer closes the socket; re-STUN at match; heartbeat `udp_endpoint` / `lan_endpoint` refresh (client + BattleShip-Server).
- **Phase 1:** `mmStunProbeIpv4Endpoint` — configurable servers, retries, dual-server port compare → `MM_STUN_NAT_SYMMETRIC_SUSPECTED`.
- **Phase 2 (partial):** Deferred same-WAN `peer_lan` bootstrap on `MN_AM_BOOTSTRAP_LAN` staging tick (WAN attempt still runs synchronously in the match handler).

## Follow-up (not in this change)

- Full async WAN bootstrap slices across staging frames.
- TURN relay when symmetric NAT suspected or bootstrap exhausts candidates.
