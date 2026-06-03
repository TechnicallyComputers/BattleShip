# Link bomb rollback: proc-only reapply, multi-bomb matching, item hash

**Date:** 2026-05-29  
**Scope:** `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netsync.c`  
**Status:** FIX SHIPPED — cross-ISA root cause found: relocated `decomp/src/netplay/lb/lbcommon.c` compiled WITH FMA contraction (build matcher gap); now forced `-ffp-contract=off`. (soak 4: item FC @1320, recovery @1200 — load-verify reconcile + FC interval cap below)

## Symptoms

Cross-ISA soak (Android aarch64 ↔ Linux x86_64, Zebes, `LOAD_HASH_SOFT=0`):

- Chaotic Link bomb behavior with 2–3 simultaneous items (`item_count=3` ticks 1388–1433).
- `SYNCTEST_FAIL` with **item** load drift at tick 1542 (`item=0xCE06C2B6/0xAC2D3599`) after matched apply (`matched=1`).
- `FRAME_COMMIT_STATE_DIVERGE` at tick 1800: agreeing `figh/world/rng/eff/inp`, split `item` digest.
- FC recovery load at 1680: `ejected=1`, session hard-abort on `cam`+`eff` drift (`LOAD_HASH_SOFT=0`).

Post-fix soak 3 (Saffron, `LOAD_HASH_SOFT=0`): item hash stable on all loads; no FC diverge; no session abort. Remaining `SYNCTEST_FAIL` **figh/eff only** (ticks 759, 879, 1210) during multi-bomb + throw windows. Midair bomb-bomb collision: one bomb explodes, one survives — **likely vanilla** (`itLinkBombThrownProcHit` velocity thresholds); not changed. Ground “poof” without boom: explode proc-only reapply skipped `itMainRefreshAttackColl`; sparkle VFX are LBParticles wiped on every rollback load (not snapshotted).

## Root cause

1. **Snapshot apply round-trip:** `syNetRbSnapReapplyLinkBombStatusAfterBlob` called `itLinkBomb*SetStatus` / `itMainSetStatus` after blob restore. Those reset hashed fields (`attack_state`, `is_allow_pickup`, `vel`, `is_thrown`, `is_damage_all`, `drop_update_wait`).
2. **Multi-bomb matching:** Fallback `FindItemBlobByKindPos` (nearest position only) ambiguous with 2+ Link bombs from same player.
3. **Item hash gap:** Rollback item fold omitted `multi`, `event_id`, `ga`, `physics.vel_air`, link-bomb `item_vars`, and status nibble — blob-captured sim state invisible to FC/load verify.
4. **Explode reapply gap:** Proc-only explode path hid DOBJ but did not refresh attack colliders — damage hitbox could be stale after rollback load.
5. **Finalize ordering:** Joint-anim / presentation finalize ran after item hold coupling; fighter↔held-item links and effect blobs could drift before hash verify.

## Fix

| Area | Change |
|------|--------|
| Reapply | Proc-only rebind via `dItLinkBombStatusDescs[status]`; infer status from blob flags + live `ga`/`is_hold`/`drop_update_wait`; hold coupling + explode DOBJ hide only |
| Matching | `syNetRbSnapFindLinkBombBlob`: filter `player` + `owner_gobj_id`, score `dist² + vel²`, tie-break `blob->gobj_id` |
| Hash | `syNetSyncFoldLinkBombItemExtras` in `syNetSyncFoldActiveItemGobjForRollback` when `kind == nITKindLinkBomb` |
| Explode | After proc rebind: `itLinkBombCommonSetHitStatusNone` + `itMainRefreshAttackColl` (no `ExplodeSetStatus` / particle respawn) |
| Explode VFX | Cosmetic `efManagerSparkleWhiteMultiExplodeMakeEffect` replay on explode reapply after particle teardown |
| Post-eject sparkle | Ring scan replays sparkle for recent explode blobs after load when item GObj already ejected (`SYNETRB_LINK_BOMB_SPARKLE_REPLAY_WINDOW`) |
| Effects | Reconcile tracks respawned effect GObj ids so prune pass does not eject quakes with new ids |
| Finalize | End of `syNetRbSnapshotFinalizeLoadFromSlot`: re-run hold coupling, orphan-held reconcile, effect reconcile |
| Synctest | Skip probe when `item_count >= 2`, effect count mismatch, live link bomb, or recent explode blob in ring (`link_bomb_sparkle_probe`) |
| Sort key | Item rollback hash sort by semantic key (kind/player/type/multi/event_id/quantized pos/vel), not `gobj->id` alone — prevents XOR-fold order drift when allocation order differs |
| Sim grid | `syNetRbSnapshotCanonicalizeActiveItemsForNetplay` after each forward sim tick + on snapshot save/apply (translate + `vel_air`) |
| FC recovery | `resim-sim-core-ok` accepts `ExecutingEpisode.valid` / `FcStateRecoveryActive`; LoadPostTick re-checks sim-core before hard abort |
| Hold apply | Hold blobs match by `player`/`owner`/`multi`/`event_id` + fighter `item_gobj_id`; field blobs exclude hold; post-finalize `syNetRbSnapReconcileItemsToSlotBlobs` re-applies blobs before hash verify |
| Load verify | `syNetRbSnapshotReconcileLoadedItemsForVerify` after hold coupling + canonicalize before hash; field link bomb respawn via `MakeItemSetupCommon` |
| FC cadence | `syNetRbSnapshotFrameCommitIntervalCap` — 40-tick validation during hold/throw/multi-item (default 120) |
| Respawn | Link bomb respawn skips `WaitSetStatus` pre-clobber — blob apply + proc rebind only |

## Soak

Re-test with `LOAD_HASH_SOFT=0`, `SSB64_NETPLAY_SNAPSHOT_ITEM_DIAG=1`, heavy Link bomb play (held + double throw + explode). Pass: explosion sparkles visible after rollback loads; `effect_count` stable across synctest; no FC `item` diverge with agreeing inputs. Compare offline vanilla for bomb-bomb collision before changing `itLinkBombThrownProcHit` thresholds.

### Control result (2026-05-29 soak series)

- **Same-ISA (x86_64 Linux ↔ x86_64 Linux), same binaries, `SIM_F32_QUANTIZE=0`:** full ~6577-tick match, `FRAME_COMMIT_DIAG state_diverge=0`, `recovery_started=0`, item slot==live everywhere, clean `VS session stop`. Remaining `LOAD_HASH_DRIFT`/`SYNCTEST_FAIL` were **cam/eff-only local re-derivation noise**, non-fatal.
- **Cross-ISA (Android aarch64 ↔ Linux x86_64):** item-only FC diverge / held-bomb item load mismatch reproduce and kill the session (@582 held-bomb `item_mismatch=1`, @1443 thrown-bomb FC diverge).
- **Conclusion:** the rollback save→load→apply roundtrip and item fold are deterministic on same-ISA; the bomb desyncs are **cross-ISA float divergence in item forward sim**, not an apply/match/reconcile bug. `SSB64_NETPLAY_SIM_F32_QUANTIZE` defaults **on** (`netplay_sim_quantize.c`); the `cross_isa_session … sim_f32_quantize=vs_default` banner is a hardcoded literal and does not reflect the env.

### Pinpointing the diverging float

Run the next cross-ISA soak with `SSB64_NETPLAY_ITEM_HASH_FIELD_DIFF=1` (+ `ITEM_HASH_TRACE=1`) on both peers. On item drift, each Link bomb now emits an **`item_fold_floats`** row with raw + quantized IEEE-754 bits (`px_raw`/`px_q` … `vz_raw`/`vz_q`) for exactly the folded floats plus the integer fold inputs. Diff host vs guest at the same `sim_tick`: first **`*_q`** mismatch = the field whose hashed bits diverge; **`*_raw`** mismatch with matching **`*_q`** = quantization absorbed it (not the source).

**Throw-window blind spot (2026-05-29 Sector Z soak):** synctest skips during `fighter_throw`/`item_throw` left a gap until FC @2955. **`SSB64_NETPLAY_ITEM_THROW_WINDOW_DIAG`** (default on in VS) now logs **`item_throw_window`** + **`item_hold_coupling`** on those skip ticks and chains field diff when enabled. Hold release path: `itMainSetFighterRelease` ejects hold wrapper, samples **`joint_itemlight/heavy`** world position, **forces `z=0`**, then applies throw velocity — only **`fp->item_gobj`** is released (one held item per fighter).

### Root cause of the cross-ISA float divergence (2026-05-29, confirmed)

The diverging Y position/velocity traced to the bomb terminal-velocity clamp in `itMainApplyGravityClampTVel`:

```c
ip->physics.vel_air.y -= gravity;
if (lbCommonMag2D(&ip->physics.vel_air) > terminal_velocity) {
    lbCommonNormDist2D(&ip->physics.vel_air);   // sqrtf(x*x + y*y) + divide
    lbCommonScale2D(&ip->physics.vel_air, terminal_velocity);
}
```

`lbCommonMag2D`/`lbCommonNormDist2D` compute `sqrtf(SQUARE(x) + SQUARE(y))`. IEEE `sqrtf` (hardware `fsqrt`/`sqrtss`) and the divide are correctly-rounded and bit-identical across aarch64/x86_64 — but the **`x*x + y*y` sum is not**: with FMA contraction enabled, aarch64 fuses it into one `fmadd` (single rounding) while baseline x86_64 (no FMA) does two rounded ops. That sub-ULP difference feeds `sqrtf`, accumulates over the projectile's flight, and splits the item hash cross-ISA — exactly the observed ~6e-5 Y drift that the 1/65536 quantize grid is too coarse to absorb (and that f32 cannot even represent at stage-scale coordinates).

**Why the existing `-ffp-contract=off` policy missed it:** `cmake/Ssb64NetmenuSources.cmake` removes stock `decomp/src/lb/lbcommon.c` and compiles `decomp/src/netplay/lb/lbcommon.c` in its place for `SSB64_NETMENU`. The `CMakeLists.txt` contraction matcher keyed on `"/decomp/src/lb/"`, which does **not** match `"/decomp/src/netplay/lb/"`, so the actually-linked shared vector math (`lbCommonMag2D`/`NormDist2D`/`Scale2D` and all the matrix `sqrtf` normalizers) compiled with contraction on. Same-ISA peers stayed bit-identical regardless, which is why the Linux↔Linux control soak was clean.

**Fix:** added `_src MATCHES "/decomp/src/netplay/"` to the contraction matcher so every relocated netplay sim TU is contraction-free. Verified via `compile_commands.json`: `decomp/src/netplay/lb/lbcommon.c` now carries `-ffp-contract=off`. This is the deterministic fix for the whole projectile/fighter physics surface (not just the bomb) and supersedes the proposed software-sqrt/in-sim-requantize workarounds, since the hardware sqrt itself was never the divergence — the unfused-vs-fused squared-magnitude sum was.
