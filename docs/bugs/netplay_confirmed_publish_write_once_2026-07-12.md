# Confirmed published write-once (promote/patch/publish no longer rewrite)

**Date:** 2026-07-12  
**Session:** soak1 `1003161693` (Android↔Linux, `STRICT_INPUT=1`); regression soak `1468769950`  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

First stick movement triggers expected feel-0 GGPO (ep0/ep1 complete, matching `rollback_post`). ~180 ticks later second stick-up opens ep2 → `BASELINE_UNIVERSE_MISMATCH` at load 783 (`figh` already forked) → seal chaos → `VS_SESSION_END`. Local `SYNCTEST_OK` @631/@751; FC `state_diverge=0`.

Regression (`1468769950`): write-once logged on promote, but `patch_publish` still rewrote confirmed rows; `store_published_api` still `confirm_downgraded`; absorb skipped GGPO → wire updated / published frozen → map-only `PEER_SNAPSHOT_DIVERGE` @504 after asymmetric stick apply.

Second regression (seed `3284691918`, kill @596): write-once **over-blocked**. Packet redundancy/runway rows beyond the sender's simulated frontier were stored as `RemoteConfirmed` (fake confirms, `sy=-43` at wire 597 while Android's real 595 was `-74`). GGPO ep1 sealed with Android's true rows (`-74`), but `publish_frame` write-once swapped the sealed `-74` back to the stale `-43` **during resim** and `store_published_api` blocked the seal commit (`CommitPromoteSealed`). Linux resimmed 595/596 with `-43`, Android with `-74` → figh/anim/slot-1 fork → `PEER_SNAPSHOT_DIVERGE` @596.

## Root cause

After GGPO sealed, Linux kept mutating player-1 **confirmed published** history via:

- `confirm_rewrite writer=promote_remote_authority` (e.g. `54,31→33,23`, then continuum down to `0,0`)
- `confirm_downgrade writer=publish_frame` during resim (`33,23→54,31`) then `fabricated_confirm writer=resim_sealed` stamped the wrong stick

Post-episode stick absorb skipped GGPO while promote still rewrote published → silent cross-peer figh drift until the next resim baseline compared rings.

### Regression holes (soak `1468769950`)

1. **Tick-keyed write-once**: helper required `existing->tick == incoming->tick`. `patch_publish` compared published (sim tick) vs wire row (wire tick) → tick mismatch → write-once returned false → patch still Store'd.
2. **`store_published_api` ungated**: mid-resim still confirm-downgraded sealed rows.
3. **Promote skip + absorb silent return**: when write-once blocked promote, absorb still skipped GGPO → no rewind while wire≠published.

## Fix

1. **Write-once helper** `syNetInputRemoteConfirmedWriteOnceBlocks`: refuse Store when existing published is `RemoteConfirmed` + unpredicted and **gameplay** differs (ignore tick equality).
2. Gate **`promote_remote_authority`**, **`patch_publish`**, **`publish_frame`**, and **`store_published_api`**.
3. **`syNetInputRemoteConfirmedWriteOnceQueueCorrection`**: on block, mark predicted + `QueueOrWidenStickCorrection` so wire≠published always rewinds (except during active resim).
4. **Absorb**: restore coalesce via `QueueDeferredInputCorrection` (arm once / merge) instead of silent return.
5. **`resim_sealed` reconcile**: prefer strict wire when present before falling back to seal rows.

### Contract fixes (2026-07-12 evening, after seed `3284691918` kill)

6. **Sealed rows outrank write-once** (`syNetInputFrameMatchesSealedEpisodeRow`): any store whose gameplay matches the sealed-episode row for that sim tick passes the gate (logged `REMOTE_PUBLISH_SEAL_OVERRIDE`). The seal-rows exchange is each peer's own local-authority input — the definitive cross-peer agreement. Fixes both the mid-resim `publish_frame` swap and the blocked `store_published_api` seal commit.
7. **Sender-stamped authoritative wire frontier** (wire version 6/7 → **8/9**): INPUT packets append `auth_wire_frontier = DelayWireTickFromSim(sim)` after the connect block (checksummed). Receiver stores frames above the frontier via `syNetInputSetRemoteInputFromPacketEx(provisional=TRUE)` as **`RemoteGapFilled`** (tag `wire_provisional`) — raises `hr`/pacing and hold-last resolution but is never strict-confirmed, so real input replaces it freely and write-once no longer protects extrapolations. Provisional never downgrades an existing strict row; old-version packets are dropped (both peers rebuild together).
8. **Authority ledger Phase 1** (`PORT && SSB64_NETMENU`): sim-tick–keyed `sSYNetInputRemoteAuthorityLedger` dual-written by (a) `CommitRemoteConfirmedWire` → `AuthorityLedgerCommitWire` and (b) sealed `StorePublishedHistoryFrame` → `AuthorityLedgerCommitSeal`. Seal origin outranks wire on conflict. `TryGetRemoteConfirmedHistoryForSimTick` prefers the ledger, then falls back to the wire ring.
9. **Authority ledger Phase 2**: `promote_remote_authority` / `patch_publish` / `publish_frame` no longer invent `RemoteConfirmed` into the published ring. Confirmed path calls `syNetInputRefreshPublishedFromAuthorityLedger` (logged `source=ledger_wire|ledger_seal`). Hold-last predicted promote remains for missing ledger. Thin write-once + on-block GGPO kept only for the non-ledger path. Seal dual-write unchanged.

## Phase 3 — delete thin write-once (not started)

**Goal:** remove the remaining `syNetInputRemoteConfirmedWriteOnceBlocks` / on-block GGPO safety net so confirmed published authority is **ledger-only** (plus seal override / mechanical resim paths). Hold-last invent must either refuse to write confirmed or go through an explicit predicted layer — never mutate a ledger-backed confirmed row via promote/patch invent.

### Preconditions (all must hold on a recent Android ↔ Linux mash soak)

1. **Admission:** `pct_R≈0` (prediction path live; strict-only `ring_ready`).
2. **Load-safe:** `LOADSAFE_PROMOTE` fires; **no** `EPISODE_LOAD_REWIND` when the initiator load tick is still in the ring.
3. **GGPO:** prediction→real stick REPLACE queues corrections; episodes complete with matching load ticks / digests; no baseline timeout hang / seal_rows freeze.
4. **Ledger Phase 2 telemetry:** confirmed publishes are almost entirely `ledger_wire` / `ledger_seal`; thin `confirmed_write_once` / `REMOTE_PUBLISH_SEAL_OVERRIDE` are rare and understood (hold-last invent only).
5. **No input-authority desync:** FC `inputs=DIFFER` / write-once-driven `PEER_SNAPSHOT_DIVERGE` / sealed-row swap regressions absent on the soak.
6. **FC `figh` with `inputs=MATCH`:** not a Phase 3 blocker by itself (cross-ISA physics), but **do not start Phase 3 while a known reproducible FC figh fork is open** — recovery storms obscure write-once telemetry. Clear or quarantine the active FC class first (e.g. JumpAerial PASS harden).

### Remaining steps (after preconditions)

| Step | Work | Exit criteria |
|------|------|----------------|
| **3a** | Inventory every `WriteOnceBlocks` / invent-confirmed call site; classify ledger vs hold-last vs resim | Table in this doc or a short audit note |
| **3b** | Hold-last path: never Store `RemoteConfirmed` without ledger; predicted/gap-fill only | No new confirmed invent in promote/patch |
| **3c** | Delete thin write-once gate + on-block GGPO for that path | Grep clean under NETMENU; witness quiet |
| **3d** | Optional: snapshot serialization overlay tags (Approach C2) — separate milestone | Documented separately |

### Priority vs other work

- Phase 3 is **input-stack cleanup**, not a fix for cross-ISA `figh` FC diverges.
- Prefer **FC physics harden** (JumpAerial PASS, etc.) over Phase 3 when both are open.
- After a clean mash soak meeting the preconditions above, Phase 3 is the next input milestone.

## Verify

Re-soak stick onset + movement with `SSB64_NETPLAY_STRICT_INPUT=1`. Expect:

- `REMOTE_CONFIRMED_REPLACE_NEWER` on ticks the sender never simulated: **gone** (provisional rows are gap-filled; only real input commits strict)
- Confirmed `REMOTE_PUBLISH` lines use `source=ledger_wire` or `source=ledger_seal` (Phase 2)
- `REMOTE_PUBLISH_SEAL_OVERRIDE` / thin `confirmed_write_once` only on non-ledger conflicts (hold-last invent)
- Replay checksums match; `state_diverge=0`; no `PEER_SNAPSHOT_DIVERGE` from asymmetric stick
- Forced prediction-miss mash still OK before Phase 3 deletes remaining write-once gates
- Soak `2239186208`-class: `LOADSAFE_PROMOTE` present, no load_tick rewind; Phase 3 still gated on clean FC (JumpAerial PASS harden) + another mash without write-once-related forks
