# Netplay: resim baseline used post-coupling live world digest (PEER_SNAPSHOT_DIVERGE)

**Date:** 2026-05-23  
**Status:** FIX SHIPPED (WAN soak pending)  
**Soak:** WAN Yoshi (host) vs Link (client), FC recovery @4200, load_tick=4080, `resim begin` then `PEER_SNAPSHOT_DIVERGE` on `world`.

## Symptom

- `FRAME_COMMIT_STATE_DIVERGE` on `figh` with agreed `inp_*`.
- `INPUT_AGREE_REANCHOR` → `syNetRollbackLoadPostTick(4080)` succeeds, `resim begin`.
- `RESIM_ANCHOR_PROBE` @4080→4081: `match_f=0` (fighter hash after one forward step ≠ ring @4081).
- `LOAD_SLOT_LIVE_DRIFT`: live `world=0x6E8F32A5`, slot `world=0x8C581B88`; `figh` matched.
- `RESIM_BASELINE_SEND` / `PEER_SNAPSHOT_DIVERGE`: peer `world=0x8C581B88`, local compare `world=0x6E8F32A5` → session stop before forward resim.

Earlier soak @4560 failed earlier (`LOAD_HASH_DRIFT` figh+anim at verify, no `resim begin`) — related load/anim class, not this wire bug.

## User hypothesis: missing strike momentum in snapshots

**Ruled out.** `SYNetRbSnapFighterBlob` includes full `struct FTPhysics` (`vel_air`, `vel_damage_air`, `vel_ground`, `vel_damage_ground`, `vel_jostle_*`). `syNetRbSnapCaptureFighter` copies `blob->physics = fp->physics`. Anchor-probe `fighter_field_diff` on `vel_air_y` / joints is **live after one sim step** vs ring @4081, not missing capture at strike time.

## Root cause

1. **Load verify runs before coupling finalize** (`syNetRollbackLoadPostTick`): `syNetRollbackVerifyLoadedSlot` checks live vs ring hashes at restored save state; then `syNetRbSnapshotFinalizeLoadCoupling` + deferred weapon eject can change **world/item** digests without updating ring `hash_world`.
2. **`syNetRollbackArmResimBaselineAfterLoad` sent live digests on the wire** (`PeerBaselineWorld = live.world`) while `syNetRollbackComparePeerBaselineToLocal` compares peer packets to **ring slot** hashes → spurious `PEER_SNAPSHOT_DIVERGE` when only post-coupling world drifted.
3. **`RESIM_ANCHOR_PROBE` with `keep_loaded`** did a partial reload (no coupling pipeline) and left sim at `probe_tick` before baseline arm, polluting `syNetRollbackCollectHashes()` if probe ran first.
4. **`syNetRollbackTryDeeperLoadBeforeResim`** treated post-coupling world-only drift as failure and could spuriously reload `load_tick-1` even when verify passed.

## Fix

| Change | File |
|--------|------|
| Wire baseline digests = ring slot hashes at `load_tick`; `ResimPreHashes` still live for post-resim check | `netrollback.c` — `syNetRollbackCollectSlotBaselineDigests`, `syNetRollbackArmResimBaselineAfterLoad` |
| Arm baseline **before** anchor probe; probe `keep_loaded` restores via `syNetRollbackLoadPostTick` | `syNetRollbackBeginResim`, `syNetRollbackMaybeResimAnchorProbe` |
| Deeper pre-resim only when `figh` or `rng` ≠ slot (ignore expected world/item post-eject) | `syNetRollbackTryDeeperLoadBeforeResim` |
| Deeper restart: baseline before probe (duplicate arm removed) | `syNetRollbackTryRestartResimAtDeeperLoad` |

## Verify

Re-soak with same diag env; expect `RESIM_BASELINE_SEND` world == peer == slot @load_tick, `resim replay gate open`, forward resim without `PEER_SNAPSHOT_DIVERGE`. `RESIM_ANCHOR_PROBE match_f=0` may still log (cross-peer forward sim); separate from baseline wire.

Optional: `SSB64_NETPLAY_PEER_DIVERGE_DETAIL=1` if world still diverges cross-peer after fix (true sim fork).
