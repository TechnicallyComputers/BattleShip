# ShockSmall cosmetic RNG → FRAME_COMMIT rng diverge — 2026-07-08

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending) — superseded for all VFX by file-scope `ForcedCosmetic` in `efmanager.c`/`lbparticle.c`; see `docs/bugs/netplay_effect_vfx_forced_cosmetic_rng_2026-07-09.md`.  
**Scope:** `decomp/src/sys/utils.c`, `decomp/src/sys/utils.h`, `decomp/src/ef/efmanager.c`

## Symptom

soak2 session `163251495` / seed `2994873914`: drift scan **FAIL** at frame-commit validation **600** with matching inputs:

```
FRAME_COMMIT_STATE_DIVERGE validation=600
  local/peer figh+world+item+eff MATCH
  diverged=rng
  Linux  rng=0xFAFA8E2F  game_seed=0x62000FB0
  Android rng=0x40AFEE06 game_seed=0xAE7A2867
  cosmetic_seed identical (0xC05DE9E1)
```

`LOAD_HASH_DRIFT=0`, synctest OK. FORCE_MISMATCH@520 resim completed with both peers at `rng=0xDE09D4AC`.

## Root cause

1. Post-resim baseline agrees; Linux first advances gameplay RNG at tick **523** (+5 LCG draws).
2. By FC600: Linux took **17** gameplay draws → `0xFAFA8E2F`; Android took **22** → `0x40AFEE06` (**Δ = 5**).
3. `efManagerShockSmallMakeEffect` performs **exactly five** `syUtilsRandFloat()` draws (pos×2, discarded angle, scale, rotate). File-scope `#define syUtilsRandFloat syUtilsRandFloatCosmetic` still **redirects to the shared game seed on forward sim** (`IsResimulating == FALSE`).
4. Post-resim `gobj_alloc id=1011 link=6`: Android **4** vs Linux **3** — one extra ShockSmall on Android. Effect is excluded from `eff` hash, so only `rng` diverges.

Stage = Dream Land (`gkind=6`), `item_count=0` — not Castle bumper float drift.

## Fix

| Change | Purpose |
|--------|---------|
| `syUtilsRandFloatForcedCosmetic` | Always steps `sSYUtilsCosmeticRandomSeed` only |
| `efManagerShockSmallMakeEffect` | Use ForcedCosmetic for all five draws under netmenu |

Asymmetric VFX spawn count may still differ (figatree/ACMD residual) but no longer forks the hashed game seed. Longer-term: bisect why one peer mints an extra electric spark after DownWait/Fall.

## Verification

- Re-soak with FORCE_MISMATCH@520: FC600 must not report `diverged=rng` with matched inputs.
- Optional: `SSB64_NETPLAY_RNG_STEP_TRACE=1` + tick window 520–540 — ShockSmall steps should not appear as game-seed draws.
