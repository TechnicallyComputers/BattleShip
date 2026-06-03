# Netplay Ness PK Thunder FC recovery resim SIGSEGV (2026-06-01)

## Symptoms

- After legitimate upward jibaku (`hold_frames=51`, status 236), Ness lands in ground jibaku (status 231) with stall catch-up.
- Tick ~600: `anim_length_zero`, then `FRAME_COMMIT_STATE_DIVERGE` (figh only; world/item/rng/eff agree).
- FC recovery resim loads tick **479** while live sim is at **600** (~121-tick span through entire PK Thunder arc).
- `resim-sim-core-reject reason=figh fc_recovery` logged but soft-continue allowed poisoned load; crash in `ftMainProcUpdateInterrupt` (`fault_addr=0x38`) during first resim steps after replay gate open.

## Root cause

1. **Ancient FC load tick** — `LastFrameCommitStateAgreedTick` (~480) predated PK Thunder Hold (~536); recovery rewound through incompatible fighter state.
2. **Soft-continue bypass** — `LOAD_HASH_DRIFT soft` / baseline-storm paths continued FC recovery despite figh slot≠live after apply.
3. **Resim replay without PK Thunder hardening** — forward resim steps did not rebind procs / sanitize Hold vars before `BattleSimOnly`.

Instant jibaku (separate bug) was already fixed; this is a downstream FC recovery failure.

## Fix

- **`syNetplayNessPkScopeEarliestLoadTick`**: track per-player earliest safe load tick from PK Thunder Start/Hold entry; persist through jibaku/SpecialHiEnd until defer teardown completes.
- **`syNetplayNessClampFcRecoveryLoadTick`**: FC recovery loads clamped to ≥ scope earliest (e.g. 479→535).
- **`syNetplayNessAnyLiveFighterInFcResimDeferScope`**: defer FC resim while Hold/jibaku/bound active or defer-teardown pending.
- **`LOAD_HASH_DRIFT fc_recovery figh reject`**: hard-abort load when figh mismatch during FC recovery (no soft-continue).
- **`syNetplayNessResimReplayHardeningAfterLoadStep`**: rebind all fighters + Ness sanitize/jibaku catch-up before each FC recovery resim sim step.
- **Ground jibaku stall**: `SY_NETPLAY_NESS_JIBAKU_STALL_TICKS` 2→1 for faster `anim_length` countdown on live path.

## Verify

Reproduce cross-ISA Ness PK Thunder upward jibaku soak through tick ~600 FC diverge. Expect `fc_recovery_load_clamp` log, no 120-tick ancient load, no SIGSEGV; session either converges or fails load cleanly (`BeginResim` false / figh reject) instead of crashing.
