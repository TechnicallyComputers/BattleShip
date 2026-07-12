# Netplay: Captain Falcon Dive sim canonicalize

**Date:** 2026-07-11  
**Scope:** `PORT && SSB64_NETMENU`  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)

## Symptom class

Falcon Kick already had `syNetplayCanonicalizeCaptainGroundKickSimState` (physics + `speciallw.vel_scale`
+ TopN + MPColl harden). Falcon Dive (SpecialHi / Catch / Throw / AirHi) uses `specialhi.vel` which
aliases the same union bytes as kick `vel_scale` — without Dive canonicalize, cross-ISA vel/pose
forks can persist through catch-up the same way Kick did before its fix.

## Fix

`port/net/sys/netplay_sim_quantize.c`:

- `syNetplayFighterInCaptainFalconDiveScope` — Captain/NCaptain SpecialHi*
- `syNetplayCanonicalizeCaptainFalconDiveSimState` — quantize physics, `specialhi.vel`, TopN
  translate/rotate.z, harden MPColl `pos_prev` / zero `pos_diff`
- Called from `syNetplayCanonicalizeFighterSimState` next to Kick

## Test plan

- [ ] Synctest / FORCE_MISMATCH resim during Falcon Dive; no Dive-window `figh` vel/TopN forks
      attributable to missing Dive grid snap.
- [ ] Control: Kick canonicalize still runs; Dive catch/throw still functions.
