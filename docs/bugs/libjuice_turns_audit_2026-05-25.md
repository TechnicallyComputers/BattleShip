# libjuice / coturn TURNS audit (2026-05-25)

## Summary

BattleShip automatch ICE uses vendored **libjuice** with **UDP STUN/TURN on port 3478** only. The matchmaking API returns **`turns_port` (default 5349)** for coturn TLS/TURNS operators, but **libjuice has no API to connect over TLS** to coturn. Cellular or corporate firewalls that block **UDP 3478** cannot be fixed by pointing libjuice at 5349 without upstream TLS support.

## libjuice (vendored `third_party/libjuice`)

| Item | Finding |
|------|---------|
| `juice_turn_server_t` | `{ host, username, password, port }` — **UDP TURN** ([`juice.h`](../../third_party/libjuice/include/juice/juice.h) ~L80–85) |
| TURNS / TLS | **Not supported** — no TLS fields; [`turn.c`](../../third_party/libjuice/src/turn.c) implements RFC 8656 over UDP |
| `juice_set_ice_tcp_mode` | **ICE-TCP** (active client), not TURNS — separate candidate type; requires coturn TCP relay config if used |
| Second TURN on 5349 without TLS | **Invalid** — would not speak TURNS protocol |

## BattleShip-Server

[`turn_credentials.rs`](../../BattleShip-Server/src/turn_credentials.rs) and [`MATCHMAKING.md`](../../BattleShip-Server/docs/MATCHMAKING.md) document UDP-only client behavior. `GET /v1/turn-credentials` still returns `turns_port` for ops and future clients.

## Client behavior (this tree)

| Approach | Feasibility | Notes |
|----------|-------------|-------|
| TURNS 5349 via current libjuice | **Not supported** | No TLS to coturn |
| UDP TURN 3478 | **Supported** | Production path via `/v1/turn-credentials` |
| ICE-TCP (`SSB64_MATCHMAKING_ICE_TCP=1`) | **Experimental** | `juice_set_ice_tcp_mode(ACTIVE)` in [`mm_ice.c`](../../port/net/matchmaking/mm_ice.c); not TURNS; soak required |
| libjuice upgrade/fork | **Future** | Track upstream for TURNS/TLS |
| VPN / user network | **Ops** | Out of game scope |

When credentials include `turns_port != 0`, [`mm_ice_automatch.c`](../../port/net/matchmaking/mm_ice_automatch.c) logs that libjuice uses UDP `turn_port` only.

## Coturn ops checklist

- **UDP 3478:** `listening-port`, `fingerprint`, `use-auth-secret` (matches `/v1/turn-credentials` HMAC usernames).
- **TCP/TLS 5349:** `tls-listening-port`, cert paths — useful for **non–libjuice** clients; BattleShip does not allocate here today.
- **Verify UDP from Android:** `turnutils_uclient -t -u <user> -w <pass> -p 3478 <host>`
- **Verify TLS (ops only):** `turnutils_uclient` with TLS flags against 5349 — success does not imply the game client can use it.

See also [`docs/netplay_ice_migration.md`](../netplay_ice_migration.md) § TURN / firewall.

## Non-goals

- Adding a fake `juice_turn_server_t` entry on port 5348/5349 without TLS.
- Server tick authority or post-bootstrap rendezvous changes.
