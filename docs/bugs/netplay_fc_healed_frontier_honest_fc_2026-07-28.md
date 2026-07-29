# Netplay: Healed exclusive-end Begin refuse hung inputs-agree FC

**Date:** 2026-07-28  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Soak:** Android guest ↔ Linux host, seed `1961215037`, session `313936707`  
**Follow-on to:** [healed frontier survive deepen](netplay_fc_healed_frontier_survive_deepen_2026-07-28.md)  
**Sim root (same soak):** [Hold gravity blob authority](netplay_hold_gravity_blob_authority_2026-07-28.md)

## Symptom

```text
FC@1276 inputs_agree=1 figh-only (Ness SpecialAirHiHold topn_ty)
FRAME_COMMIT_INPUT_AGREE_ONSET onset=1265
deferred recovery load_tick=1265 mismatch=1264
try_begin_fail stage=commit_covered_healed_exclusive_end
  mismatch=1264 healed/resolved_through=1269
recovery_started=0  (Android hang; Linux recovery_started=1)
PEER_BASELINE_COMPARE ignore_stale_behind_resolved load=1263 healed=1269
```

Detection worked. Resim was refused because durable heal had been raised past a still-forked fighter onset via exclusive-end soft-continue.

## Root cause

1. SoftLip-era `HealedThrough` + `commit_covered_healed_exclusive_end` Begin refuse blocked any correction whose onset was strictly behind the durable exclusive-end coverage.
2. Exclusive-end `ignore_stale` / `FIGH_STALE_AGGREGATE_OK` soft-continued past real figh digests, so Commit could raise heal through ticks that were not truly agreed.
3. Mid-Hold gravity tracking (separate doc) produced the physics fork; protocol then hung recovery instead of failing closed or resimming.

## Fix (architecture)

`PORT && SSB64_NETMENU`:

1. **Remove Begin refuse** on `mismatch < HealedThrough`. `ExclusiveEndCoveredThrough` remains for SoftLip Live / deepen soft-continue only — it must not starve FC Begin.
2. **`syNetRollbackFcHonestFighCompareActive()`** — while `FcStateRecoveryActive` or deferred inputs-agree FC is pending, suspend exclusive-end soft-continue (`ignore_stale`, Arm ignore, `FIGH_STALE_AGGREGATE`) so deepen sees real fighter digests.
3. Prefer honest `PEER_SNAPSHOT_DIVERGE` / deepen exhaust over `recovery_started=0` hang.

No new SoftLip/FC exclusive-end exceptions.

## Acceptance

- [ ] Re-soak Hold / SoftLip: inputs-agree FC with onset behind prior exclusive-end coverage must `resim begin` (`recovery_started` non-zero) or fail-closed — never `commit_covered_healed_exclusive_end` spam with `recovery_started=0`.
- [ ] SoftLip deepen after Commit still soft-continues exclusive-end baselines while Live (not mid FC recovery).
- [ ] Mid-FC-recovery peer baselines with figh mismatch are not `ignore_stale` / `FIGH_STALE_AGGREGATE_OK`.
