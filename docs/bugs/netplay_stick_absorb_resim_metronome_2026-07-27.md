# Stick-absorb coalesce → fat deferred resim metronome (2026-07-27)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** Android guest ↔ Linux host dual-stick mash (post [peer_convergence hang fix](netplay_stick_absorb_peer_convergence_runway_hang_2026-07-27.md))  
**Bucket:** resim feel / deferred span growth  
**After:** [dual-stick GGPO resim storm](netplay_dual_stick_ggpo_resim_storm_2026-07-27.md), [absorb peer_convergence/runway hang](netplay_stick_absorb_peer_convergence_runway_hang_2026-07-27.md)

## Symptom

Dual-stick netmenu after the hang fix: sim advances but resims feel like a ~17-tick metronome — repeated fat spans while both players mash.

Logs show `stick_absorb_coalesce` holding Begin, while deferred `target_tick` keeps tracking the live frontier (`EpisodeResolvedThrough` + absorb window). Each completed episode re-arms absorb with `max(phase_lock,8)×2` (~16 ticks) before the next Begin, so one coalesced deferred episode can cover ~17 sim ticks of gameplay per pulse.

## Root cause

1. **Long absorb window** — `syNetRollbackCloseCorrectionEpisode` used `max(phase_lock, 8) × 2` (cap 32). That was right for same-player REPLACE storms but too long once deferred targets advance with frontier during coalesce (dual-stick).
2. **No early break on hard corrections** — onset, release, buttons, intent flip, and branch-deferred ledger refreshes still waited the full absorb window even though they need a fresh Begin soon.

**Not** the fix: re-enabling mismatch−1 live-cap during absorb (sim must advance until `StickAbsorbUntil` on sim ticks).

## Fix

| Layer | Change |
|-------|--------|
| Absorb length (NETMENU) | `absorb_window = clamp(phase_lock, 4, 8)` — no ×2; `StickAbsorbUntilSim = completed_target + absorb_window` |
| Hard break | `syNetRollbackStickAbsorbNoteHardCorrection()` clears absorb state |
| LEDGER_REFRESH | After `needs_rewind`, before `QueueOrWiden` / Force (when hash_confirm did not soft-own): call NoteHard when `syNetInputLedgerRefreshHardStickCorrection` (button / release / onset / intent disagree / real_stick / branch-deferred) |

## Acceptance

Matched APK + Linux netmenu, dual-stick mash:

- Resim cadence no longer locked to ~17-tick fat pulses every absorb cycle
- `stick_absorb_coalesce` still appears for soft REPLACE storms (shorter window)
- Hard polarity / button corrections can Begin without sitting the full absorb window
- No return of absorb-era live-cap hang (`rollback_epoch_cap` stuck before `StickAbsorbUntil`)

**Follow-on:** Fix2 hard-expire on general `real_stick` mag cleared absorb too often → short dual-slot Begin ping-pong (~11 resims/s). Narrowed in [dual-slot ping-pong](netplay_stick_absorb_dual_slot_pingpong_2026-07-27.md).

## Related

- [`netplay_stick_absorb_dual_slot_pingpong_2026-07-27.md`](netplay_stick_absorb_dual_slot_pingpong_2026-07-27.md) — narrow hard-expire + invent / coalesce soft-own
- [`netplay_stick_absorb_peer_convergence_runway_hang_2026-07-27.md`](netplay_stick_absorb_peer_convergence_runway_hang_2026-07-27.md) — prior hang (peer_convergence + runway during coalesce)
- [`netplay_dual_stick_ggpo_resim_storm_2026-07-27.md`](netplay_dual_stick_ggpo_resim_storm_2026-07-27.md) — any-slot absorb + coalesce Begin
