# Netplay ICE migration (libjuice)

BattleShip automatch uses [libjuice](https://github.com/paullouisageneau/libjuice) for full ICE (STUN + TURN + connectivity checks) when built with `SSB64_NETPLAY_ICE=ON` (default with `SSB64_NETMENU`).

## Client

- `port/net/matchmaking/mm_ice.c` — libjuice agent wrapper (thread-safe recv/candidate queues).
- `port/net/matchmaking/mm_ice_automatch.c` — automatch glue (queue → ICE connect → netpeer bootstrap).
- `port/net/sys/netpeer.c` — all SSNP egress/ingress via `mmIceSend` / `mmIcePopReceived` when ICE transport is active.
- Submodule: `third_party/libjuice`.

### Environment

| Variable | Purpose |
|----------|---------|
| `SSB64_MATCHMAKING_STUN_SERVERS` | Comma-separated STUN hosts (`host:port`, default Google) |
| `SSB64_MATCHMAKING_TURN_HOST` / `TURN_USER` / `TURN_PASS` / `TURN_PORT` | TURN server (overridden by `/v1/turn-credentials` when available) |
| `SSB64_MATCHMAKING_BIND` | Local bind for ICE UDP port (default `0.0.0.0:7778`) |

Legacy hand-rolled `mm_stun.c` / `mm_turn.c` are removed from ICE builds.

## Server (BattleShip-Server)

- `POST /v1/queue` — optional `ice_sdp` (local description).
- `GET /v1/match/{ticket}` — `peer_ice_sdp`, `ice_signals[]` (trickle, drained per poll).
- `POST /v1/match/{ticket}/ice` — `{ "candidate": "..." }` trickle to peer.
- `GET /v1/turn-credentials` — coturn time-limited username/password (requires `COTURN_STATIC_AUTH_SECRET`).

See `BattleShip-Server/docs/MATCHMAKING.md` for API details.

## Flow

1. Player ready → `mmIceInit` + gather → enqueue with `ice_sdp` + reflexive `udp_endpoint`.
2. Match → apply `peer_ice_sdp`, trickle `ice_signals`, `mmIcePoll` until completed.
3. `syNetPeerRunBootstrap` over ICE (no UDP link-sync probes).
4. Staging rendezvous unchanged.
