# Episode FSM seal skipped on frame-commit recovery (2026-05-24)

**Date:** 2026-05-24  
**Status:** FIX SHIPPED (soak pending)

## Symptom

WAN 1v1 resoak (`422146559`) with `FRAME_COMMIT_DIAG=2` and authority-digest patch:

- Clean FC through validation **360**; `FRAME_COMMIT_STATE_DIVERGE` @ **480** with matching `inp_*`, divergent `figh`/`rng`.
- Recovery: `resolved_load=360`, `EPISODE_FSM seal span=120 exceeds max — skip seal`, `EPISODE_REPLAY_DIVERGE` from tick **362** with neutral `inp=0x811C9DC5`.
- Match continued after `Commit -> Live` but replay could not reconstruct live fighter state.

## Root cause

1. **`syNetRollbackEpisodeFsmBegin`** clamped `target_tick` to `mismatch + 64` when the requested span exceeded `SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN` (64).
2. **`syNetRollbackEpisodeSealInputs`** was still called with the **unclamped** `target_tick` (e.g. validation+1 = 481), so `span = 120` and seal was **skipped** entirely (`inputs_sealed` stayed false).
3. Resim replay then used unpublished/neutral inputs (`syNetInputResolveFrame` fallback), not authority-sealed rows — immediate `EPISODE_REPLAY_DIVERGE` even though frame-commit input digests agreed.

Secondary: frame-commit state digests sampled **live** hashes on the advanced validation tick while rollback snapshots are saved for the **completed** tick before `syNetInputAdvanceAuthoritativeSimTick` (off-by-one vs ring). Post-verify `ftMainRebindStatusProcs` could clobber joint anim restored in `FinalizeLoad`.

## Fix

| Change | Location |
|--------|----------|
| Seal through `syNetRollbackEpisodeFsmGetTargetTick()` after FSM begin | `syNetRollbackEpisodeBegin` in `netrollback.c` |
| Raise `SYNETROLLBACK_EPISODE_SEAL_MAX_SPAN` to **128** (validation cadence 120 + slack) | `netrollback_episode.h` |
| Frame-commit `figh`/`world`/`item`/`rng` from ring slot at `validation_tick - 1` when present | `syNetFrameCommitBuildToken` in `netpeer_frame_commit.c` |
| Re-apply blob joint anim after post-verify rebind | `syNetRbSnapshotReapplyJointAnimAtTick` + `syNetRollbackLoadPostTick` |

## Verify

Re-soak same WAN pair with `FRAME_COMMIT_DIAG=2`:

1. On FC state diverge @480: expect `EPISODE_FSM seal_inputs` (not `skip seal`) with `span` ≤ 128.
2. No `EPISODE_REPLAY_DIVERGE` at 362 with neutral `inp` unless true sim drift remains.
3. Optional: `RESIM_RECONCILE_LOG=1` + `FIGHTER_SLOT_HASH_LOG` window 360–520 if `figh` still diverges at 480 (live determinism — separate from this seal bug).

## Related

- [`netplay_frame_commit_authority_digest_2026-05-24.md`](netplay_frame_commit_authority_digest_2026-05-24.md)
- [`netrollback_episode_input_seal_2026-05-20.md`](netrollback_episode_input_seal_2026-05-20.md)
- [`netrollback_fighter_midmatch_drift_2026-05-20.md`](netrollback_fighter_midmatch_drift_2026-05-20.md)
