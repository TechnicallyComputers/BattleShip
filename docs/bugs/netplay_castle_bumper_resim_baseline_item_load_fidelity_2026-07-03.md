# netplay: Castle bumper resim-baseline item load non-idempotency freezes the session (VS_SESSION_END)

**Status:** DIAGNOSTIC + ROBUSTNESS FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending). Root-cause item field still to be pinned from the new diagnostic on next repro.

## Symptom

Soak `1357964800` (Fox Firefox + Peach's Castle bumper). The determinism drift scan reports `RESULT: PASS` (forward `sim_state` hashes agree tick-by-tick, `LOAD_HASH_DRIFT=0`, `SYNCTEST_FAIL=0`), yet the user observed a **hang followed by a desync when starting Firefox after a failed resim**, and the session ended early at `max_sim_tick=523`.

The `RESULT: PASS` is misleading: the failure is entirely in the **rollback load / resim-baseline** path, which the forward-sim drift scanner does not cover.

## Root sequence

1. `SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH=1` is set **on the host/android only** (`soak2-android.log:62`) — a stress harness that forces a mismatch at wire tick 520 every session to exercise the resim path.
2. It triggers a symmetric resim `mismatch=520 target=522 load_tick=519`. Both peers load snapshot slot 519.
3. Both peers apply a **byte-identical** bumper state — the `gbumper_apply_probe` lines are equal on both peers (`tx=0xC464C15E ty=0x455C5000 sx=0x3F800000 pal=0 multi=0 hit_anim_length=65488 atk_state=3`; host `soak2-android.log:7339`, guest `soak2-linux.log:9797`).
4. The post-finalize item hash diverges anyway:
   - Host: `live item=0x8EAC9B20` == slot `0x8EAC9B20` (round-trips, `soak2-android.log:7359`).
   - Guest: `live item=0x0DC8675A` != slot `0x8EAC9B20` (`soak2-linux.log:9812`).
   - `0x0DC8675A` appears **exactly once** in the whole guest log — it never occurs in forward sim. It is the bumper's *resting* fold, a load-only artifact.
5. `figh`/`world`/`rng`/`anim`/`weapon`/`map` all match — this is **item-only** self load-fidelity drift on the guest: the guest cannot reproduce its own saved item hash.
6. The resim-baseline gate treats `item` as a hard gameplay subsystem (`syNetRollbackPeerBaselineWireGameplayMatchArmed` / `...SlotGameplaySubsystemOk`, `netrollback.c`). Only *camera* had a cosmetic downgrade. So the guest's gate never opens → the guest never seals its episode rows.
7. The host (`baseline_matched=1 seal_rows_missing=0x1`) waits for the guest's seal rows, times out 3× (`RESIM_BASELINE_TIMEOUT streak=1/2/3`, `soak2-android.log:7548/7652/7755`), then hits `RESIM_BASELINE_TIMEOUT streak — hard desync recovery` and sends `VS_SESSION_END` (received by the guest at `soak2-linux.log:9968`). The guest meanwhile spins in `rollback_epoch_hold` / `sym_reject_cap` / `load_fail_hold`. Net effect: hang → session death.

## Two distinct defects

**A. Root determinism bug (guest-only item load non-idempotency).** Since the item apply probe is byte-identical across peers, the divergent field is one the item fold reads (`syNetSyncFoldActiveItemGobjForRollback` / `syNetSyncFoldGBumperItemExtras`, `netsync.c`) but the apply probe does not print — a bumper field re-derived during load-finalize (candidates: `dobj->scale.x`/AObj presentation, `mobj->palette_id`, or `attack_records[].victim_gobj` resolution). The exact field could not be pinned from these logs because the per-component item-fold diagnostic was not enabled during capture.

**B. Robustness bug.** An item-only baseline block had no bounded recovery — it froze into `rollback_epoch_hold` and ended in `VS_SESSION_END` (host) / an endless hold (guest). Even after (A) is fixed, any future item load-fidelity gap would kill the session the same way.

## Fix (this change)

Both `PORT && SSB64_NETMENU`, in `port/net/sys/netrollback.c`:

1. **Diagnostic at the exact failure point.** `syNetRollbackLogResimBaselinePostLoad` now, when `live.item != slot.item`, emits `syNetSyncLogItemHashDriftDiag` + `syNetSyncLogItemFieldDiffDiag` with reason `resim_baseline_item` (self-gated by `SSB64_NETPLAY_ITEM_HASH_FIELD_DIFF=1`). A repro with that env now dumps the per-field bumper fold (`item_field_diff` / `item_fold_floats`, raw + quantized bits) at the load-fidelity failure, so the non-idempotent field can be pinned and then made to round-trip in the snapshot.

2. **Bounded recovery instead of hard desync.** When the item-only self load-fidelity drift is item-only (figh/world/rng/weapon/map agree), the load_tick is recorded in `sSYNetRollbackBaselineItemOnlySelfDriftLoadTick`. In `syNetRollbackOnBaselineGateTimeout`, before the terminal `VS_SESSION_END`, when the block is recoverable (this peer's baseline digest matched — it is stuck sealing — **or** it hit item-only self drift at this load_tick), the handler backs off to an earlier load-safe slot via `syNetRollbackTryRestartResimAtDeeperLoad(load_tick - 1)`. A single non-idempotent snapshot poisons baseline agreement at that one load_tick; an earlier slot predates the collision and reproduces cleanly, so a deeper resim converges over the poisoned tick. Bounded by `SYNETROLLBACK_BASELINE_DEEPER_MAX_ATTEMPTS` and the ring; if no clean earlier slot exists it falls through to the normal hard-desync teardown (no worse than before). The flag is cleared on baseline arm and on session/resim reset.

## Verification

- Lints clean on `port/net/sys/netrollback.c`.
- Builds: netmenu + offline (pending in this session).
- Soak repro of the FORCE_MISMATCH@520 Firefox/bumper scenario with `SSB64_NETPLAY_ITEM_HASH_FIELD_DIFF=1` to (a) confirm the session survives via deeper-load resync instead of `VS_SESSION_END`, and (b) capture the `item_field_diff` line that pins the non-idempotent bumper field for the follow-up root-cause snapshot fix.

## Related

- `netplay_castle_platform_anim_phase_2026-07-03.md` — Castle bumper X re-derived from the hidden ground controller; captured the controller DObj anim.
- `netplay_castle_bumper_anim_phase_apply_2026-07-03.md` — bumper root DObj anim cursor restore on apply.
- `netplay_castle_bumper_persistent_item_only_reanchor_2026-07-03.md` — frame-commit item-only reanchor escalation (the *forward* path; this doc is the *resim-baseline load* path).
