# Netplay: Ness jibaku gate status-ID collision with Pikachu Quick Attack

**Date:** 2026-06-01  
**Status:** FIX SHIPPED (soak pending)  
**Matchup:** Pikachu vs Ness (cross-ISA netplay)

## Symptoms

- Spamming Pikachu UP+B (Thunder / Quick Attack zip) + direction changes felt like desync.
- Log showed `NESS_PKTHUNDER_GATE event=anim_length_restore player=0 status=236` on **Pikachu** (fkind=9).
- `LOAD_HASH_DRIFT` soft-continues during zip/fall windows; no `FRAME_COMMIT_STATE_DIVERGE`.

## Root cause

Character special status enums share numeric IDs across fighters. At `nFTCommonStatusSpecialStart + 16`:

| Fighter | Status ID | Meaning |
|---------|-----------|---------|
| Pikachu | 236 | `nFTPikachuStatusSpecialAirHi` (Quick Attack zip) |
| Ness    | 236 | `nFTNessStatusSpecialAirHiJibaku` |

Ness PK Thunder rollback helpers keyed only on `status_id`:

- `syNetplayNessSanitizePKJibakuStatusVars` — called on **every** fighter during ring save/apply
- `syNetplayQuantizeNessPKJibakuStatusVars` — same during blob quantize
- `syNetplayNessAnyLiveFighterInJibakuBurstScope` — triggered coupling canonicalize for all fighters

When Pikachu entered status 236, sanitize wrote `pkjibaku_anim_length = 28` into the `status_vars` union, corrupting `ftPikachuSpecialHiStatusVars` (stick range, pass timer, zip state).

## Fix

Require Ness `fkind` (`nFTKindNess` / `nFTKindNNess`) before any jibaku union touch or burst-scope scan:

- `syNetplayNessSanitizePKJibakuStatusVars`
- `syNetplayQuantizeNessPKJibakuStatusVars`
- `syNetplayCanonicalizeNessPKJibakuSimState`
- `syNetplayCanonicalizeNessPKJibakuLaunchState`
- `syNetplayNessAnyLiveFighterInJibakuBurstScope`
- `syNetplayNessFighterInPKJibakuCatchUpScope` / `syNetplayFighterInNessPKJibakuSimScope` — now take `FTStruct *` with fkind guard (not status ID alone)

## Follow-up hardening (same date)

Status IDs **232–237** collide across Pikachu Quick Attack and Ness PK Thunder air chain. Even with the fkind guard on Ness paths, rollback still needed Pikachu-side parity with Fox/Ness special gates:

- **`port/net/sys/netplay_pikachu_quickattack_gate.c/.h`** — synctest defer for QA chain + QA-tagged FallSpecial landing; sanitize/quantize status vars; catch-up startup→zip→end→fall; shock-FX scope.
- **`port/net/sys/netplay_sim_quantize.c`** — quantize `ftPikachuSpecialHiStatusVars`, QA FallSpecial drift/lag, zip joint pose; live canonicalize on ring save.
- **`port/net/sys/netrollbacksnapshot.c`** — ring save/apply hooks; proc rebind + catch-up through end/landing.
- **`port/net/sys/netrollback.c`** — effect-hash repair when sim core matches; **block soft-continue on fighter hash mismatch** (prevents tick-1188 silent fork).

## Verification

Soak: Pikachu vs Ness, spam UP+B Quick Attack + dash cancels both directions. Expect **zero** `anim_length_restore` for non-Ness fkind; `effect-repair ok` on eff-only drift; **`soft-continue blocked reason=fighter_mismatch`** instead of silent fork at QA end; no `FRAME_COMMIT_STATE_DIVERGE` with matching inputs.

## Arwing platform fc_recovery lockup (2026-06-01, ~5760-tick soak)

**Symptom:** Quick Attack spam from standing on the Sector Z Arwing. Frame-commit diverge at tick 5760 (figh-only, inputs/world/rng/eff agree) → fc_recovery load at 5640 aborted → `rollback_epoch_hold` lockup.

**Root cause:** `start_catchup` ran during snapshot **apply** (status 235→236) before load-hash verify. Live fighter no longer matched the ring blob (`vel_air_y`, `joint2_ty`, status/motion). Cross-ISA platform pass (`MAP_VERTEX_COLL_PASS`) compounded Y/velocity drift on elevated surfaces. Deferred weapon eject on failed verify left PK Thunder segments in a bad state during emergency restore.

**Fix:**

- Defer Pikachu QA catch-up during snapshot apply; run **`syNetplayPikachuCatchUpAllAfterLoadVerify`** only after load-hash verify accepts the load (`netrollbacksnapshot.c`, `netrollback.c`).
- Defer weapon eject until verify succeeds; cancel on abort/restore (`syNetRbSnapshotCommitDeferredWeaponEject` / `CancelDeferredWeaponEject`).
- **`syNetplayPikachuClampFcRecoveryLoadTick`** — bump fc_recovery load forward when ring slot has QA catch-up pending (when span allows).
- Extend synctest defer + landing quantize to status **59** (`LandingFallSpecial`) with QA drift/lag signature.
- Platform QA canonicalize: extra physics/coll/top-joint quantize when `MAP_VERTEX_COLL_PASS` during QA scope (`netplay_sim_quantize.c`).

**Verify:** Repeat Arwing soak; fc_recovery reanchor at QA boundary should pass verify (or clamp load) instead of `fc_recovery figh reject` + epoch hold.
