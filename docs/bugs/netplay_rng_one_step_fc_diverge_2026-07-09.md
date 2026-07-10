# Netplay gameplay RNG one-step FC diverge (Android client) — 2026-07-09

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)  
**Scope:** `decomp/src/sys/utils.c`, `port/net/sys/netsync_rng_trace.c`, `port/net/sys/netpeer.c`, `port/net/sys/netreplay.c`, `port/net/sys/netrollbacksnapshot.c`

## Symptom

soak2 session `1492680620` / seed `1381809416` (Android client / Linux host, Dream Land, post-PK-Fire fix):

```
FRAME_COMMIT_STATE_DIVERGE validation=1937
  figh+world+item+eff MATCH, inputs MATCH
  diverged=rng only
  Linux  rng=0x70D8EA28  game_seed=0x68F4FBFD  cosmetic_seed=0x68F4FBFD
  Android rng=0x8D8C872B  game_seed=0xCC2DDECC  cosmetic_seed=0x68F4FBFD
```

`rng_hash_walk` reported `count=0` / `archived_tick=1831` because `syUtilsRandFloat()` advanced the gameplay LCG without entering the step ring (only `syUtilsRandUShort` was traced).

PK Fire item respawn is **not** implicated (`item_count=0` at diverge; last synctest OK @1829).

## Root cause

Android's **gameplay** seed was exactly **one LCG step ahead** of Linux (`0x68F4FBFD → 0xCC2DDECC`) while **cosmetic** stayed pinned at `0x68F4FBFD`. That split pattern matches a single untraced `syUtilsRandFloat()` draw burning the hashed gameplay stream on one peer only — the same class as `docs/bugs/netplay_effect_vfx_forced_cosmetic_rng_2026-07-09.md`, but with only one draw over ~90 ticks of idle forward sim (1830–1920).

Likely trigger: one asymmetric VFX/particle jitter path still mapped through `syUtilsRandFloatCosmetic()` → `syUtilsRandFloat()` on a peer build missing the `efmanager`/`lbparticle` `ForcedCosmetic` macros, or a remaining non-VFX `syUtilsRandFloat` site.

## Fix

| Change | Purpose |
|--------|---------|
| `syUtilsRandFloat()` trace hooks (`utils.c`) | `rng_hash_walk` / step ring see float draws on FC rng diverge |
| `rng_hash_walk` archived-tick selection (`netsync_rng_trace.c`) | Use last archived tick `<= sim_tick` (not only `== sim_tick`) |
| Bootstrap/replay cosmetic pin (`netpeer.c`, `netreplay.c`) | `syUtilsResetCosmeticRandomSeed` after every `syUtilsSetRandomSeed` |
| `rng_finalize_heal` + `rng_cosmetic_split` logs (`netrollbacksnapshot.c`) | Surface game/cosmetic desync at load finalize and ring save |

**Update 2026-07-10:** `rng_cosmetic_split` removed — cosmetic≠game is expected under `ForcedCosmetic`. See `docs/bugs/netplay_ftparam_effect_scatter_rng_fc_diverge_2026-07-10.md` for soak2 @3070 root cause (`ftParamMakeEffect` scatter).

**Re-soak requirement:** rebuild **both** peers from the same commit so Android picks up `efmanager`/`lbparticle` `ForcedCosmetic` macros from `netplay_effect_vfx_forced_cosmetic_rng_2026-07-09.md`.

## Verification

- Re-soak session `1492680620` profile: FC @1937 must not report `diverged=rng` with matched inputs.
- On any future rng-only FC fail, `rng_hash_walk` should show `count>0` when the fork used `syUtilsRandFloat`.
- `rng_cosmetic_split` / `rng_finalize_heal` must not fire during clean forward sim.
