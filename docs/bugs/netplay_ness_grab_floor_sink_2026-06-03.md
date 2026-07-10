# Netplay Ness grab hold floor sink (2026-06-03)

**Date:** 2026-06-03  
**Scope:** `decomp/src/ft/ftcommon/ftcommoncapturepulled.c`, `ftcommoncapturewait.c`, `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netplay_statusvars_witness.c`  
**Status:** FIX SHIPPED — soak pending (Ness→DK hold vs offline)

## Symptoms

After PK Thunder netplay fixes, Ness standard grab hold could sink the grabber/victim into the floor vs offline. Soak showed persistent `SYNCTEST_SKIP reason=grab_coupling` and witness `catchwait throw_wait = shuffle_tics + 1` spam (mid-tick off-by-one, not random stomp).

## Root cause

1. **Stale grabber hand matrix** — `ftCommonCapturePulledRotateScale` read `joint_itemheavy_id` without invalidating/rebuilding grabber part transforms (documented upstream fix never landed in decomp).
2. **Stale grabber floor on refresh** — `syNetRbSnapshotRefreshGrabCouplingGeometry` invalidated transforms but did not refresh grabber map collision before victim Y projection.
3. **Grabber root Y quantize during coupling** — post-refresh quantize could grid-shift grabber root Y while victim Y was floor-projected from grabber `floor_line_id`.

## Fix

| Change | Purpose |
|--------|---------|
| PORT invalidate + `ftParamsUpdateFighterPartsTransformAll` in `ftCommonCapturePulledRotateScale` | Current hand joint matrix before XZ/rot coupling |
| PORT NULL guards on capture ProcMap paths | Rollback mid-grab safety |
| Refresh grabber `proc_map` (or default collision) before victim coupling in geometry refresh | Current `floor_line_id` for Y snap |
| Skip grabber root Y in `syNetRbSnapQuantizeFighterCoupledGeometry` | Avoid quantize pulling grabber into floor during hold |
| Witness catchwait: tolerate `throw_wait == shuffle_tics + 1` | Suppress mid-tick false positives |

Related: [`netplay_grab_geometry_stale_joint_2026-05-22.md`](netplay_grab_geometry_stale_joint_2026-05-22.md), [`../grab_coupling_geometry_handoff_2026-05-23.md`](../grab_coupling_geometry_handoff_2026-05-23.md).
