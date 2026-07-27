# Post-resim gap-hold mid-span walk-back → LOCAL_PUBLISH_LATCH_REFUSE (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** session `1719068841` seed `2931794294` — Android client (lp=1) ↔ Linux host (lp=0)  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `BASELINE_UNIVERSE_MISMATCH` / `PEER_SNAPSHOT_DIVERGE` (`class=replay_determinism`, Turn status=18)  
**After:** [seal epoch skew](netplay_seal_epoch_skew_identical_span_2026-07-26.md) cleared; [post-resim exclusive-target gap](netplay_post_resim_local_publish_gap_branch_deferred_2026-07-26.md) accepted

## Symptom

Epoch-skew kill gone. Session dies ~563 on Turn universe deepen after heavy hold_last invent / ledger flips. Soft `PEER … stick_absorb` @539 with swapped figh hashes across peers.

| Check | Result |
|-------|--------|
| `RESIM_SEAL_ROWS_EXHAUSTED` / `EPOCH_SKEW_APPLY` | 0 |
| Android `LOCAL_PUBLISH` P1 | 535, 536, then **gap** → 539 (no 537/538) |
| Android post-resim @538 | `LOCAL_PUBLISH_LATCH_REFUSE` 537/538 `frontier=536` |
| Android seal @535–538 | `SEAL_PACK_GAP_HOLD` 537 from 536 `(50,17)` — seal only, no feel-0 |

## Root cause

Follower resim `535→538` left local feel-0 through 536. Seal packed 537 via `SEAL_PACK_GAP_HOLD`, but `ResyncControllersAfterResim` only pinned `[target, restore_end)` and only copied from **t−1**.

Exclusive target 538 needs gameplay at 537 to pin; 537 was never staged → no `POST_RESIM_GAP_HOLD_LAST` → `PromoteAfterResimExit` resolved latch → `LOCAL_PUBLISH_LATCH_REFUSE`. Owner wire never carried 537/538; peer invent + subsequent P0 flip GGPO left Turn state in disagreeing universes (`agree_through_load=1`).

## Fix (`port/net/sys/netinput.c` — `syNetInputRollbackResyncControllersAfterResim`)

| Layer | Change |
|-------|--------|
| Pin window | `[mismatch_tick, restore_end)` — mid-span holes, not only exclusive target |
| Donor | t−1, else `LocalGameplayLastTick`, else walk-back ≤32 ticks |
| Promote | Unchanged — `PromoteAfterResimExit` flushes feel-0 → `LOCAL_PUBLISH` after depth=0 |

## Acceptance

Matched APK + Linux binary:

- After follower/initiator resim with mid-span seal gap holds: `POST_RESIM_GAP_HOLD_LAST` for missing mid-span + exclusive target (e.g. 537 from 536, 538 from 537)
- 0× `LOCAL_PUBLISH_LATCH_REFUSE` for those ticks when a prior feel-0 exists
- `LOCAL_PUBLISH` covers the former gap; peer `wire_gap` does not skip owner mid-span
- No `BASELINE_UNIVERSE` deepen from this publish hole (residual invent/Turn sticky is separate)

## Related

- [`netplay_post_resim_finish_defer_feel0_pin_2026-07-26.md`](netplay_post_resim_finish_defer_feel0_pin_2026-07-26.md) — Resync skipped on FINISH_DEFER / Verify early-return
- [`netplay_post_resim_local_publish_gap_branch_deferred_2026-07-26.md`](netplay_post_resim_local_publish_gap_branch_deferred_2026-07-26.md) — exclusive-target pin (accepted; this is the residual hole)
- [`netplay_history_auth_immutable_2026-07-20.md`](netplay_history_auth_immutable_2026-07-20.md) — `SEAL_PACK_GAP_HOLD` / latch refuse past frontier
- [`netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md`](netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md) — invent stability next after absorb retire
