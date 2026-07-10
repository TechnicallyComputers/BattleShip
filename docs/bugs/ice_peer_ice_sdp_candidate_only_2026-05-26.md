# ICE peer_ice_sdp candidate-only / queue ufrag validation (2026-05-26)

**Date:** 2026-05-26  
**Status:** Fix shipped  
**Area:** `mm_ice.c`, `mm_ice_automatch.c`, `mm_matchmaking.c`, matchmaking server, `debug.env`

## Symptoms

- **LTE automatch:** Host log `peer_ice_sdp len=265 prefix=a=candidate:… typ srflx` → `mmIceApplyRemoteDescription failed`. Guest filtered peer host lines then same failure; Android **SIGABRT** during `ICE_CONNECT` trickle poll.
- **Wi‑Fi:** ICE connected (`input_bind_ack`, sustained INPUT) but in-game controls wrong / rollback desync — separate from this slug.

## Root cause

1. Opponent **`ice_sdp` at queue time** was a single trickle **candidate line** (no `a=ice-ufrag:`), not `juice_get_local_description` output. `juice_set_remote_description` correctly rejects it.
2. **`BeginConnect`** applied `peer_ice_sdp` before draining match **`ice_signals`**, and never fell back to `juice_add_remote_candidate` for candidate-only payloads.
3. **`sIceRemoteDescApplied` stayed false** → `juice_set_remote_gathering_done` never posted → ICE stuck in `connecting`.
4. **`port_log` from libjuice state callback** on a worker thread risked Android instability during heavy trickle polling.

## Fix

- **`mmIceSdpHasIceUfrag` / `mmIceApplyPeerIceSignaling`:** full SDP → `set_remote_description`; candidate-only → `add_remote_candidate` + warn.
- **Bind tick / server `POST /v1/queue`:** reject or defer enqueue without `a=ice-ufrag:`.
- **`BeginConnect`:** drain trickle queue before applying peer SDP; log `has_ufrag`.
- **Juice state logging** deferred to `mmIcePoll` (main-thread flush).
- **`SSB64_NETPLAY_LOG_LOCAL_INPUT=1`** in repo `debug.env` for next input-mapping soak.

## Verification

- Wi‑Fi / LTE automatch: match logs `has_ufrag=1` on both sides; `SSB64 ICE: connected remote=…`.
- LTE with bad client: server **400** on queue without ufrag; client defers bind until local description includes ufrag.
- Input soak: grep `LOG_LOCAL_INPUT` with `SSB64_NETPLAY_LOG_LOCAL_INPUT=1`.
