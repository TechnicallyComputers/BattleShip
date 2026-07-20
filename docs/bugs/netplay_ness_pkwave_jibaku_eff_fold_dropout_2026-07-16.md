# Netplay: PK wave eff-fold classifier dropout at jibaku + peer-target livecap hang

**Date:** 2026-07-16  
**Build:** netmenu (`SSB64_NETMENU=ON`), soak1 session `489290440` / seed `1578789228`  
**Match:** Ness (P0) vs Ness (P1), Android host vs Linux guest  
**Status:** FIX IMPLEMENTED (re-soak pending)

## Symptom

```
FRAME_COMMIT_STATE_DIVERGE validation=3181
  local  figh=0xF7EDA4A2 world=0x928E774F item=0x811C9DC5 rng=0xE8C8EFE3 eff=0xB444909E
  peer   figh=0xF7EDA4A2 world=0x928E774F item=0x811C9DC5 rng=0xE8C8EFE3 eff=0x21D4909E
  inp_local=inp_peer (MATCH)
```

Eff-only, inputs match, synctest 26 OK / 0 FAIL, `LOAD_HASH_DRIFT=0`. Then a **permanent hang**:
both peers loop `try_begin_fail stage=fc_ness_pk_defer mismatch=3141 target=3182` while
`sim advance blocked (rollback_epoch_cap=3183 source=2)` freezes sim — the fighter never exits
jibaku, so the defer never clears. Session ended only by user quit (`VS_SESSION_END tick=3183`).

## Root cause (two independent bugs)

### 1. Eff-fold classifier dropout at the hold → jibaku transition

The PK Thunder wave shell (`gobj_id=1011`, `gcPlayAnimAll`, joint 5) legitimately drifts its
`anim_frame` cross-ISA during hold — resim epoch 36 (load 3143) left Android 2 frames behind
Linux. That skew is *tolerated by design*: while `syNetplayLiveEffectIsNessPKWave` classifies the
shell, the fold hashes `status_total_tics` instead of `anim_frame`
(`netplay_ness_pkwave_eff_frame_commit_2026-07-10.md`). Fold hashes matched 3144–3178 despite the
skew.

At tick 3179 jibaku triggered (P0 status 233 → 236). `syNetplayFighterInNessPKWaveSimScope` only
covered Start/Hold, so the classifier dropped out while the wave shell survived 2 more ticks
(destroyed after 3180) — the fold fell back to the **drifted** `anim_frame`:

| tick | Android eff hash | Linux eff hash | note |
|------|------------------|----------------|------|
| 3178 | `0x701CD53D` | `0x701CD53D` | hold, status_total_tics folded |
| 3179 | `0x9B74909E` | `0x1E2C909E` | jibaku, anim_frame folded — fork |
| 3180 | `0xB444909E` | `0x21D4909E` | frame-commit compares this snapshot |

The snapshot layer already stamps `respawn=NESS_PK_WAVE` through jibaku
(`syNetRbSnapFighterInNessPKWaveScope || syNetplayNessFighterInPKJibakuCatchUpScope`); only the
live-hash classifier had the narrow scope.

### 2. Peer-target livecap deadlock during `fc_ness_pk_defer`

The FC state divergence armed recovery (mismatch=3141, target=3182) plus a peer epoch /
episode-FSM peer-convergence target. `syNetRollbackTryBeginDeferredStateMismatch` defers via
`fc_ness_pk_defer` while any Ness is in the volatile jibaku window — correct — but the
**peer-target livecap** (`rollback_epoch_cap=3183 source=2`, from
`syNetRollbackComputePeerEpochLiveCap` / `syNetRollbackEpisodeFsmGetLiveSimCap` peer-convergence)
froze sim at 3183. Frozen sim → jibaku status never advances → defer scope never exits →
deadlock. Same shape as `netplay_ness_pk_defer_ggpo_livecap_deadlock_2026-07-13.md`, which lifted
only the *own deferred correction* cap (source=8); this hang came through the peer-target path
(source=2).

## Fix

| Layer | Change |
|-------|--------|
| **Eff-fold scope** (`netplay_sim_quantize.c`) | `syNetplayFighterInNessPKWaveSimScope` widened from Start/Hold to the full PK Thunder Hi family (Start/Hold/End/Jibaku/Bound, ground + air), so the surviving wave shell keeps folding `status_total_tics` until it destroys |
| **Legacy peer cap** (`netrollback.c`) | `syNetRollbackComputePeerEpochLiveCap` returns no-cap while `syNetplayNessAnyLiveFighterInFcStateRecoveryDeferScope()` — every contributor (peer epoch, pending/deferred symmetric, deferred corrections, FC recovery target, pending episodes) waits on a resim that `fc_ness_pk_defer` holds off; supersedes the narrower 07-13 gate (removed) |
| **Episode FSM cap** (`netrollback.c`) | `syNetRollbackGetLiveSimCap` ignores the FSM peer-convergence cap (`fsm_source==2`) in the same scope, falling through to the legacy computation so an in-flight local resim (source=1) still caps |

The defer-scope predicate is deterministic sim state (fighter digests agreed), so both peers lift
identically and the caps re-arm once the volatile scope exits.

## Re-soak pass criteria

Session `489290440` class: no eff-only `FRAME_COMMIT_STATE_DIVERGE` at a hold→jibaku transition;
if any FC state divergence does arm during jibaku, sim keeps advancing (no
`sim advance blocked (rollback_epoch_cap=...)` loop) and the resim begins after the volatile
scope exits. `netplay-scan-drift.py` RESULT PASS.
