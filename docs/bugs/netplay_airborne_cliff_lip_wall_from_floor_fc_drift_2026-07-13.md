# Airborne CLIFF-lip wall-from-floor — FRAME_COMMIT figh topn_tx

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-13  
**Session:** `401300810` seed `998483872` (Android client ↔ Linux host, roles swapped)

## Symptom

Stable mash soak (synctest 7 OK / 0 FAIL / 330 intro skip, `LOAD_HASH_DRIFT=0`) then:

- `FRAME_COMMIT_STATE_DIVERGE @720` — **`figh` only**, `inputs=MATCH`
- Peer field: Kirby P0 **`topn_tx`** Android ~1322.8 vs Linux ~1176.6 (Δx ≈ **146.16**); status/motion matched (`27/21` Fall)
- Prior GGPO resims @391/@474/@631 completed cleanly; FC recovery storm (epoch 4+) and later `PEER_SNAPSHOT_DIVERGE` @950 are consequential

Drift scan: `genuine cross-ISA determinism failure` at tick 720.

## Root cause

Not "desync after resim storm" and not a miss of the status-agnostic airborne PASS|CLIFF `pos_prev` harden ([`netplay_airborne_pass_cliff_coll_harden_fc_drift_2026-07-13.md`](netplay_airborne_pass_cliff_coll_harden_fc_drift_2026-07-13.md)).

Pre-FC `MpLanding` cross-peer compare:

| gut | fflags | fline | fdist | tr_x |
|-----|--------|-------|-------|------|
| 630–632 | PASS `0x4000` | 1 | match | match |
| **633** | **CLIFF `0x8000`** | 3 | match | match |
| **634+** | CLIFF | 3 | **still bit-identical** | **forks** (−0.08 → +6.8/frame → locks Δ=146.16) |

Y/`fdist` stay matched the whole window — classic **wall X push**, not floor projection.

`mpProcessCheckTestL/RWallCollisionAdjNew` attaches under-edge walls from a swept floor when:

```c
(line_collide != FALSE) && !(floor_flags & MAP_VERTEX_COLL_PASS)
```

PASS soft platforms skip that path. Dream Land **ledge lips are often CLIFF-only** (`0x8000` without PASS), so the moment projection flips PASS→CLIFF the wall-from-floor path arms and Cross-ISA Diff float forks `translate.x`.

`ftMainProcPhysicsMap` already sets `pos_prev = *TopN` every physics tick, so BeforeSim `pos_prev` re-anchor cannot prevent this class.

CliffCatch is separate (`CheckTestL/RCliffCollision`) and is not this gate.

## Fix

In both L and R AdjNew wall-from-floor gates (`decomp/src/mp/mpprocess.c`), under `PORT && SSB64_NETMENU` only, treat **CLIFF like PASS**:

```c
!(floor_flags & (MAP_VERTEX_COLL_PASS | MAP_VERTEX_COLL_CLIFF))
```

Offline / non-netmenu keeps vanilla PASS-only skip.

## Verify

Re-soak Android↔Linux Dream Land with soft-platform air travel that crosses platform **edges** (JumpB / Fall over lip):

- No `FRAME_COMMIT_STATE_DIVERGE` `figh` with `inputs=MATCH` from mid-jump TopN.x drift after PASS→CLIFF
- `MpLanding` may still log `branch=diff` / CLIFF `fflags`; `tr_x` must stay peer-matched
- CliffCatch / grounded cliff-floor walk still feel correct (AdjNew is air/damage special path; grounded uses non-AdjNew)

## Related

- [`netplay_jumpaerial_pass_floor_fc_drift_2026-07-12.md`](netplay_jumpaerial_pass_floor_fc_drift_2026-07-12.md) — PASS air `pos_prev` harden
- [`netplay_airborne_pass_cliff_coll_harden_fc_drift_2026-07-13.md`](netplay_airborne_pass_cliff_coll_harden_fc_drift_2026-07-13.md) — broaden harden scope (necessary but insufficient for CLIFF lip wall path)
- [`netplay_cliff_floor_pass_harden_fc_drift_2026-07-11.md`](netplay_cliff_floor_pass_harden_fc_drift_2026-07-11.md) — grounded CLIFF walk
