# Netplay: stale peer baseline re-arms FC deepen after healed Commit

**Date:** 2026-07-28  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Seed:** `3282055674` (Dream Land Ness ditto, Android guest / Linux host)  
**Related:** [fc shrink target fork hang](netplay_fc_shrink_target_fork_hang_2026-07-28.md), [stick absorb peer_convergence](netplay_stick_absorb_peer_convergence_post_episode_hang_2026-07-27.md), [seal epoch skew](netplay_seal_epoch_skew_mismatch_fork_2026-07-28.md)

## Symptom

```text
FC@481 inputs_agree=1 — P1 status 15 vs 18 (tap_stick fork)
[both] Commit -> Live mismatch=473 target=482  figh@482 MATCH (0x82FDF035)
[Linux] RESIM_BASELINE_RECV load_tick=473 (Android still mid-episode)
[Linux] peer baseline resync armed load_tick=473 mismatch=474 target=485
[Linux] EPISODE_FSM begin epoch=38 … target=485 → tuple_align → Commit@482
        post-ep38 figh@482 DIVERGES (0x36E9A452 vs Android 0x82FDF035)
[Linux] sim advance blocked (rollback_epoch_cap=486 source=2)
[Android] received VS_SESSION_END @485
```

No `SHRINK_TO_PEER_PREFIX`. Brief `dup_pending` during join then recovery proceeded — not the invent hang.

## Root cause

1. Bilateral FC recovery healed Live through `resolved_through=482`.
2. Peer still finishing / retransmitting a mid-episode baseline at `load_tick=473` (onset already covered).
3. Live Linux treated that baseline as a universe mismatch and **`ArmPeerBaselineResync`** queued FC deepen `474→485` past the just-healed frontier.
4. Second unilateral resim rewrote the healed state; peer-convergence epoch cap (`source=2`) froze sim until session end.

## Fix

`PORT && SSB64_NETMENU`:

1. **`ComparePeerBaselineToLocal`** — if Live (not resim / not deferred state pending) and `(load_tick+1) <= resolved_through`, ignore the baseline (`PEER_BASELINE_COMPARE ignore_stale_behind_resolved`). In-flight AwaitingBaseline still compares. (Initially shipped as strict `<`; inclusive exclusive end added for seed `214064425` — see [exclusive-end diverge](netplay_fc_exclusive_end_baseline_diverge_post_heal_2026-07-28.md).)
2. **`ArmPeerBaselineResync`** — same guard: refuse to arm when `mismatch <= resolved_through` while Live (`peer baseline resync ignore_stale`).

Legitimate FC escalate from frame-commit (re-arms local FC before notify) is unchanged.

## Verification

Re-soak Dream Land Ness ditto through FC heal:

- Prefer `ignore_stale_behind_resolved` / `resync ignore_stale` over `peer baseline resync armed` for loads behind `resolved_through` while Live
- No second FC begin that re-diverges a matching post-Commit `figh`
- No perpetual `rollback_epoch_cap … source=2` immediately after a healed FC Commit
