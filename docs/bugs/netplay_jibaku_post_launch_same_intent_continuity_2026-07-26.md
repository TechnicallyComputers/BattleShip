# Post-jibaku same-intent continuity (not only micro) (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Bucket:** input contract / jibaku relaunch → `BASELINE_UNIVERSE`  
**After:** [snap_post_jibaku_micro](netplay_jibaku_post_launch_micro_ggpo_relaunch_2026-07-26.md); [SNAP_AGREE wpn](netplay_snap_agree_wpn_hold_aim_2026-07-26.md)

## Symptom

Soak `seed=2697432430` (~1584 ticks): invent/`wpn` SNAP_AGREE healthy, then:

| Tick | Event |
|------|--------|
| 1538 | First-pass jibaku matched — `dist=(208,210)` `vel≈(141,142)` |
| 1538 | Predicted invent `(66,10)` → wire `(66,17)` (Δsy=7) → material GGPO epoch 70 |
| 1542 | Resim relaunch — `dist=(-32,83)` `vel≈(-72,187)` (both peers) |
| 1546+ | p1 `fhash_light` forks during 236 / 234; live aggregate `figh` often still matches |
| ~1579 | `BASELINE_UNIVERSE` inputs-agree deepen → `VS_SESSION_END` |

`snap_post_jibaku_micro` fired later (1546+) for Δ≤3, but **missed the entry REPLACE**.

## Root cause

[`snap_post_jibaku_micro`](netplay_jibaku_post_launch_micro_ggpo_relaunch_2026-07-26.md) Promote-only only when `|Δ| ≤ micro_db` (default **3**). Same-intent mag drift of **7** (inside confirmed `continuity_db` default **12**) still opened GGPO through the already-committed jibaku entry tick → Hold extend / miss self-hit → corrupt second launch. Downstream light/baseline skew is fallout.

## Fix

When predicted (or any path in the completed-sim same-intent branch) and `syNetplayNessSnapTickIsPostJibakuLaunch(player, sim_tick)`:

- Promote-only if `|Δx|,|Δy| ≤ continuity_db` (not only `micro_db`)
- Log class: `snap_post_jibaku_same_intent` (summary counter still `skipped_snap_post_jibaku_micro`)
- Unchanged: opposite intent, buttons, release, Hold snap (pre-entry aim) still rewind

## Acceptance

Matched APK + Linux, Ness ditto Hold→jibaku with late same-intent wire Δ≈7 after first-pass entry:

- `class=snap_post_jibaku_same_intent` for `(66,10)→(66,17)`-class at entry tick
- **0×** second `jibaku_trigger` with near-vertical / flipped `dist` from that GGPO
- Mid-Hold material aim before entry still short `resim begin`
- Micro path still counted; hash_confirm / SNAP_AGREE+wpn unchanged

## Follow-up

Soak `4173754130`: same-intent Δ=15 > `continuity_db` still GGPO’d entry; Hold `FORCE_GGPO` chewed first-pass. Deepened in [Hold/jibaku stick GGPO protect](netplay_ness_hold_jibaku_stick_ggpo_protect_2026-07-27.md) (uncapped protect + no hash_confirm FORCE on Hold).

## Related

- [`netplay_ness_hold_jibaku_stick_ggpo_protect_2026-07-27.md`](netplay_ness_hold_jibaku_stick_ggpo_protect_2026-07-27.md)
- [`netplay_jibaku_post_launch_micro_ggpo_relaunch_2026-07-26.md`](netplay_jibaku_post_launch_micro_ggpo_relaunch_2026-07-26.md)
- [`netplay_snap_agree_wpn_hold_aim_2026-07-26.md`](netplay_snap_agree_wpn_hold_aim_2026-07-26.md)
- [`netplay_presim_invent_confirm_without_rewind_2026-07-26.md`](netplay_presim_invent_confirm_without_rewind_2026-07-26.md)
