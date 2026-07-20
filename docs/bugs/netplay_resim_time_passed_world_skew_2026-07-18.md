# Netplay resim skips `time_passed` → world-only PEER_SNAPSHOT_DIVERGE (2026-07-18)

**Soak:** soak1 Linux + Android after intro Wait advance/FC-defer fix. Intro Appear smooth; desync on first stick movement post-GO.

## Symptom

- `SYNCTEST_OK` through ~391; Go world transition aligned (`0x55ED739C` @390).
- First post-Go stick → Linux GGPO `mismatch=396` `load=395` → baseline wait → `RESIM_BASELINE_TIMEOUT` (`seal_rows_missing`) → deepen load **394**.
- After resim, Linux `rollback_post tick=395` has `time_passed=4` `world=0x2233B265` (tick **394** clock/hash).
- Android live @395: `time_passed=5` `world=0xE8B897F8`.
- Linux sends corrupted `RESIM_BASELINE_SEND load_tick=395 world=0x2233B265`.
- Android: `BASELINE_UNIVERSE_MISMATCH` → deepen exhausted → **`PEER_SNAPSHOT_DIVERGE`** — **world only** (`figh`/`map` MATCH). `GGPO_CLASS_SUMMARY`: 0 stick/button classes.

## Root cause

`gSCManagerBattleState->time_passed` is derived from `sSYNetSyncBattleGoSimTick` in `syNetSyncReconcileBattleTimePassedCore`. Vanilla timer early-outs under netplay.

`syNetSyncReconcileBattleTimePassedForSimTick` / `ForSnapshotSave` **returned immediately while `syNetRollbackIsResimulating()`**, so `BattleSimOnly` resim never advanced the battle clock. Load@394 restored `time_passed=4`; fighters resimmed to 395; world hash stayed on 394.

The resim early-out was an over-broad reaction to load-time `FromSimTick` using a frontier `GetTick` (LOAD_HASH_DRIFT, 2026-05-17). The real guard is `sim_tick == syNetInputGetTick()` on the ForSimTick path — not “never during resim.”

## Fix

1. **`ForSimTick`** — drop resim early-out; keep GetTick match guard; run core on live + resim steps.
2. **`ForSnapshotSave`** — always run core for `completed_sim_tick` (including resim slot rewrite).

## Verify

Re-soak first post-Go movement. Expect:

- After any deepen/resim through Go clocks, `world_detail` / baseline at tick T has `time_passed = T - go_latch` (Go@390 → @395 is 5).
- No world-only `PEER_SNAPSHOT_DIVERGE` with matching figh/map right after first stick.

## Related

- [`netplay_battle_go_resim_wait_skew_2026-06-11.md`](netplay_battle_go_resim_wait_skew_2026-06-11.md) — Go latch / authoritative GO tick
- [`netrollback_rng_item_identity_drift_2026-05-17.md`](netrollback_rng_item_identity_drift_2026-05-17.md) — battle clock + load GetTick drift
- [`netplay_intro_wait_advance_frontier_deadlock_2026-07-18.md`](netplay_intro_wait_advance_frontier_deadlock_2026-07-18.md) — intro pacing that unblocked this soak
