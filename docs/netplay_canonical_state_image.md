# Canonical State Image (CSI) — cross-peer / cross-build comparison

## Purpose

At the point where traversal and input-layer diagnostics agree across peers but **simulation hashes still diverge**, most rollback systems either become **truly deterministic** or **quietly lie**: “matching” or “differing” hashes over **raw in-memory structs** are often **not comparable across machines**.

This document defines **Canonical State Image (CSI)** work: **platform-independent serialized state** used for NetSync, rollback verification, and first-field divergence — **not** `memcmp(FTStruct)` across Fedora vs CachyOS vs Windows unless layout and UB are proven identical.

Companion: [`netplay_rollback_state_contract.md`](netplay_rollback_state_contract.md) (snapshot closure, Cursor next steps), [`netplay_frame_composition.md`](netplay_frame_composition.md) (`gch` / UpdateSet).

---

## 1. Core problem

Implicitly comparing:

> `memcmp(struct A on peer A)` vs `memcmp(struct A on peer B)`

is **invalid** unless **all** of the following hold:

- Identical **ABI** (layout, alignment, padding rules)
- No **UB**-sourced or uninitialized padding reads
- No **pointer** identity in the compared bytes
- Same **compiler** / flags / `LP64` assumptions

Deterministic **game logic** does not imply deterministic **memory representation** across builds or OSes.

**One-line diagnosis of the bug class:**

> The system is comparing **memory representations**; cross-peer rollback truth requires comparing **canonical state representations**.

---

## 2. Model: Canonical State Image (CSI)

**Game state for comparison does not live in RAM as structs; it lives in a canonical byte stream** (or equivalent typed sequence) built by explicit rules.

- **Serialize** only registered, semantics-first fields into a **deterministic-order** buffer.
- **Diff** field-by-field or over that buffer — optionally hash **only** the canonical stream (e.g. FNV/xxHash), **never** raw `FTStruct` bytes.

---

## 3. Design goals

| Goal | Meaning |
|------|--------|
| Deterministic ordering | Same field and entity order on every peer, every tick. |
| No padding dependence | No raw struct `memcpy` as the oracle. |
| No pointer identity | Pointers → **stable IDs** (e.g. `GObj->id`, slot index, 0 if null). |
| No platform ABI in wire | Fixed-width integers; explicit float policy (see below). |

---

## 4. Step A — Field registry (explicit truth)

Define an **enumerated registry** of what is comparable — e.g. `CSIFieldID` or subsystem tables. **No** “walk the struct with offsetof hacks” as the **source of truth** for what matters; the registry is the contract.

Each registered field maps to:

- read path from game state,
- canonical type,
- ordering key (which fighter / which world object).

---

## 5. Step B — Subsystem serializers (no struct memcpy)

Each subsystem implements explicit writers, e.g.:

```c
void csi_serialize_fighter(const FTStruct *fp, CSIWriter *w);
```

Rules inside writers:

- `csi_write_u32(w, (uint32_t)fp->percent_damage);` — **fixed-width**, explicit casts where needed.
- **No** `memcpy(w, fp, sizeof(*fp))`.
- **No** relying on compiler padding being zero.

---

## 6. Step C — Canonical ordering (critical)

**Fighters:** always emit in **stable ID order** (e.g. monotonic `fighter_gobj->id` or agreed slot order) — **not** linked-list walk order unless that list is provably identical on all peers.

**World / items / effects:** same: **stable ID ascending**, not allocator address order.

**RNG:** emit explicit **fixed-width** state (e.g. `uint64_t` seed + any counter the sim uses), **never** “whatever is in the platform RNG” or wall time.

---

## 7. Step D — Fixed-width canonical types

Ban ambiguous C types in the wire image:

| C / decomp habit | Canonical |
|------------------|-----------|
| `int` / `long` | `int32_t` / `uint32_t` / `int64_t` (explicit) |
| `float` / `f32` | Policy: IEEE-754 bit pattern as `uint32_t` **or** quantized fixed-point — **pick one** and document; floats are a common false-diff source. |
| Pointers | `uint32_t` stable ID only |

---

## 8. Step E — Padding and packed structs

- **Do not** use `#pragma pack` on **live** game structs as “the fix.”
- If an intermediate transport blob is packed, it is **downstream of** explicit field writes — not the in-memory source of truth.

Best practice: **correctness never depends on packed struct layout of decomp types.**

---

## 9. Step F — Pointer normalization

Any field like `target`, `throw_gobj`, `fighter_gobj`, etc.:

- Serialize **`target ? stable_id(target) : 0`**, never raw pointer bits.

Eliminates ASLR, heap layout, and “same object different address” false divergence.

---

## 10. Step G — Ground truth vs derived vs recomputed

Classify every registered field:

| Class | Compare across peers? | Rollback |
|-------|-------------------------|----------|
| **A. Ground truth** | Yes (in CSI) | Must save/restore or re-derive from CSI + inputs |
| **B. Derived** (anim cache, render-only, etc.) | **No** — omit from cross-peer CSI | Usually omit from rollback blob; recompute after restore |
| **C. Recomputed each tick** (scratch collision, temp flags) | **No** — omit from CSI **unless** a bug makes it leak across ticks | Must be zeroed or recomputed after restore before sim |

**Critical rule:** comparing **B** or **C** across machines **guarantees phantom divergence**.

---

## 11. Step H — Oracle output (field-first)

Prefer logs like:

```text
tick=480 slot=0 damage=23|23
tick=480 slot=0 pos.x=1200|1198  ← FIRST_DIVERGENCE
```

Not only:

```text
figh=0x7AE6370A|0x174EBC58
```

Hashes over **CSI bytes** are fine **after** serialization; raw struct hashes are not a cross-platform oracle.

---

## 12. Optional: hash the canonical stream only

```text
canonical_bytes = SerializeWorldCSI(tick)
hash = FNV1a(canonical_bytes)
```

Use that for **cheap** checks — still **after** flattening, **never** over raw `FTStruct`.

---

## 13. Why Fedora vs CachyOS (or Debug vs Release) “wiggles”

CSI eliminates false signals from:

- ABI / padding / alignment drift
- Compiler reordering or different `struct` packing assumptions
- Undefined reads in padding
- Pointer bits in “hashes”

Both peers converge on **semantic equality**, not **memory equality**.

---

## 14. Fighter snapshot blob vs NetSync hash coverage (rollback)

Rollback save uses **`SYNetRbSnapFighterBlob`** in `port/net/sys/netrollbacksnapshot.c`. Cross-peer oracles use **`syNetSyncHashFighterStructLight`** (per-slot, partial) and **`syNetSyncHashBattleFightersFull`** (all joints + extended physics). Divergence with matching inputs usually means either **blob round-trip omission** (synctest / `LOAD_HASH_DRIFT` + `SSB64_NETPLAY_SNAPSHOT_FIGHTER_FIELD_DIFF=1`) or **live forward-sim nondeterminism** (synctest passes locally, peers split on `fighter_slot_hash` before next FC).

| Surface | In fighter blob | In `HashFighterStructLight` | In `HashBattleFightersFull` |
|---------|-----------------|------------------------------|-----------------------------|
| Top joint translate | `joint_translate[]` (all joints saved) | top joint only | all joints |
| `motion_attack_id`, `hitstatus`, jostle | yes | partial / via gameplay | yes |
| Joint `event32`, `gobj_anim_frame` | yes | no (use `anim` partition) | anim hash separate |
| Coupled weapon / item gobj IDs | yes (2026-05-19) | via gameplay / weapon hash | weapon hash |

Probe ticks **870, 880, 890, 899** with `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1` and `SSB64_NETPLAY_SNAPSHOT_FIGHTER_DIAG=1` before editing blob fields.

---

## 15. Minimal implementation task (for agents)

Replace (or augment) rollback / NetSync **cross-peer** comparison paths that still depend on **implicit struct memory** with:

1. A **field registry** (explicit list of comparable state).
2. **Explicit serializers** per subsystem (fighter core, then world closure).
3. **Stable sort keys** for fighters and world objects (ID order).
4. **Pointer → ID** only on the wire.
5. **Omit derived / per-frame scratch** from cross-peer CSI unless proven load-bearing for sim.
6. A **field-by-field** or **region** diff oracle for first divergence near ticks **~360–480** (see rollback doc).

Follow-on design work: derive the **initial CSI field list** from **SSB64’s real** `FTStruct`, `SCBattleState`, item manager, and RNG surfaces actually read during `gcRunAll` — that is where bitfields, enum width, and float normalization must be nailed down.

---

## References (code starting points)

- Light / non-canonical hashes today: `port/net/sys/netsync.c` (`syNetSyncHashFighterStructLight`, etc.)
- NetSync logging: `port/net/sys/netpeer.c`
- Rollback save/restore: `port/net/sys/netrollback.c`
- Fighter type: `decomp/src/ft/fttypes.h` — `FTStruct`
- Battle transfer state: `gSCManagerTransferBattleState` / `SCBattleState` in `port/net/sc/sctypes.h` / decomp `sc/sctypes.h`
