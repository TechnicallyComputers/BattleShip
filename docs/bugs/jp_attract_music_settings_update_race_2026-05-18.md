# JP build: attract/intro music silent — PORT audio settings-update race

**Date:** 2026-05-18
**Area:** `decomp/src/sc/scmanager.c` (`scManagerRunLoop`, `#ifdef PORT`
branch) ↔ `decomp/src/sys/audio.c` audio-thread settings-update restart
**Symptom (JP only):** attract/intro/title BGM silent. SFX works, stage
music works, JP text/assets fine. US plays the same sequence correctly.

## Root cause

`scManagerRunLoop` calls `syAudioSetSettingsUpdated()` then, on N64
(`#else`), spin-waits `while (syAudioGetSettingsUpdated()) ;` and
`while (syAudioGetRestarting()) ;` so the audio thread's
settings-update restart completes before scene dispatch. The `#ifdef
PORT` branch **skipped both waits** — old comment "Audio is stubbed on
PC — skip the spin-waits that would hang." That comment is obsolete: the
audio pipeline has run for real since 2026-04-24, so the restart now
executes asynchronously to scene dispatch.

The audio-thread settings-update path (`audio.c` ~1273–1311): it sums
`port_cmdLen` over sound players + non-`AL_STOPPED` CSPlayers. If
`port_cmdLen == 0` it does the full restart (`n_alClose` →
`portAudioLoadAssets` → `syAudioMakeBGMPlayers`) and **clears**
`dSYAudioIsSettingsUpdated`. Otherwise it calls `syAudioStopBGMAll()`
and leaves the flag set.

**US masks the race:** first scene is `nSCKindStartup` (~3 s N64 logo;
`scmanager.c:498–504` REGION split). During those 3 s all CSPlayers are
`AL_STOPPED` → `port_cmdLen == 0` → restart completes, flag cleared,
long before any BGM call.

**JP breaks:** first scene is `nSCKindOpeningRoom` immediately.
`mvOpeningRoomFuncStart` calls `syAudioPlayBGM(0, nSYAudioBGMOpening)`
(`mvopeningroom.c`, no REGION guard) while the flag is still TRUE.
`syAudioPlayBGM` sets `sSYAudioCSPlayerStatuses[0] = AL_PLAYING`, so the
audio thread now sees `port_cmdLen != 0`, takes the
`syAudioStopBGMAll()` branch (wipes the queued BGM) and does **not**
clear the flag — the attract BGM never starts. SFX and stage music are
entered ≫3 s later, after the flag has cleared, so they work.

Ruled out: JP vs US `B1_sounds1_ctl` / `S1_music_sbk` are byte-for-byte
identical (BankParser / ctl→bank mapping not the cause); the
`mvopeningroom.c` BGM trigger has no `#if REGION_US` guard. (Parallel
sub-agent investigation; mechanism then verified directly in
`scmanager.c` + `audio.c`.)

## Fix

Restore the settings-update / restarting waits in the `#ifdef PORT`
branch, yielding via `port_coroutine_yield()` instead of the N64
busy-spin. This is the **established codebase idiom** for PORT
audio-condition waits — `scautodemo.c` / `scvsbattle.c` already do
`while (syAudioCheckBGMPlaying(0)) { #ifdef PORT port_coroutine_yield();
#endif }`; those terminate on PORT, proving the audio restart runs
independently of the game coroutine. A `> 100000`-iteration cap with a
`port_log` diagnostic is a hang-proof backstop (directly addresses the
old comment's documented hang fear): if the flag never clears it logs
and falls through — no worse than the pre-fix skip, never an infinite
hang. The N64-only framebuffer clear stays in `#else`.

`extern void port_coroutine_yield(void);` added to the file's existing
PORT extern block (same pattern as sc1pgame.c / scvsbattle.c etc.).

## Verification

JP runtime log:
`scManagerRunLoop — past audio/FB setup (settings-update cleared after
16 yields)` — the restart cleared the flag in 16 cooperative yields
(not the cap; no hang). The game then ran the full attract loop
(scenes 27→28→29→30→33→31→34→36→32→35→37, incl. mvOpeningRoom) with no
hang, no crash, watchdog never firing. The documented wipe path is
structurally eliminated — the flag is clear in `scManagerRunLoop`, long
before any scene's `syAudioPlayBGM`. **Audible confirmation pending
user** (logs can't verify sound output). US regression (shared PORT
branch): rebuild + attract re-run.

## Notes

`scmanager.c` is decomp source (`decomp` submodule, branch
`jp-rom-support`/`port-patches`). Pure PORT-branch change — N64/US-ROM
matching builds (`PORT` undefined) are unaffected; US **port** build now
runs the same waits as N64 here (more correct than skipping; US first
scene is still `nSCKindStartup` so it clears immediately too). Submodule
pointer bump required when this lands (see CLAUDE.md submodule workflow).
Related JP bring-up: `jp_submotion_figatree_misclassified_2026-05-18`,
`jp_us_only_funcs_called_unconditionally_2026-05-18`.
