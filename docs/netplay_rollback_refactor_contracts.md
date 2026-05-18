# Netplay rollback refactor contracts (GGPO / GekkoNet alignment)

This note tracks the **target** rollback architecture during the GGPO-style refactor. It complements [`netplay_rollback_test_matrix.md`](netplay_rollback_test_matrix.md).

## Authoritative sim tick

- **`syNetInputGetTick()`** is the only sim step counter during VS.
- Wire labels use **`sim + D`** (committed input delay). See [`netplay_timebase_authority.md`](netplay_timebase_authority.md).

## Input timeline (target)

Each remote human slot should expose one logical row per sim tick with explicit state:

| State | Meaning |
|-------|---------|
| `missing` | No row usable for sim or strict admission |
| `predicted` | Speculative remote input used for forward sim |
| `confirmed` | Authoritative remote packet stored for this wire/sim row |
| `incorrect_prediction` | Confirmed input disagreed with what was published; rollback should start here |

**Implemented (2026-05-18):**

- **Unified resim reconcile** — `syNetInputRollbackReconcileResimSpan`: remote slots = wire-confirmed; local slots = transmitted (else non-predicted published per tick). Called from `syNetRollbackBeginResim`.
- **Conservative remote button prediction** — remote human slots: hold-last sticks, buttons default 0 (`SSB64_NETPLAY_PREDICT_REMOTE_BUTTONS_HOLD=1` for legacy hold-last).
- **Symmetric peer notices** — default **diag-only** when phase_lock prediction window ≥ 2; set `SSB64_NETPLAY_ROLLBACK_SYMMETRIC=1` for legacy follower coupling.

**Transitional / unsafe today:**

- **`nSYNetInputSourceRemoteGapFilled`** — hold-last synthetic wire rows for strict admission. Must **not** be treated as confirmed authority for rollback mismatch or resim seeding once timeline refactor lands.
- **Published history vs remote ring double-scan** — rollback compares rings; target is `incorrect_prediction` markers on the timeline.

## Rollback trigger (target)

- **Local and input-driven** (GekkoNet `GetMinIncorrectFrame` style): rollback when confirmed input proves a prior prediction wrong.
- **Peer symmetric rollback notices** (`SSB64_NETPLAY_ROLLBACK_SYMMETRIC`, INPUT `peer_connect_status` padding) are **transitional**. Target: diagnostic only or removed; peers must not rewind because another peer mispredicted.

## Resimulation (target)

- **Pure sim step**: publish historical inputs + `scVSBattleFuncUpdate` battle sim only.
- **Excluded during resim**: `syNetPeerUpdate`, fresh HID, `syNetReplayUpdate`, network send/receive, presentation/audio driven by new sim.

Current path still runs `syNetInputFuncRead()` (which may pump ingress) + full `scVSBattleFuncUpdate()` including `syNetPeerUpdate()` — see Phase 3 of the refactor plan.

## Snapshot restore (target)

- **All-or-nothing**: failed load must not leave a partially applied world.
- **`LOAD_HASH_DRIFT`** after apply is a **hard failure** until restore is complete (item translate fix reduced one class; fighter/anim gaps may remain).
- Ring depth **`SSB64_NETPLAY_ROLLBACK_SNAPSHOT_FRAMES`** must cover practical rollback span (scan window 256 vs default ring 32 is a known sizing tension).

## Log signatures (regression)

| Log | Interpretation |
|-----|----------------|
| `LOAD_HASH_DRIFT` | Snapshot apply did not reproduce saved hashes — **stop session** (target) |
| `ROLLBACK_IDENTITY_DRIFT` | Resim with unchanged confirmed input did not reproduce pre-resim hashes |
| `REMOTE_CONFIRMED_CONFLICT` | Two authoritative confirmed packets disagree on same wire tick |
| `remote_gap_fill` | Synthetic hold-last wire row (admission-only target) |
| `peer symmetric rollback` | Peer notice queued resim (transitional gameplay contract) |
| `STRICT MISS (R)` | Strict admission stall on missing exact wire row |

## Related bug write-ups

See [`docs/bugs/README.md`](bugs/README.md) entries: `netrollback_symmetric_rollback`, `netrollback_prediction_recovery_storm`, `netrollback_strict_wire_gap_stall`, `netrollback_rng_item_identity_drift`, `netrollback_remote_confirmed_conflict`.
