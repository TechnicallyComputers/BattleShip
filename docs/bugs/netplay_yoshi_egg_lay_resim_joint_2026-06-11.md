# Netplay Yoshi egg-lay resim joint presentation (2026-06-11)

## Symptom

Soak1 (Fox vs Yoshi, `FORCE_MISMATCH` @520): resim completed with **identical** post digests on
Linux and Android (`figh post=0x95FAEAC4`, `anim post=0xEE2FD174` @522), but Yoshi leg joints
were visibly wrong after load — symmetric presentation bug, not desync.

## Root cause

1. `yoshi_egg_lay_attack_probe` marked every tick inside the egg-lay attack window as fragile,
   forcing resim-load walkback (519 → 503 → 487). Every candidate was still inside the attack
   window (Yoshi status 231 / `SpecialAirN`, motion 206), so walkback only lengthened replay
   without escaping the poisoned scope.
2. Snapshot load re-pinned EVENT16 figatree leg AObj mid-attack; unhalfswap logged thousands of
   `phase1_invalid` lines on leg streams (`0xfff26800`, `0xffec6800`, …) even with `force=1`.
3. Aggregate hashes still matched (sim-critical fold fields OK); only visible joint transforms were
   wrong. Existing gameplay fragile repair lacked modelpart cosmetic replay + terminal fold hard-pin
   for the egg-lay PRESERVE_MODELPART status family.

## Fix

- **`netrollbacksnapshot.c`**:
  - Remove `yoshi_egg_lay_attack_probe` from `syNetRbSnapshotSynctestShouldSkipProbeTick` — stop
    futile deep walkback; live synctest defer via `syNetRbSnapshotAnyYoshiEggLayAttackerScopeActive`
    unchanged.
  - `syNetRbSnapRefreshYoshiEggLayAttackPresentationFromSlot` — modelpart cosmetic + figatree
    refresh + PreSim unhalfswap + joint re-apply + fold hard-pin + part-transform invalidate.
  - `PrepareLoadedSlotForVerify` — call egg-lay repair when load slot is in attack scope.
  - Forward resim — per-tick egg-lay repair while live attacker remains in attack scope.
  - `ResyncLiveFightersFromSlotForSim` — egg-lay repair instead of intro fidelity on anchor probe.
  - `syNetRbSnapRefreshGameplayAnimFragilePresentationFromSlot` — add PreSim unhalfswap + fold
    hard-pin (aligns with anchor-probe gameplay reconcile).

## Verification

Re-soak Fox vs Yoshi with inject @520. Expect:

- `RESIM_LOAD_ANCHOR_ADJUST` **without** `yoshi_egg_lay_attack_probe` deep walkback (load @519)
- Fewer `phase1_invalid` leg streams at load apply (`SSB64_AOBJ_UNHALFSWAP_DIAG=1`)
- Stable Yoshi leg joints after resim @522 with matching figh/anim digests on both peers
