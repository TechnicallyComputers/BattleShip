# Netplay Yoshi egg-lay resim baseline figh drift (`PEER_SNAPSHOT_DIVERGE` @519)

**Date:** 2026-07-06  
**Scope:** `PORT && SSB64_NETMENU`  
**Soak:** `soak2-linux.log` / `soak2-android.log` (session `991898541`, Samus vs Yoshi)

## Symptom

Post SIGSEGV fix soak: both peers report `PEER_SNAPSHOT_DIVERGE x1` at `load_tick=519`, session stops ~520. `sigsegv=0`. Only `figh` partition mismatches; world/item/rng/anim/wpn/map all match.

```
peer figh=0x1FD01DBD | local figh=0x4F1DD8F3   (Linux)
peer figh=0x4F1DD8F3 | local figh=0x1FD01DBD   (Android)
```

## Game state (not egg escape yet)

- P0 Samus: status **177** (`CaptureYoshi`), motion 150 — swallow animation
- P1 Yoshi: status **230** (egg-lay attack), motion 204
- `weapon_count=0`, `effect_count=0`
- `fighter_detail tag=peer_diverge` shows **identical** per-fighter kinematics and `fhash=0x08CEAC40` on both peers

## Root cause

**False desync — not true sim divergence.**

Resim baseline wire intentionally used **ring slot aggregate** `hash_fighter` at `load_tick`, not post-apply live. After Yoshi egg-lay snapshot apply on the Linux follower:

- Live aggregate `figh` = `0x1FD01DBD` (canonical post-apply, matches Android)
- Ring aggregate `figh` = `0x4F1DD8F3` (stale mid-fill fold from Samus charge→capture transition @517)
- Every **per-player** slot hash still matches live (`syNetRbSnapshotAllFighterSlotHashesMatchAtTick` would pass)

Linux timeline:

1. `RESIM_BASELINE_RECV` Android live `figh=0x1FD01DBD`
2. `BASELINE_PREEMPTIVE_LIVE_CAP` — follower sim already at 521 when baseline arrives
3. Load @519 → `LOAD_SLOT_LIVE_DRIFT` (live ≠ ring aggregate)
4. `RESIM_BASELINE_SEND` still used stale ring `figh=0x4F1DD8F3`
5. Mutual `PEER_SNAPSHOT_DIVERGE`

Existing load-verify path already tolerates this via `syNetRollbackLoadVerifyPerSlotFighDriftOk` + `syNetRbSnapshotRefreshSlotHashFighterWhenPerSlotMatch`, but **baseline arm/compare did not**.

## Fix

`port/net/sys/netrollback.c`:

1. **`syNetRollbackArmResimBaselineAfterLoad`** — when per-slot figh drift OK, refresh ring aggregate and arm wire with **live** `figh`.
2. **`syNetRollbackCollectBaselineCompareLocal`** — compare against live `figh` when per-slot drift OK (not stale ring).
3. **`syNetRollbackPeerBaselineDriftIsStaleAggregateFighOnly`** — tolerate peer stale aggregate when all other partitions + per-slot hashes agree; open resim replay gate instead of `PEER_SNAPSHOT_DIVERGE`.

## Verification

Re-run Samus vs Yoshi soak2 through egg-lay → escape past tick 529. Expect:

- `PEER_BASELINE_WIRE_LIVE_FIGH` / `PEER_BASELINE_FIGH_STALE_AGGREGATE_OK` on Linux follower @519
- No `PEER_SNAPSHOT_DIVERGE`
- Session continues past 520
