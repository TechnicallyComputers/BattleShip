# ICE shared-LAN single gather (re-gather removed) — 2026-05-27

**Status:** FIX SHIPPED  
**Area:** `mm_ice_automatch.c`, `mm_ice.c`, `docs/netplay_ice_migration.md`

## Problem

Post-match **LAN-direct re-gather** (`mmIceRegatherLanDirect` in `BeginConnect`) caused:

- **`LAN-direct re-gather timed out`** on Linux + Android (sync 360× `mmIcePoll()` spin, not frame-async like BIND).
- **`returning to character select (ICE LAN-direct re-gather failed)`** on both peers before VS.
- Android **SIGABRT** on rematch (automatch reset vs stale match poll race).

Re-gather was optional polish to reduce coturn **CreatePermission 403** noise on LAN, not required for host↔host connectivity.

## Fix

**Single gather + signaling/path policy** for the whole automatch attempt:

1. Removed `mnVSNetAutomatchAMIceMaybeRegatherLanDirect` and `mmIceRegatherLanDirect`.
2. `BeginConnect` only refreshes LAN, applies `mmIceSetCandidatePolicy`, then proceeds to peer SDP / trickle (unchanged relay strip, path validation, relay settle).
3. Kept `mnVSNetAutomatchAMIceShouldQueueTurnEndpoint` (skip coturn `turn_endpoint` when `ICE_LAN_DIRECT=1` or known LAN without needing relay).

## Trade-off

LAN automatch may still log benign TURN activity during the initial full gather. Host↔host should win via existing policy. Dev LAN: `SSB64_MATCHMAKING_ICE_LAN_DIRECT=1` + `BIND`/`LAN_ENDPOINT`.

## Verification

- Shared-LAN Linux + Android: match completes ICE without re-gather logs; no CSS kick from re-gather failure.
- LTE + LAN: full gather at queue unchanged; cross-NAT still gets TURN in SDP.
