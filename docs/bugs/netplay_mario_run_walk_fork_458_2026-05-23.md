# Netplay — Mario Run vs WalkSlow fork @458 (session 1799831564)

**Date:** 2026-05-23  
**Status:** INVESTIGATION (recovery fixes landed; live sim root cause open)

## Symptom

WAN soak `1799831564` (Mario vs Samus, FC @120/240/360 clean):

- First cross-peer fighter fork logged at **tick 473** (`FHASH_LIGHT_MISMATCH_TRIGGER`).
- `FRAME_COMMIT_STATE_DIVERGE` @**480**: `inp=0x9BC11E86` agree; `world=0xCDDF7E6E` agree; `figh` + `rng` diverge.
- Recovery: seal span 120, `resim_reconcile_span 361→481`, but **`AwaitingBaseline → Live`** without `Replay`, triple deepen 360→358→356, `EPISODE_SEAL_ROWS_REJECT stale_episode_tuple`.

## Tick-by-tick diff (host vs guest)

| Tick | Host Mario (P0) | Guest Mario (P0) |
|------|-----------------|------------------|
| **360** | `figh` window matches (`0xF4D2CEBD` live aggregate); slot baseline identical cross-peer | same |
| **400–457** | No `fhash_light` logs (stable local light hash; cross-peer already split) | same |
| **458** | `nFTCommonStatusWait` (0xa) after `WalkMiddle` | **`nFTCommonStatusRun`** (0x11) — **first status fork** |
| **471** | `nFTCommonStatusWalkSlow` (0xb), floor **1**, Y≈1105, `vel_ground` active | still **Run** (0x11) from 458 |
| **472** | WalkSlow + moving | Run, `status_tics=16`, lower platform Y≈1080 |
| **473** | WalkSlow; Samus shield @ `C48E2000` | Run; Samus @ `C48B9800` (small X drift) |
| **480** | `ftMainSetStatus` dash/attack line (0x15/0x16), `gobj_alloc` 1011 | same pattern, same frame ids |

Enum refs (`decomp/src/ft/ftdef.h`): `Wait=10`, `WalkSlow=11`, `WalkMiddle=12`, `Dash=15`, `Run=16`, `RunBrake=17`.

## Root cause layering

1. **Not inputs / FC sealing:** `hist_win [360,480) all=0x9BC11E86` matches on both peers — this is a **120-tick window digest**, not per-tick equality. Micro-drift from ~458 can still produce a matching window hash.
2. **Live sim:** Mario enters **Run** on guest @458 while host enters **Wait** then **WalkSlow** @471. `ftCommonRunCheckInterruptDash` gates on `fighter_gobj->anim_frame` vs `attr->dash_to_run` — sensitive to **anim_frame** divergence after load.
3. **RNG @480:** Consequence of divergent fighter sim after ~458, not independent first cause (`rng` matched at load 360).
4. **Recovery:** Post-load `LOAD_HASH_DRIFT anim-only` @360; baseline exchange agrees `figh/world/rng` but not `anim`; old path called `AbortPendingResimForBaselineMismatch` → `ResetBaselineResimState` → **Live** without forward replay.

## Code fixes (2026-05-23)

| Change | File |
|--------|------|
| Reapply joint/`gobj_anim_frame` **before** load-hash verify + again after coupling | `netrollback.c` `syNetRollbackLoadPostTick` |
| Anim-only baseline: reapply + **open replay gate** (not abort-to-Live) | `syNetRollbackTryOpenResimReplayGateAfterAnimResync`, gate + abort + `PEER_BASELINE_ANIM_ONLY` paths |

## Next soak / bisect

Add to diagnostic bundle:

```bash
SSB64_NETPLAY_SIM_STATE_TICK_INTERVAL=1   # enables fighter_slot_hash
SSB64_NETPLAY_FHASH_LIGHT_MISMATCH_TRIGGER_SECOND_MIN=400
```

Target: first tick where `status`/`motion`/`anim_frame` differ for P0 with same sealed per-tick inputs (if exposed via input history dump).

## Related

- [`netrollback_fighter_midmatch_drift_2026-05-20.md`](netrollback_fighter_midmatch_drift_2026-05-20.md)
- [`netplay_frame_commit_authority_digest_2026-05-24.md`](netplay_frame_commit_authority_digest_2026-05-24.md)
- [`netplay_episode_seal_target_mismatch_2026-05-24.md`](netplay_episode_seal_target_mismatch_2026-05-24.md)
