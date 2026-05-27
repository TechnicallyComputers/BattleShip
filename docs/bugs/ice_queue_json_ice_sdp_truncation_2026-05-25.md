# ICE queue POST dropped `ice_sdp` (JSON buffer too small)

**Date:** 2026-05-25  
**Status:** Fixed  
**Area:** `mm_matchmaking.c` (`mmRunJoin`)

## Symptoms

- Automatch matched with valid `peer_lan` but `peer_ice_sdp` empty on the opponent.
- Linux log: `SSB64 ICE: match has no peer_ice_sdp` while `peer_lan=192.168.66.x:7778` was present.
- ICE connect could not apply remote description; session stalled until timeout or quit.

## Root cause

`mmRunJoin` built `POST /v1/queue` JSON in a **448-byte** stack buffer. `mmJsonInsertStringField(..., "ice_sdp", ...)` silently returned when the escaped SDP did not fit, so the server stored `ice_sdp: None` even after local gathering succeeded.

## Fix

- `MM_JOIN_JSON_CAP` **9216** for queue POST body.
- `mmJsonInsertStringField` returns success/failure; log `ice_sdp insert FAILED` and `ice_sdp len=… in_json=…` on POST.
- Always log `peer_ice_sdp` empty/non-empty on match parse.

## Verification

Both ICE clients queue with `ice=yes` and `in_json=1` in automatch logs; match poll shows `peer_ice_sdp len=…` on both sides.
