# Netplay: rebirth halo offset stomped by inactive overlay scrub

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`), soak pending
**Date:** 2026-07-03

## Symptom

During netplay VS KO/rebirth, the respawn platform could carry fighters all the way down to the
stage center / ground plane instead of stopping at the vanilla rebirth platform height. The match
remained deterministic: both peers showed the same wrong position and drift scans reported
`LOAD_HASH_DRIFT=0`.

In soak session `85081742` and the follow-up trace soak, `death_rebirth_sim` showed the platform
height was initially correct:

```text
tick=749 rebirth_halo_y=0x4537C000  # 2940.0, vanilla platform height
```

After the first save/load verify cycle inside the rebirth window, both peers flipped to:

```text
tick=750 rebirth_halo_y=0x00000000
```

From that point vanilla `ftCommonRebirthCommonProcMap` derived the descent endpoint from zero and
the fighter descended toward `Y=0` instead of the rebirth halo map-object height.

## Evidence

The stage-tagged `status_trail` probe around tick 750 isolated the corruption to the saved fighter
blob:

```text
death_rebirth_sim tick=749 ... rebirth_halo_y=0x4537C000
status_trail tag=A_apply_status_restore tick=750 phase=live ... rebirth_halo_y=0x4537C000
status_trail tag=apply_after tick=750 phase=blob ... rebirth_halo_y=0x00000000 rebirth_pos_y=0x46147000 halo_lower=54 halo_despawn=354 halo_num=1
status_trail tag=B_apply_fighter_end tick=750 phase=live ... rebirth_halo_y=0x00000000
```

Only `rebirth.halo_offset` was zeroed. `rebirth.pos.y`, `halo_lower_wait`, `halo_despawn_wait`, and
`halo_number` survived, matching a union stomp over the first 16 bytes of the rebirth overlay.

## Root Cause

`FTStruct.status_vars` is a union. The dead overlay aliases the beginning of the rebirth overlay:

```c
typedef struct ftCommonDeadStatusVars {
    s32 wait;  // aliases rebirth.halo_offset.x
    Vec3f pos; // aliases rebirth.halo_offset.y/z and rebirth.pos.x
} ftCommonDeadStatusVars;

typedef struct ftCommonRebirthStatusVars {
    Vec3f halo_offset;
    Vec3f pos;
    s32 halo_despawn_wait;
    s32 halo_number;
    s32 halo_lower_wait;
} ftCommonRebirthStatusVars;
```

`syNetRbSnapScrubInactiveStatusVarsInBlob` correctly skipped the rebirth scrub while the fighter was
in `RebirthDown..RebirthWait`, but inactive-overlay scrubs that share the same union storage still
ran. The first diagnosis caught the dead scrub:

```c
if ((status_id < nFTCommonStatusDeadDown) || (status_id > nFTCommonStatusDeadUpStar))
{
    memset(&status_vars->common.dead, 0, sizeof(status_vars->common.dead));
}
```

That memset zeroed the live rebirth `halo_offset` in the captured blob. A follow-up soak with
`status_trail tag=capture_final` showed the bug persisted because `attackair`, `captureyoshi`, and
`tarucann` were still scrubbed in rebirth scope. On LP64, `captureyoshi` and `tarucann` are large
enough to clear offsets 0..15 of the common union, covering `rebirth.halo_offset.x/y/z` and
`rebirth.pos.x`.

Verify/emergency restore then applied the corrupted blob back to live state. Because both peers ran
the same scrub, the result was deterministic but wrong compared to vanilla.

## Fix

Rebirth statuses now leave `syNetRbSnapScrubInactiveStatusVarsInBlob` before the inactive common
overlay scrubs run, matching the existing Kirby Stone and Fox Firefox early-return pattern:

```c
if ((status_id >= nFTCommonStatusRebirthDown) && (status_id <= nFTCommonStatusRebirthWait))
{
    return;
}
```

The capture path also logs a `status_trail tag=capture_final` line after status-vars copy/scrub so
future soaks can inspect the authoritative saved overlay rather than the pre-copy zeroed blob.

## Verification

- `build-netmenu` `ssb64`: passed.
- `build-offline` `ssb64`: passed.
- Next KO/rebirth soak should keep `rebirth_halo_y=0x4537C000` (or the stage's actual rebirth
  map-object height) in `capture_final`, `apply_after`, and live `death_rebirth_sim` through
  save/load verify cycles.
