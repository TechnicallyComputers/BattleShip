# Netplay — FC onset Resolve miss via EpisodeResolvedThrough floor (RING_CLAMP abandon)

**Date:** 2026-07-28  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** Android guest ↔ Linux host, session `2028838966`, seed `3538623210`, Dream Land
Ness ditto, `fc_validation_ticks=120`, `rb_snap=128`  
**Follow-on to:** [FC onset older than ring hard fork](netplay_fc_onset_older_than_ring_hard_fork_2026-07-27.md),
[FC late mint resim grid skip](netplay_fc_late_mint_resim_grid_skip_2026-07-28.md)

## Symptom

FC cadence healthy (`sent=3 recv=3 compared=3`). First diverge at validation **721**, inputs MATCH,
figh-only:

```
FRAME_COMMIT_INPUT_AGREE_ONSET … onset=613 shared=613
FRAME_COMMIT_INPUT_AGREE_ONSET_RING_CLAMP … clamped_load=714 mismatch=715
  min_load=596 frontier=714 reason=1
FRAME_COMMIT_INPUT_AGREE_ONSET_UNRECOVERABLE … onset_load=612 min_load=596 clamped_load=714
  — ring clamp abandoned onset; fail closed
FRAME_COMMIT_DIAG … recovery_started=0
```

Onset **in-ring** (`612 ≥ min_load≈596`) — not the “older than ring” case — yet recovery never armed.

## Root cause

`syNetRollbackResolveStateMismatchLoadTick` always raised `min_load` through
`syNetRollbackLoadTickMinBound`, which includes:

```c
episode_floor = EpisodeResolvedThrough - 1;
```

Light input episodes had marched `resolved_through` to **714** without healing the SoftLip /
`replay_determinism` fighter fork. Resolve then called
`FindLatestLoadSafeTickAtOrBefore(probe=612, min_tick=713)` → immediate `~0` because
`612 < 713`. RING_CLAMP fell forward to the intersected frontier (**714**), which abandons
onset (`714 > 612`) → July-27 intentional fail-close.

Late-mint / missed FC grids were **not** causal this soak.

## Fix

`PORT && SSB64_NETMENU`:

1. `ResolveStateMismatchLoadTick(..., apply_episode_floor)` — FC input-agree onset Resolve and
   FC `TryBegin` load pick pass `FALSE` (caller ring floor only). Ordinary paths keep `TRUE`.
2. Log `FRAME_COMMIT_ONSET_RESOLVE probe=… ring_min=… episode_floor=… resolved=…` on the
   ring-only path for soak confirmation.
3. July-27 fail-close unchanged when Resolve still misses (no load-safe ≤ onset) and clamp
   abandons onset, or when onset is older than the ring.

Does **not** fix the underlying SoftLip physics fork — only restores the intended in-ring FC
recovery arm so `recovery_started≥1` can attempt heal.

## Acceptance

- [ ] Re-soak input-agree figh diverge with light-storm `resolved_through` ahead of onset:
      expect `FRAME_COMMIT_ONSET_RESOLVE … resolved≤onset_load`, no immediate
      `ring clamp abandoned onset` solely from episode floor; `recovery_started≥1`.
- [ ] True older-than-ring onset / true empty load-safe span ≤ onset: still
      `ONSET_UNRECOVERABLE` fail-closed.
- [ ] Ordinary GGPO TryBegin still respects episode floor (no deepen behind
      `resolved_through` without FC deepen flags).

## Related

- [`netplay_fc_onset_older_than_ring_hard_fork_2026-07-27.md`](netplay_fc_onset_older_than_ring_hard_fork_2026-07-27.md)
- [`netplay_fc_input_agree_onset_ring_clamp_2026-07-19.md`](netplay_fc_input_agree_onset_ring_clamp_2026-07-19.md)
- [`netplay_shared_correction_frontier_2026-07-19.md`](netplay_shared_correction_frontier_2026-07-19.md)
