# Netplay: Whispy blink -10 reseed via ForcedCosmetic → map-only diverge (2026-07-13)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

**Session:** soak1 seed `1530031167` (Android client ↔ Linux host, Dream Land, D=2)

## Symptom

User: desync when jumping over ledge and coming back. Session ends after landing:

```text
RESIM_BASELINE_MISMATCH map-only deeper exhausted … peer_map=0xEB72CBDD local_map=0xD81798BB
PEER_SNAPSHOT_DIVERGE load_tick=792
  figh/world/item/rng/anim/wpn MATCH — only map differs
```

FC `state_diverge=0`. Synctest OK through the match.

## Timeline

| Tick | Notes |
|------|-------|
| 729–775 | Jump / Fall / Ottotto near ledge (status 32→18→20→23) — **blink/fold still matched** |
| 776–782 | LandingAir (31) while Whispy blink lockout `0→-9` — **matched** |
| 783–789 | Wait (10); blink `-3…-9` — **matched** |
| **790** | Both leave lockout → Android `blink=214`, Linux `blink=44` — first fold/map fork |
| 795 | Stick GGPO → deepen on map-only → PEER_SNAPSHOT_DIVERGE @792 |

Prior `-10` reseeds @199 and @507 already agreed (`287`, `262`). Lockout path (leave-zero + ForcedCosmetic) is fine until the shared cosmetic stream is polluted.

## Root cause

`-10` blink reseed used `syUtilsRandIntRangeForcedCosmetic`. JumpAerial / VFX also burn that LCG asymmetrically (by design — keep game `rng` clean). Between reseed #2 and #3 the cosmetic stream forked; both peers hit `-10` on the same tick but drew different waits → `ground_fold` / map hash diverge with matched fighters and gameplay RNG.

The ledge land is timing coincidence (reseed ~7 ticks after Landing), not PASS/CLIFF coll harden.

## Fix

Under netplay rollback semantics, `-10` reseed mixes **read-only** `syUtilsRandSeed()` with `syNetInputGetTick()` (no LCG advance, no ForcedCosmetic). Init blink wait still ForcedCosmetic (peers start from the same reset).

## Verify

- Dream Land: jump over platform edge + land through a Whispy blink lockout exit.
- Expect matching `pupupu_ground blink=` after reseed; no map-only `PEER_SNAPSHOT_DIVERGE` when figh/rng agree.

Related: [`netplay_pupupu_whispy_blink_rng_fc_2026-07-12.md`](netplay_pupupu_whispy_blink_rng_fc_2026-07-12.md), [`netplay_pupupu_whispy_blink_zero_hold_map_diverge_2026-07-13.md`](netplay_pupupu_whispy_blink_zero_hold_map_diverge_2026-07-13.md).
