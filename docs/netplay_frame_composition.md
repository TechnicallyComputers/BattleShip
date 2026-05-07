# Frame composition determinism (UpdateSet)

## Purpose

After tick gating, rollback frontier work, and admission ordering fixes, remaining **gameplay desync** often reduces to **frame composition**: two peers advance the same sim tick index but build a **different set of entities / different traversal order** before or during `gcRunAll`.

**When `gch` / `pairs=` already match peers but `figh` diverges mid-match:** treat composition logging as **sufficiently ruled out**; stop investing in new traversal or input-only experiments and follow **“Cursor next steps”** in [`netplay_rollback_state_contract.md`](netplay_rollback_state_contract.md) (full snapshot diff, first field-level delta ~360–480, rollback completeness, derived-state bugs). For **cross-build / cross-OS** comparison, any “struct hash” must follow **Canonical State Image** rules in [`netplay_canonical_state_image.md`](netplay_canonical_state_image.md) — raw memory is not a portable oracle.

This document states a **formal invariant**, how to **detect violations**, where to **audit in code**, and what **success** looks like in logs.

## Invariant (anchor)

For every completed sim frame `t`:

1. **UpdateSet(t)** — the multiset of entities that participate in simulation for that frame, and the **order** in which `gcRunAll` visits them — must be **identical** on all peers.
2. **UpdateSet(t)** must be derivable from **committed pre-frame state** and **agreed inputs** only — not from wall-clock timing, packet arrival order inside the frame, or OS thread scheduling. **`Inputs_t` must be frozen before `Sim_t`** (read-only during `gcRunAll`; see [*Inputs vs RNG*](netplay_taskman_simtick.md#inputs-vs-rng-same-index-different-mutability) in [`netplay_taskman_simtick.md`](netplay_taskman_simtick.md)). Same for **which frame is `t`**: only the **rollback sim index** (`syNetInputGetTick`) counts — not Taskman passes, not VI / presentation cadence ([`netplay_taskman_simtick.md`](netplay_taskman_simtick.md) — *Simulation authority*).
3. **No hidden ordering** — iteration must not depend on nondeterministic container ordering, allocation-address order, or mid-frame list mutation that changes later iterations in the same frame.

Items, map collision hashes, and global RNG are **downstream**; they diverge *after* composition or ordering has already forked.

## Failure definition (not “hash differed”)

Treat as a composition / ordering bug when **any** of:

- Different **count** of GObj nodes visited in the common-link phase vs peer at the same tick, or different **`gobj_run`** / **`prun`** counts (see logging below).
- Different **ordered** `(link_index, gobj->id)` prefix at the same tick (`pairs=` line).
- Different **`gch`** (traversal fingerprint) at the same tick — implies membership, flags, fighter slot fields, and/or process-queue shape diverged.

`figh` / `mph` mismatch alone is a **symptom**; **`gch`** mismatch targets **whether the engine’s run lists match**.

## Code locations to audit

| Area | Files / symbols |
|------|------------------|
| Per-frame sim entry | `scVSBattleFuncUpdate` (`port/net/sc/sccommon/scvsbattle.c`), `ifCommonBattleUpdateInterfaceAll` → `gcRunAll` |
| Run loop | `gcRunAll` (`decomp/src/sys/objman.c`) — `gGCCommonLinks[]` then `sGCProcessQueue[]` |
| Link / process mutations | `gcLinkGObj*`, `gcRemoveGObjFromLinkedList`, `gcLinkGObjProcess`, `gcEjectGObj`, `gcRunGObj` / `gcRunGObjProcess` (eject paths) |
| Presence / slots | `syNetInput*` publish vs remote ring (`pub_vs_remote mismatch kind=presence`), fighter spawn / `pkind` |
| Skew / partial frame | `scVSBattleFuncUpdateSkewPacingNetSlice` — intentionally **does not** call `gcRunAll`; ensure nothing else mutates run lists “for” the held tick |

### Anti-patterns to grep for

- Push/pop or link/unlink **during** the same frame’s update walk (mutating lists you are iterating).
- Activation that **inserts** into `gGCCommonLinks` mid-frame based on network arrival rather than committed tick state.
- Iterating **unordered** structures for gameplay (not currently used for `gcRunAll`, but watch port additions).

## Instrumentation (proof-oriented)

### Traversal fingerprint + counts

**Environment:** `SSB64_NETPLAY_GC_TRAVERSAL_DIAG`

| Value | Effect |
|-------|--------|
| `0` / unset | Off |
| `1` | After each periodic `SSB64 NetSync:` line, log **`gc_traversal`**: `gch` (FNV hash of `gcRunAll`-shaped walk), `gobj` (all common-link GObjs), `grun` (would run `gcRunGObj`), `prun` (non-paused `GObjProcess` count in queue pass). |
| `2` | Same as `1`, plus `pairs="L<link>:g<id>,..."` for the first **16** common-link `(link_index, gobj->id)` pairs — use for **first-difference** eyeball vs peer logs. |

Implementation:

- `gcPortGcRunAllTraversalFingerprintEx` / `gcPortHashGcRunAllTraversalFingerprint` — `decomp/src/sys/objman.c` (PORT).
- `syNetSyncHashGcRunAllTraversalFingerprint` — `port/net/sys/netsync.c` (wrapper for other callers).
- Logging — `syNetPeerLogNetSyncValidation` in `port/net/sys/netpeer.c`.

**Note:** NetSync runs on the stats interval (same cadence as existing `SSB64 NetSync:` lines). The snapshot is **post-step world** at that wall time; both peers should compare lines at the **same logged `tick=`**.

### Fighter phase trace (per-slot, human)

When **`gch` matches** but **`figh`** / per-player input checksums diverge, the fork is likely **inside fighter update** (controller → `fp->input.pl` → later processes), not the `gcRunAll` list. For the **rollback engineering contract** (snapshot completeness, hidden globals, resim purity), see [`netplay_rollback_state_contract.md`](netplay_rollback_state_contract.md).

**Environments**

| Variable | Effect |
|----------|--------|
| `SSB64_NETPLAY_FIGHTER_PHASE_TRACE` | **`0`** off. **`≥1`**: after each NetSync validation tick, emit **`ft_phase`** lines for human (`nFTPlayerKindMan`) slots that completed a trace for the last `gcRunAll` pass. **`≥2`**: also `port_log` immediately when `SYController` hash ≠ `syNetInputGetHistoryFrame` hash for that slot/tick (`ctrl_hist_mismatch`). |
| `SSB64_NETPLAY_FIGHTER_PHASE_ASSERT` | **`≥1`**: `port_log` **`ft_phase_assert`** when controller snapshot and history frame hashes disagree at **Phase A** (start of `ftMainProcUpdateInterrupt`). |

**Phases (implementation)**

| Phase | Boundary in code |
|-------|---------------------|
| **A** | Start of `ftMainProcUpdateInterrupt`: hash `gSYControllerDevices[slot]`, hash `syNetInputGetHistoryFrame(slot, capture_tick)`, `syNetSyncHashFighterStructLight(fp)` (**stA**). **`ord`** = monotonic order index for human fighters entering interrupt this `gcRunAll`. |
| **B** | After the `is_control_disable` input block (stick buffers / tap counters): hash `fp->input.pl` (**plB**), light fighter hash (**stB**). |
| **C** | End of `ftMainProcParams`: light fighter hash (**stC**). |

**Log line** (`SSB64 NetSync: ft_phase ...`): `tick` = `syNetInputGetTick()` captured at `ifCommonBattleGoUpdateInterface` → `gcRunAll` entry; `vtick` = NetSync validation tick; `hmatch=1` when history exists and controller hash equals history frame hash.

**Caveats**

- Only **`nFTPlayerKindMan`** is traced (not COM / CPU / key replays).
- Skipped during **`syNetRollbackIsResimulating()`** to avoid nested `gcRunAll` noise.
- **`SYController`** includes `button_tap` / `button_release`; **`SYNetInputFrame`** packs canonical `buttons` + sticks — hashes can differ if taps were not yet derived on the controller side. Treat **`hmatch`** as a **signal**, not absolute truth, until packed fields are aligned with publish semantics.

### Cross-peer comparison

1. Enable `SSB64_NETPLAY_GC_TRAVERSAL_DIAG=2` on **both** machines.
2. Diff logs at the same `tick=`.
3. If `gch` differs: compare `gobj` / `grun` / `prun`, then `pairs=` string — first differing token is the **earliest ordered composition mismatch** in the common-link lists.

## Fix strategy (architectural)

**Required:** Derive **who runs and in what order** from a **frozen** description at the frame boundary (committed state + inputs). All consumers iterate **only** that description.

**Forbidden:** Growing/shrinking/reordering the live `gGCCommonLinks` / process queues based on mid-frame side effects in a way that two peers can interpret differently.

Full engine refactors are large; use instrumentation first to prove **composition** is the fork, then narrow the smallest **pure-function** gate (often presence / slot / spawn timing).

## Related docs

- [`netplay_architecture.md`](netplay_architecture.md) — VS update ordering and net boundaries.
- [`netplay_taskman_simtick.md`](netplay_taskman_simtick.md) — sim tick vs taskman / skew.
- [`netplay_pacing.md`](netplay_pacing.md) — env var index including `SSB64_NETPLAY_GC_TRAVERSAL_DIAG`.
