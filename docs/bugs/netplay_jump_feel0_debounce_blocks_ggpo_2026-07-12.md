# Jump onset: post-resim debounce blocks feel-0 provisional stick GGPO

**Date:** 2026-07-12  
**Session:** `2089186088` seed `3647187948` (Android client lp=1 ↔ Linux host lp=0, D=2, phase_lock=4)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

```text
[host/Android] SYNCTEST_FAIL tick=394
[guest/Linux]  PEER_SNAPSHOT_DIVERGE load_tick=419  figh only (world/rng/map MATCH)
```

P1 (Android local) Wait→KneeBend@391 on Android; Linux first-pass still Wait@391 (predicted neutral), then GGPO resim@391→394. After `resim complete`, both post-hashes matched — then sticks forked:

| tick | Android STICK p1 | Linux STICK p1 (last) |
|------|------------------|------------------------|
| 391  | 11,75            | 11,75 (after resim)    |
| 394  | 15,55            | 13,71                  |
| 395  | 13,31            | 15,55                  |

Linux logged `REMOTE_CONFIRMED_REPLACE_NEWER` 13,71→15,55 at sim 394 and stamped 394–396 with the same stick via feel-0 send-lead frames — **0×** `GGPO input correction queued` until the next onset @420.

## Root cause

1. Feel-0 send-lead packs provisional hold-last rows into the INPUT window. Host stores them as `RemoteConfirmed` and may sim with HID@T for sim T+k.
2. First jump correction (neutral→11,75 @391) resims correctly; `ArmDebounceAfterResim` sets `DebounceUntilSim = tick+3`.
3. `syNetRollbackShouldQueueGgpoCorrection` used only `MismatchAllowedDuringDebounce`. During those 3 frames, mismatch ticks **≥** `LastCommitted` (391) are denied — including 394, where the real stick (15,55) replaces the provisional (13,71).
4. `CommitRemoteConfirmedWire` therefore skips feel-0 / late-wire GGPO gates, `Promote`s the new stick into published history, and leaves live state that already advanced with the provisional → permanent Jump phase / TopN fork → synctest fail → baseline `figh` diverge.

Secondary: after feel-0 marks the superseded publish predicted, `RequestInputCorrection` re-ran `ShouldDeferPredictedAnalogCorrection`, which treats analog→neutral like “onset ahead of wire” and can no-op even when `GetTick() > sim_tick`.

## Fix

1. **`netrollback.c`** — `syNetRollbackShouldQueueGgpoCorrection` → `CorrectionAllowedAtTick` (episode window `[LastCommitted, +phase_lock]` still admits continued replaces during debounce).
2. **`netinput.c`** — `ShouldDeferPredictedAnalogCorrection` returns FALSE when `GetTick() > sim_tick` (already-simulated wire replace must rewind).

## Verify

- Re-soak Android ↔ Linux jump shortly after GO: after first `GGPO … queued` / `resim complete`, expect further `GGPO … queued` on provisional→real stick REPLACE (grep `feel0_provisional_replace` / `late_wire_completed_sim` with patch log if enabled).
- Matching KneeBend/Jump onset ticks; no `SYNCTEST_FAIL` at first jump; no `PEER_SNAPSHOT_DIVERGE` figh-only from stick phase lag within ~50 ticks of the jump.
