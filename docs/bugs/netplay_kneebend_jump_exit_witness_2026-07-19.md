# Netplay — KneeBend→Jump exit witness (soak 343630197)

**Date:** 2026-07-19  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** INSTRUMENTATION (`PORT && SSB64_NETMENU`, re-soak to name writer)  
**Session:** soak1 `343630197` seed `395161118` (Android client ↔ Linux host)

## Symptom

| Signal | Detail |
|--------|--------|
| Kill | `PEER_SNAPSHOT_DIVERGE`@397 `figh` (+anim/cam) `bucket=REPLAY_DETERMINISM` |
| STATUS_FORK | `@396` Android `20` KneeBend vs Linux `22` JumpF |
| Inputs | Matched through 397: `btn=0x0002` stick `(0,0)` |
| PHYSICS_FORK | `@399` CLIFF TopN — consequence (Linux already airborne) |
| SoftLip / RESIM_STICK | Downstream of JumpF + stick GGPO@398 |

Ep1 GGPO@391 (button) resim’d clean. Not the seed.

## Gap

SoftLipPhase / `JA_VEL_WITNESS` target JumpAerial CLIFF `ja_vel`. This kill never entered JumpAerial — squat-exit timing forked one tick earlier with identical published sticks.

## Instrumentation

| Piece | Change |
|-------|--------|
| `SSB64_KNEEBEND_WITNESS=1` | `syNetplayMaybeLogKneeBendWitness` from KneeBend entry / ProcUpdate — `kb_anim`, `dobj_spd`, `length`, `will_exit`, shorthop, `btn_rel`, `status_tics` |
| `fhash_light` | Fold KneeBend `anim_frame` / `jump_force` / `input_source` / `is_shorthop` |
| scan-drift | `KNEEBEND_EXIT_FORK`; never demote KneeBend↔Jump STATUS_FORK; compound prefers squat-exit over stick SoftLip |

## Re-soak

1. Both peers: add `export SSB64_KNEEBEND_WITNESS=1` (also in [`scripts/netplay-cliff-softlip-soak.env.example`](../../scripts/netplay-cliff-softlip-soak.env.example)).
2. Grep `KNEEBEND_WITNESS` around tick 393–396: compare `kb_anim` / `dobj_spd` / `will_exit` bits.
3. Harden only the named gate (anim_speed, length compare, or input latch) — no coarse Jump snap until then.

## Related

- [`netplay_jumpaerial_ja_vel_witness_2026-07-19.md`](netplay_jumpaerial_ja_vel_witness_2026-07-19.md) (different class)
- [`netplay_seal_ledger_resim_stick_fork_2026-07-19.md`](netplay_seal_ledger_resim_stick_fork_2026-07-19.md)
