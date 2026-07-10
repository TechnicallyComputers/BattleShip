# Netplay — Ness PK Thunder hold fall ladder fork after resim → jibaku FC (2026-07-10)

**Date:** 2026-07-10  
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android soak2 session `833649982` seed `2652032666`  
**Match:** `FRAME_COMMIT_STATE_DIVERGE @560` `figh` only, inputs MATCH (status **236** air jibaku)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)  
**Follow-up to:** [`netplay_ness_pkthunder_jibaku_resim_hold_drift_2026-07-10.md`](netplay_ness_pkthunder_jibaku_resim_hold_drift_2026-07-10.md), [`netplay_ness_pkthunder_jibaku_resim_selfhit_collide_2026-07-10.md`](netplay_ness_pkthunder_jibaku_resim_selfhit_collide_2026-07-10.md)

## Symptom

| Probe | Tick | Detail |
|-------|------|--------|
| `SYNCTEST_FAIL` / `LOAD_HASH_DRIFT` | 509 | `wpn` only (synctest save/load round-trip during Hold) |
| `FORCE_MISMATCH` resim | 519→522 | Identical `rollback_load` @519 (`vel_y=-8.0`, `fhash=0x7829BC20`) |
| `rollback_post` | 520 | **Fall ladder already forked** |
| `jibaku_trigger` | 552 | Linux Y≈538; Android lower after faster fall |
| `FRAME_COMMIT` | 560 | `topn_ty` 544.57 vs 768.40 — launch arc amplified hold Y fork |

Post-resim hold fall (same load baseline):

| Peer | `rollback_post` Y | `vel_air.y` |
|------|-------------------|-------------|
| Android (client) | 964.145 | **-7.5** |
| Linux (host) | 966.145 | **-6.5** |

Resim first-tick ΔY: Android −7.0, Linux −6.0 — absolute retarget from ephemeral `ThrowEntryTick`, not vanilla −8.5 from snapshot −8.0.

## Root cause

`syNetplayNessCanonicalPKThunderHoldFallVelY` recomputed fall speed from **ephemeral** `ThrowEntryTick` / `HoldEntryGravityDelay` after `ForceRebuild` on load. Peers rebuilt different TE (≈504 vs ≈506) → different −0.5 ladder rungs while the snapshot vel (−8.0) already agreed.

Secondary: `ExpectedGravityDelayFromTracking` took `max(throw_expected, hold_expected)`, so a stale TE could resurrect `pkthunder_gravity_delay` and skip gravity ticks.

## Fix

| Layer | Change |
|-------|--------|
| **Canonical fall vel** | Snap live `vel_air.y` to the −0.5·N ladder (post-vanilla gravity). No TE absolute retarget. |
| **Gravity expected** | Hold status uses hold-local entry tracking only — never `max` with throw TE. |
| **Load harden** | Clear hold/throw entry ephemerals, rebuild, **then** canonicalize (order was reversed). |

## Verification

1. Cross-ISA soak with `FORCE_MISMATCH` during Ness air Hold → jibaku.
2. `rollback_post` after resim: matching `vel_air.y` / Ness `fhash` on both peers.
3. No `FRAME_COMMIT_STATE_DIVERGE` through jibaku window.

## Related

- Synctest `wpn` @509 is a separate PK Thunder trail save/load probe failure (unresolved in this soak); it is not the FC @560 fighter fork.
