# Mixed-input quantize off + prediction/baseline optimizations

**Date:** 2026-05-18  
**Status:** FIX SHIPPED (soak verification pending)

## Symptoms (quantize-off soak)

- Controls felt correct with `SSB64_NETPLAY_MIXED_INPUT_QUANTIZE=0`, but host GGPO still logged `pred=1` with `sx=±85 sy=±85` while remote sticks were partial keyboard encodings.
- Client `PEER_SNAPSHOT_DIVERGE` at `load_tick=1404` with **only `map`** differing; fighter/world/rng/item/anim/weapon/camera matched.

## Root cause

1. **Local quantize default on** — `syNetInputQuantizeStickToDigitalCardinals` on dominant-cardinal partial sticks (e.g. `sx≈70, sy≈8`) promoted both axes to ±85, causing accidental jumps when moving horizontally.

2. **Remote prediction** — `syNetInputMakePredictedFrameRemoteHuman` used the same full cardinal snap on the prediction path, producing `85,85` predictions against partial remote confirms.

3. **Peer baseline compare** — Gameplay-matching peers with map-only digest drift hard-aborted inside the resync window instead of arming `PEER_BASELINE_RESYNC`. Compare could also run mid–forward-resim span.

4. **ROLLBACK_SYNC spam** — Duplicate wire notifies were logged and re-processed before symmetric rollback was applied.

## Fix

### `port/net/sys/netinput.c`

- **Quantize default off** — `SSB64_NETPLAY_MIXED_INPUT_QUANTIZE=1` opt-in only.
- **Dominant-axis prediction snap** — `syNetInputSnapStickDominantAxisForPrediction()` replaces full cardinal quantize on remote prediction (snap strong axis only; zero weak axis; leave diagonals raw).

### `port/net/sys/netrollback.c` + `netpeer.c`

- **Map-only drift** — `PEER_BASELINE_MAP_DRIFT` arms peer baseline resync when core gameplay hashes match (map still authoritative via resync, not soft-continue).
- **Defer baseline compare** during local forward resim span; flush after resim complete / deferred symmetric flush.
- **ROLLBACK_SYNC dedup** — `syNetRollbackAcceptPeerSymmetricRollbackNotify()` suppresses stale recv log + notify.

## Env

| Variable | Default | Role |
|----------|---------|------|
| `SSB64_NETPLAY_MIXED_INPUT_QUANTIZE` | **0** | Local wire snap to ±85 (opt-in) |

## Related

- [`netplay_mixed_input_2026-05-18.md`](netplay_mixed_input_2026-05-18.md)
- [`netrollback_sync_contract_anim_load_2026-05-18.md`](netrollback_sync_contract_anim_load_2026-05-18.md)
