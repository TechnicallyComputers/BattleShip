# Netplay baseline camera wire authority (Phase 44)

**Date:** 2026-06-10  
**Symptom:** Post-Phase-43 soak1 still fails intro resim — gameplay digests match, camera-only `PEER_SNAPSHOT_DIVERGE` on Android and `RESIM_BASELINE_MISMATCH cam=1` on Linux. No `CAMERA_RING_RESYNC` / `COSMETIC_RING_OK` logs.

## Root cause

Phase 43 camera ring resync required `peer->camera == GetSlotHashCamera(load_tick)` at compare time. After sim advances past `load_tick`, baseline echo/compare runs at `sim > load_tick` while intro camera presentation continues to change. Fresh ring reads and live fallbacks can return tick-skewed camera hashes even though:

- Gameplay digests (figh/world/item/rng/anim/map) already match on the wire.
- Baseline arm froze authoritative digests at load time.
- Cross-peer camera differences during intro Appear are cosmetic, not gameplay divergence.

Android aborted because `ComparePeerBaselineToLocal` compared peer wire camera against a live-skewed local camera and called `FailPeerSnapshotDiverge`.

## Fix

1. **`syNetRollbackCollectBaselineCompareLocal()`** — Prefer ring@load_tick but substitute **armed slot camera** when `load_tick == PeerBaselineLoadTick`.

2. **`syNetRollbackAlignArmedBaselineCameraFromPeerWire()`** — Re-arm `PeerBaselineCamera` from peer wire when gameplay digests match (log `BASELINE_CAMERA_WIRE_ALIGN`).

3. **Baseline gate (`TryOpenResimBaselineGateFromPeerDigest`)** — `slot_ok` excludes camera; gameplay+slots match opens gate and aligns camera from peer wire before replay.

4. **`TryOpenResimReplayGateAfterCameraRingResync`** — If ring camera != peer but gameplay matches armed, adopt peer wire camera (cross-peer cosmetic path).

5. **`ComparePeerBaselineToLocal`** — `PEER_BASELINE_CAMERA_COSMETIC_OK` early return when only camera differs; expanded `PEER_BASELINE_COSMETIC_RING_OK` for gameplay match regardless of camera local read.

6. **`FailPeerSnapshotDiverge`** — Suppress abort when drift is camera-only cosmetic.

7. **Diagnostics** — `cam=0x…` in `RESIM_BASELINE_SEND` / `RESIM_BASELINE_RECV` logs.

## Files

- `port/net/sys/netrollback.c`
- `port/net/sys/netpeer.c`

## Verification

Re-run soak1 (single-appear @239 and dual-appear rematch @239). Expect:

- `BASELINE_CAMERA_WIRE_ALIGN` or `RESIM_BASELINE_CAMERA_RING_RESYNC` when peers disagree on camera only
- `PEER_BASELINE_CAMERA_COSMETIC_OK` instead of `PEER_SNAPSHOT_DIVERGE` on baseline echo
- `resim replay gate open` / `resim complete`
- `cam=0x…` visible in baseline SEND/RECV logs
