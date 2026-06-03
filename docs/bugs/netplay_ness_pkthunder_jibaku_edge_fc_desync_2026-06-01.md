# Netplay — Ness jibaku edge hit frame-commit fighter desync (Sector Z)

**Date:** 2026-06-01  
**Status:** FIX SHIPPED (soak pending)  
**Log:** `netplay-session-trimmed-rollback.log` (Android↔Linux cross-ISA, tick ~6480)

## Symptom

After extended jibaku testing, Ness jibaku into Pikachu lying in down-wait near Sector Z blast zone. Both fighters snap downward off stage; hard desync at tick **6480** (`FRAME_COMMIT_STATE_DIVERGE`, `figh` only). Session stuck in `rollback_epoch_hold` and stopped.

## Root cause

1. **120-tick frame-commit gap** — Last agreed commit @6360; validation interval default 120 allowed silent fighter drift through Hold + jibaku + knockback fly (6440–6459) before detection @6480.
2. **Deferred PK cull blocked orphan strip** — `jibaku_trigger` schedules defer-teardown; `RunLiveJibakuCatchUpAll` skipped all cull while defer active, leaving **5 orphan PK Thunder weapons** live through jibaku hit @6440–6442.
3. **Cross-ISA knockback coupling** — Inputs/world/RNG agree at 6480; only `figh` differs. Jibaku launch → hit on down-wait → damage fly off stage is collision/knockback sensitive (hist_diag p0+p1 diverged in [6360,6480)).

## Fix

| Layer | Change |
|-------|--------|
| **Frame commit cadence** | `syNetRbSnapshotFrameCommitIntervalCap` caps to 40 ticks while `syNetplayNessAnyLiveFighterInFcResimDeferScope()` (Hold/jibaku/bound/defer). |
| **Pre-jibaku cull** | `syNetplayNessCullOrphanPKThunderKeepHead` at `jibaku_trigger` (log `jibaku_pre_cull`). |
| **Defer partial cull** | While defer active, still cull non-head orphans for defer player each tick. |
| **Coupling canonicalize** | During live jibaku burst (231/236), `syNetplayCanonicalizeFighterSimState` on all fighters after catch-up (victim knockback grid). |

## Files

- `port/net/sys/netrollbacksnapshot.c` — FC interval cap during PK Thunder defer scope
- `port/net/sys/netplay_ness_pkthunder_gate.c` — pre/partial cull + coupling canonicalize

## Verification

Re-test Sector Z edge: down-wait victim + Ness Hold → jibaku into blast zone. Frame commit should fire every ~40 ticks during PK Thunder; `jibaku_pre_cull` should drop weapon count before hit; no 120-tick silent drift to `FRAME_COMMIT_STATE_DIVERGE`.
