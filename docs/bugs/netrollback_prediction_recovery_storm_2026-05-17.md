# NetRollback prediction recovery storm

**Date:** 2026-05-17  
**Status:** RESOLVED

## Symptoms

With `SSB64_NETPLAY_PHASE_LOCK_PREDICTION_TICKS=3`, one peer could enter a rollback storm while the other peer stayed clean. Logs showed repeated host-side mismatches over adjacent ticks, for example predicted remote stick rows such as `(0,0)` or an old diagonal being corrected by confirmed Link inputs a few ticks later. Visually this presented as fighter jitter and extreme pose/air-state popping during chained restores.

## Root Cause

The confirmed-input contract prevented predicted remote rows from satisfying admission and rollback lookup, but forward simulation was still allowed to continue predicting immediately after a rollback corrected a predicted row. If the remote player was changing stick direction every frame, the local peer would correct tick `N`, resim a short span, then immediately advance another speculative tick and correct `N+1`, producing one rollback per arriving confirmed row.

## Fix

When rollback corrects a published history row that was originally simulated from predicted remote input, rollback now arms a short recovery window equal to the configured phase-lock prediction window. During that recovery, exact confirmed remote rows still advance normally, but missing remote rows are not admitted through prediction. This keeps multi-tick prediction available in normal play while forcing the sim to catch up to confirmed input after a correction storm begins.

## Verification

Build target: `ssb64`.
