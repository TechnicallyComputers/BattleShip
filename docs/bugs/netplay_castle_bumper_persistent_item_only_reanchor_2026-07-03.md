# Netplay: persistent Castle bumper item-only skew becomes a later fighter fork

**Date:** 2026-07-03
**Scope:** `port/net/sys/netpeer.c`, `port/net/sys/netsync.c`, `port/net/sys/netrollbacksnapshot.c`.
`PORT && SSB64_NETMENU`, active frame-commit validation / diagnostics only.
**Status:** FIX IMPLEMENTED (soak pending).
**Class:** recovery policy gap for a movable stage hazard whose item-only skew is latent gameplay state.

## Symptom

Android/Linux Castle bumper soak (`session=1153107250`, FORCE_MISMATCH on) no longer crashed and no
longer produced `SYNCTEST_FAIL` / `LOAD_HASH_DRIFT`, but failed frame-commit validation:

```text
FRAME_COMMIT_STATE_DIVERGE validation=1320 diverged=figh,item inputs=MATCH
```

The earlier validations (600, 720, 840, 960, 1080, 1200) were all logged as
`FRAME_COMMIT_ITEM_COSMETIC_OK`: only `item` differed while `figh/world/rng/eff` agreed.

At 1320, the item diff named the Castle bumper (`gobj_id=1013`, `kind=gbumper`). Both peers agreed on
scale, palette, `multi`, and `hit_anim_length`, but the bumper's folded position did not:

- Android: `pos.x = 215.25`
- Linux: `pos.x = 218.75`

That 3.5-unit, grid-snapped position skew was latent for hundreds of ticks, then became gameplay when
Fox touched the bumper from a different relative position. The fighter fields diverged (`topn_tx`,
`topn_ty`, `coll_pos_diff`, `vel_damage_air`), while RNG remained identical.

## Root cause

Frame-commit validation treated all item-only mismatches with matching fighter/world/rng/effect as
cosmetic forever. That was useful for short-lived visual item hash noise, but it is unsafe for Peach's
Castle's movable bumper: the bumper's position is an item hash field and later collision input. A
persistent item-only skew is therefore not cosmetic; it is a delayed fighter fork.

This run proves the pattern:

1. FORCE_MISMATCH resim at 519->522 left the bumper at two different valid grid positions.
2. Frame commit suppressed the item-only mismatch at every 120-tick validation from 600 through 1200.
3. At 1320, the stored bumper offset changed Fox's bumper contact result and forked `figh,item`.

`itgbumper.c`'s `itGBumperCommonProcHit` only updates scale/palette flash (`hit_anim_length`, `multi`,
`palette_id`); it does not explain the position skew. The remaining source is the replay/apply path for
the animation-driven root translate. The added diagnostics below are meant to distinguish load-side
non-idempotence from replay-side drift in the next soak.

## Fix

Two changes landed:

1. **Persistent item-only escalation** (`netpeer.c`): a single item-only frame-commit mismatch still logs
   as `FRAME_COMMIT_ITEM_COSMETIC_OK`, but the second consecutive item-only mismatch with matching inputs
   logs `FRAME_COMMIT_ITEM_PERSISTENT_DIVERGE` and routes through the normal frame-commit mismatch
   recovery (`syNetRollbackOnPeerFrameCommitStateMismatch`). In this exact soak, that would reanchor at
   validation 720 instead of allowing the bumper offset to become a fighter fork at 1320.
2. **Better bumper diagnostics**:
   - `netsync.c` now prints GBumper position raw/quantized bits (`px/py/pz`) alongside scale/palette in
     `item_fold_floats`, so the next item diff names the folded position without relying on rounded
     decimal output.
   - `netrollbacksnapshot.c` lets `gbumper_apply_probe` fire when
     `SSB64_NETPLAY_ITEM_HASH_FIELD_DIFF=1` is set, not only under
     `SSB64_NETPLAY_SNAPSHOT_ITEM_DIAG=1`. The existing Android soak `debug.env` already enables the
     former, so the next run will capture apply-idempotence data without another env update.

## Soak procedure

Re-run the Android/Linux Castle pair with the same debug env. Expected:

- No `FRAME_COMMIT_STATE_DIVERGE[figh,item]` at 1320 from a long-lived bumper position skew.
- If the bumper still item-skews, the second item-only validation should trigger
  `FRAME_COMMIT_ITEM_PERSISTENT_DIVERGE` and reanchor before fighter state forks.
- `item_fold_floats` should now show `px_q/py_q/pz_q`; `gbumper_apply_probe` should be present around
  rollback loads, distinguishing load-side vs replay-side position drift.

## Related

- [`netplay_castle_bumper_resim_uncanonicalized_drift`](netplay_castle_bumper_resim_uncanonicalized_drift_2026-07-02.md)
- [`netplay_castle_bumper_anim_phase_apply`](netplay_castle_bumper_anim_phase_apply_2026-07-03.md)
- [`netplay_castle_bumper_hit_anim_length_free_run_fold`](netplay_castle_bumper_hit_anim_length_free_run_fold_2026-07-02.md)
