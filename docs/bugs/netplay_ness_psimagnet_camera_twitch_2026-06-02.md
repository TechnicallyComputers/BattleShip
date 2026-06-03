# Netplay — Ness PSI Magnet (Down+B) camera twitch cross-ISA (2026-06-02)

**Date:** 2026-06-02  
**Status:** Fix shipped (soak pending)  
**Area:** `port/net/sys/netplay_sim_quantize.c`, `decomp/src/ft/ftchar/ftness/ftnessspeciallw.c`, `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netsync.c`

## Symptom

Cross-ISA Ness×Ness soak: visible **camera twitch on first Down+B (PSI Magnet)**. Trim log @ tick 150: first `effect_count 0→1` (Psychic Magnet VFX), `cam=` hash swings every frame during magnet hold while fighter light hash stayed flat. Not PK Thunder / jibaku (`status=233` events occur much later).

## Root cause

PSI Magnet (status 237–238) runs full fighter anim + parent-attached Psychic Magnet effect (`gcPlayAnimAll` on TopN joint) while battle camera pans on unquantized joint bounds. Unlike PK Thunder Hold, SpecialLw had no end-of-tick pose canonicalize, no effect-hash coupling fold, and no rollback respawn path for the magnet shell.

Global `syNetplayCanonicalizeGMCameraSimState` ran only after all fighters at tick end — camera updated mid-tick on raw anim joints during absorb.

## Fix

| Layer | Change |
|-------|--------|
| **Scope** | `syNetplayFighterInNessSpecialLwSimScope` (status 237–246) |
| **Live sim** | `syNetplayCanonicalizeNessSpecialLwSimState` — full joint anim pose + Psychic Magnet effect anim; camera canonicalize when `is_absorb` |
| **Fighter procs** | Hold/Hit ProcUpdate call canonicalize (mirrors PK Thunder Hold) |
| **Effect hash** | `syNetplayLiveEffectIsNessPsychicMagnet` — status-gated fold (skip free translate) |
| **Rollback** | `SYNETRB_EFFECT_RESPAWN_NESS_PSYCHIC_MAGNET`, ensure/prune, joint blob quantize in SpecialLw scope |

## Verification

1. Cross-ISA: first Down+B — stable camera framing during magnet hold; `cam=` stops frame-to-frame whips in trim log.
2. Rollback load during magnet: Psychic Magnet effect respawns; no orphan `effect_count` drift.
3. PK Thunder / PK wave effects unchanged (joint-5 wave vs TopN magnet distinguished by status scope).

## Related

- [`netplay_camera_quantize_2026-06-02.md`](netplay_camera_quantize_2026-06-02.md)
- [`netplay_ness_pkthunder_hold_quantize_2026-06-02.md`](netplay_ness_pkthunder_hold_quantize_2026-06-02.md)
