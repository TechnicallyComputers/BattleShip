#ifndef SYS_NETPLAY_SIM_QUANTIZE_H
#define SYS_NETPLAY_SIM_QUANTIZE_H

#include <PR/ultratypes.h>
#include <ssb_types.h>
#include <sys/objdef.h>
#include <ft/fttypes.h>
#include <mp/mpdef.h>
#include <gm/gmdef.h>

/* TRUE when netplay F32 normalization is active for this sim step. */
extern sb32 syNetplaySimQuantizeActive(void);

/*
 * TRUE when rollback/netplay policy may mutate live forward-sim behavior (active VS session or resim).
 *
 * Dual boundary in decomp (see CLAUDE.md and docs/netplay_rollback_refactor_contracts.md):
 *
 *   Compile: #if defined(PORT) && defined(SSB64_NETMENU)  — stripped from SSB64_NETMENU=OFF builds.
 *   Runtime: syNetplayRollbackSemanticsActive()            — gates policy inside netmenu binary.
 *
 *   #if defined(PORT) && defined(SSB64_NETMENU)
 *   // SSB64_NETMENU: stripped from offline builds. Runtime: active VS/resim only.
 *   // Netplay rollback only: <one-line why>.
 *   if (syNetplayRollbackSemanticsActive() != FALSE) { ...; return; }
 *   #endif
 *   // Vanilla forward sim
 *
 * Port safety (LP64, null guards) stays #ifdef PORT without SSB64_NETMENU.
 */
extern sb32 syNetplayRollbackSemanticsActive(void);

/*
 * TRUE when live forward-sim rollback policy may run (reconcile, grab refresh, catch-up).
 * Requires syNetplayRollbackSemanticsActive() and excludes offline battle modes (Training, etc.).
 */
extern sb32 syNetplayRollbackLiveForwardSimEligible(void);

/* Round to a shared 1/65536 grid (double intermediate) on all peers. */
extern f32 syNetplayQuantizeF32(f32 value);
/*
 * Same grid as syNetplayQuantizeF32 but always active for rollback map-hash save/verify (ignores
 * SSB64_NETPLAY_SIM_F32_QUANTIZE). Snapshot blobs stay raw; hash boundaries use this pass only.
 */
extern f32 syNetplayQuantizeF32ForRollbackHash(f32 value);
/* Like syNetplayQuantizeF32 but preserves AOBJ_ANIM_NULL (F32_MIN). */
extern f32 syNetplayQuantizeAnimScalar(f32 value);
extern void syNetplayQuantizeVec3f(Vec3f *vec);
/* Write grid-rounded copy; does not modify *src. */
extern void syNetplayQuantizeVec3fInto(Vec3f *dst, const Vec3f *src);

extern void syNetplayQuantizeDObjAnimScalars(DObj *dobj);
extern void syNetplayQuantizeDObjTranslate(DObj *dobj);
extern void syNetplayQuantizeDObjRotate(DObj *dobj);
extern void syNetplayQuantizeDObjScale(DObj *dobj);
/* Translate + rotate + scale after anim-joint evaluation. */
extern void syNetplayQuantizeDObjAnimPose(DObj *dobj);
/*
 * Canonicalize the DObj's AObj interpolation node chain (length/length_invert/value_base/
 * value_target/rate_base/rate_target) on the shared grid — the exact field set the rollback
 * snapshot quantizes in syNetRbSnapCaptureAObjNode/syNetRbSnapApplyAObjNode. Without this the
 * live forward-sim AObj state stays unquantized while a snapshot-restored replay does not, so a
 * resim restarts joints from a slightly different interpolation track (visible joint drift), and
 * the rollback anim hash forks cross-ISA. Call after gcPlayDObjAnimJoint advances the chain.
 */
extern void syNetplayQuantizeDObjAObjChain(DObj *dobj);

extern sb32 syNetplayFighterInIntroSimScope(const struct FTStruct *fp);

/* Sector Z intro / Wait: canonicalize Great Fox + Arwing map DObj tree on the shared grid. */
extern sb32 syNetplaySectorArwingIntroMapScopeActive(void);
extern void syNetplayCanonicalizeSectorArwingIntroMapPose(void);

/* Quantize rebirth.pos/halo_offset and derive root Y from procMap formula on the shared grid. */
extern void syNetplayCanonicalizeRebirthFighterMapPose(GObj *fighter_gobj);
/* Restore rebirth union from snapshot blob (multi-rebirth load / finalize repair). */
extern void syNetplayRestoreRebirthStatusVars(struct FTStruct *fp, const union FTStatusVars *blob_status_vars);
/* If rebirth.pos.y is at/below halo platform, force apex to map_bound_top (inverted-descent guard). */
extern void syNetplayRepairRebirthApexIfInverted(struct FTStruct *fp);

extern void syNetplayQuantizeFighterPhysics(struct FTPhysics *physics);
extern void syNetplayQuantizeMPCollData(MPCollData *coll);
/* Shared-grid pass over fighter physics, MPColl, joints, and root pose (live + snapshot). */
extern void syNetplayCanonicalizeFighterSimState(GObj *fighter_gobj);
/* Apply the shared-grid fighter pass to every active fighter at the accepted sim boundary. */
extern void syNetplayCanonicalizeActiveFightersForNetplay(void);

/*
 * Shared-grid pass over a live item's folded sim state: ITPhysics (vel_ground/vel_air) and the root
 * DObj translate (the position syNetSyncFoldItemState hashes). Movable items (Peach's Castle GBumper)
 * otherwise accumulate cross-ISA f32 position drift that forces an unrecoverable deep resim.
 */
extern void syNetplayCanonicalizeItemSimState(GObj *item_gobj);
/* Apply the shared-grid item pass to every active item at the accepted sim boundary. */
extern void syNetplayCanonicalizeActiveItemsForNetplay(void);

/*
 * Forward-vs-resim per-joint AObj ground-truth probe (default off; SSB64_NETPLAY_AOBJ_LEG_TRACE=1,
 * windowed by SSB64_NETPLAY_AOBJ_LEG_TRACE_TICK_MIN/_TICK_MAX). Logs chain digest, raw EVENT32 stream
 * digest, anim cursor scalars and rotate output per active fighter joint, tagged phase=fwd|resim.
 * Independent of SIM_F32_QUANTIZE. Call once per completed sim tick.
 */
extern void syNetplayTraceActiveFighterAObj(u32 tick);

/* Fighter attack hitbox world positions (after gmCollisionGetFighterPartsWorldPosition). */
extern void syNetplayQuantizeFTAttackColl(struct FTAttackColl *attack_coll);
/* Persist attack hitbox world positions on the sim grid (physics / snapshot). */
extern void syNetplayCanonicalizeFighterAttackCollPositions(GObj *fighter_gobj);

extern sb32 syNetplayFighterInNessPKJibakuSimScope(const struct FTStruct *fp);

extern sb32 syNetplayFighterInNessPKThunderHoldSimScope(const struct FTStruct *fp);

extern void syNetplayQuantizeNessPKJibakuStatusVars(struct FTStruct *fp, union FTStatusVars *status_vars);

extern void syNetplayQuantizeNessPKThunderHoldStatusVars(struct FTStruct *fp, union FTStatusVars *status_vars);

extern void syNetplayQuantizeNessPKThunderLandingStatusVars(struct FTStruct *fp, union FTStatusVars *status_vars);

extern void syNetplayQuantizeNessPKThunderHoldPassiveVars(struct FTStruct *fp, struct FTNessPassiveVars *passive);

extern void syNetplayQuantizePikachuQuickAttackStatusVars(struct FTStruct *fp, union FTStatusVars *status_vars);

extern void syNetplayQuantizePikachuQuickAttackLandingStatusVars(struct FTStruct *fp, union FTStatusVars *status_vars);

extern void syNetplayCanonicalizePikachuQuickAttackSimState(struct GObj *fighter_gobj);

extern void syNetplayCanonicalizeNessPKJibakuSimState(struct GObj *fighter_gobj);

extern void syNetplayCanonicalizeNessPKJibakuLaunchState(struct GObj *fighter_gobj);

extern void syNetplayCanonicalizeNessPKThunderWeaponSimState(struct GObj *weapon_gobj);

extern void syNetplayCanonicalizeNessPKThunderHoldSimState(struct GObj *fighter_gobj);

extern sb32 syNetplayFighterInNessSpecialLwSimScope(const struct FTStruct *fp);

extern sb32 syNetplayLiveEffectIsNessPsychicMagnet(const struct GObj *effect_gobj, const struct EFStruct *ep);

extern void syNetplayCanonicalizeNessSpecialLwSimState(struct GObj *fighter_gobj);

extern void syNetplayQuantizeGMCameraState(struct GMCamera *camera, f32 *pause_eye_x, f32 *pause_eye_y);

extern void syNetplayCanonicalizeGMCameraSimState(void);

#endif /* SYS_NETPLAY_SIM_QUANTIZE_H */
