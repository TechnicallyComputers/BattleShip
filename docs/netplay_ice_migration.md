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
| `SSB64_MATCHMAKING_BIND` | Local bind `host:port` (default **`0.0.0.0:0`** = OS ephemeral). **`host:0`** = ephemeral on that NIC. **`host:min-max`** or **`SSB64_MATCHMAKING_PORT_RANGE`** for a scan range. Legacy fixed port: `0.0.0.0:7778`. Host IP sets libjuice `bind_address`. |
| `SSB64_MATCHMAKING_LAN_ENDPOINT` | Registered `lan_endpoint` for queue/heartbeats; also used as bind IP fallback when bind is `0.0.0.0` |
| `SSB64_MATCHMAKING_LAN_INTERFACE` | Restrict auto LAN detect to one interface name (`eth1`, etc.) |
| `SSB64_MATCHMAKING_ICE_LAN_DIRECT` | When `1`: skip STUN/TURN at queue gather (dev LAN soak). **Automatch default is full STUN/TURN** at queue join so LTE+LAN cross-NAT still gets TURN; shared-LAN uses signaling/path policy only (no second gather). |
| `SSB64_MATCHMAKING_ICE_TCP` | Experimental ICE-TCP active mode after `juice_create` (not TURNS) |
| `SSB64_MATCHMAKING_ICE_SIGNAL_HOST` | Always trickle local host candidates (default: omit when no local LAN) |
| `SSB64_MATCHMAKING_ICE_VERBOSE` | Log ICE candidate filter / policy |
| `SSB64_MATCHMAKING_ICE_REQUIRE_TURN` | Force TURN before gather; default on when no local LAN |

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

1. Player ready → `mmIceInit` + gather (default bind `0.0.0.0:0` ephemeral) → wait in `MN_AM_BIND` until gathering done → refresh `local_lan` + candidate policy → enqueue with full `ice_sdp` (ufrag + host when LAN-direct) and srflx or RFC1918 `udp_endpoint` / `lan_endpoint`.
2. Queue poll (`MN_AM_POLL`) until matched.
3. `MN_AM_ICE_CONNECT` → host: `POST /v1/match/{ticket}/ice/role-ready` then apply `peer_ice_sdp`; guest: defer peer SDP until poll `ice_connect.edges[].peer_controlling_ready` (trickle polls refresh). **Continue** `GET /v1/match/{ticket}` via trickle poll, drain `ice_signals`, `mmIcePoll` until completed. Requires server `ice_connect` in match JSON ([`netplay_ice_session_coordination.md`](netplay_ice_session_coordination.md)).

### Shared-LAN (single gather + policy)

Automatch uses **one** ICE agent/gather per attempt (see [`docs/bugs/ice_cross_nat_automatch_gather_2026-05-27.md`](bugs/ice_cross_nat_automatch_gather_2026-05-27.md)). After `match_found` on a shared subnet, `mnVSNetAutomatchAMIceBeginConnect` applies candidate policy only — **no** agent teardown or second gather ([`docs/bugs/ice_lan_single_gather_policy_2026-05-27.md`](bugs/ice_lan_single_gather_policy_2026-05-27.md)):

- Strip peer `typ=relay` from SDP when `allow_peer_host`; suppress local relay signaling on LAN.
- Path validation + relay settle before aborting a bad nomination.
- `turn_endpoint` omitted on queue when `ICE_LAN_DIRECT=1`, or when `local_lan` is known and relay would be unused (`mnVSNetAutomatchAMIceShouldQueueTurnEndpoint`).
- **CreatePermission 403** during initial TURN gather on a LAN-only match is **benign ops noise**; connectivity should still complete host↔host. To avoid TURN entirely on LAN dev soaks, set `SSB64_MATCHMAKING_ICE_LAN_DIRECT=1` + `BIND`/`LAN_ENDPOINT` ([`docs/bugs/ice_lan_dual_nic_and_thread_safety_2026-05-26.md`](bugs/ice_lan_dual_nic_and_thread_safety_2026-05-26.md)).

4. `syNetPeerRunBootstrap` over ICE (no UDP link-sync probes).
5. Staging rendezvous (`STAGE_SCENE_READY/GO`) unchanged; `SSB64_NETPLAY_AUTOMATCH_CONNECT_TIMEOUT_MS` covers ICE + staging wait.

Pre-ticket local candidates are buffered until `MM_POLL_QUEUED` assigns a ticket, then flushed via `POST /v1/match/{ticket}/ice`.

See [`docs/bugs/ice_trickle_poll_gap_2026-05-25.md`](bugs/ice_trickle_poll_gap_2026-05-25.md) for the 2026-05-25 CGNAT/mobile failure mode.
