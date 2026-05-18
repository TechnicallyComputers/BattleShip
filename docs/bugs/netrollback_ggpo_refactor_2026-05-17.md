# GGPO-style rollback refactor (2026-05-17)

## Summary

Refactored the in-tree rollback stack toward GGPO/GekkoNet invariants without vendoring external libraries.

## Changes

- **Input timeline** (`netinput_timeline.c`): tracks earliest sim tick with published-vs-strict-confirmed mismatch; gap fill is admission-only.
- **Local rollback authority**: `SSB64_NETPLAY_ROLLBACK_SYMMETRIC` defaults off; peer notices are diagnostic unless explicitly enabled.
- **Pure resim**: `syNetInputPublishSynchronizedTick` + `scVSBattleFuncUpdateBattleSimOnly` (no peer/replay/HID).
- **Atomic load**: pre-load emergency snapshot + restore + `syNetPeerStopVSSession` on `LOAD_HASH_DRIFT`.
- **Dynamic objects**: item cap overflow fails save; missing snapshot items respawn via `itManagerMakeItemSetupCommon`.
- **Hashes**: item/weapon NetSync hashes include `lr`, `player`, and owner gobj id.
- **Module split**: `netpeer_transport.c` owns ingress pump before FuncRead; timeline module split from netinput.
- **Synctest**: `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1` probes load+verify periodically; `scripts/netplay_rollback_matrix.sh` documents presets.

## Verification

- Build `ssb64` after changes.
- Two-peer soak with matrix script presets; confirm no `LOAD_HASH_DRIFT` / `REMOTE_CONFIRMED_CONFLICT` in normal play.
