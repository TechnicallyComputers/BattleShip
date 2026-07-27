# Ledger-refresh mid-jibaku stick GGPO → Hold extend → vertical launch (2026-07-26)

**Status:** SUPERSEDED absorb policy — see [`netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md`](netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md); vertical re-launch → fix `jibaku_post_cull` / invent, not absorb  
**Soak:** session `990461745` seed `571785487` — Android client ↔ Linux host (Ness ditto)  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** input contract / `REPLAY_DETERMINISM`

## Symptom

First-pass PK Thunder self-hit launched in the aimed direction; after a short confirmed-input resim Ness re-triggered jibaku nearly straight down.

| Pass | Tick | `jibaku_launch_dist` |
|------|------|----------------------|
| First-pass (good) | 1626 | `dist=(-223, 68)` → `vel≈(-191, 58)` |
| Post-resim (bad) | 1630 | `dist=(13, -75)` → `vel≈(34, -197)` |

Same shape earlier: good @1047 `(-191, 56)`, post-resim @1049 `(-78, -34)`. Both peers committed the bad launch (symmetric wrong physics).

## Root cause

1. First-pass jibaku @1626 matched on both peers.
2. Android later ledger-refreshed P0 @1626: hold_last `(-79,23)` → wire `(-76,37)` at `sim_now=1629` (already in jibaku).
3. `LEDGER_REFRESH_COMPLETED_SIM_CORRECT` called `QueueOrWiden` **without** the `jibaku_stick` absorb used by `StickReplaceNeedsRewind`.
4. `QueueOrWiden` early-fold (open deferred / post-episode `StickAbsorbUntilSim`) can arm GGPO without re-running StickReplace policy.
5. Short resim load@1625→target@1630 stayed Hold (`jibaku_post_cull hold_skip=1`, no resim-path collide) — thunder kept moving; late re-collide @1630 with head nearly above Ness → vertical launch.

Policy after `ness_pk_defer` retirement: mid-jibaku **micro** stick-only = Promote-only; material aim GGPO ([narrow absorb](netplay_jibaku_stick_absorb_material_ggpo_2026-07-26.md)). Ledger refresh was a hole for the micro path. Do **not** re-add TryBegin defer.

## Fix (`PORT && SSB64_NETMENU`)

| Layer | Change |
|-------|--------|
| `syNetInputJibakuStickAbsorbBlocksGgpo` | Shared helper: live jibaku/bound + stick-only (not buttons/release) + no BRANCH_DEFERRED ticket |
| `StickReplaceNeedsRewind` | Route jibaku absorb through the helper |
| `LEDGER_REFRESH` | On completed-sim stick delta: Promote history, skip `QueueOrWiden` when helper TRUE (`skipped class=ledger_jibaku_stick`) |
| `QueueOrWidenStickCorrection` | Belt: same absorb before early-fold / Request |

Offline unchanged. Hold/Start aim REPLACE still GGPO (absorb is live jibaku/bound only).

## Acceptance

Matched APK + Linux, Ness ditto Hold→jibaku with late stick wire after entry:

- `LEDGER_REFRESH_COMPLETED_SIM_CORRECT … skipped class=ledger_jibaku_stick` (or QueueOrWiden `class=jibaku_stick`) for mid-jibaku stick-only
- No GGPO span through the entry tick from that class
- First-pass `jibaku_launch_dist` kept (no second trigger with near-vertical `dist`)
- Mid-Hold aim REPLACE before entry still short-resims

## Residual

Legitimate Hold-aim REPLACE that must rewind through the entry tick still needs resim weapon/collide quality (`jibaku_post_cull hold_skip` / restore). That is separate from this absorb hole — see retire follow-up in [`netplay_ness_pk_defer_retire_input_resim_2026-07-26.md`](netplay_ness_pk_defer_retire_input_resim_2026-07-26.md).

## Related

- [`netplay_ness_pk_defer_retire_input_resim_2026-07-26.md`](netplay_ness_pk_defer_retire_input_resim_2026-07-26.md) — TryBegin defer retired; jibaku_stick absorb kept
- [`netplay_ness_jibaku_stick_ggpo_storm_eff_load_2026-07-17.md`](netplay_ness_jibaku_stick_ggpo_storm_eff_load_2026-07-17.md) — original StickReplace absorb
- [`netplay_ness_jibaku_launch_dist_hold_head_fc_2026-07-19.md`](netplay_ness_jibaku_launch_dist_hold_head_fc_2026-07-19.md) — launch = fighter − head
