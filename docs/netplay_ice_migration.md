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
| `SSB64_MATCHMAKING_STUN_SERVERS` | Comma-separated STUN hosts (`host:port`); if unset, defaults to coturn host **3478** (same as server) |
| `SSB64_MATCHMAKING_TURN_HOST` / `TURN_USER` / `TURN_PASS` / `TURN_PORT` | Dev fallback TURN when `/v1/turn-credentials` is unavailable (no static password fallback) |
| `SSB64_MATCHMAKING_BIND` | Local bind for ICE UDP port (default `0.0.0.0:7778`) |
| `SSB64_MATCHMAKING_ICE_TCP` | Experimental ICE-TCP active mode after `juice_create` (not TURNS) |

Automatch calls **`GET /v1/turn-credentials`** and passes `stun_host`, `stun_port`, `turn_host`, `turn_port`, `username`, and `password` into libjuice. Coturn **TURNS** on port **5349** is listed as `turns_port` in the API but is not used by libjuice (UDP TURN/STUN on **3478** only). See [`docs/bugs/libjuice_turns_audit_2026-05-25.md`](bugs/libjuice_turns_audit_2026-05-25.md).

Legacy hand-rolled `mm_stun.c` / `mm_turn.c` are removed from ICE builds.

### TURN / firewall (coturn ops)

| Port / proto | Role | BattleShip client |
|--------------|------|-------------------|
| UDP **3478** | STUN + TURN (RFC 8656) | **Used** — libjuice `stun_server_*` + `juice_turn_server_t` |
| TCP/TLS **5349** | TURNS (coturn `tls-listening-port`) | **Not used** — API `turns_port` only; logged as unused |
| ICE-TCP | libjuice `juice_set_ice_tcp_mode` | **Experimental** — `SSB64_MATCHMAKING_ICE_TCP=1`; not TURNS; needs coturn TCP relay if deployed |

**Operator checklist (no coturn config in this repo):**

1. UDP listener on **3478** with `fingerprint` + `use-auth-secret` aligned with `COTURN_STATIC_AUTH_SECRET`.
2. Optional TLS on **5349** for other clients; do not expect BattleShip to connect there until libjuice gains TURNS.
3. From a phone on cellular: `turnutils_uclient -t -u <user> -w <pass> -p 3478 <host>` — if UDP 3478 is blocked, ICE may fail even with trickle fixes; TURN over TLS is not a workaround today.
4. CGNAT / symmetric NAT: UDP TURN relay on 3478 is the supported relay path when direct host candidates fail.

## Server (BattleShip-Server)

- `POST /v1/queue` — optional `ice_sdp` (local description).
- `GET /v1/match/{ticket}` — `peer_ice_sdp`, `ice_signals[]` (trickle, drained per poll).
- `POST /v1/match/{ticket}/ice` — `{ "candidate": "..." }` trickle to peer.
- `GET /v1/turn-credentials` — coturn time-limited username/password (requires `COTURN_STATIC_AUTH_SECRET`).

See `BattleShip-Server/docs/MATCHMAKING.md` for API details.

## Flow

1. Player ready → `mmIceInit` + gather → wait in `MN_AM_BIND` until gathering done → enqueue with `ice_sdp` + srflx `udp_endpoint` (`mmIceGetSrflxHostport`).
2. Queue poll (`MN_AM_POLL`) until matched.
3. `MN_AM_ICE_CONNECT` → apply `peer_ice_sdp`, **continue** `GET /v1/match/{ticket}` via trickle poll (`EnqueuePollIceTrickle`), drain `ice_signals`, `mmIcePoll` until completed.
4. `syNetPeerRunBootstrap` over ICE (no UDP link-sync probes).
5. Staging rendezvous (`STAGE_SCENE_READY/GO`) unchanged; `SSB64_NETPLAY_AUTOMATCH_CONNECT_TIMEOUT_MS` covers ICE + staging wait.

Pre-ticket local candidates are buffered until `MM_POLL_QUEUED` assigns a ticket, then flushed via `POST /v1/match/{ticket}/ice`.

See [`docs/bugs/ice_trickle_poll_gap_2026-05-25.md`](bugs/ice_trickle_poll_gap_2026-05-25.md) for the 2026-05-25 CGNAT/mobile failure mode.
