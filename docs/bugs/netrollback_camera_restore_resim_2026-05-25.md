# Netplay: camera wrong after FC resim (session 990645650)

**Date:** 2026-05-25  
**Status:** FIX SHIPPED (soak pending)  
**Logs:** `client-auto.log` + `ssb64 (2).log` — Pokemon Stadium, FC@480 reanchor load 360 → resim 361–481.

## Symptom

After a 120-tick resim, the battle camera stayed on the wrong framing (stock-loss / player-zoom style) through catch-up replay. `sim_state_tick` showed `cam=0xA4A09E37` unchanged for ticks 481–600 while `figh`/`anim` moved. Session ended at FC@600 (`PEER_BASELINE_RESYNC_STORM` @353). No SIGSEGV; `SSB64_NETPLAY_SNAPSHOT_FIGHTER_FIELD_DIFF=1` produced no `fighter_field_diff` lines (load @360 verified).

## Log timeline (camera hashes)

| Phase | `cam` (live `syNetSyncHashGMCamera`) | Notes |
|-------|--------------------------------------|--------|
| Post-load baseline arm @360 | `0x8AED33F7` | `resim complete` pre |
| End of forward resim @480 | `0xD44C7412` | `resim complete` post; `RESIM_STATE_DELTA` |
| First live tick logged @481 | `0xA4A09E37` | One sim step after resim; then flat ~120 ticks |
| FC@600 peer token | host `0x45E5B464` world vs client `0xD8B05EF4` | Underlying sim fork, not camera-only |

`rb_applied=1` is `syNetRollbackGetAppliedResimCount()` (rollback ordinal), not “replay without sim.” `sim_state_tick` always logs **live** camera hash, not ring slot digest.

## Root cause

1. **Snapshot restore copied `GMCamera` but did not refresh the battle CObj.** `syNetRbSnapApplyCamera` assigned `gGMCameraStruct` and fighter zoom pointers, then returned. View/projection (`CObj`) stays stale until `gmCameraRunFuncCamera` runs (normally via `gcRunAll` in `ifCommonBattleGoUpdateInterface`). After load/resim boundaries the struct and CObj can disagree → visible wrong camera while the partial hash stays constant.

2. **`syNetSyncHashGMCamera` was too narrow** for stock-zoom / follow modes: only `target_dist`, `fovy`, pause eyes, and pzoom player. `status_curr`, follow target, and zoom distances were excluded, so verify and FC tokens could miss camera-class drift (`LOAD_HASH_DRIFT` on `cam` still blocks resim — anim-only soft-continue requires cam match).

3. **Episode seal gap** (`EPISODE_SEAL_ROWS_WAIT` slot 1) is a separate issue; replay gate still opened. Not the direct visual restore bug.

## Fix

| Change | File |
|--------|------|
| After apply, call `gmCameraRunFuncCamera(gGMCameraGObj)` when non-NULL | `netrollbacksnapshot.c` — `syNetRbSnapApplyCamera` |
| Fold `status_curr`, zoom/follow distances, eyes, `pfollow` player into `syNetSyncHashGMCamera` | `netsync.c` |

## Verify

Re-soak same diag env (`SIM_STATE_TICK_INTERVAL=1`, FC cadence 120). Expect:

- Camera framing tracks fighters after resim catch-up.
- `cam=` in `sim_state_tick` changes when zoom mode transitions (not frozen across long KO sequences unless hashed fields truly idle).
- Optional: `LOAD_HASH_DRIFT` on `cam` if restore regresses (no longer silent).

Optional bisect: `SSB64_NETPLAY_HASH_TRANSITION_LOG=1` (add `cam` to transition log in a follow-up if needed).
