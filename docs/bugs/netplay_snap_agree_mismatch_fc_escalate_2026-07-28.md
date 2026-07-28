# Netplay — snap_agree saw the fork for 300+ ticks; sparse FC grid detected it after ring eviction

**Date:** 2026-07-28  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** Android guest ↔ Linux host, Dream Land Ness ditto, seed `277502444`  
**Related:** [FC onset older than ring](netplay_fc_onset_older_than_ring_hard_fork_2026-07-27.md), [FC late mint on resim grid skip](netplay_fc_late_mint_resim_grid_skip_2026-07-28.md), [snap_agree watermark](netplay_snap_agree_hash_confirm_watermark_2026-07-26.md)

## Symptom

- First `SNAP_AGREE_MISMATCH snap=393` (figh only; world/rng/item/wpn match). Mismatch persisted — final diag `matched=34 mismatch≈237`.
- FC grid compares were sparse (`sent=3` over 720 ticks — light-episode churn ate boundaries even with late mint). First full FC compare with diverge: **validation=721**.
- `FRAME_COMMIT_INPUT_AGREE_ONSET validation=721 onset=433` but `min_load=594` (ring_cap=128, sim≈720) → onset **162 ticks older than the ring** → `ONSET_UNRECOVERABLE` fail-closed → `PEER_SNAPSHOT_DIVERGE` / `class=replay_determinism`, `recovery_started=0`.

Fail-closed was correct: no snapshot at onset 432 exists; a short resim cannot heal a fork that old. The failure is **detection latency**, not the recovery rule.

## Root cause

`syNetPeerSnapAgreeTryMatch` counted mismatches but was deliberately log-only ("FC grid owns diverge") because snap_agree packets carry no input digest and cannot drive input-agree onset classification themselves. With the FC grid starved to a handful of compares, a fork visible at snap 393 wasn't classified until FC@721 — long after the onset aged out of the 128-slot snapshot ring.

## Fix

`PORT && SSB64_NETMENU` — keep recovery semantics on the FC path; make detection follow snap_agree:

1. **`SNAP_AGREE_ESCALATE`** — consecutive-mismatch streak (monotonic in snap_tick, reset on any match at/after the last counted tick). Every N mismatches (default **8**, env `SSB64_NETPLAY_SNAP_AGREE_ESCALATE`, 0 disables) arm `syNetPeerFrameCommitArmLateMint(snap_tick + 1, "snap_agree mismatch streak")` — an **off-grid** FC token mint. Token digests are retro-safe (built from the snapshot ring at validation−1 + settled input history). Streak re-arms every N while the fork persists, so an eaten mint retries.
2. **Receive-side pairing arm** — `syNetPeerHandleFrameCommitPacket`, when a peer token arrives for a validation id with no local token and `validation_tick > LastFrameCommitValidationTick`, arm a late mint for that id. A unilateral escalation mint from either peer therefore always pairs (previously the pending peer slot could rot unmatched — off-grid ids were never locally minted).

With N=8, a fork at 393 gets a full FC compare (input digests + fighter slots) at ~validation 401–415, where `min_load` is still ~280 — onset in-ring, input-agree recovery can actually load at/before onset and resim.

## What this does not change

- snap_agree still never resims from its own 5-hash compare; classification/recovery stay on the FC token path (input digest certifies bilateral inputs).
- `ONSET_UNRECOVERABLE` fail-closed rule unchanged — it remains correct when onset genuinely predates the ring.
- Grid mints, late mint aging (`ring_cap-2`), and `LATE_MINT_FORCE` under deferred storm unchanged.

## Verify on re-soak

- On a persistent state fork: `SNAP_AGREE_ESCALATE snap=S streak=8` within ~8 ticks of first `SNAP_AGREE_MISMATCH`, followed by `FRAME_COMMIT_LATE_MINT validation≈S+1` on **both** peers (one via streak, one via `peer token unmatched`).
- `FRAME_COMMIT_INPUT_AGREE_ONSET` with onset **in-ring** and `recovery_started=1` (no `ONSET_UNRECOVERABLE` for forks born <ring_cap ticks ago).
- No FC mint storm during clean play (escalation requires 8 consecutive mismatches).

## Follow-on (2026-07-28)

Seed `1221028269`: escalate minted/paired from ~1591 but recovery only armed at 1697 — `INPUT_SKEW_WAIT` dropped every compare while digests disagreed. Bypass when streak ≥ escalate: [fc_escalate_input_skew_bypass](netplay_fc_escalate_input_skew_bypass_2026-07-28.md).
