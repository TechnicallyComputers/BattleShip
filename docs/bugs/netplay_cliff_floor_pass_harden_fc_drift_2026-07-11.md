# Cliff-floor MPColl harden — FRAME_COMMIT figh topn_tx — 2026-07-11

**Status:** **FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)**

## Symptom

soak2 session `362259664` / seed `1994546352` (Android host=client / Linux guest=host, Dream Land, Captain P0 / Kirby P1):

- Intentional `FORCE_MISMATCH` resim @520 (`resim=1`) completed cleanly.
- `FRAME_COMMIT_STATE_DIVERGE @600` **`figh` only**, inputs MATCH.
- Peer field diff: Kirby P1 `Wait` — **`topn_tx` only** (Linux `0x43A6E8EA` ≈ 333.82 vs Android `0x43A4613D` ≈ 328.76; `anim_hash` matched).
- Triggered `FRAME_COMMIT_INPUT_AGREE_REANCHOR` → second resim load @480 → target 601 (`fc_recovery=1`, span 120) — the soak `resim=2` / instability.

## Root cause

Kirby `Pass` → `AttackAirF` → `LandingAirF` → `Wait` on a Dream Land soft/ledge line with **`floor_flags=0x8000` (`MAP_VERTEX_COLL_CLIFF`)**, not `MAP_VERTEX_COLL_PASS` (`0x4000`).

`syNetplayHardenPassPlatformCollBeforeSim` / capture fold / post-load refresh only gated on **PASS**. Matched MpLanding fall through gut 553; after landing, grounded walk forked ~5 units of TopN X by snap 599.

Same stale `pos_prev` / `pos_diff` vs TopN class as [`netplay_frame_commit_pass_platform_fork_2026-07-04.md`](netplay_frame_commit_pass_platform_fork_2026-07-04.md); Sector Arwing already quantized PASS|CLIFF elsewhere.

## Fix

Broaden pass-platform ground coll scope to **`MAP_VERTEX_COLL_PASS | MAP_VERTEX_COLL_CLIFF`** in:

- `syNetplayFighterInPassPlatformGroundCollScope` (`netplay_sim_quantize.c`)
- `syNetRbSnapRefreshPassPlatformGroundCollAfterLoad`
- Fighter capture `pos_prev` re-anchor fold (`netrollbacksnapshot.c`)

## Related

- Follow-on synctest `eff` @749: [`netplay_orphan_damage_effect_synctest_2026-07-11.md`](netplay_orphan_damage_effect_synctest_2026-07-11.md) (separate cosmetic fold hole).
