# Netplay: RebirthWait stick drop → FC figh inputs=DIFFER races input GGPO — 2026-07-12

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-12  
**Seed:** `2653556481` (session `984995323`, Android client ↔ Linux host, Dream Land)

## Symptom

Synctest OK, LOADSAFE healthy, then:

- `FRAME_COMMIT_STATE_DIVERGE @1560` **`diverged=figh`**, scan tags `inputs=DIFFER`
- Snap 1559: Android P0 still **RebirthWait** (`status=9`); Linux P0 **Fall** (`status=26`, `ga=Air`, tics=0)
- Android: `GGPO input correction queued` @1559 `pred sx=0,0` → remote `sx=78 sy=-2` (correct)
- Linux: no input GGPO (local authority); arms **FC state resim** `mismatch=1560 corrected=-1`
- Competing episodes (Android input @1559 vs Linux FC @1560) → seal deepen / `RESIM_BASELINE_TIMEOUT` / `VS_SESSION_END`

First `STICK_SAMPLE` matched neutrals through 1558; Linux stick onset @1559 is the real diverge.

## Root cause

1. **Vanilla:** `ftCommonRebirthWait` stick/ground interrupt → `ftCommonFallSetStatus`. Feel-0 prediction keeps remote at neutral → peer stays in RebirthWait while authority drops to Fall.

2. **Input stack correctly queues GGPO** on the predicting peer.

3. **FC still compares the forked snap** and, when `inp` digests differ, arms **state-only** recovery (`correction_player=-1`). That races the stick episode, forks seal tuples, and kills the session.

## Fix

Under `PORT && SSB64_NETMENU`:

1. **`syNetRollbackDeferredInputCorrectionCoversTick`** — query deferred input GGPO span.
2. **FC sample hold** — `syNetPeerFrameCommitAfterCompletedSimStep` skips minting tokens while deferred input GGPO covers the completed tick (does not bump the FC interval cursor).
3. **FC compare** — if state digests diverge **and** input digests differ: log `FRAME_COMMIT_INPUT_SKEW_PENDING_GGPO` / `_WAIT` and **do not** call state recovery / do not arm deferred state mismatch.
4. **HandleFrameCommitStateMismatchCore** — belt-and-suspenders same policy if invoked with input skew.

## Verify

Re-soak KO → RebirthWait → stick drop (authority mash during halo):

- Predicting peer: `GGPO … queued` then episode complete; **no** FC state resim / seal storm
- Authority peer: `FRAME_COMMIT_INPUT_SKEW_WAIT` (or no FC compare until after GGPO) — session continues
- No `VS_SESSION_END` from this class; post-GGPO FC digests agree
