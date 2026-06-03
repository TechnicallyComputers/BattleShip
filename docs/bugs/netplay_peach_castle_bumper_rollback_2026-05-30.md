# Peach's Castle bumper rollback SIGSEGV (stale `bumper_gobj`)

**Date:** 2026-05-30  
**Scope:** `decomp/src/gr/grcommon/grcastle.c`, `port/net/sys/netrollbacksnapshot.c`  
**Status:** FIX SHIPPED (soak pending)

## Symptoms

Cross-ISA soak on **Peach's Castle** (automatch `stage=0` = `nGRKindCastle`), Link vs DK bomb spam, `SSB64_NETPLAY_ROLLBACK_LOAD_HASH_SOFT=1`:

- **SIGSEGV** in `grCastleBumperProcUpdate+0x27`, `fault_addr=0x38`.
- Crash follows FC recovery resim (`FRAME_COMMIT_INPUT_AGREE_REANCHOR validation=7980 → load 7940`, resim span 7940→7981).
- GFX stale-DL diag fires (render fallout, not root cause).
- `fault_addr=0x38` = `offsetof(DObj, translate.vec.f)` reached through a **NULL `DObj`** — `DObjGetStruct(gobj)->translate` where `gobj->obj == NULL`.

Item desync is the upstream trigger (DK item throw + multiple bombs: first cross-peer item split @7879, `frame_commit_item_diverge` @7980 with `figh`/`world`/`rng`/`eff` agreeing), but the **crash** is the Castle stage-repair gap turning a clean desync into a hard process exit.

## Root cause

`grCastleBumperProcUpdate` (the moving-platform process) dereferences both the platform `DObj` and `gGRCommonStruct.castle.bumper_gobj`'s `DObj` **unconditionally** — the only NULL test is `bumper_gobj != NULL`, not `bumper_gobj->obj != NULL`.

The bumper is a singleton stage-hazard item (`nITKindGBumper`) on the item link. Rollback resolved `castle.bumper_gobj` via `gcFindGObjByID(bumper_gobj_id)` in `syNetRbSnapApplyGround`. Two problems:

1. **gobj-id reuse:** ids are recycled after `gcEjectGObj`, so a captured bumper id can resolve to an unrelated transient item (a bomb) that the **later** item-apply phase then ejects — leaving `castle.bumper_gobj` dangling.
2. **Apply ordering:** in `ApplySlotToLive`, the stage repair runs *before* `syNetRbSnapApplyItems`, which can eject/respawn item gobjs. Nothing re-synced `castle.bumper_gobj` afterward.

Either path leaves a non-NULL `bumper_gobj` whose `obj` is NULL → `grCastleBumperProcUpdate` crashes on the next sim tick.

## Fix

| Change | Location |
|--------|----------|
| NULL-guard both platform + bumper `DObj` derefs (`#ifdef PORT`); skip the rail update for the tick instead of crashing | `decomp/src/gr/grcommon/grcastle.c` |
| `syNetRbSnapFindLiveCastleBumperGObj` — scan item link for the live `nITKindGBumper` (authoritative singleton) | `port/net/sys/netrollbacksnapshot.c` |
| `syNetRbSnapResolveCastleBumperGObj` — prefer live scan; fall back to captured id only when it validates as `nITKindGBumper` | same |
| `syNetRbSnapApplyGround` Castle case + `syNetRbSnapEnsureCastleBumperAfterParticleReset` use the validated resolver | same |
| Re-resolve `castle.bumper_gobj` **after** `syNetRbSnapApplyItems` in `ApplySlotToLive` | same |

## Soak pass criteria

Peach's Castle cross-ISA (Link vs DK) with `LOAD_HASH_SOFT=1`, `SYNCTEST=1`, sustained bomb spam + item throws:

- No SIGSEGV (`grCastleBumperProcUpdate`, `fault_addr=0x38`) through FC recovery resim.
- Bumper rail keeps tracking the platform after rollback loads (no frozen/teleported bumper).
- Session survives past the FC recovery at tick ~7940 without process exit.

## Not fixed here (upstream item desync)

The DK item-throw + multi-bomb item-hash split (`frame_commit_item_diverge` @7980, held/thrown/spawn-queue bomb lifecycle) and the `EPISODE_SEAL_ROWS missing_slots=0x1` recovery stall are separate issues — see related. This change stops the desync from crashing; it does not stop the desync.

## Related

- [`netplay_hyrule_twister_rollback_2026-05-29.md`](netplay_hyrule_twister_rollback_2026-05-29.md) — same crash family (particle/ground gobj stale after rollback, `fault_addr=0x38`)
- [`netplay_link_bomb_rollback_2026-05-29.md`](netplay_link_bomb_rollback_2026-05-29.md) — item cross-ISA work (upstream trigger)
- [`netplay_yoster_cloud_rollback_2026-05-29.md`](netplay_yoster_cloud_rollback_2026-05-29.md) — ground gobj rebind pattern
