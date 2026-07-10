# Netplay: Castle platform animation phase drives bumper position drift

**Date:** 2026-07-03
**Scope:** `port/net/sys/netrollbacksnapshot.c`. `PORT && SSB64_NETMENU`, rollback snapshot apply/capture only.
**Status:** FIX IMPLEMENTED (soak pending).
**Class:** snapshot coverage gap for an animation-driven stage-hazard controller.

## Symptom

Android/Linux Peach's Castle soak (`session=1440487876`, Firefox + bumper, multiple resims) failed
frame-commit validation with matching inputs:

```text
FRAME_COMMIT_STATE_DIVERGE validation=600 diverged=figh,item inputs=MATCH
FRAME_COMMIT_ITEM_PERSISTENT_DIVERGE validation=840 first=720 count=2
FRAME_COMMIT_STATE_DIVERGE validation=840 diverged=item inputs=MATCH
```

The `item_fold_floats` diagnostic named the Castle bumper's folded X position as the only live item
field that differed:

- tick 600: Android `px_q=0xC4830800`, Linux `px_q=0xC4829800` (~3.5 units apart)
- tick 840: Android `px_q=0xC3D20000`, Linux `px_q=0xC3CF6000` (~5.25 units apart)

Scale, palette, `multi`, and the live flash fields matched. The tick-600 fighter divergence was
downstream: player 1 was in bumper knockback, so the bumper's different X position changed the fighter
trajectory even though RNG/world/effect/input digests agreed.

## Root cause

The Castle bumper item does not move itself. `grCastleBumperProcUpdate` derives the item's X position
from the hidden Castle ground controller DObj:

```c
bumper_dobj->translate.vec.f.x = ground_pos->x + gGRCommonStruct.castle.bumper_pos.x;
```

That controller is a ground GObj with `dobjs == NULL` for map/yakumono purposes, animated by
`gcAddAnimJointAll` / `gcPlayAnimAll`. The Castle ground payload only stored `bumper_gobj_id` and the
constant `bumper_pos` offset, so rollback never rewound the controller's DObj animation cursor or AObj
chain. After peer-specific rollback spans, each peer replayed from a different platform phase; the next
Castle bumper update re-derived different grid-snapped item positions.

This is the same coverage class as the DK Jungle barrel and Yamabuki gate: a stage presentation GObj
outside the generic yakumono snapshot whose animation phase is gameplay-relevant through a stage hazard.

## Fix

Add a slot-local `SYNetRbSnapCastlePlatformBlob` beside the existing barrel/gate/arwing partitions.
Capture finds the Castle ground controller by its `grCastleBumperProcUpdate` process, walks its DObj tree,
and stores translate/rotate plus full `SYNetRbSnapDObjAnimBlob` cursor/AObj state. Apply restores the
same tree after Castle stage repair and re-seats any anim-joint pose, then re-pins translate/rotate to
the captured grid-snapped values.

The 128-byte Castle ground payload remains unchanged; the new data lives only in local ring snapshots.

## Soak procedure

Re-run the Android/Linux Peach's Castle Firefox bumper soak with the same `SSB64_NETPLAY_ITEM_HASH_FIELD_DIFF=1`
diagnostic env. Expected result:

- no `FRAME_COMMIT_STATE_DIVERGE` at validations 600 or 840,
- no persistent item-only bumper `px_q` skew after the forced/non-deterministic resims,
- `item_fold_floats` for `kind=gbumper` agrees on `px_q` cross-peer whenever frame-commit validation runs.

## Related

- [`netplay_castle_bumper_persistent_item_only_reanchor`](netplay_castle_bumper_persistent_item_only_reanchor_2026-07-03.md)
- [`netplay_castle_bumper_anim_phase_apply`](netplay_castle_bumper_anim_phase_apply_2026-07-03.md)
- [`netplay_castle_bumper_hit_anim_length_free_run_fold`](netplay_castle_bumper_hit_anim_length_free_run_fold_2026-07-02.md)
