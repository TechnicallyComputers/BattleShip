# GuardOff / GuardSetOff neutral grab blocked (netplay) — 2026-06-07

**Status:** FIX SHIPPED (soak pending)  
**Scope:** `decomp/src/ft/ftcommon/ftcommonguard2.c`

## Symptom

Netplay only (`SSB64_NETMENU=ON`, rollback active). Offline grab-after-shield works.

1. Hold R (shield up) for a while, release R, then immediately re-press R expecting a grab.
2. Grab never fires; fighter stays in shield drop / re-enters shield instead.
3. Logs (player doing the input): **`Catch(162)` = 0**, **`CatchWait(164)` = 0** for the whole session despite hundreds of ticks in `GuardOff(154)` (572×) and regular `152→153→154` cycles (~11-tick GuardOff run length).

User input: **release R → re-press R** (no separate A press). Grab should not be blocked by lingering shield *sub-state* when the button is released and re-pressed.

## Root cause

Vanilla guard status interrupt wiring:

| Status | `proc_interrupt` | Catch check? |
|--------|------------------|--------------|
| `GuardOn` (152) | `ftCommonGuardCommonProcInterrupt` | Yes (A-tap via `ftCommonCatchCheckInterruptGuard`) |
| `Guard` (153) | `ftCommonGuardCommonProcInterrupt` | Yes |
| `GuardOff` (154) | **`NULL`** | **No** |
| `GuardSetOff` (155) | **`NULL`** | **No** |

Offline, input-delay is absent: release R → short `GuardOff` → `Wait` → re-press R is evaluated in `Wait` where `ftCommonGroundCheckInterrupt` runs `ftCommonCatchCheckInterruptCommon` / Attack11 Z-tap paths → grab.

Netplay applies inputs through the delay buffer (`D=2` typical). The re-press edge lands **inside the ~11-tick `GuardOff` window**, which has no interrupt table. The Z press-edge is consumed with no catch transition; when `GuardOff` ends, held R re-enters `GuardOn`/`Guard` (shield), never `Catch`.

This is not a stale `is_shield` heal issue (Phase 34c applied-input predicate is clean: 0× `z_auth=0 z_live=1` in the post-fix soak). The grab input simply never reaches a status that checks for catch.

## Fix

Netmenu-only, runtime-gated forward-sim policy in `ftcommonguard2.c` (`PORT && SSB64_NETMENU`, `syNetplayRollbackSemanticsActive()`):

- **`ftCommonGuardNetplayCatchCheckInterruptGuardDrop`:** vanilla-aligned catch only — **`ftCommonCatchCheckInterruptCommon`** (Z held + A tap) then **`ftCommonCatchCheckInterruptGuard`** (A tap). **Not** `ftCommonCatchCheckInterruptAttack11` (Z tap alone; jab-chain-only in vanilla).
- Call at the top of **`ftCommonGuardOffProcUpdate`** and **`ftCommonGuardSetOffProcUpdate`** before vanilla update logic; early `return` when catch fires.

Offline binary and offline modes in the netmenu binary are unchanged (gate false → vanilla `NULL` proc_interrupt preserved).

Determinism: both peers run the netmenu binary; checks read only synced applied input (`button_tap` / `button_hold` on `fp->input`), same as existing catch interrupts → `figh` folds identically.

### Follow-up (2026-06-07): spurious Z-only grab

**Symptom:** After the initial fix, R-grabbing felt normal overall, but **pressing Z alone sometimes triggered grab** during shield play (re-shield after drop read as grab).

**Cause:** Initial fix called `ftCommonCatchCheckInterruptAttack11` first — that function fires on **`button_tap & button_mask_z` alone** and is only wired from the jab (`Attack1`) interrupt chain in vanilla (`ftcommonattack1.c`). Running it every tick in `GuardOff`/`GuardSetOff` turned any Z press-edge in the drop window into a grab, including presses meant to re-shield.

**Correction:** Drop Attack11 from the helper; keep Common (Z hold + A tap) + Guard (A tap) only. Z alone no longer grabs from the drop window; grab during drop requires A (with or without Z held), matching vanilla neutral/shield-grab semantics.

## Verify

Re-soak netplay with release→re-press after sustained shield:

- Grab during `GuardOff`/`GuardSetOff` requires **A** (Z hold + A or A alone) — **no Z-only grab** from the drop window.
- Pressing Z alone during/after shield drop should **re-shield** (GuardOn), not grab.
- No regression on sustained R hold (should stay in `Guard(153)`, not oscillate 152↔154).
- Offline / `syNetplayRollbackSemanticsActive()==FALSE`: no behavior change.
- `build-netmenu` + `build-offline` link clean.

Related: [netplay_guard_shield_tap_churn_2026-06-05.md](netplay_guard_shield_tap_churn_2026-06-05.md) (Phase 34c applied-input predicate; separate from this status-window issue).
