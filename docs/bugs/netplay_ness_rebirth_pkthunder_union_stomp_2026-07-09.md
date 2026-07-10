# Netplay: Ness rebirth apex stomped by PK Thunder pointer cleanup

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`), soak pending
**Date:** 2026-07-09

## Symptom

During netplay VS KO/rebirth, every fighter except Ness descended from the top of the screen
(`map_bound_top`) down to the rebirth platform. Ness appeared to rise from the stage center /
ground plane toward the platform instead.

Soak2 session (Captain Falcon vs Ness, Dream Land): Captain rebirth @ tick 1700 looked correct;
Ness rebirth @ tick 2689 was inverted on both Linux and Android peers.

## Evidence

`death_rebirth_sim` for Ness during `RebirthDown`:

```text
tick=2689 rebirth_pos_y=0x00000000 rebirth_halo_y=0x451DE000 gobj_ty=0x4601B000
tick=2690 rebirth_pos_y=0x00000000 rebirth_halo_y=0x451DE000 gobj_ty=0x425F4940  # ~56, center
```

Captain at the same lifecycle stage kept `rebirth_pos_y=0x4601B000` (8300.0 = `map_bound_top`).

Per-tick snapshot save showed an alternating pattern on Ness:

```text
capture_final tick=2689 rebirth_pos_y=0x4601B000   # first capture (pre-cull)
capture_final tick=2689 rebirth_pos_y=0x00000000   # tail recapture (post-cull)
```

`halo_offset.y` and `halo_lower_wait` survived; only `rebirth.pos.y` was zeroed.

## Root Cause

`FTStruct.status_vars` is a union. On LP64, `ftNessSpecialHiStatusVars.pkthunder_gobj` (8-byte
pointer at offset 12) aliases `ftCommonRebirthStatusVars.pos.x` (offset 12) and `.pos.y` (offset 16).

Vanilla rebirth descent uses `rebirth.pos.y` as the apex:

```c
Y = ((pos.y - halo_offset.y) / 8100) * SQUARE(halo_lower_wait) + halo_offset.y;
```

With `pos.y = 0` and `halo_offset.y = 2526`, the fighter starts near Y=0 and rises toward the
platform — the inverse of vanilla top-down rebirth.

`syNetRbSnapFillSlotFromLive` captures fighters, then runs `syNetRbSnapCullAllOrphanPKThunderLive`
before the tail `syNetRbSnapRecaptureLiveFightersIntoSlot`. For Ness fighters not in PK Thunder
status (including `RebirthDown..RebirthWait`), that helper wrote:

```c
fp->status_vars.ness.specialhi.pkthunder_gobj = NULL;
```

NULLing the 8-byte pointer zeroed `rebirth.pos.x` and `rebirth.pos.y` in live memory. The same
pattern existed in `syNetRbSnapRebindFighterCoupledGObjs` on rollback apply.

## Fix

Skip `pkthunder_gobj = NULL` while the fighter is in rebirth scope (`RebirthDown..RebirthWait`).
Orphan PK Thunder weapon culling still runs; only the union write through the wrong overlay accessor
is gated.

Sites:

- `syNetRbSnapCullAllOrphanPKThunderLive`
- `syNetRbSnapRebindFighterCoupledGObjs` (Ness coupled-weapon else branch)

## Verification

Re-soak Captain vs Ness (or any Ness KO). During rebirth, both `capture_final` passes and all
`death_rebirth_sim` lines should keep `rebirth_pos_y=0x4601B000` (or the stage's `map_bound_top`
bit pattern), with `gobj_ty` descending from the apex toward `rebirth_halo_y`.
