# Netplay: rollback snapshot audit hardening (Samus / anim hash / cliff / camera)

**Date:** 2026-07-10  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)

## Context

Follow-up to the `netrollbacksnapshot.c` audit after fireball wpn + CliffWait camera fixes. High-ROI
gaps that share the same failure classes as already-landed bugs.

## Fixes

### 1. Samus charge-shot orphan cull (fireball class)

Fireball prepare_verify cull was moved out of the presentation `else if` chain; Samus charge release
was still behind that chain and missing from `TryRepairWeaponHashForVerify`.

- Independent `if` for `syNetRbSnapCullSamusChargeShotsForSlotVerify` in prepare_verify.
- Empty-slot + non-empty paths in `syNetRbSnapshotTryRepairWeaponHashForVerify` mirror fireball.

### 2. Re-fold `hash_animation` after fighter recapture

`hash_fighter` was re-folded after `syNetRbSnapRecaptureLiveFightersIntoSlot`; `hash_animation` was
still the mid-fill value. Item/weapon/map tail passes can mutate joints after the first anim fold.

- Re-fold `slot->hash_animation = syNetSyncHashFighterAnimationStateForRollback()` with
  `hash_fighter` at the committed fighter-blob instant.

### 3. Cliff statuses in gameplay resim anim-fragile scope

`syNetRbSnapStatusInGameplayResimAnimFragileScope` covered locomotion / damage / tech-chase /
specials but not `CliffCatch`…`CliffEscapeSlow2`. Resim hard-pin / fragile presentation paths
skipped ledge statuses (related window: CliffWait camera yank soak).

- Range check `nFTCommonStatusCliffCatch`…`nFTCommonStatusCliffEscapeSlow2` → fragile.

### 4. Camera `func_camera` rebind on apply

`syNetRbSnapApplyCamera` memcpy'd `GMCamera` including raw `func_camera`. Stale process pointers
across ASLR / rebuilds can call the wrong camera mode after load.

- After blob assign + `vel_at` zero: `gmCameraSetStatus(status_curr)` then restore `status_prev`
  (SetStatus overwrites prev). Bounds-check `nGMCameraStatusDefault`…`nGMCameraStatusZebes`.

Companion scrub fix: [netplay_twister_tarucann_pikachu_scrub](netplay_twister_tarucann_pikachu_scrub_2026-07-10.md).

## Test plan

- [ ] Samus charge release near synctest / empty weapon slot; no orphan charge-shot `wpn` drift.
- [ ] Yoshi egg-lay / joint-mutating windows: anim partition matches after load verify.
- [ ] CliffWait / CliffClimb through FORCE_MISMATCH resim; presentation hard-pin applies.
- [ ] Camera load diag: `status_curr` drives correct mode after apply (`SSB64_NETPLAY_CAMERA_LOAD_DIAG=1`).
