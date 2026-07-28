# Stick-absorb hard-expire → dual-slot Begin ping-pong (2026-07-27)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** Android guest ↔ Linux host dual-stick mash (post [absorb metronome shrink](netplay_stick_absorb_resim_metronome_2026-07-27.md))  
**Bucket:** resim frequency / absorb coalesce  
**After:** [absorb metronome](netplay_stick_absorb_resim_metronome_2026-07-27.md), [dual-stick GGPO storm](netplay_dual_stick_ggpo_resim_storm_2026-07-27.md)

## Symptom

No hang / desync. Spans short (med 4, max 6) after Fix2 absorb shrink, but dual-stick still felt like a constant rubber-band:

| Metric | soak1 (Fix2) |
|--------|----------------|
| `EPISODE_EXEC` | 144 both sides over mm 433–1183 |
| Cadence | ~every 5.2 ticks globally (~11.5 resims/s) |
| Cross-slot | 135/143 consecutive episodes swap slot |
| Absorb coalesce | ~12 `try_begin_fail stick_absorb_coalesce` vs 144 execs |

## Root cause

1. **Hard absorb expire too broad** — `syNetInputLedgerRefreshHardStickCorrection` treated general `real_stick` mag REPLACE as hard and called `NoteHardCorrection`, clearing `StickAbsorbUntilSim`. Opposite-slot mag hits cleared absorb immediately → Begin every ~phase_lock ticks instead of coalescing under the window.
2. **Predicted same-intent still QueueOrWiden** inside an already-armed same-slot deferred span (extra widen/NoteHard pressure). Bare predicted micro-skip remains forbidden (soak 740113729).
3. **Zero invent after brief neutral confirm** — `TryFillFromLastNonNeutral` refused `last_nn` whenever `last_nn.tick <= last_confirmed.tick`, so a short wire `(0,0)` buried a hot stick and seeded onset GGPOs.

## Fix

| Layer | Change |
|-------|--------|
| Hard expire | NoteHard only for branch-deferred / buttons / release / onset / dash-gate XOR / opposite analog intent — **not** same-octant mag |
| Soft skip | Predicted same-intent ≤`continuity_db` during absorb coalesce when deferred already covers **same player** + tick → Promote-only (`absorb_coalesce_same_intent`) |
| Invent | If `last_confirmed` is near-neutral but remote stick still hot (`SlotStickHotRecent`) and `last_nn` age ≤8, allow `last_nn` fill |

Not re-enabled: predicted bare micro-skip; mismatch−1 live-cap during absorb.

## Acceptance

Matched APK + Linux netmenu, dual-stick mash:

- Global `EPISODE_EXEC` cadence clearly below Fix2 ~5-tick metronome (absorb coalesce engages again)
- Cross-slot Begin ping-pong (gap ≤6) much rarer; `stick_absorb_coalesce` try_begin_fail appears for soft REPLACE storms
- Spans stay short (no return of ×2 absorb fat pulses)
- No absorb-era hang (`runway_cap` / `peer_convergence` freeze before `StickAbsorbUntil`)
- No PEER from predicted micro silent Promote

**Follow-on:** Fix3 soak still ~12 resims/s — dash-gate + global NoteHard clear. See [dual-hot NoteHard](netplay_stick_absorb_dual_hot_notehard_2026-07-27.md).

## Related

- [`netplay_stick_absorb_dual_hot_notehard_2026-07-27.md`](netplay_stick_absorb_dual_hot_notehard_2026-07-27.md) — exempt NoteHard + skip under dual-hot + drop dash-gate hard
- [`netplay_stick_absorb_resim_metronome_2026-07-27.md`](netplay_stick_absorb_resim_metronome_2026-07-27.md) — Fix2 span shrink + early hard expire (narrowed here)
- [`netplay_stick_absorb_peer_convergence_runway_hang_2026-07-27.md`](netplay_stick_absorb_peer_convergence_runway_hang_2026-07-27.md) — hang during coalesce
- [`netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md`](netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md) — why predicted micro-skip stays off
