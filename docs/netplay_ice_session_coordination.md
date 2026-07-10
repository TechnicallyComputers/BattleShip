# ICE session coordination (2p shipped, 4p schema)

HTTPS signaling for ICE **role assignment** and **connect ordering**, separate from trickle candidates and from netplay **sim authority** (`you_are_host` / `sim_authority_player_id`).

## 2p automatch (shipped)

- **Edge:** single `"pair"` between the two matched players.
- **ICE controlling:** sim host (`you_are_host=true`) — same player as `sim_authority_player_id` in poll JSON.
- **Flow:**
  1. Host: `juice_set_ice_controlling(true)` → `POST /v1/match/{ticket}/ice/role-ready` → apply peer SDP.
  2. Guest: `juice_set_ice_controlling(false)` → poll until `ice_connect.edges[].peer_controlling_ready==true` → apply peer SDP.
- **Poll:** `GET /v1/match/{ticket}` includes `ice_connect` on every matched response (including trickle polls during `ICE_CONNECT`).

See [ice_role_ready_coordination_2026-05-27.md](bugs/ice_role_ready_coordination_2026-05-27.md).

## Poll object: `ice_connect`

```json
"ice_connect": {
  "topology": "pair",
  "connect_epoch": 1,
  "sim_authority_player_id": "uuid",
  "edges": [{
    "edge_id": "pair",
    "local_role": "controlling | controlled",
    "controlling_player_id": "uuid",
    "peer_controlling_ready": false
  }]
}
```

| Field | Meaning |
|-------|---------|
| `topology` | `pair` today; `star`, `mesh`, `custom` for 4p/custom lobbies |
| `connect_epoch` | Bump on session reconnect / ICE restart (future) |
| `sim_authority_player_id` | Netplay sim host (bootstrap START, slot 0 authority in 2p) |
| `edge_id` | Stable edge key (2p: `"pair"`) |
| `local_role` | Viewer's ICE role on this edge |
| `controlling_player_id` | Who must nominate / post `role-ready` on this edge |
| `peer_controlling_ready` | Controlling peer has posted `role-ready` |

## POST `ice/role-ready`

```json
{ "edge_id": "pair", "connect_epoch": 1 }
```

Only `controlling_player_id` for that edge may POST (`403` otherwise).

## 4p follow-on (not implemented)

### Edge graph

For N players, edges are pairwise links. Suggested **edge_id**:

```text
e:<min_uuid>:<max_uuid>
```

(lexicographic min/max `player_id`, same determinism as today's host pick.)

### Controlling assignment (per edge)

**Default rule:** controlling = lexicographically smaller `player_id` on that edge.

**Sim authority** remains one `sim_authority_player_id` (lobby host / match host) — independent of ICE controlling on each edge.

### Topology examples

| Topology | Edges | Notes |
|----------|-------|-------|
| `pair` | 1 | Current automatch |
| `star` | N−1 | Sim host ↔ each guest; host controlling on all host edges if host is smaller UUID on each edge, else mixed |
| `mesh` | n(n−1)/2 | Each client runs multiple libjuice agents; gate SDP apply per edge |
| `custom` | Lobby-defined | Mixed LAN/relay policies per edge |

### Typed signals (future unification)

| `type` | 2p transport | Purpose |
|--------|--------------|---------|
| `candidate` | `POST .../ice` `{ "candidate": "..." }` | Trickle ICE |
| `ice_role_ready` | `POST .../ice/role-ready` | Controlling peer ready for checks |
| `ice_desc_applied` | TBD | Optional ack after remote SDP applied |

4p may fold types into one `POST .../ice` envelope or keep sub-routes; poll always merges latest `ice_connect` + trickle queue.

### Client port work (4p)

- Multiple `juice` agents per session (one per remote edge).
- Per-edge deferred SDP + `peer_controlling_ready` wait.
- Match/lobby payload lists all edges for this client.

## Server

BattleShip-Server: [`src/ice_connect.rs`](../../BattleShip-Server/src/ice_connect.rs), [`docs/MATCHMAKING.md`](../../BattleShip-Server/docs/MATCHMAKING.md).

## Client

- [`port/net/matchmaking/mm_matchmaking.c`](../port/net/matchmaking/mm_matchmaking.c) — parse cache, `EnqueueIceRoleReady`
- [`port/net/matchmaking/mm_ice_automatch.c`](../port/net/matchmaking/mm_ice_automatch.c) — host/guest gating

Automatch **requires** a server that returns `ice_connect` (no legacy fallback).
