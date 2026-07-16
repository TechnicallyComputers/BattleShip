# Netplay: SYNCTEST_FAIL after resim — missing target tick snapshot (2026-07-13)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptom (soak1 `1194816363`, seed `1812701613`)

Both peers: `SYNCTEST_FAIL tick=393 emergency_ok=1 load_ok=0` immediately after stick GGPO.

```
resim begin … mismatch_tick=391 load_tick=390 target_tick=393 span=2
map_hash_save tick=391 …
map_hash_save tick=392 …
POST_RESIM_LIVE sim=393 target=393 …
map_hash_save tick=394 …   ← never 393
SYNCTEST_FAIL tick=393 … load_ok=0
```

No `LOAD_HASH_DRIFT`, no `LOAD blocked (BATTLE_SIM_HOLD)`. Drift scan: MATCH UNSTABLE for synctest only; pair has no overlapping `sim_state_tick` rows. Gameplay was P1 Ness KneeBend→JumpF on stick-up REPLACE @391 — not a live diverge.

Same class of noise noted earlier as secondary in `netplay_seal_tuple_fork_asymmetric_stall_2026-07-12.md` (SYNCTEST_FAIL @394, map hash never saved).

## Root cause

1. **Resim saves `[mismatch, target)`** — `AdvanceResimBudget` loops `while (t < target)`, saves each `t`, then `AdvanceAuthoritativeSimTick` leaves `GetTick() == target` (exclusive frontier).
2. **`CloseCorrectionEpisode` sets `resolved_through = target`**.
3. **`AfterBattleUpdate` early-returned on `completed_tick <= resolved_through`**, so the first live completion of `target` skipped `SavePostTick`. Next tick saved `target+1`; synctest (`probe = completed - 1`) then loaded the missing `target` slot → `load_ok=0` FAIL on both peers (false positive).

## Fix

1. **Strict `< resolved_through`** for the post-resim save skip — still avoid re-saving resim-rewritten ticks, but allow `SavePostTick(target)` when that tick first completes live.
2. **`SYNCTEST_SKIP reason=no_snapshot`** when the probe ring slot is absent (existence probe via `GetStoredSubsystemHashes`) instead of FAIL.
3. **Bump `SynctestNextProbeTick` past `completed_target`** in `FinishForwardResim` so early post-Wait schedules do not race the first post-resim save.

## Verify on re-soak

- After GGPO with `POST_RESIM_LIVE sim=T target=T`, expect `map_hash_save tick=T` on the next live completion (not a gap to `T+1`).
- Prefer `SYNCTEST_OK` / `SYNCTEST_SKIP reason=no_snapshot` over `SYNCTEST_FAIL … load_ok=0` for empty slots.
- Drift scan should not report MATCH UNSTABLE solely from this false FAIL.

Follow-up (2026-07-15): follower wire/hr cap leaving `POST_RESIM_LIVE sim=target-1` → live +1 phase skew — [`netplay_post_resim_exclusive_tick_wire_cap_skew_2026-07-15.md`](netplay_post_resim_exclusive_tick_wire_cap_skew_2026-07-15.md).
