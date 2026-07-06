# Netplay Kirby JumpAerial synctest probe — 2026-06-09

**Status:** UN-SKIPPED (2026-07-04). Verify presentation repair shipped; soak pending.

## Symptom

Pikachu vs Kirby Dream Land synctest soak: `SYNCTEST_FAIL` @ tick 3564 on both peers. Kirby in `JumpAerialF2` (status 224, motion 199); Pikachu idle in Wait. Save `ring_save_diag` OK; load/verify drift on `figh` full hash + `anim` with `guard_shield_load_drift` on both fighters (`shield=55`).

Probe ran one tick before Kirby transitioned to `SpecialAirHi` (status 258 @3565).

## Root cause

Same probe-boundary class as `fox_firefox_probe`: live state had left the fragile window, but the historical slot tick was still Kirby `JumpAerialF1`…`JumpAerialF5`. Snapshot apply + verify finalize does not round-trip those aerial jump poses reliably (joint anim / residual shield presentation).

## Fix

`syNetRbSnapRefreshKirbyJumpAerialPresentationFromSlot()` re-pins joint figatree from the slot blob during verify prepare/resim (same family as Yoshi egg-lay). The `kirby_jump_aerial_probe` synctest skip was removed 2026-07-04 — see `netplay_link_guard_release_fc_drift_2026-07-04.md`.

Previously: `syNetRbSnapshotSynctestShouldSkipProbeTick()` — `reason=kirby_jump_aerial_probe` when any slot fighter blob is Kirby/NKirby in `nFTKirbyStatusJumpAerialF1` … `nFTKirbyStatusJumpAerialF5` (`syNetRbSnapBlobInKirbyJumpAerialSynctestFragileScope()`).

## Soak pass criteria

Pikachu/Kirby or Link/Kirby match with synctest enabled; trim ticks around triple-jump aerial (~749–759):

- `SYNCTEST_OK` on JumpAerial slot ticks (no `kirby_jump_aerial_probe` skip).
- No `LOAD_HASH_DRIFT` + `fighter_mismatch` soft-continue block on those probe ticks.
