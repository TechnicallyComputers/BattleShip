# Netplay — Ness PK Thunder jibaku backwards ledge snap (2026-06-03)

**Date:** 2026-06-03  
**Status:** Fix revised (soak pending)  
**Area:** `port/net/sys/netplay_ness_pkthunder_gate.c`, `decomp/src/ft/ftchar/ftness/ftnessspecialhi.c`

## Symptom

Cross-ISA soak (Sector Z): after Hold → jibaku, Ness visibly launched backwards and snapped onto a ledge (`air_jibaku_ground_snap` @3091, `procmap_pass_cliff`, `floor_flags=0x8000`). Offline play at the same geometry does not show this. Sim hashes stayed matched through the event (`figh_ok=1`).

## Root cause

Rollback-only interactions:

1. **Orphan PK weapon chain** — deferred jibaku cull left `weapons=5` at trigger; extra segments shift self-hit timing vs offline single-head collide.
2. **Hold `pkthunder_pos` sync** — per-tick head refresh during Hold overwrote the anchor on the self-hit frame; offline only sets `pkthunder_pos` in `CheckCollidePKThunder`.
3. **Duplicate ground-router hooks** — `ftNessSpecialHiJibakuSetStatus` called `Refresh/Notify` before `goto setair`, then air SetStatus ran them again (double `jibaku_trigger` @1259).
4. **Stale hold anchor at entry** — `NotifyHoldEntered` witnessed stale `(0,0)` before head refresh; orphan weapons could shift self-hit timing vs offline.

## Fix

| Layer | Change |
|-------|--------|
| **Self-hit coupling** | `PrepareHoldSelfHitCoupling`: cull orphan PK weapons keep-head before Hold `CheckCollide` when jibaku-eligible (rollback-active only). |
| **Hold anchor** | Skip `SyncPKThunderPosDuringHold` on jibaku-eligible Hold ticks; let `CheckCollide` own the contact anchor (offline parity). |
| **Launch refresh** | `RefreshPKThunderPosForJibakuLaunch`: cull first; preserve fresh collision anchor when head within stale threshold; refresh only when stale. |
| **Ground router** | Remove duplicate Refresh/Notify from ground `JibakuSetStatus` entry (air path owns netplay hooks). |
| **Jibaku notify** | Per-player same-tick dedupe in `NotifyJibakuTriggered`. |
| **Hold entry anchor** | `NotifyHoldEntered`: cull orphan PK weapons, refresh anchor from head, then stale witness (no cliff edge guard — vanilla slide-off at ledges). |

All paths netmenu-only (`PORT && SSB64_NETMENU` + `syNetplayRollbackSemanticsActive()` on decomp hooks).

## Verification

1. Cross-ISA Sector Z Hold → jibaku on platform edges: no backwards ledge snap or cross-map launch snap; Ness slides off edges like offline (no `cliff_edge` ground-snap block).
2. No duplicate `jibaku_trigger` same tick from ground hold → pass-floor → air path.
3. `jibaku_weapon_state weapons=1` (or 0 post-cull) at trigger, not 5.
4. Offline / netmenu-off binary unchanged.

## Related

- [`netplay_ness_pkthunder_hold_early_exit_pass_floor_2026-06-02.md`](netplay_ness_pkthunder_hold_early_exit_pass_floor_2026-06-02.md)
- [`netplay_ness_pkthunder_stale_pos_anchor_2026-06-03.md`](netplay_ness_pkthunder_stale_pos_anchor_2026-06-03.md)
- [`netplay_ness_pkthunder_orphan_weapons_crash_2026-06-01.md`](netplay_ness_pkthunder_orphan_weapons_crash_2026-06-01.md)
