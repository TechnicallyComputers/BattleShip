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
- **No patch-only correction (2026-05-18)** — significant predicted-remote mismatch queues deferred symmetric resim; no live `PatchPublishedFromRemoteConfirmed` on that tick. Prediction recovery disabled unless `SSB64_NETPLAY_PREDICTION_RECOVERY=1`. Digital tap patch-without-rollback disabled during active rollback.
- **Symmetric resim execution** — wire-locked `target_tick` when symmetric follower is active (no per-peer `highest_remote + D + 1` shrink); post-load **baseline gate** (`figh`/`world`/`item`/`rng`) before `AdvanceResimBudget`; skip snapshot save during `resim_pending` / episode cooldown; cosmetic RNG reset on snapshot load; confirmed-only remote rows during resim (no predicted fallback). See [`netrollback_rng_item_identity_drift_2026-05-17.md`](bugs/netrollback_rng_item_identity_drift_2026-05-17.md).
- **Symmetric peer notices** — follower resim on by default when rollback is active (`SSB64_NETPLAY_ROLLBACK_SYMMETRIC=0` disables; `SSB64_NETPLAY_ROLLBACK_SYMMETRIC_DIAG=1` log-only).
- **Resim RNG verify** — log after each completed resim by default (`SSB64_NETPLAY_RESIM_RNG_VERIFY=0` disables).
- **Resim coordination transport (2026-05-18)** — while `ResimPending`, peers still run `ReceiveRemoteInput` + `SendLocalInput` + `ROLLBACK_BASELINE` + `ROLLBACK_SYNC` (type 24) so symmetric notify and baseline echo are not blocked. Exception to the “no network during forward resim” target below.
- **Rollback episode** — `SYNetRollbackEpisode` tracks mismatch/load/target and phase (`AwaitingBaseline` / `ForwardResim`); syncs to legacy `ResimPending` / baseline gate flags.

**Out of scope (longer term):** full snapshot byte exchange; disabling symmetric notify for pure independent GGPO until independent detection is proven symmetric; hard-blocking resim on anim-only `LOAD_HASH_DRIFT` (soft-continue policy remains). See [`netplay_rollback_test_matrix.md`](netplay_rollback_test_matrix.md#out-of-scope-longer-term).

**Transitional / unsafe today:**

- **`nSYNetInputSourceRemoteGapFilled`** — hold-last synthetic wire rows for strict admission. Must **not** be treated as confirmed authority for rollback mismatch or resim seeding once timeline refactor lands.
- **Published history vs remote ring double-scan** — rollback compares rings; target is `incorrect_prediction` markers on the timeline.

## Rollback trigger (target)

- **Local and input-driven** (GekkoNet `GetMinIncorrectFrame` style): rollback when confirmed input proves a prior prediction wrong.
- **Peer symmetric rollback notices** — still the **coordination contract** for matching `mismatch_tick` / `target_tick`. Not a substitute for independent per-peer mismatch detection until that path is proven to pick the same span without notify.

## Resimulation (target)

- **Pure sim step**: publish historical inputs + `scVSBattleFuncUpdate` battle sim only.
- **Excluded during forward resim sim loop**: fresh HID, adaptive delay bumps, full `syNetPeerUpdate` gameplay path.

**Current (2026-05-18):** Resim branch pumps coordination I/O only (`ReceiveRemoteInput`, `SendLocalInput`, baseline/sync). Forward resim still uses `scVSBattleFuncUpdateBattleSimOnly()`. `syNetInputFuncRead()` may still run inside battle update paths.

## Snapshot restore (target)

- **All-or-nothing**: failed load must not leave a partially applied world.
- **`LOAD_HASH_DRIFT`** after apply is a **hard failure** until restore is complete (item translate fix reduced one class; fighter/anim gaps may remain).
- Ring depth **`SSB64_NETPLAY_ROLLBACK_SNAPSHOT_FRAMES`** must cover practical rollback span (scan window 256 vs default ring 32 is a known sizing tension).

## Hash partition map (integrity-first)

All digests are **current-frame snapshots** (no trajectory). Consumers must not compare hashes across different functions.

| Consumer | Fighter | Item |
|----------|---------|------|
| NetSync validation / frame-commit token | `syNetSyncHashBattleFighters()` (light) | `syNetSyncHashActiveItemsForRollback()` — XOR fold, **sorted by `gobj_id`** |
| Rollback baseline / resim-complete / `RESIM_POST` | `syNetSyncHashBattleFightersFull()` | same rollback item hash |
| Typed snapshot ring | per-slot subsystem hash in blob | full `SYNetRbSnapItemBlob` in [`port/net/sys/netrollbacksnapshot.c`](../port/net/sys/netrollbacksnapshot.c) |

**Order dependence:** item rollback hash XORs per-item folds in `gobj_id` order. Snapshot restore guarantees per-`gobj_id` state, not linked-list order — post-load sim can reorder the list before hash time.

**Optional probe:** `SSB64_NETPLAY_VALIDATION_DUAL_HASH=1` logs when light vs Full fighter hashes differ at NetSync validation (after item bisect).

**Cross-peer resim boundary:** `SYNETPEER_PACKET_RESIM_POST` (type 25) carries `(epoch, load, mismatch, target)` + `figh/item/rng/input_digest`; compare when local forward resim completes and keys match pending peer token.

## Log signatures (regression)

| Log | Interpretation |
|-----|----------------|
| `LOAD_HASH_DRIFT` | Snapshot apply did not reproduce saved hashes — **stop session** (target) |
| `ROLLBACK_IDENTITY_DRIFT` | Resim with unchanged confirmed input did not reproduce pre-resim hashes |
| `REMOTE_CONFIRMED_CONFLICT` | Two authoritative confirmed packets disagree on same wire tick |
| `remote_gap_fill` | Synthetic hold-last wire row (admission-only target) |
| `peer symmetric rollback` | Peer notice queued resim (transitional gameplay contract) |
| `ROLLBACK_SYNC_SEND` / `ROLLBACK_SYNC_RECV` | Dedicated symmetric rollback notice packet (type 24) |
| `RESIM_BASELINE_ECHO` | Passive peer echoed baseline digest without local resim episode |
| `RESIM_POST_MATCH` / `RESIM_POST_DIVERGE` | Cross-peer post-resim digest handshake |
| `FRAME_COMMIT_PAIRING_FAIL` | Same `frame_id` but `\|tick_anchor_local - tick_anchor_peer\| > 1` |
| `FRAME_COMMIT_DIAG` | Shutdown counter summary (`fc_sent`, `fc_compared`, …) |
| `STRICT MISS (R)` | Strict admission stall on missing exact wire row |

## Related bug write-ups

See [`docs/bugs/README.md`](bugs/README.md) entries: `netrollback_symmetric_rollback`, `netrollback_prediction_recovery_storm`, `netrollback_strict_wire_gap_stall`, `netrollback_rng_item_identity_drift`, `netrollback_remote_confirmed_conflict`.
