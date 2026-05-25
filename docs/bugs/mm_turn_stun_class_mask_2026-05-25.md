# TURN Allocate: STUN error-class mask and REALM/NONCE parsing (2026-05-25)

## Symptom

Coturn reachable on UDP 3478 (ICE/`turnutils_uclient` OK) but client logs:

`SSB64 Automatch TURN: allocate failed ... (no response from coturn)`

or allocate never completes; `turn=(none)` on queue.

## Root cause

1. **`mmTurnCheckResponse`** used class mask `0xC100` instead of RFC 5389 `0x0110`. Allocate **401** responses (`0x0113`) were not recognized as errors, so the client never parsed **REALM**/**NONCE** or sent the authenticated Allocate retry.

2. **`mmTurnParseAttr`** copied REALM/NONCE without a trailing `'\0'`, so `strlen()` in MESSAGE-INTEGRITY could read garbage and fail auth even after (1).

## Fix

- `port/net/matchmaking/mm_turn.c`: `STUN_CLASS_MASK 0x0110`, null-terminate parsed string attributes, require successful REALM+NONCE parse before auth retry.
