# Netplay: Failed Z-Cancel Flash colanim (unreplicated LUS CVar)

**Slug:** `netplay_failed_zcancel_flash_colanim_2026-07-10`  
**Status:** FIXED (NETMENU stripped menu + compile-time vanilla gates)  
**Severity:** High — snapshot / GObj divergence; can present as early-match “desync”

## Symptom

Automatch with one peer who enabled **Flash on Failed Z-Cancel** in the LUS Esc menu: instability / session abort around intro or early battle. Not a frame-commit state hash diverge in the soak that motivated the audit — the root class is unreplicated sim-affecting CVars.

## Root cause

`gEnhancements.CasualRules.FailedZCancelFlash` → `ftCommonAttackAirProcMap` → `ftParamCheckSetFighterColAnimID(..., nGMColAnimFighterDamageFireStart, 15)`. Colanim is fighter/GObj state in rollback snapshots. Auto Z-Cancel similarly changes landing status. Same class as tap-jump ([netplay_tap_jump_local_cvar_desync_2026-05-25.md](netplay_tap_jump_local_cvar_desync_2026-05-25.md)).

## Fix

1. **`PortMenuNetplay`** for `SSB64_NETMENU=ON` — no Gameplay casual / cheats / script-mods widgets ([netmenu_port_menu_2026-07-10.md](../netmenu_port_menu_2026-07-10.md)).
2. Enhancement helpers return vanilla under `#if defined(SSB64_NETMENU)` so stale offline cfg keys cannot re-enable the path.

## Audit hook

Any `port_enhancement_*` / `CVarGet*` under `decomp/src/ft|it|wp|mp|gm` during the sim tick without a netmenu/VS gate is a latent desync.
