# ICE automatch trickle poll gap (CGNAT / mobile)

**Date:** 2026-05-25  
**Status:** Fixed  
**Area:** Automatch FSM (`scautomatch.c`), matchmaking poll (`mm_matchmaking.c`), libjuice glue (`mm_ice_automatch.c`)

## Symptoms

- Android on 5G + desktop on LAN matched on the same session but ICE never logged `connected` / `completed`.
- Logs showed `ICE: state=gathering` / `connecting` only, then user cancel after ~60s.
- `wan=0.0.0.0:7778` on queue POST; `ignoring MM_POLL_MATCHED while state=7` (misleading — state 7 is `MN_AM_ICE_CONNECT`, not an error).

## Root cause

1. **Primary:** `GET /v1/match/{ticket}` (which returns `ice_signals[]` trickle) only ran in `MN_AM_POLL`. After the first `MM_POLL_MATCHED`, the FSM moved to `MN_AM_ICE_CONNECT` and **stopped polling**, so inbound remote candidates never arrived.
2. **Secondary:** Local trickle `POST /v1/match/{ticket}/ice` was dropped when `sIceTicket` was still empty (candidates during gather-before-queue).
3. **Cosmetic:** `mmIceGetReflexiveHostport` used selected addresses (post-connect), so queue `udp_endpoint` showed bind `0.0.0.0:7778`.
4. **Timeout:** `sMnAMConnectDeadlineMs` was only set on the legacy `mnVSNetAutomatchAMEnterVs` path, not ICE connect/staging.

## Fix

- Continue match poll in `MN_AM_ICE_CONNECT` via `mmMatchmakingEnqueuePollIceTrickle` (trickle-only: parse `ice_signals`, no duplicate `MM_POLL_MATCHED`).
- Pre-ticket candidate ring buffer flushed on `MM_POLL_QUEUED`.
- `mmIceGetSrflxHostport` from gathered `typ srflx` candidates.
- `MN_AM_BIND` waits for gathering-done before `POST /v1/queue`.
- Connect deadline set on ICE match; kept through staging rendezvous until handshake completes.
- TURN credential fetch success/failure logged at ICE init.

## Barrier / staging

No change to battle barrier protocol. Staging rendezvous (`STAGE_SCENE_READY/GO`) still runs after ICE bootstrap; connect deadline now covers the staging wait.

## Verification

Desktop LAN + Android 5G automatch: expect `SSB64 ICE: connected remote=...`, bootstrap, `staging_go`, VS load. See `docs/netplay_ice_migration.md`.
