# Rollback state contract (GGPO-style correctness)

## Purpose

This document states what **rollback-correct** netplay actually requires in this port, separate from **input** synchronization and separate from **frame composition** (`gcRunAll` traversal). Diagnostics such as controller snapshot experiments (`SSB64_NETPLAY_CONTROLLER_FREEZE_SNAPSHOT`) or `SSB64_NETPLAY_GC_TRAVERSAL_DIAG` can show *where* a fork appears; they do not replace the engineering contract below.

Companion: [`netplay_frame_composition.md`](netplay_frame_composition.md) (UpdateSet / `gch`), [`netplay_taskman_simtick.md`](netplay_taskman_simtick.md) (rollback frame index vs Taskman/VI — *Simulation authority*), [`netplay_architecture.md`](netplay_architecture.md) (wiring overview).

## Core contract

Rollback assumes the simulation advance for one tick is a **pure function** of:

1. **The rollback snapshot** — every byte of state that can influence the outcome of that tick’s sim, and  
2. **The agreed inputs** for that tick (and any other explicitly modeled exogenous data) — already **assigned and frozen** for index `t` **before** `Sim_t` runs; **read-only** during `gcRunAll` (no mid-frame republish, no rebinding `Inputs_t` from Taskman/VI/arrival order). Same frame index `t` as `syNetInputGetTick()`; see [*Inputs vs RNG*](netplay_taskman_simtick.md#inputs-vs-rng-same-index-different-mutability) in [`netplay_taskman_simtick.md`](netplay_taskman_simtick.md).

**GGPO-style rollback does not “solve” nondeterminism or missing state.** It forces the engine to **either** become closed under that snapshot **or** fail visibly (resim mismatch, divergent hashes). Correctness depends on **complete simulation state capture and deterministic re-execution**, not on better input timing alone.

One-line summary:

> **Rollback correctness is state closure plus deterministic re-execution; input sync is necessary but not sufficient.**

## What a “GGPO-style fix” looks like here (not diagnostics)

These are **engineering requirements**, not logging features:

### 1. Enlarge snapshot coverage

Grow the rollback save/restore region until **hash-stable resim** holds for the scenarios you care about (e.g. same inputs and snapshot → same `figh` / NetSync line as peer). Then shrink only with proof (field-level bisection, checksums). If `gch` matches peers but `figh` diverges, the usual read is: **sim state is not closed under the current snapshot** (something that affects fighters is outside the blob or not restored identically).

### 2. Audit fighter struct completeness (fighter “closure”)

`FTStruct` (and friends) is **necessary, rarely sufficient**. Also audit everything a fighter **reads** during that tick from: collision (`MPCollData` / stage), items, weapons, effects, other fighters’ observable state, RNG streams used by gameplay, temporary scratch, and **port-only** side tables. “Completeness” means **fighter closure under the tick**: no read depends on a value that is not in the snapshot and not deterministically derivable from it.

### 3. Eliminate hidden globals

Any `static` / BSS / module-level / bridge cache that is **read** during sim must either be **in the snapshot** or **recomputed deterministically** from snapshot + inputs for that tick. The bar is not “we avoid writing globals in battle”; it is **no sim-affecting read depends on mutable state outside the rollback boundary**.

### 4. Eliminate cross-slot smuggling

Per-slot arrays are fine if slot assignment and writes are deterministic from snapshot + inputs. **Shared accumulators**, “last writer wins” globals, or **writes keyed to the wrong slot** are not. Asymmetry often shows up as **slot-specific** divergence after the same high-level traversal.

### 5. Single “frame step” purity (architectural)

Ideally, **all mutations that affect rollback-visible gameplay** occur inside one well-defined **authoritative sim step** (the same boundary that advances `syNetInputGetTick()` / GGPO battle frame), or they are **included in the snapshot**. In practice the codebase still has taskman, scene wrappers, and net ingress; the rule is: **no sim-affecting mutation** in paths that run **outside** that step unless restored or re-derived on resim. Debug, UI, and audio hooks must not poke sim state unless snapshotted.

## Relationship to observed symptoms

| Observation | Implication |
|-------------|-------------|
| `gch` matches, `figh` diverges | Traversal / UpdateSet likely aligned; fork is **inside fighter evolution** or **state read by fighters** not in snapshot, not “wrong `gcRunAll` list” alone. |
| Controller freeze does not fix `figh` | Unlikely that **mid-frame live `gSYControllerDevices` overwrite** is the sole cause; prioritize **snapshot completeness** and **deterministic resim**. |
| `pub_vs_remote` / presence lines | Input **admission** / ring semantics; orthogonal to proving **full sim state** closure. |

---

## Cursor next steps (investigation phase — post traversal / input)

Use this section once **`gch` / `pairs=` match across peers** but **`figh` (or world hash) diverges** — i.e. frame composition and live-controller overwrite are **no longer the primary hypothesis**. **Deprioritize** further work on:

- `SSB64_NETPLAY_GC_TRAVERSAL_DIAG` (beyond confirming match),
- `SSB64_NETPLAY_CONTROLLER_FREEZE_SNAPSHOT`,
- `SSB64_NETPLAY_FIGHTER_PHASE_TRACE` / phase asserts **as the main signal**,

unless a regression re-opens composition or input admission.

### 1. Full fighter + world state snapshot diff (tick boundaries)

**Cross-peer or cross-build comparison must not rely on raw struct memory** (padding, UB, pointers, ABI). Use a **Canonical State Image** — see [`netplay_canonical_state_image.md`](netplay_canonical_state_image.md). Hashes over explicit serialized fields (or over the canonical byte stream only) are the oracle; `memcmp(FTStruct)` across OSes is not.

At **tick boundaries**, bracket the **authoritative sim step** (the same boundary as `syNetInputAdvanceAuthoritativeSimTick()` / completed `scVSBattleFuncUpdate` — see [`netplay_taskman_simtick.md`](netplay_taskman_simtick.md)):

- **Before** sim step: hash (or hierarchical sub-hash) of:
  - each active fighter’s **`FTStruct`** (and any port-owned shadow state not embedded in `FTStruct` that battle reads),
  - **global sim state** in the **minimal closure** the tick reads: e.g. item manager / spawn state, stage collision fields used by fighters, gameplay RNG seeds/streams, effect lists that affect hitboxes — **expand** until either the diverging subsystem is isolated or bisection proves a region.
- **After** sim step: repeat the same hashes.

Goal: identify **which subsystem** diverges first (fighter vs item vs stage vs RNG), not only that NetSync’s light fighter hash disagrees.

### 2. First diverging field (field-level delta; ticks ~360–480)

Empirical logs often show **`figh` aligned through ~360, fork by ~480**. Target instrumentation there:

- On the **first tick** where any peer-local hash disagrees with the remote reference (or where local pre/post step ordering breaks invariants), compute **field-level** (or **POD-region**) **deltas** on **CSI-serialized** semantics — not hash-only, and not raw struct XOR across different builds/OSes unless the region is provably padding-free and pointer-free. Deliverable: **one named field or explicit struct offset range** that is the **earliest** divergence — suitable for a bug write-up and a rollback save/restore audit.

Techniques: bisection on **CSI-serialized regions**, generated compares over explicit field lists, or temporary typed per-field compare in PORT-only code gated by an env var — avoid blind `memcmp` over whole `FTStruct` for cross-OS peers (see [`netplay_canonical_state_image.md`](netplay_canonical_state_image.md)).

### 3. Audit rollback completeness

For the struct(s) implicated in step 2:

- Verify **every** read/write field in that path is included in **rollback save → restore** (`port/net/sys/netrollback.c` and related).
- Search for **hidden globals** (static/module/BSS, port bridges) that the path reads — if not snapshotted and not re-derived from snapshot + inputs, they are suspects.
- Resim check: after `syNetRollbackRunResim`, does the field **match** the non-resim forward path at the same tick? If not, **restore** or **save** is incomplete, or ordering of recompute vs restore is wrong.

### 4. Derived-state / recomputation bugs

Hunt state that is **assumed** to be recomputed each tick but **actually accumulates**, or is **lazily cached** across ticks:

- “Compute once” statics, dirty flags, caches keyed only by tick without resim invalidation,
- Port-only globals mirroring decomp state,
- Anything that **differs** after rollback resim vs cold forward (compare at same `syNetInputGetTick()`).

---

## Compressed diagnosis (evidence-linked)

The **frame graph** and **`gcRunAll` traversal order** are **deterministic across peers** when `gch` / `pairs` match. **Input timing** alone is **not** the full story when controller-freeze does not remove `figh` divergence. **Simulation state** therefore still contains at least one quantity that is **not fully replicated**, **not deterministically re-derived** from agreed inputs + snapshot, or **not consistently restored** on rollback — and that quantity **accumulates** visible divergence over time in the **fighter / world** layer.

## One-line summary (for agents)

> After ruling out structural traversal and primary input-buffer timing, the remaining desync is a **missing or inconsistently restored (or incorrectly re-derived) piece of simulation state** inside the **fighter/world** layer — find it with **snapshot diff + field-level first delta**, then **close the rollback surface**.

## References (code and docs)

- **Canonical comparison (CSI):** [`netplay_canonical_state_image.md`](netplay_canonical_state_image.md)  
- Rollback / resim: `port/net/sys/netrollback.c`, `port/net/sys/netpeer.c`  
- Input history and publish: `port/net/sys/netinput.c`  
- Diagnostics: `port/net/sys/netsync.c`, `port/net/sys/netfighterphase.c`, `port/net/sys/netcontrollerfreeze.c`  
- VS update entry: `port/net/sc/sccommon/scvsbattle.c` — `scVSBattleFuncUpdate`  
- Sim step / `gcRunAll`: `decomp/src/if/ifcommon.c`, `decomp/src/sys/objman.c`
