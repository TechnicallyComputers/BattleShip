# Netrollback FC recovery resim SIGSEGV (2026-06-01)

## Symptoms

- After successful input-correction resim (~721→841), frame-commit **state diverge** at tick 1080 with **matching inputs** triggered FC recovery resim (961→1081).
- Session crashed in `ftMainProcUpdateInterrupt` (`fault_addr=0x38`) during second `BeginResim`, before `resim begin epoch=` log — likely anchor probe or first sim step on poisoned load.
- Log showed `LOAD_SLOT_LIVE_DRIFT` and `LOAD_HASH_DRIFT resim-sim-core-ok` with large **figh** slot/live mismatch while world/item/rng matched.

## Root cause

1. **`syNetRollbackLoadHashDriftIsResimSimCoreOk`** ignored fighter hash (`(void)live_f`) — FC recovery continued with slot≠live figh after snapshot apply.
2. **`syNetRollbackMaybeResimAnchorProbe`** ran `BattleSimOnly` during FC recovery on drifted load state.
3. **`input.controller`** could be NULL after snapshot load; **`item_gobj`** could be non-NULL with NULL `ITStruct` payload.

## Fix

- Require **figh hash match** in `syNetRollbackLoadHashDriftIsResimSimCoreOk` when `FcStateRecoveryActive`.
- Skip **anchor probe** during FC state recovery.
- **`syNetRbSnapshotRebindAllFighters`**: rebind `input.controller` for human fighters; prune stale `item_gobj`.
- **`ftMainProcUpdateInterrupt` / `ftMainProcParams` (PORT)**: null-safe controller and `itGetStruct` for hammer/sword paths.
- **PK Thunder follow-up (2026-06-01)**: see [netplay_ness_pkthunder_fc_recovery_resim](netplay_ness_pkthunder_fc_recovery_resim_2026-06-01.md) — clamp FC load tick, defer during jibaku scope, hard figh reject, resim replay hardening.

## Verify

Reproduce FC state diverge window (post-resim live drift → validation 1080). Expect `resim-sim-core-reject reason=figh fc_recovery` or failed `BeginResim` instead of SIGSEGV. Normal input-correction resim (non-FC) unchanged.
