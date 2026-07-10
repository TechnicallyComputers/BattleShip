# Netplay CopyLink JumpAerial knockback FC drift — 2026-07-04

**Date:** 2026-07-04  
**Session:** `371591666` (Link P0 / Kirby P1, Linux ↔ Android cross-ISA soak, seed `2343000409`)  
**Status:** FIX IMPLEMENTED (soak pending)

## Symptoms

```
LOAD_HASH_DRIFT @480  eff only (sim-core-ok on both peers)
FRAME_COMMIT_INPUT_AGREE_REANCHOR last_agreed=480 mismatch=481
FRAME_COMMIT_STATE_DIVERGE @600  figh only, inputs MATCH (0xAA5DF2D2)
VS_SESSION_END ~754 (FC recovery resim from tick 480 failed)
```

No `SYNCTEST_FAIL` or SIGSEGV on this session — FC cross-peer compare caught a latent physics fork that local synctest did not surface.

### FC @600

Both peers agreed on world/item/rng/eff at validation 600. Fighter digest diverged on player 0 (Link) only at snap 599:

- Link in `DamageFlyHi` (status 51, motion 44) on both sides with matched status/motion/anim hash
- `status_total_tics`: Linux 40 vs Android 41
- `topn_tx`/`topn_ty`, `coll_pos_diff_x/y`, `vel_damage_air_x/y` diverged (~120 units X at hit onset)

Kirby P1 had small downstream `topn` diffs. Hidden fork from tick 481 per reanchor trace; Kirby `JumpAerialF1` connected at ~559 and Link entered knockback with different `tr_x` at the MpLanding branch (Linux `0x4438327B` vs Android `0x44346C0F`).

### LOAD_HASH_DRIFT @480

During FC recovery resim at tick 480: `eff live=0x8C04FD5C` vs `verify=0x811C9DC5`; all core partitions matched (`resim-sim-core-ok`). Context: Link SpecialN charge (status 176) with shield bubble; Kirby CopyLink (status 277). Cosmetic eff family — separate from the FC killer.

## Root cause

Two compounding cross-ISA surfaces:

1. **Anim-end wait threshold** — Link SpecialN charge ends via `ftAnimEndSetWait` when `anim_frame <= 0`. One peer held a sub-grid positive frame for an extra tick (~489 vs 490), shifting Link's Wait pose into Kirby's JumpAerial hit window @559. Same class as GuardOn release @950765188.

2. **Airborne knockback MPColl scratch** — After the hit, Link's airborne `DamageFlyHi` integration used stale `pos_prev`/`pos_diff` on one peer while the load-path re-anchor (`syNetRbSnapRefreshAirborneDamageKnockbackCollAfterLoad` + capture mirror) only ran on snapshot apply, not during live forward sim. The fold captured the re-anchored scratch but forward sim diverged tick-to-tick.

## Fixes

1. **`syNetplayCanonicalizeAnimEndWaitThreshold`** (`port/net/sys/netplay_sim_quantize.c`): generalizes guard-release snap — when quantized `anim_frame <= 1/65536`, snap to `0.0F` for:
   - `GuardOn` + `is_release` (replaces standalone `syNetplayCanonicalizeGuardReleaseAnimThreshold`)
   - Link SpecialN family (`nFTLinkStatusSpecialN` … `SpecialAirNEmpty`)
   - Kirby CopyLink SpecialN family (`nFTKirbyStatusCopyLinkSpecialN` … `CopyLinkSpecialAirNEmpty`)

2. **`syNetplayHardenAirborneDamageKnockbackCollForFighter` + `syNetplayHardenAirborneDamageKnockbackCollBeforeSim`**: forward-sim mirror of load-side re-anchor — for airborne `DamageFlyHi`…`DamageFall`, set `pos_prev = *TopN`, zero `pos_diff` at end of `syNetplayCanonicalizeFighterSimState` and before `gcRunAll` (alongside pass-platform hardening in `scvsbattle.c`).

3. **Diagnostic scope**: `SSB64_NETPLAY_KIRBY_COPYLINK_TRACE=1` now covers JumpAerialF1–F5 via `syNetplayKirbyFighterInCopyLinkCombatScope`.

## Soak pass criteria

Re-run soak2 Link/Kirby cross-ISA (seed `2343000409` or equivalent):

- No `FRAME_COMMIT_STATE_DIVERGE` at tick 600 (or later CopyLink combat windows).
- No `FRAME_COMMIT_INPUT_AGREE_REANCHOR mismatch=481` after tick 480 reanchor.
- `./scripts/netplay-scan-drift.py` reports `RESULT: PASS`.
- Tick 480 `eff` WARN may persist (cosmetic); not a session killer.
