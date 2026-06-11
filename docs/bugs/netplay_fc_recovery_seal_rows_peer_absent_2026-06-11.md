# FC recovery seal-row wait deadlock when peer never joins episode (netplay)

**Date:** 2026-06-11
**Scope:** `port/net/sys/netrollback.c`, `port/net/sys/netrollback_episode.c`
**Status:** FIX SHIPPED — soak pending
**Logs:** `soak1-linux.log` (failed run). Note: `soak1-android.log` from the same capture session is a
**different match** (cseed `0xBB23823E` vs `0xD86C56BD`, fighter slots swapped, ~79 s wall-clock offset
at the same sim tick) — it is the *successful* control run, not the peer of the failure. Do not
cross-read the two as one session.

## Symptom

Kirby vs (fkind 8) Yoshi's Story soak, intro resim @230 OK. At FC validation **480**,
`FRAME_COMMIT_STATE_DIVERGE` (figh+world, inputs agree `0xDDB91A1B`) → deferred fc_recovery resim
`mismatch=361 target=481`, deepened to `load_tick=359`, `span=121`, `owner=local_initiator`.

Baseline negotiation succeeds (`RESIM_BASELINE_RECV load_tick=359` matches → `baseline_matched=1`),
but the peer **never joins the episode**:

```
ROLLBACK_SYNC_SEND slot=1 mismatch_tick=360 ... (hundreds, no RECV ever)
EPISODE_SEAL_ROWS_SEND epoch=2 slot=0 begin=0..120          (outbound complete, repeatedly)
EPISODE_SEAL_ROWS_WAIT load_tick=359 missing_slots=0x2      (slot 1 rows: zero received)
EPISODE_SEAL_ROWS_WAIT_DETAIL slot=1 span=121 first_invalid_idx=0 lo_mask_bits=0 hi_mask_bits=0
INPUT send cap sim=359 (strict stall egress limit)
BASELINE_UNIVERSE_STORM_CAP ... repeats=200+
```

Battle sim freezes at tick **359** (render frames continue) for 3 × 34-frame timeout streaks, then:

```
RESIM_BASELINE_TIMEOUT ... streak=3 baseline_matched=1 seal_rows_missing=0x2
RESIM_SEAL_ROWS_TIMEOUT load_tick=359 missing_slots=0x2
RESIM_BASELINE_TIMEOUT streak — hard desync recovery
VS session stop (sim 489)
```

User-visible: the game "locks up quickly" after a (sometimes successful) forward sim, then the
session dies.

## Root cause

1. **Liveness hole:** the episode replay gate (`syNetRollbackTryOpenResimReplayGate`) hard-requires
   inbound seal rows for every remote-authority slot. If the peer never enters the episode (sync
   notify rejected/lost, peer wedged), the initiator has no fallback even though it already holds
   **wire-confirmed** inputs for the entire span — FC at 480 had *proved* input agreement
   (`FRAME_COMMIT_INPUT_AGREE_REANCHOR last_agreed=360`). `syNetRollbackEpisodeSealInputs`
   deliberately skips remote-authority slots when exchange is enabled (netrollback_episode.c
   seal loop), so those rows stay invalid forever.

2. **Diagnosability hole:** the peer-side recruit gate
   `syNetRollbackAcceptPeerSymmetricRollbackNotify` rejected silently on 6 of 7 paths
   (rollback_inactive / bad_args / already_applied / resolved_through / dup_pending /
   dup_deferred). A peer that drops every `ROLLBACK_SYNC` is indistinguishable from packet loss in
   the logs, which is why the peer-side cause of this failure could not be determined post-hoc.

## Fix

1. **`syNetRollbackEpisodeTrySelfSealMissingPeerRows()`** (`netrollback_episode.c`) — all-or-nothing
   fallback: for every incomplete remote-authority slot, fill missing rows from
   `syNetInputGetRemoteHistoryFrame()` (**wire-confirmed only** — never hold-last, never
   predicted; validates the full span before mutating). Confirmed wire frames are byte-identical
   to what the remote authority would have sealed for itself, so the replay cannot fork from a
   late-joining peer. Marks the peer seal tick masks so `AllPeerSealRowsComplete` passes.

2. **Timeout hook** (`syNetRollbackOnBaselineGateTimeout`, `netrollback.c`) — after the existing
   seal-row retry budget (`SEAL_ROWS_TIMEOUT_MAX_RETRIES=2`) is exhausted and only when
   `baseline_matched=1`, attempt self-seal; on success log
   `RESIM_SEAL_ROWS_SELF_SEAL_FALLBACK`, reset the timeout streak, and reopen the replay gate
   instead of falling into hard desync recovery. The peer still gets ~2 timeout windows
   (~70 frames) to join normally first; partially-confirmed spans still take the hard path.

3. **`PEER_SYMMETRIC_NOTIFY_REJECT reason=… applied=… resolved_through=… pending=… deferred=…`**
   (`netrollback.c`) — rate-limited (1 line / 30 sim ticks) reject log on every previously-silent
   gate, so the next failed soak shows *why* a peer ignored the episode recruit.

## Still open

Why the failed run's peer never joined (its log was not captured). Candidates visible from code:
peer rollback inactive at recruit time, `resolved_through` raced past the mismatch, or the
pending-episode deferred-begin stalling while `BASELINE_PREEMPTIVE_LIVE_CAP` holds its sim. The new
reject log resolves this ambiguity on the next reproduction.

## Validation

Re-run the Yoshi's Story cross-OS soak (`ROLLBACK_INJECT_TICK=230`, FC diag 2). On a recurrence of
the missing-peer pattern expect: two `RESIM_SEAL_ROWS_TIMEOUT retry` lines, then
`EPISODE_SEAL_ROWS_SELF_SEAL slot=1 filled=121 span=121` + `RESIM_SEAL_ROWS_SELF_SEAL_FALLBACK` +
`resim replay gate open`, sim resumes, **no** `RESIM_BASELINE_TIMEOUT streak — hard desync
recovery`, no VS stop. On the peer, grep `PEER_SYMMETRIC_NOTIFY_REJECT` to identify the recruit
rejection reason.
