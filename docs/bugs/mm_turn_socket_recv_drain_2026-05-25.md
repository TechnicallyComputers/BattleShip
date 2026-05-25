# TURN Allocate: shared-socket recv drain + matching reply loop (2026-05-25)

**Slug:** `mm_turn_socket_recv_drain_2026-05-25`
**Status:** FIX SHIPPED (soak pending)
**Severity:** High — TURN relay never acquired despite coturn reachable (`turnutils_uclient` OK)

## Symptom

`turnutils_uclient -u netplay -w … -p 3478 -y coturn.technicallycomputers.ca` succeeds, but BattleShip logs:

`SSB64 Automatch TURN: allocate failed server=216.154.76.149:3478 (no response from coturn)`

and automatch joins with `turn=(none)`.

The logged IP is the **DNS A record** for `coturn.technicallycomputers.ca`, not a wrong TURN host override.

## Root cause (application-side)

TURN runs on the **same UDP socket** as STUN binding and netplay (`syNetPeerGetUdpSocketFd()`, typically bound to `:7778`).

1. **Single `recvfrom` per attempt** — `mmTurnSendRecv` returned the first datagram in the queue. Late **STUN Binding** replies (from `mmStunProbeIpv4Endpoint` on the same fd) or other stray packets were consumed as the “Allocate response”. Transaction ID / method checks failed; the real coturn **401** or **success** reply was never read → logged as “no response”.

2. **Short timeout** — 600 ms × 3 attempts was tight vs coturn’s 401 + authenticated retry round-trip.

3. **Static transaction IDs** — `mmTurnFillTxId` used only `getpid()` (identical every call in-process), increasing collision risk when stray STUN packets shared the socket.

The earlier **STUN class mask** bug (`0xC100` vs `0x0110`, see `mm_turn_stun_class_mask_2026-05-25.md`) prevented handling 401 when a matching packet *was* received; this fix addresses the case where the matching packet was **never selected**.

## Fix

`port/net/matchmaking/mm_turn.c`:

1. **`mmTurnDrainSocket`** — non-blocking drain (up to 64 datagrams) before Allocate; logs count when > 0.
2. **`mmTurnSendRecvAllocate` / `mmTurnSendRecvForMethod`** — after `sendto`, loop `select`+`recvfrom` until deadline (3 s wall) discarding datagrams until **matching** tx_id + method + class (Allocate: SUCCESS or ERROR; permission/bind: SUCCESS).
3. **`mmTurnFillTxId`** — `getrandom` / `arc4random` / Android `/dev/urandom` (same policy as `mm_stun.c`).
4. **5 allocate attempts**, 500 ms select slices, logs `server=hostname (resolved_ip:port)` and `stun_err=` when coturn returns ERROR-CODE.
5. **XOR-RELAYED-ADDRESS** — require IPv4 family bytes `(0, 1)` before decode.

## Verification

1. Same machine as failing session: automatch should log `SSB64 Automatch TURN: relay=… server=coturn.technicallycomputers.ca (216.154.x.x:3478)`.
2. If credentials wrong: `stun_err=401` (not “no response”).
3. If STUN leftovers were the issue: one-time `drained N stale datagram(s) before Allocate` then success.

## Related

- `mm_turn_stun_class_mask_2026-05-25.md` — 401 class mask + REALM/NONCE null termination
- `netplay_nat_punchthrough_2026-05-23.md` — TURN relay in match JSON as `turn_endpoint` / `peer_turn`
