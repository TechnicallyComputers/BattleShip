# Turn `lr_dash` status_vars scrub → synctest restore clears InvertLR sticky (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** soak1 session `1579824759` seed `2715741274` — Android client ↔ Linux host  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `REPLAY_DETERMINISM`  
**Phase:** 3 of invent/contract pass (Turn/Dash `lr_dash` kill after Phases 1–2)

## Symptom

Peers soft-stable to ~2322 with matched sticks through load, then:

| Tick | Android P0 | Linux P0 |
|------|------------|----------|
| 1957–1959 | `lr_dash=1` `entry=1` | same |
| **1960** | still `lr_dash=1` `entry=1` | **`lr_dash=0` `entry=0`** |
| **1961** allow | `BRANCH_COMMITTED` **`did_dash=1`** → Dash | **`did_dash=0`** stays Turn |

Then `BASELINE_UNIVERSE` / `PEER` (inputs agree) → seal storm → `VS_SESSION_END` @2322.

Linux alone ran synctest / `emergency_restore` between 1959 interrupt and 1960 update (`SYNCTEST_OK tick=1958`). Android did not. `fhash_light` / `fhash_full` at 1959 still matched (`0xA9F3FFB5` / `0x3F8DEB65`) — `lr_dash` was not folded.

`tap_x` at 1960 is already 5 (`FTCOMMON_DASH_BUFFER_TICS_MAX` = 3), so `DashCheckTurn` cannot re-arm.

## Root cause

`syNetRbSnapScrubInactiveStatusVarsInBlob` zeroed inactive `dead` / `rebirth` / … overlays on every ring save. On LP64:

| Overlay | `sizeof` | Effect on Turn |
|---------|----------|----------------|
| `dead` | 16 | Zeros through `turn.lr_dash` at +12 |
| `rebirth` | 36 | Wipes the rest of the Turn overlay |

Live InvertLR Turn still has `lr_dash=±1`. Captured blob has `lr_dash=0`. Load → `syNetplayTurnSyncEntryLrDashAfterLoad` **Notes 0**, clearing the process-local entry sticky that Harden relies on. Peer without that restore keeps the pin → Turn vs Dash at allow with identical sticks.

Same poison class as JumpAerial / KneeBend / Damage hitstun scrub exemptions. Distinct from [future sticky](netplay_turn_entry_lr_dash_future_sticky_resim_2026-07-26.md) (live-ahead Note inventing Dash on earlier resim) and [stomp Harden](netplay_turn_lr_dash_stomp_fc_2026-07-19.md) (same-tick union clear without scrub).

Not invent micro / soft-refresh: Phase 1–2 ledger paths did not overlap this fork.

## Fix (`PORT && SSB64_NETMENU`)

| Layer | Change |
|-------|--------|
| Scrub | Early-return for `nFTCommonStatusTurn` / `TurnRun` in `syNetRbSnapScrubInactiveStatusVarsInBlob` |
| Light hash | Fold `turn.lr_dash` / `lr_turn` in `syNetSyncHashFighterStructLight` + blob light mirror |
| Field diff | `fold_turn_lr_dash` / `fold_turn_lr_turn` on load_drift bisect |
| Comment | `SyncAfterLoad` notes blob must keep `lr_dash` across scrub |

Offline / non-rollback unchanged. InvertLR sticky Harden + future `note_tick` gate kept.

## Acceptance (re-soak)

Matched APK + Linux binary, grounded InvertLR with stick held through allow; force or natural synctest mid-Turn (`SSB64_TURN_DASH_WITNESS=1`):

- After synctest/emergency restore mid-Turn: both peers `lr_dash=±1` `entry=±1` through allow
- Matching `did_dash` at allow with same sticks
- Optional: scrub poison would have shown `fold_turn_lr_dash` live≠blob before this fix
- No Turn vs Dash `STATUS_FORK` → PEER figh-only from this path

## Related

- [`netplay_turn_entry_lr_dash_future_sticky_resim_2026-07-26.md`](netplay_turn_entry_lr_dash_future_sticky_resim_2026-07-26.md) — future sticky / SyncAfterLoad
- [`netplay_turn_lr_dash_stomp_fc_2026-07-19.md`](netplay_turn_lr_dash_stomp_fc_2026-07-19.md) — why entry sticky exists
- [`netplay_jumpaerial_statusvars_scrub_synctest_2026-07-19.md`](netplay_jumpaerial_statusvars_scrub_synctest_2026-07-19.md) — same scrub class
- [`netplay_hold_last_same_intent_soft_refresh_2026-07-26.md`](netplay_hold_last_same_intent_soft_refresh_2026-07-26.md) — Phase 2 invent (not this kill)
- [`docs/refactor/ftstatusvars_overlay_map_2026-06-02.md`](../refactor/ftstatusvars_overlay_map_2026-06-02.md)
