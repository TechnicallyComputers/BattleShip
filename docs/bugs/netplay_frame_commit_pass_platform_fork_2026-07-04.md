# Netplay frame-commit @600 + pass-platform Fox translate fork

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Session:** soak2 `1570738194` (Linux host + Android client)

## Symptoms

- `SYNCTEST_FAIL=0`, `LOAD_HASH_DRIFT=0`, no SIGSEGV
- `FRAME_COMMIT_STATE_DIVERGE validation=600 snap_tick=599` — `figh,rng`, inputs MATCH
- Player 1 (Fox): identical `status=213` / `motion=188` (d-air), but `topn_tx` Linux `0x450AC000` (2220.0) vs Android `0x44F98DF2` (1996.4) ≈ 224 units
- RNG forked downstream; world/item/eff matched
- Recovery: `FRAME_COMMIT_INPUT_AGREE_REANCHOR last_agreed=480`

Secondary artifact: player 0 (Kirby) showed +1 tick timing at frame commit (`status_total_tics` live 89 vs blob 58) — client ran sim 600 before FC compare.

## Root causes (two layers)

### 1. Frame-commit ordering (client overshoot)

Linux: finish sim 599 → save → FC validation=600 (snap 599) → sim 600.

Android: finish sim 599 → **sim 600** → FC validation=600. Live Kirby fields were one tick ahead at compare time.

**Fix:** In `scVSBattleFuncUpdate`, call `syNetPeerFrameCommitAfterCompletedSimStep()` **before** `syNetInputAdvanceAuthoritativeSimTick()`. `syNetPeerFrameCommitAfterCompletedSimStep` now gates on `validation_tick = completed_tick + 1` while `syNetInputGetTick()` still names the completed boundary.

### 2. Fox Pass translate fork @570 (forward sim)

First cross-peer fork: Android forward MpLanding at upt=570 — Fox `tr_x=0x44F98DF2`; Linux/resim `0x450AC000`. Both: `branch=diff`, `vv0=1`, `fflags=0x00004000` (pass platform), same landing verdict, **different translate**. Wrong X persisted through d-air to FC @599.

Resim on both peers byte-matched `FOX_STATE_FORK_TRACE` ticks 578–600 with correct X — fork is **Android forward sim only** after episode @520.

Suspect: stale `coll_data.pos_prev` vs TopN during Squat→Pass on `MAP_VERTEX_COLL_PASS` (Pass entry uses diff-branch integration).

**Fix:**

- `syNetplayHardenPassPlatformCollBeforeSim()` — before `gcRunAll` (forward + resim), re-anchor `pos_prev=*TopN`, zero `pos_diff` for **any grounded fighter on `MAP_VERTEX_COLL_PASS`** (initially Squat/Pass only; broadened after soak `908190465`); grid-quantize TopN when quantize active.
- Same re-anchor at post-tick canonicalize (`syNetplayCanonicalizeFighterSimState`).
- Snapshot load: `syNetRbSnapRefreshPassPlatformGroundCollAfterLoad` (stale-integration gate, no floor probe).
- Capture mirror: extend knockback/down-tech `pos_prev` re-anchor in `syNetRbSnapCaptureFighter` to pass-platform grounded scope.

## Files

| File | Change |
|------|--------|
| `decomp/src/sc/sccommon/scvsbattle.c` | FC before tick advance; pre-sim pass harden |
| `port/net/sys/netpeer.c` | FC interval uses `validation_tick = completed + 1` |
| `port/net/sys/netpeer_frame_commit.c` | Comment update |
| `port/net/sys/netplay_sim_quantize.c` | Harden + canonicalize hook |
| `port/net/sys/netrollbacksnapshot.c` | Load refresh + capture mirror |

## Verification

Rebuild netmenu + offline; re-run soak2 cross-ISA pair. Expect FC @600 pass and no Fox `topn_tx` fork at snap 599.
