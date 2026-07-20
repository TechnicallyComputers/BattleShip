# Ness jibaku stick GGPO storm + PK-wave eff LOAD_HASH_DRIFT

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-17  
**Session:** `11903082` seed `705580761` (Android client ↔ Linux host, Dream Land, Ness ditto)

## Symptom

```
LOAD_HASH_DRIFT tick=660 … eff=0xE8A3695A/0x08194CA5  (figh/wpn/map/rng match; repair-ok)
… ness_pk_defer TryBegin held while sim advances …
resim begin load_tick=660 mismatch=661 target=679 span=18
…
PEER_SNAPSHOT_DIVERGE load_tick=691 partition=figh (player=1)
```

Synctest 5 OK / 0 FAIL; scan-drift `diverged=eff` UNRESOLVED at 660; session stop on peer snapshot.

## Root cause (two layers)

### 1. Stick-only GGPO during jibaku → deferred span storm

| tick | event |
|------|--------|
| 633 | Hold stick GGPO tip resim (span 2) OK |
| 660 | AirHiJibaku entry (`status_tics=0`) |
| 661 | Stick REPLACE `sy=4`→`15` (buttons match) queues GGPO |
| 661–678 | `try_begin_fail stage=ness_pk_defer` + livecap lift; target grows each tick |
| 660→679 | Finally begins **span-18** resim; Android saw `drift_ticks=31` baseline |

Jibaku launch velocity is locked at entry — stick cannot change the trajectory. Deferring the correction until jibaku exits only widens the predict window and poisons the follow-on cascade (figh diverge @691).

Hold/Start stick REPLACE must still rewind (thunder aim).

### 2. PK-wave eff fold includes `is_effect_attach`

At jibaku@660 the wave shell (`respawn=5`) is still folded. Fold mixed:

- `status_id` + **`is_effect_attach`** + `status_total_tics` (not `anim_frame`)

`is_effect_attach` is **not** in the fighter hash. Load/prune around jibaku catch-up mutates attach while the shell stays live → local slot vs post-load live eff hash forks with `fighter_field_diff` full_ok. Soft `repair-ok` continued; the storm above is what killed the session.

## Fix

| Layer | Change |
|-------|--------|
| **Input** (`netinput.c`) | `syNetInputStickReplaceNeedsRewind`: if buttons match, not a release, and `syNetplayNessPlayerInJibakuStickAbsorbScope(player)` (jibaku/bound) → Promote-only (`class=jibaku_stick`), no GGPO |
| **Gate** (`netplay_ness_pkthunder_gate.c`) | New per-player jibaku/bound stick-absorb predicate |
| **Eff fold** (`netsync.c`) | PK-wave fold drops `is_effect_attach`; keep `status_id` + `status_total_tics` |

## Verify

Re-soak Ness ditto PK Thunder through jibaku with stick noise:

- No stick-only `GGPO … queued` during jibaku/bound (may log `skipped class=jibaku_stick`)
- No `LOAD_HASH_DRIFT … eff=` at jibaku entry with figh match
- No `ness_pk_defer` target growth from stick REPLACE; no span≫2 resim solely from mid-jibaku stick
- `netplay-scan-drift.py` RESULT PASS

## Related

- [`netplay_ness_pk_hold_aim_ggpo_defer_2026-07-15.md`](netplay_ness_pk_hold_aim_ggpo_defer_2026-07-15.md) — mid-Hold GGPO allowed (aim)
- [`netplay_ness_pkwave_eff_frame_commit_2026-07-10.md`](netplay_ness_pkwave_eff_frame_commit_2026-07-10.md) — fold `status_total_tics` not `anim_frame`
- [`netplay_ness_pkwave_jibaku_eff_fold_dropout_2026-07-16.md`](netplay_ness_pkwave_jibaku_eff_fold_dropout_2026-07-16.md) — classifier scope through jibaku
- [`netplay_ness_pk_defer_ggpo_livecap_deadlock_2026-07-13.md`](netplay_ness_pk_defer_ggpo_livecap_deadlock_2026-07-13.md) — livecap lift while defer holds TryBegin
