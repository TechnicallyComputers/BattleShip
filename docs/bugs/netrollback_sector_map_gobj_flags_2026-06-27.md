# Sector Z map GObj visibility flag breaks rollback map-hash round-trip

**Date:** 2026-06-27
**Scope:** `port/net/sys/netrollbacksnapshot.c` (`SSB64_NETMENU`)
**Status:** FIX IMPLEMENTED (soak pending)

## Symptoms

Cross-ISA soak (Android host ↔ Linux guest) on **Sector Z** (`stage=1`, gkind=1)
ends with a fatal, deterministic `LOAD_HASH_DRIFT` at the **first tick after the
intro** (`scripts/netplay-scan-drift.py` → `RESULT: FAIL`):

```
LOAD_HASH_DRIFT tick=389 figh=…/= world=…/= item=…/= wpn=…/=
  map=0x4F980D06/0x002FCC29  rng=…/=  cam=0x2A104EB7/0x3D4144B6  anim=…/= eff=…/=
```

Both peers produce **bit-identical** `snap` and `live` hashes — this is not a
peer-to-peer desync but an internal snapshot save→apply→rehash round-trip hole.
Only `map` and `cam` diverge; `map` is the fatal one (`cam` alone would be
tolerated as presentational). After the drift the engine takes
`syNetRbSnapshotRestoreLiveEmergency` and the game thread wedges (`WATCHDOG HANG`).

The whole intro was never verified: synctest skips every tick while
`game_status == nSCBattleGameStatusWait` (`SYNCTEST_SKIP reason=intro_wait`,
330×), so the latent hole only surfaced at the first non-skipped tick (the GO
boundary, 389).

## Root cause

The map diag isolates the drift to the **ground fold**, not kinematics:

```
map_hash_drift tick=389 kin=0xDA2DE1D1
  ground_fold_slot=0x5F52D426  ground_fold_scratch=0x28582E57
map_hash_ground_payload tick=389 gkind=1 slot_len=40 scratch_len=40 match=0 first_off=28
```

Offset 28 of `SYNetRbSnapGroundSector` is `map_gobj_flags` (the Arwing map GObj's
`flags`, i.e. `GOBJ_FLAG_HIDDEN` / `GOBJ_FLAG_NONE`). It has **dual authority**:

| Path | Behavior |
|------|----------|
| Ground capture (`syNetRbSnapCaptureGround`, Sector case) | Stores the **raw** live `map_gobj->flags`, folded into the map hash |
| Apply (`syNetRbSnapApplyGround` Sector case) | Never writes the raw byte back |
| Live restore (`grSectorSyncArwingMapGObjFlags`, called from the Arwing apply) | **Re-derives** `map_gobj->flags` from `arwing_status` / `arwing_appear_timer` / root DObj `anim_wait` |

So the hash folds a **raw** flag while apply produces a **derived** flag. During
the intro the flag is static, so the save-time self-test passes and the field
round-trips trivially. At GO the Arwing status/timers change, the derivation
yields a different visibility flag than the raw captured byte, and because `map`
is sim-critical and **never soft-continues** (`netrollback.c`
`syNetRollbackLoadHashDriftTrySoftContinue` blocks on `map_mismatch`), the drift
is fatal.

The derived flag is a pure function of inputs (`arwing_status`,
`arwing_appear_timer`, root `anim_wait`) that are **already folded into the map
hash**, so the raw `map_gobj_flags` byte contributes no desync-detection value —
it only breaks the round-trip.

## Fix

Exclude `map_gobj_flags` from the rollback map-hash fold in
`syNetRbSnapFoldGroundPayloadHashForRollback` (Sector case) by zeroing it in the
fold scratch copy — exactly the precedent already used for Yoster's `map_head`
(`yo->map_head = 0;` "live pointer for restore only — exclude from cross-peer
digest"). Snapshot blob bytes stay raw; only the hash view drops the field.

```c
case nGRKindSector:
    if (n >= (u32)sizeof(SYNetRbSnapGroundSector)) {
        SYNetRbSnapGroundSector *sec = (SYNetRbSnapGroundSector *)payload_scratch;
        sec->arwing_target_x = syNetplayQuantizeF32ForRollbackHash(sec->arwing_target_x);
        sec->map_gobj_flags = 0U; /* presentational; derived on load, redundant in hash */
    }
    break;
```

With `map` round-tripping, tick 389 reduces to a `cam`-only difference (the
by-design intro camera re-integration), which the `presentational-only` path in
`netrollback.c` already tolerates — no emergency restore, no hang.

## Why excluding (not restoring) is correct

`map_gobj->flags` is a GObj **visibility** flag (controls whether the Arwing map
is drawn); nothing in the Sector sim/collision branches on it (collision uses
`is_arwing_line_active` etc.). It is deliberately reconstructed on load by
`grSectorSyncArwingMapGObjFlags` (added in
`netplay_sector_arwing_rollback_2026-05-30.md`) because the map GObj/DObj tree is
rebuilt during rollback. Restoring the raw byte would fight that derivation;
folding a derived presentational flag into the never-soft-continue `map`
partition was the design smell.

## Follow-ups (not in this fix)

- The `intro_wait` synctest skip is a total coverage blind spot for `map`/`cam`
  during the whole countdown; consider probing at least those folds during Wait
  or deliberately at the first post-Wait tick.
- `syNetRbSnapshotRestoreLiveEmergency` wedging the game thread (watchdog hang
  instead of a clean fail) is a separate bug — diagnosed and fixed in
  [netrollback_emergency_restore_sparkle_window_overflow](netrollback_emergency_restore_sparkle_window_overflow_2026-06-27.md)
  (the emergency sentinel's `tick == 0xFFFFFFFF` flowed into a Link-bomb
  sparkle-window `<= probe_tick` loop that never terminates on u32 wraparound).

## Test plan

1. Sector Z cross-ISA soak (Android ↔ Linux): `netplay-scan-drift.py` → no
   `map=` divergence at the GO boundary; `RESULT` no longer `FAIL` on this cause.
2. Confirm any remaining tick-389 drift is `cam`-only and resolves
   `presentational-only` (no `RestoreLiveEmergency` / watchdog hang).
3. Regression: other stages (Hyrule twister, Kongo barrel, Yoshi clouds)
   unchanged; Sector deck-jump / Arwing patrol FC recovery unaffected.
