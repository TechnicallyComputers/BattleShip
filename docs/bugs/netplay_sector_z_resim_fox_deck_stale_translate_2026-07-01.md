# Sector Z resim: Fox slides on Great Fox deck after rollback load

**Date:** 2026-07-01  
**Scope:** `decomp/src/gr/grcommon/grsector.c`, `port/net/sys/netrollbacksnapshot.c`  
**Status:** FIX SHIPPED — soak pending (Sector Z cross-ISA resim)

## Symptoms

Sector Z cross-ISA soak (`AUTOMATCH_STAGE_KIND=1`): after frame-commit resim (load tick 360 → replay 361–481), Fox (P1) on Great Fox platform (`floor_line=1`, cliff flags) **teleports ~769 units** while in grounded SpecialN. Pikachu unaffected. Both peers show identical post-resim state (deterministic replay divergence, not peer desync).

## Root cause

1. **`mp_yaku[1]` skip on deck-derived apply** — `syNetRbSnapApplyMap` intentionally skips restoring yakumono line 1 when `syNetRbSnapSectorArwingDeckYakumonoDerivedFromSlot` is true (Arwing patrol). Translate stays at **pre-load live** position (~tick 480) while `syNetRbSnapApplyArwing` restores the flight tree to the **load tick** (360).

2. **Bogus platform speed** — `grSectorArwingUpdateCollisions` → `mpCollisionSetYakumonoPosID` derives `gMPCollisionSpeeds[1]` as `(new_pos − old_translate)`. The stale anchor produces large speeds (~42 u/frame). `ftmain` adds `vel_speed` each tick for grounded fighters (`ga == Ground`), sliding Fox during SpecialN.

3. **Forward sim unaffected** — never loads without restoring yaku; deck translate and flight tree stay aligned. Resim replay also diverges Dash→Walk at tick 392 (secondary), but the visible ~769 u slide is from deck carry, not status alone.

## Fix

In `grSectorArwingReconcileDeckYakumonoFromFlightTree` (`PORT && SSB64_NETMENU`, rollback semantics only): when line 1 is live (`line_active && z_near`) and translate differs from the flight-tree position by more than one patrol frame (~45 u), **snap yakumono translate** to the reconciled pos and **zero `gMPCollisionSpeeds[1]`** before `UpdateCollisions` runs. Normal per-frame carry (≤~42 u delta) is unchanged.

## Test plan

1. Re-run Sector Z cross-ISA soak with forced resim (`FORCE_MISMATCH` @ tick 520): Fox stays at pre-resim platform position through SpecialN; no ~769 u slide at resim complete.
2. Deck carry unchanged: fighter standing on patrolling Arwing hull still inherits lateral motion when deck is actually moving.
3. `fighter_cargo_diag` during resim SpecialN: `vg_x/vg_y ≈ 0` when deck translate is stable (matches forward-sim soak2 ticks 400–479).
