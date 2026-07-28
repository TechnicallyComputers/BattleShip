# Dual-hot absorb window / sticky coalesce (2026-07-27)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** Android guest ↔ Linux host dual-stick mash (post [dual-hot NoteHard](netplay_stick_absorb_dual_hot_notehard_2026-07-27.md))  
**Bucket:** resim frequency / absorb coalesce  
**After:** [dual-hot NoteHard](netplay_stick_absorb_dual_hot_notehard_2026-07-27.md)

## Symptom

Fix4 soak: quiet periods looked good, but dual-stick **hot burst** (mm 572–648) still ran **~every 5.1 ticks (~11.7 resims/s)** with cross-slot Begin pairs. Skipping NoteHard only stopped *early* Begin; natural `phase_lock=4` absorb expiry still alternated slots every window.

## Root cause

Post-episode absorb length ≡ phase_lock (4) while both remotes keep deferred armed →

`episode → absorb 4 → Begin other slot → absorb 4 → …`

Global mash cadence stays ~5 ticks regardless of NoteHard policy.

## Fix

| Layer | Change |
|-------|--------|
| Arm absorb | If `syNetInputDualStickHotActive(completed_target)`: window **12** (else clamp phase_lock 4..8) |
| Sticky refresh | On absorb coalesce QueueOrWiden while dual-hot: push `UntilSim` to `now+12`, capped at `armed_from+16` |
| Deferred target | Under dual-hot coalesce: clamp target ≤ `mismatch+12` (no frontier fat span) |

## Acceptance

Matched APK + Linux netmenu, dual-stick mash hot burst:

- Burst `EPISODE_EXEC` density clearly below ~5 ticks (target ~every 12–16+ ticks)
- Spans stay bounded (med ideally ≤~8–12, not Fix2 ~17)
- More `stick_absorb_coalesce` during mash; no absorb-era hang
- No token mismatch / desync from longer coalesce

## Related

- [`netplay_stick_absorb_dual_hot_notehard_2026-07-27.md`](netplay_stick_absorb_dual_hot_notehard_2026-07-27.md) — exempt NoteHard + skip under dual-hot
- [`netplay_stick_absorb_resim_metronome_2026-07-27.md`](netplay_stick_absorb_resim_metronome_2026-07-27.md) — why unbounded absorb×frontier was bad
