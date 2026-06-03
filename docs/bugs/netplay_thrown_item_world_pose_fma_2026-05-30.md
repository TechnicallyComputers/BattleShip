# Thrown-item world-pose FMA leak (gmcollision.c contraction gap)

**Date:** 2026-05-30
**Scope:** `CMakeLists.txt` (fp-contract matcher), `decomp/src/gm/gmcollision.c` (root), `decomp/src/it/itmain.c` (leak point)
**Status:** FIX SHIPPED (soak pending) — added `/decomp/src/gm/` to the `-ffp-contract=off` matcher.

## Symptoms

Cross-ISA soak (Android aarch64 host ↔ Linux x86_64 guest, **Sector Z / Great Fox**, `LOAD_HASH_SOFT=1`), Link vs DK bomb spam:

- All sync partitions agree (`figh`/`world`/`rng`/`eff`/`cseed`/`inp`) — **only `item` forks.**
- First item-hash mismatch at **tick 787** (host `item=0xD7F32576` vs guest `0xE8423777`); masked during the throw window (`synctest_throw_window`), so it escalates silently.
- `FRAME_COMMIT_STATE_DIVERGE` at validation tick **800** (`snap_tick 799`): split `item` digest only (`0xFEFD30BC` vs `0x33CAA04F`).
- Both peers reanchor to load 760 and resim 760→801. Guest completes (1 rollback) then stalls at 802; host hits `RESIM_BASELINE_TIMEOUT` → hard desync stop. No crash.

This is **not** a slope-collision fork. The fighters stand on the same Sector Z geometry and `figh` agrees through the divergence, and the diverging bomb is **airborne** (`link_status=3`, `vy=+64.8`, rising) — it has not touched the stage.

## Root cause

`SSB64_NETPLAY_ITEM_HASH_FIELD_DIFF` pinpointed the exact field at tick 787, step 3 (the airborne thrown bomb at x≈-469):

| Peer | `px_raw` | `px_q` | pos.x |
|------|----------|--------|-------|
| host (aarch64) | `0xC3EAA5C7` | `0xC3EAA5C7` | -469.29514 |
| guest (x86_64) | `0xC3EAA5C8` | `0xC3EAA5C8` | -469.29517 |

Two **adjacent floats** (1 ULP apart), and `px_q == px_raw` on both peers — **the 1/65536 quantize grid was a no-op.** At stage-scale |x|>256, the f32 ULP (~3.05e-5 at magnitude 469) is *larger* than the grid step (~1.5e-5), so the absolute grid cannot merge two adjacent floats. Quantization only absorbs drift coarser than ~half a grid cell; it structurally cannot heal a 1-ULP split at these coordinates.

Where the 1 ULP enters: when DK throws the bomb, `itMainSetFighterRelease` (`itmain.c:404`) seeds the item's **persistent synced position** directly from the fighter's hand-joint **world** position:

```c
gmCollisionGetFighterPartsWorldPosition(fp->joints[joint_id], &pos);  // itmain.c:425
DObjGetStruct(item_gobj)->translate.vec.f.x = pos.x;                   // itmain.c:427
```

`gmCollisionGetFighterPartsWorldPosition` (in `decomp/src/gm/gmcollision.c`) walks the joint parent chain calling `gmCollisionTransformMatrixAll` + `gmCollisionGetWorldPosition`. Those are dense dot-products:

```c
// gmCollisionGetWorldPosition, gmcollision.c:204
product.x = ((mtx[0][0] * vec->x) + (mtx[1][0] * vec->y) + (mtx[2][0] * vec->z)) + mtx[3][0];
```

The rotation matrix uses the game's **table-based** `lbCommonSin`/`lbCommonCos` (single multiply + truncate + lookup — fully cross-ISA deterministic), so trig is **not** the source. The source is **FMA contraction**: with contraction enabled, clang fuses `a*b + c*d + e*f` into `fmadd` on aarch64 (single rounding) while baseline x86_64 uses separate rounded ops. That sub-ULP split rounds to adjacent f32s in the world position.

**Why it leaks only into items, not fighters:** a fighter's *synced* pose is its quantized physics root (velocity-integrated on the shared grid); its joint world positions are transient collision inputs that feed integer collision flags (robust to 1 ULP). A thrown item is different — the joint world position *becomes* the item's persistent synced translate, importing the non-canonical matrix divergence into the item hash.

**Why the matcher missed it:** same gap class as the 2026-05-29 `lbcommon.c` fix. The `CMakeLists.txt` contraction matcher keyed on `"/decomp/src/gm/gmcamera\\.c$"` only, leaving `gmcollision.c` (and `gmcolscripts.c`, `gmcommon.c`) compiled **with** contraction.

## Fix

Replaced the single-file `gmcamera.c` match with the whole `/decomp/src/gm/` tree in the `-ffp-contract=off` matcher (`CMakeLists.txt`). All five `gm/` TUs are gameplay sim (collision, camera, collision-scripts, common, rumble); the block is `SSB64_NETMENU`-only so offline builds are unaffected (and contraction-off is *more* faithful to N64 behavior — the N64 FPU had no FMA). This deterministic-izes the entire collision/world-pose matrix surface, so the throw-release world position is now bit-identical cross-ISA and the item hash no longer forks at the throw.

**Verified:** `compile_commands.json` shows `-ffp-contract=off` on `gmcollision.c`, `gmcolscripts.c`, `gmcommon.c`, `gmcamera.c`, `gmrumble.c`. `cmake --build build --target ssb64 -j 4` clean (Linux, `SSB64_NETMENU=ON`).

## Soak

Re-run the Sector Z Android↔Linux Link/DK bomb-spam soak with `SSB64_NETPLAY_ITEM_HASH_FIELD_DIFF=1` + `ITEM_THROW_WINDOW_DIAG=1`. Pass: no item-only `FRAME_COMMIT_STATE_DIVERGE` after throws; thrown-bomb `px_raw` matches host/guest at the first synctest after release.

## Related / follow-ups

- Supersedes the slope hypothesis for this divergence (stage collision is deterministic — `figh` agrees).
- Same gap pattern as [`netplay_link_bomb_rollback_2026-05-29.md`](netplay_link_bomb_rollback_2026-05-29.md) (relocated `lbcommon.c` contraction) and the matcher policy in [`netplay_cross_isa_determinism_2026-05-27.md`](netplay_cross_isa_determinism_2026-05-27.md).
- **Open:** `lbCommonCheckAdjustSim2D` (`decomp/src/netplay/lb/lbcommon.c:491`) compares against libm `cosf(angle + 90°)`. libm `cosf` is not correctly-rounded and can differ 1 ULP across aarch64/x86_64 glibc, flipping the comparison in knife-edge cases. Not on the airborne bomb path that diverged here, but a candidate cross-ISA hole — replace with table `lbCommonCos` if it ever shows in a field diff (behavior change → needs its own verification). The `__sinf`/`__cosf` matrix builders later in that file feed GBI fixed-point (render-only, `FTOFIX32`-quantized) and are not sync-critical.
- **Structural note:** the absolute 1/65536 grid cannot absorb cross-ISA drift at |coord|>256 (ULP > grid step). The durable defense is keeping the *source* contraction-free (this fix) rather than relying on the grid; a relative/mantissa-clamp requantize for large-magnitude item coords remains a possible future hardening if a non-contraction source ever surfaces.
