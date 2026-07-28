# Netplay: light-episode resolved floor over hold_last guesses → permanent input loss + FC starvation

**Date:** 2026-07-27
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, awaiting re-soak)
**Follow-on to:** [netplay_light_input_episodes_2026-07-27.md](netplay_light_input_episodes_2026-07-27.md)

## Symptom

Soak (Android guest vs Linux host, light-episode + wire-skip build): permanent state fork
starting at tick ~397, first detected only by FC grid at validation **841** (~450 ticks
later), FC recovery baseline compare failed (`inputs agree through load`), session aborted.

- `SNAP_AGREE_MISMATCH` continuous from snap 396 (figh only; world/item/rng equal at first).
- Linux p1 fighter timeline: `status=11` (Wait) while Android p1 walked → dash/walk fork.
- `FRAME_COMMIT_DIAG sent=2 recv=5 compared=2` over ~960 ticks — grid points 480/600/720
  never minted on Linux.

## Root cause (three interacting holes)

Light episodes (`owner=local_light`) removed the seal-row exchange. In the heavy protocol,
seal rows guaranteed every tick in `[mismatch, target)` was replayed with **authoritative
peer inputs** before the episode was marked resolved. Light episodes replay span ticks past
the wire frontier with **hold_last guesses** — but three mechanisms still assumed the
heavy contract "completed span == authoritative span":

1. **`resolved_through` floor** (`CloseCorrectionEpisode` / `NoteEpisodeResimCompleted`)
   advanced to the full resim target. Light ep `mismatch=396 target=400` replayed 397–399
   as hold_last `(13,0)`; when the true wire landed (`397=(40,0)`, `398=(53,2)`,
   `399=(57,4)`), the corrections hit `CORRECTION_CLAMP_RESOLVED mismatch=398->400` and
   became **promote-only** history patches (`LEDGER_REFRESH_COMPLETED_SIM_CORRECT`) with no
   state resim. Each subsequent light episode advanced the floor further, baking the error
   in. Linux's dash tap counter started 3 ticks late (`tap_x=1` @400 vs @397 on Android) →
   walk-vs-dash status fork.

2. **`ReleaseLiveCapsAfterResimPostMatch` overlap-clear** dropped any deferred correction
   whose span overlapped the just-completed resim. Wire for tick 397 arrived *mid-resim*
   (after 397 had already been replayed with the guess), correctly armed a deferred at 397,
   then post-match cleared it. History was promote-patched (`post_pre_promote`), which also
   made the timeline reconcile see `published == remote` and clear the earliest-incorrect
   mark — tick 397 became invisible to every detector while its state stayed stale.

3. **FC grid starvation**: `FrameCommitAfterCompletedSimStep` mints only when
   `completed % fc_interval == 0` and silently skips the whole interval when a deferred
   GGPO covers the boundary tick. With light episodes churning every ~2–5 ticks during
   stick motion, grids 480/600/720 were all eaten → `sent=2` and the fork ran undetected
   for ~450 ticks, past snapshot-ring depth, making FC recovery unable to reach the fork.

## Fix

**Predicted-replay watermark** (`netinput.c`): `syNetInputResolveFrame`'s resim hold_last
branch records the lowest tick replayed with a predicted source
(`sSYNetInputResimPredictedReplayLowestTick`, reset in
`syNetInputRollbackPrepareForResim`, accessor
`syNetInputResimLowestPredictedReplayTick`). Heavy sealed episodes never take the
hold_last branch → watermark 0 → all caps below are no-ops (heavy semantics unchanged).

1. **Floor cap** (`netrollback.c`): both `resolved_through` raises route through
   `syNetRollbackCapResolvedThroughForPredictedReplay` — the floor never advances past the
   watermark (`RESOLVED_THROUGH_PRED_CAP` log). Late wire for a guessed span tick now
   queues a normal rewind instead of `CORRECTION_CLAMP_RESOLVED`.
2. **Deferred keep** (`netrollback.c`): `ReleaseLiveCapsAfterResimPostMatch` keeps a
   deferred whose mismatch is at/after the watermark (`DEFERRED_KEEP_PRED_SPAN` log) — the
   next episode begins at the true fork tick and loads the pre-fork snapshot.
3. **Timeline keep** (`netrollback.c`): `ClearTimelineForCompletedResim` clamps its
   reconcile span to the watermark so earliest-incorrect marks for guessed ticks survive
   the promote race.
4. **FC late mint** (`netpeer.c`): a blocked grid mint arms
   `sSYNetPeerFrameCommitLateMintPendingTick` (`FRAME_COMMIT_LATE_MINT_ARM`) and mints the
   *same grid-aligned validation id* on a later completed step once the deferred clears
   (`FRAME_COMMIT_LATE_MINT`). Retro-safe: `syNetFrameCommitBuildToken` reads state digests
   from the stored snapshot ring at `validation-1` and the input digest from settled
   history. Dropped past 96 ticks (ring depth guard). Follow-on: `tick_anchor` must be
   snap_tick (not live GetTick) or late mint PAIRING_FAILs — see
   [netplay_light_absorb_coalesce_onset_delay_2026-07-27.md](netplay_light_absorb_coalesce_onset_delay_2026-07-27.md).

## Soak expectations

- `RESOLVED_THROUGH_PRED_CAP` on most light episodes during stick motion.
- `CORRECTION_CLAMP_RESOLVED` for wire-sourced corrections of never-confirmed ticks: gone.
- `DEFERRED_KEEP_PRED_SPAN` occasionally (mid-resim wire races).
- `FRAME_COMMIT_DIAG sent≈recv≈compared` ≈ battle_ticks / 120.
- Slightly deeper light resims (mismatch a few ticks behind frontier) — expected cost.

## Follow-on (same-day soak)

PRED_CAP kept the deferred, but light absorb coalesce delayed Begin ~3 ticks → SoftLip on
Wait vs owner Dash → `replay_determinism` at FC@961. Fixed in
[netplay_light_absorb_coalesce_onset_delay_2026-07-27.md](netplay_light_absorb_coalesce_onset_delay_2026-07-27.md).

**JumpAerial SoftLip @740:** `MERGE_DEEPEN` pulled deferred below the hold_last watermark so
`DEFERRED_KEEP` cleared; late wire promote-only. Fixed in
[netplay_light_pred_span_merge_deepen_hold_last_2026-07-27.md](netplay_light_pred_span_merge_deepen_hold_last_2026-07-27.md).

**Stick-onset micro-cascade @400:** KEEP retained deferred with target past contiguous wire →
hold_last invent SoftLip fork / FC hang. Fixed in
[netplay_light_pred_cap_wire_ready_coalesce_2026-07-27.md](netplay_light_pred_cap_wire_ready_coalesce_2026-07-27.md).

## Non-goals

State-authority resync when FC recovery's baseline compare fails with
`inputs agree through load` remains an open design decision (session still aborts).
