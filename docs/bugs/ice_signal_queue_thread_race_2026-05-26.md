# ICE trickle signal queue thread race (2026-05-26)

**Date:** 2026-05-26  
**Status:** Fix shipped  
**Area:** `mm_matchmaking.c` (`sMmIceSignalQ`)

## Symptoms

- Intermittent Android **SIGABRT** during automatch **`MN_AM_ICE_CONNECT`**, often mid high-frequency trickle `GET /v1/match/{ticket}` polling (`signals=0 (+0)` in logs).
- Empty or useless backtrace on Android crash handler.
- Not tied to netplay env vars (`STRICT_SLACK`, ingress diag, etc.).
- Host stderr may show sporadic libjuice **TURN CreatePermission 403** on relay attempts; that is separate from this abort (coturn permission timing / path selection), not required for LAN host↔host.

## Root cause

Inbound `ice_signals[]` from match poll JSON are queued in `sMmIceSignalQ` on the **matchmaking worker thread** (`mmParseIceSignalsFromBody` → `mmIceSignalQueuePush`). The **game thread** drains the same ring in `mnVSNetAutomatchAMIceDrainRemoteCandidates` via `mmMatchmakingPopIceCandidate` and reads depth via `mmMatchmakingIceSignalsQueuedCount` — **without synchronization**.

Concurrent push/pop/count updates could tear `sMmIceSignalCount` / head index or partially write a 280-byte SDP line → Scudo/`abort()` on Android under trickle poll load.

Juice state logging was already deferred to `mmIcePoll` (main thread); this queue was still racy.

## Fix

- Dedicated `sIceSignalMutex` around push, pop, count, and clear on `sMmIceSignalQ`.

## Verification

- LAN automatch Android guest + Linux host: complete `ICE_CONNECT` without SIGABRT through `SSB64 ICE: connected remote=…` and bootstrap.
- Stress: several back-to-back automatch attempts on Android debug APK with `ICE_VERBOSE` and trickle polling active.
