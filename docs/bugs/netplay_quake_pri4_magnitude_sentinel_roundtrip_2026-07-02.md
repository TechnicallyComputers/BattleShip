# netplay pri-4 quake shell priority lost across save/load (magnitude 0xFF sentinel collision)

**Status:** SUPERSEDED (`PORT && SSB64_NETMENU`) by
[`netplay_impact_wave_respawn_quake_alias_2026-07-02.md`](netplay_impact_wave_respawn_quake_alias_2026-07-02.md).
This write-up correctly identified why the `pri=4` shell failed to round-trip, but the first fix
(`efManagerQuakeMakeEffect((s32)(s8)blob->quake_magnitude)`) was wrong: the `pri=4` shell is not a
quake at all. It is an `efManagerImpactWaveMakeEffect(..., index=4, ...)` shell whose
`impact_wave.index` aliases `quake.priority` in `effect_vars`. Creating it through the quake factory
with magnitude `-1` gives it no quake anim joint, so it stops animating/ejecting and can keep driving
camera updates until the effect pool is exhausted. The replacement fix gives ImpactWave its own
respawn/match/hash path and removes the `priority<=4` quake classification.

## Symptom

Soak2 session `184160435`, guest (`soak2-linux.log`): deterministic **eff-only** `SYNCTEST_FAIL` at
tick 2310 during a Fox air-Firefox charge. The failure is a **local** save/load fidelity gap
(capture vs verify on the same peer), not a cross-peer divergence:

```
eff_fold_diag tag=capture tick=2310 idx=0 gobj_id=1011 respawn=1 quake_pri=4 pos=(0x44A50000,0x421E0000,0)
eff_fold_diag tag=capture tick=2310 idx=1 gobj_id=1011 respawn=1 quake_pri=3 pos=(0,0x41D80000,0)
...
eff_fold_diag tag=verify  tick=2310 idx=0 gobj_id=1011 respawn=1 quake_pri=0 pos=(0x44A50000,0x421E0000,0)   <-- priority 4 -> 0
eff_fold_diag tag=verify  tick=2310 idx=1 gobj_id=1011 respawn=1 quake_pri=3 pos=(0,0x41D80000,0)
LOAD_HASH_DRIFT tick=2310 ... eff=0x262DC912/0x26524076
slot_effect_enforce tick=2310 ejected=3 canonical=1 slot_count=2
quake_surplus_prune tick=2310 ejected=2 slot_count=2
quake_unmatched_prune tick=2310 ejected=1
```

Two quake blobs share the recycled `gobj_id=1011` — a priority-4 shell (Fox Firefox charge impact
wave at a world-space position) and a genuine priority-3 camera quake. The `pri=3` quake round-trips
fine; the `pri=4` shell comes back as `pri=0`, so the effect hash forks and synctest fails.

## Root cause

Vanilla `efManagerQuakeMakeEffect(magnitude)` (`decomp/src/ef/efmanager.c`) ends with:

```c
ep->effect_vars.quake.priority = 3 - magnitude;   /* magnitude 0..3 -> priority 3..0 */
```

so a genuine quake only ever has priority 0..3. Capture inverts the formula:

```c
blob->quake_magnitude = (u8)(3 - ep->effect_vars.quake.priority);
```

Priorities 0..3 serialize to magnitudes 3..0. **Priority 4** (the impact shell, classified
quake-like for hashing/matching by earlier work) serializes to `(u8)(3 - 4) = (u8)(-1) = 0xFF`,
which collides with the "unknown magnitude" sentinel. Restore therefore could not reconstruct the
priority from magnitude and fell into a special path:

```c
/* old syNetRbSnapRespawnQuakeEffectForBlob */
if (blob->quake_magnitude != 0xFFU) return efManagerQuakeMakeEffect((s32)blob->quake_magnitude);
... return efManagerQuakeMakeEffect(0);   /* mint priority-0 shell, "stamp priority later" */
```

The plan was for `ApplyEffectBlobToGObj`'s `memcpy(&ep->effect_vars, blob->effect_vars, …)` to stamp
priority 4 back. But the two same-id quakes drive heavy canonicalization churn
(`slot_effect_enforce` / `quake_surplus_prune` / `quake_unmatched_prune`), all keyed on
`syNetRbSnapLiveEffectMatchesBlob` which matches quakes **by priority**. A freshly minted priority-0
shell matches neither the `pri=4` nor the `pri=3` blob, so it gets pruned and re-minted as a bare
priority-0 shell without the blob ever being re-applied — stranding the `pri=4` shell at priority 0
at verify time.

## Fix

Reconstruct the priority through the **same vanilla formula**, sign-extending the magnitude byte so
priority 4 (magnitude `-1`) round-trips like any other quake:

```c
/* syNetRbSnapRespawnQuakeEffectForBlob */
return efManagerQuakeMakeEffect((s32)(s8)blob->quake_magnitude);
```

- magnitudes `0..3` → `efManagerQuakeMakeEffect(0..3)` → priority `3..0` (unchanged behavior).
- magnitude `0xFF` → `(s8)0xFF = -1` → `efManagerQuakeMakeEffect(-1)` → the `default` switch arm adds
  no quake anim joint (correct — the shell is not a real camera quake) and the trailing
  `priority = 3 - (-1) = 4` sets the right priority directly.

The respawned shell now matches its `pri=4` blob immediately, so `FindLiveQuakeEffectForBlob` /
`MatchesBlob` keep it as the canonical quake instead of pruning/re-minting a bare priority-0 shell.
The `effect_vars` memcpy in `ApplyEffectBlobToGObj` still restores the full union (priority 4,
consistent), and `RescheduleQuakeProcIfActive` still refuses to bind the camera proc for priority > 3
(see `netplay_quake_pri4_shell_camera_proc_misbind_2026-07-02.md`), so the shell stays camera-inert.

This eliminates the fragile "mint priority-0 then stamp" special case and the sentinel collision.

## Why this surfaced now

The previous change removed the `effect_count_transition_probe` synctest skip
(`netplay_effect_quake_priority_union_fold_2026-07-02.md`). That skip had been masking every
effect-count-transition tick (Firefox impact-wave spawn/despawn is exactly such a transition), so the
priority-4 round-trip gap only became a visible `SYNCTEST_FAIL` once full synctest coverage was
restored. This is the intended outcome of removing masking skips: expose the real save/load bug.

## Files

- `port/net/sys/netrollbacksnapshot.c` — `syNetRbSnapRespawnQuakeEffectForBlob` reconstructs priority
  via sign-extended magnitude (`efManagerQuakeMakeEffect((s32)(s8)blob->quake_magnitude)`).
