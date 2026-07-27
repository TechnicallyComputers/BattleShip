# Hold tracking clock skew → sanitize_delay resurrects pkjibaku_delay → resim misses jibaku (2026-07-27)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** session `936249480`, seed `2582443676`  
**Bucket:** resim reconstruct / hold-entry tracking clock (contractual)

## Symptom

Good soak until late Ness Hold → first-pass self-hit jibaku, then FC recovery resim; jibaku “doesn’t fire.”

| Tick | Path | Event |
|------|------|--------|
| 5126 | first-pass | `jibaku_collide` / `jibaku_trigger` head `(-1865,1230)` → air jibaku |
| 5160/61 | FC | inputs agree; `topn_tx` / `figh` diverge (both `status=230`) |
| 5121→5162 | FC resim `span=41` `fc_recovery=1` | |
| 5120 load | resim | `sanitize_delay was=0 now=41 expected=41 status_tics=56` |
| 5126 | resim | head at `(-1865,1230)` again — **no** collide; Hold continues |
| 5253 | post | `hold_early_exit thunder_destroy` |

First-pass launch was correct; resim rewrote Hold with collide blocked.

## Root cause

Contractual hold-tracking clock mix (not a jibaku status absorb):

1. `syNetplayNessResimHardeningAfterSnapshotLoad` anchors `HoldEntryTick` to `load_tick+1` while `GetTick()` is still the live FC frontier (~5161) — correct for entry pin ([`netplay_airborne_cliff_lip_jibaku_fc_drift_2026-07-18.md`](netplay_airborne_cliff_lip_jibaku_fc_drift_2026-07-18.md)).
2. `HoldFramesSinceEntry` still used **frontier** `GetTick()` → `EntryDelay = live_delay + (frontier − entry) ≈ 96` while blob `pkjibaku_delay=0`.
3. During replay at sim ~5121: `expected = 96 − 56 ≈ 40` → `sanitize_delay` resurrected `0→41`.
4. Vanilla CheckCollide requires delay 0 → head flies through the first-pass hit point; no `jibaku_trigger` on resim.

Same class as gravity resurrection from skewed hold frames; gravity already preferred `status_total_tics` for expected (soak `128377995`). Pkjibaku delay tracking did not.

## Fix (`PORT && SSB64_NETMENU`)

| Layer | Change |
|-------|--------|
| `HoldFramesSinceEntry` | Honor `HoldTrackingAnchorTick` when set (same clock as Sync). |
| `SyncHoldEntryTracking` | Rebuild `HoldEntryDelay` as `live_delay + status_total_tics` (Hold-local age). |
| `ExpectedPkjibakuDelayFromTracking` | Subtract `status_total_tics`, not wall-clock hold_frames (gravity contract). |
| `SanitizePKThunderDelayIfZero` | Skip resurrection when Hold `status_total_tics >= FTNESS_PKJIBAKU_DELAY` (blob delay=0 authoritative after grace). |

No FC deepen carve-out; no move-context GGPO absorb. Resim may still run — it must reconstruct the self-hit.

## Acceptance (re-soak)

Matched APK + Linux, Ness Hold → self-hit, including FC recovery through the Hold window:

- Load mid/late Hold: `sanitize_delay_skip` / no `sanitize_delay was=0 now=N` with `N≫0` when `status_tics ≥ 30`
- Resim through first-pass collide tick: `jibaku_trigger` with first-pass-class launch (not Hold-through + `thunder_destroy`)
- Early-Hold scrub repair (delay=0, `status_tics < 30`) still allowed via sanitize

## Related

- [`netplay_airborne_cliff_lip_jibaku_fc_drift_2026-07-18.md`](netplay_airborne_cliff_lip_jibaku_fc_drift_2026-07-18.md) — load_tick+1 anchor (gravity freeze)
- [`netplay_ness_pkthunder_resim_sanitize_2026-06-01.md`](netplay_ness_pkthunder_resim_sanitize_2026-06-01.md) — legitimate delay=0 skip
- [`netplay_jibaku_post_launch_micro_ggpo_relaunch_2026-07-26.md`](netplay_jibaku_post_launch_micro_ggpo_relaunch_2026-07-26.md) — resim must re-fire self-hit
