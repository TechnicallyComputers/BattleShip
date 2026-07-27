# Frame-commit pairing grid (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Bucket:** frame-commit / hash_confirm prerequisite  
**Blocks:** [hash_confirm StickReplace](netplay_hash_confirm_runway_align_2026-07-26.md)

## Symptom

Matched soak `seed=2908879106` (~1814 ticks):

```
FRAME_COMMIT_DIAG sent=17 recv=17 compared=0 pairing_fail=0 …
```

Tokens exchanged, **never compared**. `LastFrameCommitStateAgreedTick` stayed unset → StickReplace `hash_confirm` always fail-closed (`skipped_hash_confirm=0`) despite ~47 Android same-intent ≤12 ledger GGPOs that needed it.

## Root cause

`syNetPeerFrameCommitAfterCompletedSimStep` minted when:

```text
validation_tick = completed + 1
(validation_tick - Last) >= fc_interval
```

Intro Wait advances `Last` with each local Wait tick (no send). Peers leave Go with **different `Last`**, so the first post-Go mint picks **different `validation_tick`** (e.g. 518 vs 520). Recv stores peer pending under one id; local mint looks up another → `compared` never increments. Item/Ness **stress interval cap** (40 vs 120) could widen the same class of mismatch.

## Fix (`port/net/sys/netpeer.c`)

Mint only on a **shared session grid**:

- `fc_interval = syNetSessionParamsGetEffectiveFrameCommitValidationTicks()` (default 120)
- require `completed_tick % fc_interval == 0`
- `validation_tick = completed_tick + 1` (snap @ `completed` unchanged)
- do **not** use `FrameCommitIntervalCap` to choose the validation id

Deferred GGPO cover still skips without bumping `Last` (retry at next grid point).

## Acceptance

Matched APK + Linux soak:

- `FRAME_COMMIT_DIAG … compared > 0` (roughly `sent` / near `floor(ticks/interval)`)
- `pairing_fail` stays low; state diverge only on real forks
- `skipped_hash_confirm` / `class=hash_confirm` can rise above 0 when predicted micro/continuity REPLACE hits after FC agreed
- 0× new intro/Go FC storms

## Related

- [`netplay_hash_confirm_runway_align_2026-07-26.md`](netplay_hash_confirm_runway_align_2026-07-26.md)
- [`netplay_intro_wait_advance_frontier_deadlock_2026-07-18.md`](netplay_intro_wait_advance_frontier_deadlock_2026-07-18.md)
- [`netplay_frame_commit_pass_platform_fork_2026-07-04.md`](netplay_frame_commit_pass_platform_fork_2026-07-04.md)
