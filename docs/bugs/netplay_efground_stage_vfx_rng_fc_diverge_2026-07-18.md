# Netplay efGround stage ambient VFX RNG — FRAME_COMMIT diverge — 2026-07-18

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)  
**Scope:** `decomp/src/ef/efground.c`, `decomp/src/sys/utils.h`, `port/net/sys/netsync_rng_trace.c`

## Symptom

soak1 session `736365822` / seed `4158472547` (Android client / Linux host, Dream Land):

```
FRAME_COMMIT_STATE_DIVERGE validation=1035
  figh+world+item+eff MATCH, inputs MATCH
  diverged=rng only
  Linux  rng=0xE81C1E96  game_seed=0x3392C117
  Android rng=0xA7C2B364  game_seed=0xCE65B049
```

`0x3392C117 → 0xA3FD777E → 0xCE65B049` is exactly **two** gameplay LCG steps. Synctest clean; GGPO stick correction @901 (`sy=0` pred vs `sy=71` wire) → JumpAerial from DamageFall; both peers' post-resim fighter_detail / mph match. Android alone advanced the game seed during resim (post `rng=0xA4A48461`) and again at archived tick **911** (`rng_hash_walk` `ring_steps=1`, site inside `syUtilsRandFloat`).

## Root cause

July 9 ForcedCosmetic remap covered `efmanager.c` / `lbparticle.c` but **not** `efground.c`. Dream Land's stage ambient actor (`dEFGroundPupupuParams`, clouds/etc.) still called raw:

| Site | Draws |
|------|--------|
| `efGroundUpdatePhysics` altitude jitter | `syUtilsRandFloat` ×1 per spawn |
| `make_wait` / `param_id` / `lr` / `make_queue` | `syUtilsRandIntRange` |

Those are pure presentation placement/scheduling. After GGPO, asymmetric ground-effect spawn (or re-init) burns the hashed gameplay LCG on one ISA while `figh`/`eff` stay matched — same class as [`netplay_effect_vfx_forced_cosmetic_rng_2026-07-09.md`](netplay_effect_vfx_forced_cosmetic_rng_2026-07-09.md).

`rng_hash_walk` site used `__builtin_return_address(0)` inside `AfterGameSeedStep`, so it always pointed into `syUtilsRandFloat`/`UShort` and could not name the consumer.

## Fix

| Change | Purpose |
|--------|---------|
| `efground.c` file-scope macros under `SSB64_NETMENU` | Remap to `ForcedCosmetic` (else `Cosmetic`) like efmanager/lbparticle |
| `utils.c` passes `return_address(0)` into rng trace | Dump the real consumer PC (not inside AfterGameSeedStep) |
| `utils.h` comment | Document efground in the ForcedCosmetic policy set |

## Verification

- Re-soak session `736365822` profile (Dream Land, early JumpAerial GGPO): FC must not report `diverged=rng` with matched inputs.
- Optional: `SSB64_NETPLAY_RNG_STEP_SITE=1` — gameplay `rng_step` lines must not show `efGround*` sites.
- Shared flyroll draw ~779 (`83F21FF3 → E81C1E96`) remains gameplay RNG (intentional).
