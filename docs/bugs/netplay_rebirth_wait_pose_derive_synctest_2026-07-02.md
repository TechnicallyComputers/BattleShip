# Netplay: rebirth Stand/Wait pose re-derivation forks synctest

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)
**Date:** 2026-07-02
**Builds:** `build-netmenu` + `build-offline` — pending link check.

## Symptom

First soak after [`netplay_dead_rebirth_synctest_unskip`](netplay_dead_rebirth_synctest_unskip_2026-07-02.md)
removed the rebirth-window synctest skips. Session `1323928886` no longer crashes
(`sigsegv=0` — the [`netplay_rebirth_halo_zombie_gobjproc_segv`](netplay_rebirth_halo_zombie_gobjproc_segv_2026-07-02.md)
fix held) but both peers now report a **deterministic** `SYNCTEST_FAIL` / `LOAD_HASH_DRIFT`
at tick 2789:

```
[FAIL] tick 2789: diverged=eff,figh -> UNRESOLVED (no follow-up) [synctest probe]
LOAD_HASH_DRIFT tick=2789 ... eff=0xFED4935D/0x811C9DC5   (halo present -> empty)
fighter_field_diff tick=2789 player=1 field=gobj_translate_y live=0x00000000 blob=0x4537C000
fighter_field_diff tick=2789 player=1 field=fold_topn_ty / fold_j0_ty / top_joint_y / j0_ty  live=0 blob=0x4537C000
```

The fighter is player 1 (Fox) in **`RebirthWait` (status 9)** standing on the respawn
platform (`halo_despawn=284`, mid-window). The probe is **destructive**: from tick 2790
onward the live fighter carries the corruption (`gobj_ty=0`, `rebirth_halo_y=0`,
`halo_effect_present=0`).

## Root cause

The rebirth **union round-trips perfectly** — `rebirth_halo_offset_y`, `rebirth_pos_y`,
and `halo_lower_wait` are *not* logged as field diffs (the dumper only logs mismatches at
`netrollbacksnapshot.c:9259`), so `halo_offset.y = 0x4537C000` on both sides. Only the
**derived root/joint Y** collapses to 0.

Vanilla `ftCommonRebirthCommonProcMap` (the `translate.y = ((pos.y − halo_offset.y)/8100)·halo_lower_wait² + halo_offset.y`
derivation) runs **only during `RebirthDown`** (`decomp/src/ft/ftcommon/ftcommonrebirth.c`).
`RebirthStand`/`RebirthWait` never touch the root Y again — it stays static at the last Down
frame (which equals `halo_offset.y` once `halo_lower_wait` reached 0).

The port's `syNetplayCanonicalizeRebirthFighterMapPose` re-ran that derivation on **every**
rollback / synctest-verify canonicalize pass across the whole `RebirthDown..RebirthWait`
scope. The three snapshot apply paths are inconsistent about restoring the rebirth union
before canonicalizing:

- `syNetRbSnapApplyFighter` (`:7938`) — restore union → re-derive Y. ordered.
- `syNetRbSnapRestoreRebirthFightersAfterFinalize` (`:35125`) — restore union → gobj pose → TopN → re-derive Y. ordered.
- `syNetRbSnapReapplyFighterJointAnimFromBlobEx` (`:32684`, the **verify-prep reapply**) — apply gobj pose → `syNetplayCanonicalizeFighterSimState` (calls the rebirth re-derive at `netplay_sim_quantize.c:1083`) **without restoring the rebirth union first**.

So the verify-prep canonicalize could re-derive Y against a stale/zero `halo_offset`,
forking the captured `gobj_translate` (stored verbatim at `:5551`) to Y=0. The corruption
then persisted into live state, and the collapsed fighter pose cascaded a stale-halo prune
(`eff` count 1→0).

## Fix

Match vanilla: `syNetplayCanonicalizeRebirthFighterMapPose` now re-derives the root Y **only
when `status_id == nFTCommonStatusRebirthDown`** (the only state that runs ProcMap). For
`RebirthStand`/`RebirthWait` it trusts the restored blob pose (`gobj_translate` + joints)
verbatim. Union quantization (`pos`/`halo_offset`), apex repair, and the DObj/TopN grid
quantize are unchanged, so the derivation can no longer fork the captured pose regardless of
whether the union was restored before the canonicalize call.

`port/net/sys/netplay_sim_quantize.c` — `syNetplayCanonicalizeRebirthFighterMapPose`:

```c
if (fp->status_id == nFTCommonStatusRebirthDown)
{
    apex_y  = ftStatusVarsRebirth(fp)->pos.y;
    base_y  = ftStatusVarsRebirth(fp)->halo_offset.y;
    wait_sq = (f32)SQUARE(ftStatusVarsRebirth(fp)->halo_lower_wait);
    map_y   = (((apex_y - base_y) / 8100.0F) * wait_sq) + base_y;
    dobj->translate.vec.f.y = map_y;
}
```

This is a hash/pose round-trip fix; the vanilla RebirthDown descent path (the ProcMap PORT
hook + `ftCommonRebirthDownSetStatus`, both at `status_id == RebirthDown`) is unchanged.

## Verify

- `build-netmenu` `ssb64` target: pending.
- `build-offline` `ssb64` target: pending (offline never links `port/net/**`; decomp hook is
  `PORT && SSB64_NETMENU`).
- Soak pending: re-run the double-KO / rebirth soak; expect `SYNCTEST_FAIL=0` /
  `LOAD_HASH_DRIFT=0` through the rebirth window. If the `eff` (halo count) divergence
  persists independent of the fighter pose, follow up on the verify-prep halo
  prune/enforce-authoritative path.
