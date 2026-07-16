# Netplay: post-resim live SavePostTick without gcRunAll → +1 map phase skew → PEER_SNAPSHOT_DIVERGE — 2026-07-16

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-16  
**Session:** `662223406` seed `3473614077` (Android guest ↔ Linux host, Dream Land / Ness)

## Symptom

`netplay-scan-drift.py` → RESULT PASS (no `LOAD_HASH_DRIFT` / `SYNCTEST_FAIL`). Guest (Linux) UNSTABLE `PEER_SNAPSHOT_DIVERGE` @load **1595**; host soft-recovers. Fail-closed partitions: **figh + map + anim + cam**; world / item / rng / wpn match. Inputs agree through load.

## Root cause

After epoch-9 resim (`load=1400 mismatch=1401 target=1403`) both peers logged matching `POST_RESIM_LIVE sim=1403 target=1403` and post digest `mph=0x1EC731A4` (state after completing tick 1402 — exclusive frontier).

| Peer | First live `map_hash_save tick=1403` | `pupupu_ground` @1403 |
|------|--------------------------------------|------------------------|
| Android | `0x1EC731A4` (**same as 1402**) | blink 73 / ww 1051 |
| Linux | `0x3B5E2B81` (advanced; JumpAerial ran) | blink 72 / ww 1050 |

Android then permanently lagged: `Android[t] == Linux[t-1]` for map / Whispy until leave-zero reseed forked (~1485 vs ~1486) and baseline @1595 hard-diverged.

Unlike [`netplay_post_resim_exclusive_tick_wire_cap_skew_2026-07-15.md`](netplay_post_resim_exclusive_tick_wire_cap_skew_2026-07-15.md) (follower stuck at `sim=target-1`), the exclusive-tick **pin already fired**. The remaining hole: `syNetPeerUpdate()` can finish `FinishForwardResim` on the **live** `scVSBattleFuncUpdate` path **after** the interface section, then `AfterBattleUpdate` + `FrameCommit` + `Advance` still run with `GetTick == target` and **no** live `ifCommonBattleUpdateInterfaceAll` for that tick — labeling post-(target-1) state as tick `target`. Strict `< resolved_through` (synctest target save-gap fix) intentionally allows that save.

Linux completed resim via the early resim `PeerUpdate` + return path (Frame complete, then next frame real battle + save). Android completed resim mid-live FuncUpdate and saved immediately (no Frame complete between `Commit -> Live` and `map_hash_save 1403`).

## Fix

Under `PORT && SSB64_NETMENU`:

1. **`sSYNetRollbackAwaitLiveSimAfterResim`** — armed in `FinishForwardResim` when closing to Live; cleared only when a live FuncUpdate pass both ran `gcRunAll` and still has the same `GetTick`.
2. **`syNetRollbackAllowLivePostBattleSave`** — gate used by `scVSBattleFuncUpdate` before `AfterBattleUpdate` / frame-commit / Advance.
3. **Require `live_battle_sim_ran`** on the VS live save path so a skipped interface never commits a phantom tick.
4. **Scripts** — `netplay-scan-drift.py` and `netplay-trim-logs.py --sync-report` report `PEER_MAP_PHASE_FORK` (sustained `map_hash` +1 phase skew) and sync-report compares `mph` in pair sim_state diffs.

## Verify

Re-soak Dream Land through stick GGPOs past several `POST_RESIM_LIVE sim=T target=T`:

- Expect `POST_RESIM_LIVE_SAVE_DEFER` when PeerUpdate closes resim mid-FuncUpdate, then a later live save of `T` with advanced map (not equal to `T-1`).
- Matched `pupupu_ground` / JumpAerial onset after promote.
- Drift scan / sync-report flag `PEER_MAP_PHASE_FORK` if the skew regresses (even when local synctest PASSes).
