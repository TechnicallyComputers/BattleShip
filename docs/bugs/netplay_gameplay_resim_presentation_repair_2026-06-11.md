# Netplay gameplay resim presentation repair (2026-06-11)

## Symptom

Soak3 (Fox vs DK, `FORCE_MISMATCH` @520): rollback resim completed with matching
digests, but DK's right leg spun in a circle after load while in **SpecialAirNStart**
(status 223) — presentation-only, not sim desync.

Earlier soak (Mario vs Luigi @520 SpecialN fireball) had the same post-resim class:
joint/figatree corruption after intro when terminal repair only handled countdown Wait.

## Root cause

Same EVENT32/EVENT16 figatree family as intro Appear (`netplay_dk_link_intro_resim_presentation_2026-06-11.md`):

1. Snapshot load re-pins joint `event32` pointers; many alias EVENT16 figatree leg streams
   (`phase1_invalid` on `0xfff26800` / `0xffec6800` during load @519).
2. Intro PreSim unhalfswap (`syNetRbSnapshotPreSimUnhalfswapIntroAppearAnim`) bails post-GO.
3. Gameplay fragile scope (`Pass`/`Squat`/locomotion only) excluded character special-table
   statuses like DK `SpecialAirNStart` (223) and knockdown tech-chase (`DownBounceD`..`Passive`, soak1
   Donkey `DownBounceU` @520 rotated 90° on ground during bomb-hit resim).
4. Post-resim terminal repair called `RefreshPresentationForLoadedTick` but skipped figatree
   re-pin + transform rebuild for gameplay fragile fighters.

## Fix

- **`netrollbacksnapshot.c`**:
  - `syNetRbSnapStatusInGameplayResimAnimFragileScope` — locomotion statuses + knockdown tech-chase
    (`DownBounceD`..`Passive`) + character special-table statuses (`>= nFTCommonStatusSpecialStart`),
    excluding Appear presentation scope.
  - `syNetRbSnapshotPreSimUnhalfswapGameplayResimAnim` — post-GO resim: evict EVENT32 cache +
    unhalfswap joint + figatree-walked DObj streams for fragile fighters each sim tick.
  - `syNetRbSnapRefreshGameplayAnimFragilePresentationFromSlot` — slot re-pin + figatree refresh +
    joint re-apply + transform invalidate (mirrors intro Appear cosmetic path).
  - `ApplyFighterJointPoseAndAnimFromBlob` — evict + figatree DObj unhalfswap on every load apply.
  - Forward-resim first tick + resim-complete — call gameplay fragile slot repair.
- **`netrollback.c`**: `syNetRollbackTryRecoverWeaponHashDrift`, `syNetRollbackFcRecoveryWpnOnlyDriftOk`.
- Post-resim-complete gameplay path: part-transform rebuild only (no second figatree re-pin).
  See `netplay_fc_recovery_weapon_drift_2026-06-11.md`.
  - Anchor-probe gameplay reconcile — full PreSim unhalfswap pass after joint re-pin.
- **`scvsbattle.c`**: call gameplay PreSim hook alongside intro PreSim during resim.
- **`netrollbacksnapshot.h`**: export `syNetRbSnapshotPreSimUnhalfswapGameplayResimAnim`.

## Verification

Re-soak Fox vs DK with inject @520. Expect:

- `resim complete` load_tick=519 mismatch_tick=520
- No DK right-leg spin after resim; stable `fhash_light` while in SpecialAirNStart
- `anim` post-resim drift reduced vs pre-fix (`0x8DFB0601 → 0x4F302C73` class)
- Intro forward sim unchanged (gameplay hook no-ops during countdown Wait)

Env: existing soak3 config; optional `SSB64_AOBJ_UNHALFSWAP_DIAG=1` to confirm fewer
`phase1_invalid` leg streams after load repair.
