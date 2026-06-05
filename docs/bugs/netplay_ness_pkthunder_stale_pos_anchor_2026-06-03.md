# Netplay — Ness PK Thunder stale `pkthunder_pos` anchor (2026-06-03)

**Date:** 2026-06-03  
**Status:** Revised (2026-06-04) — per-tick Hold sync reverted; jibaku keeps self-hit anchor  
**Area:** `port/net/sys/netplay_ness_pkthunder_gate.c`, `decomp/src/ft/ftchar/ftness/ftnessspecialhi.c`

## Symptom

Cross-ISA soak: after a late-game PK Thunder Hold + jibaku, Ness visibly snapped a long distance across Sector Z (reported: lower platform against the wall). `NESS_PKTHUNDER_GATE` showed `pkthunder_pos_refresh` blob anchors jumping thousands of units — e.g. hold entry tick 3794: `before=(0, 1280.66)` → `after=(3211, 4686)` where `1280.66` was the Y from a jibaku ~1400 ticks earlier. Sim hashes stayed matched (`state_diverge=0`, `figh_ok=1`).

## Root cause

Vanilla only writes `status_vars.ness.specialhi.pkthunder_pos` on thunder self-collision. Netplay added blob refresh on snapshot save (`RefreshPKThunderPosInBlobFromHead`) but **`RefreshPKThunderPosFromHead` was never called on live Hold ticks**, so `pkthunder_pos` could carry ghost Y (and X≈0) across throws. Jibaku launch angle uses `pkthunder_pos` vs fighter translate; stale anchors produced wrong coupling and visible snaps even when rollback hashes agreed.

## Fix

| Layer | Change |
|-------|--------|
| **Throw entry** | `NotifyThrowStarted`: zero `pkthunder_pos`; reset per-player stale witness flag. |
| **Hold entry** | `NotifyHoldEntered`: cull orphan weapons, refresh from live head, then stale witness; log fighter/anchor/head coords. |
| **Hold tick** | **Reverted 2026-06-04:** no per-tick head sync (vanilla self-hit anchor for jibaku). `SyncPKThunderPosDuringHold` = NaN probe only. |
| **Jibaku launch** | `RefreshPKThunderPosForJibakuLaunch`: repair corrupt anchors only (`NeedsApplyRepair`); do not override valid self-hit when head has orbited. |
| **Diag** | `pkthunder_pos_stale` witness when anchor/head delta > 128 units; `hold_enter` / `jibaku_trigger` logs include fighter + anchor + head positions. |

All paths netmenu-only (`PORT && SSB64_NETMENU` + `syNetplayRollbackSemanticsActive()` on decomp hooks).

## Verification

1. Cross-ISA Ness soak with `SSB64_NETPLAY_NESS_PKTHUNDER_GATE_DIAG=1`: no `pkthunder_pos_refresh` with `before=(0, <ghost Y>)` on hold entry after fix; `anchor` tracks `head` on `hold_enter`.
2. Reproduce late-game Hold → jibaku: no cross-map visual snap; optional `pkthunder_pos_stale` absent after first hold tick.
3. Offline / netmenu-off binary: unchanged (stubs + no rollback hooks).

## Related

- [`netplay_ness_pkthunder_hold_sanitize_race_2026-06-03.md`](netplay_ness_pkthunder_hold_sanitize_race_2026-06-03.md)
- [`netplay_ness_pkthunder_jibaku_quantize_2026-06-01.md`](netplay_ness_pkthunder_jibaku_quantize_2026-06-01.md)
