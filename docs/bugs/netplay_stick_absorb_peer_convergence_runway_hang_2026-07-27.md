# Stick-absorb coalesce + peer_convergence / dual-hot runway hang (2026-07-27)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** Android guest ↔ Linux host dual-stick mash (`soak1-android.log` / `soak1-linux.log`)  
**Bucket:** `REPLAY_DETERMINISM` / admission pacing  
**After:** [dual-stick GGPO resim storm](netplay_dual_stick_ggpo_resim_storm_2026-07-27.md)

## Symptom

Both peers move joysticks → shared sim freezes ~441–444 while render/audio keep ticking.

| Peer | Freeze | Notes |
|------|--------|-------|
| Android | `sim advance blocked (runway_cap) next_sim=445 hr=443 cap=444 frontier_sim=441` | Endless `REMOTE_PUBLISH_SKIP hold_last_completed_sim` @442 |
| Linux | `rollback_epoch_cap=438/441 source=2` (`peer_target` / peer_convergence) | `stick_absorb_coalesce` then mint spam @441 → `VS_SESSION_END` |

Admission: Android `R=0`; Linux `late=409`, low `R`. Not HardStall / zero_onset.

## Root cause

The dual-stick storm fix coalesces local GGPO Begin behind `StickAbsorbUntil` (sim-tick window) and lifts **deferred mismatch-1** live-cap during that wait. Two other caps still froze the match:

1. **peer_convergence / peer_target (`source=2`)** — FSM `EpisodeFsmGetLiveSimCap` and `ComputePeerEpochLiveCap` (deferred/notify targets) re-capped live while absorb waited. Sim never reached `StickAbsorbUntil` → Begin never fired.
2. **Dual-hot / analog-ramp D+1 runway** — when the other peer froze, `hr` stalled; dual-hot tightened `cap = frontier+D+1` and permanently blocked the moving peer.

Absorb is measured in **sim ticks**, so any live-cap during coalesce is a deadlock.

## Fix

| Layer | Change |
|-------|--------|
| Query | `syNetRollbackStickAbsorbCoalesceWaiting()` — absorb until active + local deferred pending (not peer-sym) |
| Arm absorb | `EpisodeFsmOnPostMatch()` when arming post-episode absorb (drop leftover convergence) |
| TryBegin coalesce | Clear peer_convergence on coalesce wait |
| `GetLiveSimCap` | If FSM returns `source=2` while coalesce waiting → clear convergence, fall through |
| `ComputePeerEpochLiveCap` | Return no-cap while coalesce waiting |
| DualStickHot / AnalogRamp | Return FALSE while coalesce waiting (no D+1 runway tighten) |

## Acceptance

Matched APK + Linux netmenu, dual-stick mash after Go:

- No perpetual `rollback_epoch_cap … source=2` while `stick_absorb_coalesce` is logging
- No multi-second `runway_cap` with stuck `frontier_sim` during absorb
- Absorb still coalesces storm (widened deferred / `stick_absorb_coalesce`, not 2× begins/GGPO)
- Soft GGPO after absorb expires still allowed

## Related

- [`netplay_dual_stick_ggpo_resim_storm_2026-07-27.md`](netplay_dual_stick_ggpo_resim_storm_2026-07-27.md) — absorb coalesce + deferred live-cap lift (incomplete without this)
- [`netplay_stick_absorb_peer_convergence_post_episode_hang_2026-07-27.md`](netplay_stick_absorb_peer_convergence_post_episode_hang_2026-07-27.md) — **follow-on:** soak `3438514102` hang @723 after episode (WindowActive + stale clear)
- [`netplay_stick_absorb_resim_metronome_2026-07-27.md`](netplay_stick_absorb_resim_metronome_2026-07-27.md) — **follow-on:** shorter absorb + hard-correction early expire (fat ~17-tick resim feel)
- [`netplay_stick_storm_cooldown_livecap_deadlock_2026-07-12.md`](netplay_stick_storm_cooldown_livecap_deadlock_2026-07-12.md) — classic absorb ↔ live-cap class
