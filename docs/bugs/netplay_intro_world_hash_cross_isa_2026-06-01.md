# Netplay intro world-hash cross-ISA desync (frame-commit tick 120)

**Date:** 2026-06-01  
**Status:** FIX SHIPPED (soak pending)  
**Log:** `netplay-session-trimmed-rollback.log` (Android aarch64 client + Linux x86_64 host, Peach's Castle, Link vs Samus)

## Symptoms

- Session aborts at **first frame-commit checkpoint** (`fc_validation_ticks=120`, snap tick **119**), still in intro (`status=5` / `motion=4`, `SYNCTEST_SKIP reason=intro_wait`).
- `FRAME_COMMIT_STATE_DIVERGE`: **only `world` hash** mismatches; `figh`, `item`, `rng`, `eff`, and inputs agree.
- Rollback resim loads tick 119, then `BASELINE_UNIVERSE_MISMATCH` / `PEER_SNAPSHOT_DIVERGE` on `world` and session stop.
- Divergence is present from **sim tick 1** (not mid-match).

| Peer | Tick 1 `world` | Tick 119 `world` | `world_detail` at load tick 119 |
|------|----------------|------------------|----------------------------------|
| Linux host | `0x88FE01C4` | `0x88FE01C4` | `spawn_wait=1234`, `appear_valid=19`, `mapobjs=15`, `random_valid=17` |
| Android client | `0x4E02DD3E` | `0x4E02DD3E` | `spawn_wait=0`, `appear_valid=0`, `mapobjs=0`, `random_valid=0` |

Cosmetic seed (`cseed` in `sim_state_tick`) also differs from tick 1 (`0xEB899372` vs `0xE7847115`) while gameplay `rng` matches through the checkpoint — separate from `world` hash (cosmetic stream is not folded into `syNetSyncHashRollbackWorld`).

## What `world` hash covers

Live hash: `syNetSyncHashRollbackWorld()` in `port/net/sys/netsync.c`.

FNV fold over:

1. **`gSCManagerBattleState`** — `time_remain`, `time_passed`, `game_status`, per-player `stock_count` / `score` / `falls` / `stale_id`
2. **`gITManagerAppearActor`** — `spawn_wait`, appear weight table sum/count, `mapobjs_num`, `mapobjs[]`
3. **`gITManagerRandomWeights`** — container/MBall weight table sum/count, `kinds[]`, `blocks[]`
4. **PORT ground extras** — Hyrule twister + Yamabuki gate folds (Peach's Castle: typically inert)

Snapshot ring stores the same fields in `SYNetRbSnapWorldBlob` via `syNetRbSnapCaptureWorld()` / `syNetRbSnapApplyWorld()` in `port/net/sys/netrollbacksnapshot.c`. Slot hash is `slot->hash_world = syNetSyncHashRollbackWorld()` after capture (`syNetRbSnapFillSlotFromLive`).

Frame-commit tokens and peer baseline digests use this same live hash (`syNetRbSnapshotGetSlotHashWorld`, `netpeer.c` RESIM_BASELINE).

## Where appear / random-weight state is initialized

Both tables are set during **`scVSBattleStartBattle`** (netplay TU: `decomp/src/netplay/sc/sccommon/scvsbattle.c`), **before** sim tick 0:

1. `syNetPeerCommitStagedBootstrapMetadataForBattleStart()` → `syNetReplayApplyBattleMetadata()` copies `item_toggles` / `item_appearance_rate` into `gSCManagerTransferBattleState`
2. `itManagerInitItems()` → `itManagerSetupContainerDrops()` fills **`gITManagerRandomWeights`**
3. `grCommonSetupInitAll()` → **`itManagerMakeAppearActor()`** fills **`gITManagerAppearActor`** (`decomp/src/gr/grcommonsetup.c`)

`itManagerMakeAppearActor()` returns `NULL` without touching globals when any guard fails (`decomp/src/it/itmanager.c`):

- `item_appearance_rate == nSCBattleItemSwitchNone`
- `item_toggles == 0`
- `gMPCollisionGroundData->item_weights == NULL`
- summed toggled weights == 0
- `mpCollisionGetMapObjCountKind(nMPMapObjKindItem) == 0`

Success path calls `itManagerSetItemSpawnWait()` (RNG-based, uses shared gameplay seed) and sets `mapobjs_num` / weight tables — matching the Linux `world_detail` numbers in the log.

Automatch host may enable items via `SSB64_NETPLAY_AUTOMATCH_ITEMS=1` when composing `MATCH_CONFIG` (`syNetPeerComposeAutomatchMatchMetadata` in `port/net/sys/netpeer.c`). Client applies host packet via staged metadata at `StartBattle`; local `AUTOMATCH_ITEMS` env is not required on the client.

## Root cause

`itManagerMakeAppearActor()` and `itManagerSetupContainerDrops()` return early when items are disabled (`rate == nSCBattleItemSwitchNone` or `item_toggles == 0`) **without clearing** `gITManagerAppearActor` / `gITManagerRandomWeights`. Those globals persist across VS battles in the same process.

`syNetSyncHashRollbackWorld()` always folds appear/random-weight fields into `world`, so a peer that played a prior local VS with items enabled (e.g. Linux desktop) keeps stale tables even when netplay metadata sets items off. A cold-start peer (e.g. Android) has zeros → instant `world` fork from tick 1, detected at first frame-commit (tick 120).

Soak log (`world_init_diag`): both peers show `rate=0 toggles=0`, but Linux still had `appear_spawn_wait=1238 appear_valid=19 random_valid=17` while Android had zeros — confirming stale globals, not metadata skew or cross-ISA guard failure.

## Fix (2026-06-01)

`itManagerResetSpawnPolicyGlobals()` in `decomp/src/it/itmanager.c`:

- Called at the start of `itManagerInitItems()` (every battle, before `itManagerSetupContainerDrops()`).
- Called from the `itManagerSetupContainerDrops()` failure path instead of only zeroing `weights_sum`.

Clears spawn wait, mapobj/weight counts, table pointers, and sums on both globals so disabled-item netplay matches cold-start peers.

## Root-cause class (original investigation)

Peers agree on fighters, items on stage, RNG at FC, and inputs — so **bootstrap metadata and sim inputs are largely aligned**. The split is specifically **item spawn policy globals**:

- Linux: appear actor + container random weights initialized (`0x88FE01C4`)
- Android: both tables still zero (`0x4E02DD3E` = battle timers only, empty appear/random folds)

That pattern means **`itManagerMakeAppearActor()` and/or `itManagerSetupContainerDrops()` did not populate globals on Android** at `StartBattle`, while they did on Linux — not a rollback hash-formula bug.

Likely sub-causes to confirm on next repro (see diagnostics below):

1. **Battle metadata** — `item_toggles` / `item_appearance_rate` differ at `StartBattle` on Android (stale commit, dropped `MATCH_CONFIG`, or host/client metadata skew).
2. **Stage collision readiness** — `gMPCollisionGroundData->item_weights == NULL` or `mapobjs_num == 0` on Android after `mpCollisionInitGroundData()` (asset/reloc/PORT_RESOLVE or timing).
3. **Cross-ISA-only failure** in `PORT_RESOLVE(gMPCollisionGroundData->item_weights)` or mapobj enumeration on Android.

Unlikely:

- Rollback snapshot roundtrip (`ring_save_player full_ok=0` is local blob-vs-live capture noise during intro anim; both sides show it every tick).
- Fighter/sim fork ( `figh` matches at FC; light fighter hash constant `0x7C8D158D` through intro).

## Why detection waits until tick 120

- `SYNCTEST_SKIP reason=intro_wait` suppresses rollback synctest during intro.
- First `FRAME_COMMIT_COMPARE` runs at `validation=120` (`fc_validation_ticks=120` from session negotiate).
- Hidden divergence from tick 1 → 119; abort looks like “start of game” hard desync.

## Diagnostics added (2026-06-01)

After `grCommonSetupInitAll()` in netplay `scVSBattleStartBattle`, when `SSB64_NETPLAY_WORLD_INIT_DIAG=1`:

- Logs battle `item_toggles`, `item_appearance_rate`, `item_weights` pointer, appear `spawn_wait` / `mapobjs_num`, random `valids_num`, and live `syNetSyncHashRollbackWorld()`.

When `itManagerMakeAppearActor()` returns `NULL` under `PORT && SSB64_NETMENU`, logs which guard failed (same fields).

## Next steps

1. Re-run cross-ISA soak (Peach's Castle, items off) after a prior local VS with items on Linux — `world_init_diag` should show zeros on both peers; FC tick 120 should pass.
2. Consider folding intro into FC earlier or extending synctest once appear tables are proven symmetric (optional hardening).

## Related

- `docs/bugs/netrollback_rng_item_identity_drift_2026-05-17.md` — `gITManagerRandomWeights` snapshot/hash coverage
- `docs/bugs/netplay_sector_arwing_rollback_2026-05-30.md` — prior tick-120 `world` note (item random weights)
