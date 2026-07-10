# Netplay VS resim joint topology drift (2026-06-11)

## Symptom

Soak1 (Donkey vs Link, episode 2 @551 and episode 3 @646 walkback):

- `LOAD_HASH_DRIFT` on **figh + anim** after resim load @551 while item/world matched.
- `fighter_field_diff tag=load_drift player=1` (Link): `joint_presence` bit 2 live-only,
  `joint2_ty` / `joint2_rx` live pose with zeroed blob TRS.
- `RESIM_ANCHOR_PROBE_MISMATCH` on gameplay walkback @645→646: `joint0_tx` and
  `joint_anim_frame` drift on Link (Wait) after +1 probe sim.

Tick **519** bomb resim (episode 1) was **not** affected — `RESIM_ANCHOR_PROBE_POSTLOAD`
matched figh/anim; joint issue is VS gameplay resim, not the bomb load anchor.

## Root cause

1. **Intro-only joint topology repair** — `syNetRbSnapReconcileFighterJointPresenceFromSlot`
   + terminal fold hard-pin lived in `IntroLoadFidelityPreSanityRepair` only. VS resim
   `RefreshFigatreePresentationFromSlot` could materialize hidden-part roots absent at capture
   without ejecting them before load-hash verify.
2. **ResyncLiveFighters gap** — anchor-probe prep for non-intro loads skipped joint repair
   between `PrepareLoadedSlotForVerify` and +1 sim.
3. **Narrow gameplay anchor scope** — `syNetRbSnapStatusInGameplayResimAnimFragileScope` omitted
   `Wait` / walk / jump / `Fall`, so post-probe `ReconcileAnchorProbeGameplayFromProbeSlot`
   did not re-pin Link on steady ground states.

## Fix

- **`syNetRbSnapVsLoadJointFidelityRepairFromSlot`** — reconcile hidden-part topology, reapply
  joint AObj from blob, hard-pin figh-light fold contributors (mirrors intro repair tail).
- Call from:
  - `syNetRbSnapRefreshFigatreePresentationFromSlot` (non-intro),
  - `syNetRbSnapshotResyncLiveFightersFromSlotForSim` (non-intro / non-egg-lay / non-shield).
- Expand `syNetRbSnapStatusInGameplayResimAnimFragileScope` for Wait, walk, jump, Fall.
- `ReconcileAnchorProbeGameplayFromProbeSlot` — `ReconcileFighterJointPresenceFromBlob` before
  probe AObj re-pin.

## Verification

Re-soak Donkey vs Link through episode 2+ resims. Expect:

- No `joint_presence` / `joint2_*` lines on `load_drift` @551 after load.
- Fewer `RESIM_ANCHOR_PROBE_MISMATCH` steps during @646 walkback (or shallower walkback).
- Episode 1 @519 bomb resim unchanged (postload figh/anim still match).
