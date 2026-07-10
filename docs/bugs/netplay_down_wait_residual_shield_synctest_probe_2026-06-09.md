# Netplay down-wait residual shield synctest probe — 2026-06-09

**Status:** FIX SHIPPED (soak pending). **2026-06-11 resim follow-up:** `down_wait_residual_shield_probe` removed from resim-load fragile walkback; residual-shield joint repair on load/forward-resim (see `netplay_link_bomb_resim_load_fail_2026-06-11.md`).

## Symptom

Falcon vs Pikachu Peach's Castle synctest soak (cross-ISA Android/Linux): `SYNCTEST_FAIL` @ tick **4650** on both peers. Pikachu in `DownWaitD` (status 69); Falcon in `KneeBend` (status 20). Save `ring_save_diag` OK; load/verify drift on `figh` full hash + `anim` with `guard_shield_load_drift` on both fighters (`shield=55`, `is_shield=0`). Live forward sim stayed aligned through tick 4934+.

Anim hash jump @4647 marks knockdown transition into the probed window.

## Root cause

Same probe-boundary class as `kirby_jump_aerial_probe` / `pikachu_quickattack_probe`: snapshot apply + verify finalize does not round-trip down-bounce/wait joint poses when shield stamina remains from prior shielding but the bubble is not active (`shield_health > 0`, `is_shield=0`).

## Fix

`syNetRbSnapshotSynctestShouldSkipProbeTick()` — `reason=down_wait_residual_shield_probe` when any slot fighter blob is in `nFTCommonStatusDownBounceD` … `nFTCommonStatusDownWaitU` with residual shield stamina.

Implementation: `syNetRbSnapBlobInDownWaitResidualShieldSynctestFragileScope()` in `port/net/sys/netrollbacksnapshot.c`.

## Soak pass criteria

Falcon/Pikachu Peach's Castle match with synctest enabled; trim ticks around knockdown + tech chase (~4640–4660):

- `SYNCTEST_SKIP reason=down_wait_residual_shield_probe` instead of `SYNCTEST_FAIL` @4650.
- No `LOAD_HASH_DRIFT` + `fighter_mismatch` soft-continue block on that probe tick.
- Live `sim_state_tick` hashes remain matched (already true in failing soak).
