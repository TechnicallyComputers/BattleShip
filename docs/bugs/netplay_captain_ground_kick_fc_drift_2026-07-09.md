# Netplay: Captain ground-kick landing slide FC drift @3120

**Date:** 2026-07-09  
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android soak2 session `458909621` / seed `3443634566`  
**Match:** Captain Falcon vs Yoshi  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)

## Symptom

- `SYNCTEST_OK=23`, `SYNCTEST_FAIL=0`, `LOAD_HASH_DRIFT=0`
- `FRAME_COMMIT_STATE_DIVERGE validation=3120 snap_tick=3119` — `figh` only, inputs MATCH
- P0 Captain `status=228` (`SpecialLwLanding`): `topn_tx` Linux `0x44BEE424` vs Android `0x44B9E9C4` (~37 units)
- P1 Yoshi `status=51` (`SpecialHi`): smaller coupled `topn_tx` drift (~2.5 units)
- `last_agreed=3000`; forward sim diverged over 120 ticks without intermediate FC checkpoint

## Root cause

First cross-peer `MpLanding` translate fork at **upt=3103** during Captain landing slide on Dream Land `floor_line=3` with `fflags=0x00008000` (`MAP_FLAG_FLOOREDGE`), `branch=diff`, `vv0=0` — same pattern family as pass-platform FC drift but **not** gated on `MAP_VERTEX_COLL_PASS` (`0x4000`).

- Forward sim @3103: Linux `tr_x=0x44A94EE4` (1354.5) vs Android `0x44A46AEC` (1315.3)
- Resim replay from tick-3000 snapshot: both peers agree `tr_x=0x44B3A32D` — fork is **forward sim only**, not rollback replay

Stale MPColl `pos_prev` vs TopN during grounded diff-branch integration on flooredge floors let cross-ISA f32 error accumulate through status 230 ground kick → status 228 landing slide.

## Fix

Mirror pass-platform coll hardening for grounded Captain kick / landing slide:

- `syNetplayFighterInCaptainGroundKickGroundCollScope` — Captain/NCaptain, `ga=Ground`, `status` ∈ `{SpecialLw, SpecialLwLanding}`
- `syNetplayHardenCaptainGroundKickCollBeforeSim` — pre-`gcRunAll` re-anchor `pos_prev=*TopN`, zero `pos_diff`
- `syNetplayCanonicalizeCaptainGroundKickSimState` — post-tick grid on `vel_ground`, `vel_air`, `speciallw.vel_scale`, TopN `rotate.z`, then coll re-anchor
- Snapshot load: `syNetRbSnapRefreshCaptainGroundKickGroundCollAfterLoad`
- Capture mirror: extend knockback/down-tech/pass-platform `pos_prev` fold to captain ground-kick scope

## Files

| File | Change |
|------|--------|
| `port/net/sys/netplay_sim_quantize.c` | Scope + harden + canonicalize |
| `port/net/sys/netplay_sim_quantize.h` | Exports |
| `decomp/src/sc/sccommon/scvsbattle.c` | Pre-sim harden hook (forward + resim) |
| `port/net/sys/netrollbacksnapshot.c` | Load refresh + capture mirror |

## Verification

Rebuild netmenu; re-run soak2 cross-ISA pair. Expect no `FRAME_COMMIT_STATE_DIVERGE` @3120 and matching Captain `topn_tx` through kick landing window.
