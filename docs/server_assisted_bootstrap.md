# Server-assisted bootstrap

Optional **server-coordinated pre-battle handshake** that complements existing P2P UDP bootstrap. The goal is to reduce WiFi / jitter sensitivity by centralizing **exchange and conservative timing policy** while keeping **simulation and inputs P2P** (no server tick authority during battle unless scope is expanded later).

This document complements [`docs/netplay_architecture.md`](netplay_architecture.md) (overall net stack, VS gate, netinput) and [`docs/netcode_agent_rules.md`](netcode_agent_rules.md) (overlay policy under `port/net/`).

---

## Problem

Barrier timing and clock negotiation today live entirely in the netpeer layer (`port/net/sys/netpeer.c` when `SSB64_NETMENU` is enabled): `TIME_PING` / `TIME_PONG` use wall clock (`CLOCK_REALTIME`), median offset / RTT picks, `SYNETPEER_MIN_START_LEAD_MS`, jitter slack, VI quantization, and skew retry when offset spread exceeds `SSB64_NETPLAY_BARRIER_MAX_CONTRACT_SKEW_MS`. On WiFi, spread grows and retries latch, producing **non-deterministic mistiming** across peers even when payloads match.

**Goal:** Use a matchmaking / coordinator server (HTTPS path via `port/net/matchmaking/mm_matchmaking.h`) only for **bootstrap contract**: rendezvous, optional RTT aggregation, and **one authoritative “start epoch”** both clients obey — **not** to advance battle ticks in-session.

**Key invariant:** Battle **sim tick indexing** and **input bundles** remain **P2P** and **logical** (`tick` in packets). The server influences **wall-clock barrier only** (and optionally **recommended env knobs**: sample count, settle rounds, extra lead). **Never** mix server clock into `syNetInputTick` or sim tick indexing.

---

## Architectural sketch

```mermaid
sequenceDiagram
  participant CA as Client_A
  participant S as Bootstrap_Server
  participant CB as Client_B
  Note over CA,CB: Phase pre-battle only
  CA->>S: Register session + ping samples (or pings via relay)
  CB->>S: Register session + ping samples (or pings via relay)
  S->>S: Merge RTTs; conservative deadline policy
  S->>CA: agreed_epoch barrier_deadline_ms policy_json
  S->>CB: agreed_epoch barrier_deadline_ms policy_json
  Note over CA,CB: Existing UDP P2P unchanged for inputs
  CA->>CB: BATTLE_START_TIME still sent host to guest (optional redundancy)
```

---

## Design tiers

| Tier | Server role | Pros | Cons |
|------|-------------|------|------|
| **A – Coordinator only** | Clients POST opaque summaries (min/median RTT, offset spread, sample count); server returns **deadline_ms + epoch** + **policy** | Minimal protocol; game still measures UDP | Still trusts client summaries unless signed |
| **B – Server-measured RTT** | Short-lived pings **through server** (TCP or UDP); server computes conservative **lead** from **max observed RTT + margin** | Symmetric path | Adds client↔server RTT unless carefully designed |
| **C – Hybrid** | Automatch already returns `session_id` / endpoints (`MmMatchResult`); extend backend with `/sessions/{id}/bootstrap` after match | Natural fit for existing curl flow | Requires backend work |

**Recommendation:** Evolve toward **C + B-lite**: after `MmMatchResult` is drained, both clients call **`POST /v1/sessions/{match_id}/bootstrap/ping`** a few times; the server records arrivals with a **monotonic server clock** and responds with **`bootstrap_epoch`**, **`barrier_deadline_unix_ms`**, **`recommended_extra_lead_ms`**, **`clock_sync_rounds`**. Clients **override or bias** the local host path in `syNetPeerHostFinishClockSyncAndSendStart` when server bootstrap is enabled, instead of relying purely on local `lead_ms` computation.

---

## Client integration (current tree)

1. **Module:** `port/net/bootstrap/mm_server_barrier.{c,h}` — gated by `PORT` + `SSB64_NETMENU` (all host OSes when netmenu is enabled, including Windows MSVC and Linux→MinGW cross). It uses the same HTTPS/curl patterns as `mm_matchmaking.c` when fully implemented.

2. **Netpeer fork:** After local clock sync computes `start_ms_raw` and quantized `start_ms` in `syNetPeerHostFinishClockSyncAndSendStart`, **`mmServerBarrierTryApplyHostSchedule`** may replace `start_ms_raw` with `barrier_deadline_unix_ms` when `contract_complete` is true; netpeer then re-applies VI ceil quantization if `SSB64_NETPLAY_BARRIER_VI_ALIGN` is on.

3. **Context:** `syNetPeerSetAutomatchBootstrapContext(match_id, ticket_id)` is set after automatch bootstrap (ICE and legacy paths) and cleared on automatch reset / VS session stop.

4. **Guest ping:** `mmServerBarrierPostPing()` runs at `syNetPeerStartVSSession` so the server sees both peers before the host finishes clock sync (non-host does not override schedule).

5. **Fallback:** If HTTPS fails, credentials are missing, or `contract_complete` is false → **existing P2P-only path**. Log `SSB64 NetPeer: server bootstrap …` once per session unless verbose.

6. **Security / abuse:** Session id + short-lived JWT or ticket from automatch (`ticket_id` in `MmMatchResult`) bound to bootstrap POST — noted for production hardening; out of scope for the first stub.

---

## Rollout flags

| Variable | Meaning |
|----------|---------|
| `SSB64_NETPLAY_SERVER_BOOTSTRAP` | When set to `1`, enables HTTPS barrier coordination via `mm_server_barrier.c` (`POST /v1/sessions/{match_id}/bootstrap/ping`). When unset or `0`, only the local lead path runs. |
| `SSB64_NETPLAY_SERVER_BOOTSTRAP_VERBOSE` | When set to `1`, logs each bootstrap POST and parsed contract fields. |

Related existing knobs (unchanged): `SSB64_NETPLAY_BARRIER_EXTRA_LEAD_MS`, `SSB64_NETPLAY_BARRIER_MAX_CONTRACT_SKEW_MS`, clock sync sample env vars documented in [`docs/netplay_pacing.md`](netplay_pacing.md) where applicable.

---

## Fallback matrix

| Condition | Behavior |
|-----------|----------|
| `SSB64_NETPLAY_SERVER_BOOTSTRAP` unset / `0` | Pure local `lead_ms` + VI quantization (today). |
| Flag `1`, HTTPS unavailable or non-200 | Local schedule; log fallback reason. |
| Flag `1`, response stale / wrong session | Local schedule; log and optionally bump barrier epoch in future impl. |
| Flag `1`, contract OK | Host sets `sSYNetPeerBattleStartUnixMs` / barrier deadline from server values (quantized with existing `syNetPeerQuantizeCeilUnixMs` / VI align policy). |

---

## REST / OpenAPI

Machine-readable contract: [`openapi/server_assisted_bootstrap.yaml`](openapi/server_assisted_bootstrap.yaml) (fragment: `POST …/bootstrap/ping`, response fields for epoch and deadline).

---

## Risks and mitigations

- **Extra latency:** One HTTPS RTT before VS scene — acceptable if only at bootstrap.
- **Server SPOF:** Fallback to pure P2P; match may fail closed if both paths fail (product decision).
- **Clock skew vs wall deadline:** Keep using **quantized wall deadline** only for **barrier wait**; never feed server wall time into sim ticks.

---

## Phase 2 (separate PR — optional follow-up)

**Netsync-triggered recovery + re-bootstrap:** If ongoing play detects divergent NetSync hashes or sustained mismatch, enter a **recovery mode** (freeze / widen delay / optional server **re-bootstrap** endpoint). This is **not** part of the MVP server-assisted bootstrap track; it avoids coupling scope explosion. Implement as a dedicated change set after bootstrap HTTPS is stable.

See also: barrier and execution gate overview in [`docs/netplay_architecture.md`](netplay_architecture.md).
