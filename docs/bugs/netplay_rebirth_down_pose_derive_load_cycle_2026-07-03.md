# Netplay: RebirthDown pose re-derivation forks load-cycle verify

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`), soak pending
**Date:** 2026-07-03
**Session:** `1298654388`

## Symptom

After rebirth halos were excluded as cosmetic, soak session `1298654388` still produced a deterministic
synctest-only failure:

```text
LOAD_HASH_DRIFT tick=4109 ... diverged=figh
SYNCTEST_FAIL tick=4109
```

Both peers failed at the same tick, and all non-fighter partitions matched (`world`, `item`, `wpn`,
`map`, `rng`, `cam`, `anim`, `eff`). This is a save/load idempotency failure, not cross-peer
simulation drift.

## Evidence

At tick 4109 player 0 is in `nFTCommonStatusRebirthDown` (`status=7`, `motion=1`) while player 1 is
dead. The fighter-field diff isolates the mismatch to the rebirthing fighter's derived root/TopN Y:

```text
fighter_field_diff tag=load_drift tick=4109 player=0 field=fold_topn_ty live=0x46078949 blob=0x460B8764
fighter_field_diff tag=load_drift tick=4109 player=0 field=gobj_translate_y live=0x46078949 blob=0x460B8764
fighter_field_diff tag=load_drift tick=4109 player=0 field=top_joint_y live=0x46078949 blob=0x460B8764
```

No `rebirth_pos_y`, `rebirth_halo_offset_y`, or `halo_lower_wait` field diff was emitted. The
rebirth union round-tripped; only the pose derived from it was stale. The live value is roughly two
descent ticks below the blob value, matching a `halo_lower_wait` that had advanced past the loaded
tick before a later load/verify canonicalize pass rewrote the pose.

## Root Cause

The prior fix in `netplay_rebirth_wait_pose_derive_synctest_2026-07-02.md` stopped
`syNetplayCanonicalizeRebirthFighterMapPose` from re-deriving root Y during `RebirthStand` and
`RebirthWait`, but it still re-derived during `RebirthDown`:

```c
map_y = (((pos.y - halo_offset.y) / 8100.0F) * SQUARE(halo_lower_wait)) + halo_offset.y;
```

That derivation is correct in vanilla forward sim because `ftCommonRebirthDownProcUpdate` decrements
`halo_lower_wait`, then `ftCommonRebirthCommonProcMap` derives Y from that same post-decrement value.
The netmenu hook runs after ProcMap, so re-deriving there is redundant but harmless during forward
sim.

Rollback load/verify is different. Several repair tails call the generic fighter canonicalizer after
the live rebirth union has already advanced beyond the slot being verified. That canonicalizer does
not restore the rebirth union first; it only sees current live `halo_lower_wait`. Re-deriving
`RebirthDown` Y in that context overwrites the snapshot pose with a later platform height. Some
subsequent paths restore the union from the blob again, so the diff reports matching
`halo_lower_wait` but a stale root/TopN Y.

## Fix

`syNetplayCanonicalizeRebirthFighterMapPose` no longer derives root Y at all. It still quantizes
`pos`, `halo_offset`, and the restored/forward-derived DObj/TopN translate. Vanilla
`ftCommonRebirthCommonProcMap` remains the sole owner of the `RebirthDown` descent formula during
forward simulation, while rollback load/verify trusts the pose captured in the fighter blob.

This makes the whole rebirth window follow one rule: the union values are state, and the root pose is
the authoritative captured/rendered pose for load idempotence. Canonicalization may grid-snap that
pose, but must not recalculate it from possibly out-of-phase live counters.

## Verification

- `build-netmenu` `ssb64`: pending.
- `build-offline` `ssb64`: pending.
- Next soak should keep `LOAD_HASH_DRIFT=0` through `RebirthDown`. If it fails again, check for any
  remaining bare calls to `syNetplayCanonicalizeFighterSimState` that mutate dead/rebirth pose after
  blob restore rather than merely quantizing it.
