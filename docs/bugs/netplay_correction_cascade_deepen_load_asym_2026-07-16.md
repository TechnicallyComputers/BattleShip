# Netplay: correction cascade + deepen load asymmetry → PEER_SNAPSHOT_DIVERGE (2026-07-16)

## Symptom

Soak session `811284586` (Android client / Linux host, seed `3719103085`) died at
`load_tick=634` with **figh + anim + map + camera** forked (world/item/rng/wpn matched).
`netplay-scan-drift.py` RESULT PASS (no LOAD_HASH_DRIFT); sync-report UNSTABLE on
`PEER_SNAPSHOT_DIVERGE x1` both peers. Session had **17 resims** by tick ~636 —
almost all 2-tick `local_initiator` episodes after a single Z-button GGPO at tick 612.

## Root cause (two coupled failures)

### 1. 2-tick correction cascade

Linux queued one real wire GGPO at tick **612** (`btn=0x4000` predicted vs remote `0`).
After each 2-tick episode completed, `CORRECTION_CLAMP_RESOLVED` bumped the next deferred
mismatch to `resolved_through` and immediately `TryBeginDeferredMismatch` opened another
span-2 episode (`615→617→…→633`). Stick-absorb was armed on complete but did **not** hold
Begin, so absorb never coalesced — it just re-armed micro-episodes.

### 2. Asymmetric deepen / load rewind

At tick 633 Android got a stick GGPO, `CORRECTION_MERGE_DEEPEN` walked mismatch back to
**622**, and started a 13-tick resim from `load=621`. Linux was mid shallow episode
(`load=632`, `mismatch=633`). Peer deepen was rejected as `resolved_through` (622 < 633),
`LOAD_TICK_NEGOTIATE` refused peer load 621 because baseline already matched at 632, and
`tuple_align` resealed with **`load=632`, `mismatch=622`** (invalid: load ≥ mismatch).
Android finished epoch 17 from 621; Linux “finished” from 632 → baselines at 634 disagree.

## Fix (`PORT && SSB64_NETMENU`)

| Change | Where | Behavior |
|--------|--------|----------|
| **Stick-absorb coalesce** | `StickAbsorbHoldingDeferredCoalesce` + `TryBeginDeferredMismatch` | Hold Begin while deferred tip is at/after `resolved_through`; merge REPLACE into one deferred span. Exit via **pump countdown** (not `sim >= absorb_until`). **Keep** live-cap at `mismatch-1` while holding. |
| **Accept deepen behind resolved** | `AcceptPeerSymmetricRollbackNotify` | Mid-active-episode peer deepen (`mismatch < active_mismatch`) is accepted so `TryAlign` can run. |
| **Skip soft-clamp on deepen** | `OnPeerSymmetricRollbackNotifyEx` | Do not bump peer mismatch up to `resolved_through` when it is deeper than our active episode mismatch. |
| **tuple_align load rewind** | `TryAlignActiveEpisodeTuple` | If peer mismatch requires `load < cur_load`, call `TryRestartResimAtDeeperLoad`; never reseal with `load >= mismatch`. |
| **LOAD_TICK_NEGOTIATE carve-out** | `TryNegotiateResimLoadTickWithPeer` | Allow deepen despite matched baseline when peer load is required for an earlier input-correction mismatch. |

### Follow-up regression (soak `1232571553`, 2026-07-17)

First coalesce build **lifted** deferred live-cap while holding Begin so sim could reach `absorb_until`. That let live advance past the deferred mismatch with wrong prediction (Android held @950 while sim ran to 953; Linux at 957 when SYNC arrived). Epoch 7 then posted divergent figh from matched baseline @949 → `PEER_SNAPSHOT_DIVERGE` @951 (figh-only). Fix: restore live-cap; key coalesce exit on `StickAbsorbCoalescePumpsLeft` decremented each held TryBegin.

## Verification

- `build-netmenu` clean.
- Re-soak stick-heavy / Z-edge sessions: expect far fewer than ~10 sequential span-2
  episodes after one GGPO; if a peer deepens mid-episode, both peers share the same
  `(load, mismatch, target)` and no figh/map `PEER_SNAPSHOT_DIVERGE` from load asymmetry.
- Absorb coalesce: `try_begin_fail stick_absorb_coalesce` for a few pumps then one Begin; sim must **not** advance past deferred mismatch during the hold.

## Related

- `netplay_stick_replace_policy_consolidate_2026-07-12.md` — absorb intent (Begin was missing).
- `netplay_ggpo_behind_resolved_through_seal_stall_2026-07-12.md` — soft-clamp / clamp-resolved.
- `netplay_stick_storm_cooldown_livecap_deadlock_2026-07-12.md` — live-cap vs deferred Begin.
- `netplay_divergent_load_tick_baseline_stall_2026-07-12.md` — divergent load_tick baselines.
