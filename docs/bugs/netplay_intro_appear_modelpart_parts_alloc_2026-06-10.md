# Netplay intro Appear modelpart FTParts alloc (2026-06-10)

## Symptoms

Post–P2 soak @240: both peers survive resim load; Kirby faces forward on both clients; **Yoshi invisible on one peer only** during AppearR egg hatch.

## Root cause

`syNetRbSnapApplyFighterModelPartsFromBlob` skipped joints where `ftGetParts(joint) == NULL`. After `ftMainRefreshFigatreeVisual`, live DObjs can exist without `user_data.p` FTParts on one ISA — modelpart DL push skipped asymmetrically while sim state (`inv=0`, status 220) remained valid.

## Fix (P2b)

- **`syNetRbSnapEnsureFighterJointParts()`** — mirror `ftManagerMakeFighter` FTParts alloc (`ftManagerGetNextPartsAlloc`, flags, accesspart gobj when applicable).
- **`syNetRbSnapApplyFighterModelPartsFromBlob()`** — pre-pass ensure parts on all common joints, then `ftParamSetModelPartID` as before.
- **`SSB64_NETPLAY_SNAPSHOT_MODELPART_DIAG=1`** — log `modelpart_ensure_parts player=… joint=…`.

## Verify

Re-run `INJECT_TICK=240` cross-ISA soak. Expect Yoshi visible on both peers through AppearR; optional `MODELPART_DIAG` shows ensure on any joints that were missing FTParts.
