# Netplay intro Appear — figatree subtree FTParts + anchor-probe hygiene (2026-06-10)

## Symptom

Forced resim soak (`INJECT_TICK=240`) crashed on first replay tick after
`EPISODE_FSM phase AwaitingBaseline -> Replay`:

```
SIGSEGV fault_addr=0x38
ftMainProcUpdateInterrupt+0x600
```

Phase 3d (3-phase presentation split) improved `appear_modelpart_diag` (many
joints gained non-null `parts=`) but crash persisted on both Linux and Android.

## Root cause

1. **Subtree gap:** `syNetRbSnapEnsureAllFighterJointParts` only materialized
   `FTParts` on `fp->joints[ji] != NULL`. `lbCommonAddFighterPartsFigatree`
   walks the full TopN child DObj tree and unconditionally dereferences
   `current_dobj->user_data.p` — tree nodes not indexed in `fp->joints[]` could
   still be visited with NULL `FTParts` → NULL+offset SIGSEGV on first forward-sim
   figatree bind / motion tick.

2. **Anchor-probe hygiene:** `syNetRollbackMaybeResimAnchorProbe` used bare
   `FinalizeLoad` (no terminal 3-phase prep), always advanced sim tick to
   `probe_tick` even when `LoadPostTick` failed after probe, and walkback break
   restored `load_tick` without forcing a successful reload before replay gate.

## Fix

| Area | Change |
|------|--------|
| `netrollbacksnapshot.c` | `syNetRbSnapEnsureFigatreeSubtreeParts` — walk TopN subtree like figatree bind; alloc minimal `FTParts` on unmapped DObjs; call from `EnsureAllFighterJointPartsForSlot` |
| `netrollbacksnapshot.c` | `syNetRbSnapshotRefreshPresentationForLoadedTick` — presentation-only 3-phase refresh + appear diag |
| `netrollback.c` | Anchor probe: `PrepareLoadedSlotForVerify`; hold sim at `load_tick` when post-probe reload fails; walkback break forces reload; terminal `LoadPostTick` + replay-gate presentation refresh |
| `lbcommon.c` | Netmenu NULL guard in `lbCommonAddFighterPartsFigatree` (belt-and-suspenders) |

## Verification

```bash
cmake --build build --target ssb64 -j 4
```

Re-soak with `SSB64_NETPLAY_SNAPSHOT_APPEAR_PRESENTATION_DIAG=1`,
`SSB64_NETPLAY_SNAPSHOT_MODELPART_DIAG=1`, `INJECT_TICK=240`.

Pass: no SIGSEGV on replay gate; appear diag at replay gate; `resim complete`.
