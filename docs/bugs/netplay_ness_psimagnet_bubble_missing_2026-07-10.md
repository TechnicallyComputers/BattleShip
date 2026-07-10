# Netplay: Ness PSI Magnet bubble missing under synctest soak

**Date:** 2026-07-10  
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android soak2  
**Match:** Captain Falcon vs Ness — determinism PASS, presentation gap during Down+B  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptom

With SYNCTEST enabled, Ness Down+B (PSI Magnet) shows fighter absorb glow + loop sfx but **no
Psychic Magnet bubble** animating around him. Offline / non-netmenu shows the bubble normally.

## Log evidence (soak2-linux.log @ tick 2728–2771)

| Observation | Detail |
|-------------|--------|
| Input | `SpecialLwCheck -> PASS` @2728, status **237** Start → **238** Hold @2742 |
| Effect snapshot | `effect save tick=2742..2859 effect_count=0` entire hold window |
| Effect hash | `eff=0x811C9DC5` (empty) on every tick while status=238 |
| Gameplay | `is_absorb` / absorb coll still active — magnet **gameplay** without VFX shell |

The bubble is not “frozen” — the `efManagerNessPsychicMagnetMakeEffect` GObj was **never live**
during forward sim.

## Root cause

Vanilla `ftNessSpecialLwInitVars` only calls `efManagerNessPsychicMagnetMakeEffect` when
`fp->is_effect_attach == FALSE`. Netplay can leave `is_effect_attach` TRUE from prior coupled
effects (rebirth halo presentation, PK wave attach, snapshot blob restore) **without** a live
Psychic Magnet shell. InitVars then skips mint; absorb state still runs from the same function.

Secondary: `syNetRbSnapFinalizeFighterEffectAttachFlags` kept attach for rebirth halo / PK wave /
Captain kick / Pikachu shock but **not** Psychic Magnet, so snapshot apply could clear
`is_effect_attach` after `EnsureNessPsychicMagnetEffectsFromSlot` minted the bubble.

## Fix

1. **`port/net/sys/netplay_sim_quantize.c`** — `syNetplayEnsureNessPsychicMagnetEffect`: when in
   Hold/Hit magnet scope and no live magnet exists, mint the bubble regardless of stale
   `is_effect_attach`. Called from `syNetplayCanonicalizeNessSpecialLwSimState` (every Hold
   ProcUpdate tick) and `ftNessSpecialLwInitVars` (Hold entry).

2. **`port/net/sys/netrollbacksnapshot.c`** — extend `FinalizeFighterEffectAttachFlags`
   `keep_attach` for `syNetRbSnapFighterBlobNessPsychicMagnetAttachPending` and live magnet
   presence (mirrors Captain kick / PK wave pattern).

## Test plan

- [ ] Re-soak Captain vs Ness: Down+B hold shows animating Psychic Magnet bubble on both peers.
- [ ] Grep hold window for `effect_count>=1` or non-empty `eff` hash while status=238.
- [ ] Offline Down+B unchanged (ensure gated on `syNetplayRollbackSemanticsActive`).
- [ ] Synctest restore during magnet: bubble respawns via existing `EnsureNessPsychicMagnetEffectsFromSlot`.
