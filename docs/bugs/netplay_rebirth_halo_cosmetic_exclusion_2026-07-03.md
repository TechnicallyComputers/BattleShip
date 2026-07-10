# Netplay: rebirth halo recycled-id cosmetic exclusion

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)
**Date:** 2026-07-03

## Symptom

Soak session `1926122400` confirmed the previous drop-in gate fix was deployed, but both peers
still reported deterministic `SYNCTEST_FAIL` / `LOAD_HASH_DRIFT` in the double-KO rebirth window:

```text
tick 2189: diverged=eff,figh
eff_fold_diag tag=capture tick=2189 count=1 ... gobj_id=1011 respawn=6 parent_id=1000 ...
eff_fold_diag tag=verify  tick=2189 count=0 ... hash=0x811C9DC5

tick 2309: diverged=eff
eff_fold_diag tag=capture tick=2309 count=2
  idx=0 gobj_id=1011 respawn=6 parent_id=1000 anim_frame=0x41C80000
  idx=1 gobj_id=1011 respawn=6 parent_id=1000 anim_frame=0x41A80000
eff_fold_diag tag=verify tick=2309 count=0
```

Runtime rebirth diagnostics showed this was not the previous one-frame drop-in boundary case. At
tick 2189 player 1 was still in `RebirthWait` with `is_rebirth=1`, `is_effect_attach=1`, and a live
halo. By tick 2309 both players were in `RebirthWait`, each with a halo, and the two halo GObjs
shared the recycled pool id `1011`.

## Root cause

The rebirth halo effect is a fighter-attached visual shell driven by `gcPlayAnimAll`. Its only
coupling is `EFStruct::fighter_gobj` plus the DObj `user_data.p` pointer to the fighter's TopN
joint. The actual rebirth gameplay state is already in the fighter snapshot:
`status_vars.common.rebirth`, `is_rebirth`, `is_effect_attach`, pose, invincibility/camera state,
and lifecycle timers.

Keeping the halo in the rollback effect snapshot made the id-keyed effect layer authoritative for a
cosmetic shell whose GObj id is recycled aggressively. During verify-only synctest loads the live
emergency state can contain a matching halo under a different recycled id, or two simultaneous
halos with the same id. The general missing-effect respawn pass is intentionally disabled during
verify-only loads, so the snapshot collapsed the live halo set and produced an `eff` hash drift;
when a fighter kept `is_effect_attach=1` with no visible halo shell, `figh` could drift too.

This is the same failure class as the quake and ImpactWave recycled-id bugs, except the halo is
attached to the fighter for presentation rather than carrying gameplay state.

## Fix

`syNetRbSnapEffectHiddenFromRollback` now treats live rebirth halo shells as cosmetic rollback-hidden
effects. The predicate matches only real halo coupling:

- `ep->proc_update == gcPlayAnimAll`
- `ep->fighter_gobj` resolves to a live fighter
- the effect root DObj `user_data.p` is the fighter TopN joint

Hidden halos are excluded by the shared active-effect enumerator, so they are omitted from both the
rollback effect snapshot and `syNetSyncHashActiveEffectsForRollback`. Forward simulation and the
existing rebirth presentation repair still mint/prune halo shells from fighter state, but the shell
itself no longer participates in authoritative rollback state.

## Verify

- `build-netmenu` `ssb64` target: passed.
- `build-offline` `ssb64` target: passed.
- Soak pending: rerun the double-KO rebirth soak from session `1926122400`; expected result is no
  `eff` / `figh` `SYNCTEST_FAIL` from `respawn=6` halo rows. Rebirth-only halo frames should fold
  as zero effects on both capture and verify.
