# Netplay intro resim presentation (Phase 41)

**Date:** 2026-06-10  
**Status:** FIX SHIPPED (re-soak pending)  
**Soak:** soak2 Yoshi's Island @239 dual-appear resim (Yoshi AppearR + Kirby AppearL)

## Symptom

Phase 40 fixed structural resim (baseline `map` hash, `resim complete` cross-ISA). Visible intro rollback on Yoster still showed camera/figatree drift during the resim burst:

- `RESIM_ANCHOR_PROBE` @239→240: `match_cam=0` (Peach soak1 had `match_cam=1`)
- `resim complete`: `cam=0x2E1CC28C/0x119BF93C` drift over 2-tick burst
- Kirby `entry_lr=-1` looked like a repair failure in diags but is correct for AppearL

## Root cause

1. **Camera re-pin too late** — `syNetRbSnapRestoreIntroCameraPresentationFromSlot` ran only at replay-gate `post_cosmetic`, after baseline verify. Load/anchor-probe prep and forward-resim ticks left live camera diverged from slot blob during intro.
2. **No per-tick figatree refresh during forward resim** — gate cosmetic ran before the burst; sim advanced Appear physics without refreshing figatree DLs for the 2-tick replay window.
3. **Diag ambiguity** — `spawn_lr_blob=-1` conflated sidecar vs infer; AppearL `-1` read as failure.

## Fix (Phase 41)

| Item | Change |
|------|--------|
| P41a | Re-pin intro camera from load slot in `PrepareLoadedSlotForVerify`, `RefreshPresentationForLoadedTick`, and `ResyncLiveFightersFromSlotForSim` (before anchor-probe +1 sim) |
| P41b | `syNetRbSnapshotRefreshIntroPresentationAfterForwardResimTick(load_tick)` after each forward-resim sim tick: cosmetic camera re-pin + `ftMainRefreshFigatreeVisual` for Appear fighters |
| P41c | `appear_presentation_diag`: `appear_facing=AppearR\|AppearL\|none`, `spawn_lr_captured`, `spawn_lr_sidecar` |

## Diagnostics

- `SSB64_NETPLAY_SNAPSHOT_APPEAR_PRESENTATION_DIAG=1` — updated appear lines
- `SSB64_NETPLAY_CAMERA_LOAD_DIAG=1` — `intro_camera_cosmetic_diag` on each camera re-pin

## Verify

Re-run soak2 (Yoshi vs Kirby, Yoshi's Island, inject @240). Expect:

- `appear_presentation_diag` Kirby: `appear_facing=AppearL spawn_lr_captured=1 spawn_lr_sidecar=-1 entry_lr=-1`
- Improved camera stability through anchor probe and resim burst (visual; sim hash cam drift post-resim may still occur from input correction)
