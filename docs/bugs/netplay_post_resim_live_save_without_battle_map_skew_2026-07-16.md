# Netplay: post-resim live SavePostTick without gcRunAll → +1 map phase skew → PEER_SNAPSHOT_DIVERGE — 2026-07-16

**Status:** FIX DEEPENED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-16 (same-pass hole: 2026-07-18)  
**Session:** `662223406` seed `3473614077` (Android guest ↔ Linux host, Dream Land / Ness)  
**Follow-up soaks:** `2015151666` / `1278030009` (map phase) · `1217807688` (Hold-aim emergency → figh-only PEER)

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

### Same-pass hole (soak `2015151666`, 2026-07-18)

User-visible: ledge JumpAerial / Fall → SpecialHiStart (PK Thunder) → FC@550 / PEER@546. Onset was **not** soft-lip or jibaku float — permanent `Android[t] == Linux[t-1]` map after epoch-2 `POST_RESIM_LIVE sim=413`.

| Peer | First live `map_hash_save tick=413` |
|------|-------------------------------------|
| Android | `0xD86F763A` (**same as tick 412** / resim post mph) |
| Linux | `0x7FC1090D` (advanced) |

No `POST_RESIM_LIVE_SAVE_DEFER` on Android: interface ran for `GetTick==413` **before** `PeerUpdate` finished resim back to exclusive `413`, so `live_battle_sim_ran && tick_at==GetTick` cleared Await and saved post-(412) as 413.

**Deepen:**

5. **`sSYNetRollbackAwaitLiveSimSamePassBlock`** — armed with Await in `FinishForwardResim`; `AllowLivePostBattleSave` always DEFERs while set (`reason=same_pass_as_finish`).
6. **`syNetRollbackOnLiveFuncUpdateBegin`** — clears SamePassBlock at `scVSBattleFuncUpdate` entry so the *next* pass can prove a real post-resim battle.

### Same-pass orphan step (soak `1278030009`, 2026-07-18)

Dual-stick jump spam → stick GGPOs → resims. Same-pass DEFER fired (good), but every Android `same_pass_as_finish` then saved Whispy **blink Δ=2** (skipped the peer’s first post-resim map tic). Permanent `Linux[t]==Android[t-1]` from tick **402** → `PEER_SNAPSHOT_DIVERGE@437`.

Post-`POST_RESIM_LIVE` `MpLanding` advanced live map before Allow deferred; the next FuncUpdate advanced again and labeled both steps as tick `T`.

**Deepen 2:**

7. **Exclusive emergency capture** at end of `FinishForwardResim` (after effect reconciles).
8. **Same-pass DEFER restores** that capture (`restored_exclusive=1`) and re-pins `GetTick` to the exclusive frontier so the next FuncUpdate’s single `gcRunAll` is the only map step for `T`.

### Hold-aim emergency restore (soak `1217807688`, 2026-07-18)

Same-tree rebuild: map phase fixed (`restored_exclusive=1` @416/418/593). Kill became **figh-only** `PEER@601` during Ness SpecialHiHold after stick GGPO resim `591→593`. Android SamePass DEFER+emergency restore promoted PK thunder **head → aim anchor**; Linux (no DEFER) kept anchor. Heads matched initially; figh diverged; map/anim/wpn/cam matched.

**Deepen 3:**

9. When SamePass DEFER and `syNetplayNessAnyLiveFighterInPkHoldAimScope()`, restore via **`LoadSnapshotAfterCompletedTick(T-1)`** + Ness resim hardening/sanitize (`restored_exclusive=2 via=ring_hold_aim`). Emergency restore remains the fallback for non-Hold (map orphan) cases.

### Hold-aim ring collapse (soak `1623117354`, 2026-07-18)

Map phase stayed matched. Android `via=ring_hold_aim` @573/@576 still collapsed sticky aim (`anchor==head`) while Linux (no DEFER) kept distinct aim — harden/reconcile after ring load promoted live head onto `pkthunder_pos`. Hold geometry rematched by ~579 after later resims; PEER@645 was a later SpecialAirHiEnd figh+cam fork (see sibling doc).

**Deepen 4:**

10. Before SamePass DEFER restore, **`syNetplayNessBeginSamePassDeferHoldAimPreserve`** captures live `pkthunder_pos` for Hold-aim-scope Ness; after restore, **`EndSamePassDeferHoldAimPreserve`** re-applies it (`event=hold_aim_preserve` when diag shows a rewrite).

**Deepen 5 (soak `1303842452`):**

11. Hold + emergency available → **`via=emergency_hold_aim`** (exclusive frontier + aim preserve). Do **not** `LoadPostTick(T-1)`+Harden — that froze Hold Y (FC@590 `topn_ty`). Ring without Harden is Hold fallback only.

**Deepen 6 (soak `2082786682`):**

12. SamePass Hold preserve arms a **sanitize skip** for Hold delay/gravity (ApplySlot sanitize becomes HoldEntry sync only). Stale tracking must not rewrite `pkthunder_gravity_delay` after emergency restore (PEER@645 Android `vel_air.y=0` vs Linux −0.5). No extra `SanitizeAllFighters` on Hold DEFER paths.

**Deepen 7 — boundary Finish (soak `1903062639`, 2026-07-18):**

13. **Architecture:** stop peer-local SamePass emergency/ring restore entirely. If live interface already ran when resim completes, **defer** `FinishForwardResim` to the next `syNetRollbackOnLiveFuncUpdateBegin` (`FINISH_DEFER_TO_FUNCUPDATE_BEGIN`). Await+SamePassBlock only reject stale Allow (no world restore). Boundary Finish never arms SamePass restore or exclusive emergency capture. Closes JumpAerial TopN.x fork after matched post@2737 (Android DEFER `via=emergency` then second MpLanding@2738 Δx≈−1.27 → PEER@2757).

## Verify

Re-soak Dream Land with both sticks jumping (and ledge + PK Thunder Hold aim):

- Mid-pass resim complete after interface: `FINISH_DEFER_TO_FUNCUPDATE_BEGIN` (not `via=emergency` / `emergency_hold_aim`).
- Next FuncUpdate: one shared live battle for exclusive T; Whispy blink **Δ=1**; no peer-only TopN.x jump after Finish.
- No FC `figh` inputs=MATCH on Hold `topn_ty` / JumpAerial `topn_tx`.
- Drift scan / sync-report flag `PEER_MAP_PHASE_FORK` if map skew regresses.
