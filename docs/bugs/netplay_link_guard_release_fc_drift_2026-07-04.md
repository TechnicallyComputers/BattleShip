# Netplay Link guard release FC drift + dust eff load hash — 2026-07-04

**Date:** 2026-07-04  
**Session:** `950765188` (Link P0 / Kirby P1, Linux ↔ Android cross-ISA soak)  
**Status:** FIX IMPLEMENTED (soak pending)

## Symptoms

```
FRAME_COMMIT_STATE_DIVERGE @600  figh only, inputs MATCH
LOAD_HASH_DRIFT @480  eff only (sim-core-ok on both peers)
SYNCTEST_SKIP  kirby_jump_aerial_probe (x11, probes 749–759)
SYNCTEST_SKIP  guard_release_boundary_probe (x1, probe 880)
```

### FC @600

Both peers agreed on world/item/rng/eff at validation 600. Fighter digest diverged on player 0 (Link) only:
`status_total_tics` local=3 vs peer=1 at snap 599. Link was in `GuardOff` (status 70, motion -2) on both sides with identical `anim_hash`, but Linux entered `GuardOff` two ticks earlier than Android (596 vs 598) while Z-release inputs matched.

Root cause: `ftCommonGuardOnProcUpdate` transitions to `GuardOff` when `anim_frame <= 0` after `is_release` is set. Cross-ISA float left one peer above zero for extra GuardOn ticks despite `syNetplayQuantizeAnimScalar`.

### LOAD_HASH_DRIFT @480

Live `eff=0xDEEE4097` (one DustLight on gobj_id=1011) vs verify `eff=0x811C9DC5` (empty sentinel). Context: Kirby CopyLink copy commit (status 277). Dust has no snapshot respawn path — forward fold counted it, verify-load did not.

## Fixes

1. **`syNetplayCanonicalizeAnimEndWaitThreshold`** (`port/net/sys/netplay_sim_quantize.c`): when `GuardOn` + `is_release`, or Link/Kirby CopyLink SpecialN anim-end-wait statuses, snap quantized `anim_frame` to `0.0F` if within one quantize grid step so both peers fire the release/end transition on the same tick.

2. **Dust cosmetic rollback exclusion** (`syNetRbSnapEffectHiddenFromRollback` + `syNetRbSnapLiveEffectExcludedFromRollbackHash`): hide `efManagerDustLightProcUpdate` / `efManagerDustHeavyDoubleProcUpdate` from the rollback hash + snapshot (same pattern as quake/ImpactWave/inhale-wind).

3. **Kirby JumpAerial verify presentation repair** (`syNetRbSnapRefreshKirbyJumpAerialPresentationFromSlot`): re-pin joint figatree from slot blob during `prepare_verify`, resim resync, and forward resim while Kirby/NKirby is in `JumpAerialF1`…`F5`.

4. **Removed synctest skips**: `kirby_jump_aerial_probe`, `guard_release_boundary_probe` (and deleted the now-unused guard-release probe helper).

## Soak pass criteria

Re-run soak2 Link/Kirby cross-ISA with synctest enabled:

- No `FRAME_COMMIT_STATE_DIVERGE` at tick 600 (or later guard-release windows).
- No paired `LOAD_HASH_DRIFT[eff]` at tick 480.
- Probes 749–759 and 880 run full `SYNCTEST_OK` (no skip reasons above).
- `./scripts/netplay-scan-drift.py` reports `RESULT: PASS`.
