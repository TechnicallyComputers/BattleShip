# Mid-game ICE reconnect (2026-06-01)

## Symptom

ICE automatch VS aborted ~3 s after a brief WiFi/LTE drop: strict remote-miss (`'R'`) hold hit `SSB64_NETPLAY_STRICT_R_ABORT_FRAMES` (~180 frames) and called `syNetPeerStopVSSession`.

## Root cause

No mid-match reconnect path existed. libjuice rejects in-place ICE restart; the only recovery was full automatch bootstrap. Sim catch-up over 30 s is infeasible (input ring / gap-fill caps).

## Fix

- Symmetric sim-tick hold (`'H'`) with transport-coordinated pause + overlay
- ICE agent destroy/re-gather + HTTPS trickle on preserved ticket; server `connect_epoch` bump via `POST ice/restart`
- 30 s host forfeit → normal VS results + server `POST result` (`forfeit_timeout`) / `GET outcome`
- Defer `syNetPeerClearAutomatchBootstrapContext()` until match truly ends

## Automatch / staging SIGSEGV (2026-06-01 follow-up)

Reconnect hold called `ifCommonBattlePauseSetupFromPlayer` while VS bootstrap was active (`execution ready` but `game_status != Go`, `fighter_gobj == NULL`), e.g. Android `NetworkMonitor` or `mmIceIsFailed` during ICE connect.

**Fix:** `syNetReconnectMidMatchEligible()` (VSBattle scene, session params, exec/ingress gates, post–execution-begin boot window, one-shot remote `wire_base` arm); deferred `NetworkMonitor.install` on SDL_main via `port_android_network_drain()` (never JNI from the game coroutine fiber); gate hold/ICE-fail/disconnect/network paths; skip pause UI without safe fighter; null `fighter_gobj` guard in `ifCommonBattlePausePlayerCanRequestPause`.

## Files

`port/net/sys/netreconnect.c`, `mm_ice_reconnect.c`, `scvsbattle_reconnect.c`, server migration `20260601000000_match_result_metadata.sql`
