# Effect/particle VFX forced-cosmetic RNG — FRAME_COMMIT rng diverge — 2026-07-09

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)  
**Scope:** `decomp/src/ef/efmanager.c`, `decomp/src/lb/lbparticle.c`, `decomp/src/sys/utils.c`, `decomp/src/sys/utils.h`, `port/net/sys/netsync_rng_trace.c`

## Symptom

soak2 session `1511799209` / seed `841032191` (Linux guest / Android host, Dream Land):

```
FRAME_COMMIT_STATE_DIVERGE validation=1320
  figh+world+item+eff MATCH, inputs MATCH
  diverged=rng only
  Linux  rng=0x773B9741  game_seed=0x4B0D8F7D
  Android rng=0x9703D776  game_seed=0xAF6A51B7
FRAME_COMMIT_INPUT_AGREE_REANCHOR last_agreed=1200 mismatch=1201
```

RNG consumption fork visible on Linux forward sim @1197/1201/1205 with asymmetric `gobj_alloc id=1011` bursts (kick-window VFX). `rng_hash_walk` returned `count=0` because step archival required env flags.

## Root cause

Per-effect ShockSmall override (2026-07-08) fixed one consumer, but **all** `efmanager.c` / `lbparticle.c` jitter still mapped `syUtilsRandFloat` → `syUtilsRandFloatCosmetic()`, which burns the **hashed gameplay LCG** on forward sim. Asymmetric cosmetic effect spawn counts (Captain kick flame, dust, ShockSmall, …) therefore fork `rng` while `figh`/`eff` stay matched — identical class to `docs/bugs/netplay_shocksmall_cosmetic_rng_fc_diverge_2026-07-08.md`.

## Fix

| Change | Purpose |
|--------|---------|
| `efmanager.c` / `lbparticle.c` file-scope macros under `SSB64_NETMENU` | Remap to `syUtilsRandFloatForcedCosmetic` / `syUtilsRandIntRangeForcedCosmetic` for all VFX jitter |
| `syUtilsRandIntRangeForcedCosmetic` | IntRange twin of forced-cosmetic float API |
| Remove ShockSmall per-function `#undef` override | Redundant after file-scope policy |
| `netsync_rng_trace.c` | Always ring-buffer gameplay LCG steps during active netplay VS so `rng_hash_walk` on FC rng diverge dumps the prior tick without `SSB64_NETPLAY_RNG_HASH_TRACE` |

Pure VFX asymmetry may still differ visually and can surface in `eff` when an effect is folded into the hash; it no longer poisons the shared gameplay seed.

## Verification

- Re-soak session `1511799209` profile: FC1320 must not report `diverged=rng` with matched inputs.
- On any future rng-only FC fail, `rng_hash_walk` should show `archived_tick=<validation-1>` and `count>0` without env flags.
- Optional: `SSB64_NETPLAY_RNG_STEP_TRACE=1` window 1195–1210 — effect-manager draws should not appear as gameplay `rng_step` lines.
