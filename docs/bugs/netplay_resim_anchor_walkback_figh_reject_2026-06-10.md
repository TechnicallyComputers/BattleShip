# Netplay resim anchor walkback figh reject (2026-06-10)

## Symptoms

- Post–P2 soak (`INJECT_TICK=240`): load @239 succeeds on Linux + Android; anchor walkback reaches load @231.
- `LOAD_HASH_DRIFT resim-sim-core-ok` with large `figh` slot≠live (`0x02CB442C` / `0xDD265BD4`) at Kirby Wait→AppearR boundary.
- Symmetric SIGSEGV in `ftMainProcUpdateInterrupt` (`fault_addr=0x38`) on anchor probe forward sim after poisoned walkback load.

## Root cause

`syNetRollbackLoadHashDriftIsResimSimCoreOk` ignored fighter hash drift outside FC recovery. Anchor walkback during `BeginResimInitialLoad` soft-continued intro status-transition loads where world/item/rng matched but apply produced incompatible fighter sim (Kirby `status=250` AppearR blob vs live Wait path).

## Fix

- **`syNetRollbackLoadHashDriftIsResimSimCoreOk`**: require `live_f == slot_figh` when `BeginResimInitialLoad` (same policy as FC recovery).
- **Anchor walkback loop**: on `LoadPostTick` failure, revert `load_tick` / episode to `before_load` (last contract tick) instead of leaving episode pinned to failed deeper tick.
- **`syNetRbSnapApplyFighterModelPartsFromBlob`**: skip joints where `ftGetParts(joint) == NULL` (secondary guard; `ftParamSetModelPartID` dereferences parts before joint NULL check).

## Follow-up

Baseline deeper restart bypassed this gate — see [`netplay_resim_deeper_load_figh_reject_2026-06-10.md`](netplay_resim_deeper_load_figh_reject_2026-06-10.md).

## Verify

Re-run `INJECT_TICK=240` cross-ISA soak. Expect walkback to stop at @231 with `resim fidelity — deferring` or successful load at @232, no SIGSEGV @0x38, session proceeds toward replay or clean load fail.
