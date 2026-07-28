# Dual-hot NoteHard / dash-gate → absorb ping-pong (2026-07-27)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** Android guest ↔ Linux host dual-stick mash (post [dual-slot ping-pong](netplay_stick_absorb_dual_slot_pingpong_2026-07-27.md))  
**Bucket:** resim frequency / absorb coalesce  
**After:** [dual-slot ping-pong](netplay_stick_absorb_dual_slot_pingpong_2026-07-27.md), [absorb metronome](netplay_stick_absorb_resim_metronome_2026-07-27.md)

## Symptom

Fix3 soak: no hang; spans short (med ~4); invent/coalesce helpers live — but dual-stick density still **~every 5 ticks (~12 resims/s)** with cross-slot Begin ping-pong. Most ledger “hard” hits were same-sign dash-gate smash shed / flips during dash-dance; `NoteHard` cleared (then later exempted poorly) global absorb so the other remote Began immediately.

## Root cause

1. **`NoteHard` cleared global absorb** — opposite-slot deferred could Begin as soon as it armed.
2. **Same-sign dash-gate counted as hard** — smash↔sub-smash mag shed (`-72→-48`) NoteHard’d constantly during mash.
3. **DualStickHotPredictTighten returns FALSE during absorb** — cannot gate NoteHard on that API (it early-outs while coalesce waits, which is exactly when NoteHard must stay off).

## Fix

| Layer | Change |
|-------|--------|
| NoteHard(player) | Exempt that slot only (`StickAbsorbExemptPlayer`); do **not** zero `StickAbsorbUntilSim` |
| QueueOrWiden / CoalesceWaiting | Exempt slot may Begin; other slots still absorb-coalesce |
| Dual-hot mash | `syNetInputDualStickHotActive` (ignore absorb) → **skip NoteHard entirely** — wait full absorb window |
| Hard helper | Drop `StickDashGateDisagreeX`; keep buttons / release / onset / opposite analog intent / branch-deferred |

## Acceptance

Matched APK + Linux netmenu, dual-stick mash:

- Global `EPISODE_EXEC` density clearly below ~5-tick metronome while both sticks hot
- Cross-slot Begin pairs with mm gap ≤6 much rarer; more `stick_absorb_coalesce` try_begin_fail
- Spans stay short (no ×2 absorb return)
- No absorb-era hang (`runway_cap` / `peer_convergence` freeze)
- Single-stick polarity (onset/flip/release/button) can still exempt early Begin when not dual-hot

**Follow-on:** Fix4 hot burst still every ~5t via absorb *expiry* metronome — [dual-hot window](netplay_stick_absorb_dual_hot_window_2026-07-27.md).

## Related

- [`netplay_stick_absorb_dual_hot_window_2026-07-27.md`](netplay_stick_absorb_dual_hot_window_2026-07-27.md) — absorb 12 + sticky cap 16 while dual-hot
- [`netplay_stick_absorb_dual_slot_pingpong_2026-07-27.md`](netplay_stick_absorb_dual_slot_pingpong_2026-07-27.md) — narrowed hard classes; invent last_nn
- [`netplay_stick_absorb_peer_convergence_runway_hang_2026-07-27.md`](netplay_stick_absorb_peer_convergence_runway_hang_2026-07-27.md) — why DualStickHotPredictTighten must stay false during absorb
