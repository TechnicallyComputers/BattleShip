# Netplay — Ness PK Thunder air Hold timer carryover (2026-06-03)

**Date:** 2026-06-03  
**Status:** Fix shipped (soak pending)  
**Area:** `decomp/src/ft/ftchar/ftness/ftnessspecialhi.c`, `port/net/sys/netplay_ness_pkthunder_gate.c`

## Symptom

Netplay aerial PK Thunder Hold felt “too strong” vs offline: Ness floated longer before gravity/drift transitions and self-hit timing felt late. Cross-ISA soak was stable but gameplay diverged from vanilla feel.

## Root cause

Netplay rollback policy re-armed timers that vanilla treats as **continuous from Start through Hold**:

1. **`HoldInitStatusVars`** reset `pkjibaku_delay=30` and `pkthunder_gravity_delay=25` on every Hold entry, undoing Start-phase countdown (~7–10 frames).
2. **`HoldSwitchRefreshDelay`** blind re-armed full gravity (and sometimes jibaku) delay on ground/air Hold platform switches.
3. **`sanitize_delay`** restored scrubbed zero delay to full `30` instead of hold-local floor; **`sanitize_gravity`** used hold-only `status_total_tics`, over-restoring when Start countdown should carry over.
4. **`HoldJibakuCollideBlocked`** gated self-hit on Hold-local `status_total_tics < 30`, redundant with vanilla `pkjibaku_delay` and wrong once carryover is restored.

## Fix

| Layer | Change |
|-------|--------|
| **Hold init** | Preserve `pkjibaku_delay` / `pkthunder_gravity_delay` from Start; still clear trail passive ring + `is_thunder_destroy`. |
| **Hold switch** | Re-sync hold-entry tracking only; no timer re-arm. |
| **Sanitize** | Restore scrubbed delay/gravity from hold-entry tracking (`entry_delay - hold_frames`); skip bump when live counter > 0. |
| **Collide** | Remove extra `HoldJibakuCollideBlocked` gate; vanilla `pkjibaku_delay` is authoritative. |

## Verification

1. Netplay VS: aerial UP+B Hold drift/gravity transitions match offline Training (same netmenu build, rollback inactive).
2. Cross-ISA soak: no new Hold/jibaku desyncs; gate diag `sanitize_delay` shows `expected=` from tracking, not blind `now=30`.
3. Rollback mid-Hold: scrub recovery restores timers from entry tracking without full re-arm.

## Related

- [`netplay_ness_pkthunder_hold_early_exit_pass_floor_2026-06-02.md`](netplay_ness_pkthunder_hold_early_exit_pass_floor_2026-06-02.md)
- [`netplay_ness_pkthunder_resim_sanitize_2026-06-01.md`](netplay_ness_pkthunder_resim_sanitize_2026-06-01.md)
