# Dead* statusvars scrub + release-class stick GGPO → KO resim hang (2026-07-27)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** session `1945843913` (Hold clear; hang at fighter KO ~5171–5186)  
**Bucket:** FTStatusVars scrub / input contract (Dead* history admission)

## Symptoms

- KO @5171: transient `corrupt dead_gate union_wait=46 dead_gate_wait=0` at status entry (witness race before `SetWait`).
- While DeadLeftRight (`status=1`): LEDGER skipped onset/real_stick via `dead_ghost_stick`, then @5178 predicted `(−43,66)` → wire `(−4,27)` **queued GGPO** (no absorb skip).
- Resim load @5177: `dead_wait_union_mismatch union=0 bridge=39` / `corrupt dead_gate` spam.
- `BASELINE_UNIVERSE` deepen 5177→5171 (inputs agree); Android `rollback_epoch_cap=5182` blocked live advance for multiple seconds (recovered ~5215).

## Root cause

### 1. Capture scrub stomped `dead.wait`

`syNetRbSnapScrubInactiveStatusVarsInBlob` preserved `common.dead` for Dead* but still ran the **rebirth** memset when status was Dead. `ftCommonRebirthStatusVars` aliases and exceeds `ftCommonDeadStatusVars`, so capture’s pin of `dead.wait ← dead_gate_wait` was zeroed before the ring slot committed.

Apply restored `dead_gate_wait` from the bridge field but `status_vars.common.dead.wait=0` from the blob → witness mismatch and poisoned resim overlays. Deepen treated the load as replay-determinism (figh diverge) across the KO window.

Secondary scrub bug: dead memset ceiling was `DeadUpStar`, so **DeadUpFall** blobs were also scrubbed.

### 2. Dead stick absorb release hole

Existing `dead_ghost_stick` Promote-only required `!IsRelease`. Mag shed `(−43,66)→(−4,27)` is `IsRelease` under confirmed deadband → GGPO opened on an already-Dead snap tick. Stick cannot change Dead countdown hashed state; the episode only created apply/deepen surface for (1).

Not a new move-context absorb — closes the hole in the existing Dead* history admission and keys scope on **snap@sim_tick** when committed (live Dead* fallback).

## Fix

| Layer | Change |
|-------|--------|
| Scrub | Early-return for `DeadDown`..`DeadUpFall` (same pattern as Rebirth). Dead memset ceiling → `DeadUpFall`. |
| Capture | Re-pin `dead.wait` from `dead_gate_wait` **after** scrub. |
| Apply | `SetWait(bridge)` before `VerifyDeadWaitInvariant`. |
| Stick GGPO | `syNetplayPlayerInDeadGhostStickAbsorbScope(player, sim_tick)` — snap@tick Dead* first; absorb stick-only **including** release; buttons still rewind. |

## Acceptance (re-soak)

Matched APK + Linux through a mid-stock KO:

- No `BASELINE_UNIVERSE` deepen chain across `dead_init` from stick-only Dead REPLACE
- No multi-second `rollback_epoch_cap` hang at KO
- Few/no `dead_wait_union_mismatch` / `corrupt dead_gate` during Dead resim (entry-tick witness race OK)
- Stick REPLACE while snap@tick Dead* → `class=dead_ghost_stick` (incl. mag-shed), not GGPO
- Hold micro protect / `pct_R` still OK; no new jibaku/Dead invent absorb beyond this contract

## Related

- [`netplay_dead_stick_ggpo_resim_rng_whispy_blow_2026-07-20.md`](netplay_dead_stick_ggpo_resim_rng_whispy_blow_2026-07-20.md) — original Dead* stick absorb
- [`netplay_rebirth_halo_offset_dead_scrub_2026-07-03.md`](netplay_rebirth_halo_offset_dead_scrub_2026-07-03.md) — inverse alias (dead scrub stomped rebirth)
- [`netplay_dead_gate_force_sleep_synctest_2026-07-03.md`](netplay_dead_gate_force_sleep_synctest_2026-07-03.md) — apply-time Sleep / gate clear
- [`netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md`](netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md) — do not re-add move-context absorb
