# DeadExplode stamped-quake misclass + thunder trail RNG — FRAME_COMMIT eff,rng — 2026-07-11

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)  
**Scope:** `port/net/sys/netrollbacksnapshot.c`, `decomp/src/wp/wppikachu/wppikachuthunder.c`, `decomp/src/wp/wpness/wpnesspkthunder.c`

## Symptom

soak2 session `537679573` / seed `1401518185` (Android host role=client / Linux guest role=host, Dream Land, Ness vs Pikachu):

```
FRAME_COMMIT_STATE_DIVERGE validation=6240
  figh+world+item MATCH, inputs MATCH
  diverged=eff,rng
  Linux  folded eff count=1; Android count=2 (same recycled gobj_id=1011)
FRAME_COMMIT_INPUT_AGREE_REANCHOR last_agreed=6120 mismatch=6121
```

Synctest: 49 OK / 0 FAIL (local rollback hash stable). Drift scan: genuine cross-ISA determinism failure at tick 6240.

Folded shells at blast-zone coords (`pos.x ≈ ±8500`) with `respawn=1` (QUAKE), `HaveStruct`-class lifetime, `fighter_gobj==NULL` — KO DeadExplode presentation, not Whispy / camera quakes.

## Root cause (eff)

`syNetRbSnapLiveEffectIsQuake` stamped/frozen heuristic:

```
fighter_gobj == NULL && anim_frame > 0 && quake.priority <= 3 && func_run == NULL
```

matches **DeadExplode** shells (`efManagerHaveStructProcUpdate`) via incidental `effect_vars.quake` union bytes. Live `efManagerQuakeProcUpdate` quakes are already `HiddenFromRollback`, but `syNetRbSnapLiveEffectExcludedFromRollbackHash` previously kept heuristic “quakes” in the fold while `anim_frame > 0`:

```c
if (IsQuake(...))
    return (anim_frame <= 0.0F) ? TRUE : FALSE;
```

Cross-ISA VFX lifetime then forked fold count / anim on recycled `gobj_id=1011` without touching fighter state — same class as Yoshi hatch stamped-quake misclass and ImpactWave exclusion.

## Root cause (rng)

Pikachu Thunder / Ness PK Thunder trail procs burn gameplay `syUtilsRandIntRange` for `texture_id_curr` flicker. Weapon hash does not fold `texture_id`. Asymmetric trail spawn/lifetime therefore forks `rng` while `figh`/`wpn` stay matched — ForcedCosmetic class (`netplay_effect_vfx_forced_cosmetic_rng_2026-07-09.md`).

## Fix

| Change | Purpose |
|--------|---------|
| Reject `efManagerHaveStructProcUpdate` in stamped-quake heuristic | DeadExplode never classified as `respawn=QUAKE` |
| Always exclude `IsQuake` from rollback hash (drop `anim_frame` gate) | Close leftover hole for any remaining heuristic false positives |
| `HiddenFromRollback`: orphan HaveStruct (no fighter parent) | Snapshot/verify skip KO explode shells |
| Thunder trail `syUtilsRandIntRange` → `ForcedCosmetic` under NETMENU | Texture flicker off gameplay LCG |

## Verification

- Re-soak same profile (Dream Land, Ness/Pikachu, long session through KO window ~6240): no `FRAME_COMMIT` `diverged=eff,rng` with matched inputs.
- Fold diag around KO: DeadExplode shells must not appear as `respawn=1` in `eff` capture.
- Optional: `SSB64_NETPLAY_RNG_STEP_TRACE` during Thunder — trail texture draws must not appear as gameplay `rng_step`.

## Related

- `netplay_quake_cosmetic_rollback_exclusion_2026-07-02.md`
- `netplay_effect_vfx_forced_cosmetic_rng_2026-07-09.md`
- `netplay_ftparam_effect_scatter_rng_fc_diverge_2026-07-10.md`
