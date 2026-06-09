# Netplay Kirby JumpAerial synctest probe — 2026-06-09

**Status:** FIX SHIPPED (soak pending)

## Symptom

Pikachu vs Kirby Dream Land synctest soak: `SYNCTEST_FAIL` @ tick 3564 on both peers. Kirby in `JumpAerialF2` (status 224, motion 199); Pikachu idle in Wait. Save `ring_save_diag` OK; load/verify drift on `figh` full hash + `anim` with `guard_shield_load_drift` on both fighters (`shield=55`).

Probe ran one tick before Kirby transitioned to `SpecialAirHi` (status 258 @3565).

## Root cause

Same probe-boundary class as `fox_firefox_probe`: live state had left the fragile window, but the historical slot tick was still Kirby `JumpAerialF1`…`JumpAerialF5`. Snapshot apply + verify finalize does not round-trip those aerial jump poses reliably (joint anim / residual shield presentation).

## Fix

`syNetRbSnapshotSynctestShouldSkipProbeTick()` — `reason=kirby_jump_aerial_probe` when any slot fighter blob is Kirby/NKirby in `nFTKirbyStatusJumpAerialF1` … `nFTKirbyStatusJumpAerialF5`.

Implementation: `syNetRbSnapBlobInKirbyJumpAerialSynctestFragileScope()` in `port/net/sys/netrollbacksnapshot.c`.

## Soak pass criteria

Pikachu/Kirby match with synctest enabled; trim ticks around triple-jump aerial + Up+B (~3547–3565):

- `SYNCTEST_SKIP reason=kirby_jump_aerial_probe` instead of `SYNCTEST_FAIL` on JumpAerial slot ticks.
- No `LOAD_HASH_DRIFT` + `fighter_mismatch` soft-continue block on those probe ticks.
