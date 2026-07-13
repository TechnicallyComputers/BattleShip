# Divergent episode load_tick → baseline exchange stall (follower freeze)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-12  
**Seed:** `2657747101` (Android ↔ Linux, Dream Land)

## Symptom

First GGPO of the match (Linux queued at tick 424 on a predicted publish — the strict-only
ring_ready fix working as intended). Linux ran its episode normally (`load=423 mismatch=424
target=426`, resim complete, Live). Android froze at sim ~430 and never advanced; Linux ran
ahead to 483 until the user killed both. Not an input fork: `state_diverge=0`, no synctest fail,
replays diverge only because Android stopped simming.

## Root cause chain

1. **Stale load-safe metadata (trigger).** `syNetRollbackSavePostTick` marks a snapshot
   load-unsafe when its tick simmed under predicted remote input — judged once at capture and
   never revisited. After the strict-only admission fix
   (`netplay_provisional_ring_ready_blocks_predict_2026-07-12.md`), most live ticks legitimately
   sim under prediction, so nearly every recent snapshot stayed permanently load-unsafe even
   after the wire strict-confirmed those inputs with zero corrections.
2. **Episode load rewind.** Joining Linux's episode (locked load 423), Android's
   `syNetRollbackResolveLoadTickForSnapshot` found no load-safe slot in 409–423:
   `EPISODE_LOAD_REWIND locked_load=423 resolved=408`.
3. **Baseline keyed on exact load_tick.** Linux echoes/awaits only `load_tick=423`; Android
   awaits `408`. `RESIM_BASELINE_TIMEOUT ... baseline_matched=0` after 14 retransmits. The
   regular echo path on Linux refused Android's 408 baselines because
   `load_tick <= resolved_through` (426 post-Live).
4. **Timeout re-armed forever.** The timeout fallback `ArmPeerBaselineResync(408)` armed another
   baseline wait nobody would ever answer → permanent follower freeze.

## Fixes

1. **Retroactive load-safe promotion** (`syNetRollbackPromoteConfirmedLoadSafeSnapshots`,
   called after `SavePostTick` each committed tick). Scans the live snapshot ring each frame:
   when every remote-human slot is strict-confirmed at tick T and published gameplay matches
   (`syNetInputRemoteHumanPublishedMatchesConfirmedForSimTick`, netinput.c), pin the snapshot
   load-safe (`syNetRbSnapshotPinLoadSafeAtTick`). Budgeted 16 pins / 128 scans per frame.
   **Watermark stall fix (soak `3218864814`):** the first cut walked contiguous from tick 1 and
   `break`'d on the first unconfirmed/pending tick — early Wait/bootstrap holes stalled the
   walk forever (`0× LOADSAFE_PROMOTE`), so Linux still hit `EPISODE_LOAD_REWIND 734→720` and
   false-agreed baseline on the stale 734 ring digest. Now unconfirmed ticks are **skipped**
   (not blocking), the ring floor is rescanned every frame so later-arriving confirms promote
   holes, and load-safe slots are treated as independent (FindLatestLoadSafe does not need
   contiguous safety from t=1).
   **NULL probe fix (soak `2187301316`):** `GetStoredSubsystemHashes(t, NULL,…)` always returned
   FALSE because out-params were required non-NULL — promote treated every tick as missing and
   ResolveLoadTick always fell through to FindLatestLoadSafe (same 734→720). Out-params are now
   optional. Log: `LOADSAFE_PROMOTE` / rare `LOADSAFE_PROMOTE_PENDING`.
2. **Hash-only baseline echo** (`syNetRollbackTryHashOnlyBaselineEcho`). When the regular echo
   is refused (typically `load_tick <= resolved_through` after going Live), reply with stored
   snapshot-ring digests for that tick via `syNetPeerSendRollbackBaselineDigestDirect` — no
   snapshot load, no state change, rate-limited by the existing echo min-advance. A
   deeper-loaded follower now gets a comparable baseline even after the initiator is Live.
   Log: `RESIM_BASELINE_ECHO hash_only`.
3. **Divergent-load timeout escape**
   (`syNetRollbackTryProceedAfterDivergentLoadBaselineTimeout`). Baseline gate timeout now
   proceeds with the replay (like the env-proceed path) instead of arming an unanswerable
   resync when: episode FSM active, digest never matched, peer baseline traffic was observed
   only for a **different** load_tick (`sSYNetRollbackPeerBaselineForeignLoadTick`, recorded on
   the foreign-baseline ignore branch), and all peer seal rows are complete (inputs canonical).
   Post-resim verification (RESIM_POST digest / synctest / FC) still catches real divergence.
   Log: `RESIM_BASELINE_DIVERGENT_LOAD_PROCEED`.

## Files

- `port/net/sys/netrollback.c` — promotion sweep + watermark, foreign load_tick tracking,
  hash-only echo, timeout escape.
- `port/net/sys/netinput.c/.h` — `syNetInputRemoteHumanPublishedMatchesConfirmedForSimTick`.
- `port/net/sys/netpeer.c/.h` — `syNetPeerSendRollbackBaselineDigestDirect` (refactored out of
  `TrySendRollbackBaselineDigest`).

## Verify (re-soak Android ↔ Linux stick mash)

- Expect `LOADSAFE_PROMOTE through=… count=…` early and after prediction windows (not 0×).
- On GGPO episodes: both peers should share the same `load_tick` (no `EPISODE_LOAD_REWIND`
  when the ring still holds the initiator's load).
- If a rewind still occurs (ring wrap / fragile anchor): expect `RESIM_BASELINE_ECHO hash_only`
  from the Live peer and gate open on the follower; worst case a single
  `RESIM_BASELINE_TIMEOUT` followed by `RESIM_BASELINE_DIVERGENT_LOAD_PROCEED` — never a
  frozen follower with the peer running ahead.
- Replay checksums should match through mid-match checkpoints (seed `3218864814` forked by
  @600 after a 734-vs-720 false baseline agree).

Related: `netplay_provisional_ring_ready_blocks_predict_2026-07-12.md` (trigger),
`netplay_confirmed_publish_write_once_2026-07-12.md` (authority ledger),
`netplay_baseline_universe_mismatch_ignored_2026-07-12.md` (echo-retry freeze).
