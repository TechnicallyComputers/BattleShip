# Netplay: rebirth halo EFStruct free-list desync — 2026-06-30

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)
**Scope:** `decomp/src/ef/efmanager.c`, `port/net/sys/netrollbacksnapshot.c`, `decomp/src/ft/ftcommon/ftcommonrebirth.c`

## Symptom

`soak2` cross-ISA pair (session `11770459`, android host / linux guest) reached **STABLE (soft recovery)** with quake synctest clean (`synctest_ok=17 fail=0`), but Kirby's **second stock** rebirth at tick **2532** had no respawn platform:

- `make_effect_fail step=ef_struct free=26` on both peers (deterministic)
- `halo_ensure_fail reclaimed=0 ef_free=26->26` every tick through the rebirth window
- Link was still in `RebirthDown` from tick 2522 (overlapping dual rebirth)

First-stock rebirth halos at ticks 1614, 2110, and 2522 succeeded; failure required more KOs and a second concurrent halo mint.

## Root cause

`efManagerGetNextStructAlloc(TRUE)` failed because `sEFManagerStructsAllocFree == NULL` while `sEFManagerStructsFreeNum == 26` — the singly-linked EFStruct pool counter and free-list head were **out of sync** (same family as [netplay_quake_effect_pool_free](netplay_quake_effect_pool_free_2026-06-28.md), but without `ef_pool_free_reject` logs: silent struct leaks off the list rather than rejected double-frees).

Not pool exhaustion: 26 slots were nominally free; the list head was empty so `efManagerRebirthHaloMakeEffect` could not allocate userdata before `gcMakeGObj`.

## Fix

| Area | Change |
|------|--------|
| `efManagerNetAuditPool` + `efManagerNetRebuildFreeList` | Walk free-list length vs `free_num`, count in-use structs from live effect GObjs, rebuild the chain from pool base when mismatched; auto-retry in `efManagerGetNextStructAlloc` when `free_num>0` and head is NULL |
| `make_effect_fail` diag | Log `list_len`, `alloc_free`, `in_use`, `live_eff_gobjs` (`SSB64_NETPLAY_REBIRTH_GATE_DIAG=1`) |
| `syNetRbSnapForwardPruneStaleRebirthHalosForMint` | Forward-sim prune of rebirth halos whose fighter left lifecycle or has `is_effect_attach=0`; called from reclaim before mint |
| `syNetRbSnapReclaimStaleEffectShellsForRebirthHalo` | Runs forward prune + `efManagerNetAuditPool(TRUE, "rebirth_halo_reclaim")` |
| `ftCommonRebirthDownSetStatus` | Calls reclaim/audit prep before vanilla `efManagerRebirthHaloMakeEffect` (netmenu) |

Offline builds never compile the audit/repair path; decomp allocator semantics unchanged.

## Verify

Re-run `soak2` with `SSB64_NETPLAY_REBIRTH_GATE_DIAG=1` (+ existing synctest env). Expect:

- Kirby second-stock rebirth @~2532: `halo_effect_present=1`, no `make_effect_fail`
- If corruption still occurs: `pool_audit mismatch ... -> repair` then successful mint
- Synctest remains `fail=0`

## Audit hook

`make_effect_fail step=ef_struct` with `free>0` and no `step=gobj` = free-list head NULL while counter says slots available; walk pool in-use vs list length before blaming exhaustion. Overlapping dual `RebirthDown` is a good stress repro.
