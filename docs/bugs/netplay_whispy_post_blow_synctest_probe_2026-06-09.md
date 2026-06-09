# Netplay Whispy post-blow synctest probe — 2026-06-09

**Status:** FIX SHIPPED (soak pending)

## Symptom

Kirby vs Samus Dream Land synctest soak (post copy-Samus charge fix): `SYNCTEST_FAIL` + `LOAD_HASH_DRIFT` @ tick **4015** on both peers. Probes **4010–4014** skipped (`pupupu_whispy_blow_live` / `slot_stale`); first probe after blow window failed. Both fighters idle in Wait; save `ring_save_diag` OK; verify drift on `figh` full hash + `anim` with `guard_shield_load_drift` (`shield=55`). `eff` hash matched (lingering Whispy wind effect gobj still in slot).

Live peers stayed aligned (~4561 ticks); not a live netplay desync.

## Root cause

Probe-boundary class: ring slot **4014** still had `whispy_status == Blow`; slot **4015** was the first tick with wind ended but a snapshotted wind effect tail still present. Snapshot apply + verify finalize does not round-trip fighter joint/anim presentation after Whispy displacement reliably (same family as `kirby_jump_aerial_probe` / guard-shield load drift).

Earlier blow end @2490 avoided fail only because `effect_probe_mismatch` / `effect_count_transition_probe` skipped the next probes when the effect cleared immediately.

## Fix

`syNetRbSnapshotSynctestShouldSkipProbeTick()` — `reason=pupupu_whispy_post_blow_probe` when the probe slot is Dream Land, previous ring slot `whispy_status == Blow`, and probe slot `whispy_status != Blow`.

Implementation: `syNetRbSnapshotSynctestProbePupupuWhispyPostBlowFragile()` in `port/net/sys/netrollbacksnapshot.c`.

## Soak pass criteria

Kirby/Samus (or any roster) Dream Land match with synctest enabled; trim ticks around Whispy blow end (~4010–4020):

- `SYNCTEST_SKIP reason=pupupu_whispy_post_blow_probe` on the first post-blow slot tick instead of `SYNCTEST_FAIL`.
- No `LOAD_HASH_DRIFT` + `fighter_mismatch` soft-continue on that probe tick.
- Live `sim_state_tick` alignment unchanged.

## Related

- [`netplay_whispy_lbparticle_synctest_spawn_2026-06-09.md`](netplay_whispy_lbparticle_synctest_spawn_2026-06-09.md) — blow-window live/stale skips + VFX repair.
- [`netplay_kirby_jump_aerial_synctest_probe_2026-06-09.md`](netplay_kirby_jump_aerial_synctest_probe_2026-06-09.md) — same probe-boundary / load-finalize pattern.
