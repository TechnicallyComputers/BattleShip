# netplay effect hash folds `effect_vars.quake.priority` for non-quake effects (union alias)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`), soak pending.

## Symptom

Soak2 session `1855301997` (host=`soak2-android.log`, guest=`soak2-linux.log`):

```
[FAIL] tick 2160: diverged=eff,item inputs=MATCH (genuine cross-ISA determinism failure)
```

`fc_div_inputs_match=2` — the peers produced different frame-commit tokens from **identical
inputs**, i.e. a genuine cross-ISA determinism failure, not a rollback misprediction. The
effect-fold diagnostic at the capture tick showed a single effect on both peers with all visible
fields matching **except** the quake priority:

```
[host]  eff_fold_diag tag=capture tick=2159 idx=0 gobj_id=1011 bank=0 respawn=6 parent_id=1000 is_pause=0 anim_frame=0x41400000 quake_pri=1   ... pos=(0,0,0)
[guest] eff_fold_diag tag=capture tick=2159 idx=0 gobj_id=1011 bank=0 respawn=6 parent_id=1000 is_pause=0 anim_frame=0x41400000 quake_pri=197 ... pos=(0,0,0)
```

`respawn=6` is `SYNETRB_EFFECT_RESPAWN_REBIRTH_HALO`, `parent_id=1000` is the coupled fighter —
this is the rebirth-halo effect (proc `gcPlayAnimAll`), **not a quake**. The `quake_pri` field read
`1` on one peer and `197` on the other, and that byte was folded straight into the effect hash,
diverging `eff`.

## Root cause

`EFStruct.effect_vars` is a union; `effect_vars.quake.priority` is only the live member for genuine
quake shells. `syNetSyncHashEffectStructForRollback` (`port/net/sys/netsync.c`) folded it
**unconditionally** for every effect:

```c
fold = syNetSyncFnvAccumulateU32(fold, (u32)ep->effect_vars.quake.priority);
```

For a non-quake effect (rebirth halo, Ness PK effects, generic damage particles, …) that read
incidental union bytes belonging to a different overlay. Those bytes are not guaranteed to agree
cross-ISA — for the rebirth halo they held `1` vs `197` — so the effect hash forked even though the
effect's real state was identical on both peers.

This is the effect-hash analogue of the `FTStatusVars` union-stomp class: reading one union overlay
while a different overlay is live yields garbage that happens to differ across builds.

## Fix

Fold `effect_vars.quake.priority` only when the effect is a genuine quake, using the same predicate
the snapshot layer already uses to classify quakes (`syNetRbSnapLiveEffectIsQuake`), exposed to the
sync-hash layer as `syNetRbSnapshotLiveEffectIsQuake`:

```c
if (syNetRbSnapshotLiveEffectIsQuake(gobj, ep) != FALSE)
{
    fold = syNetSyncFnvAccumulateU32(fold, (u32)ep->effect_vars.quake.priority);
}
```

- Genuine quakes (incl. priority-4 Firefox impact shells, which the snapshot layer already treats as
  quake-like for same-id round-trip stability) still fold their authoritative priority — no hash
  change for them (the tick-599/600 pri=4 shell hashed identically on both peers already).
- The rebirth halo (`fighter_gobj != NULL`, proc `gcPlayAnimAll`) is correctly excluded, so its
  incidental union byte no longer poisons the effect hash.

Both peers run identical netmenu code, so the gate is applied symmetrically.

## Also in this change: removed `effect_count_transition_probe` synctest skip

Per the same soak's request, the `effect_count_transition_probe` synctest skip was removed from
`syNetRbSnapshotSynctestShouldSkipProbeTick`. It suppressed the synctest self-check on any tick whose
`effect_count` differed from the prior tick (e.g. Fox Firefox charge ImpactWave spawn/despawn),
masking real save/load fidelity gaps instead of localizing them. The resim-anchor path never used it
(it runs with `s_syNetRbSnapResimAnchorEffectRepairTolerant == TRUE`, which gated the block off), so
removal only re-enables full synctest coverage on effect-count transitions. The Yoshi egg-lay hatch
cosmetic that the skip path used to replay is still recovered on the full-verify path via
`syNetRbSnapshotRecoverYoshiEggLayHatchAfterSynctest`. The now-unused
`syNetRbSnapshotSlotEffectCountTransitionFragile` helper was deleted.

## Not addressed here (separate issue)

Ticks 600 and 2160 also diverge on `item` (and 600 on `rng`). The `item_field_diff` shows the drift
is the Castle bumper (`kind=23 gbumper`, `gobj_id=1013`) — a pre-existing cross-ISA float
determinism problem in the bumper item, unrelated to the effect union fold or the probe skip. The
`rng` divergence at 600 is downstream of the item mismatch. Tracked separately.

## Files

- `port/net/sys/netsync.c` — gate the priority fold on `syNetRbSnapshotLiveEffectIsQuake`.
- `port/net/sys/netrollbacksnapshot.c` — expose `syNetRbSnapshotLiveEffectIsQuake` wrapper; remove
  `effect_count_transition_probe` skip block + unused `syNetRbSnapshotSlotEffectCountTransitionFragile`.
- `port/net/sys/netrollbacksnapshot.h` — declare `syNetRbSnapshotLiveEffectIsQuake` + `struct EFStruct`.
