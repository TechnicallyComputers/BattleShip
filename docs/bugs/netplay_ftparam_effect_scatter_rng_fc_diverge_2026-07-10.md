# Netplay ftParam motion-event effect scatter RNG — FRAME_COMMIT diverge — 2026-07-10

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)  
**Scope:** `decomp/src/ft/ftparam.c`, `port/net/sys/netrollbacksnapshot.c` (remove misleading `rng_cosmetic_split` spam)

## Symptom

soak2 session `2000458514` / seed `2533079915` (Android client / Linux host):

```
FRAME_COMMIT_STATE_DIVERGE validation=3070
  figh+world+item+eff MATCH, inputs MATCH
  diverged=rng only
  Linux  rng=0x7B083E7C  game_seed=0xEF3A58D1  cosmetic=0xEF3A58D1
  Android rng=0x958C498F  game_seed=0xBC821850  cosmetic=0xEF3A58D1
```

`0xEF3A58D1 → 0xBC821850` is exactly one LCG step. Fork lands on tick **3046** when player 0 (Ness, fkind=10) exits rebirth status 9 into Appear (status 26 / motion 20): halo `gcEjectGObj`, `ftMainSetStatus` plays motion-script effect events.

`rng_hash_walk` @3069: Android `ring_steps=1` from archived tick 3046; Linux had no gameplay draw over the same window.

## Root cause

`ftParamMakeEffect()` still used raw `syUtilsRandFloat()` for:

1. Motion-script `effect_scatter` jitter (`rng_x/y/z` fields on `nFTMotionEventEffect`)
2. `nEFKindDustExpandLarge` position fuzz

Those are pure VFX placement. When Appear motion events fire asymmetrically across peers (or one peer runs an extra scatter axis), the gameplay LCG advances on one side only while `figh`/`eff` stay matched — same class as `docs/bugs/netplay_effect_vfx_forced_cosmetic_rng_2026-07-09.md`, but outside `efmanager.c` / `lbparticle.c`.

The `rng_cosmetic_split` ring-save diagnostic was misleading: cosmetic≠game is **expected** under `ForcedCosmetic` forward sim and logged ~1800 lines per peer per soak.

## Fix

| Change | Purpose |
|--------|---------|
| `ftParamEffectJitterRand()` → `syUtilsRandFloatForcedCosmetic` under `SSB64_NETMENU` | Scatter / dust jitter never burns hashed gameplay seed |
| Remove `rng_cosmetic_split` ring-save log | Expected cosmetic split is not a defect signal |

## Verification

- Re-soak session `2000458514` profile: FC @3070 must not report `diverged=rng` with matched inputs.
- Ness rebirth→Appear window (~3046) should keep `hash_rng=0x7B083E7C` on both peers.
- `rng_finalize_heal` should not fire on clean forward sim after rollback load @2950.
