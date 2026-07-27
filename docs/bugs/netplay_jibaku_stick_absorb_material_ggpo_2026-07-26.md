# Narrow jibaku stick absorb — material REPLACE must GGPO (2026-07-26)

**Status:** SUPERSEDED by [`netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md`](netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md)  
**Soak (pre-fix):** session `391759253` seed `2713861087` — Android client ↔ Linux host (Ness ditto)  
**Bucket:** `REPLAY_DETERMINISM` / input contract  
**After:** defer-removal Phases 1–4 accepted; [`netplay_ness_pk_defer_retire_input_resim_2026-07-26.md`](netplay_ness_pk_defer_retire_input_resim_2026-07-26.md)

## Symptom

Soft-stable session; Phase 4 echo clean. Hard residue @**1539**:

| Step | Detail |
|------|--------|
| P1 | AirHiJibaku (`status=236`) |
| Predict | hold_last `(64,31)` |
| Wire | `(52,13)` |
| Absorb | `REMOTE_PUBLISH_SKIP hold_last_ness_jibaku_absorb` + `LEDGER_REFRESH … skipped class=ledger_jibaku_stick` |
| Kill | `BASELINE_UNIVERSE_MISMATCH` / PEER — **inputs agree through load** |

Full mid-jibaku stick absorb blocked the short confirmed-input rewind that Phases 1–4 now allow.

## Root cause

`syNetInputJibakuStickAbsorbBlocksGgpo` returned TRUE for **any** stick-only REPLACE in jibaku/bound (July 17 storm fix, when `ness_pk_defer` grew spans). After defer retirement, absorbing material aim deltas leaves peers on divergent jibaku physics with no GGPO heal.

Launch vel is locked at entry — micro noise is still worthless to rewind; large REPLACE is a desync seed.

## Fix (`PORT && SSB64_NETMENU`)

| Layer | Change |
|-------|--------|
| Absorb helper | Block GGPO only when `dx,dy ≤ 16` (jibaku deadband) and same analog intent (or non-analog) |
| Material | `dx` or `dy` > 16, or intent flip → return FALSE → GGPO / ledger QueueOrWiden |
| Log | `jibaku_stick absorb bypass material …` for soak triage |
| Keep | Buttons/release rewind; BRANCH_DEFERRED bypass; hold_last invent skip in jibaku scope; no TryBegin defer |

Deadband 16: keeps July-17 `sy 4→15` and ledger ±14 class as Promote-only; soak `(64,31)→(52,13)` (dy=18) GGPO.

## Acceptance

Matched APK + Linux, Ness ditto through jibaku with late stick wire:

- Material REPLACE: `jibaku_stick absorb bypass material` + short `resim begin` (span ≪ 20); no perpetual absorb skip
- Micro/same-intent noise: still `skipped class=jibaku_stick` / `ledger_jibaku_stick`
- No inputs-agree PEER at mid-jibaku from absorbed material aim
- Prefer short GGPO over SoftLip; if resim re-launches vertically, chase `jibaku_post_cull` — not full re-absorb

## Related

- [`netplay_ness_jibaku_stick_ggpo_storm_eff_load_2026-07-17.md`](netplay_ness_jibaku_stick_ggpo_storm_eff_load_2026-07-17.md) — original full absorb (superseded under netmenu for material)
- [`netplay_jibaku_ledger_refresh_stick_resim_launch_2026-07-26.md`](netplay_jibaku_ledger_refresh_stick_resim_launch_2026-07-26.md) — ledger hole; micro still absorbed
- [`netplay_ness_pk_defer_retire_input_resim_2026-07-26.md`](netplay_ness_pk_defer_retire_input_resim_2026-07-26.md) — TryBegin defer retired
