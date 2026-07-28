# Netplay: light absorb coalesce delays onset rewind → SoftLip / replay_determinism

**Date:** 2026-07-27
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, awaiting re-soak)
**Follow-on to:** [netplay_light_episode_resolved_floor_pred_cap_2026-07-27.md](netplay_light_episode_resolved_floor_pred_cap_2026-07-27.md)

## Symptom

Soak after pred-cap / FC late-mint fix: agree through tick 647, permanent state fork from
dash onset ~648, FC@961 `FRAME_COMMIT_STATE_DIVERGE` with **inputs agreeing**, same
status/motion, different `topn_tx`. Recovery: `BASELINE_UNIVERSE class=replay_determinism`,
`inputs agree through load`, deepen exhausted. Session end ~971.

Prior fix signals were healthy (`RESOLVED_THROUGH_PRED_CAP`, `DEFERRED_KEEP_PRED_SPAN`,
`FRAME_COMMIT_LATE_MINT`, `compared=4`).

## Root cause

1. **Absorb coalesce wait on light episodes.** After a light ep PRED_CAP'd and kept a
   deferred at the hold_last watermark (tick 648), `StickAbsorbCoalesceWaiting` blocked
   `TryBegin` for ~3 ticks (`try_begin_fail stage=stick_absorb_coalesce`). Live sim advanced
   under Wait + wrong stick while Android already entered status 18 (dash/kneebend) at 648
   with `sx=25 tap_x=1`. SoftLip ran on Linux with **p1 status=10** vs Android **status=18**
   at gut=650. ep18 eventually resimmed wire correctly — figh hashes matched at 655 — but
   cam/position had already forked; continuous figh diverge from 663; FC saw pure position
   drift with matching inputs.

2. **FC late-mint pairing.** `syNetFrameCommitBuildToken` set `tick_anchor = GetTick()`.
   Linux late-minted validation 841 at completed=845 (anchor 845) while Android minted
   on-time (anchor 840) → `anchor_diff > 1` → `PAIRING_FAIL` (`pairing_fail=1` in diag).
   Digests were comparable; the fork ran undetected until grid 961.

## Fix

1. **`syNetRollbackStickAbsorbCoalesceWaiting`:** when light input episodes are enabled,
   return FALSE (never delay Begin). Absorb window still arms for peer-cap / epoch ignore;
   same-burst REPLACE still merges via deferred Queue while pending or mid-resim.

2. **`syNetFrameCommitBuildToken`:** set `tick_anchor = validation_tick - 1` (snap boundary
   the digests name), so late mint and on-time mint pair.

## Soak expectations

- No `try_begin_fail stage=stick_absorb_coalesce` under light episodes
- SoftLip at onset ticks should show matching status on both peers (no Wait-vs-18 window)
- `FRAME_COMMIT_DIAG pairing_fail=0` when late mint fires; diverge (if any) caught at the
  armed grid id, not ~120 ticks later
- Slightly more frequent short light Begins during stick storms — expected

## Non-goals

True SoftLip / map-coll non-determinism under identical inputs remains a separate
investigation if re-soak still shows `replay_determinism` with no absorb delay.
