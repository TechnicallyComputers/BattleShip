# Automatch startup pacing: watchdog, ICE poll, strict wire, pre-bind INPUT (2026-05-26)

**Date:** 2026-05-26  
**Status:** Fix shipped  
**Area:** `port_watchdog.cpp`, `scautomatch.c`, `mm_ice.c`, `mm_ice_automatch.c`, `netinput.c`, `netpeer.c`

## Symptoms (session `892224148`)

- No SIGABRT after ice signal queue mutex fix; ICE completed and VS loaded.
- Spurious **WATCHDOG HANG** during long `ICE_CONNECT` trickle polling (scene 66).
- **Asymmetric ICE nomination**: guest host↔host, host host↔relay → guest `recv=3` / `drop=716`, strict **R** abort at sim 1–4 (`pct_R≈98%`).
- Host **INPUT drop bind_not_complete** for seq 0–12 while guest still waiting on bind.

## Fixes

| Change | Purpose |
|--------|---------|
| `port_watchdog_set_connect_phase_pause` during `ICE_CONNECT` + `ENTER` bootstrap | Suppress false hang alarms during HTTPS/ICE waits |
| `mnVSNetAutomatchAMIceConnectTricklePollInterval` | Slow/skip trickle `GET` after `connected` + gathering quiet |
| Filter peer **relay** trickle when shared LAN; fail validation if nominated pair is relay on LAN | Prefer host↔host on same subnet |
| `syNetInputRollbackSimAdvanceAllowed`: block advance until `hr >= wire_base - lead_buffer` | Align sim progress with remote wire frontier |
| Default `STRICT_REMOTE_LEAD_BUFFER_TICKS=2` when unset | Extra startup slack without raising strict slack env |
| Stage INPUT while `bind` incomplete but battle gates not ready | Do not drop early peer bundles during staging |

**Follow-up (2026-06-02):** [netplay_input_bind_startup_cadence_2026-06-02.md](netplay_input_bind_startup_cadence_2026-06-02.md) — `INPUT_BIND` RTX used taskman `% 15` while exec-hold counted display-rate gate pumps; immediate send on `StartVSSession`, wall-ms RTX, bind during staging rendezvous.

## Verification

- LAN automatch: both sides `ICE: connected remote=` with host typ on LAN; no watchdog during connect; VS runs past sim 4 without strict-R abort.
- LTE cross-NAT: relay still allowed when `peer_lan` not shared.
