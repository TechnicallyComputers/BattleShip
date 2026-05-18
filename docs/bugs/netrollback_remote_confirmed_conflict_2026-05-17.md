# NetRollback remote confirmed conflict

**Date:** 2026-05-17  
**Status:** RESOLVED

## Symptoms

After rollback resim began running full battle updates, host and client stayed visually coherent but deterministic state diverged quickly. Logs showed no `LOAD_HASH_DRIFT`, yet the first client rollback compared two supposedly confirmed remote rows for the same tick:

- published history: `src=1 pred=0`, `sy=-85`
- remote ring: `src=1 pred=0`, `sy=0`

That means a remote ring row that had already been used by sim was overwritten by a later packet carrying a different value for the same wire tick.

## Root Cause

The packet staging path unconditionally wrote every INPUT bundle row into `sSYNetInputRemoteHistory`. Redundant or out-of-order UDP bundles could therefore replace an existing confirmed row with stale data. Rollback then treated the stale replacement as authoritative and resimulated toward it, causing confirmed-vs-confirmed mismatches and state divergence.

## Fix

Remote input ingest now treats confirmed rows as immutable:

- Empty row: store incoming confirmed input.
- Predicted row: replace with incoming confirmed input.
- Identical confirmed row: accept as duplicate.
- Conflicting confirmed row: keep the original row, rate-limit a `REMOTE_CONFIRMED_CONFLICT` log with packet sequence/current tick/frame index, and do not update the remote ring.

This preserves the first confirmed value used by simulation and turns stale duplicate packets into diagnostics instead of rollback authority changes.
