# Ground jibaku → SpecialHiEnd cliff stick GGPO (2026-07-27)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Bucket:** input contract / cliff softlip after jibaku  
**After:** [Hold/jibaku stick protect](netplay_ness_hold_jibaku_stick_ggpo_protect_2026-07-27.md); [ground snap quantize](netplay_ness_pkthunder_jibaku_ground_snap_quantize_2026-07-10.md)

## Symptom

Soak `seed=341352768` (~1217 ticks): Hold→jibaku protect live; single matched `jibaku_trigger` @1166; peers match through air 236 → ground 231, then:

| Tick | Event |
|------|--------|
| 1186 | Matched `air_jibaku_ground_snap` (procmap_pass_cliff) → status **231** |
| 1188–1193 | Status 231; same-intent invent Promote (`micro` / `continuity`) |
| **1194** | Status **230** (`SpecialHiEnd`); predicted `(-67,69)→(-63,70)` → **GGPO queued** |
| 1194–1197 | Resim on CLIFF (`SoftLipX floor_edge_skip`, `cliff=1`) |
| **1197** | `BASELINE_UNIVERSE` — Android `top=0x447817D8` (~992) vs Linux `0x44797C6C` (~998); `anim_hash` match; inputs agree |
| 1211 | `VS_SESSION_END` |

SoftLipX only logged skips (no AdjNew writer) — diverge is TopN from resim physics on cliff edge, not a live soft-lip suppress bug.

## Root cause

1. `syNetplayNessSnapTickBlocksStickGgpoForJibaku` covered jibaku/bound and Hold-with-ahead-jibaku, but **not** `SpecialHiEnd` / `SpecialAirHiEnd` after jibaku already finished. End is inside `IsPKThunderHold` for FORCE skip, yet StickReplace still rewound predicted same-intent at End.  
2. StickReplace checked jibaku protect in an `else if` after `if (!predicted)`, so confirmed Δ>`continuity_db` never hit protect either.

## Fix

| Layer | Change |
|-------|--------|
| BlocksStickGgpo | Also TRUE when snap is SpecialHiEnd/AirHiEnd **and** jibaku/bound exists in lookback ≤8 ticks (Hold→End cancel without jibaku stays unprotected) |
| StickReplace | Check `BlocksStickGgpoForJibaku` **first** on same-intent completed-sim (confirmed + predicted, uncapped) |

**Superseded (same day):** [full Hold/End same-intent protect](netplay_pkthunder_hold_same_intent_ggpo_2026-07-27.md) — `IsPKThunderHold` covers End without lookback; ahead-jibaku gate removed.

Log class unchanged: `ness_jibaku_stick_protect`.

## Acceptance

Matched APK + Linux, Ness jibaku landing on Dream Land CLIFF edge:

- Prefer **0×** `GGPO input correction queued` on status **230** within ≤8 ticks after 231/236  
- Prefer `class=ness_jibaku_stick_protect` for End same-intent invent  
- No `BASELINE_UNIVERSE` from TopN-only cliff fork immediately after ground jibaku End  
- Hold→End cancel (no jibaku in lookback) still short-resims material stick when needed

## Related

- [`netplay_ness_hold_jibaku_stick_ggpo_protect_2026-07-27.md`](netplay_ness_hold_jibaku_stick_ggpo_protect_2026-07-27.md)
- [`netplay_ness_pkthunder_jibaku_ground_snap_quantize_2026-07-10.md`](netplay_ness_pkthunder_jibaku_ground_snap_quantize_2026-07-10.md)
- [`netplay_airborne_cliff_lip_jibaku_fc_drift_2026-07-18.md`](netplay_airborne_cliff_lip_jibaku_fc_drift_2026-07-18.md)
