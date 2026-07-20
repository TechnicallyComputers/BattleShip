# Netplay cross-ISA determinism (ARM / x86_64)

**Date:** 2026-05-27  
**Status:** Fix in progress — universal F32 normalization + intro pacing  
**Area:** `port/net/sys/netplay_sim_quantize.c`, `decomp/src/sys/objanim.c`, `port/net/sys/netsync.c`, `port/net/sys/netinput.c`, `CMakeLists.txt`

## Problem

Linux↔Linux netplay is healthy. Linux↔Android (AArch64 guest vs x86_64 host) fails during intro: Fox stuck in `status=5`, camera asymmetry, early `fhash_full` splits. Logs show **1-ULP** joint translate differences (~tick 104) while `fhash_light` still matches — cross-ISA `f32`, not wire loss.

Rollback cannot heal: resim re-runs ISA-specific math and the gap reopens. See also [`netplay_joint_anim_desync_bisect_2026-05-23.md`](netplay_joint_anim_desync_bisect_2026-05-23.md) (AObj topology fork from `anim_frame` ULP).

**Not Android-only:** macOS Apple Silicon and Linux `aarch64` use the same Clang decomp path. Fixes apply on **all peers** when netplay is active (`syNetplayQuantizeF32`), not `#ifdef __ANDROID__`.

## Soak matrix

| Host | Guest | Priority | Expected |
|------|-------|----------|----------|
| Linux x86_64 | Linux x86_64 | Control | Pass (user-confirmed) |
| Linux x86_64 | Android arm64-v8a | P0 | Intro through tick 200; Fox movable after GO |
| macOS arm64 | Linux x86_64 | P0 | Same ISA split as Android; faster iteration |
| macOS arm64 | macOS arm64 | P1 | ARM control (like Linux↔Linux) |
| Linux aarch64 | Linux x86_64 | P1 | ISA-only control (native build) |

## Cross-ISA soak env bundle

Use [`scripts/netplay-cross-isa-soak.env.example`](../../scripts/netplay-cross-isa-soak.env.example) on **both** peers (desktop `debug.env` or Android **Restart with debug.env**).

Key knobs:

- `SSB64_NETPLAY_SIM_F32_QUANTIZE=1` (default on during VS when unset; set `0` to bisect)
- `SSB64_NETPLAY_SIM_STATE_TICK_INTERVAL=1`
- `SSB64_NETPLAY_FIGHTER_SLOT_HASH_LOG=1`, `SSB64_NETPLAY_FIGHTER_SLOT_HASH_TICK_MIN=0`, `SSB64_NETPLAY_FIGHTER_SLOT_HASH_TICK_MAX=150`
- `SSB64_NETPLAY_JOINT_TRANSLATE_TRACE=1`, `SSB64_NETPLAY_JOINT_TRANSLATE_TRACE_SLOT=1` (Fox P1)
- `SSB64_NETPLAY_SNAPSHOT_FIGHTER_FIELD_DIFF=1`
- `SSB64_NETPLAY_FRAME_COMMIT_DIAG=2`, `SSB64_NETPLAY_VALIDATION_DUAL_HASH=1`

Session banner: `SSB64 NetPeer: cross_isa_session …` at VS start (OS, compiler, `isa=aarch64|x86_64|other`).

## Analysis procedure

1. Align logs on same `tick` and `commit_gen` (not host-only tick 1 while `remote_sim=0`).
2. Find first Fox `fhash_full` mismatch; check `joint_translate` hex — **1 ULP** in `tx`/`ty`/`tz` ⇒ ISA float; whole-number `anim_frame` (e.g. `0x40000000` vs `0x3F800000`) ⇒ pacing / different input timeline.
3. Confirm `figh` aggregate matches at tick 1 after `push` catches up (~guest inputs at `hr>0`).

## Fixes shipped (2026-05-27)

| Change | Purpose |
|--------|---------|
| `syNetplayQuantizeF32` / `syNetplayQuantizeVec3f` | Double-round to 1/65536 grid on all peers during VS |
| `gcParseDObjAnimJoint` / `gcPlayDObjAnimJoint` hooks | Normalize anim scalars + joint translate after anim step |
| `syNetSyncHashF32` → quantize before bit fold | Hash matches canonical sim |
| Snapshot fighter blob save/apply quantize | Ring slot matches live forward sim |
| `-ffp-contract=off` on sim-critical TUs | Reduce FMA contraction drift (Clang/GCC, all OSes) |
| **(2026-05-29)** matcher also covers `/decomp/src/netplay/` | `SSB64_NETMENU` swaps stock `decomp/src/lb/lbcommon.c` → `decomp/src/netplay/lb/lbcommon.c`; the old `"/decomp/src/lb/"` pattern missed it, so the linked `lbCommonMag2D`/`NormDist2D` (`sqrtf(x*x + y*y)`) fused on aarch64 only → Link bomb cross-ISA desync. See `netplay_link_bomb_rollback_2026-05-29.md`. |
| **(2026-05-30)** matcher covers whole `/decomp/src/gm/` (was `gmcamera.c` only) | `gmcollision.c` matrix/world-pose dot-products (`gmCollisionGetWorldPosition`/`TransformMatrixAll`) compiled WITH contraction → 1-ULP world-position drift that `itMainSetFighterRelease` seeds into a thrown item's persistent synced position (the 1/65536 grid is finer than the f32 ULP at \|x\|>256, so it can't merge). Sector Z Link/DK bomb-spam item-only desync. See `netplay_thrown_item_world_pose_fma_2026-05-30.md`. |
| Intro `Wait` pacing in `syNetInputRollbackSimAdvanceAllowed` | Block `hr==0` only; same live `runway_cap` as post-Go (see `netplay_intro_wait_advance_frontier_deadlock_2026-07-18`) |
| `gmCameraPlayerZoomFuncCamera` intro gate | Avoid host-only pzoom during wait without peer wire |

## Soak results

| Pairing | Build | Result | First diverge tick | Notes |
|---------|-------|--------|-------------------|-------|
| Linux ↔ Linux | Desktop netplay | Pass (user baseline) | — | Control; re-verify after quantize default-on |
| Linux ↔ Android | APK + desktop | **Manual required** | | Use `scripts/netplay-cross-isa-soak.env.example` on both peers |
| macOS ARM ↔ Linux x86 | Desktop netplay | **Manual required** | | Same env bundle; faster than APK-only iteration |
| Linux aarch64 ↔ Linux x86 | Native builds | **Manual optional** | | ISA-only control |

**Build verification (2026-05-27):** `cmake --build build --target ssb64 -j 4` succeeded on Linux with `SSB64_NETMENU=ON`.

**Pass criteria (intro):** Fox `fhash_full` matches host/guest ticks 0–150 at same `commit_gen`; no `hr==0` host-only `cam` split; Fox leaves `status=5` after GO.

## Long-term

Full [Canonical State Image](../netplay_canonical_state_image.md) field registry replaces raw `fhash_*` oracles. Phases here are a pragmatic float + intro subset.

## Deprioritized until cross-ISA green

- Fox reflector snapshot ([`netplay_fox_speciallw_snapshot_load_2026-05-27.md`](netplay_fox_speciallw_snapshot_load_2026-05-27.md))
- Linux-only rollback mispredict tuning
