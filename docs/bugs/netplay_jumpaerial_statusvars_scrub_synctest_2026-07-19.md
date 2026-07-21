# Netplay — JumpAerial status_vars scrub → SYNCTEST SNAPSHOT_FIDELITY

**Date:** 2026-07-19  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** `1977761953` (Android client + Linux host), seed `2601855221`

## Symptom

Both peers identical:

```
SYNCTEST_FAIL tick=631 bucket=SNAPSHOT_FIDELITY
LOAD_HASH_DRIFT tick=631 figh=…/… (all other partitions MATCH) class=snapshot_fidelity
fighter_field_diff … player=1 light_ok=0 full_ok=0 anim_ok=1 status=25 motion=19
```

Forward `sim_state` / slot hashes at 631 matched cross-peer. P1 Kirby SoftLipPhase @631:
`status=25` (JumpAerialB), CLIFF sticky, nonzero `ja_vel_x` / `ja_drift`.

Later `RESIM_STICK_FORK` @1022+ / Hold gravity asym are secondary (session already unstable).

## Root cause

`FTCommonStatusVars` is a union. `jumpaerial` aliases `attackair` / `dead` / `rebirth` / …

`syNetRbSnapScrubInactiveStatusVarsInBlob` zeroed those inactive overlays on every ring
save. For JumpAerialF/B that **poisons** captured `drift` / `vel_x` / `turn_tics` in the
blob while `slot.hash_fighter` is still folded from **live** (which keeps real `ja_*` via
`syNetSyncHashFighterStructLight`). Synctest load restores scrubbed zeros → local
round-trip `figh` diverge → `SNAPSHOT_FIDELITY`.

Same poison class as Twister / Cliff / Ness PK Thunder / Samus charge scrub exemptions.
KneeBend / GuardKneeBend own `kneebend` at the same union offset and are hash-folded the
same way — exempted together.

Tooling gaps that hid the field name:

1. `syNetRbSnapHashFighterBlobLight` omitted JumpAerial / KneeBend NETMENU folds that live has.
2. Second-layer `fighter_field_diff` omitted `fold_ja_*`, `fold_kb_*`, `fold_vel_air_y`,
   `fold_coll_pos_prev_y` → `light_ok=0` with zero `field=` lines.

## Fix

In `port/net/sys/netrollbacksnapshot.c`:

1. Early-return scrub for JumpAerialF/B and KneeBend / GuardKneeBend.
2. Mirror KneeBend + JumpAerial folds into `syNetRbSnapHashFighterBlobLight` (`SSB64_NETMENU`).
3. Second-layer load_drift: `fold_ja_vel_x` / `fold_ja_drift`, KneeBend folds, plus
   `fold_vel_air_y` / `fold_coll_pos_prev_y`.

`scripts/netplay-scan-drift.py`: `STATUSVARS_SCRUB` when `SYNCTEST_FAIL` coincides with
JumpAerial/KneeBend `light_ok=0` or `fold_ja_*` / `fold_kb_*` field diffs.

## Test plan

- [ ] Re-soak Kirby/Ness JumpAerial along Dream Land CLIFF with synctest on; confirm no
      `SYNCTEST_FAIL` / `LOAD_HASH_DRIFT` at JumpAerial ticks with nonzero SoftLip `ja_*`.
- [ ] If fidelity still fails, expect `fold_ja_vel_x` / `fold_ja_drift` lines under
      `fighter_field_diff tag=load_drift` (no longer silent `light_ok=0`).
- [ ] scan-drift should tag `STATUSVARS_SCRUB` only on residual scrub-class fails.
