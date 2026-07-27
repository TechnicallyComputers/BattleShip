# Ness Hold / jibaku stick GGPO protect (2026-07-27)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Bucket:** Hold/jibaku feel — resim rewrite of first-pass launch  
**After:** [same-intent continuity](netplay_jibaku_post_launch_same_intent_continuity_2026-07-26.md); invent hash_confirm FORCE; SNAP_AGREE+wpn

## Symptom

Soak `seed=4173754130` (~3137 ticks): `BASELINE_UNIVERSE=0` but Hold/jibaku physics feel wrong.

| Tick | Event |
|------|--------|
| 2340 / 2346 | `HASH_CONFIRM_FORCE_GGPO … snap_mismatch` mid-Hold |
| **2347** | Android-only first-pass jibaku `dist=(-213,-24)` |
| **2349** | Both relaunch after resim Hold `dist=(-99,-111)` |
| **2951** | Both first-pass `dist=(-223,5)` |
| 2951 | Material LEDGER `(-75,41)→(-71,56)` Δsy=15 → GGPO |
| **2955** | Both vertical relaunch `dist=(-1,-182)` |

`snap_post_jibaku_same_intent` (≤`continuity_db`) did not cover Δ15; hash_confirm FORCE through Hold undid good launches.

## Root cause

1. **hash_confirm** armed during Ness Hold → defer → `snap_mismatch` → **FORCE_GGPO** through Hold into the entry window → resim misses self-hit / relaunches wrong.  
2. **Post-jibaku Promote-only** was capped at `continuity_db` (12); entry REPLACE with larger same-intent Δ still opened GGPO.

## Fix

| Layer | Change |
|-------|--------|
| Snap helpers | `syNetplayNessSnapTickIsPKThunderHold`; `syNetplayNessSnapTickBlocksStickGgpoForJibaku` (jibaku/bound **or** Hold with jibaku in `(tick, tick+4]`) |
| hash_confirm | Structurally ineligible on Hold snap — no defer/FORCE from invent during Hold |
| FORCE paths | Skip Force (defer resolve / supersede / ledger mismatch) when Hold or jibaku-protect |
| StickReplace | Same-intent + `BlocksStickGgpoForJibaku` → Promote-only **no magnitude cap** (`class=ness_jibaku_stick_protect`) |

Opposite intent / buttons / release still rewind. **Superseded:** mid-Hold same-intent is now Promote-only for the full Start/Hold/End window — see [hold same-intent GGPO](netplay_pkthunder_hold_same_intent_ggpo_2026-07-27.md).

## Acceptance

Matched APK + Linux, Ness ditto:

- Prefer **0×** `HASH_CONFIRM_FORCE_GGPO` during status 233 Hold  
- Prefer **0×** second `jibaku_trigger` within ≤5 ticks with flipped/near-vertical `dist` after a good first-pass  
- `class=ness_jibaku_stick_protect` for entry Δ>12 same-intent  
- Mid-Hold aim before any jibaku snap still short `resim begin`  
- No new `BASELINE_UNIVERSE` storm from promote-only Hold invent (FC / SNAP_AGREE+wpn remain)

## Follow-up

Soak `341352768`: protect held through launch/231; **SpecialHiEnd@1194** still GGPO’d cliff softlip → `BASELINE`. See [jibaku End cliff stick GGPO](netplay_jibaku_end_cliff_stick_ggpo_2026-07-27.md).

## Related

- [`netplay_jibaku_end_cliff_stick_ggpo_2026-07-27.md`](netplay_jibaku_end_cliff_stick_ggpo_2026-07-27.md)
- [`netplay_jibaku_post_launch_same_intent_continuity_2026-07-26.md`](netplay_jibaku_post_launch_same_intent_continuity_2026-07-26.md)
- [`netplay_hash_confirm_mismatch_force_rewind_2026-07-26.md`](netplay_hash_confirm_mismatch_force_rewind_2026-07-26.md)
- [`netplay_snap_agree_wpn_hold_aim_2026-07-26.md`](netplay_snap_agree_wpn_hold_aim_2026-07-26.md)
