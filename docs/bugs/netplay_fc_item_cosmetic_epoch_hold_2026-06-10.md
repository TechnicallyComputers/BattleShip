# Netplay: post-intro FC item cosmetic + epoch hold deadlock (2026-06-10)

## Symptom

Yoshi/Kirby cross-OS soak: intro resim completes (`epoch=1 Commit -> Live` @242), match runs ~120 ticks, then **freezes @363** until `VS_SESSION_END`. Both peers spam:

```
rollback_epoch_hold epoch=1 owner=peer_epoch sim=363 cap=362 peer_target=361
tick_commit blocked (load_fail_hold) … allow_battle_sim=0 publish=0
```

## Root cause

1. **Frame commit @360** (120-tick cadence): inputs + figh/world/rng/eff agree; **item partition only** diverges (`0x64213D72` vs `0x1B47A0E2`, same gobj 1013 kind 23). Item hashes had differed cross-OS since ~350 while gameplay partitions stayed matched.

2. **Recovery armed too early**: `HandleFrameCommitStateMismatchCore` called `syNetRollbackArmPeerEpochForStateResim` before `TryCommitCorrectionBegin`, setting `peer_target=361` cap even when resim never started.

3. **Episode guard rejected resim**: input-agree reanchor → `mismatch=241`, `resolved_through=242` from intro episode → `TryCommitCorrectionBegin` returned FALSE (`241 < 242`). Log: `state_resync_commit_failed`.

4. **Deadlock**: sim advanced to 363, exceeded cap 362, `syNetRollbackShouldBlockLiveBattleAdvance` suppressed battle sim with no resim in flight.

## Fix (Phase 46)

1. **`FRAME_COMMIT_ITEM_COSMETIC_OK`** — when inputs agree and only `item_digest` differs (figh/world/rng/eff match), advance `last_agreed` and skip deferred FC recovery (mirrors camera cosmetic authority pattern).

2. **Defer peer epoch cap** — arm `syNetRollbackArmPeerEpochForStateResim` only after `TryCommitCorrectionBegin` succeeds; clear peer epoch + FC recovery flags on commit/resim failure.

3. **Live FC episode reset** — when `mismatch < resolved_through` but `sim_tick > resolved_through`, reset correction episode anchor instead of rejecting (intro episode closed; live FC is a new episode).

## Follow-up

Cross-OS item hash drift at tick 350+ (fold differs per platform for same gobj) may need deterministic item fold audit separate from FC cosmetic bypass.

## Validation

Re-run Yoshi/Kirby cross-OS soak1: expect `FRAME_COMMIT_ITEM_COSMETIC_OK @360`, no `rollback_epoch_hold @363`, match continues past tick 400.
