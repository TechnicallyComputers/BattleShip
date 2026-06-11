# Netplay baseline camera cosmetic tick skew (Phase 43)

**Date:** 2026-06-10  
**Symptom:** Soak1 rematch (dual-appear @239) — match 1 completes; match 2 fails baseline on **camera only** (`slot_ok=0`, `slots_match=1`). Android aborts via `PEER_SNAPSHOT_DIVERGE`; Linux skips replay (`AwaitingBaseline -> Live`) then walkbacks.

## Root cause

Phase 42 fixed ring-sourced `fighter_slots`, but intro **camera hash** has the same tick-skew class: `cam` changes every sim tick during dual Appear while baseline negotiation runs at `load_tick=239` with sim already at 240+.

Soak1 match 2 @239:

```
RESIM_BASELINE_BISECT slot_ok=0 slots_match=1 ... cam=1
PEER_DIVERGE_DIFF partition=camera peer=0x125041B3 local=0x4FAA41B3
```

Main gameplay digests agree; only camera (and live-only slot compare on Android) diverge.

Secondary FSM bug: `syNetRollbackAbortPendingResimForBaselineMismatch()` called `syNetRollbackResetBaselineResimState()` before deeper walkback, forcing **`AwaitingBaseline -> Live`** while resim was still active — forward sim without replay until walkback @238.

## Fix

1. **`RESIM_BASELINE_CAMERA_RING_RESYNC`** — When gameplay wire digests match and peer `camera` equals `ring@load_tick`, re-arm `PeerBaselineCamera` / `SlotCamera` from ring and open replay gate (mirrors Phase 42 slot ring resync).

2. **`syNetRollbackPeerBaselineWireGameplayMatchArmed()`** — Gameplay wire match excluding camera; used by slot/camera resync paths so cosmetic camera drift does not block recovery.

3. **`syNetRollbackCollectRingBaselineAtTick()`** — `ComparePeerBaselineToLocal` always prefers ring@load_tick for local compare (never live fallback when ring is valid).

4. **`PEER_BASELINE_COSMETIC_RING_OK`** — During resim baseline wait, if peer matches ring on gameplay + camera + slots, continue without `PEER_SNAPSHOT_DIVERGE`.

5. **FSM** — `syNetRollbackClearBaselineResimNegotiationFlags()` replaces full reset before walkback; episode stays out of `Live` until deeper restart re-arms `AwaitingBaseline`.

6. **Diagnostics** — `BASELINE_CAMERA_RING_ARM` logs ring vs live camera at arm.

## Files

- `port/net/sys/netrollback.c`

## Verification

Re-run soak1 rematch. Expect match 2:

- `RESIM_BASELINE_CAMERA_RING_RESYNC` or direct baseline digest match
- `resim replay gate open` / `resim complete epoch=2`
- No `AwaitingBaseline -> Live` before replay
- No Android `PEER_SNAPSHOT_DIVERGE` when only camera/slots were live-skewed
