# ICE transport torn down at VSBattle load — 2026-05-27

## Symptoms

- LTE/CGNAT ICE automatch **connects** and staging rendezvous completes (exec sync, `input_bind_ack`, slot map correct).
- At `scene->VSBattle`: `bind failed err=99` (EADDRNOTAVAIL), then flood of `input_bind send_fail os_err=9` (EBADF).
- Guest abort: `strict remote MISS stall abort sim=1`; session `sent=11 recv=3 dropped=1455`.
- Both peers control Mario (P1) instead of guest controlling Fox (P2).

## Root cause

1. **`mnVSNetAutomatchAMFinalizeVsLoad()` called full `mnVSNetAutomatchAMReset()`**, which runs `mmIceShutdown()` and `syNetPeerSetIceTransport(FALSE)` while the VS netplay session is still active.
2. **`scVSBattleStartBattle()`** then calls `syNetInputStartVSSession()` (resets all sim slots to `Local`) and **`syNetPeerStartVSSession()`**, which fell back to raw UDP `syNetPeerOpenSocket()` against the ICE-selected **relay/srflx** bind address (`216.154.76.149:…`) — not locally assignable → err 99.
3. Early return on bind failure skipped **`syNetPeerApplySimSlotInputSources()`**, so guest local HID fed sim 0 (Mario) instead of sim 1 (Fox).

Bootstrap metadata and fighter spawn were correct (`p0=0/0 p1=0/1` = Mario + Fox); the failure was transport + input-slot wiring at battle entry.

## Fixes

| Area | Change |
|------|--------|
| `scautomatch.c` | Split `mnVSNetAutomatchAMReset()` into search-state vs transport teardown; **`FinalizeVsLoad` clears search FSM only** — ICE stays up until abort or full reset. |
| `netpeer.c` | `syNetPeerStartVSSession`: skip `OpenSocket` whenever **`sSYNetPeerIceTransport`**; on raw-UDP bind fail with active session, continue and re-apply slots instead of returning. |
| `netpeer.c` / `scvsbattle.c` | Export **`syNetPeerReapplySimSlotInputSources()`**; call after battle `StartVSSession` so slot map survives `syNetInputStartVSSession` reset. |

## Verification

- ICE automatch (LAN or LTE): staging → VSBattle with **no** `bind failed err=99`, control path stays `ICE`, no `input_bind send_fail os_err=9`.
- Guest `slot_map` after battle entry: `local_sim=1`, `src0=RPred src1=Loc`; Fox responds to local pad, Mario to remote inputs.
- VS session completes without immediate `strict remote MISS` / transport drop storm.
