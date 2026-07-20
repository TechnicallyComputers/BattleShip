# ICE offer-exchange timeout + abort srflx bind (2026-07-17)

**Date:** 2026-07-17  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Area:** `mm_ice.c`, `mm_ice_automatch.c`, `netpeer.c`, `scautomatch.c`  
**Sessions:** soak1 Linux `1172679767`, `1343839314` (Android log for these attempts was stale / prior session `11903082`)

## Symptoms

- Linux host: `ICE: completed` on LAN `host↔host`, then `automatch offer exchange timed out role=host` → `ICE bootstrap failed`.
- Abort path: `configured bind=216.154.76.149:…` → `bind failed err=99` (EADDRNOTAVAIL) → `bootstrap abort notify open failed`.
- Android: long connectivity-phase lag (`poll_phase_tics=876` on second attempt); guest slow to leave ICE_CONNECT / enter bootstrap while host already offering.

## Root cause

1. **Asymmetric CONNECTED→COMPLETED / bootstrap entry**  
   Host entered `RunBootstrap` only after `COMPLETED` and blocked in offer exchange (~6s). Guest (Android poll-mode + HTTPS `juice_pause_io` during CONNECTING/CONNECTED) could still be finishing gathering / settling while the host burned the offer window. Host needs the guest’s AUTOMATCH_OFFER; guest only sends after it also enters bootstrap.

2. **Abort notify after `mmIceShutdown()`**  
   `scautomatch` called `mmIceShutdown()` then `mnVSNetAutomatchAMIceNotifyPeerAbort`. Selected path was gone; `mmIceGetBootstrapBindHostport` fell through to **srflx/WAN**. Abort then `OpenSocket` on a non-local address → err=99.

3. **Abort raw-UDP vs live ICE**  
   Even with a LAN bind, opening a second UDP socket on the ICE-selected port races libjuice. Abort must use `mmIceSend` while the agent is live. `BootstrapFailTeardown` also clears `IsActive`, so `SendBytes` was a silent no-op unless temporarily re-armed.

## Fixes

| Area | Change |
|------|--------|
| `mm_ice_automatch.c` | Enter bootstrap on **CONNECTED or COMPLETED** once a nominated path validates (do not wait for gathering-complete alone). |
| `netpeer.c` | ICE offer-exchange window: ≥720 retries (~12s at default sleep), capped at env max. |
| `netpeer.c` | Abort notify: prefer live ICE send (re-arm `IsActive`); raw UDP only for RFC1918 bind; never open on WAN/srflx. |
| `mm_ice.c` | `mmIceGetBootstrapBindHostport`: RFC1918 selected / local host only — **no reflexive fallback**. |
| `scautomatch.c` | `NotifyPeerAbort` **before** `mmIceShutdown` on ICE connect/bootstrap failure. |

## Verification

- LAN Linux↔Android: ICE `connected`/`completed` → offer exchange succeeds without `bind failed err=99`.
- On forced bootstrap fail: log `bootstrap abort burst sent path=ICE` (not `bind=216.…` / err=99).
- Slow Android connectivity: host offer window (~12s) covers guest CONNECTED→bootstrap skew.

## Follow-up (2026-07-17 soak `35957586`)

Linux host still hung in offer exchange after `ICE: connected` (fix binary present: `ICE offer exchange window retries=720`). Log showed **~71** `ICE trickle worker GET` lines in ~2s of CONNECTING — interval was **2 ticks (~30 Hz)**. Each HTTPS GET pauses libjuice poll I/O on Android → connectivity lag on both peers; guest never enters bootstrap / sends AUTOMATCH_OFFER. Android `soak1-android.log` was still prior session `11903082` (APK/log not refreshed).

Additional fixes:

| Area | Change |
|------|--------|
| `ConnectTricklePollInterval` | Shared LAN → **0** (no trickle); non-LAN → **30** ticks. |
| `ConnectTrickleMayEnqueue` | Wall-clock min **300 ms** (env `SSB64_MATCHMAKING_ICE_CONNECT_TRICKLE_MIN_MS`). |
| `ConnectingTrickleAsyncActive` | FALSE on shared LAN. |
| `ConnectTick` | `sIceBootstrapHandoff` blocks nested bootstrap during offer-exchange yield; drop pending polls + `EnsureIoResumed` before handoff. |
| `IceBootstrapPeer` | Drop pending trickle jobs + resume I/O before `RunBootstrap`. |

## Follow-up (2026-07-17 soak `338155370`)

Soak launched the **AppImage** (`CA bundle /tmp/.mount_Battle…`), not `build-netmenu/BattleShip`. Log still had ~77 CONNECTING trickle GETs and never left UI **"MATCH FOUND"** (`MN_AM_ICE_CONNECT`) — **"CONNECTING TO OPPONENT"** is only `MN_AM_ENTER` after bootstrap. Android log still stale session `11903082`.

| Area | Change |
|------|--------|
| `mmMatchmakingEnqueuePollIceTrickle` | Central gate via `ConnectTrickleMayEnqueue` (shared-LAN / connected / 300 ms). |
| `mnVSNetAutomatchAMStatusText` | After ICE connected/completed, show **CONNECTING TO OPPONENT** while still in `ICE_CONNECT`. |
| Packaging | Rebuild `BattleShip-Netplay-x86_64.AppImage` + install to soak path; rebuild Android APK. |
