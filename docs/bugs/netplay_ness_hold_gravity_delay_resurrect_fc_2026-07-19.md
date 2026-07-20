# Ness Hold gravity-delay resurrect → fall-onset +1 tick (2026-07-19)

**Soak:** soak1 session `128377995` seed `3328218731` — Android client ↔ Linux host  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `REPLAY_DETERMINISM`

## Symptom

1. SoftLipPhase parity OK; Hold-head MpLanding matched (prior head harden OK).
2. Hold `tr_x` matched through fall onset; SoftLipX CLIFF present but **not** the killer.
3. Gut **8315**: Android Hold `tr_y` still frozen; Linux already on −0.5 ladder.
4. `PEER_SNAPSHOT_DIVERGE` @8317 `figh`-only after P0 Hold gravity release **+1 tick early on Linux**.

| Gut | Android Hold `tr_y` | Linux Hold `tr_y` |
|-----|---------------------|-------------------|
| 8314 | matched (frozen) | matched (frozen) |
| **8315** | still frozen | **−0.5** (fall starts) |

Earlier FC@8047 (`topn_tx`, PSI Magnet air) limped with deferred recovery; match continued until Hold gravity kill.

## Root cause

`syNetplayNessSanitizePKThunderGravityDelay` could **raise** live `pkthunder_gravity_delay` when HoldEntryTracking `expected > live` (`was=0→1`, `was=1→2`). Android got extra sanitizes @8313 (`status_tics` / GetTick skew) → delay hit 0 one tick later → TopN.y ladder fork.

Contributors:

1. **`ExpectedGravityDelayFromTracking`** used `GetTick() − HoldEntryTick` as hold_frames; wall-clock vs `status_total_tics` skew made `expected` too high.
2. **`syNetplayNessSyncHoldEntryTracking`** could **raise** `HoldEntryGravityDelay` mid-Hold when reconstructed > entry, feeding higher `expected`.
3. Sanitize had no mid-Hold “never bump delay upward” rule (SamePass DEFER already skipped sanitize, but forward Hold did not).

## Fix (`PORT && SSB64_NETMENU`)

1. **Hold gravity authority** — during Hold, if `expected > live`, do **not** increase delay; log `hold_gravity_resurrect_blocked`. Still allow clamp max/min and decreases (resim / post-load).
2. **HoldEntryGravityDelay freeze** — pin at hold_enter / ForceRebuild (`< 0` seed only); remove mid-Hold raise.
3. **Expected delay** — Hold countdown uses `status_total_tics` as hold_frames (not wall GetTick delta).
4. **Fall-onset harden** — existing `syNetplayNessHardenPKThunderHoldAirFallAfterTranslate` on Hold ProcMap; diag `hold_fall_onset_harden` when delay==0 and vel_y < 0.
5. **scan-drift** — `HOLD_GRAVITY_RESURRECT` / `_BLOCKED`; `EARLIEST_FORK`; ty-only fighter PHYSICS_FORK tagged `hold_gravity_risk` (SoftLipX demoted).

Offline / non-rollback unchanged. Head harden from [`netplay_pkthunder_hold_head_cliff_mplanding_jibaku_2026-07-19.md`](netplay_pkthunder_hold_head_cliff_mplanding_jibaku_2026-07-19.md) kept.

## Verify

**Agent:** `cmake --build build --target ssb64 -j 4` (netmenu).

**Human re-soak:** Ness Air Hold through gravity release (Dream Land ok):

- No `sanitize_gravity was < now` mid-Hold on either peer
- Optional `hold_gravity_resurrect_blocked` only when tracking would have resurrected
- No PEER/FC `figh` from Hold fall-onset +1 tick; `tr_y` ladder matches at first −0.5 rung

## Related

- [`netplay_pkthunder_hold_head_cliff_mplanding_jibaku_2026-07-19.md`](netplay_pkthunder_hold_head_cliff_mplanding_jibaku_2026-07-19.md) — Hold-head CLIFF (separate)
- [`netplay_ness_pkthunder_jibaku_resim_hold_drift_2026-07-10.md`](netplay_ness_pkthunder_jibaku_resim_hold_drift_2026-07-10.md) — Hold fall vel ladder / ProcMap harden
- [`netplay_hold_last_soft_onset_lookback_release_fc_2026-07-18.md`](netplay_hold_last_soft_onset_lookback_release_fc_2026-07-18.md) — unrelated hold-last input class
