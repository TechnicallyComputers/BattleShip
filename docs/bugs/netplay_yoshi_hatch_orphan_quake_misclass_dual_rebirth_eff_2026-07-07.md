# Netplay: orphaned Yoshi hatch shell misclassified as quake during dual rebirth — eff FC drift

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)
**Date:** 2026-07-07

## Symptom

Cross-ISA soak session `87367049` (Android host / Linux guest, Samus + Yoshi, synctest OFF) ran stably
through dual rebirth until frame-commit validation at tick **7320**:

```text
FRAME_COMMIT_STATE_DIVERGE validation=7320 diverged=eff inputs=MATCH
local  eff=0x67B09225 | peer eff=0xDE2E9225
figh/world/item/rng matched on both peers
last_agreed=7200 mismatch=7201 resolved_load=7200
```

`eff_fold_diag` showed a single folded effect on recycled `gobj_id=1011`:

```text
tick=7236 (Android): count=1 respawn=1 bank=65535 parent_id=0 anim=0x3F800000
tick=7237 (Linux):   count=1 respawn=1 bank=65535 parent_id=0 anim=0x3F800000
```

Both peers stayed one `anim_frame` apart for ~84 ticks until FC at 7320 (`0x42A80000` vs `0x42A60000`).

## Root cause

1. **Stale hatch cosmetic shell** — Yoshi egg-lay hatch break shells are tagged with
   `SYNETRB_YOSHI_EGG_LAY_HATCH_COSMETIC_BANK_ID` (`0xFFFF`) and are intended to stay out of rollback
   hash/snapshot (see `netplay_yoshi_egg_lay_hatch_rollback_2026-06-05.md`).

2. **Dual rebirth recycled-id overlap** — during overlapping `RebirthWait` both fighters still report
   rebirth halos on `gobj_id=1011`, while a detached hatch shell can linger on the same recycled id with
   `ep->fighter_gobj == NULL`, world-root pose, and advancing `anim_frame`.

3. **Cosmetic hide predicate too strict** — `syNetRbSnapEffectHiddenFromRollback` only hid hatch shells
   when `proc_update == syNetRbSnapYoshiEggLayHatchCosmeticProcUpdate`. Orphaned shells that lost proc /
   parent coupling fell through.

4. **Stamped-quake heuristic false positive** — `syNetRbSnapLiveEffectIsQuake` treats
   `fighter_gobj==NULL && anim_frame>0 && quake.priority<=3` as a frozen quake. Orphan hatch shells alias
   `effect_vars.quake.priority == 0`, so `syNetRbSnapEffectRespawnKindFromLive` returned `respawn=1` and
   `syNetRbSnapLiveEffectExcludedFromRollbackHash` folded `anim_frame` into the authoritative eff hash.

5. **Cross-ISA one-tick inclusion gap** — Android entered the fold at tick **7236**, Linux at **7237**,
   producing a persistent off-by-one `anim_frame` drift with identical inputs (classic cosmetic timing skew).

## Fix

`port/net/sys/netrollbacksnapshot.c` (`PORT && SSB64_NETMENU`):

| Change | Purpose |
|--------|---------|
| `syNetRbSnapLiveEffectIsYoshiEggLayHatchCosmetic` matches **bank marker alone** | Orphan shells remain identifiable after proc/parent loss |
| `syNetRbSnapEffectHiddenFromRollback` hides any hatch-bank shell | Symmetric capture/verify exclusion |
| `syNetRbSnapLiveEffectExcludedFromRollbackHash` returns TRUE for hatch-bank shells | Prevent eff hash fold even if hidden check order changes |
| `syNetRbSnapLiveEffectIsQuake` stamped branch rejects hatch-bank shells | Stop respawn=1 misclassification in diag + respawn paths |
| `syNetRbSnapEffectRespawnKindFromLive` returns NONE for hatch-bank shells | Avoid quake respawn routing on orphans |

## Verify

- Re-run session `87367049` scenario (Samus/Yoshi dual rebirth soak, synctest OFF) past tick 7320; expect
  no `FRAME_COMMIT_STATE_DIVERGE` on `eff`.
- With `SSB64_NETPLAY_EFFECT_FOLD_DIAG=1`, ticks 7236–7320 should show `effect_count=0` / no
  `bank=65535` rows in eff fold during dual rebirth overlap.
- Regression: Yoshi egg escape hatch presentation still fires; synctest ON remains stable.

Related: [netplay_yoshi_egg_lay_hatch_rollback_2026-06-05.md](netplay_yoshi_egg_lay_hatch_rollback_2026-06-05.md),
[netplay_rebirth_halo_cosmetic_exclusion_2026-07-03.md](netplay_rebirth_halo_cosmetic_exclusion_2026-07-03.md),
[netplay_quake_cosmetic_rollback_exclusion_2026-07-02.md](netplay_quake_cosmetic_rollback_exclusion_2026-07-02.md).
