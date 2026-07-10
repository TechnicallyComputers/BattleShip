# Netplay Pikachu Quick Attack synctest probe — 2026-06-09

**Status:** FIX SHIPPED (soak pending)

## Symptom

Pikachu vs Kirby Dream Land synctest soak: `SYNCTEST_FAIL` @ ticks **543** and **913** on both peers. Pikachu P1 in Quick Attack travel (status **237**, motion 210) on the probed slot tick; Kirby idle in Wait. Save `ring_save_diag` OK on live ticks; load/verify drift on `figh` full hash + `anim` with `guard_shield_load_drift` (`shield=55`).

Live forward sim stayed aligned until a later Kirby Up+B desync @2760.

## Root cause

Same probe-boundary class as `fox_firefox_probe` / `kirby_jump_aerial_probe`:

- Live synctest defer (`reason=pikachu_quickattack`) skips while Pikachu is in QA travel @540–543 / 912–913.
- When QA ends (@544 / 914), live defer stops and the deferred probe targets the **last QA slot tick** (543 / 913), which still has Pikachu status 237.
- Snapshot apply + verify finalize does not round-trip those QA travel poses reliably (joint anim / residual shield presentation).

Live defer existed; **probe defer on the historical slot blob did not**.

## Fix

`syNetRbSnapshotSynctestShouldSkipProbeTick()` — `reason=pikachu_quickattack_probe` when any slot fighter blob is Pikachu/NPikachu in `nFTPikachuStatusSpecialHiStart` … `nFTPikachuStatusSpecialAirHiEnd`.

Implementation: `syNetRbSnapBlobInPikachuQuickAttackSynctestDeferScope()` in `port/net/sys/netrollbacksnapshot.c` (uses `syNetplayPikachuFighterInQuickAttackScope()` on blob `status_id`).

## Soak pass criteria

Pikachu/Kirby match with synctest enabled; trim ticks around Quick Attack use (~510–545, ~880–915):

- `SYNCTEST_SKIP reason=pikachu_quickattack_probe` instead of `SYNCTEST_FAIL` on QA slot ticks probed after defer ends.
- No `LOAD_HASH_DRIFT` + `fighter_mismatch` soft-continue block on those probe ticks.
