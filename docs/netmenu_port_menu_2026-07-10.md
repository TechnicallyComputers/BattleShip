# NETMENU Port menu (full tree, allowlist-gated)

**Date:** 2026-07-10  
**Builds:** `SSB64_NETMENU=ON` uses [`port/gui/PortMenuNetplay.cpp`](../port/gui/PortMenuNetplay.cpp); offline keeps full [`PortMenu.cpp`](../port/gui/PortMenu.cpp).

## Why

Unreplicated LUS Gameplay CVars (notably **Flash on Failed Z-Cancel**) mutate fighter colanim / landing status mid-sim. Peers with different checkbox state fork GObj/snapshot state and look like “desync after lag.” See also [netplay_tap_jump_local_cvar_desync_2026-05-25.md](bugs/netplay_tap_jump_local_cvar_desync_2026-05-25.md).

## Product split

| Binary                        | Esc menu                        | Enhancement / cheat CVars                                                                                                                                                                                              |
| ----------------------------- | ------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Offline (`SSB64_NETMENU=OFF`) | Full PortMenu                   | Honor user CVars                                                                                                                                                                                                       |
| Netplay (`SSB64_NETMENU=ON`)  | PortMenuNetplay (same sections) | **Allowlist** in [`netplay_cvar_allowlist.cpp`](../port/enhancements/netplay_cvar_allowlist.cpp) — only audited-safe names are read; everything else under `gEnhancements.*` / `gCheats.*` returns vanilla (int **0**) |

Config path is still shared (`BattleShip` app dir). Integrity = **allowlist at every enhancement reader**. Stale offline keys in `BattleShip.cfg.json` are ignored on netplay; they are **not** bulk-zeroed (so offline settings survive).

## Menu presentation (netplay)

PortMenuNetplay builds the **same** Settings / Assets / About tree as offline (including Gameplay, Cheats, Tap Jump, Script Mods). After build, [`ApplyNetplayEnhancementVisualLocks`](../port/gui/PortMenu.cpp) greys out any widget whose `cVar` fails the allowlist: ImGui disabled styling, no interaction, hover tooltip explains netplay ignore. Checkbox/combobox **values still reflect shared cfg** so you can see what offline has enabled.

Script Mods panel: still listed, but wrapped in `BeginDisabled` with an explanatory note (`ApplyNetplayScriptModsVisualLock`).

Allowlisted widgets stay fully interactive (C-Stick / D-Pad / Analog Remap, Hitbox View, Disable HUD, Widescreen, Shuffle Music). Cheats stay visible but greyed — netplay **always** unlocks the full roster/stages regardless of cfg.

## Allowlist policy

Readers must use `port_enhancement_cvar_get_integer` / `port_enhancement_cvar_get_float` (not raw `CVarGet*`) for `gEnhancements.*` / `gCheats.*` (except unlock helpers, which are forced on — see below).

**Allowed on netplay (audited):**

| Name / prefix | Why |
|---------------|-----|
| `gEnhancements.CStickSmash.P*` | INPUT_PREWIRE before net HID latch |
| `gEnhancements.DPadJump.P*` | same |
| `gEnhancements.AnalogRemap.P*` (Enabled/Deadzone/Range) | same |
| `gEnhancements.HitboxView` | DISPLAY only |
| `gEnhancements.DisableHUD` | DISPLAY only |
| `gEnhancements.Widescreen` | DISPLAY / aspect latch |
| `gEnhancements.ShuffleMusic` | AUDIO only (local `rand`; peers may hear different tracks) |
| `gCheats.*` | Allowlisted for policy/docs; **runtime always unlocks all** via `port_cheat_unlock_*` returning 1 |

**Forced on netplay (not optional):** all character/stage/menu unlocks (`UnlockCheats.cpp` under `SSB64_NETMENU`) so every peer shares CSS/SSS content.

**Denied by default:** any other `gEnhancements.*` (including new offline hacks until explicitly allowlisted), Tap Jump, hazards, neutral spawns, CasualRules, CompRuleset, Music Selection, Classic Co-op, etc.

To ship a new enhancement on netplay: add it to the allowlist (or a dedicated game mode), audit sim impact — the Esc menu will automatically stop greying it out once allowlisted.

## Defense in depth

Tap Jump / stage hazards / neutral spawns also keep `syNetPeerIsVSSessionActive()` vanilla gates for VS sessions. Allowlist already denies them for the whole netplay binary.

## Packaging

`scripts/package-*.sh --netplay` / `-DSSB64_NETMENU=ON` must ship the netplay binary only. Offline packages must not use PortMenuNetplay.
