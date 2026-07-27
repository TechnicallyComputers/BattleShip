# PK Thunder Hold same-intent stick GGPO (full Hold window) (2026-07-27)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Bucket:** input contract / Hold aim invent → wpn poison → jibaku CLIFF  
**After:** [Hold/jibaku protect](netplay_ness_hold_jibaku_stick_ggpo_protect_2026-07-27.md); [End cliff](netplay_jibaku_end_cliff_stick_ggpo_2026-07-27.md)

## Symptom

Soak `seed=1822159258` (~1710 ticks): End-cliff protect live (first jibaku @1166 survived with protect through AirHiEnd). Still:

| Tick | Event |
|------|--------|
| 1638 | Hold enter p1 |
| 1640–1683 | Mid-Hold LEDGER invent → `QueueOrWiden` every ~7 ticks |
| **1683** | `(70,18)→(69,30)` Δsy=12 — **not** protected (jibaku @1688 still +5 / not in ring; ahead was 4) |
| 1675–1684 | Transient `wpn` / `fhash_light` forks; rematch by launch |
| **1688** | Matched `jibaku_trigger` |
| 1700 | Still matched in air 236 |
| **1702** | Resim during CLIFF `MpLanding`; then `fhash` fork |
| **1708** | `BASELINE_UNIVERSE` inputs-agree on status 236 |
| Also | Linux-only first-pass jibaku @1432 → resim @1431 → both @1434 (double launch) |

## Root cause

`BlocksStickGgpoForJibaku` only covered Hold when a committed jibaku existed in `(tick, tick+4]`. Late-Hold invent arrives while jibaku is not yet in the ring → same-intent still GGPOs → short resims rewrite thunder head (`wpn`) and later poison air-jibaku CLIFF flight. Ahead widen alone cannot see a future jibaku that is not snapshotted yet.

## Fix

`syNetplayNessSnapTickBlocksStickGgpoForJibaku`:

- `IsPostJibakuLaunch` **or**
- `IsPKThunderHold` (Start/Hold/End, ground+air) — **entire** window, not ahead-gated

Same-intent StickReplace / CommitRemote / LEDGER → Promote-only (`ness_jibaku_stick_protect`). Opposite intent, buttons, release still rewind. End-after-jibaku covered because Hold helper includes End (supersedes lookback-only End rule).

## Acceptance

Matched APK + Linux, Ness long Hold → jibaku near CLIFF:

- Prefer **0×** `GGPO deferred` / `QueueOrWiden` for same-intent stick while status 232–234 / 228–230  
- `class=ness_jibaku_stick_protect` for mid-Hold Δsy≈12 class  
- Prefer **0×** Linux-only early `jibaku_trigger` then matched relaunch ≤3 ticks later from Hold invent  
- No `BASELINE` immediately after air-jibaku CLIFF when launch first-pass matched  
- Opposite-intent / button / release during Hold still short `resim begin`

**Follow-up (`932522105`):** uncapped Hold protect + `SameAnalogIntent` `>8` edge let large/Y-flip aim Promote-only → wpn poison → BASELINE deepen. Hold protect now continuity-capped — [`netplay_pkthunder_hold_aim_protect_universe_spike_2026-07-27.md`](netplay_pkthunder_hold_aim_protect_universe_spike_2026-07-27.md).

## Related

- [`netplay_ness_hold_jibaku_stick_ggpo_protect_2026-07-27.md`](netplay_ness_hold_jibaku_stick_ggpo_protect_2026-07-27.md)
- [`netplay_jibaku_end_cliff_stick_ggpo_2026-07-27.md`](netplay_jibaku_end_cliff_stick_ggpo_2026-07-27.md)
- [`netplay_snap_agree_wpn_hold_aim_2026-07-26.md`](netplay_snap_agree_wpn_hold_aim_2026-07-26.md)
