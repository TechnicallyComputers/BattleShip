# NetRollback — fighter joint anim_joint.event32 snapshot

**Date:** 2026-05-19  
**Status:** Fix shipped (soak verification pending)

## Symptom

Zebes crouch/S-spam soak (diag env with `SSB64_NETPLAY_RESIM_TICK_TRACE=1`):

- `LOAD_HASH_DRIFT` **anim-only** at tick ~419 (`snap anim` ≠ `live anim` after load).
- `FRAME_COMMIT_STATE_DIVERGE` on **figh** at ~450 with matched input digests.
- Repeated `BASELINE_UNIVERSE_MISMATCH` on **figh** at negotiated `load_tick` (world/rng often match); **no** `resim baseline gate open` / **no** `resim_tick` lines.
- `fighter_detail` at load: Kirby **status/motion** split across peers (e.g. 28/22 vs 30/24).

## Root cause

Fighter joints used `SYNetRbSnapDObjAnimBlob` (wait/frame/speed + first 6 AObj nodes) but not:

- `anim_joint.event32` stream cursor (`AObjAnimAdvance` is `(script)++`)
- `dobj->flags` (Show/Hidden)

Same gap as yakumono ([`netrollback_yakumono_anim_snapshot_2026-05-19.md`](netrollback_yakumono_anim_snapshot_2026-05-19.md)). After load, `hash_animation` at verify time could diverge from the slot hash captured at save even when `figh`/`world`/`rng` matched — soak soft-continued anim-only drift, then fighter CSI diverged and blocked the baseline gate.

## Fix

Extend `SYNetRbSnapFighterBlob` with per-joint `joint_dobj_flags[]` and `joint_anim_joint_event32[]`. Capture/apply in `syNetRbSnapCaptureFighter` / `syNetRbSnapApplyFighter` (anim blob, then event32, then flags). Post-load log: `fighter_anim post-load tick=… live_anim=0x… slot_anim=0x…`.

## Verification

Re-run Zebes S-spam soak with existing diag env. Pass signals:

1. `fighter_anim post-load`: `live_anim == slot_anim` on both peers at load tick.
2. No anim-only `LOAD_HASH_DRIFT` at save boundaries (or rare only).
3. `resim baseline gate open` then `resim_tick` lines with matching `figh`/`anim` cross-peer.
4. No `BASELINE_UNIVERSE_MISMATCH` at the negotiated load tick.

If (3) still fails with matching anim, next bisect is **ring slot poisoning** (load-safe / predicted-remote asymmetry) or upstream sim divergence before save.
