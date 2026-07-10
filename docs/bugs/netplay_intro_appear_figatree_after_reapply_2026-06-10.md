# Netplay intro Appear figatree-after-reapply (2026-06-10)

## Symptoms

Post–deeper-load figh-reject soak (`INJECT_TICK=240`): `resim complete` both peers, no crash/desync; Kirby + Yoshi **both face camera** after load despite symmetric sim (`lr=0`, AppearL/R statuses).

## Root cause

`syNetRbSnapshotPrepareLoadedSlotForVerify` runs `SyncFighterPresentation` (`ftMainRefreshFigatreeVisual`) **before** `ReapplyJointAnimFromSlot` re-pins joint pose from blob. Figatree DLs were built from pre-repin joints; live DObjs matched ring but rendered mesh stayed camera-facing.

Secondary: `syNetplayCanonicalizeFighterSimState` let the last joint in the loop overwrite `gobj_anim_frame` (independent blob field).

## Fix (Phase 3b)

- **`syNetRbSnapRefreshFigatreePresentationFromSlot()`** — terminal verify-prep pass: figatree refresh then full joint/anim/modelpart reapply.
- **Canonicalize** — restore `gobj_anim_frame` after joint anim scalar loop.
- **`syNetRbSnapRestoreFighterTopNYawFromLr()`** — Appear Kirby/Yoshi when `lr != 0`: TopN yaw from `lr` (matches `ftMainSetStatus`).

## Fix (Phase 3c — 2026-06-10): reapply-before-sync + Appear diag

Yoshi-only facing wrong after 3b: Kirby already in Wait; Yoshi still in AppearL (`lr==0`, motion-driven yaw).

| Change | Purpose |
|--------|---------|
| `FinalizeLoad` + `RefreshFigatreePresentationFromSlot` | **Reapply → Sync** (and second Sync after post-effect reapply) so `ftMainRefreshFigatreeVisual` binds at restored `gobj->anim_frame` |
| `syNetRbSnapLogAppearPresentationDiag()` | `SSB64_NETPLAY_SNAPSHOT_APPEAR_PRESENTATION_DIAG=1`: `entry_lr`, `topn_ry`, `joint1_ry`, modelpart cursors after verify prep |

## Fix (Phase 3d — 2026-06-10): 3-phase presentation (crash @0x38 regression)

3c bundled modelpart in pre-Sync reapply → Kirby joints with `parts=(nil)` but `live_mp=0` → first replay `SetModelPartID` SIGSEGV.

| Change | Purpose |
|--------|---------|
| **Pose/anim reapply** (no modelpart) → **Sync** → **modelpart push** | Figatree binds at restored `anim_frame` before DL/MObj replay |
| `syNetRbSnapEnsureAllFighterJointPartsForSlot` | `lbCommonAddFighterPartsFigatree` has no NULL `FTParts` guard |
| Modelpart `memcpy` | Per-joint status only when `ftGetParts(joint) != NULL`; else cursor `-1` |

## Verify

Re-soak `INJECT_TICK=240` with `SSB64_NETPLAY_SNAPSHOT_APPEAR_PRESENTATION_DIAG=1`. Expect Yoshi Appear facing correct on both peers; `resim complete` unchanged.
