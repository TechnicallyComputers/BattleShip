# Netplay guard shield zombie GObj UAF (fireball + shield)

**Date:** 2026-06-05  
**Status:** Fix shipped (soak pending)  
**Area:** Netmenu rollback / shield reconcile (`syNetRbSnapPruneStaleShields`)

## Symptoms

- SIGSEGV during netplay fireball + Mario shield testing (~sim tick 2039–2043).
- Host log: `guard_shield_prune reason=no_fighter` immediately followed by `gcEjectGObj DOUBLE-EJECT DETECTED` on effect GObj id **1011** (shield bubble).
- A few ticks later: `gobj_alloc` reuses the same address → crash (`fault_addr` in heap/Gfx range; STALE-DL diag in crash handler).
- Guest may survive until strict stall abort, then crash again when menu/transition allocates shield gobjs on corrupted free-list state.

## Root cause

1. **False `no_fighter` prune.** Shield bubbles can transiently have `ep->fighter_gobj == NULL` while `effect_vars.shield.player` still identifies a live fighter (fireball hit / guard teardown decouples the pointer without destroying the bubble). The prune path ejected instead of rebind-by-player-slot (Phase 24 owner model).

2. **Zombie list nodes after double-eject.** When vanilla or an earlier reconcile pass already ejected a GObj (`obj_kind = 0xFE`), a second reconcile eject hit `gcEjectGObj` DOUBLE-EJECT bail **without unlinking** the GObj from the Effect linked list. `syNetRbSnapEjectGObj` returned early on the sentinel but left the zombie in `gGCCommonLinks`. Pool reuse of that address left stale `link_next` refs → UAF / SIGSEGV.

## Fix

| Change | Purpose |
|--------|---------|
| `syNetRbSnapTryRebindOrphanShieldEffect` | Before `no_fighter` eject, rebind via `shield.player` when fighter is still shielding |
| `syNetRbSnapUnlinkSentinelGObjFromLists` | Scrub 0xFE gobjs still linked in Effect/SpecialEffect lists |
| `syNetRbSnapEjectGObj` | Call unlink helper when sentinel detected (no-op gcEjectGObj) |
| `syNetRbSnapPruneStaleShields` | Skip/rebind sentinels at loop head; post-teardown sentinel check before eject |

## Verify

- Rebuild netmenu; repeat Mario fireball vs shield soak past tick ~2040.
- Log should show `path=keep reason=no_fighter_rebind` instead of `no_fighter` + DOUBLE-EJECT clusters during active shield.
- Zero `DOUBLE-EJECT DETECTED` on gobj id 1011 during shield spam; no SIGSEGV after strict stall / session stop.
