# ICE peer SDP JSON unescape + BeginConnect gather (2026-05-26)

**Date:** 2026-05-26  
**Status:** Fixed  
**Area:** `mm_matchmaking.c`, `mm_ice_automatch.c`, libjuice glue

## Symptoms

- libjuice stderr: `Missing ICE user fragment in remote description`, `Candidates gathering already started`.
- Logs showed `peer_ice_sdp` with literal `\n` between SDP lines (`a=ice-ufrag:…\na=ice-pwd:…`) instead of real newlines.
- ICE never reached `SSB64 ICE: connected` despite `/v1/turn-credentials` HTTP 200 and same-LAN peers.

## Root cause

1. **Primary:** `mmJsonCopyQuotedValue` and `mmParseIceSignalsFromBody` copied JSON string bytes without decoding escapes. Serde returns `peer_ice_sdp` with `\n` in JSON; libjuice `ice_parse_sdp` only splits on real `\n` and does not flush a final line without a terminator → empty `ice_ufrag`.
2. **Secondary:** `mnVSNetAutomatchAMIceBeginConnect` cleared `sIceGatheringDone` and called `mmIceStartGathering()` again on an agent that already gathered → libjuice warn + `juice_set_remote_gathering_done` never ran.

## Fix

- `mmJsonDecodeQuotedString` / `mmJsonSkipPastQuotedString`; used for `peer_ice_sdp` and `ice_signals[]`.
- `mmRunIceSignal` JSON-escapes trickle `candidate` on POST.
- `BeginConnect`: keep prior gather-done state; do not re-call `mmIceStartGathering`; log apply/add-candidate failures and first SDP line when decoded.

## Verification

Automatch PC + Android: logs should show `first_line=a=ice-ufrag:…` (real newline), then `SSB64 ICE: connected remote=…`. TURN auth failures are separate (coturn HMAC); STUN/srflx + host candidates can still work on LAN.
