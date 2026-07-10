# Netplay — re-enable Sector Z Arwing patrol synctest probes

**Date:** 2026-07-01  
**Status:** Fix implemented (`PORT && SSB64_NETMENU`, soak pending)  
**Area:** `port/net/sys/netrollbacksnapshot.c`

## Change

Removed the synctest skip gates added in [netplay_sector_arwing_deck_jitter_2026-06-05.md](netplay_sector_arwing_deck_jitter_2026-06-05.md):

- `syNetRbSnapshotSectorArwingPatrolLiveSynctestFragile` (`reason=sector_arwing_patrol`)
- `syNetRbSnapshotSectorArwingPatrolSlotSynctestFragile` (`reason=sector_arwing_patrol_probe`)

Deck-coupled fighters remain covered by `sector_arwing_deck` / `sector_arwing_deck_probe`.

## Why

`soak2` (session `1262668438`, ~1200 ticks) stayed paired through active Arwing patrol but skipped **~209** synctest ticks during flight. Forward `map_hash_save` / `sim_state_tick mph` advanced consistently cross-peer; with skips removed, periodic save→restore→verify runs during patrol so `SYNCTEST_FAIL` / `map_hash_yaku1` drift can be located and fixed definitively.

## Expected soak behavior

- Fewer `SYNCTEST_SKIP reason=sector_arwing_patrol` lines (zero during empty-deck patrol).
- Patrol synctest map hash fixed by [netplay_sector_z_patrol_map_hash_verify](netplay_sector_z_patrol_map_hash_verify_2026-07-01.md) — expect `SYNCTEST_OK` through former fail ticks @1100+.
- Real rollback resims unchanged (full `ApplyArwing` still runs on load).

## Verify

Sector Z cross-ISA soak with `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1` + `SSB64_NETPLAY_SNAPSHOT_MAP_HASH_DIAG=1` + `SSB64_NETPLAY_SNAPSHOT_ARWING_DIAG=1` through at least one full patrol window.
