# Netplay desync bisect — session 4 (seed 3401516817, stage 3)

**Date:** 2026-05-25  
**Status:** INVESTIGATION (log bisect complete; live root cause open)  
**Evidence:** `client-auto.log` (host/automation) + `ssb64 (4).log` (guest), WAN P2P, Mario (P0) vs Fox (P1).

## Executive summary

| Layer | Finding |
|-------|---------|
| **FC checkpoints** | @120/240/360/**480 pass** — last agreed validation **480** |
| **First cross-peer sim fork** | **Tick 519** — Mario only (`figh`/`anim`); **`world`/`rng`/`mph` still match** |
| **RNG fork** | **Tick 527** — host `rng` advances (`DF3813F4` → `BB248827`); guest stays at `DF3813F4` |
| **World fork** | **Tick 586** — cascade after ~67 ticks of fighter-only drift |
| **FC failure** | **@600** — `inp` agree; `figh` + `world` + `rng` diverge |
| **Recovery** | Load **@480** → **`LOAD_HASH_DRIFT`** (`figh`, `cam`, `anim`, `eff`) → VS stop, **no resim** |

This is **not** “RNG fork at 481” on both peers — tick **481** is the first post-checkpoint combat step and both logs show **identical** `sim_state_tick` through **518**. The fork is **519**.

## Tick bisect (automated `sim_state_tick` compare)

| Tick range | Host vs guest `sim_state_tick` |
|------------|--------------------------------|
| 1–480 | Identical (FC@480 pass) |
| 481–518 | **Identical** (including `rng=0xDF3813F4` on both) |
| **519** | **First diff** — `figh`, `anim` (and `cam` in full compare); `world`/`rng`/`mph` **match** |
| 520–526 | `figh`/`anim` diverge; `world` still matches |
| **527** | + **`rng`** diverges (host advances, guest frozen) |
| 527–585 | `figh`/`anim`/`rng` diverge; `world` still matches |
| **586** | + **`world`** diverges |
| 599–600 | Full partition split (matches FC@600) |

## Tick 519 — root fork (Mario P0)

At **518** both peers agree: Mario `status=16` (`nFTCommonStatusRun`), `motion=10`, identical `fhash_*` / `anim_hash`.

At **519**:

| Field | Host (`client-auto.log`) | Guest (`ssb64 (4).log`) |
|-------|--------------------------|-------------------------|
| Mario status / motion | **20 / 14** (`nFTCommonStatusKneeBend`) | **16 / 10** (still **Run**) |
| Fox P1 | `status=153` `motion=-1` (hitstun) | **same** |
| `world` | `0x7437C8C6` | **same** |
| `rng` | `0xDF3813F4` | **same** |
| `ftMainSetStatus` log | **`status=0x14 motion=14`** (only on host) | **none** at 519; guest **`0x11 motion=11`** (RunBrake) at **520** |

So the first deterministic split is **Mario’s action-state machine**: host enters jump squat (KneeBend) while guest remains in Run for the same sim tick index, with **stage/world collision hash still aligned**.

## Ingress / pipeline skew at fork (suspect, not proven per-tick inp)

At tick 519 timing metadata diverges:

| | Host | Guest |
|--|------|-------|
| `ahead` | -7 | -5 |
| `remote_sim` (peer completed) | **521** | **519** |
| `hr` / `wire` | 523 / 521 | 521 / 521 |
| `INPUT recv` primary slot | **1** (Fox remote) | **0** (Mario remote) |
| `INPUT send` primary slot | **0** (Mario local) | **1** (Fox local) |

Host believes the peer is **two sim ticks ahead** when logging local tick 519. That matches a recurring pattern: **frame-commit input window can agree at FC@600 while per-tick remote/local apply order differs mid-match** (see also [`netplay_mario_run_walk_fork_458_2026-05-23.md`](netplay_mario_run_walk_fork_458_2026-05-23.md) — 120-tick `inp` digest masks micro-drift from ~458).

**Next proof step:** soak with `SSB64_NETPLAY_NETSYNC_INPUT_DIAG=1` and bracket **518–522** for per-tick `sim_tick` / `wire_tick` / published buttons on **player 0**.

## Downstream cascade

1. **519–526:** Mario divergent (`host` → KneeBend → special **207** by 521; guest stays Run / RunBrake **17**).
2. **527:** Host consumes RNG (`hash_transition tick=527 partition=rng`); guest never leaves `DF3813F4` → all later host RNG-driven branches differ.
3. **586+:** `world` hash diverges (items/effects/ground state accumulation).
4. **600:** FC reports triple partition + agreeing `inp=0x0F92E3EF`.

## LOAD_HASH_DRIFT @480 (recovery failure — separate from 519 fork)

After FC@600, reanchor `resolved_load=480`. Verify on host:

```
figh: snap 0x80E2627D / live 0x97CD1103
cam:  0x1F269D40 / 0x6CE3A3C4
anim: 0xA785F741 / 0xB9FC02E1
eff:  snap 0xA21A1AB1 / live 0x811C9DC5
world/map/rng/item/wpn: matched
```

`fighter_field_diff`: P0 `status=15 motion=9`, P1 `status=81 motion=70` — live anim/light hashes do not round-trip after apply+finalize even though **FC@480 tokens had matched** when the slot was saved.

Treat as **snapshot rehydration** (figh/cam/anim/eff), not as “peers were already desynced at 480” (live forward sim was still matched through 518).

## What this rules out

- **Not** intro spawn stagger (tick 150: both Mario/Fox `status=5 motion=4`, symmetric).
- **Not** anim-only hidden drift before 519 (`sim_state_tick` byte-equal 481–518).
- **Not** first detectable fork at FC@480 (checkpoint clean).

## Recommended soak env

```bash
SSB64_NETPLAY_SIM_STATE_TICK_INTERVAL=1
SSB64_NETPLAY_FIGHTER_SLOT_HASH_LOG=1
SSB64_NETPLAY_SNAPSHOT_FIGHTER_FIELD_DIFF=1
SSB64_NETPLAY_FHASH_LIGHT_MISMATCH_TRIGGER_SECOND_MIN=500
# Fork @519 input proof (shipped 2026-05-25):
SSB64_NETPLAY_INPUT_FORK_DIAG=1
SSB64_NETPLAY_INPUT_FORK_DIAG_MIN=515
SSB64_NETPLAY_INPUT_FORK_DIAG_MAX=530
SSB64_NETPLAY_FRAME_COMMIT_DIAG=2
SSB64_NETPLAY_PATCH_PUBLISH_LOG=1
```

Log lines to grep:

| Tag | Meaning |
|-----|---------|
| `fork_sim_row` | Per-player published vs sim-consumed vs remote-confirmed @ sim tick; includes `req_wire`, `wire_from_sim`, `hr`, `remote_sim_frontier`, `D` |
| `fork_ingress` | UDP bundle frame: `wire` → `sim` mapping on receive |
| `fork_wire_commit` | Remote confirmed wire stored / patched into published ring |

**Pass criterion @519:** Mario (player 0) `sim_btn` / `pub_btn` / `conf_btn` must match on host and guest; host-only jump squat (`status 0x14`) should correlate with **both** sides seeing the same P0 buttons on that sim tick.

## Related

- [`netplay_mario_run_walk_fork_458_2026-05-23.md`](netplay_mario_run_walk_fork_458_2026-05-23.md) — same class (Mario locomotion fork, FC inp agree, anim load drift)
- [`netplay_joint_anim_desync_bisect_2026-05-23.md`](netplay_joint_anim_desync_bisect_2026-05-23.md) — Tier 1 AObj rebuild (load/resim anim)
- [`netrollback_effect_snapshot_presence_phase1_2026-05-25.md`](netrollback_effect_snapshot_presence_phase1_2026-05-25.md) — `eff` partition on load @480
- [`netrollback_camera_restore_resim_2026-05-25.md`](netrollback_camera_restore_resim_2026-05-25.md) — `cam` on load @480
