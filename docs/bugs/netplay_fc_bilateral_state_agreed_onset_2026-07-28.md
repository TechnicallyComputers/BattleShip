# Netplay: FC input-agree onset forked by asymmetric snap_agree watermarks

**Date:** 2026-07-28  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Seed:** `3388243596` (post light-demote / snap_agree escalate soak)

## Symptom

FC@981 healed digests after negotiate, but peers armed different mismatch ticks:

- Linux: `FRAME_COMMIT_INPUT_AGREE_ONSET validation=981 onset=974 shared=974 scan_begin=974`
- Android: `onset=962 shared=962 scan_begin=962`

Both had `inputs=MATCH`. Recovery still completed (Android initiator / Linux follower), but asymmetric onset widens seal negotiate and risks ring-clamp / `ONSET_UNRECOVERABLE` on deeper forks.

## Root cause

July 11 shared-onset (`FindEarliestHumanNonNeutralInSpan`) is bilateral **only when `scan_begin` matches**. `scan_begin` came from local `LastFrameCommitStateAgreedTick` (snap_agree watermark). Peers can advance that watermark differently while input digests still agree → different first non-neutral tick in the scan → different FC mismatch.

## Fix

1. Add `state_agreed_tick` to `SYNetFrameCommitToken` (wire +4 B; `TOKEN_U32S` 9→10). Mint fills from `syNetRollbackGetLastFrameCommitStateAgreedTick()`.
2. On input-agree FC mismatch, `scan_begin = min(local_agreed, peer->state_agreed_tick)` (treat 0 as absent). Same floor → same shared onset when digests match.
3. Log `local_agreed=` / `peer_agreed=` on `FRAME_COMMIT_INPUT_AGREE_ONSET`.

**Wire note:** both peers must be on this build (FC packet size changed). Offline unchanged (`SSB64_NETMENU` mint fill only; struct field is harmless zeros offline).

## Verification

Re-soak Dream Land Ness ditto (Android↔Linux):

- On residual FC input-agree: both peers log the **same** `onset=` / `scan_begin=` (min watermark)
- No asymmetric `mismatch_tick` negotiate churn from onset alone
- Prefer seed near continuous stick after a long snap_agree lead
