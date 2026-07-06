# Netplay: Kirby inhale-wind SIGSEGV on synctest verify load — 2026-07-04

**Status:** FIX IMPLEMENTED (round 6 — proc_dead after decoupled effect_gobj, soak pending)  
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android soak2  
**Session:** `195770602` / `794094749` @1710; **`1704028704`** @2429 (inhale loop capture); soak2 tick **~571** (forward spawn after copy eject)

## Symptom

Drift scan **PASS** (31 synctest OK, 0 FAIL, 330 `intro_wait` skips). Both peers
`SIGSEGV fault_addr=0xa` at guest `max_sim_tick=4109` on the **32nd** periodic synctest
probe (tick 4110, sentinel `tick=4294967295`).

Last log before crash:

```
B_apply_fighter_end tick=4110 player=1 fkind=8 status=270 motion=245  (Kirby SpecialNLoop)
effect save tick=4110 effect_count=0
```

Registers: `x0=x1=0x7fecd5f4ea30` (same ptr as `gobj_alloc id=1011 link=6` at tick 4107),
`x2=0x10e` (270 = `nFTKirbyStatusSpecialNLoop`).

## Root cause

The inhale wind is a **cosmetic** excluded from the effect snapshot
(`netplay_kirby_inhale_wind_cosmetic_rollback_exclusion_2026-07-03.md`). Forward sim
spawned a live wind shell at tick **4107** (`efManagerKirbyInhaleWindMakeEffect` on recycled
`gobj_id=1011`) while the ring slot captured at **4110** has `effect_count=0`.

Synctest verify calls `syNetRbSnapApplySlotToLive`, which applies fighter blobs while the
live frontier wind GObj remains on link 6. `syNetRbSnapPruneStaleKirbyInhaleWindEffects`
runs only **after** fighter apply, rebind, shield teardown, map apply, and particle reset
(~line 33118) — too late. Mid-apply reconcile walks the stale shell (`obj=nil` particle
infrastructure pattern from prior eject/recycle) and dereferences through `DObjGetStruct`
→ `fault_addr=0xa`.

This is the expected failure mode after removing the blanket `kirby_specialn_inhale` synctest
defer: synctest now exercises the inhale window, exposing a load-order gap rather than a new
`figh` divergence (drift scan stayed clean).

## Fix

`port/net/sys/netrollbacksnapshot.c`:

- Add `syNetRbSnapEjectLiveKirbyInhaleWindBeforeFighterApply` — unconditionally eject every
  live GObj carrying `efManagerKirbyInhaleWindProcUpdate` on both effect links.
- Call it at the top of `syNetRbSnapApplySlotToLive` before the fighter apply loop.
- **Round 2 (`1704028704` @2429):** also call at the start of `syNetRbSnapFillSlotFromLive`
  (before capture) and again after `syNetRbSnapshotPrepareMapStateForHash` / `map_hash_save`
  (before tail `hash_animation` folds). Synctest calls `syNetRbSnapshotCaptureLiveEmergency`
  immediately after the ring save for tick N+1; the second capture pass runs map reconcile
  between `hash_fighter` and `hash_animation` while Kirby holds `SpecialNLoop` and a live wind
  shell remains on recycled `gobj_id=1011` → `SIGSEGV fault_addr=0xa` with `effect save
  tick=4294967295` (not apply — crash is before `syNetRbSnapshotLoad`).
- Call again in `syNetRbSnapApplySlotToLive` after fighter apply/rebind and before
  `syNetRbSnapApplyMap` (defense if apply re-couples before map/particle reconcile).

Wind is never snapshotted; `ftKirbySpecialNLoopProcUpdate` re-spawns it on the next forward
tick when `!is_effect_attach && motion_vars.flags.flag0==1`. Matches verify fold
(`effect_count=0`).

Existing late prune/sweep guards remain as defense in depth.

Orphan guard (session `794094749` @1710): `syNetRbSnapEjectKirbyInhaleWindEffectGObj` skips
`lbParticleFindStructForEffectGObj` teardown when `ep`, `xf`, or owner fighter coupling is broken
and uses direct `gcEjectGObj` instead — full particle destroy on `obj=nil` shells SIGSEGV
`fault_addr=0xa` during pre-apply eject.

Verify-finalize sweep (session `1836363854` @2429, **`139215096` @1709**): hidden-cosmetic sweep plus
`syNetRbSnapEjectStaleParticleCouplingEffectsForVerify` and hardened `syNetRbSnapEjectGObj` (live-xf check +
pool-safe `lbParticleFindStructForEffectGobj`) so copy-eject/re-inhale cycles cannot leave orphan or
slot-authoritative shells on recycled `gobj_id=1011` for enforce/rebind to SIGSEGV (`fault_addr=0xa`).
See `docs/bugs/netplay_kirby_reinhale_copy_eject_verify_sigsegv_2026-07-04.md`.

## Round 3 — forward sim spawn @571 (copy eject → empty inhale)

**Symptom:** Both peers `SIGSEGV fault_addr=0xa` at tick **571** during Kirby
`SpecialNLoop` (270) — **not** on synctest verify. Copy Fox lost at tick ~486; Kirby idle
at 546; empty re-inhale at 551; first sustained inhale-loop wind spawn at 571
(`efManagerKirbyInhaleWindMakeEffect`, `gobj_alloc id=1011`). `effect_count=0` throughout;
last `SYNCTEST_OK` = tick 389.

**Root cause:** Rounds 1–2 only hardened verify/capture apply order. Forward sim still hit
stale `pc->xf->effect_gobj` on recycled `gobj_id=1011` from copy FX / landing FX / verify
eject churn. `efManagerKirbyInhaleWindMakeEffect` never set `xf->effect_gobj` (unlike most
MakeEffect paths), so `lbParticleFindStructForEffectGobj` / pool walks could couple the new
shell to an unrelated live transform → `DObjGetStruct` on `fault_addr=0xa`.

**Fix:**

- `decomp/src/lb/lbparticle.c` — `lbParticleClearStaleEffectGobjCoupling(effect_gobj)`:
  netmenu-only pool walk; clear `xf->effect_gobj` on allocated transforms matching the new
  GObj address before wiring.
- `decomp/src/ef/efmanager.c` — in `efManagerKirbyInhaleWindMakeEffect`: call scrub after
  `gcMakeGObjSPAfter`; after `lbParticleAddTransformForStruct`, set
  `xf->effect_gobj = effect_gobj` and `xf->proc_dead = efManagerDefaultProcDead` (match other
  effects).

## Round 4 — forward bare eject + empty inhale @441 (soak2, no copy)

**Symptom:** Round 3 insufficient. Soak2 still `SIGSEGV fault_addr=0xa` on **first** empty inhale
(SpecialNStart tick 422 → SpecialNLoop tick 441). Prior `gobj_id=1011` use: jump squat FX tick 419,
`gcEjectGObj` tick 435 during SpecialNStart — no copy, no synctest probe.

**Root cause:** Round 3 scrub only cleared `xf->effect_gobj` on **allocated** transforms. Forward
`gcEjectGObj` (most cosmetic teardown) never ran particle destroy; stale couplings survived on
free-list transforms and on live structs that no longer passed `lbParticleTransformIsAllocated`.

**Fix:**

- `decomp/src/lb/lbparticle.c` — broaden `lbParticleClearStaleEffectGobjCoupling`: walk all particle
  struct xforms (no alloc gate), transform free list, and queued generators.
- `decomp/src/ef/efmanager.c` — `efManagerNetplayTeardownParticleCouplingBeforeForwardEject`:
  eject owning particle without `proc_dead` re-entering `gcEjectGObj`, full pool scrub, clear
  `ep->xf`.
- `decomp/src/sys/objman.c` — call teardown at start of `gcEjectGObj` for effect links (6/8).

## Round 5 — `syNetRbSnapEjectGObj` item misroute (soak2 @431, symbolicated)

**Symptom:** Rounds 3–4 insufficient. Soak2 Linux + Android still `SIGSEGV fault_addr=0xa` on first
empty inhale at tick **431** (`gobj_alloc id=1011 link=6` immediately before crash). No synctest
probe; `effect save effect_count=0`.

**Root cause (symbolicated against `build-bundle-linux-netplay-us/BattleShip`):** crash PC is
`syNetRbSnapEjectItemGObjForRollback` line 1383 (`ip->owner_gobj->id`), not particle/DObj code.
Capture-time inhale-wind eject calls `syNetRbSnapEjectGObj`, which since 2026-06-12 routed any
GObj with non-NULL `user_data.p` through the item path via bare `itGetStruct(gobj) != NULL`.
Fresh effect shells (`nGCCommonKindEffect` / 1011) always pass — `EFStruct*` is reinterpreted as
`ITStruct*`, garbage `owner_gobj` → `fault_addr=0xa`. Particle-coupling fixes never ran on this
path.

**Fix:**

- `port/net/sys/netrollbacksnapshot.c` — gate item eject on
  `(gobj->id == nGCCommonKindItem) && (itGetStruct(gobj) != NULL)` so effect/weapon shells reach
  the existing netmenu effect eject branch below.

## Round 6 — synctest verify proc_dead after inhale (soak2 @510)

**Symptom:** Round 5 fixed forward empty-inhale spawn. New crash on **synctest verify apply** at tick **510**
(both peers): `SIGSEGV fault_addr=0xe0` in `efManagerDefaultProcDead+0x7`, after
`B_apply_fighter_end` for Kirby `SpecialNStart` (269) on second neutral-B inhale. Emergency capture
`tick=4294967295`.

**Root cause (symbolicated):** disasm at `+0x7` is `mov 0xe0(%rdi),%rax` with `rdi=effect_gobj` after
`effect_gobj` was loaded NULL — `efGetStruct(NULL)` dereferences `GObj.user_data.p`. Round 3
`lbParticleClearStaleEffectGobjCoupling` cleared `xf->effect_gobj` on pool transforms but left
`xf->proc_dead = efManagerDefaultProcDead`. `syNetRbSnapResetParticlesForRollback` →
`lbParticleEjectStructAll` → `lbParticleEjectTransform` invokes proc_dead on a decoupled transform.

**Fix:**

- `decomp/src/lb/lbparticle.c` — `lbParticleClearEffectGobjCouplingOnTransform`: also null
  `xf->proc_dead` when severing `effect_gobj`.
- `decomp/src/ef/efmanager.c` — `efManagerDefaultProcDead`: netmenu guard for NULL `xf` /
  `effect_gobj` before `efGetStruct` / `gcEjectGObj`.

## Verify

- `cmake --build build-netmenu --target ssb64 -j 4` — links clean.
- Re-soak session `195770602` path: Kirby neutral-B inhale through tick ≥4110 — expect zero
  `SIGSEGV`, synctest OK count continues past probe 32.
- Re-soak session `1704028704` path: hold Kirby inhale loop through probe **2429** — expect
  `SYNCTEST_OK` (not SIGSEGV on emergency capture `tick=4294967295`).
- Re-soak copy-eject → empty inhale path (tick ~571): Kirby copy Fox, lose copy, neutral-B
  empty inhale ~1 s — expect zero `SIGSEGV` on first wind spawn.
- Re-soak minimal empty inhale (tick ~441): Kirby neutral-B from idle, no copy — expect zero
  `SIGSEGV` on first wind spawn.
- Re-soak inhale through verify probe (tick ~510): hold/release neutral-B, synctest verify apply —
  expect zero `SIGSEGV` in `efManagerDefaultProcDead` (`fault_addr=0xe0`).
- Drift scan still PASS; no `LOAD_HASH_DRIFT[eff]` from inhale wind.
