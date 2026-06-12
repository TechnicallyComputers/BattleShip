# Netplay FC recovery weapon load drift (2026-06-11)

## Symptom

Soak3 Fox vs Yoshi episode 2 (@600): both peers queued `fc_recovery` resim load @480 → target @601.

- **Android:** full 120-tick resim as `local_initiator`, `resim complete epoch=2`.
- **Linux:** repeated `LOAD_HASH_DRIFT tick=480` with **wpn-only** mismatch (live empty vs ring
  has weapons), walkback 480→474, `BATTLE_SIM_HOLD reason=resim_load_fail`, never reached
  `resim begin`.

Episode 1 had already poisoned Linux ring saves with extra weapons (@534+) while Android stayed
empty despite matching world/rng/inputs.

## Root cause

1. **Forward sim weapon asymmetry (episode 1 tail):** post-resim-complete gameplay figatree
   re-pin (`RefreshGameplayAnimFragilePresentationFromSlot` + `RefreshPresentationForLoadedTick`)
   ran after verify matched, mutating figh/anim differently on Linux vs Android (unhalfswap
   outcomes). Linux-only weapon spawn @534 while world/rng still matched.
2. **FC recovery load verify:** `syNetRollbackLoadHashDriftIsResimSimCoreOk` required wpn match;
   no weapon repair path (unlike effect repair). Stale ring weapon blobs blocked Linux recovery
   while peer proceeded.

## Fix

- **`netrollbacksnapshot.c`**
  - `syNetRbSnapshotTryRepairWeaponHashForVerify` — re-apply weapon blobs + finalize coupling +
    commit deferred eject before verify.
  - `RefreshIntroPresentationAfterResimComplete` — gameplay path: part-transform rebuild only;
    drop post-verify figatree re-pin (forward resim PreSim + first-tick repair remain).
- **`netrollback.c`**
  - `syNetRollbackTryRecoverWeaponHashDrift` — log + continue on repair success.
  - `syNetRollbackFcRecoveryWpnOnlyDriftOk` — FC recovery resim load when world/item/map/rng/figh
    agree but ring retains stale weapon blobs from poisoned local history.

## Verification

Re-soak Fox vs Yoshi with inject @520. Expect:

- No Linux-only wpn divergence @534+ (figh/anim should track Android while world matches).
- Episode 2 FC recovery: Linux reaches `resim begin` / `resim complete epoch=2` (no
  `BATTLE_SIM_HOLD resim_load_fail`).
- Optional log: `LOAD_HASH_DRIFT weapon-repair ok` or `fc_recovery wpn-only ok`.
