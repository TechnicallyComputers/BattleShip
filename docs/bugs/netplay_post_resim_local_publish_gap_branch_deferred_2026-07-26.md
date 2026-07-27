# Post-resim local publish gap → BRANCH_DEFERRED silent Turn/Dash (2026-07-26)

**Status:** FIX ACCEPTED (`PORT && SSB64_NETMENU`)  
**Soak (pre-fix):** session `1334008206` — kill @428 Turn vs Dash publish gap  
**Soak (accept):** session `802174271` — Android client ↔ Linux host; session to ~1413  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `PEER_SNAPSHOT_DIVERGE` / Turn(18) vs Dash(15) — **cleared on accept soak**

## Symptom

Matched builds after retiring hold-last ahead invent (0× `*_flip_ahead` / `*_release_ahead`). Session dies ~437 on universe deepen. Seed is Turn→Dash at tick **420**:

| Peer | @420 P0 |
|------|---------|
| Linux (owner) | `BRANCH_COMMITTED` · `did_dash=1` · `STICK_SAMPLE` pl `(-80,6)` / device `(-83,6)` |
| Android (remote) | `BRANCH_DEFERRED` · `did_dash=0` · hold_last `(-83,6)` |

**0×** `branch_deferred_same_stick` GGPO in the session. Same-stick heal never armed.

## Timeline

1. Both peers GGPO / follower resim `417→420` (exclusive target).
2. Linux `LOCAL_PUBLISH` P0 **419** `(-83,6)` then **421** `(-83,7)` — **gap at 420** (no `HISTORY_AUTH_FIRST_WRITE` / `LOCAL_PUBLISH`).
3. Post-resim live @420: Linux commits Dash; Android notes `BRANCH_DEFERRED` on predicted hold_last.
4. Android later gets `remote_confirmed` for neighboring wire ticks, never a same-stick confirm that forces GGPO for the deferred Dash tick.

## Root cause

Three layers:

1. **Exclusive-target publish hole** — Resim does not FuncRead/HID-stage the exclusive target. `ResyncControllersAfterResim` only re-pins local gameplay from wire-locked samples; tick 420 had none. Live battle still ran Turn→Dash from pl sticks while History/auth never grew a feel-0 row for 420 → peer never got owner wire for the Dash tick.

2. **Promote during Finish is a no-op** — `PromoteAllLocalAuthoritySlots` returns while `syNetRollbackIsResimulating()` (`ResimDepth>0`). Resync runs before depth is cleared, so even a gameplay pin would not `LOCAL_PUBLISH` until a later path.

3. **Same-stick ticket footgun** — `RequestInputCorrection` / `QueueOrWiden` cleared `BRANCH_DEFERRED` tickets when folding into open resim/pending, so a late ledger refresh could not `deferred_force`.

Related accepted class: [`netplay_branch_deferred_same_stick_silent_peer_2026-07-26.md`](netplay_branch_deferred_same_stick_silent_peer_2026-07-26.md) (heal path OK when wire arrives; this soak never delivered owner auth for the deferred tick).

## Fix

| Layer | Change |
|-------|--------|
| `ResyncControllersAfterResim` | If no wire lock for exclusive target / restore ticks, pin feel-0 gameplay from prior sample (`POST_RESIM_GAP_HOLD_LAST`) — not live HID |
| `FinishForwardResim` | After `ResimDepth=0`, `syNetInputRollbackPromoteAfterResimExit` promotes `[mismatch, restore_end)` so gap pins become `LOCAL_PUBLISH` before first live battle |
| `BRANCH_COMMITTED` (local) | `syNetInputEnsureLocalSimTickPublished` belt: stage+Promote if gameplay-auth History still missing |
| `Request` / `QueueOrWiden` | Do **not** clear BRANCH_DEFERRED tickets when only folding into open resim/pending |

## Acceptance

Matched Android APK + Linux `BattleShip` (session `802174271`):

- `POST_RESIM_GAP_HOLD_LAST` fires; exclusive-target Dash ticks carry gameplay-auth History
- Android `BRANCH_DEFERRED` @410 → `GGPO … branch_deferred_same_stick` equal sticks; @536/@871 unequal wire → StickReplace → both `BRANCH_COMMITTED` `did_dash=1`
- 0× early `did_dash` forks; 0× ahead invent; 0× `BASELINE_UNIVERSE` from this class
- Session end ~1413 is a **separate** Dream Land flower/`ground_fold` map-only PEER (see follow-up bugdoc)

## Related

- [`netplay_post_resim_gap_hold_midspan_walkback_2026-07-26.md`](netplay_post_resim_gap_hold_midspan_walkback_2026-07-26.md) — residual: mid-span feel-0 hole + t−1-only donor → `LATCH_REFUSE`
- [`netplay_branch_deferred_same_stick_silent_peer_2026-07-26.md`](netplay_branch_deferred_same_stick_silent_peer_2026-07-26.md)
- [`netplay_hold_last_flip_ahead_peer_2026-07-26.md`](netplay_hold_last_flip_ahead_peer_2026-07-26.md) — prior soak in chain
- [`netplay_post_resim_wirelocked_hid_restage_2026-07-13.md`](netplay_post_resim_wirelocked_hid_restage_2026-07-13.md) — wire-lock restage (complementary)
- [`netplay_feel0_send_before_sample_release_skew_2026-07-13.md`](netplay_feel0_send_before_sample_release_skew_2026-07-13.md) — auth frontier vs GetTick
- [`netplay_pupupu_flower_loopstart_repair_anim_map_diverge_2026-07-26.md`](netplay_pupupu_flower_loopstart_repair_anim_map_diverge_2026-07-26.md) — accept-soak residual map PEER
