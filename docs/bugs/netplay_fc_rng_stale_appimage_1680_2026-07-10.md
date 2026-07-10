# Netplay FC rng @1680 — stale soak binary (ForcedCosmetic already in tree)

**Date:** 2026-07-10  
**Scope:** soak redeploy (no new code)  
**Soak:** session `1703284357` / seed `4223357131` (Pikachu vs Kirby, Dream Land)  
**Status:** KNOWN CLASS — redeploy Jul 10+ netplay AppImage/APK; re-soak pending

## Clarification

| Signal | Result |
|--------|--------|
| Synctest | **11 OK, 0 FAIL** — not a synctest failure |
| `LOAD_HASH_DRIFT` @1560 `diverged=eff` | Soft-continued (`resim-sim-core-ok`); figh/rng matched |
| `FRAME_COMMIT_STATE_DIVERGE` @1680 | **`diverged=rng` only**, inputs MATCH, figh/world/item/eff MATCH |

## Evidence @1680

```
Android: rng=0x9D783A63  game_seed=0xDBBD3834  cosmetic=0x21472525  archived_tick=1388
Linux:   rng=0x1AFCA4C6  game_seed=0x5878C627  cosmetic=0x21472525  archived_tick=1679
```

`0xDBBD3834 → 0x5878C627` is exactly **one** MS-VC LCG step (`*214013 + 2531011`).  
Linux advanced the hashed gameplay seed during sim tick **1679**; Android did not. Cosmetic seeds stayed identical.

Game state at the fork: P0 Pikachu `SpecialAirLwHit` (230), P1 Kirby `DamageFlyHi` (51). `effect_count=0` on both peers at the capture — VFX burned RNG without a lasting hashed effect shell.

## Root cause

Same class as:

- [netplay_effect_vfx_forced_cosmetic_rng_2026-07-09.md](netplay_effect_vfx_forced_cosmetic_rng_2026-07-09.md)
- [netplay_ftparam_effect_scatter_rng_fc_diverge_2026-07-10.md](netplay_ftparam_effect_scatter_rng_fc_diverge_2026-07-10.md)

Linux soak peer still ran `/mnt/raid0/Software/BattleShip/BattleShip-Netplay-x86_64.AppImage` dated **2026-06-27** (md5 `2b05e604…`). That binary predates file-scope `ForcedCosmetic` in `efmanager`/`lbparticle` and `ftParamEffectJitterRand`. A newer package already exists at `dist/BattleShip-Netplay-x86_64.AppImage` (**2026-07-10**, md5 `1d30365f…`) but was not copied to the soak path.

On the Jun 27 build, asymmetric / presentation-only VFX still maps through `Cosmetic()` → gameplay LCG on forward sim, forking `rng` while `figh`/`eff` stay matched.

## Action

1. Deploy matching Jul 10+ netplay builds to **both** peers:
   ```bash
   cp dist/BattleShip-Netplay-x86_64.AppImage /mnt/raid0/Software/BattleShip/
   # + matching Android netplay.debug APK from the same tree
   ```
2. Re-soak. Expect no `FRAME_COMMIT … diverged=rng` with matched inputs through Thunder / damage-fly VFX windows.
3. If rng-only FC still appears on the **new** binary, enable `SSB64_NETPLAY_RNG_STEP_SITE=1` and re-capture — that is a new remaining site, not this redeploy gap.
