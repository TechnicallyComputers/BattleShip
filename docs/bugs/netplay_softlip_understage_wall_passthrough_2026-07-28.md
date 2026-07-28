# Soft-lip suppress — under-stage wall / underside pass-through

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-28  
**Sessions:** soak1 Android ↔ Linux Dream Land seed `2412131430`

## Symptom

Fighters (JumpAerial / Fall) leave a Dream Land soft platform, travel under/beside the stage body, then rise back onto the main platform (`fline=3`) instead of colliding with stage walls. Both peers matched (gameplay regression, not desync).

| Window | Pattern |
|--------|---------|
| gut≈670–671 | Leave soft floor `fline=3→-1`, `fflags=PASS`, sticky latched |
| gut≈692+ | Below stage (`y<0`), softlip still armed, `mask_curr` never gets L/R wall |
| gut≈732–737 | `SoftLipX path=rwall_suppress` strips real wall hits (`status=24` JumpAerialF) |
| gut≈746–747 | `y` crosses 0 → `fline=3` on main platform |
| gut≈1057–1110 | P1 same class with `lwall_suppress` |

No Ness jibaku in this soak (status 231/236 absent) — jibaku harden carve-out does not apply.

## Root cause

Soft-lip AdjNew suppress (`mpProcessNetplaySuppressAdjNewWallSoftLipEx` / `SuppressAdjNewWallOnUnattachedSoftLip`) stayed armed for the whole under-stage trip:

1. `mpProcessSetCollProjectFloorID` on project **fail** set `floor_line_id=-1` but left stale `floor_flags & PASS`.
2. Floor CheckTest StickyNote latched those stale bits even on miss (`fline==-1`).
3. SoftLipEx re-Noted residual + swept PASS every wall pass, refreshing the latch forever.
4. Sticky cleared only on grounded solid FLOOR — never while airborne under the stage.

Original suppress target remains DamageFall / JumpAerial TopN.x drift on soft lips ([`direct_wall`](netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md), [`ceil_edge`](netplay_airborne_cliff_lip_ceil_edge_fc_drift_2026-07-18.md), [`sticky`](netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md)). Projected-lip frames (`fline≠-1`) still need suppress; blanket `fline==-1` disable would regress those.

## Fix

In `decomp/src/mp/mpprocess.c` under `PORT && SSB64_NETMENU`:

- **Project fail** — clear `PASS|CLIFF` from `floor_flags` (stop stale residual).
- **Sticky Note** — only from a live soft floor (`fline!=-1`) or a **successful** PASS\|CLIFF project; SoftLipEx does not re-latch when `fline==-1`.
- **Sticky TTL** — `MPPROCESS_NETPLAY_SOFTLIP_STICKY_TTL` (24) UpdateMains without refresh; expires airborne latch after leaving a soft floor. Rollback `SoftLipStickySet` restores full TTL (TTL not snapshotted).
- Swept-only SoftLipEx suppress requires `fline!=-1` (sticky TTL covers brief lip frames with `fline==-1`).

Jibaku harden path unchanged.

## Verify

Re-soak Android↔Linux Dream Land seed `2412131430` (or mash soak):

- Leave soft cloud → under stage → stage wall bounce / blast, no rise onto `fline=3` through the body
- No `rwall_suppress` / `lwall_suppress` with `fline=-1` after sticky TTL while deep under stage
- DamageFall / JumpAerial soft-lip TopN.x peer match still holds (projected `fline≠-1` + sticky TTL)
- Repackage AppImage **and** reinstall Android APK

Agent verify: `cmake --build build --target ssb64` only.

## Related

- [`netplay_airborne_cliff_lip_jibaku_clip_2026-07-18.md`](netplay_airborne_cliff_lip_jibaku_clip_2026-07-18.md) — jibaku blanket suppress clip (carve-out; different status class)
- [`netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md`](netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md) — why suppress cannot require `fline==-1` alone
- [`netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md`](netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md) — sticky latch (TTL bounds lifetime)
