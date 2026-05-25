#include <sys/netrollbacksnapshot.h>

#include <sys/netsync.h>
#include <sys/objdef.h>
#include <sys/objanim.h>
#include <sys/objhelper.h>
#include <sys/objman.h>
#include <sys/utils.h>

#include <ft/fighter.h>
#include <ft/ftchar/ftkirby/ftkirby.h>
#include <ft/ftchar/ftkirby/ftkirbyfunctions.h>
#include <ft/ftchar/ftmario/ftmario.h>
#include <ft/ftchar/ftlink/ftlink.h>
#include <ft/ftchar/ftpikachu/ftpikachu.h>
#include <ft/ftchar/ftness/ftness.h>
#include <ft/ftchar/ftness/ftnessfunctions.h>
#include <ft/ftchar/ftsamus/ftsamus.h>
#include <ft/ftchar/ftsamus/ftsamusfunctions.h>
#include <ft/ftchar/ftfox/ftfox.h>
#include <ft/ftchar/ftyoshi/ftyoshi.h>
#include <ft/ftchar/ftyoshi/ftyoshifunctions.h>
#include <ft/ftdef.h>
#include <ft/ftmain.h>
#include <ft/ftparam.h>
#include <ft/ftcommon/ftcommonfunctions.h>
#include <ft/fttypes.h>
#include <wp/wpdef.h>
#include <wp/wpfox/wpfoxblaster.h>
#include <wp/wpkirby/wpkirbycutter.h>
#include <wp/wplink/wplinkboomerang.h>
#include <wp/wplink/wplinkspinattack.h>
#include <wp/wpmario/wpmariofireball.h>
#include <wp/wpness/wpnesspkfire.h>
#include <wp/wpness/wpnesspkthunder.h>
#include <wp/wppikachu/wppikachuthunder.h>
#include <wp/wppikachu/wppikachuthunderjolt.h>
#include <wp/wpsamus/wpsamusbomb.h>
#include <wp/wpsamus/wpsamuschargeshot.h>
#include <wp/wpyoshi/wpyoshieggthrow.h>
#include <wp/wpyoshi/wpyoshistar.h>
#include <wp/wpvars.h>
#include <gm/gmdef.h>
#include <gm/gmcamera.h>
#include <it/item.h>
#include <it/itdef.h>
#include <it/itmanager.h>
#include <wp/weapon.h>
#include <it/ittypes.h>
#include <mp/map.h>
#include <mp/mptypes.h>
#include <sc/scmanager.h>
#include <sc/sctypes.h>
#include <wp/wpprocess.h>
#include <wp/wpmanager.h>
#include <wp/wptypes.h>

#ifdef PORT
#include <sys/netrollback.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <it/itmain.h>
#include <it/itcommon/itfflower.h>
#include <it/itcommon/itlgun.h>
#include <it/itcommon/itstarrod.h>
#include <it/itground/itfushigibana.h>
#include <it/itground/ithitokage.h>
#include <it/itmonster/itdogas.h>
#include <it/itmonster/itiwark.h>
#include <it/itmonster/itkamex.h>
#include <it/itmonster/itlizardon.h>
#include <it/itmonster/itnyars.h>
#include <it/itmonster/itspear.h>
#include <it/itmonster/itstarmie.h>
#include <it/itvars.h>
#include <it/itfighter/itlinkbomb.h>

#include <ef/effect.h>
#include <ef/efmanager.h>
#include <ef/eftypes.h>
#include <gr/ground.h>
#include <gr/grdef.h>
#include <ef/efdef.h>
#include <mp/map.h>
#include <lb/lbparticle.h>

extern ITDesc dItLinkBombItemDesc;

extern void portFixupStructU16(void *base, unsigned int byte_offset, unsigned int num_words);

extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);
#endif

/* -------------------------------------------------------------------------- */
/* Internal blob layouts (typed; no raw FTStruct/ITStruct memcpy)              */
/* -------------------------------------------------------------------------- */

typedef struct SYNetRbSnapMPCollBlob
{
	Vec3f pos_prev;
	Vec3f pos_diff;
	Vec3f vel_speed;
	Vec3f vel_push;
	MPObjectColl map_coll;
	Vec2f cliffcatch_coll;
	u16 mask_prev;
	u16 mask_curr;
	u16 mask_unk;
	u16 mask_stat;
	u16 update_tic;
	s32 ewall_line_id;
	sb32 is_coll_end;
	Vec3f line_coll_dist;
	s32 floor_line_id;
	f32 floor_dist;
	u32 floor_flags;
	Vec3f floor_angle;
	s32 ceil_line_id;
	u32 ceil_flags;
	Vec3f ceil_angle;
	s32 lwall_line_id;
	u32 lwall_flags;
	Vec3f lwall_angle;
	s32 rwall_line_id;
	u32 rwall_flags;
	Vec3f rwall_angle;
	s32 cliff_id;
	s32 ignore_line_id;

} SYNetRbSnapMPCollBlob;

typedef struct SYNetRbSnapAttackCollBlob
{
	s32 attack_state;
	u32 group_id;
	s32 joint_id;
	s32 damage;
	s32 element;
	Vec3f offset;
	f32 size;
	s32 angle;
	s32 knockback_scale;
	s32 knockback_weight;
	s32 knockback_base;
	s32 shield_damage;
	u32 fgm_level;
	u32 fgm_kind;
	ub32 is_hit_air;
	ub32 is_hit_ground;
	ub32 can_rebound;
	ub32 is_scale_pos;
	u32 motion_attack_id;
	u16 motion_count;
	u16 stat_count;
	Vec3f pos_curr;
	Vec3f pos_prev;
	struct
	{
		GMHitFlags victim_flags;
		u32 victim_gobj_id;

	} attack_records[GMATTACKREC_NUM_MAX];
	FTAttackMatrix attack_matrix;

} SYNetRbSnapAttackCollBlob;

typedef struct SYNetRbSnapDamageCollBlob
{
	s32 hitstatus;
	s32 joint_id;
	s32 placement;
	sb32 is_grabbable;
	Vec3f offset;
	Vec3f size;

} SYNetRbSnapDamageCollBlob;

typedef struct SYNetRbSnapAObjNodeBlob
{
	u8 track;
	u8 kind;
	u8 pad[2];
	f32 length_invert;
	f32 length;
	f32 value_base;
	f32 value_target;
	f32 rate_base;
	f32 rate_target;
	/*
	 * `interpolate` is a code-segment function pointer set by `nGCAnimEvent32SetInterp` events. It is
	 * stable across rollback within a single process, so we serialize it as `uintptr_t` and restore by
	 * literal write. It is NOT folded into the cross-peer anim hash (see
	 * `syNetSyncFoldFighterAnimJointContribution`), so absolute-address differences between peers don't
	 * matter — only intra-process determinism does.
	 */
	uintptr_t interpolate_ptr;

} SYNetRbSnapAObjNodeBlob;

typedef struct SYNetRbSnapDObjAnimBlob
{
	f32 anim_wait;
	f32 anim_speed;
	f32 anim_frame;
	u8 aobj_count;
	u8 aobj_chain_total;
	u8 is_anim_root;
	u8 dobj_flags;
	/*
	 * Cache the active figatree event32 cursor at capture time. Restored before the AObj chain rebuild so
	 * any subsequent `gcParseDObjAnimJoint` advances from the exact stream position. Stored as a literal
	 * runtime pointer (same intra-process stability rationale as `interpolate_ptr`).
	 */
	uintptr_t event32_ptr;
	SYNetRbSnapAObjNodeBlob aobj[SYNETROLLBACK_SNAPSHOT_AOBJ_CHAIN_MAX];

} SYNetRbSnapDObjAnimBlob;

typedef struct SYNetRbSnapFighterBlob
{
	sb32 is_valid;
	s32 player;
	s32 fkind;
	u32 gobj_id;

	s32 status_id;
	s32 motion_id;
	s32 percent_damage;
	s32 damage_resist;
	s32 shield_health;
	s32 stock_count;
	s32 lr;
	sb32 ga;
	u32 hitlag_tics;
	u32 status_total_tics;
	u8 jumps_used;

	struct FTPhysics physics;
	SYNetRbSnapMPCollBlob coll;

	u32 motion_vars_flags[4];
	s32 attack1_status_id;
	s32 attack1_input_count;
	f32 attack1_followup_frames;
	s32 cliffcatch_wait;
	s32 tics_since_last_z;
	s32 acid_wait;
	s32 twister_wait;
	s32 tarucann_wait;
	s32 damagefloor_wait;
	s32 playertag_wait;

	ub32 is_attack_active;
	ub32 is_hitstatus_nodamage;
	ub32 is_fastfall;
	ub32 is_hitstun;
	ub32 is_shield;
	ub32 is_cliff_hold;
	ub32 is_catchstatus;
	ub32 is_catch_or_capture;
	u8 is_effect_attach;
	u8 fighter_snap_pad[3];
	u32 camera_mode;

	FTAnimDesc anim_desc;
	Vec3f anim_vel;
	Vec2f magnify_pos;

	s32 motion_attack_id;
	GMStatFlags stat_flags;
	u16 stat_count;
	s32 invincible_tics;
	s32 intangible_tics;
	s32 special_hitstatus;
	s32 star_invincible_tics;
	s32 star_hitstatus;
	s32 hitstatus;

	SYNetRbSnapAttackCollBlob attack_colls[4];
	SYNetRbSnapDamageCollBlob damage_colls[11];

	f32 hitlag_mul;
	s32 attack_damage;
	f32 attack_knockback;
	s32 shield_damage;
	s32 damage_queue;
	s32 damage_angle;
	s32 damage_element;
	s32 damage_lr;
	s32 damage_player;
	s32 damage_kind;

	u32 throw_gobj_id;
	u32 catch_gobj_id;
	u32 capture_gobj_id;
	u32 item_gobj_id;
	u32 search_gobj_id;
	uintptr_t throw_desc_ptr;

	FTMotionScript motion_scripts[2][2];
	Vec3f joint_translate[FTPARTS_JOINT_NUM_MAX];
	SYNetRbSnapDObjAnimBlob joint_anim[FTPARTS_JOINT_NUM_MAX];
	u8 joint_dobj_flags[FTPARTS_JOINT_NUM_MAX];
	u8 joint_event32_pad[3];
	uintptr_t joint_anim_joint_event32[FTPARTS_JOINT_NUM_MAX];

	FTModelPartStatus modelpart_status[FTPARTS_JOINT_NUM_MAX - nFTPartsJointCommonStart];
	FTTexturePartStatus texturepart_status[2];

	u8 status_vars[sizeof(union FTStatusVars)];
	u8 passive_vars[sizeof(union FTPassiveVars)];

	f32 gobj_anim_frame;

	/* Fighter-owned weapon GObjs (resolved after weapon apply; never trust memcpy'd pointers). */
	u32 coupled_egg_weapon_gobj_id;
	u32 coupled_boomerang_weapon_gobj_id;
	u32 coupled_spin_attack_weapon_gobj_id;
	u32 coupled_charge_weapon_gobj_id;
	u32 coupled_pkthunder_weapon_gobj_id;
	u32 coupled_thunder_weapon_gobj_id;

	/* Fighter-attached effect GObjs (rebound after effect reconcile; scrubbed from status_vars blob). */
	u32 guard_effect_gobj_id;
	u32 captureyoshi_effect_gobj_id;
	u32 fox_speciallw_effect_gobj_id;

} SYNetRbSnapFighterBlob;

typedef struct SYNetRbSnapYakuBlob
{
	Vec3f translate;
	Vec3f speed;
	s32 user_data_s;
	SYNetRbSnapDObjAnimBlob anim;
	u8 flags;
	u8 pad[3];
	uintptr_t anim_joint_event32;

} SYNetRbSnapYakuBlob;

typedef struct SYNetRbSnapWorldBlob
{
	SCBattleState battle;
	s32 rng_seed;
	u32 item_spawn_wait;
	u16 item_weights_sum;
	u8 item_weights_valids_num;
	u8 item_mapobjs_num;
	u8 item_mapobjs[SYNETRB_SNAPSHOT_MAX_MAPOBJS];
	u16 item_random_weights_sum;
	u8 item_random_weights_valids_num;
	u8 item_random_weight_kinds[nITKindEnumCount];
	u16 item_random_weight_blocks[nITKindEnumCount];

} SYNetRbSnapWorldBlob;

typedef struct SYNetRbSnapItemBlob
{
	sb32 is_valid;
	u32 gobj_id;
	s32 kind;
	s32 type;
	u8 team;
	u8 player;
	s32 player_num;
	s32 percent_damage;
	u32 hitlag_tics;
	s32 lr;
	struct ITPhysics physics;
	SYNetRbSnapMPCollBlob coll;
	sb32 ga;
	ITAttackColl attack_coll;
	ITDamageColl damage_coll;
	s32 lifetime;
	u32 owner_gobj_id;
	u32 reflect_gobj_id;
	u32 damage_gobj_id;
	u32 arrow_gobj_id;
	u16 multi;
	u32 event_id;
	f32 spin_step;
	Vec3f translate;
	u8 item_flags;
	u8 item_flags_pad[3];
	u8 item_vars[sizeof(union ITStatusVars)];
	u32 attack_record_victim_gobj_id[GMATTACKREC_NUM_MAX];

} SYNetRbSnapItemBlob;

typedef struct SYNetRbSnapWeaponBlob
{
	sb32 is_valid;
	u32 gobj_id;
	u32 instance_id;
	s32 kind;
	u8 team;
	u8 player;
	s32 player_num;
	s32 lr;
	struct WPPhysics physics;
	SYNetRbSnapMPCollBlob coll;
	sb32 ga;
	WPAttackColl attack_coll;
	s32 lifetime;
	u32 owner_gobj_id;
	u32 reflect_gobj_id;
	u32 absorb_gobj_id;
	u32 group_id;
	Vec3f translate;
	Vec3f rotate;
	Vec3f scale;
	SYNetRbSnapDObjAnimBlob anim;
	u8 weapon_vars[sizeof(union wpStatusVars)];
	u32 attack_record_victim_gobj_id[GMATTACKREC_NUM_MAX];
	u32 spawn_parent_gobj_id;
	u32 var_parent_gobj_id;
	u32 var_head_gobj_id;
	u32 var_trail_gobj_id[WPPKTHUNDER_PARTS_COUNT];
	u8 spawn_profile;
	u8 pad[3];

} SYNetRbSnapWeaponBlob;

#define SYNETRB_EFFECT_SNAP_NO_STRUCT (1U << 0)

#define SYNETRB_EFFECT_RESPAWN_NONE          0U
#define SYNETRB_EFFECT_RESPAWN_QUAKE           1U
#define SYNETRB_EFFECT_RESPAWN_SHIELD          2U
#define SYNETRB_EFFECT_RESPAWN_YOSHI_SHIELD    3U
#define SYNETRB_EFFECT_RESPAWN_FOX_REFLECTOR   4U
#define SYNETRB_EFFECT_RESPAWN_NESS_PK_WAVE    5U

#define SYNETRB_SNAPSHOT_GROUND_PAYLOAD_MAX 128U

typedef struct SYNetRbSnapGroundHyrule
{
	u32 twister_gobj_id;
	f32 twister_leftedge_x;
	f32 twister_rightedge_x;
	f32 twister_vel;
	u16 twister_wait;
	u16 twister_speed_wait;
	u16 twister_turn_wait;
	u16 twister_line_id;
	u8 twister_status;
	u8 twister_pos_count;

} SYNetRbSnapGroundHyrule;

typedef struct SYNetRbSnapGroundJungle
{
	u32 tarucann_gobj_id;
	u8 tarucann_status;
	u8 pad;
	u16 tarucann_wait;
	f32 tarucann_rotate_step;

} SYNetRbSnapGroundJungle;

typedef struct SYNetRbSnapGroundZebes
{
	f32 acid_level_curr;
	f32 acid_level_step;
	u16 acid_level_wait;
	u8 acid_status;
	u8 acid_attr_id;
	u8 rumble_wait;
	u8 pad;

} SYNetRbSnapGroundZebes;

typedef struct SYNetRbSnapGroundYamabuki
{
	u32 monster_gobj_id;
	u32 gate_gobj_id;
	Vec3f gate_pos;
	u8 gate_status;
	ub8 gate_noentry;
	u8 pad;
	u16 monster_wait;
	u16 gate_wait;
	u8 monster_id_prev;

} SYNetRbSnapGroundYamabuki;

typedef struct SYNetRbSnapGroundInishieScale
{
	f32 platform_base_y;
	f32 string_length;
	Vec3f platform_translate;
	Vec3f string_translate;

} SYNetRbSnapGroundInishieScale;

typedef struct SYNetRbSnapGroundInishie
{
	f32 splat_alt;
	f32 splat_accelerate;
	u16 splat_wait;
	u8 splat_status;
	u8 pblock_status;
	u32 pblock_gobj_id;
	u16 pblock_appear_wait;
	u8 pblock_pos_count;
	u8 players_tt[4];
	ub8 players_ga[4];
	u32 pakkun_gobj_id[2];
	SYNetRbSnapGroundInishieScale scale[2];

} SYNetRbSnapGroundInishie;

typedef struct SYNetRbSnapGroundYosterCloud
{
	u32 gobj_id;
	f32 altitude;
	f32 pressure;
	u8 status;
	s8 anim_id;
	ub8 is_cloud_line_active;
	s8 pressure_timer;
	u8 evaporate_wait;
	u8 pad;

} SYNetRbSnapGroundYosterCloud;

typedef struct SYNetRbSnapGroundYoster
{
	SYNetRbSnapGroundYosterCloud clouds[3];

} SYNetRbSnapGroundYoster;

typedef struct SYNetRbSnapGroundSector
{
	u32 map_gobj_id;
	f32 arwing_target_x;
	u16 arwing_appear_timer;
	u16 arwing_state_timer;
	u8 arwing_status;
	s8 arwing_flight_pattern;
	u8 arwing_type_cycle;
	u8 arwing_laser_ammo;
	u16 arwing_laser_timer;
	u8 arwing_laser_count;
	u8 arwing_pilot_curr;
	u8 arwing_pilot_prev;
	ub8 is_arwing_z_near;
	ub8 is_arwing_z_collision;
	ub8 is_arwing_line_active;
	ub8 is_arwing_line_collision;

} SYNetRbSnapGroundSector;

typedef struct SYNetRbSnapGroundPupupu
{
	u16 whispy_wind_wait;
	u16 whispy_wind_duration;
	s16 whispy_blink_wait;
	u8 whispy_status;
	s8 whispy_eyes_status;
	s8 whispy_mouth_status;

} SYNetRbSnapGroundPupupu;

typedef struct SYNetRbSnapGroundCastle
{
	u32 bumper_gobj_id;
	Vec3f bumper_pos;

} SYNetRbSnapGroundCastle;

typedef struct SYNetRbSnapGroundBlob
{
	u8 gkind;
	u8 pad;
	u16 payload_len;
	u8 payload[SYNETRB_SNAPSHOT_GROUND_PAYLOAD_MAX];

} SYNetRbSnapGroundBlob;

typedef struct SYNetRbSnapEffectBlob
{
	sb32 is_valid;
	u32 gobj_id;
	u16 bank_id;
	u8 link_id;
	u8 snap_flags;
	u8 respawn_kind;
	u8 quake_magnitude;
	u32 fighter_gobj_id;
	f32 anim_frame;
	u32 proc_update_fingerprint;
	/* Sanitized copy of `EFStruct::effect_vars` (pointer slots cleared on capture). */
	u8 effect_vars[sizeof(((EFStruct){0}).effect_vars)];

} SYNetRbSnapEffectBlob;

#define SYNETRB_WEAPON_SPAWN_DEFAULT           0U
#define SYNETRB_WEAPON_SPAWN_PK_REFLECT_HEAD   1U
#define SYNETRB_WEAPON_SPAWN_PK_REFLECT_TRAIL  2U

typedef struct SYNetRbSnapCameraBlob
{
	GMCamera camera;
	u32 camera_gobj_id;
	u32 pzoom_fighter_gobj_id;
	u32 pfollow_fighter_gobj_id;
	s8 pzoom_fighter_player;
	s8 pfollow_fighter_player;
	u8 camera_player_pad[2];
	f32 pause_eye_x;
	f32 pause_eye_y;

} SYNetRbSnapCameraBlob;

typedef struct SYNetRbSnapshotSlot
{
	u32 tick;
	sb32 is_valid;
	sb32 is_load_safe;
	u32 hash_fighter;
	u32 hash_world;
	u32 hash_item;
	u32 hash_weapon;
	u32 hash_map;
	u32 hash_rng;
	u32 hash_camera;
	u32 hash_animation;
	u32 hash_effect;
	SYNetRbSnapWorldBlob world;
	u16 mp_collision_tic;
	s32 mp_yakumono_count;
	sb32 mp_yaku_captured;
	SYNetRbSnapYakuBlob mp_yaku[SYNETRB_SNAPSHOT_MAX_YAKU];
	SYNetRbSnapFighterBlob fighters[GMCOMMON_PLAYERS_MAX];
	s32 item_count;
	SYNetRbSnapItemBlob items[SYNETRB_SNAPSHOT_MAX_ITEMS];
	s32 weapon_count;
	SYNetRbSnapWeaponBlob weapons[SYNETRB_SNAPSHOT_MAX_WEAPONS];
	s32 effect_count;
	SYNetRbSnapEffectBlob effects[SYNETRB_SNAPSHOT_MAX_EFFECTS];
	sb32 mp_bounds_captured;
	MPAllBounds mp_bounds;
	sb32 ground_captured;
	SYNetRbSnapGroundBlob ground;
	SYNetRbSnapCameraBlob camera;

} SYNetRbSnapshotSlot;

static SYNetRbSnapshotSlot sSYNetRbSnapshotRing[SYNETRB_SNAPSHOT_RING_MAX];
static u32 sSYNetRbSnapshotRingLen = SYNETRB_SNAPSHOT_RING_DEFAULT;

#ifdef PORT
static SYNetRbSnapshotSlot sSYNetRbEmergencySlot;
static sb32 sSYNetRbEmergencyValid;
static s32 sSYNetRbSnapshotGuardLogBudget = 16;
static sb32 sSYNetRbSnapWeaponApplyMatched[SYNETRB_SNAPSHOT_MAX_WEAPONS];
static u32 sSYNetRbSnapWeaponApplyTick;
static sb32 sSYNetRbSnapWeaponApplyPendingEject;
static s32 sSYNetRbSnapWeaponApplyMatchedCount;
static s32 sSYNetRbSnapWeaponApplyRespawnedCount;

static void syNetRbSnapLogSkippedGObj(const char *phase, const char *kind, const GObj *gobj, u32 tick)
{
	if (sSYNetRbSnapshotGuardLogBudget <= 0)
	{
		return;
	}
	port_log("SSB64 NetRbSnapshot: guard skip phase=%s kind=%s tick=%u gobj=%p id=%u\n",
	         phase,
	         kind,
	         (unsigned int)tick,
	         (void*)gobj,
	         (gobj != NULL) ? (unsigned int)gobj->id : 0U);
	sSYNetRbSnapshotGuardLogBudget--;
}

static GObj *syNetRbSnapResolveGobj(u32 id)
{
	if (id == 0U)
	{
		return NULL;
	}
	return gcFindGObjByID(id);
}

/* GObj may exist in the ID table while its typed payload is torn down (netem / rollback). */
static GObj *syNetRbSnapResolveLiveGobj(u32 id)
{
	GObj *gobj;

	if (id == 0U)
	{
		return NULL;
	}
	gobj = gcFindGObjByID(id);
	if (gobj == NULL)
	{
		return NULL;
	}
	if ((ftGetStruct(gobj) == NULL) && (itGetStruct(gobj) == NULL) && (wpGetStruct(gobj) == NULL))
	{
		return NULL;
	}
	return gobj;
}

/* All fighters share nGCCommonKindFighter (1000); resolve zoom/follow by sim slot, not gobj->id. */
static s8 syNetRbSnapFighterPlayerFromGobj(GObj *gobj)
{
	FTStruct *fp;

	if (gobj == NULL)
	{
		return -1;
	}
	fp = ftGetStruct(gobj);
	if (fp == NULL)
	{
		return -1;
	}
	if ((fp->player < 0) || (fp->player >= GMCOMMON_PLAYERS_MAX))
	{
		return -1;
	}
	return (s8)fp->player;
}

static GObj *syNetRbSnapResolveFighterGobjByPlayer(s8 player)
{
	if ((gSCManagerBattleState == NULL) || (player < 0) || (player >= GMCOMMON_PLAYERS_MAX))
	{
		return NULL;
	}
	return gSCManagerBattleState->players[player].fighter_gobj;
}

static sb32 syNetRbSnapWeaponDiagEnabled(void);
static GObj *syNetRbSnapResolveItemGobj(u32 id);

static sb32 syNetRbSnapWeaponKindUsesItemOwner(s32 kind)
{
	return ((kind >= nWPKindMonsterStart) && (kind <= nWPKindMonsterEnd)) ? TRUE : FALSE;
}

/* Fighter GObjs share kind id nGCCommonKindFighter (1000); owner must restore by sim slot. */
static GObj *syNetRbSnapResolveWeaponOwnerFromBlob(const SYNetRbSnapWeaponBlob *blob)
{
	GObj *owner_gobj;

	if (blob == NULL)
	{
		return NULL;
	}
	if (syNetRbSnapWeaponKindUsesItemOwner(blob->kind) != FALSE)
	{
		owner_gobj = syNetRbSnapResolveItemGobj(blob->owner_gobj_id);
		if (owner_gobj != NULL)
		{
			return owner_gobj;
		}
		return syNetRbSnapResolveLiveGobj(blob->owner_gobj_id);
	}
	if ((blob->player >= 0) && (blob->player < GMCOMMON_PLAYERS_MAX))
	{
		return syNetRbSnapResolveFighterGobjByPlayer((s8)blob->player);
	}
	return syNetRbSnapResolveLiveGobj(blob->owner_gobj_id);
}

static u32 syNetRbSnapWeaponInstanceIdFromGObj(GObj *weapon_gobj)
{
	WPStruct *wp;

	if (weapon_gobj == NULL)
	{
		return 0U;
	}
	wp = wpGetStruct(weapon_gobj);
	if (wp == NULL)
	{
		return 0U;
	}
	return wp->instance_id;
}

static GObj *syNetRbSnapResolveWeaponByInstanceId(u32 instance_id)
{
	GObj *weapon_gobj;

	if (instance_id == 0U)
	{
		return NULL;
	}
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp = wpGetStruct(weapon_gobj);

		if ((wp != NULL) && (wp->instance_id == instance_id))
		{
			return weapon_gobj;
		}
	}
	return NULL;
}

static s8 syNetRbSnapWeaponOwnerPlayerFromWP(const WPStruct *wp)
{
	FTStruct *owner_fp;

	if (wp == NULL)
	{
		return -1;
	}
	if ((wp->player >= 0) && (wp->player < GMCOMMON_PLAYERS_MAX))
	{
		return (s8)wp->player;
	}
	if (wp->owner_gobj != NULL)
	{
		owner_fp = ftGetStruct(wp->owner_gobj);
		if ((owner_fp != NULL) && (owner_fp->player >= 0) && (owner_fp->player < GMCOMMON_PLAYERS_MAX))
		{
			return (s8)owner_fp->player;
		}
	}
	return -1;
}

static sb32 syNetRbSnapWeaponBlobOwnerPlayerMatches(const SYNetRbSnapWeaponBlob *blob, const WPStruct *wp)
{
	s8 owner_player;

	if ((blob == NULL) || (wp == NULL) || (blob->kind != wp->kind))
	{
		return FALSE;
	}
	owner_player = syNetRbSnapWeaponOwnerPlayerFromWP(wp);
	if (owner_player < 0)
	{
		return (blob->player == wp->player) ? TRUE : FALSE;
	}
	return (blob->player == (u8)owner_player) ? TRUE : FALSE;
}

static s32 syNetRbSnapFindWeaponBlobByInstanceId(const SYNetRbSnapshotSlot *slot, sb32 *matched, u32 instance_id)
{
	s32 si;

	if ((slot == NULL) || (instance_id == 0U))
	{
		return -1;
	}
	for (si = 0; si < slot->weapon_count; si++)
	{
		if ((matched[si] == FALSE) && (slot->weapons[si].is_valid != FALSE) &&
		    (slot->weapons[si].instance_id == instance_id))
		{
			return si;
		}
	}
	return -1;
}

static s32 syNetRbSnapFindWeaponBlobByIdentity(const SYNetRbSnapshotSlot *slot, sb32 *matched, const WPStruct *wp,
                                               const Vec3f *pos,
                                               sb32 (*accept_blob)(const SYNetRbSnapWeaponBlob *))
{
	s32 si;
	s32 best = -1;
	f32 best_dist_sq = F32_MAX;

	if ((slot == NULL) || (wp == NULL) || (pos == NULL))
	{
		return -1;
	}
	for (si = 0; si < slot->weapon_count; si++)
	{
		const SYNetRbSnapWeaponBlob *blob;
		f32 dx;
		f32 dy;
		f32 dz;
		f32 dist_sq;

		if ((matched[si] != FALSE) || (slot->weapons[si].is_valid == FALSE))
		{
			continue;
		}
		blob = &slot->weapons[si];
		if (syNetRbSnapWeaponBlobOwnerPlayerMatches(blob, wp) == FALSE)
		{
			continue;
		}
		if ((accept_blob != NULL) && (accept_blob(blob) == FALSE))
		{
			continue;
		}
		dx = blob->translate.x - pos->x;
		dy = blob->translate.y - pos->y;
		dz = blob->translate.z - pos->z;
		dist_sq = (dx * dx) + (dy * dy) + (dz * dz);
		if (dist_sq < best_dist_sq)
		{
			best_dist_sq = dist_sq;
			best = si;
		}
	}
	return best;
}

static s32 syNetRbSnapFindLiveWeaponBlobIndex(const SYNetRbSnapshotSlot *slot, const WPStruct *wp)
{
	s32 si;

	if ((slot == NULL) || (wp == NULL))
	{
		return -1;
	}
	if (wp->instance_id != 0U)
	{
		for (si = 0; si < slot->weapon_count; si++)
		{
			if ((slot->weapons[si].is_valid != FALSE) && (slot->weapons[si].instance_id == wp->instance_id))
			{
				return si;
			}
		}
	}
	return -1;
}

#ifdef PORT
static sb32 syNetRbSnapLiveWeaponIsFighterCoupledReference(GObj *weapon_gobj)
{
	GObj *fighter_gobj;

	if (weapon_gobj == NULL)
	{
		return FALSE;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if (fp == NULL)
		{
			continue;
		}
		if (fp->status_vars.yoshi.specialhi.egg_gobj == weapon_gobj)
		{
			return TRUE;
		}
		if (fp->passive_vars.link.boomerang_gobj == weapon_gobj)
		{
			return TRUE;
		}
		if (fp->passive_vars.kirby.copylink_boomerang_gobj == weapon_gobj)
		{
			return TRUE;
		}
		if (fp->status_vars.link.specialhi.spin_attack_gobj == weapon_gobj)
		{
			return TRUE;
		}
		if (fp->status_vars.samus.specialn.charge_gobj == weapon_gobj)
		{
			return TRUE;
		}
		if (fp->status_vars.kirby.copysamus_specialn.charge_gobj == weapon_gobj)
		{
			return TRUE;
		}
		if (fp->status_vars.ness.specialhi.pkthunder_gobj == weapon_gobj)
		{
			return TRUE;
		}
		if (fp->status_vars.pikachu.speciallw.thunder_gobj == weapon_gobj)
		{
			return TRUE;
		}
	}
	return FALSE;
}
#endif /* PORT */

static void syNetRbSnapLogWeaponOwnerMismatch(const SYNetRbSnapWeaponBlob *blob, GObj *owner_gobj, u32 tick)
{
	FTStruct *owner_fp;

	if ((blob == NULL) || (blob->player >= GMCOMMON_PLAYERS_MAX))
	{
		return;
	}
	if (owner_gobj == NULL)
	{
		if (syNetRbSnapWeaponDiagEnabled() != FALSE)
		{
			port_log("SSB64 NetRbSnapshot: WEAPON_OWNER_MISS kind=%d blob_player=%d tick=%u\n", (int)blob->kind,
			         (int)blob->player, (unsigned int)tick);
		}
		return;
	}
	if (itGetStruct(owner_gobj) != NULL)
	{
		return;
	}
	owner_fp = ftGetStruct(owner_gobj);
	if ((owner_fp != NULL) && (owner_fp->player != blob->player))
	{
		port_log("SSB64 NetRbSnapshot: WEAPON_OWNER_MISMATCH kind=%d blob_player=%d resolved_player=%d tick=%u\n",
		         (int)blob->kind, (int)blob->player, (int)owner_fp->player, (unsigned int)tick);
	}
}

static void syNetRbSnapApplyWeaponOwnerFromBlob(WPStruct *wp, const SYNetRbSnapWeaponBlob *blob, u32 tick)
{
	GObj *owner_gobj;
	GObj *id_owner;

	if ((wp == NULL) || (blob == NULL))
	{
		return;
	}
	owner_gobj = syNetRbSnapResolveWeaponOwnerFromBlob(blob);
	wp->owner_gobj = owner_gobj;
	if (syNetRbSnapWeaponDiagEnabled() != FALSE)
	{
		id_owner = syNetRbSnapResolveLiveGobj(blob->owner_gobj_id);
		if ((owner_gobj != id_owner) && (blob->player < GMCOMMON_PLAYERS_MAX))
		{
			port_log(
			    "SSB64 NetRbSnapshot: WEAPON_OWNER_RESOLVE kind=%d blob_player=%d id_owner=%p player_owner=%p tick=%u\n",
			    (int)blob->kind, (int)blob->player, (void *)id_owner, (void *)owner_gobj, (unsigned int)tick);
		}
	}
	syNetRbSnapLogWeaponOwnerMismatch(blob, owner_gobj, tick);
}

static void syNetRbSnapRestoreYoshiChargeEggCoupling(WPStruct *wp, FTStruct *fp)
{
	if ((wp == NULL) || (fp == NULL) || (wp->kind != nWPKindEggThrow))
	{
		return;
	}
	if (wp->attack_coll.attack_state != nGMAttackStateOff)
	{
		return;
	}
	if (wp->weapon_vars.egg_throw.is_throw != FALSE)
	{
		return;
	}
	if (wp->weapon_vars.egg_throw.is_spin != FALSE)
	{
		return;
	}
	wp->weapon_vars.egg_throw.is_throw = FALSE;
	wp->weapon_vars.egg_throw.is_spin = FALSE;
	wp->physics.vel_air.x = wp->physics.vel_air.y = wp->physics.vel_air.z = 0.0F;
	wp->owner_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)fp->player);
	ftYoshiSpecialHiUpdateEggVectors(fp);
}

static GObj *syNetRbSnapResolveItemGobj(u32 id)
{
	GObj *gobj;

	if (id == 0U)
	{
		return NULL;
	}
	gobj = gcFindGObjByID(id);
	if ((gobj == NULL) || (itGetStruct(gobj) == NULL))
	{
		return NULL;
	}
	return gobj;
}

static GObj *syNetRbSnapResolveArrowGobjForItem(u32 id, GObj *item_gobj, ITStruct *ip)
{
	GObj *arrow_gobj;

	if ((id == 0U) || (item_gobj == NULL) || (ip == NULL))
	{
		return NULL;
	}
	arrow_gobj = gcFindGObjByID(id);
	if (arrow_gobj == NULL)
	{
		return NULL;
	}
	if (itGetStruct(arrow_gobj) != ip)
	{
		return NULL;
	}
	if (ip->arrow_gobj != arrow_gobj)
	{
		return NULL;
	}
	if (ip->item_gobj != item_gobj)
	{
		return NULL;
	}
	return arrow_gobj;
}

static u32 syNetRbSnapGobjId(const GObj *gobj)
{
	s32 link;

	if (gobj == NULL)
	{
		return 0U;
	}
	for (link = 0; link < GC_COMMON_MAX_LINKS; link++)
	{
		GObj *current_gobj;

		for (current_gobj = gGCCommonLinks[link]; current_gobj != NULL; current_gobj = current_gobj->link_next)
		{
			if (current_gobj == gobj)
			{
				return current_gobj->id;
			}
		}
	}
	return 0U;
}

#ifdef PORT
static void syNetRbSnapshotFinalizeLoadCouplingFromSlot(const SYNetRbSnapshotSlot *slot,
                                                        sb32 refresh_coupled_weapon_geometry);
static void syNetRbSnapshotFinalizeLoadFromSlot(const SYNetRbSnapshotSlot *slot, sb32 sync_presentation,
                                                sb32 refresh_coupled_weapon_geometry);
static sb32 syNetRbSnapLiveWeaponIsFighterCoupledReference(GObj *weapon_gobj);
static sb32 syNetRbSnapLiveWeaponIsFireballThrowPreserve(GObj *weapon_gobj);
static sb32 syNetRbSnapLiveWeaponIsThunderJoltThrowPreserve(GObj *weapon_gobj);
static sb32 syNetRbSnapLiveWeaponIsPKThunderPreserve(GObj *weapon_gobj);
static sb32 syNetRbSnapLiveWeaponIsPKFirePreserve(GObj *weapon_gobj);
static void syNetRbSnapPreEjectPKThunderWeapon(GObj *weapon_gobj, WPStruct *wp);
static void syNetRbSnapEjectUnmatchedWeaponsAfterCoupling(const SYNetRbSnapshotSlot *slot);
void syNetRbSnapCullYoshiChargeEggsForFighter(GObj *fighter_gobj, GObj *keep_egg_gobj);
void syNetRbSnapCullSamusChargeShotsForFighter(GObj *fighter_gobj, GObj *keep_charge_gobj);
GObj *syNetRbSnapReacquireFireballForFighter(GObj *fighter_gobj);
void syNetRbSnapCullOwnedFireballsNearPose(GObj *fighter_gobj, GObj *keep_fireball_gobj, const Vec3f *pos, f32 radius_sq);
void syNetRbSnapCullOwnedPKThunderForFighter(GObj *fighter_gobj, GObj *keep_head_gobj);
GObj *syNetRbSnapReacquirePKThunderHeadForFighter(GObj *fighter_gobj);

static sb32 syNetRbSnapWeaponDiagEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_SNAPSHOT_WEAPON_DIAG");
	s_env_cache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}

static sb32 syNetRbSnapCoupledDiagEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_SNAPSHOT_COUPLED_DIAG");
	s_env_cache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}

static sb32 syNetRbSnapWeaponBlobEggIsCharging(const SYNetRbSnapWeaponBlob *blob)
{
	const wpYoshiWeaponVarsEggThrow *egg_vars;

	if ((blob == NULL) || (blob->kind != nWPKindEggThrow))
	{
		return FALSE;
	}
	if (blob->attack_coll.attack_state != nGMAttackStateOff)
	{
		return FALSE;
	}
	egg_vars = (const wpYoshiWeaponVarsEggThrow *)blob->weapon_vars;
	return ((egg_vars->is_throw == FALSE) && (egg_vars->is_spin == FALSE)) ? TRUE : FALSE;
}

static sb32 syNetRbSnapWeaponEggIsCharging(const WPStruct *wp, GObj *owner_gobj)
{
	(void)owner_gobj;

	if ((wp == NULL) || (wp->kind != nWPKindEggThrow))
	{
		return FALSE;
	}
	if (wp->attack_coll.attack_state != nGMAttackStateOff)
	{
		return FALSE;
	}
	return ((wp->weapon_vars.egg_throw.is_throw == FALSE) && (wp->weapon_vars.egg_throw.is_spin == FALSE)) ? TRUE
	                                                                                                     : FALSE;
}

static sb32 syNetRbSnapWeaponOwnedByGObj(const WPStruct *wp, GObj *owner_gobj)
{
	if ((wp == NULL) || (owner_gobj == NULL))
	{
		return FALSE;
	}
	return (wp->owner_gobj == owner_gobj) ? TRUE : FALSE;
}

static sb32 syNetRbSnapWeaponOwnedByFighterGObj(const WPStruct *wp, GObj *owner_gobj)
{
	FTStruct *owner_fp;

	if ((wp == NULL) || (owner_gobj == NULL))
	{
		return FALSE;
	}
	if (wp->owner_gobj == owner_gobj)
	{
		return TRUE;
	}
	owner_fp = ftGetStruct(owner_gobj);
	if ((owner_fp != NULL) && (wp->player >= 0) && (wp->player < GMCOMMON_PLAYERS_MAX) &&
	    (owner_fp->player == wp->player))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapChargeShotOwnedByGObj(const WPStruct *wp, GObj *owner_gobj)
{
	if ((wp == NULL) || (owner_gobj == NULL) || (wp->kind != nWPKindChargeShot))
	{
		return FALSE;
	}
	if (wp->weapon_vars.charge_shot.owner_gobj == owner_gobj)
	{
		return TRUE;
	}
	return (wp->owner_gobj == owner_gobj) ? TRUE : FALSE;
}

static sb32 syNetRbSnapWeaponBlobChargeShotIsCharging(const SYNetRbSnapWeaponBlob *blob)
{
	const wpSamusWeaponVarsChargeShot *charge_vars;

	if ((blob == NULL) || (blob->kind != nWPKindChargeShot))
	{
		return FALSE;
	}
	charge_vars = (const wpSamusWeaponVarsChargeShot *)blob->weapon_vars;
	return (charge_vars->is_release == FALSE) ? TRUE : FALSE;
}

static sb32 syNetRbSnapWeaponChargeShotIsCharging(const WPStruct *wp, GObj *owner_gobj)
{
	(void)owner_gobj;

	if ((wp == NULL) || (wp->kind != nWPKindChargeShot))
	{
		return FALSE;
	}
	return (wp->weapon_vars.charge_shot.is_release == FALSE) ? TRUE : FALSE;
}

static u32 syNetRbSnapFindWeaponGobjIdInSlotForOwner(const SYNetRbSnapshotSlot *slot, s8 owner_player, s32 kind,
                                                     sb32 (*accept_blob)(const SYNetRbSnapWeaponBlob *))
{
	s32 si;

	if ((slot == NULL) || (owner_player < 0) || (owner_player >= GMCOMMON_PLAYERS_MAX))
	{
		return 0U;
	}
	for (si = 0; si < slot->weapon_count; si++)
	{
		const SYNetRbSnapWeaponBlob *blob = &slot->weapons[si];

		if ((blob->is_valid == FALSE) || (blob->kind != kind) || (blob->player != owner_player))
		{
			continue;
		}
		if ((accept_blob != NULL) && (accept_blob(blob) == FALSE))
		{
			continue;
		}
		return blob->instance_id;
	}
	return 0U;
}

static GObj *syNetRbSnapFindLiveWeaponForOwner(GObj *owner_gobj, s32 kind,
                                               sb32 (*accept_wp)(const WPStruct *, GObj *owner_gobj))
{
	GObj *weapon_gobj;

	if (owner_gobj == NULL)
	{
		return NULL;
	}
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp = wpGetStruct(weapon_gobj);

		if ((wp == NULL) || (wp->kind != kind) || (syNetRbSnapWeaponOwnedByFighterGObj(wp, owner_gobj) == FALSE))
		{
			continue;
		}
		if ((accept_wp != NULL) && (accept_wp(wp, owner_gobj) == FALSE))
		{
			continue;
		}
		wp->owner_gobj = owner_gobj;
		return weapon_gobj;
	}
	return NULL;
}

static GObj *syNetRbSnapResolveCoupledWeaponGobj(const SYNetRbSnapshotSlot *slot, u32 stored_id, GObj *fighter_gobj,
                                                 s32 kind, sb32 (*accept_blob)(const SYNetRbSnapWeaponBlob *),
                                                 sb32 (*accept_wp)(const WPStruct *, GObj *owner_gobj))
{
	GObj *weapon_gobj;
	FTStruct *fp;
	u32 slot_weapon_id;

	weapon_gobj = NULL;
	if (stored_id != 0U)
	{
		weapon_gobj = syNetRbSnapResolveWeaponByInstanceId(stored_id);
	}
	if (weapon_gobj == NULL)
	{
		weapon_gobj = syNetRbSnapFindLiveWeaponForOwner(fighter_gobj, kind, accept_wp);
	}
	if ((weapon_gobj == NULL) && (slot != NULL) && (fighter_gobj != NULL))
	{
		fp = ftGetStruct(fighter_gobj);
		if ((fp != NULL) && (fp->player >= 0) && (fp->player < GMCOMMON_PLAYERS_MAX))
		{
			slot_weapon_id =
			    syNetRbSnapFindWeaponGobjIdInSlotForOwner(slot, (s8)fp->player, kind, accept_blob);
			if (slot_weapon_id != 0U)
			{
				weapon_gobj = syNetRbSnapResolveWeaponByInstanceId(slot_weapon_id);
			}
		}
	}
	return weapon_gobj;
}

static void syNetRbSnapLogCoupledWeaponDiag(const SYNetRbSnapshotSlot *slot, s32 player, const char *kind_name,
                                            u32 blob_id, GObj *weapon_gobj)
{
	if (syNetRbSnapCoupledDiagEnabled() == FALSE)
	{
		return;
	}
	port_log("SSB64 NetRbSnapshot: coupled %s tick=%u player=%d blob_id=%u weapon_gobj=%p\n", kind_name,
	         (unsigned int)slot->tick, player, (unsigned int)blob_id, (void *)weapon_gobj);
}

static void syNetRbSnapLogCoupledWeaponMiss(const SYNetRbSnapshotSlot *slot, s32 player, const char *kind_name,
                                            u32 blob_id)
{
	if (syNetRbSnapCoupledDiagEnabled() == FALSE)
	{
		return;
	}
	port_log("SSB64 NetRbSnapshot: SNAPSHOT_COUPLED_GOBJ_MISS tick=%u player=%d kind=%s id=%u\n",
	         (unsigned int)slot->tick, player, kind_name, (unsigned int)blob_id);
}

static void syNetRbSnapBackfillFighterCoupledIdsFromWeapons(SYNetRbSnapshotSlot *slot)
{
	s32 pi;

	if (slot == NULL)
	{
		return;
	}
	for (pi = 0; pi < GMCOMMON_PLAYERS_MAX; pi++)
	{
		SYNetRbSnapFighterBlob *blob = &slot->fighters[pi];

		if (blob->is_valid == FALSE)
		{
			continue;
		}
		if (blob->fkind == nFTKindYoshi)
		{
			if (((blob->status_id == nFTYoshiStatusSpecialHi) || (blob->status_id == nFTYoshiStatusSpecialAirHi)) &&
			    (blob->coupled_egg_weapon_gobj_id == 0U))
			{
				blob->coupled_egg_weapon_gobj_id = syNetRbSnapFindWeaponGobjIdInSlotForOwner(
				    slot, (s8)pi, nWPKindEggThrow, syNetRbSnapWeaponBlobEggIsCharging);
			}
		}
		if ((blob->fkind == nFTKindLink) || (blob->fkind == nFTKindKirby))
		{
			if (blob->coupled_boomerang_weapon_gobj_id == 0U)
			{
				blob->coupled_boomerang_weapon_gobj_id =
				    syNetRbSnapFindWeaponGobjIdInSlotForOwner(slot, (s8)pi, nWPKindBoomerang, NULL);
			}
		}
		if (blob->fkind == nFTKindLink)
		{
			if (((blob->status_id == nFTLinkStatusSpecialHi) || (blob->status_id == nFTLinkStatusSpecialAirHi)) &&
			    (blob->coupled_spin_attack_weapon_gobj_id == 0U))
			{
				blob->coupled_spin_attack_weapon_gobj_id =
				    syNetRbSnapFindWeaponGobjIdInSlotForOwner(slot, (s8)pi, nWPKindSpinAttack, NULL);
			}
		}
		if (((blob->fkind == nFTKindSamus) &&
		     ((blob->status_id == nFTSamusStatusSpecialNStart) || (blob->status_id == nFTSamusStatusSpecialNLoop) ||
		      (blob->status_id == nFTSamusStatusSpecialAirNStart))) ||
		    ((blob->fkind == nFTKindKirby) &&
		     ((blob->status_id == nFTKirbyStatusCopySamusSpecialNStart) ||
		      (blob->status_id == nFTKirbyStatusCopySamusSpecialNLoop) ||
		      (blob->status_id == nFTKirbyStatusCopySamusSpecialAirNStart))))
		{
			if (blob->coupled_charge_weapon_gobj_id == 0U)
			{
				blob->coupled_charge_weapon_gobj_id = syNetRbSnapFindWeaponGobjIdInSlotForOwner(
				    slot, (s8)pi, nWPKindChargeShot, syNetRbSnapWeaponBlobChargeShotIsCharging);
			}
		}
		if (blob->fkind == nFTKindNess)
		{
			if (((blob->status_id == nFTNessStatusSpecialHiStart) || (blob->status_id == nFTNessStatusSpecialHiHold) ||
			     (blob->status_id == nFTNessStatusSpecialAirHiStart) ||
			     (blob->status_id == nFTNessStatusSpecialAirHiHold)) &&
			    (blob->coupled_pkthunder_weapon_gobj_id == 0U))
			{
				blob->coupled_pkthunder_weapon_gobj_id =
				    syNetRbSnapFindWeaponGobjIdInSlotForOwner(slot, (s8)pi, nWPKindPKThunderHead, NULL);
			}
		}
		if (blob->fkind == nFTKindPikachu)
		{
			if (((blob->status_id == nFTPikachuStatusSpecialLwStart) ||
			     (blob->status_id == nFTPikachuStatusSpecialLwLoop) ||
			     (blob->status_id == nFTPikachuStatusSpecialAirLwStart) ||
			     (blob->status_id == nFTPikachuStatusSpecialAirLwLoop)) &&
			    (blob->coupled_thunder_weapon_gobj_id == 0U))
			{
				blob->coupled_thunder_weapon_gobj_id =
				    syNetRbSnapFindWeaponGobjIdInSlotForOwner(slot, (s8)pi, nWPKindThunderHead, NULL);
			}
		}
	}
}

static void syNetRbSnapCaptureFighterCoupledIds(SYNetRbSnapFighterBlob *blob, const FTStruct *fp)
{
	blob->coupled_egg_weapon_gobj_id = 0U;
	blob->coupled_boomerang_weapon_gobj_id = 0U;
	blob->coupled_spin_attack_weapon_gobj_id = 0U;
	blob->coupled_charge_weapon_gobj_id = 0U;
	blob->coupled_pkthunder_weapon_gobj_id = 0U;
	blob->coupled_thunder_weapon_gobj_id = 0U;
	blob->guard_effect_gobj_id = 0U;
	blob->captureyoshi_effect_gobj_id = 0U;
	blob->fox_speciallw_effect_gobj_id = 0U;

	if ((fp->is_shield != FALSE) || ((fp->status_id >= nFTCommonStatusGuardStart) &&
	                               (fp->status_id <= nFTCommonStatusGuardEnd)))
	{
		blob->guard_effect_gobj_id = syNetRbSnapGobjId(fp->status_vars.common.guard.effect_gobj);
	}
	if (fp->status_id == nFTCommonStatusCaptureYoshi)
	{
		blob->captureyoshi_effect_gobj_id =
		    syNetRbSnapGobjId(fp->status_vars.common.captureyoshi.effect_gobj);
	}
	if (fp->fkind == nFTKindFox)
	{
		if (((fp->status_id >= nFTFoxStatusSpecialLwScopeStart) &&
		     (fp->status_id <= nFTFoxStatusSpecialLwScopeEnd)) ||
		    ((fp->status_id >= nFTFoxStatusSpecialAirLwStart) &&
		     (fp->status_id <= nFTFoxStatusSpecialAirLwTurn)))
		{
			blob->fox_speciallw_effect_gobj_id = syNetRbSnapGobjId(fp->status_vars.fox.speciallw.effect_gobj);
		}
	}

	if (fp->fkind == nFTKindYoshi)
	{
		if ((fp->status_id == nFTYoshiStatusSpecialHi) || (fp->status_id == nFTYoshiStatusSpecialAirHi))
		{
			blob->coupled_egg_weapon_gobj_id =
			    syNetRbSnapWeaponInstanceIdFromGObj(fp->status_vars.yoshi.specialhi.egg_gobj);
		}
	}
	if (fp->fkind == nFTKindLink)
	{
		blob->coupled_boomerang_weapon_gobj_id =
		    syNetRbSnapWeaponInstanceIdFromGObj(fp->passive_vars.link.boomerang_gobj);
		if ((fp->status_id == nFTLinkStatusSpecialHi) || (fp->status_id == nFTLinkStatusSpecialAirHi))
		{
			blob->coupled_spin_attack_weapon_gobj_id =
			    syNetRbSnapWeaponInstanceIdFromGObj(fp->status_vars.link.specialhi.spin_attack_gobj);
		}
	}
	if (fp->fkind == nFTKindKirby)
	{
		blob->coupled_boomerang_weapon_gobj_id =
		    syNetRbSnapWeaponInstanceIdFromGObj(fp->passive_vars.kirby.copylink_boomerang_gobj);
		if ((fp->status_id == nFTKirbyStatusCopySamusSpecialNStart) ||
		    (fp->status_id == nFTKirbyStatusCopySamusSpecialNLoop) ||
		    (fp->status_id == nFTKirbyStatusCopySamusSpecialAirNStart))
		{
			blob->coupled_charge_weapon_gobj_id =
			    syNetRbSnapWeaponInstanceIdFromGObj(fp->status_vars.kirby.copysamus_specialn.charge_gobj);
		}
	}
	if (fp->fkind == nFTKindSamus)
	{
		if ((fp->status_id == nFTSamusStatusSpecialNStart) || (fp->status_id == nFTSamusStatusSpecialNLoop) ||
		    (fp->status_id == nFTSamusStatusSpecialAirNStart))
		{
			blob->coupled_charge_weapon_gobj_id =
			    syNetRbSnapWeaponInstanceIdFromGObj(fp->status_vars.samus.specialn.charge_gobj);
		}
	}
	if (fp->fkind == nFTKindNess)
	{
		if ((fp->status_id == nFTNessStatusSpecialHiStart) || (fp->status_id == nFTNessStatusSpecialHiHold) ||
		    (fp->status_id == nFTNessStatusSpecialAirHiStart) ||
		    (fp->status_id == nFTNessStatusSpecialAirHiHold))
		{
			blob->coupled_pkthunder_weapon_gobj_id =
			    syNetRbSnapWeaponInstanceIdFromGObj(fp->status_vars.ness.specialhi.pkthunder_gobj);
		}
	}
	if (fp->fkind == nFTKindPikachu)
	{
		if ((fp->status_id == nFTPikachuStatusSpecialLwStart) ||
		    (fp->status_id == nFTPikachuStatusSpecialLwLoop) ||
		    (fp->status_id == nFTPikachuStatusSpecialAirLwStart) ||
		    (fp->status_id == nFTPikachuStatusSpecialAirLwLoop))
		{
			blob->coupled_thunder_weapon_gobj_id =
			    syNetRbSnapWeaponInstanceIdFromGObj(fp->status_vars.pikachu.speciallw.thunder_gobj);
		}
	}
}

/* Coupled GObj pointers in status_vars/passive_vars are never trusted from memcpy — ids on the fighter blob are authoritative. */
static void syNetRbSnapClearCoupledGObjPointersInStatusPassive(union FTStatusVars *status_vars,
							       union FTPassiveVars *passive_vars)
{
	status_vars->common.guard.effect_gobj = NULL;
	status_vars->common.captureyoshi.effect_gobj = NULL;
	status_vars->fox.speciallw.effect_gobj = NULL;
	passive_vars->link.boomerang_gobj = NULL;
	passive_vars->kirby.copylink_boomerang_gobj = NULL;
	status_vars->yoshi.specialhi.egg_gobj = NULL;
	status_vars->link.specialhi.spin_attack_gobj = NULL;
	status_vars->kirby.copysamus_specialn.charge_gobj = NULL;
	status_vars->samus.specialn.charge_gobj = NULL;
	status_vars->ness.specialhi.pkthunder_gobj = NULL;
	status_vars->pikachu.speciallw.thunder_gobj = NULL;
}

static void syNetRbSnapScrubCoupledPointersInBlob(SYNetRbSnapFighterBlob *blob)
{
	syNetRbSnapClearCoupledGObjPointersInStatusPassive((union FTStatusVars *)blob->status_vars,
							   (union FTPassiveVars *)blob->passive_vars);
}

static void syNetRbSnapScrubCoupledPointersInFighter(FTStruct *fp, const SYNetRbSnapFighterBlob *blob)
{
	(void)blob;
	syNetRbSnapClearCoupledGObjPointersInStatusPassive(&fp->status_vars, &fp->passive_vars);
}

/* Any live fighter with catch/capture GObj coupling (all link slots, not just human players). */
sb32 syNetRbSnapshotAnyFighterGrabCouplingActive(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp != NULL) && ((fp->catch_gobj != NULL) || (fp->capture_gobj != NULL)))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapshotAnyFighterItemThrowActive(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if (fp == NULL)
		{
			continue;
		}
		if ((fp->status_id >= nFTCommonStatusLightThrowStart) &&
		    (fp->status_id <= nFTCommonStatusLightThrowEnd))
		{
			return TRUE;
		}
		if ((fp->status_id >= nFTCommonStatusHeavyThrowStart) &&
		    (fp->status_id <= nFTCommonStatusHeavyThrowEnd))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapshotAnyItemThrowInFlight(void)
{
	GObj *item_gobj;

	for (item_gobj = gGCCommonLinks[nGCCommonLinkIDItem]; item_gobj != NULL; item_gobj = item_gobj->link_next)
	{
		ITStruct *ip = itGetStruct(item_gobj);

		if ((ip != NULL) && (ip->is_thrown != FALSE) && (ip->owner_gobj != NULL))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static s32 syNetRbSnapCountLiveWeapons(void)
{
	GObj *gobj;
	s32 count;

	count = 0;
	for (gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; gobj != NULL; gobj = gobj->link_next)
	{
		if (wpGetStruct(gobj) != NULL)
		{
			count++;
		}
	}
	return count;
}

/* Rebind MPColl translate/lr pointers after fighter joint restore (ApplyWorld does not refresh these). */
static void syNetRbSnapRebindAllFighterMPCollPointers(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		Vec3f *topn;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			continue;
		}
		topn = NULL;
		if (fp->joints[nFTPartsJointTopN] != NULL)
		{
			topn = &fp->joints[nFTPartsJointTopN]->translate.vec.f;
		}
		fp->coll_data.p_translate = topn;
		fp->coll_data.p_lr = &fp->lr;
		fp->coll_data.p_map_coll = &fp->coll_data.map_coll;
	}
}

sb32 syNetRbSnapshotSynctestShouldSkip(const char **reason_out)
{
	if ((gSCManagerBattleState != NULL) && (gSCManagerBattleState->game_status == nSCBattleGameStatusWait))
	{
		if (reason_out != NULL)
		{
			*reason_out = "intro_wait";
		}
		return TRUE;
	}
	if (syNetRbSnapshotAnyItemHoldCouplingActive() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "item_hold";
		}
		return TRUE;
	}
	if (syNetRbSnapshotAnyFighterItemThrowActive() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "fighter_throw";
		}
		return TRUE;
	}
	if (syNetRbSnapshotAnyItemThrowInFlight() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "item_throw";
		}
		return TRUE;
	}
	return FALSE;
}

/* Any live held item or fighter item_gobj coupling (all link slots). */
sb32 syNetRbSnapshotAnyItemHoldCouplingActive(void)
{
	GObj *fighter_gobj;
	GObj *item_gobj;

	for (item_gobj = gGCCommonLinks[nGCCommonLinkIDItem]; item_gobj != NULL; item_gobj = item_gobj->link_next)
	{
		ITStruct *ip = itGetStruct(item_gobj);

		if ((ip != NULL) && (ip->is_hold != FALSE))
		{
			return TRUE;
		}
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp != NULL) && (fp->item_gobj != NULL))
		{
			return TRUE;
		}
	}
	return FALSE;
}

/* Restore bidirectional item_gobj <-> owner_gobj for held items (all fighters/items). */
static void syNetRbSnapRebindFighterItemHoldCoupling(void)
{
	GObj *fighter_gobj;
	GObj *item_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		GObj *held_item_gobj;
		ITStruct *ip;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			continue;
		}
		held_item_gobj = fp->item_gobj;
		if (held_item_gobj == NULL)
		{
			continue;
		}
		ip = itGetStruct(held_item_gobj);
		if ((ip != NULL) && (ip->is_hold != FALSE))
		{
			ip->owner_gobj = fighter_gobj;
		}
	}
	for (item_gobj = gGCCommonLinks[nGCCommonLinkIDItem]; item_gobj != NULL; item_gobj = item_gobj->link_next)
	{
		ITStruct *ip;
		FTStruct *fp;

		ip = itGetStruct(item_gobj);
		if ((ip == NULL) || (ip->is_hold == FALSE) || (ip->owner_gobj == NULL))
		{
			continue;
		}
		fp = ftGetStruct(ip->owner_gobj);
		if (fp != NULL)
		{
			fp->item_gobj = item_gobj;
		}
	}
}

/*
 * Recompute coupled victim pose from grabber joint transforms after grabber parts are current.
 *
 * Load path: after presentation sync + joint anim reapply (finalize phase).
 * Forward path: after gcRunAll each rollback sim tick (syNetRollbackAfterBattleUpdate).
 *
 * Covers standard grab hold (CaptureWait/Pulled), throw arc (Thrown*), and DK cargo
 * (Shouldered on victim + ThrowF* on grabber — victim uses ftCommonThrownProcPhysics).
 */
void syNetRbSnapshotRefreshGrabCouplingGeometry(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		GObj *grabber_gobj;
		FTStruct *grabber_fp;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			continue;
		}
		grabber_gobj = fp->capture_gobj;
		if (grabber_gobj == NULL)
		{
			continue;
		}
		grabber_fp = ftGetStruct(grabber_gobj);
		if (grabber_fp == NULL)
		{
			continue;
		}

		ftParamInvalidateFighterTransformFromRoot(grabber_gobj);

		if (fp->status_id == nFTCommonStatusCaptureWait)
		{
			ftCommonCapturePulledProcPhysics(fighter_gobj);
			ftCommonCaptureWaitProcMap(fighter_gobj);
		}
		else if (fp->status_id == nFTCommonStatusCapturePulled)
		{
			ftCommonCapturePulledProcPhysics(fighter_gobj);
			ftCommonCapturePulledProcMap(fighter_gobj);
		}
		else if ((fp->status_id >= nFTCommonStatusThrownStart) && (fp->status_id <= nFTCommonStatusThrownEnd))
		{
			ftCommonThrownProcPhysics(fighter_gobj);
			ftCommonThrownProcMap(fighter_gobj);
		}
	}
}

/* Restore bidirectional catch_gobj <-> capture_gobj after snapshot apply (all fighters). */
static void syNetRbSnapRebindFighterGrabCoupling(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		GObj *catch_gobj;
		GObj *capture_gobj;
		FTStruct *catch_fp;
		FTStruct *capture_fp;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			continue;
		}
		catch_gobj = fp->catch_gobj;
		if (catch_gobj != NULL)
		{
			catch_fp = ftGetStruct(catch_gobj);
			if (catch_fp != NULL)
			{
				catch_fp->capture_gobj = fighter_gobj;
			}
		}
		capture_gobj = fp->capture_gobj;
		if (capture_gobj != NULL)
		{
			capture_fp = ftGetStruct(capture_gobj);
			if (capture_fp != NULL)
			{
				capture_fp->catch_gobj = fighter_gobj;
			}
		}
	}
}

static void syNetRbSnapRebindFighterCoupledGObjs(const SYNetRbSnapshotSlot *slot, sb32 refresh_coupled_weapon_geometry)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		s32 slot_index;
		const SYNetRbSnapFighterBlob *blob;
		GObj *egg_gobj;
		GObj *boomerang_gobj;
		GObj *spin_gobj;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			continue;
		}
		slot_index = fp->player;
		if ((slot_index < 0) || (slot_index >= GMCOMMON_PLAYERS_MAX))
		{
			continue;
		}
		blob = &slot->fighters[slot_index];
		if (blob->is_valid == FALSE)
		{
			continue;
		}

		egg_gobj = NULL;
		if ((fp->fkind == nFTKindYoshi) &&
		    ((fp->status_id == nFTYoshiStatusSpecialHi) || (fp->status_id == nFTYoshiStatusSpecialAirHi)))
		{
			egg_gobj = syNetRbSnapResolveCoupledWeaponGobj(slot, blob->coupled_egg_weapon_gobj_id, fighter_gobj,
			                                             nWPKindEggThrow, syNetRbSnapWeaponBlobEggIsCharging,
			                                             syNetRbSnapWeaponEggIsCharging);
			if (egg_gobj == NULL)
			{
				egg_gobj = syNetRbSnapReacquireYoshiChargeEgg(fighter_gobj);
			}
			if ((egg_gobj == NULL) && (blob->coupled_egg_weapon_gobj_id != 0U))
			{
				syNetRbSnapLogCoupledWeaponMiss(slot, (int)fp->player, "egg", blob->coupled_egg_weapon_gobj_id);
			}
			fp->status_vars.yoshi.specialhi.egg_gobj = egg_gobj;
			if ((refresh_coupled_weapon_geometry != FALSE) && (egg_gobj != NULL))
			{
				WPStruct *wp = wpGetStruct(egg_gobj);

				if (wp != NULL)
				{
					syNetRbSnapRestoreYoshiChargeEggCoupling(wp, fp);
				}
			}
			syNetRbSnapLogCoupledWeaponDiag(slot, (int)fp->player, "egg", blob->coupled_egg_weapon_gobj_id,
			                                egg_gobj);
			if (egg_gobj != NULL)
			{
				syNetRbSnapCullYoshiChargeEggsForFighter(fighter_gobj, egg_gobj);
			}
		}

		boomerang_gobj = NULL;
		if (fp->fkind == nFTKindLink)
		{
			boomerang_gobj = syNetRbSnapResolveCoupledWeaponGobj(slot, blob->coupled_boomerang_weapon_gobj_id,
			                                                     fighter_gobj, nWPKindBoomerang, NULL,
			                                                     syNetRbSnapWeaponOwnedByGObj);
			if ((boomerang_gobj == NULL) && (blob->coupled_boomerang_weapon_gobj_id != 0U))
			{
				syNetRbSnapLogCoupledWeaponMiss(slot, (int)fp->player, "boomerang",
				                                blob->coupled_boomerang_weapon_gobj_id);
			}
			fp->passive_vars.link.boomerang_gobj = boomerang_gobj;
			syNetRbSnapLogCoupledWeaponDiag(slot, (int)fp->player, "boomerang", blob->coupled_boomerang_weapon_gobj_id,
			                                boomerang_gobj);
		}
		if (fp->fkind == nFTKindKirby)
		{
			boomerang_gobj = syNetRbSnapResolveCoupledWeaponGobj(slot, blob->coupled_boomerang_weapon_gobj_id,
			                                                     fighter_gobj, nWPKindBoomerang, NULL,
			                                                     syNetRbSnapWeaponOwnedByGObj);
			if ((boomerang_gobj == NULL) && (blob->coupled_boomerang_weapon_gobj_id != 0U))
			{
				syNetRbSnapLogCoupledWeaponMiss(slot, (int)fp->player, "boomerang",
				                                blob->coupled_boomerang_weapon_gobj_id);
			}
			fp->passive_vars.kirby.copylink_boomerang_gobj = boomerang_gobj;
			syNetRbSnapLogCoupledWeaponDiag(slot, (int)fp->player, "boomerang", blob->coupled_boomerang_weapon_gobj_id,
			                                boomerang_gobj);
		}

		spin_gobj = NULL;
		if (fp->fkind == nFTKindLink)
		{
			if ((fp->status_id == nFTLinkStatusSpecialHi) || (fp->status_id == nFTLinkStatusSpecialAirHi))
			{
				spin_gobj = syNetRbSnapResolveCoupledWeaponGobj(slot, blob->coupled_spin_attack_weapon_gobj_id,
				                                                fighter_gobj, nWPKindSpinAttack, NULL,
				                                                syNetRbSnapWeaponOwnedByGObj);
				if ((spin_gobj == NULL) && (blob->coupled_spin_attack_weapon_gobj_id != 0U))
				{
					syNetRbSnapLogCoupledWeaponMiss(slot, (int)fp->player, "spin",
					                                blob->coupled_spin_attack_weapon_gobj_id);
				}
				fp->status_vars.link.specialhi.spin_attack_gobj = spin_gobj;
				syNetRbSnapLogCoupledWeaponDiag(slot, (int)fp->player, "spin",
				                                blob->coupled_spin_attack_weapon_gobj_id, spin_gobj);
			}
		}

		if (fp->fkind == nFTKindSamus)
		{
			if ((fp->status_id == nFTSamusStatusSpecialNStart) || (fp->status_id == nFTSamusStatusSpecialNLoop) ||
			    (fp->status_id == nFTSamusStatusSpecialAirNStart))
			{
				GObj *charge_gobj = syNetRbSnapResolveCoupledWeaponGobj(
				    slot, blob->coupled_charge_weapon_gobj_id, fighter_gobj, nWPKindChargeShot,
				    syNetRbSnapWeaponBlobChargeShotIsCharging, syNetRbSnapWeaponChargeShotIsCharging);

				if (charge_gobj == NULL)
				{
					charge_gobj = syNetRbSnapReacquireChargeShotForFP(fp);
				}
				if ((charge_gobj == NULL) && (blob->coupled_charge_weapon_gobj_id != 0U))
				{
					syNetRbSnapLogCoupledWeaponMiss(slot, (int)fp->player, "charge",
					                                blob->coupled_charge_weapon_gobj_id);
				}
				fp->status_vars.samus.specialn.charge_gobj = charge_gobj;
				if ((refresh_coupled_weapon_geometry != FALSE) && (charge_gobj != NULL))
				{
					ftSamusSpecialNSetChargeShotPosition(fp);
				}
				syNetRbSnapLogCoupledWeaponDiag(slot, (int)fp->player, "charge", blob->coupled_charge_weapon_gobj_id,
				                                charge_gobj);
				if (charge_gobj != NULL)
				{
					syNetRbSnapCullSamusChargeShotsForFighter(fighter_gobj, charge_gobj);
				}
			}
			else
			{
				fp->status_vars.samus.specialn.charge_gobj = NULL;
				syNetRbSnapCullSamusChargeShotsForFighter(fighter_gobj, NULL);
			}
		}
		if (fp->fkind == nFTKindKirby)
		{
			if ((fp->status_id == nFTKirbyStatusCopySamusSpecialNStart) ||
			    (fp->status_id == nFTKirbyStatusCopySamusSpecialNLoop) ||
			    (fp->status_id == nFTKirbyStatusCopySamusSpecialAirNStart))
			{
				GObj *charge_gobj = syNetRbSnapResolveCoupledWeaponGobj(
				    slot, blob->coupled_charge_weapon_gobj_id, fighter_gobj, nWPKindChargeShot,
				    syNetRbSnapWeaponBlobChargeShotIsCharging, syNetRbSnapWeaponChargeShotIsCharging);

				if (charge_gobj == NULL)
				{
					charge_gobj = syNetRbSnapReacquireChargeShotForFP(fp);
				}
				if ((charge_gobj == NULL) && (blob->coupled_charge_weapon_gobj_id != 0U))
				{
					syNetRbSnapLogCoupledWeaponMiss(slot, (int)fp->player, "charge",
					                                blob->coupled_charge_weapon_gobj_id);
				}
				fp->status_vars.kirby.copysamus_specialn.charge_gobj = charge_gobj;
				if ((refresh_coupled_weapon_geometry != FALSE) && (charge_gobj != NULL))
				{
					ftKirbyCopySamusSpecialNSetChargeShotPosition(fp);
				}
				syNetRbSnapLogCoupledWeaponDiag(slot, (int)fp->player, "charge", blob->coupled_charge_weapon_gobj_id,
				                                charge_gobj);
				if (charge_gobj != NULL)
				{
					syNetRbSnapCullSamusChargeShotsForFighter(fighter_gobj, charge_gobj);
				}
			}
			else
			{
				fp->status_vars.kirby.copysamus_specialn.charge_gobj = NULL;
				syNetRbSnapCullSamusChargeShotsForFighter(fighter_gobj, NULL);
			}
		}

		if (fp->fkind == nFTKindNess)
		{
			if ((fp->status_id == nFTNessStatusSpecialHiStart) || (fp->status_id == nFTNessStatusSpecialHiHold) ||
			    (fp->status_id == nFTNessStatusSpecialAirHiStart) ||
			    (fp->status_id == nFTNessStatusSpecialAirHiHold))
			{
				GObj *pkthunder_gobj = syNetRbSnapResolveCoupledWeaponGobj(
				    slot, blob->coupled_pkthunder_weapon_gobj_id, fighter_gobj, nWPKindPKThunderHead, NULL,
				    syNetRbSnapWeaponOwnedByGObj);

				if (pkthunder_gobj == NULL)
				{
					pkthunder_gobj = syNetRbSnapReacquirePKThunderHeadForFighter(fighter_gobj);
				}
				if ((pkthunder_gobj == NULL) && (blob->coupled_pkthunder_weapon_gobj_id != 0U))
				{
					syNetRbSnapLogCoupledWeaponMiss(slot, (int)fp->player, "pkthunder",
					                                blob->coupled_pkthunder_weapon_gobj_id);
				}
				fp->status_vars.ness.specialhi.pkthunder_gobj = pkthunder_gobj;
				syNetRbSnapLogCoupledWeaponDiag(slot, (int)fp->player, "pkthunder",
				                                blob->coupled_pkthunder_weapon_gobj_id, pkthunder_gobj);
				syNetRbSnapCullOwnedPKThunderForFighter(fighter_gobj, pkthunder_gobj);
			}
			else
			{
				fp->status_vars.ness.specialhi.pkthunder_gobj = NULL;
				syNetRbSnapCullOwnedPKThunderForFighter(fighter_gobj, NULL);
			}
		}

		if (fp->fkind == nFTKindPikachu)
		{
			if ((fp->status_id == nFTPikachuStatusSpecialLwStart) ||
			    (fp->status_id == nFTPikachuStatusSpecialLwLoop) ||
			    (fp->status_id == nFTPikachuStatusSpecialAirLwStart) ||
			    (fp->status_id == nFTPikachuStatusSpecialAirLwLoop))
			{
				GObj *thunder_gobj = syNetRbSnapResolveCoupledWeaponGobj(
				    slot, blob->coupled_thunder_weapon_gobj_id, fighter_gobj, nWPKindThunderHead, NULL,
				    syNetRbSnapWeaponOwnedByGObj);

				if ((thunder_gobj == NULL) && (blob->coupled_thunder_weapon_gobj_id != 0U))
				{
					syNetRbSnapLogCoupledWeaponMiss(slot, (int)fp->player, "thunder",
					                                blob->coupled_thunder_weapon_gobj_id);
				}
				fp->status_vars.pikachu.speciallw.thunder_gobj = thunder_gobj;
				syNetRbSnapLogCoupledWeaponDiag(slot, (int)fp->player, "thunder",
				                                blob->coupled_thunder_weapon_gobj_id, thunder_gobj);
			}
		}
	}
}

static void syNetRbSnapRefreshWeaponHitPositions(void)
{
	GObj *weapon_gobj;

	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp = wpGetStruct(weapon_gobj);

		if ((wp != NULL) && (wp->attack_coll.attack_state != nGMAttackStateOff))
		{
			wpProcessUpdateHitPositions(weapon_gobj);
		}
	}
}
#endif /* PORT */

static void syNetRbSnapCaptureMPColl(SYNetRbSnapMPCollBlob *dst, const MPCollData *src)
{
	dst->pos_prev = src->pos_prev;
	dst->pos_diff = src->pos_diff;
	dst->vel_speed = src->vel_speed;
	dst->vel_push = src->vel_push;
	dst->map_coll = src->map_coll;
	dst->cliffcatch_coll = src->cliffcatch_coll;
	dst->mask_prev = src->mask_prev;
	dst->mask_curr = src->mask_curr;
	dst->mask_unk = src->mask_unk;
	dst->mask_stat = src->mask_stat;
	dst->update_tic = src->update_tic;
	dst->ewall_line_id = src->ewall_line_id;
	dst->is_coll_end = src->is_coll_end;
	dst->line_coll_dist = src->line_coll_dist;
	dst->floor_line_id = src->floor_line_id;
	dst->floor_dist = src->floor_dist;
	dst->floor_flags = src->floor_flags;
	dst->floor_angle = src->floor_angle;
	dst->ceil_line_id = src->ceil_line_id;
	dst->ceil_flags = src->ceil_flags;
	dst->ceil_angle = src->ceil_angle;
	dst->lwall_line_id = src->lwall_line_id;
	dst->lwall_flags = src->lwall_flags;
	dst->lwall_angle = src->lwall_angle;
	dst->rwall_line_id = src->rwall_line_id;
	dst->rwall_flags = src->rwall_flags;
	dst->rwall_angle = src->rwall_angle;
	dst->cliff_id = src->cliff_id;
	dst->ignore_line_id = src->ignore_line_id;
}

static void syNetRbSnapApplyMPColl(MPCollData *dst, const SYNetRbSnapMPCollBlob *src, Vec3f *p_translate, s32 *p_lr)
{
	dst->p_translate = p_translate;
	dst->p_lr = p_lr;
	dst->p_map_coll = &dst->map_coll;
	dst->pos_prev = src->pos_prev;
	dst->pos_diff = src->pos_diff;
	dst->vel_speed = src->vel_speed;
	dst->vel_push = src->vel_push;
	dst->map_coll = src->map_coll;
	dst->cliffcatch_coll = src->cliffcatch_coll;
	dst->mask_prev = src->mask_prev;
	dst->mask_curr = src->mask_curr;
	dst->mask_unk = src->mask_unk;
	dst->mask_stat = src->mask_stat;
	dst->update_tic = src->update_tic;
	dst->ewall_line_id = src->ewall_line_id;
	dst->is_coll_end = src->is_coll_end;
	dst->line_coll_dist = src->line_coll_dist;
	dst->floor_line_id = src->floor_line_id;
	dst->floor_dist = src->floor_dist;
	dst->floor_flags = src->floor_flags;
	dst->floor_angle = src->floor_angle;
	dst->ceil_line_id = src->ceil_line_id;
	dst->ceil_flags = src->ceil_flags;
	dst->ceil_angle = src->ceil_angle;
	dst->lwall_line_id = src->lwall_line_id;
	dst->lwall_flags = src->lwall_flags;
	dst->lwall_angle = src->lwall_angle;
	dst->rwall_line_id = src->rwall_line_id;
	dst->rwall_flags = src->rwall_flags;
	dst->rwall_angle = src->rwall_angle;
	dst->cliff_id = src->cliff_id;
	dst->ignore_line_id = src->ignore_line_id;
}

static void syNetRbSnapCaptureAObjNode(SYNetRbSnapAObjNodeBlob *dst, const AObj *aobj)
{
	memset(dst, 0, sizeof(*dst));
	if (aobj == NULL)
	{
		return;
	}
	dst->track = aobj->track;
	dst->kind = aobj->kind;
	dst->length_invert = aobj->length_invert;
	dst->length = aobj->length;
	dst->value_base = aobj->value_base;
	dst->value_target = aobj->value_target;
	dst->rate_base = aobj->rate_base;
	dst->rate_target = aobj->rate_target;
	dst->interpolate_ptr = (uintptr_t)aobj->interpolate;
}

static void syNetRbSnapApplyAObjNode(AObj *aobj, const SYNetRbSnapAObjNodeBlob *src)
{
	if ((aobj == NULL) || (src == NULL))
	{
		return;
	}
	aobj->track = src->track;
	aobj->kind = src->kind;
	aobj->length_invert = src->length_invert;
	aobj->length = src->length;
	aobj->value_base = src->value_base;
	aobj->value_target = src->value_target;
	aobj->rate_base = src->rate_base;
	aobj->rate_target = src->rate_target;
	aobj->interpolate = (void *)src->interpolate_ptr;
}

#ifdef PORT
/*
 * AObj chain rebuild on apply (default ON). Disable via SSB64_NETPLAY_AOBJ_CHAIN_REBUILD=0 for A/B
 * bisect against the legacy in-place patch path. The chain topology (node count + per-slot track/kind)
 * depends on the script-parser history (`gcParseDObjAnimJoint`) and is monotonically grown by
 * `gcAddAObjForDObj` during playback. The legacy apply only patched node field VALUES — if the live
 * chain length or order differed from the captured chain, leftover live nodes or missing nodes silently
 * desynced the hash (`syNetSyncFoldFighterAnimJointContribution` folds the FULL live chain count and the
 * first 16 nodes' fields). The rebuild path drops the live chain entirely, then re-allocates the exact
 * snapshot topology in head→tail capture order.
 */
static sb32 syNetRbSnapAObjChainRebuildEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_AOBJ_CHAIN_REBUILD");
	if ((e == NULL) || (e[0] == '\0'))
	{
		s_env_cache = 1;
	}
	else
	{
		s_env_cache = (atoi(e) != 0) ? 1 : 0;
	}
	return (s_env_cache != 0) ? TRUE : FALSE;
}
#endif

static void syNetRbSnapCaptureDObjAnim(SYNetRbSnapDObjAnimBlob *dst, DObj *dobj)
{
	AObj *aobj;
	u8 count;

	memset(dst, 0, sizeof(*dst));
	if (dobj == NULL)
	{
		return;
	}
	dst->anim_wait = dobj->anim_wait;
	dst->anim_speed = dobj->anim_speed;
	dst->anim_frame = dobj->anim_frame;
	dst->is_anim_root = (u8)(dobj->is_anim_root != FALSE);
	dst->dobj_flags = dobj->flags;
	dst->event32_ptr = (uintptr_t)dobj->anim_joint.event32;
	{
		u8 chain_total;

		count = 0U;
		chain_total = 0U;
		for (aobj = dobj->aobj; aobj != NULL; aobj = aobj->next)
		{
			if (count < SYNETROLLBACK_SNAPSHOT_AOBJ_CHAIN_MAX)
			{
				syNetRbSnapCaptureAObjNode(&dst->aobj[count], aobj);
				count++;
			}
			chain_total++;
		}
		dst->aobj_count = count;
		dst->aobj_chain_total = chain_total;
#ifdef PORT
		if (chain_total > SYNETROLLBACK_SNAPSHOT_AOBJ_CHAIN_MAX)
		{
			port_log(
			    "SSB64 NetRbSnapshot: aobj_chain_overflow dobj=%p chain_total=%u cap=%u (truncating); "
			    "anim/figh hash will diverge across rollback for this joint\n",
			    (void *)dobj, (unsigned int)chain_total,
			    (unsigned int)SYNETROLLBACK_SNAPSHOT_AOBJ_CHAIN_MAX);
		}
#endif
	}
}

/*
 * Drop every AObj on the live chain back into the global pool. Mirrors `gcRemoveAObjFromDObj` but
 * inlined to avoid the `anim_wait = AOBJ_ANIM_NULL` side-effect — we restore anim cursor fields from
 * the blob immediately after, so the intermediate NULL state is wasted work and would clobber the
 * deterministic value we're about to write.
 */
static void syNetRbSnapDObjDrainAObjChain(DObj *dobj)
{
	AObj *current_aobj;
	AObj *next_aobj;

	if (dobj == NULL)
	{
		return;
	}
	current_aobj = dobj->aobj;
	while (current_aobj != NULL)
	{
		next_aobj = current_aobj->next;
		gcSetAObjPrevAlloc(current_aobj);
		current_aobj = next_aobj;
	}
	dobj->aobj = NULL;
}

static void syNetRbSnapApplyDObjAnim(DObj *dobj, const SYNetRbSnapDObjAnimBlob *src)
{
	AObj *aobj;
	u8 i;
	u8 apply_count;

	if (dobj == NULL)
	{
		return;
	}

	/* Per-DObj cursor + flags must be restored regardless of chain-rebuild policy. */
	dobj->anim_wait = src->anim_wait;
	dobj->anim_speed = src->anim_speed;
	dobj->anim_frame = src->anim_frame;
	dobj->is_anim_root = (src->is_anim_root != 0U) ? TRUE : FALSE;
	dobj->flags = src->dobj_flags;
	dobj->anim_joint.event32 = (AObjEvent32 *)src->event32_ptr;

	apply_count = src->aobj_count;
	if (apply_count > SYNETROLLBACK_SNAPSHOT_AOBJ_CHAIN_MAX)
	{
		apply_count = SYNETROLLBACK_SNAPSHOT_AOBJ_CHAIN_MAX;
	}

#ifdef PORT
	if (syNetRbSnapAObjChainRebuildEnabled() != FALSE)
	{
		/*
		 * Rebuild path: drop the live chain, then prepend snapshot entries tail→head so the rebuilt
		 * chain order (head→tail) equals the captured order. `gcAddAObjForDObj` prepends to `dobj->aobj`,
		 * so iterating snapshot[N-1..0] yields blob[0] at the head — bit-for-bit identical to capture.
		 */
		syNetRbSnapDObjDrainAObjChain(dobj);
		if (apply_count > 0U)
		{
			s32 idx;

			for (idx = (s32)apply_count - 1; idx >= 0; idx--)
			{
				AObj *node = gcAddAObjForDObj(dobj, src->aobj[idx].track);

				if (node == NULL)
				{
					break;
				}
				syNetRbSnapApplyAObjNode(node, &src->aobj[idx]);
			}
		}
		return;
	}
#endif

	/* Legacy in-place patch path (broken; kept for A/B bisect under env flag = 0). */
	aobj = dobj->aobj;
	for (i = 0U; (aobj != NULL) && (i < apply_count); i++)
	{
		syNetRbSnapApplyAObjNode(aobj, &src->aobj[i]);
		aobj = aobj->next;
	}
}

static void syNetRbSnapCaptureAttackColl(SYNetRbSnapAttackCollBlob *dst, const FTAttackColl *src)
{
	s32 i;

	dst->attack_state = src->attack_state;
	dst->group_id = src->group_id;
	dst->joint_id = src->joint_id;
	dst->damage = src->damage;
	dst->element = src->element;
	dst->offset = src->offset;
	dst->size = src->size;
	dst->angle = src->angle;
	dst->knockback_scale = src->knockback_scale;
	dst->knockback_weight = src->knockback_weight;
	dst->knockback_base = src->knockback_base;
	dst->shield_damage = src->shield_damage;
	dst->fgm_level = src->fgm_level;
	dst->fgm_kind = src->fgm_kind;
	dst->is_hit_air = src->is_hit_air;
	dst->is_hit_ground = src->is_hit_ground;
	dst->can_rebound = src->can_rebound;
	dst->is_scale_pos = src->is_scale_pos;
	dst->motion_attack_id = src->motion_attack_id;
	dst->motion_count = src->motion_count;
	dst->stat_count = src->stat_count;
	dst->pos_curr = src->pos_curr;
	dst->pos_prev = src->pos_prev;
	for (i = 0; i < GMATTACKREC_NUM_MAX; i++)
	{
		dst->attack_records[i].victim_flags = src->attack_records[i].victim_flags;
		dst->attack_records[i].victim_gobj_id =
		    syNetRbSnapGobjId(src->attack_records[i].victim_gobj);
	}
	dst->attack_matrix = src->attack_matrix;
}

static void syNetRbSnapApplyAttackColl(FTAttackColl *dst, const SYNetRbSnapAttackCollBlob *src, FTStruct *fp)
{
	s32 i;

	dst->attack_state = src->attack_state;
	dst->group_id = src->group_id;
	dst->joint_id = src->joint_id;
	dst->damage = src->damage;
	dst->element = src->element;
	dst->offset = src->offset;
	dst->size = src->size;
	dst->angle = src->angle;
	dst->knockback_scale = src->knockback_scale;
	dst->knockback_weight = src->knockback_weight;
	dst->knockback_base = src->knockback_base;
	dst->shield_damage = src->shield_damage;
	dst->fgm_level = src->fgm_level;
	dst->fgm_kind = src->fgm_kind;
	dst->is_hit_air = src->is_hit_air;
	dst->is_hit_ground = src->is_hit_ground;
	dst->can_rebound = src->can_rebound;
	dst->is_scale_pos = src->is_scale_pos;
	dst->motion_attack_id = src->motion_attack_id;
	dst->motion_count = src->motion_count;
	dst->stat_count = src->stat_count;
	dst->pos_curr = src->pos_curr;
	dst->pos_prev = src->pos_prev;
	if ((src->joint_id >= 0) && (src->joint_id < FTPARTS_JOINT_NUM_MAX))
	{
		dst->joint = fp->joints[src->joint_id];
	}
	else
	{
		dst->joint = NULL;
	}
	for (i = 0; i < GMATTACKREC_NUM_MAX; i++)
	{
		dst->attack_records[i].victim_flags = src->attack_records[i].victim_flags;
		dst->attack_records[i].victim_gobj = syNetRbSnapResolveLiveGobj(src->attack_records[i].victim_gobj_id);
	}
	dst->attack_matrix = src->attack_matrix;
}

static void syNetRbSnapCaptureFighter(SYNetRbSnapFighterBlob *blob, FTStruct *fp, GObj *fighter_gobj)
{
	s32 ji;

	memset(blob, 0, sizeof(*blob));
	blob->is_valid = TRUE;
	blob->player = fp->player;
	blob->fkind = fp->fkind;
	blob->gobj_id = syNetRbSnapGobjId(fighter_gobj);

	blob->status_id = fp->status_id;
	blob->motion_id = fp->motion_id;
	blob->percent_damage = fp->percent_damage;
	blob->damage_resist = fp->damage_resist;
	blob->shield_health = fp->shield_health;
	blob->stock_count = fp->stock_count;
	blob->lr = fp->lr;
	blob->ga = fp->ga;
	blob->hitlag_tics = fp->hitlag_tics;
	blob->status_total_tics = fp->status_total_tics;
	blob->jumps_used = fp->jumps_used;
	blob->physics = fp->physics;
	syNetRbSnapCaptureMPColl(&blob->coll, &fp->coll_data);

	blob->motion_vars_flags[0] = fp->motion_vars.flags.flag0;
	blob->motion_vars_flags[1] = fp->motion_vars.flags.flag1;
	blob->motion_vars_flags[2] = fp->motion_vars.flags.flag2;
	blob->motion_vars_flags[3] = fp->motion_vars.flags.flag3;
	blob->attack1_status_id = fp->attack1_status_id;
	blob->attack1_input_count = fp->attack1_input_count;
	blob->attack1_followup_frames = fp->attack1_followup_frames;
	blob->cliffcatch_wait = fp->cliffcatch_wait;
	blob->tics_since_last_z = fp->tics_since_last_z;
	blob->acid_wait = fp->acid_wait;
	blob->twister_wait = fp->twister_wait;
	blob->tarucann_wait = fp->tarucann_wait;
	blob->damagefloor_wait = fp->damagefloor_wait;
	blob->playertag_wait = fp->playertag_wait;

	blob->is_attack_active = fp->is_attack_active;
	blob->is_hitstatus_nodamage = fp->is_hitstatus_nodamage;
	blob->is_fastfall = fp->is_fastfall;
	blob->is_hitstun = fp->is_hitstun;
	blob->is_shield = fp->is_shield;
	blob->is_cliff_hold = fp->is_cliff_hold;
	blob->is_catchstatus = fp->is_catchstatus;
	blob->is_catch_or_capture = fp->is_catch_or_capture;
	blob->is_effect_attach = (u8)(fp->is_effect_attach != FALSE);
	blob->camera_mode = fp->camera_mode;

	blob->anim_desc = fp->anim_desc;
	blob->anim_vel = fp->anim_vel;
	blob->magnify_pos = fp->magnify_pos;

	blob->motion_attack_id = fp->motion_attack_id;
	blob->stat_flags = fp->stat_flags;
	blob->stat_count = fp->stat_count;
	blob->invincible_tics = fp->invincible_tics;
	blob->intangible_tics = fp->intangible_tics;
	blob->special_hitstatus = fp->special_hitstatus;
	blob->star_invincible_tics = fp->star_invincible_tics;
	blob->star_hitstatus = fp->star_hitstatus;
	blob->hitstatus = fp->hitstatus;

	for (ji = 0; ji < 4; ji++)
	{
		syNetRbSnapCaptureAttackColl(&blob->attack_colls[ji], &fp->attack_colls[ji]);
	}
	for (ji = 0; ji < 11; ji++)
	{
		blob->damage_colls[ji].hitstatus = fp->damage_colls[ji].hitstatus;
		blob->damage_colls[ji].joint_id = fp->damage_colls[ji].joint_id;
		blob->damage_colls[ji].placement = fp->damage_colls[ji].placement;
		blob->damage_colls[ji].is_grabbable = fp->damage_colls[ji].is_grabbable;
		blob->damage_colls[ji].offset = fp->damage_colls[ji].offset;
		blob->damage_colls[ji].size = fp->damage_colls[ji].size;
	}

	blob->hitlag_mul = fp->hitlag_mul;
	blob->attack_damage = fp->attack_damage;
	blob->attack_knockback = fp->attack_knockback;
	blob->shield_damage = fp->shield_damage;
	blob->damage_queue = fp->damage_queue;
	blob->damage_angle = fp->damage_angle;
	blob->damage_element = fp->damage_element;
	blob->damage_lr = fp->damage_lr;
	blob->damage_player = fp->damage_player;
	blob->damage_kind = fp->damage_kind;

	blob->throw_gobj_id = syNetRbSnapGobjId(fp->throw_gobj);
	blob->catch_gobj_id = syNetRbSnapGobjId(fp->catch_gobj);
	blob->capture_gobj_id = syNetRbSnapGobjId(fp->capture_gobj);
	blob->item_gobj_id = syNetRbSnapGobjId(fp->item_gobj);
	blob->search_gobj_id = syNetRbSnapGobjId(fp->search_gobj);
	blob->throw_desc_ptr = (fp->throw_desc != NULL) ? (uintptr_t)fp->throw_desc : 0U;

	memcpy(blob->motion_scripts, fp->motion_scripts, sizeof(blob->motion_scripts));
	memcpy(blob->status_vars, &fp->status_vars, sizeof(blob->status_vars));
	memcpy(blob->passive_vars, &fp->passive_vars, sizeof(blob->passive_vars));
#ifdef PORT
	syNetRbSnapCaptureFighterCoupledIds(blob, fp);
	syNetRbSnapScrubCoupledPointersInBlob(blob);
#endif
	memcpy(blob->modelpart_status, fp->modelpart_status, sizeof(blob->modelpart_status));
	memcpy(blob->texturepart_status, fp->texturepart_status, sizeof(blob->texturepart_status));

	for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
	{
		if (fp->joints[ji] != NULL)
		{
			blob->joint_translate[ji] = fp->joints[ji]->translate.vec.f;
			syNetRbSnapCaptureDObjAnim(&blob->joint_anim[ji], fp->joints[ji]);
			blob->joint_dobj_flags[ji] = fp->joints[ji]->flags;
			blob->joint_anim_joint_event32[ji] =
			    (fp->joints[ji]->anim_joint.event32 != NULL) ? (uintptr_t)fp->joints[ji]->anim_joint.event32 : 0U;
		}
	}
	if (fighter_gobj != NULL)
	{
		blob->gobj_anim_frame = fighter_gobj->anim_frame;
	}
}

static void syNetRbSnapApplyFighter(const SYNetRbSnapFighterBlob *blob, FTStruct *fp, GObj *fighter_gobj)
{
	s32 ji;
	Vec3f *topn;

	if ((blob->is_valid == FALSE) || (blob->player != fp->player) || (blob->fkind != fp->fkind))
	{
		return;
	}
	fp->status_id = blob->status_id;
	fp->motion_id = blob->motion_id;
	fp->percent_damage = blob->percent_damage;
	fp->damage_resist = blob->damage_resist;
	fp->shield_health = blob->shield_health;
	fp->stock_count = blob->stock_count;
	fp->lr = blob->lr;
	fp->ga = blob->ga;
	fp->hitlag_tics = blob->hitlag_tics;
	fp->status_total_tics = blob->status_total_tics;
	fp->jumps_used = blob->jumps_used;
	fp->physics = blob->physics;

	topn = NULL;
	if (fp->joints[nFTPartsJointTopN] != NULL)
	{
		topn = &fp->joints[nFTPartsJointTopN]->translate.vec.f;
	}
	syNetRbSnapApplyMPColl(&fp->coll_data, &blob->coll, topn, &fp->lr);

	fp->motion_vars.flags.flag0 = blob->motion_vars_flags[0];
	fp->motion_vars.flags.flag1 = blob->motion_vars_flags[1];
	fp->motion_vars.flags.flag2 = blob->motion_vars_flags[2];
	fp->motion_vars.flags.flag3 = blob->motion_vars_flags[3];
	fp->attack1_status_id = blob->attack1_status_id;
	fp->attack1_input_count = blob->attack1_input_count;
	fp->attack1_followup_frames = blob->attack1_followup_frames;
	fp->cliffcatch_wait = blob->cliffcatch_wait;
	fp->tics_since_last_z = blob->tics_since_last_z;
	fp->acid_wait = blob->acid_wait;
	fp->twister_wait = blob->twister_wait;
	fp->tarucann_wait = blob->tarucann_wait;
	fp->damagefloor_wait = blob->damagefloor_wait;
	fp->playertag_wait = blob->playertag_wait;

	fp->is_attack_active = blob->is_attack_active;
	fp->is_hitstatus_nodamage = blob->is_hitstatus_nodamage;
	fp->is_fastfall = blob->is_fastfall;
	fp->is_hitstun = blob->is_hitstun;
	fp->is_shield = blob->is_shield;
	fp->is_cliff_hold = blob->is_cliff_hold;
	fp->is_catchstatus = blob->is_catchstatus;
	fp->is_catch_or_capture = blob->is_catch_or_capture;
	fp->is_effect_attach = (blob->is_effect_attach != 0U) ? TRUE : FALSE;
	fp->camera_mode = blob->camera_mode;

	fp->anim_desc = blob->anim_desc;
	fp->anim_vel = blob->anim_vel;
	fp->magnify_pos = blob->magnify_pos;

	fp->motion_attack_id = blob->motion_attack_id;
	fp->stat_flags = blob->stat_flags;
	fp->stat_count = blob->stat_count;
	fp->invincible_tics = blob->invincible_tics;
	fp->intangible_tics = blob->intangible_tics;
	fp->special_hitstatus = blob->special_hitstatus;
	fp->star_invincible_tics = blob->star_invincible_tics;
	fp->star_hitstatus = blob->star_hitstatus;
	fp->hitstatus = blob->hitstatus;

	for (ji = 0; ji < 4; ji++)
	{
		syNetRbSnapApplyAttackColl(&fp->attack_colls[ji], &blob->attack_colls[ji], fp);
	}
	for (ji = 0; ji < 11; ji++)
	{
		fp->damage_colls[ji].hitstatus = blob->damage_colls[ji].hitstatus;
		fp->damage_colls[ji].joint_id = blob->damage_colls[ji].joint_id;
		fp->damage_colls[ji].placement = blob->damage_colls[ji].placement;
		fp->damage_colls[ji].is_grabbable = blob->damage_colls[ji].is_grabbable;
		fp->damage_colls[ji].offset = blob->damage_colls[ji].offset;
		fp->damage_colls[ji].size = blob->damage_colls[ji].size;
		if ((blob->damage_colls[ji].joint_id >= 0) && (blob->damage_colls[ji].joint_id < FTPARTS_JOINT_NUM_MAX))
		{
			fp->damage_colls[ji].joint = fp->joints[blob->damage_colls[ji].joint_id];
		}
		else
		{
			fp->damage_colls[ji].joint = NULL;
		}
	}

	fp->hitlag_mul = blob->hitlag_mul;
	fp->attack_damage = blob->attack_damage;
	fp->attack_knockback = blob->attack_knockback;
	fp->shield_damage = blob->shield_damage;
	fp->damage_queue = blob->damage_queue;
	fp->damage_angle = blob->damage_angle;
	fp->damage_element = blob->damage_element;
	fp->damage_lr = blob->damage_lr;
	fp->damage_player = blob->damage_player;
	fp->damage_kind = blob->damage_kind;

	fp->throw_gobj = syNetRbSnapResolveLiveGobj(blob->throw_gobj_id);
	fp->catch_gobj = syNetRbSnapResolveLiveGobj(blob->catch_gobj_id);
	fp->capture_gobj = syNetRbSnapResolveLiveGobj(blob->capture_gobj_id);
	fp->item_gobj = syNetRbSnapResolveItemGobj(blob->item_gobj_id);
	fp->search_gobj = syNetRbSnapResolveLiveGobj(blob->search_gobj_id);
	fp->throw_desc = (blob->throw_desc_ptr != 0U) ? (FTThrowHitDesc *)blob->throw_desc_ptr : NULL;

	memcpy(fp->motion_scripts, blob->motion_scripts, sizeof(fp->motion_scripts));
	memcpy(&fp->status_vars, blob->status_vars, sizeof(fp->status_vars));
	memcpy(&fp->passive_vars, blob->passive_vars, sizeof(fp->passive_vars));
#ifdef PORT
	syNetRbSnapScrubCoupledPointersInFighter(fp, blob);
#endif
	memcpy(fp->modelpart_status, blob->modelpart_status, sizeof(fp->modelpart_status));
	memcpy(fp->texturepart_status, blob->texturepart_status, sizeof(fp->texturepart_status));

	for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
	{
		if (fp->joints[ji] != NULL)
		{
			fp->joints[ji]->translate.vec.f = blob->joint_translate[ji];
			syNetRbSnapApplyDObjAnim(fp->joints[ji], &blob->joint_anim[ji]);
			if (blob->joint_anim_joint_event32[ji] != 0U)
			{
				fp->joints[ji]->anim_joint.event32 = (AObjEvent32 *)blob->joint_anim_joint_event32[ji];
			}
			else
			{
				fp->joints[ji]->anim_joint.event32 = NULL;
			}
			fp->joints[ji]->flags = blob->joint_dobj_flags[ji];
		}
	}
	if (fighter_gobj != NULL)
	{
		fighter_gobj->anim_frame = blob->gobj_anim_frame;
	}
}

#ifdef PORT
static sb32 syNetRbSnapFighterDiagEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_SNAPSHOT_FIGHTER_DIAG");
	s_env_cache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}

static sb32 syNetRbSnapFighterIsInThunderJoltThrowStatus(const FTStruct *fp);
static sb32 syNetRbSnapFighterIsInFireballThrowStatus(const FTStruct *fp);

static void syNetRbSnapRebindFighterStatusProcs(GObj *fighter_gobj, FTStruct *fp)
{
	if ((fighter_gobj == NULL) || (fp == NULL))
	{
		return;
	}
	ftMainRebindStatusProcs(fighter_gobj);
	fp->proc_status = NULL;
	fp->proc_accessory = NULL;
	fp->proc_damage = NULL;
	fp->proc_trap = NULL;
	fp->proc_hit = NULL;
	fp->proc_shield = NULL;
	fp->proc_passive = NULL;
	fp->proc_lagupdate = NULL;
	fp->proc_lagstart = NULL;
	fp->proc_lagend = NULL;
	if (syNetRbSnapFighterIsInThunderJoltThrowStatus(fp) != FALSE)
	{
		if (fp->fkind == nFTKindKirby)
		{
			fp->proc_accessory = ftKirbyCopyPikachuSpecialNProcAccessory;
		}
		else if (fp->fkind == nFTKindPikachu)
		{
			fp->proc_accessory = ftPikachuSpecialNProcAccessory;
		}
	}
	else if ((fp->fkind == nFTKindNess) &&
	         ((fp->status_id == nFTNessStatusSpecialN) || (fp->status_id == nFTNessStatusSpecialAirN)))
	{
		fp->proc_accessory = ftNessSpecialNProcAccessory;
	}
	else if (syNetRbSnapFighterIsInFireballThrowStatus(fp) != FALSE)
	{
		if (fp->fkind == nFTKindKirby)
		{
			fp->proc_accessory = ftKirbyCopyMarioSpecialNProcAccessory;
		}
		else
		{
			fp->proc_accessory = ftMarioSpecialNProcAccessory;
		}
	}
}

void syNetRbSnapshotRebindAllFighters(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if (fp != NULL)
		{
			syNetRbSnapRebindFighterStatusProcs(fighter_gobj, fp);
		}
	}
	syNetRbSnapRebindFighterGrabCoupling();
	syNetRbSnapRebindFighterItemHoldCoupling();
}

void syNetRbSnapshotLogFighterLoadVerifyDiag(u32 tick, u32 live_f, u32 slot_f, u32 live_a, u32 slot_a)
{
	GObj *fighter_gobj;

	if (syNetRbSnapFighterDiagEnabled() == FALSE)
	{
		return;
	}
	port_log(
	    "SSB64 NetRbSnapshot: fighter_load_verify tick=%u figh_live=0x%08X figh_slot=0x%08X anim_live=0x%08X anim_slot=0x%08X\n",
	    tick,
	    live_f,
	    slot_f,
	    live_a,
	    slot_a);
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if (fp == NULL)
		{
			continue;
		}
		port_log(
		    "SSB64 NetRbSnapshot: fighter_slot tick=%u player=%d fkind=%d status=%d motion=%d shield=%d fhash_light=0x%08X\n",
		    tick,
		    (int)fp->player,
		    (int)fp->fkind,
		    (int)fp->status_id,
		    (int)fp->motion_id,
		    (int)fp->shield_health,
		    syNetSyncHashFighterStructLight(fp));
	}
}

static SYNetRbSnapshotSlot *syNetRbSnapshotSlotForTick(u32 tick);
static u32 syNetRbSnapF32DiagBits(f32 value);

static u32 syNetRbSnapF32DiagBits(f32 value)
{
	union
	{
		f32 fv;
		u32 uv;
	} reinterpret;

	reinterpret.fv = value;
	return reinterpret.uv;
}

static sb32 syNetRbSnapFighterFieldDiffEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_SNAPSHOT_FIGHTER_FIELD_DIFF");
	s_env_cache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}

static void syNetRbSnapLogFieldDiffScalar(const char *tag, u32 tick, s32 player, const char *field, u32 live_bits,
                                          u32 blob_bits)
{
	if (live_bits != blob_bits)
	{
		port_log("SSB64 NetRbSnapshot: fighter_field_diff tag=%s tick=%u player=%d field=%s live=0x%08X blob=0x%08X\n",
		         tag, tick, (int)player, field, live_bits, blob_bits);
	}
}

void syNetRbSnapshotLogFighterFieldDiffAtTick(u32 tick, const char *tag)
{
	SYNetRbSnapshotSlot *slot;
	GObj *fighter_gobj;
	const char *reason;

	if (syNetRbSnapFighterFieldDiffEnabled() == FALSE)
	{
		return;
	}
	reason = (tag != NULL) ? tag : "unknown";
	slot = syNetRbSnapshotSlotForTick(tick);
	if ((slot == NULL) || (slot->is_valid == FALSE) || (slot->tick != tick))
	{
		return;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		const SYNetRbSnapFighterBlob *blob;
		s32 slot_index;
		s32 ji;
		DObj *dobj;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			continue;
		}
		slot_index = fp->player;
		if ((slot_index < 0) || (slot_index >= GMCOMMON_PLAYERS_MAX) ||
		    (slot->fighters[slot_index].is_valid == FALSE))
		{
			continue;
		}
		blob = &slot->fighters[slot_index];
		{
			u32 live_light;
			u32 live_full;
			u32 live_anim;

			live_light = syNetSyncHashFighterStructLight(fp);
			live_full = syNetSyncHashFighterSlotFull(fp);
			live_anim = syNetSyncHashFighterSlotAnim(fp, fighter_gobj);
			port_log(
			    "SSB64 NetRbSnapshot: fighter_field_diff tag=%s tick=%u player=%d live_fhash_light=0x%08X "
			    "live_fhash_full=0x%08X live_anim_hash=0x%08X status=%d motion=%d\n",
			    reason, tick, (int)slot_index, live_light, live_full, live_anim, (int)fp->status_id,
			    (int)fp->motion_id);
		}
		syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "status_id", (u32)fp->status_id,
		                              (u32)blob->status_id);
		syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "motion_id", (u32)fp->motion_id,
		                              (u32)blob->motion_id);
		syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "motion_attack_id", (u32)fp->motion_attack_id,
		                              (u32)blob->motion_attack_id);
		syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "hitstatus", (u32)fp->hitstatus,
		                              (u32)blob->hitstatus);
		syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "camera_mode", (u32)fp->camera_mode,
		                              (u32)blob->camera_mode);
		syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "shield_health", (u32)fp->shield_health,
		                              (u32)blob->shield_health);
		syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "jumps_used", (u32)fp->jumps_used,
		                              (u32)blob->jumps_used);
		syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "is_hitstun", (u32)(fp->is_hitstun != FALSE),
		                              (u32)(blob->is_hitstun != FALSE));
		syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "is_shield", (u32)(fp->is_shield != FALSE),
		                              (u32)(blob->is_shield != FALSE));
		syNetRbSnapLogFieldDiffScalar(
		    reason, tick, slot_index, "vel_jostle_x",
		    syNetRbSnapF32DiagBits(fp->physics.vel_jostle_x), syNetRbSnapF32DiagBits(blob->physics.vel_jostle_x));
		syNetRbSnapLogFieldDiffScalar(
		    reason, tick, slot_index, "vel_jostle_z",
		    syNetRbSnapF32DiagBits(fp->physics.vel_jostle_z), syNetRbSnapF32DiagBits(blob->physics.vel_jostle_z));
		syNetRbSnapLogFieldDiffScalar(
		    reason, tick, slot_index, "vel_air_y", syNetRbSnapF32DiagBits(fp->physics.vel_air.y),
		    syNetRbSnapF32DiagBits(blob->physics.vel_air.y));
		syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "gobj_anim_frame",
		                              syNetRbSnapF32DiagBits(fighter_gobj->anim_frame),
		                              syNetRbSnapF32DiagBits(blob->gobj_anim_frame));
		if (fp->joints[nFTPartsJointTopN] != NULL)
		{
			syNetRbSnapLogFieldDiffScalar(
			    reason, tick, slot_index, "top_joint_y",
			    syNetRbSnapF32DiagBits(fp->joints[nFTPartsJointTopN]->translate.vec.f.y),
			    syNetRbSnapF32DiagBits(blob->joint_translate[nFTPartsJointTopN].y));
		}
		syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "coll_pos_prev_y",
		                              syNetRbSnapF32DiagBits(fp->coll_data.pos_prev.y),
		                              syNetRbSnapF32DiagBits(blob->coll.pos_prev.y));
		for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
		{
			dobj = fp->joints[ji];
			if (dobj == NULL)
			{
				continue;
			}
			if (blob->joint_anim[ji].aobj_chain_total > blob->joint_anim[ji].aobj_count)
			{
				port_log(
				    "SSB64 NetRbSnapshot: fighter_field_diff tag=%s tick=%u player=%d field=joint%u_aobj_trunc "
				    "stored=%u total=%u\n",
				    reason, tick, (int)slot_index, (unsigned int)ji,
				    (unsigned int)blob->joint_anim[ji].aobj_count,
				    (unsigned int)blob->joint_anim[ji].aobj_chain_total);
			}
			syNetRbSnapLogFieldDiffScalar(
			    reason, tick, slot_index, "joint_anim_frame",
			    syNetRbSnapF32DiagBits(dobj->anim_frame), syNetRbSnapF32DiagBits(blob->joint_anim[ji].anim_frame));
		}
	}
}

void syNetRbSnapshotLogFighterFieldDiffOnLoadDrift(u32 tick)
{
	syNetRbSnapshotLogFighterFieldDiffAtTick(tick, "load_drift");
}
#endif /* PORT */

static void syNetRbSnapCaptureYakuDObj(SYNetRbSnapYakuBlob *yaku, DObj *dobj, const Vec3f *speed)
{
	if ((yaku == NULL) || (dobj == NULL))
	{
		return;
	}
	yaku->translate = dobj->translate.vec.f;
	if (speed != NULL)
	{
		yaku->speed = *speed;
	}
	else
	{
		yaku->speed.x = yaku->speed.y = yaku->speed.z = 0.0F;
	}
	yaku->user_data_s = dobj->user_data.s;
	yaku->flags = dobj->flags;
	yaku->anim_joint_event32 = (dobj->anim_joint.event32 != NULL) ? (uintptr_t)dobj->anim_joint.event32 : 0U;
	syNetRbSnapCaptureDObjAnim(&yaku->anim, dobj);
}

static void syNetRbSnapApplyYakuDObj(DObj *dobj, const SYNetRbSnapYakuBlob *yaku, Vec3f *speed_out)
{
	if ((dobj == NULL) || (yaku == NULL))
	{
		return;
	}
	syNetRbSnapApplyDObjAnim(dobj, &yaku->anim);
	if (yaku->anim_joint_event32 != 0U)
	{
		dobj->anim_joint.event32 = (AObjEvent32 *)yaku->anim_joint_event32;
	}
	else
	{
		dobj->anim_joint.event32 = NULL;
	}
	dobj->flags = yaku->flags;
	if (dobj->parent_gobj != NULL)
	{
		dobj->parent_gobj->anim_frame = dobj->anim_frame;
	}
	dobj->translate.vec.f = yaku->translate;
	dobj->user_data.s = yaku->user_data_s;
	if (speed_out != NULL)
	{
		*speed_out = yaku->speed;
	}
}

static void syNetRbSnapCaptureMap(SYNetRbSnapshotSlot *slot)
{
	s32 i;
	s32 n;
	s32 cap;
	DObj *dobj;

	slot->mp_yaku_captured = FALSE;
	slot->mp_collision_tic = 0;
	slot->mp_yakumono_count = 0;
	if ((gMPCollisionYakumonoDObjs == NULL) || (gMPCollisionSpeeds == NULL))
	{
		return;
	}
	n = gMPCollisionYakumonosNum;
	if (n < 0)
	{
		n = 0;
	}
	cap = (n > SYNETRB_SNAPSHOT_MAX_YAKU) ? SYNETRB_SNAPSHOT_MAX_YAKU : n;
	slot->mp_collision_tic = gMPCollisionUpdateTic;
	slot->mp_yakumono_count = cap;
	for (i = 0; i < cap; i++)
	{
		dobj = gMPCollisionYakumonoDObjs->dobjs[i];
		if (dobj == NULL)
		{
			memset(&slot->mp_yaku[i], 0, sizeof(slot->mp_yaku[i]));
			continue;
		}
		syNetRbSnapCaptureYakuDObj(&slot->mp_yaku[i], dobj, &gMPCollisionSpeeds[i]);
	}
	slot->mp_yaku_captured = TRUE;
	slot->mp_bounds = gMPCollisionBounds;
	slot->mp_bounds_captured = TRUE;
}

static void syNetRbSnapApplyMap(const SYNetRbSnapshotSlot *slot)
{
	s32 i;
	s32 cap;
	s32 live_n;
	DObj *dobj;

	if (slot->mp_yaku_captured == FALSE)
	{
		return;
	}
	gMPCollisionUpdateTic = slot->mp_collision_tic;
	if ((gMPCollisionYakumonoDObjs == NULL) || (gMPCollisionSpeeds == NULL))
	{
		return;
	}
	live_n = gMPCollisionYakumonosNum;
	if (live_n < 0)
	{
		live_n = 0;
	}
	cap = slot->mp_yakumono_count;
	if (cap > live_n)
	{
		cap = live_n;
	}
	if (cap > SYNETRB_SNAPSHOT_MAX_YAKU)
	{
		cap = SYNETRB_SNAPSHOT_MAX_YAKU;
	}
	for (i = 0; i < cap; i++)
	{
		dobj = gMPCollisionYakumonoDObjs->dobjs[i];
		if (dobj == NULL)
		{
			continue;
		}
		syNetRbSnapApplyYakuDObj(dobj, &slot->mp_yaku[i], &gMPCollisionSpeeds[i]);
	}
	if (slot->mp_bounds_captured != FALSE)
	{
		gMPCollisionBounds = slot->mp_bounds;
	}
}

static void syNetRbSnapCaptureWorld(SYNetRbSnapWorldBlob *world)
{
	s32 i;
	s32 n;

	if (gSCManagerBattleState != NULL)
	{
		world->battle = *gSCManagerBattleState;
	}
	else
	{
		memset(&world->battle, 0, sizeof(world->battle));
	}
	world->rng_seed = syUtilsRandSeed();
	world->item_spawn_wait = gITManagerAppearActor.spawn_wait;
	world->item_weights_sum = gITManagerAppearActor.weights.weights_sum;
	world->item_weights_valids_num = gITManagerAppearActor.weights.valids_num;
	world->item_mapobjs_num = gITManagerAppearActor.mapobjs_num;
	world->item_random_weights_sum = gITManagerRandomWeights.weights_sum;
	world->item_random_weights_valids_num = gITManagerRandomWeights.valids_num;
	n = world->item_mapobjs_num;
	if (n > SYNETRB_SNAPSHOT_MAX_MAPOBJS)
	{
		n = SYNETRB_SNAPSHOT_MAX_MAPOBJS;
	}
	if ((gITManagerAppearActor.mapobjs != NULL) && (n > 0))
	{
		for (i = 0; i < n; i++)
		{
			world->item_mapobjs[i] = gITManagerAppearActor.mapobjs[i];
		}
	}
	n = world->item_random_weights_valids_num;
	if (n > nITKindEnumCount)
	{
		n = nITKindEnumCount;
	}
	if ((gITManagerRandomWeights.kinds != NULL) && (gITManagerRandomWeights.blocks != NULL) && (n > 0))
	{
		for (i = 0; i < n; i++)
		{
			world->item_random_weight_kinds[i] = gITManagerRandomWeights.kinds[i];
			world->item_random_weight_blocks[i] = gITManagerRandomWeights.blocks[i];
		}
	}
}

static void syNetRbSnapApplyWorld(const SYNetRbSnapWorldBlob *world, u32 tick)
{
	s32 i;
	s32 n;

	if (gSCManagerBattleState != NULL)
	{
		GObj *fighter_gobj;
		s32 pi;

		*gSCManagerBattleState = world->battle;
		for (pi = 0; pi < GMCOMMON_PLAYERS_MAX; pi++)
		{
			gSCManagerBattleState->players[pi].fighter_gobj = NULL;
		}
		for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
		     fighter_gobj = fighter_gobj->link_next)
		{
			FTStruct *fp = ftGetStruct(fighter_gobj);

			if (fp == NULL)
			{
				syNetRbSnapLogSkippedGObj("apply_world", "fighter", fighter_gobj, tick);
				continue;
			}
			if ((fp->player >= 0) && (fp->player < GMCOMMON_PLAYERS_MAX))
			{
				gSCManagerBattleState->players[fp->player].fighter_gobj = fighter_gobj;
			}
		}
	}
	syUtilsSetRandomSeed(world->rng_seed);
	syUtilsResetCosmeticRandomSeed(world->rng_seed);
	gITManagerAppearActor.spawn_wait = world->item_spawn_wait;
	gITManagerAppearActor.weights.weights_sum = world->item_weights_sum;
	gITManagerAppearActor.weights.valids_num = world->item_weights_valids_num;
	gITManagerAppearActor.mapobjs_num = world->item_mapobjs_num;
	gITManagerRandomWeights.weights_sum = world->item_random_weights_sum;
	gITManagerRandomWeights.valids_num = world->item_random_weights_valids_num;
	n = world->item_mapobjs_num;
	if (n > SYNETRB_SNAPSHOT_MAX_MAPOBJS)
	{
		n = SYNETRB_SNAPSHOT_MAX_MAPOBJS;
	}
	if ((gITManagerAppearActor.mapobjs != NULL) && (n > 0))
	{
		for (i = 0; i < n; i++)
		{
			gITManagerAppearActor.mapobjs[i] = world->item_mapobjs[i];
		}
	}
	n = world->item_random_weights_valids_num;
	if (n > nITKindEnumCount)
	{
		n = nITKindEnumCount;
	}
	if ((gITManagerRandomWeights.kinds != NULL) && (gITManagerRandomWeights.blocks != NULL) && (n > 0))
	{
		for (i = 0; i < n; i++)
		{
			gITManagerRandomWeights.kinds[i] = world->item_random_weight_kinds[i];
			gITManagerRandomWeights.blocks[i] = world->item_random_weight_blocks[i];
		}
	}
#ifdef PORT
	syNetSyncReconcileBattleTimePassedForSimTick(tick);
#endif
}

s32 syNetRbEnumerateActiveItemsSorted(GObj **out, s32 max, sb32 *truncated_out)
{
	GObj *gobj;
	s32 count;
	s32 i;
	s32 j;
	sb32 saw_extra;

	if (truncated_out != NULL)
	{
		*truncated_out = FALSE;
	}
	if ((out == NULL) || (max <= 0))
	{
		return 0;
	}
	count = 0;
	saw_extra = FALSE;
	for (gobj = gGCCommonLinks[nGCCommonLinkIDItem]; gobj != NULL; gobj = gobj->link_next)
	{
		if (itGetStruct(gobj) == NULL)
		{
			continue;
		}
		if (count < max)
		{
			out[count++] = gobj;
		}
		else
		{
			saw_extra = TRUE;
		}
	}
	for (i = 1; i < count; i++)
	{
		GObj *key_gobj;
		u32 key_id;

		key_gobj = out[i];
		key_id = key_gobj->id;
		j = i;
		while ((j > 0) && (out[j - 1]->id > key_id))
		{
			out[j] = out[j - 1];
			j--;
		}
		out[j] = key_gobj;
	}
	if ((truncated_out != NULL) && (saw_extra != FALSE))
	{
		*truncated_out = TRUE;
	}
	return count;
}

s32 syNetRbEnumerateActiveWeaponsSorted(GObj **out, s32 max, sb32 *truncated_out)
{
	GObj *gobj;
	s32 count;
	s32 i;
	s32 j;
	sb32 saw_extra;

	if (truncated_out != NULL)
	{
		*truncated_out = FALSE;
	}
	if ((out == NULL) || (max <= 0))
	{
		return 0;
	}
	count = 0;
	saw_extra = FALSE;
	for (gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; gobj != NULL; gobj = gobj->link_next)
	{
		if (wpGetStruct(gobj) == NULL)
		{
			continue;
		}
		if (count < max)
		{
			out[count++] = gobj;
		}
		else
		{
			saw_extra = TRUE;
		}
	}
	for (i = 1; i < count; i++)
	{
		GObj *key_gobj;
		WPStruct *key_wp;
		u32 key_id;

		key_gobj = out[i];
		key_wp = wpGetStruct(key_gobj);
		key_id = (key_wp != NULL) && (key_wp->instance_id != 0U) ? key_wp->instance_id : (u32)key_gobj->id;
		j = i;
		while (j > 0)
		{
			WPStruct *prev_wp;
			u32 prev_id;

			prev_wp = wpGetStruct(out[j - 1]);
			prev_id = (prev_wp != NULL) && (prev_wp->instance_id != 0U) ? prev_wp->instance_id :
			                                                              (u32)out[j - 1]->id;
			if (prev_id <= key_id)
			{
				break;
			}
			out[j] = out[j - 1];
			j--;
		}
		out[j] = key_gobj;
	}
	if ((truncated_out != NULL) && (saw_extra != FALSE))
	{
		*truncated_out = TRUE;
	}
	return count;
}

#ifdef PORT

s32 syNetRbEnumerateActiveEffectsSorted(GObj **out, s32 max, sb32 *truncated_out)
{
	s32 link_pass;
	GObj *gobj;
	s32 count;
	s32 i;
	s32 j;
	sb32 saw_extra;

	if (truncated_out != NULL)
	{
		*truncated_out = FALSE;
	}
	if ((out == NULL) || (max <= 0))
	{
		return 0;
	}
	count = 0;
	saw_extra = FALSE;
	for (link_pass = 0; link_pass < 2; link_pass++)
	{
		GObj *link_head;

		link_head =
		    gGCCommonLinks[(link_pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		for (gobj = link_head; gobj != NULL; gobj = gobj->link_next)
		{
			if (efGetStruct(gobj) != NULL)
			{
				if (count < max)
				{
					out[count++] = gobj;
				}
				else
				{
					saw_extra = TRUE;
				}
				continue;
			}
			/* Particle-shell effects (e.g. dust-heavy) have no EFStruct. */
			if ((gobj->user_data.p == NULL) && (gobj->obj_kind == nGCCommonKindEffect))
			{
				if (count < max)
				{
					out[count++] = gobj;
				}
				else
				{
					saw_extra = TRUE;
				}
			}
		}
	}
	for (i = 1; i < count; i++)
	{
		GObj *key_gobj;
		u32 key_id;

		key_gobj = out[i];
		key_id = key_gobj->id;
		j = i;
		while ((j > 0) && (out[j - 1]->id > key_id))
		{
			out[j] = out[j - 1];
			j--;
		}
		out[j] = key_gobj;
	}
	if ((truncated_out != NULL) && (saw_extra != FALSE))
	{
		*truncated_out = TRUE;
	}
	return count;
}

static sb32 syNetRbSnapSnapshotItemDiagEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_SNAPSHOT_ITEM_DIAG");
	s_env_cache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}
#endif

static void syNetRbSnapCaptureItemBlobMeta(SYNetRbSnapItemBlob *blob, const ITStruct *ip);
static void syNetRbSnapApplyItemBlobMeta(ITStruct *ip, const SYNetRbSnapItemBlob *blob);

static sb32 syNetRbSnapCaptureItems(SYNetRbSnapshotSlot *slot)
{
	GObj *sorted[SYNETRB_SNAPSHOT_MAX_ITEMS];
	GObj *gobj;
	s32 count;
	s32 i;
	sb32 truncated;

	truncated = FALSE;
	count = syNetRbEnumerateActiveItemsSorted(sorted, SYNETRB_SNAPSHOT_MAX_ITEMS, &truncated);
	for (i = 0; i < count; i++)
	{
		ITStruct *ip;
		SYNetRbSnapItemBlob *blob;
		DObj *dobj;

		gobj = sorted[i];
		ip = itGetStruct(gobj);
		if (ip == NULL)
		{
			syNetRbSnapLogSkippedGObj("save", "item", gobj, slot->tick);
			continue;
		}
		blob = &slot->items[i];
		memset(blob, 0, sizeof(*blob));
		blob->is_valid = TRUE;
		blob->gobj_id = gobj->id;
		blob->kind = ip->kind;
		blob->type = ip->type;
		blob->team = ip->team;
		blob->player = ip->player;
		blob->player_num = ip->player_num;
		blob->percent_damage = ip->percent_damage;
		blob->hitlag_tics = ip->hitlag_tics;
		blob->lr = ip->lr;
		blob->physics = ip->physics;
		syNetRbSnapCaptureMPColl(&blob->coll, &ip->coll_data);
		blob->ga = ip->ga;
		blob->attack_coll = ip->attack_coll;
		blob->damage_coll = ip->damage_coll;
		blob->lifetime = ip->lifetime;
		blob->owner_gobj_id = syNetRbSnapGobjId(ip->owner_gobj);
		blob->reflect_gobj_id = syNetRbSnapGobjId(ip->reflect_gobj);
		blob->damage_gobj_id = syNetRbSnapGobjId(ip->damage_gobj);
		blob->arrow_gobj_id = syNetRbSnapGobjId(ip->arrow_gobj);
		blob->multi = ip->multi;
		blob->event_id = ip->event_id;
		blob->spin_step = ip->spin_step;
		blob->translate.x = blob->translate.y = blob->translate.z = 0.0F;
		dobj = DObjGetStruct(gobj);
		if (dobj != NULL)
		{
			blob->translate = dobj->translate.vec.f;
		}
		blob->item_flags = 0;
		if (ip->is_hold != FALSE)
		{
			blob->item_flags |= 0x01U;
		}
		if (ip->is_allow_pickup != FALSE)
		{
			blob->item_flags |= 0x02U;
		}
		if (ip->is_thrown != FALSE)
		{
			blob->item_flags |= 0x04U;
		}
		memcpy(blob->item_vars, &ip->item_vars, sizeof(blob->item_vars));
#ifdef PORT
		syNetRbSnapCaptureItemBlobMeta(blob, ip);
#endif
	}
	slot->item_count = count;
#ifdef PORT
	if (syNetRbSnapSnapshotItemDiagEnabled() != FALSE)
	{
		port_log("SSB64 NetRbSnapshot: item save tick=%u item_count=%d truncated=%d\n",
		         (unsigned int)slot->tick,
		         (int)slot->item_count,
		         (int)truncated);
	}
#endif
	if (truncated != FALSE)
	{
		port_log("SSB64 NetRbSnapshot: item cap overflow (max=%d) tick=%u — save failed\n",
		         SYNETRB_SNAPSHOT_MAX_ITEMS,
		         (unsigned int)slot->tick);
		return FALSE;
	}
	return TRUE;
}

static void syNetRbSnapCaptureItemBlobMeta(SYNetRbSnapItemBlob *blob, const ITStruct *ip)
{
	s32 i;

	if ((blob == NULL) || (ip == NULL))
	{
		return;
	}
	for (i = 0; i < GMATTACKREC_NUM_MAX; i++)
	{
		blob->attack_record_victim_gobj_id[i] = syNetRbSnapGobjId(ip->attack_coll.attack_records[i].victim_gobj);
		blob->attack_coll.attack_records[i].victim_gobj = NULL;
	}
}

static void syNetRbSnapApplyItemBlobMeta(ITStruct *ip, const SYNetRbSnapItemBlob *blob)
{
	s32 i;

	if ((ip == NULL) || (blob == NULL))
	{
		return;
	}
	for (i = 0; i < GMATTACKREC_NUM_MAX; i++)
	{
		ip->attack_coll.attack_records[i].victim_gobj =
		    syNetRbSnapResolveLiveGobj(blob->attack_record_victim_gobj_id[i]);
	}
}

static void syNetRbSnapApplyItemBlobToGObj(GObj *gobj, const SYNetRbSnapItemBlob *blob)
{
	ITStruct *ip;
	DObj *dobj;
	Vec3f *topn;

	ip = itGetStruct(gobj);
	if ((ip == NULL) || (blob == NULL))
	{
		return;
	}
	ip->kind = blob->kind;
	ip->type = blob->type;
	ip->team = blob->team;
	ip->player = blob->player;
	ip->player_num = blob->player_num;
	ip->percent_damage = blob->percent_damage;
	ip->hitlag_tics = blob->hitlag_tics;
	ip->lr = blob->lr;
	ip->physics = blob->physics;
	topn = NULL;
	dobj = DObjGetStruct(gobj);
	if (dobj != NULL)
	{
		topn = &dobj->translate.vec.f;
	}
	syNetRbSnapApplyMPColl(&ip->coll_data, &blob->coll, topn, &ip->lr);
	ip->ga = blob->ga;
	ip->attack_coll = blob->attack_coll;
	syNetRbSnapApplyItemBlobMeta(ip, blob);
	ip->damage_coll = blob->damage_coll;
	ip->lifetime = blob->lifetime;
	ip->owner_gobj = syNetRbSnapResolveLiveGobj(blob->owner_gobj_id);
	ip->reflect_gobj = syNetRbSnapResolveLiveGobj(blob->reflect_gobj_id);
	ip->damage_gobj = syNetRbSnapResolveLiveGobj(blob->damage_gobj_id);
	ip->arrow_gobj = syNetRbSnapResolveArrowGobjForItem(blob->arrow_gobj_id, gobj, ip);
	ip->multi = blob->multi;
	ip->event_id = blob->event_id;
	ip->spin_step = blob->spin_step;
	ip->is_hold = ((blob->item_flags & 0x01U) != 0U) ? TRUE : FALSE;
	ip->is_allow_pickup = ((blob->item_flags & 0x02U) != 0U) ? TRUE : FALSE;
	ip->is_thrown = ((blob->item_flags & 0x04U) != 0U) ? TRUE : FALSE;
	if (dobj != NULL)
	{
		dobj->translate.vec.f = blob->translate;
	}
	memcpy(&ip->item_vars, blob->item_vars, sizeof(ip->item_vars));
}

static s32 syNetRbSnapFindItemBlobByGobjId(const SYNetRbSnapshotSlot *slot, sb32 *matched, u32 gobj_id)
{
	s32 si;

	for (si = 0; si < slot->item_count; si++)
	{
		if ((matched[si] == FALSE) && (slot->items[si].is_valid != FALSE) && (slot->items[si].gobj_id == gobj_id))
		{
			return si;
		}
	}
	return -1;
}

static s32 syNetRbSnapFindItemBlobByKindPos(const SYNetRbSnapshotSlot *slot, sb32 *matched, s32 kind, const Vec3f *pos)
{
	s32 si;
	s32 best = -1;
	f32 best_dist_sq = F32_MAX;

	if (pos == NULL)
	{
		return -1;
	}
	for (si = 0; si < slot->item_count; si++)
	{
		const SYNetRbSnapItemBlob *blob;
		f32 dx;
		f32 dy;
		f32 dz;
		f32 dist_sq;

		if ((matched[si] != FALSE) || (slot->items[si].is_valid == FALSE) || (slot->items[si].kind != kind))
		{
			continue;
		}
		blob = &slot->items[si];
		dx = blob->translate.x - pos->x;
		dy = blob->translate.y - pos->y;
		dz = blob->translate.z - pos->z;
		dist_sq = (dx * dx) + (dy * dy) + (dz * dz);
		if (dist_sq < best_dist_sq)
		{
			best_dist_sq = dist_sq;
			best = si;
		}
	}
	return best;
}

#ifdef PORT
static void syNetRbSnapReconcileOrphanHeldItems(const SYNetRbSnapshotSlot *slot)
{
	GObj *fighter_gobj;
	GObj *item_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		s32 slot_index;
		const SYNetRbSnapFighterBlob *blob;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			continue;
		}
		slot_index = fp->player;
		if ((slot_index < 0) || (slot_index >= GMCOMMON_PLAYERS_MAX))
		{
			continue;
		}
		blob = &slot->fighters[slot_index];
		if (blob->is_valid == FALSE)
		{
			continue;
		}
		fp->item_gobj = syNetRbSnapResolveItemGobj(blob->item_gobj_id);
	}

	for (item_gobj = gGCCommonLinks[nGCCommonLinkIDItem]; item_gobj != NULL; item_gobj = item_gobj->link_next)
	{
		ITStruct *ip;
		FTStruct *fp;

		ip = itGetStruct(item_gobj);
		if (ip == NULL)
		{
			continue;
		}
		if ((ip->is_hold != FALSE) && (ip->owner_gobj != NULL))
		{
			fp = ftGetStruct(ip->owner_gobj);
			if (fp != NULL)
			{
				fp->item_gobj = item_gobj;
			}
		}
		else if (itMainItemHasOrphanHoldDisplay(item_gobj) != FALSE)
		{
			if (syNetRbSnapSnapshotItemDiagEnabled() != FALSE)
			{
				port_log("SSB64 NetRbSnapshot: reconciled orphan hold item=%u owner=%p tick=%u\n",
				         (unsigned int)item_gobj->id,
				         (void *)ip->owner_gobj,
				         (unsigned int)slot->tick);
			}
			itMainDetachOrphanHoldDisplay(item_gobj);
		}
	}
	syNetRbSnapRebindFighterItemHoldCoupling();
}
#endif

#ifdef PORT
static void syNetRbSnapReapplyLinkBombStatusAfterBlob(GObj *item_gobj, const SYNetRbSnapItemBlob *blob)
{
	ITStruct *ip;
	sb32 was_thrown;

	ip = itGetStruct(item_gobj);
	if ((ip == NULL) || (blob == NULL))
	{
		return;
	}
	was_thrown = ((blob->item_flags & 0x04U) != 0U) ? TRUE : FALSE;

	if (ip->is_hold != FALSE)
	{
		itLinkBombHoldSetStatus(item_gobj);
		return;
	}
	if (was_thrown != FALSE)
	{
		if (ip->item_vars.linkbomb.drop_update_wait > 0)
		{
			itLinkBombDroppedSetStatus(item_gobj);
		}
		else
		{
			itLinkBombThrownSetStatus(item_gobj);
		}
		ip->is_thrown = TRUE;
		return;
	}
	if (ip->ga == nMPKineticsGround)
	{
		itLinkBombWaitSetStatus(item_gobj);
	}
	else
	{
		itLinkBombFallSetStatus(item_gobj);
	}
}

static GObj *syNetRbSnapRespawnLinkBombFromBlob(const SYNetRbSnapItemBlob *blob, u32 tick, Vec3f *pos, Vec3f *vel)
{
	GObj *fighter_gobj;
	GObj *item_gobj;
	ITStruct *ip;
	DObj *dobj;

	fighter_gobj = syNetRbSnapResolveLiveGobj(blob->owner_gobj_id);
	if (fighter_gobj == NULL)
	{
		port_log("SSB64 NetRbSnapshot: item respawn failed kind=%d tick=%u gobj_id=%u owner_id=%u (no owner)\n",
		         (int)nITKindLinkBomb,
		         (unsigned int)tick,
		         (unsigned int)blob->gobj_id,
		         (unsigned int)blob->owner_gobj_id);
		return NULL;
	}
	item_gobj = itManagerMakeItem(fighter_gobj, &dItLinkBombItemDesc, pos, vel, ITEM_FLAG_PARENT_FIGHTER);
	if (item_gobj == NULL)
	{
		return NULL;
	}
	ip = itGetStruct(item_gobj);
	dobj = DObjGetStruct(item_gobj);
	if ((dobj != NULL) && (dobj->child != NULL))
	{
		gcAddXObjForDObjFixed(dobj, 0x2E, 0);
		gcAddXObjForDObjFixed(dobj->child, 0x2E, 0);
	}
	if (ip != NULL)
	{
		ip->multi = 0;
		ip->lifetime = ITLINKBOMB_LIFETIME;
		ip->item_vars.linkbomb.scale_id = 0;
		ip->item_vars.linkbomb.scale_int = ITLINKBOMB_SCALE_INT;
		ip->attack_coll.can_rehit_shield = TRUE;
		ip->physics.vel_air.x = ip->physics.vel_air.y = ip->physics.vel_air.z = 0.0F;
	}
	itLinkBombWaitSetStatus(item_gobj);
	return item_gobj;
}

static GObj *syNetRbSnapRespawnItemFromBlob(const SYNetRbSnapshotSlot *slot, const SYNetRbSnapItemBlob *blob)
{
	Vec3f pos;
	Vec3f vel;
	GObj *spawned;

	if ((slot == NULL) || (blob == NULL))
	{
		return NULL;
	}
	pos = blob->translate;
	vel.x = vel.y = vel.z = 0.0F;

	if (blob->kind == nITKindLinkBomb)
	{
		spawned = syNetRbSnapRespawnLinkBombFromBlob(blob, slot->tick, &pos, &vel);
		if (spawned == NULL)
		{
			port_log("SSB64 NetRbSnapshot: item respawn failed kind=%d tick=%u gobj_id=%u\n",
			         (int)blob->kind,
			         (unsigned int)slot->tick,
			         (unsigned int)blob->gobj_id);
			return NULL;
		}
		syNetRbSnapApplyItemBlobToGObj(spawned, blob);
		syNetRbSnapReapplyLinkBombStatusAfterBlob(spawned, blob);
		return spawned;
	}
	if ((blob->kind >= nITKindFighterStart) && (blob->kind <= nITKindFighterEnd))
	{
		port_log("SSB64 NetRbSnapshot: item respawn unsupported kind=%d tick=%u gobj_id=%u\n",
		         (int)blob->kind,
		         (unsigned int)slot->tick,
		         (unsigned int)blob->gobj_id);
		return NULL;
	}
	spawned = itManagerMakeItemSetupCommon(NULL, blob->kind, &pos, &vel, ITEM_FLAG_PARENT_DEFAULT);
	if (spawned == NULL)
	{
		port_log("SSB64 NetRbSnapshot: item respawn failed kind=%d tick=%u gobj_id=%u\n",
		         (int)blob->kind,
		         (unsigned int)slot->tick,
		         (unsigned int)blob->gobj_id);
		return NULL;
	}
	syNetRbSnapApplyItemBlobToGObj(spawned, blob);
	return spawned;
}
#endif /* PORT */

static void syNetRbSnapApplyItems(const SYNetRbSnapshotSlot *slot)
{
	GObj *gobj;
	s32 si;
	sb32 matched[SYNETRB_SNAPSHOT_MAX_ITEMS];
#ifdef PORT
	s32 ejected_count;
	s32 matched_count;
	s32 respawned_count;
#endif

	memset(matched, 0, sizeof(matched));
#ifdef PORT
	ejected_count = 0;
	matched_count = 0;
	respawned_count = 0;
#endif
	for (gobj = gGCCommonLinks[nGCCommonLinkIDItem]; gobj != NULL;)
	{
		GObj *next_gobj;
		ITStruct *ip;
		s32 found;

		next_gobj = gobj->link_next;
		ip = itGetStruct(gobj);
		if (ip == NULL)
		{
			syNetRbSnapLogSkippedGObj("apply", "item", gobj, slot->tick);
			gobj = next_gobj;
			continue;
		}
		found = syNetRbSnapFindItemBlobByGobjId(slot, matched, gobj->id);
		if (found < 0)
		{
			DObj *dobj = DObjGetStruct(gobj);

			if (dobj != NULL)
			{
				found = syNetRbSnapFindItemBlobByKindPos(slot, matched, ip->kind, &dobj->translate.vec.f);
			}
		}
		if (found < 0)
		{
			gcEjectGObj(gobj);
#ifdef PORT
			ejected_count++;
#endif
			gobj = next_gobj;
			continue;
		}
		matched[found] = TRUE;
		syNetRbSnapApplyItemBlobToGObj(gobj, &slot->items[found]);
#ifdef PORT
		if (ip->kind == nITKindLinkBomb)
		{
			syNetRbSnapReapplyLinkBombStatusAfterBlob(gobj, &slot->items[found]);
		}
		matched_count++;
#endif
		gobj = next_gobj;
	}
	for (si = 0; si < slot->item_count; si++)
	{
		const SYNetRbSnapItemBlob *blob;
		GObj *spawned;

		if (matched[si] != FALSE)
		{
			continue;
		}
		blob = &slot->items[si];
		if (blob->is_valid == FALSE)
		{
			continue;
		}
#ifdef PORT
		spawned = syNetRbSnapRespawnItemFromBlob(slot, blob);
#else
		{
			Vec3f pos;
			Vec3f vel;

			pos = blob->translate;
			vel.x = vel.y = vel.z = 0.0F;
			spawned = itManagerMakeItemSetupCommon(NULL, blob->kind, &pos, &vel, ITEM_FLAG_PARENT_DEFAULT);
			if (spawned != NULL)
			{
				syNetRbSnapApplyItemBlobToGObj(spawned, blob);
			}
		}
#endif
		if (spawned == NULL)
		{
			continue;
		}
#ifdef PORT
		respawned_count++;
#endif
	}
#ifdef PORT
	if (syNetRbSnapSnapshotItemDiagEnabled() != FALSE)
	{
		port_log("SSB64 NetRbSnapshot: item apply tick=%u ejected=%d matched=%d respawned=%d blob_count=%d\n",
		         (unsigned int)slot->tick,
		         ejected_count,
		         matched_count,
		         respawned_count,
		         (int)slot->item_count);
	}
#endif
#ifdef PORT
	syNetRbSnapReconcileOrphanHeldItems(slot);
#endif
}

#ifdef PORT
static u8 syNetRbSnapWeaponSpawnProfile(const WPStruct *wp)
{
	if (wp == NULL)
	{
		return SYNETRB_WEAPON_SPAWN_DEFAULT;
	}
	if (wp->proc_update == wpNessPKReflectHeadProcUpdate)
	{
		return SYNETRB_WEAPON_SPAWN_PK_REFLECT_HEAD;
	}
	if (wp->proc_update == wpNessPKReflectTrailProcUpdate)
	{
		return SYNETRB_WEAPON_SPAWN_PK_REFLECT_TRAIL;
	}
	return SYNETRB_WEAPON_SPAWN_DEFAULT;
}

static f32 syNetRbSnapVec3DistSq(const Vec3f *a, const Vec3f *b)
{
	f32 dx;
	f32 dy;
	f32 dz;

	dx = a->x - b->x;
	dy = a->y - b->y;
	dz = a->z - b->z;
	return (dx * dx) + (dy * dy) + (dz * dz);
}

static u32 syNetRbSnapFindWeaponGobjIdByKindGroupPlayer(const SYNetRbSnapshotSlot *slot, s32 kind, u32 group_id,
                                                        u8 player, u32 skip_gobj_id)
{
	s32 si;

	if (slot == NULL)
	{
		return 0U;
	}
	for (si = 0; si < slot->weapon_count; si++)
	{
		const SYNetRbSnapWeaponBlob *other = &slot->weapons[si];

		if ((other->is_valid == FALSE) || (other->instance_id == skip_gobj_id))
		{
			continue;
		}
		if ((other->kind == kind) && (other->group_id == group_id) && (other->player == player))
		{
			return other->instance_id;
		}
	}
	return 0U;
}

static u32 syNetRbSnapFindThunderJoltAirParentGobjId(const SYNetRbSnapshotSlot *slot, const SYNetRbSnapWeaponBlob *blob)
{
	s32 si;
	u32 best_id;
	f32 best_dist;

	best_id = 0U;
	best_dist = 1.0e30F;
	for (si = 0; si < slot->weapon_count; si++)
	{
		const SYNetRbSnapWeaponBlob *other = &slot->weapons[si];
		f32 dist;

		if ((other->is_valid == FALSE) || (other->gobj_id == blob->gobj_id))
		{
			continue;
		}
		if ((other->kind != nWPKindThunderJoltAir) || (other->player != blob->player) ||
		    (other->owner_gobj_id != blob->owner_gobj_id))
		{
			continue;
		}
		dist = syNetRbSnapVec3DistSq(&blob->translate, &other->translate);
		if (dist < best_dist)
		{
			best_dist = dist;
			best_id = other->gobj_id;
		}
	}
	return best_id;
}

static u32 syNetRbSnapInferWeaponSpawnParentGobjId(const SYNetRbSnapshotSlot *slot, const SYNetRbSnapWeaponBlob *blob)
{
	switch (blob->kind)
	{
	case nWPKindThunderJoltGround:
		return syNetRbSnapFindThunderJoltAirParentGobjId(slot, blob);

	case nWPKindThunderTrail:
	case nWPKindPKThunderTrail:
		if (blob->var_head_gobj_id != 0U)
		{
			return blob->var_head_gobj_id;
		}
		if (blob->var_parent_gobj_id != 0U)
		{
			return blob->var_parent_gobj_id;
		}
		return syNetRbSnapFindWeaponGobjIdByKindGroupPlayer(slot, nWPKindThunderHead, blob->group_id, blob->player,
		                                                  blob->instance_id);

	case nWPKindPKThunderHead:
		if (blob->spawn_profile == SYNETRB_WEAPON_SPAWN_PK_REFLECT_HEAD)
		{
			u32 pk_head_id;

			pk_head_id = syNetRbSnapFindWeaponGobjIdByKindGroupPlayer(slot, nWPKindPKThunderHead, blob->group_id,
			                                                          blob->player, blob->instance_id);
			if (pk_head_id != 0U)
			{
				return pk_head_id;
			}
		}
		return blob->owner_gobj_id;

	default:
		return blob->owner_gobj_id;
	}
}

static void syNetRbSnapScrubWeaponVarsInBlob(SYNetRbSnapWeaponBlob *blob)
{
	union wpStatusVars *vars = (union wpStatusVars *)blob->weapon_vars;
	s32 i;

	switch (blob->kind)
	{
	case nWPKindChargeShot:
		vars->charge_shot.owner_gobj = NULL;
		break;

	case nWPKindBoomerang:
		vars->boomerang.parent_gobj = NULL;
		break;

	case nWPKindPKThunderHead:
		vars->pkthunder.parent_gobj = NULL;
		for (i = 0; i < WPPKTHUNDER_PARTS_COUNT; i++)
		{
			vars->pkthunder.trail_gobj[i] = NULL;
		}
		break;

	case nWPKindPKThunderTrail:
		vars->pkthunder_trail.parent_gobj = NULL;
		vars->pkthunder_trail.head_gobj = NULL;
		break;

	case nWPKindIwarkRock:
		vars->rock.owner_gobj = NULL;
		break;

	case nWPKindDogasSmog:
		vars->smog.attr = NULL;
		break;

	default:
		break;
	}
}

static void syNetRbSnapCaptureWeaponBlobMeta(SYNetRbSnapshotSlot *slot, SYNetRbSnapWeaponBlob *blob, GObj *gobj,
                                             const WPStruct *wp)
{
	s32 i;

	if ((blob == NULL) || (wp == NULL))
	{
		return;
	}
	blob->spawn_profile = syNetRbSnapWeaponSpawnProfile(wp);
	blob->spawn_parent_gobj_id = syNetRbSnapGobjId(wp->owner_gobj);
	blob->var_parent_gobj_id = 0U;
	blob->var_head_gobj_id = 0U;
	for (i = 0; i < WPPKTHUNDER_PARTS_COUNT; i++)
	{
		blob->var_trail_gobj_id[i] = 0U;
	}
	for (i = 0; i < GMATTACKREC_NUM_MAX; i++)
	{
		blob->attack_record_victim_gobj_id[i] = syNetRbSnapGobjId(wp->attack_coll.attack_records[i].victim_gobj);
		blob->attack_coll.attack_records[i].victim_gobj = NULL;
	}
	switch (blob->kind)
	{
	case nWPKindChargeShot:
		blob->var_parent_gobj_id = syNetRbSnapGobjId(wp->weapon_vars.charge_shot.owner_gobj);
		break;

	case nWPKindBoomerang:
		blob->var_parent_gobj_id = syNetRbSnapGobjId(wp->weapon_vars.boomerang.parent_gobj);
		break;

	case nWPKindPKThunderHead:
		blob->var_parent_gobj_id = syNetRbSnapGobjId(wp->weapon_vars.pkthunder.parent_gobj);
		for (i = 0; i < WPPKTHUNDER_PARTS_COUNT; i++)
		{
			blob->var_trail_gobj_id[i] = syNetRbSnapGobjId(wp->weapon_vars.pkthunder.trail_gobj[i]);
		}
		break;

	case nWPKindPKThunderTrail:
		blob->var_parent_gobj_id = syNetRbSnapGobjId(wp->weapon_vars.pkthunder_trail.parent_gobj);
		blob->var_head_gobj_id = syNetRbSnapGobjId(wp->weapon_vars.pkthunder_trail.head_gobj);
		break;

	case nWPKindIwarkRock:
		blob->var_parent_gobj_id = syNetRbSnapGobjId(wp->weapon_vars.rock.owner_gobj);
		break;

	default:
		break;
	}
	syNetRbSnapScrubWeaponVarsInBlob(blob);
}

static void syNetRbSnapFinalizeWeaponBlobParents(SYNetRbSnapshotSlot *slot)
{
	s32 si;

	if (slot == NULL)
	{
		return;
	}
	for (si = 0; si < slot->weapon_count; si++)
	{
		if (slot->weapons[si].is_valid != FALSE)
		{
			slot->weapons[si].spawn_parent_gobj_id =
			    syNetRbSnapInferWeaponSpawnParentGobjId(slot, &slot->weapons[si]);
		}
	}
}

static void syNetRbSnapApplyWeaponBlobMeta(WPStruct *wp, const SYNetRbSnapWeaponBlob *blob)
{
	s32 i;

	if ((wp == NULL) || (blob == NULL))
	{
		return;
	}
	for (i = 0; i < GMATTACKREC_NUM_MAX; i++)
	{
		wp->attack_coll.attack_records[i].victim_gobj =
		    syNetRbSnapResolveLiveGobj(blob->attack_record_victim_gobj_id[i]);
	}
	switch (blob->kind)
	{
	case nWPKindChargeShot:
		wp->weapon_vars.charge_shot.owner_gobj = wp->owner_gobj;
		if (wp->weapon_vars.charge_shot.owner_gobj == NULL)
		{
			wp->weapon_vars.charge_shot.owner_gobj = syNetRbSnapResolveLiveGobj(blob->var_parent_gobj_id);
		}
		break;

	case nWPKindBoomerang:
		wp->weapon_vars.boomerang.parent_gobj = wp->owner_gobj;
		if (wp->weapon_vars.boomerang.parent_gobj == NULL)
		{
			wp->weapon_vars.boomerang.parent_gobj = syNetRbSnapResolveLiveGobj(blob->var_parent_gobj_id);
		}
		break;

	case nWPKindPKThunderHead:
		wp->weapon_vars.pkthunder.parent_gobj = wp->owner_gobj;
		if (wp->weapon_vars.pkthunder.parent_gobj == NULL)
		{
			wp->weapon_vars.pkthunder.parent_gobj = syNetRbSnapResolveLiveGobj(blob->var_parent_gobj_id);
		}
		for (i = 0; i < WPPKTHUNDER_PARTS_COUNT; i++)
		{
			wp->weapon_vars.pkthunder.trail_gobj[i] = syNetRbSnapResolveLiveGobj(blob->var_trail_gobj_id[i]);
		}
		break;

	case nWPKindPKThunderTrail:
		wp->weapon_vars.pkthunder_trail.parent_gobj = syNetRbSnapResolveLiveGobj(blob->var_parent_gobj_id);
		wp->weapon_vars.pkthunder_trail.head_gobj = syNetRbSnapResolveLiveGobj(blob->var_head_gobj_id);
		if (wp->weapon_vars.pkthunder_trail.head_gobj == NULL)
		{
			wp->weapon_vars.pkthunder_trail.head_gobj = wp->weapon_vars.pkthunder_trail.parent_gobj;
		}
		break;

	case nWPKindIwarkRock:
		wp->weapon_vars.rock.owner_gobj = wp->owner_gobj;
		if (wp->weapon_vars.rock.owner_gobj == NULL)
		{
			wp->weapon_vars.rock.owner_gobj = syNetRbSnapResolveLiveGobj(blob->var_parent_gobj_id);
		}
		break;

	case nWPKindDogasSmog:
	{
		WPDesc *weapon_desc = &dITDogasWeaponSmogWeaponDesc;

		wp->weapon_vars.smog.attr =
		    (WPAttributes *)((uintptr_t)*weapon_desc->p_weapon + (intptr_t)weapon_desc->o_attributes);
		portFixupStructU16(wp->weapon_vars.smog.attr, 0x10, 6);
		break;
	}

	default:
		break;
	}
}

static GObj *syNetRbSnapResolveWeaponSpawnParent(const SYNetRbSnapWeaponBlob *blob)
{
	GObj *parent_gobj;

	if (blob == NULL)
	{
		return NULL;
	}
	parent_gobj = syNetRbSnapResolveWeaponByInstanceId(blob->spawn_parent_gobj_id);
	if (parent_gobj == NULL)
	{
		parent_gobj = syNetRbSnapResolveLiveGobj(blob->spawn_parent_gobj_id);
	}
	if (parent_gobj == NULL)
	{
		parent_gobj = syNetRbSnapResolveWeaponOwnerFromBlob(blob);
	}
	return parent_gobj;
}

static f32 syNetRbSnapPKFireAngleFromBlob(const SYNetRbSnapWeaponBlob *blob)
{
	if ((blob == NULL) || (blob->lr == 0))
	{
		return 0.0F;
	}
	return (blob->rotate.z / (f32)blob->lr) - F_CST_DTOR32(90.0F);
}
#endif /* PORT */

static sb32 syNetRbSnapCaptureWeapons(SYNetRbSnapshotSlot *slot)
{
	GObj *gobj;
	s32 count;
	sb32 truncated;

	count = 0;
	truncated = FALSE;
	for (gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; gobj != NULL; gobj = gobj->link_next)
	{
		WPStruct *wp;
		SYNetRbSnapWeaponBlob *blob;
		DObj *dobj;

		if (count >= SYNETRB_SNAPSHOT_MAX_WEAPONS)
		{
			truncated = TRUE;
			break;
		}
		wp = wpGetStruct(gobj);
		if (wp == NULL)
		{
			syNetRbSnapLogSkippedGObj("save", "weapon", gobj, slot->tick);
			continue;
		}
		blob = &slot->weapons[count];
		memset(blob, 0, sizeof(*blob));
		blob->is_valid = TRUE;
		blob->gobj_id = gobj->id;
		blob->instance_id = wp->instance_id;
		blob->kind = wp->kind;
		blob->team = wp->team;
		{
			s8 owner_player = syNetRbSnapWeaponOwnerPlayerFromWP(wp);

			if (owner_player >= 0)
			{
				blob->player = (u8)owner_player;
			}
			else
			{
				blob->player = wp->player;
			}
		}
		blob->player_num = wp->player_num;
		blob->lr = wp->lr;
		blob->physics = wp->physics;
		syNetRbSnapCaptureMPColl(&blob->coll, &wp->coll_data);
		blob->ga = wp->ga;
		blob->attack_coll = wp->attack_coll;
		blob->lifetime = wp->lifetime;
		blob->owner_gobj_id = syNetRbSnapGobjId(wp->owner_gobj);
		blob->reflect_gobj_id = syNetRbSnapGobjId(wp->reflect_gobj);
		blob->absorb_gobj_id = syNetRbSnapGobjId(wp->absorb_gobj);
		blob->group_id = wp->group_id;
		blob->translate.x = blob->translate.y = blob->translate.z = 0.0F;
		blob->rotate.x = blob->rotate.y = blob->rotate.z = 0.0F;
		blob->scale.x = blob->scale.y = blob->scale.z = 1.0F;
		dobj = DObjGetStruct(gobj);
		if (dobj != NULL)
		{
			blob->translate = dobj->translate.vec.f;
			blob->rotate = dobj->rotate.vec.f;
			blob->scale = dobj->scale.vec.f;
			syNetRbSnapCaptureDObjAnim(&blob->anim, dobj);
		}
		memcpy(blob->weapon_vars, &wp->weapon_vars, sizeof(blob->weapon_vars));
#ifdef PORT
		syNetRbSnapCaptureWeaponBlobMeta(slot, blob, gobj, wp);
#endif
		count++;
	}
	slot->weapon_count = count;
#ifdef PORT
	syNetRbSnapFinalizeWeaponBlobParents(slot);
	if (syNetRbSnapWeaponDiagEnabled() != FALSE)
	{
		port_log("SSB64 NetRbSnapshot: weapon save tick=%u weapon_count=%d truncated=%d\n",
		         (unsigned int)slot->tick,
		         (int)slot->weapon_count,
		         (int)truncated);
	}
#endif
	if (truncated != FALSE)
	{
		port_log("SSB64 NetRbSnapshot: weapon cap overflow (max=%d) tick=%u — save failed\n",
		         SYNETRB_SNAPSHOT_MAX_WEAPONS,
		         (unsigned int)slot->tick);
		return FALSE;
	}
	return TRUE;
}


#ifdef PORT

static u32 syNetRbSnapFnvAccumulateU32(u32 hash, u32 value)
{
	hash ^= value;
	hash *= 16777619U;
	return hash;
}

static sb32 syNetRbSnapLiveEffectListedInSnapshot(const SYNetRbSnapshotSlot *slot, u32 gobj_id);

static sb32 syNetRbSnapSnapshotEffectDiagEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG");
	s_env_cache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}

static u32 syNetRbSnapPointerFingerprintLow32(const void *p)
{
	uintptr_t u;

	u = (uintptr_t)p;
	if (sizeof(u) > sizeof(u32))
	{
		u ^= u >> 32;
	}
	return (u32)u;
}

static u32 syNetRbSnapGObjFuncProcFingerprint(GObj *gobj)
{
	GObjProcess *proc;

	if (gobj == NULL)
	{
		return 0U;
	}
	for (proc = gobj->gobjproc_head; proc != NULL; proc = proc->link_next)
	{
		if (proc->kind == nGCProcessKindFunc)
		{
			return syNetRbSnapPointerFingerprintLow32((const void *)proc->exec.func);
		}
	}
	return 0U;
}

static u8 syNetRbSnapEffectRespawnKindFromLive(const GObj *gobj, const EFStruct *ep)
{
	FTStruct *fp;

	if ((gobj == NULL) || (ep == NULL))
	{
		return SYNETRB_EFFECT_RESPAWN_NONE;
	}
	if (ep->proc_update == efManagerQuakeProcUpdate)
	{
		return SYNETRB_EFFECT_RESPAWN_QUAKE;
	}
	if (ep->proc_update == efManagerFoxReflectorProcUpdate)
	{
		return SYNETRB_EFFECT_RESPAWN_FOX_REFLECTOR;
	}
	if (ep->proc_update == efManagerShieldProcUpdate)
	{
		if (ep->fighter_gobj != NULL)
		{
			fp = ftGetStruct(ep->fighter_gobj);
			if ((fp != NULL) && (fp->fkind == nFTKindYoshi))
			{
				return SYNETRB_EFFECT_RESPAWN_YOSHI_SHIELD;
			}
		}
		return SYNETRB_EFFECT_RESPAWN_SHIELD;
	}
	if ((ep->proc_update == gcPlayAnimAll) && (ep->fighter_gobj != NULL))
	{
		fp = ftGetStruct(ep->fighter_gobj);
		if ((fp != NULL) && (fp->fkind == nFTKindNess) && (fp->is_effect_attach != FALSE))
		{
			return SYNETRB_EFFECT_RESPAWN_NESS_PK_WAVE;
		}
	}
	return SYNETRB_EFFECT_RESPAWN_NONE;
}

static sb32 syNetRbSnapEffectIdAllowed(const SYNetRbSnapshotSlot *slot, u32 gobj_id)
{
	s32 ei;
	s32 pi;

	if ((slot == NULL) || (gobj_id == 0U))
	{
		return FALSE;
	}
	if (syNetRbSnapLiveEffectListedInSnapshot(slot, gobj_id) != FALSE)
	{
		return TRUE;
	}
	for (pi = 0; pi < GMCOMMON_PLAYERS_MAX; pi++)
	{
		const SYNetRbSnapFighterBlob *fb;

		if (slot->fighters[pi].is_valid == FALSE)
		{
			continue;
		}
		fb = &slot->fighters[pi];
		if ((fb->guard_effect_gobj_id == gobj_id) || (fb->captureyoshi_effect_gobj_id == gobj_id) ||
		    (fb->fox_speciallw_effect_gobj_id == gobj_id))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static void syNetRbSnapClearFighterEffectPointerIfMatch(FTStruct *fp, GObj *ejected_gobj)
{
	if ((fp == NULL) || (ejected_gobj == NULL))
	{
		return;
	}
	if (fp->status_vars.common.guard.effect_gobj == ejected_gobj)
	{
		fp->status_vars.common.guard.effect_gobj = NULL;
	}
	if (fp->status_vars.common.captureyoshi.effect_gobj == ejected_gobj)
	{
		fp->status_vars.common.captureyoshi.effect_gobj = NULL;
	}
	if (fp->status_vars.fox.speciallw.effect_gobj == ejected_gobj)
	{
		fp->status_vars.fox.speciallw.effect_gobj = NULL;
	}
}

static void syNetRbSnapPruneOrphanFighterAttachedEffects(const SYNetRbSnapshotSlot *slot)
{
	s32 pass;
	GObj *gobj;
	GObj *next;

	if (slot == NULL)
	{
		return;
	}
	for (pass = 0; pass < 2; pass++)
	{
		GObj *link_head;

		link_head = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		for (gobj = link_head; gobj != NULL; gobj = next)
		{
			EFStruct *ep;
			FTStruct *fp;

			next = gobj->link_next;
			ep = efGetStruct(gobj);
			if ((ep == NULL) || (ep->fighter_gobj == NULL))
			{
				continue;
			}
			if (syNetRbSnapEffectIdAllowed(slot, gobj->id) != FALSE)
			{
				continue;
			}
			fp = ftGetStruct(ep->fighter_gobj);
			syNetRbSnapClearFighterEffectPointerIfMatch(fp, gobj);
			if ((fp != NULL) && (fp->is_effect_attach != FALSE))
			{
				fp->is_effect_attach = FALSE;
			}
			gcEjectGObj(gobj);
		}
	}
}

static void syNetRbSnapFinalizeFighterEffectAttachFlags(const SYNetRbSnapshotSlot *slot)
{
	s32 pi;
	GObj *fighter_gobj;

	if (slot == NULL)
	{
		return;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		const SYNetRbSnapFighterBlob *blob;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			continue;
		}
		pi = fp->player;
		if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX))
		{
			continue;
		}
		blob = &slot->fighters[pi];
		if (blob->is_valid == FALSE)
		{
			continue;
		}
		fp->is_effect_attach = (blob->is_effect_attach != 0U) ? TRUE : FALSE;
		if (fp->is_effect_attach == FALSE)
		{
			continue;
		}
		if ((blob->guard_effect_gobj_id == 0U) && (blob->captureyoshi_effect_gobj_id == 0U) &&
		    (blob->fox_speciallw_effect_gobj_id == 0U))
		{
			fp->is_effect_attach = FALSE;
		}
	}
}

static u32 syNetRbSnapFoldGroundPayloadHash(const SYNetRbSnapGroundBlob *ground)
{
	u32 hash;
	u32 n;

	if ((ground == NULL) || (ground->payload_len == 0U))
	{
		return 2166136261U;
	}
	hash = 2166136261U;
	hash = syNetRbSnapFnvAccumulateU32(hash, (u32)ground->gkind);
	n = (u32)ground->payload_len;
	if (n > SYNETRB_SNAPSHOT_GROUND_PAYLOAD_MAX)
	{
		n = SYNETRB_SNAPSHOT_GROUND_PAYLOAD_MAX;
	}
	{
		u32 i;

		for (i = 0; i < n; i++)
		{
			hash = syNetRbSnapFnvAccumulateU32(hash, (u32)ground->payload[i]);
		}
	}
	return hash;
}

static void syNetRbSnapCaptureGround(SYNetRbSnapshotSlot *slot)
{
	u8 gkind;

	memset(&slot->ground, 0, sizeof(slot->ground));
	slot->ground_captured = FALSE;
	if (gSCManagerBattleState == NULL)
	{
		return;
	}
	gkind = gSCManagerBattleState->gkind;
	slot->ground.gkind = gkind;
	switch (gkind)
	{
	case nGRKindHyrule:
	{
		const GRCommonGroundVarsHyrule *src = &gGRCommonStruct.hyrule;
		SYNetRbSnapGroundHyrule *dst = (SYNetRbSnapGroundHyrule *)slot->ground.payload;

		slot->ground.payload_len = (u16)sizeof(*dst);
		dst->twister_gobj_id = (src->twister_gobj != NULL) ? (u32)src->twister_gobj->id : 0U;
		dst->twister_leftedge_x = src->twister_leftedge_x;
		dst->twister_rightedge_x = src->twister_rightedge_x;
		dst->twister_vel = src->twister_vel;
		dst->twister_wait = src->twister_wait;
		dst->twister_speed_wait = src->twister_speed_wait;
		dst->twister_turn_wait = src->twister_turn_wait;
		dst->twister_line_id = src->twister_line_id;
		dst->twister_status = src->twister_status;
		dst->twister_pos_count = src->twister_pos_count;
		slot->ground_captured = TRUE;
		break;
	}
	case nGRKindJungle:
	{
		const GRCommonGroundVarsJungle *src = &gGRCommonStruct.jungle;
		SYNetRbSnapGroundJungle *dst = (SYNetRbSnapGroundJungle *)slot->ground.payload;

		slot->ground.payload_len = (u16)sizeof(*dst);
		dst->tarucann_gobj_id = (src->tarucann_gobj != NULL) ? (u32)src->tarucann_gobj->id : 0U;
		dst->tarucann_status = src->tarucann_status;
		dst->tarucann_wait = src->tarucann_wait;
		dst->tarucann_rotate_step = src->tarucann_rotate_step;
		slot->ground_captured = TRUE;
		break;
	}
	case nGRKindZebes:
	{
		const GRCommonGroundVarsZebes *src = &gGRCommonStruct.zebes;
		SYNetRbSnapGroundZebes *dst = (SYNetRbSnapGroundZebes *)slot->ground.payload;

		slot->ground.payload_len = (u16)sizeof(*dst);
		dst->acid_level_curr = src->acid_level_curr;
		dst->acid_level_step = src->acid_level_step;
		dst->acid_level_wait = src->acid_level_wait;
		dst->acid_status = src->acid_status;
		dst->acid_attr_id = src->acid_attr_id;
		dst->rumble_wait = src->rumble_wait;
		slot->ground_captured = TRUE;
		break;
	}
	case nGRKindYamabuki:
	{
		const GRCommonGroundVarsYamabuki *src = &gGRCommonStruct.yamabuki;
		SYNetRbSnapGroundYamabuki *dst = (SYNetRbSnapGroundYamabuki *)slot->ground.payload;

		slot->ground.payload_len = (u16)sizeof(*dst);
		dst->monster_gobj_id = (src->monster_gobj != NULL) ? (u32)src->monster_gobj->id : 0U;
		dst->gate_gobj_id = (src->gate_gobj != NULL) ? (u32)src->gate_gobj->id : 0U;
		dst->gate_pos = src->gate_pos;
		dst->gate_status = src->gate_status;
		dst->gate_noentry = src->gate_noentry;
		dst->monster_wait = src->monster_wait;
		dst->gate_wait = src->gate_wait;
		dst->monster_id_prev = src->monster_id_prev;
		slot->ground_captured = TRUE;
		break;
	}
	case nGRKindInishie:
	{
		const GRCommonGroundVarsInishie *src = &gGRCommonStruct.inishie;
		SYNetRbSnapGroundInishie *dst = (SYNetRbSnapGroundInishie *)slot->ground.payload;
		s32 ci;

		slot->ground.payload_len = (u16)sizeof(*dst);
		dst->splat_alt = src->splat_alt;
		dst->splat_accelerate = src->splat_accelerate;
		dst->splat_wait = src->splat_wait;
		dst->splat_status = src->splat_status;
		dst->pblock_status = src->pblock_status;
		dst->pblock_gobj_id = (src->pblock_gobj != NULL) ? (u32)src->pblock_gobj->id : 0U;
		dst->pblock_appear_wait = src->pblock_appear_wait;
		dst->pblock_pos_count = src->pblock_pos_count;
		memcpy(dst->players_tt, src->players_tt, sizeof(dst->players_tt));
		memcpy(dst->players_ga, src->players_ga, sizeof(dst->players_ga));
		dst->pakkun_gobj_id[0] = (src->pakkun_gobj[0] != NULL) ? (u32)src->pakkun_gobj[0]->id : 0U;
		dst->pakkun_gobj_id[1] = (src->pakkun_gobj[1] != NULL) ? (u32)src->pakkun_gobj[1]->id : 0U;
		for (ci = 0; ci < 2; ci++)
		{
			SYNetRbSnapGroundInishieScale *sd = &dst->scale[ci];
			const GRInishieScale *ls = &src->scale[ci];

			sd->platform_base_y = ls->platform_base_y;
			sd->string_length = ls->string_length;
			if (ls->platform_dobj != NULL)
			{
				sd->platform_translate = ls->platform_dobj->translate.vec.f;
			}
			if (ls->string_dobj != NULL)
			{
				sd->string_translate = ls->string_dobj->translate.vec.f;
			}
		}
		slot->ground_captured = TRUE;
		break;
	}
	case nGRKindYoster:
	{
		const GRCommonGroundVarsYoster *src = &gGRCommonStruct.yoster;
		SYNetRbSnapGroundYoster *dst = (SYNetRbSnapGroundYoster *)slot->ground.payload;
		s32 ci;

		slot->ground.payload_len = (u16)sizeof(*dst);
		for (ci = 0; ci < 3; ci++)
		{
			const GRYosterCloud *lc = &src->clouds[ci];
			SYNetRbSnapGroundYosterCloud *sc = &dst->clouds[ci];

			sc->gobj_id = (lc->gobj != NULL) ? (u32)lc->gobj->id : 0U;
			sc->altitude = lc->altitude;
			sc->pressure = lc->pressure;
			sc->status = lc->status;
			sc->anim_id = lc->anim_id;
			sc->is_cloud_line_active = lc->is_cloud_line_active;
			sc->pressure_timer = lc->pressure_timer;
			sc->evaporate_wait = lc->evaporate_wait;
		}
		slot->ground_captured = TRUE;
		break;
	}
	case nGRKindSector:
	{
		const GRCommonGroundVarsSector *src = &gGRCommonStruct.sector;
		SYNetRbSnapGroundSector *dst = (SYNetRbSnapGroundSector *)slot->ground.payload;

		slot->ground.payload_len = (u16)sizeof(*dst);
		dst->map_gobj_id = (src->map_gobj != NULL) ? (u32)src->map_gobj->id : 0U;
		dst->arwing_target_x = src->arwing_target_x;
		dst->arwing_appear_timer = src->arwing_appear_timer;
		dst->arwing_state_timer = src->arwing_state_timer;
		dst->arwing_status = src->arwing_status;
		dst->arwing_flight_pattern = src->arwing_flight_pattern;
		dst->arwing_type_cycle = src->arwing_type_cycle;
		dst->arwing_laser_ammo = src->arwing_laser_ammo;
		dst->arwing_laser_timer = src->arwing_laser_timer;
		dst->arwing_laser_count = src->arwing_laser_count;
		dst->arwing_pilot_curr = src->arwing_pilot_curr;
		dst->arwing_pilot_prev = src->arwing_pilot_prev;
		dst->is_arwing_z_near = src->is_arwing_z_near;
		dst->is_arwing_z_collision = src->is_arwing_z_collision;
		dst->is_arwing_line_active = src->is_arwing_line_active;
		dst->is_arwing_line_collision = src->is_arwing_line_collision;
		slot->ground_captured = TRUE;
		break;
	}
	case nGRKindPupupu:
	{
		const GRCommonGroundVarsPupupu *src = &gGRCommonStruct.pupupu;
		SYNetRbSnapGroundPupupu *dst = (SYNetRbSnapGroundPupupu *)slot->ground.payload;

		slot->ground.payload_len = (u16)sizeof(*dst);
		dst->whispy_wind_wait = src->whispy_wind_wait;
		dst->whispy_wind_duration = src->whispy_wind_duration;
		dst->whispy_blink_wait = src->whispy_blink_wait;
		dst->whispy_status = src->whispy_status;
		dst->whispy_eyes_status = src->whispy_eyes_status;
		dst->whispy_mouth_status = src->whispy_mouth_status;
		slot->ground_captured = TRUE;
		break;
	}
	case nGRKindCastle:
	{
		const GRCommonGroundVarsCastle *src = &gGRCommonStruct.castle;
		SYNetRbSnapGroundCastle *dst = (SYNetRbSnapGroundCastle *)slot->ground.payload;

		slot->ground.payload_len = (u16)sizeof(*dst);
		dst->bumper_gobj_id = (src->bumper_gobj != NULL) ? (u32)src->bumper_gobj->id : 0U;
		dst->bumper_pos = src->bumper_pos;
		slot->ground_captured = TRUE;
		break;
	}
	default:
		break;
	}
}

static void syNetRbSnapApplyGround(const SYNetRbSnapshotSlot *slot)
{
	const SYNetRbSnapGroundBlob *ground;

	if ((slot == NULL) || (slot->ground_captured == FALSE))
	{
		return;
	}
	ground = &slot->ground;
	if (gSCManagerBattleState == NULL)
	{
		return;
	}
	if (ground->gkind != gSCManagerBattleState->gkind)
	{
		return;
	}
	switch (ground->gkind)
	{
	case nGRKindHyrule:
		if (ground->payload_len >= (u16)sizeof(SYNetRbSnapGroundHyrule))
		{
			const SYNetRbSnapGroundHyrule *src = (const SYNetRbSnapGroundHyrule *)ground->payload;
			GRCommonGroundVarsHyrule *dst = &gGRCommonStruct.hyrule;

			dst->twister_gobj = (src->twister_gobj_id != 0U) ? gcFindGObjByID(src->twister_gobj_id) : NULL;
			dst->twister_leftedge_x = src->twister_leftedge_x;
			dst->twister_rightedge_x = src->twister_rightedge_x;
			dst->twister_vel = src->twister_vel;
			dst->twister_wait = src->twister_wait;
			dst->twister_speed_wait = src->twister_speed_wait;
			dst->twister_turn_wait = src->twister_turn_wait;
			dst->twister_line_id = src->twister_line_id;
			dst->twister_status = src->twister_status;
			dst->twister_pos_count = src->twister_pos_count;
		}
		break;
	case nGRKindJungle:
		if (ground->payload_len >= (u16)sizeof(SYNetRbSnapGroundJungle))
		{
			const SYNetRbSnapGroundJungle *src = (const SYNetRbSnapGroundJungle *)ground->payload;
			GRCommonGroundVarsJungle *dst = &gGRCommonStruct.jungle;

			dst->tarucann_gobj = (src->tarucann_gobj_id != 0U) ? gcFindGObjByID(src->tarucann_gobj_id) : NULL;
			dst->tarucann_status = src->tarucann_status;
			dst->tarucann_wait = src->tarucann_wait;
			dst->tarucann_rotate_step = src->tarucann_rotate_step;
		}
		break;
	case nGRKindZebes:
		if (ground->payload_len >= (u16)sizeof(SYNetRbSnapGroundZebes))
		{
			const SYNetRbSnapGroundZebes *src = (const SYNetRbSnapGroundZebes *)ground->payload;
			GRCommonGroundVarsZebes *dst = &gGRCommonStruct.zebes;

			dst->acid_level_curr = src->acid_level_curr;
			dst->acid_level_step = src->acid_level_step;
			dst->acid_level_wait = src->acid_level_wait;
			dst->acid_status = src->acid_status;
			dst->acid_attr_id = src->acid_attr_id;
			dst->rumble_wait = src->rumble_wait;
		}
		break;
	case nGRKindYamabuki:
		if (ground->payload_len >= (u16)sizeof(SYNetRbSnapGroundYamabuki))
		{
			const SYNetRbSnapGroundYamabuki *src = (const SYNetRbSnapGroundYamabuki *)ground->payload;
			GRCommonGroundVarsYamabuki *dst = &gGRCommonStruct.yamabuki;

			dst->monster_gobj = (src->monster_gobj_id != 0U) ? gcFindGObjByID(src->monster_gobj_id) : NULL;
			dst->gate_gobj = (src->gate_gobj_id != 0U) ? gcFindGObjByID(src->gate_gobj_id) : NULL;
			dst->gate_pos = src->gate_pos;
			dst->gate_status = src->gate_status;
			dst->gate_noentry = src->gate_noentry;
			dst->monster_wait = src->monster_wait;
			dst->gate_wait = src->gate_wait;
			dst->monster_id_prev = src->monster_id_prev;
		}
		break;
	case nGRKindInishie:
		if (ground->payload_len >= (u16)sizeof(SYNetRbSnapGroundInishie))
		{
			const SYNetRbSnapGroundInishie *src = (const SYNetRbSnapGroundInishie *)ground->payload;
			GRCommonGroundVarsInishie *dst = &gGRCommonStruct.inishie;
			s32 ci;

			dst->splat_alt = src->splat_alt;
			dst->splat_accelerate = src->splat_accelerate;
			dst->splat_wait = src->splat_wait;
			dst->splat_status = src->splat_status;
			dst->pblock_status = src->pblock_status;
			dst->pblock_gobj = (src->pblock_gobj_id != 0U) ? gcFindGObjByID(src->pblock_gobj_id) : NULL;
			dst->pblock_appear_wait = src->pblock_appear_wait;
			dst->pblock_pos_count = src->pblock_pos_count;
			memcpy(dst->players_tt, src->players_tt, sizeof(dst->players_tt));
			memcpy(dst->players_ga, src->players_ga, sizeof(dst->players_ga));
			dst->pakkun_gobj[0] = (src->pakkun_gobj_id[0] != 0U) ? gcFindGObjByID(src->pakkun_gobj_id[0]) : NULL;
			dst->pakkun_gobj[1] = (src->pakkun_gobj_id[1] != 0U) ? gcFindGObjByID(src->pakkun_gobj_id[1]) : NULL;
			for (ci = 0; ci < 2; ci++)
			{
				const SYNetRbSnapGroundInishieScale *ss = &src->scale[ci];
				GRInishieScale *ls = &dst->scale[ci];

				ls->platform_base_y = ss->platform_base_y;
				ls->string_length = ss->string_length;
				if (ls->platform_dobj != NULL)
				{
					ls->platform_dobj->translate.vec.f = ss->platform_translate;
				}
				if (ls->string_dobj != NULL)
				{
					ls->string_dobj->translate.vec.f = ss->string_translate;
				}
			}
		}
		break;
	case nGRKindYoster:
		if (ground->payload_len >= (u16)sizeof(SYNetRbSnapGroundYoster))
		{
			const SYNetRbSnapGroundYoster *src = (const SYNetRbSnapGroundYoster *)ground->payload;
			GRCommonGroundVarsYoster *dst = &gGRCommonStruct.yoster;
			s32 ci;

			for (ci = 0; ci < 3; ci++)
			{
				const SYNetRbSnapGroundYosterCloud *sc = &src->clouds[ci];
				GRYosterCloud *lc = &dst->clouds[ci];

				lc->gobj = (sc->gobj_id != 0U) ? gcFindGObjByID(sc->gobj_id) : NULL;
				lc->altitude = sc->altitude;
				lc->pressure = sc->pressure;
				lc->status = sc->status;
				lc->anim_id = sc->anim_id;
				lc->is_cloud_line_active = sc->is_cloud_line_active;
				lc->pressure_timer = sc->pressure_timer;
				lc->evaporate_wait = sc->evaporate_wait;
			}
		}
		break;
	case nGRKindSector:
		if (ground->payload_len >= (u16)sizeof(SYNetRbSnapGroundSector))
		{
			const SYNetRbSnapGroundSector *src = (const SYNetRbSnapGroundSector *)ground->payload;
			GRCommonGroundVarsSector *dst = &gGRCommonStruct.sector;

			dst->map_gobj = (src->map_gobj_id != 0U) ? gcFindGObjByID(src->map_gobj_id) : NULL;
			dst->arwing_target_x = src->arwing_target_x;
			dst->arwing_appear_timer = src->arwing_appear_timer;
			dst->arwing_state_timer = src->arwing_state_timer;
			dst->arwing_status = src->arwing_status;
			dst->arwing_flight_pattern = src->arwing_flight_pattern;
			dst->arwing_type_cycle = src->arwing_type_cycle;
			dst->arwing_laser_ammo = src->arwing_laser_ammo;
			dst->arwing_laser_timer = src->arwing_laser_timer;
			dst->arwing_laser_count = src->arwing_laser_count;
			dst->arwing_pilot_curr = src->arwing_pilot_curr;
			dst->arwing_pilot_prev = src->arwing_pilot_prev;
			dst->is_arwing_z_near = src->is_arwing_z_near;
			dst->is_arwing_z_collision = src->is_arwing_z_collision;
			dst->is_arwing_line_active = src->is_arwing_line_active;
			dst->is_arwing_line_collision = src->is_arwing_line_collision;
		}
		break;
	case nGRKindPupupu:
		if (ground->payload_len >= (u16)sizeof(SYNetRbSnapGroundPupupu))
		{
			const SYNetRbSnapGroundPupupu *src = (const SYNetRbSnapGroundPupupu *)ground->payload;
			GRCommonGroundVarsPupupu *dst = &gGRCommonStruct.pupupu;

			dst->whispy_wind_wait = src->whispy_wind_wait;
			dst->whispy_wind_duration = src->whispy_wind_duration;
			dst->whispy_blink_wait = src->whispy_blink_wait;
			dst->whispy_status = src->whispy_status;
			dst->whispy_eyes_status = src->whispy_eyes_status;
			dst->whispy_mouth_status = src->whispy_mouth_status;
		}
		break;
	case nGRKindCastle:
		if (ground->payload_len >= (u16)sizeof(SYNetRbSnapGroundCastle))
		{
			const SYNetRbSnapGroundCastle *src = (const SYNetRbSnapGroundCastle *)ground->payload;
			GRCommonGroundVarsCastle *dst = &gGRCommonStruct.castle;

			dst->bumper_gobj = (src->bumper_gobj_id != 0U) ? gcFindGObjByID(src->bumper_gobj_id) : NULL;
			dst->bumper_pos = src->bumper_pos;
		}
		break;
	default:
		break;
	}
}

u32 syNetRbSnapshotFoldGroundHash(const void *slot_opaque)
{
	const SYNetRbSnapshotSlot *slot = (const SYNetRbSnapshotSlot *)slot_opaque;

	if ((slot == NULL) || (slot->ground_captured == FALSE))
	{
		return 2166136261U;
	}
	return syNetRbSnapFoldGroundPayloadHash(&slot->ground);
}

static void syNetRbSnapSanitizeEffectVarsBlob(u8 *vars_out, const EFStruct *ep)
{
	EFStruct scratch;

	if ((vars_out == NULL) || (ep == NULL))
	{
		return;
	}
	memcpy(&scratch.effect_vars, &ep->effect_vars, sizeof(scratch.effect_vars));
	scratch.effect_vars.common.xf = NULL;
	scratch.effect_vars.dust_light.xf = NULL;
	scratch.effect_vars.dust_heavy.xf = NULL;
	memcpy(vars_out, &scratch.effect_vars, sizeof(scratch.effect_vars));
}

static GObj *syNetRbSnapTryRespawnEffectFromBlob(const SYNetRbSnapEffectBlob *blob)
{
	GObj *fighter_gobj;
	GObj *effect_gobj;

	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return NULL;
	}
	fighter_gobj = NULL;
	if (blob->fighter_gobj_id != 0U)
	{
		fighter_gobj = gcFindGObjByID(blob->fighter_gobj_id);
		if ((fighter_gobj != NULL) && (ftGetStruct(fighter_gobj) == NULL))
		{
			fighter_gobj = NULL;
		}
	}
	switch (blob->respawn_kind)
	{
	case SYNETRB_EFFECT_RESPAWN_QUAKE:
		if (blob->quake_magnitude != 0xFFU)
		{
			return efManagerQuakeMakeEffect((s32)blob->quake_magnitude);
		}
		break;
	case SYNETRB_EFFECT_RESPAWN_SHIELD:
		if (fighter_gobj != NULL)
		{
			return efManagerShieldMakeEffect(fighter_gobj);
		}
		break;
	case SYNETRB_EFFECT_RESPAWN_YOSHI_SHIELD:
		if (fighter_gobj != NULL)
		{
			return efManagerYoshiShieldMakeEffect(fighter_gobj);
		}
		break;
	case SYNETRB_EFFECT_RESPAWN_FOX_REFLECTOR:
		if (fighter_gobj != NULL)
		{
			return efManagerFoxReflectorMakeEffect(fighter_gobj);
		}
		break;
	case SYNETRB_EFFECT_RESPAWN_NESS_PK_WAVE:
		if (fighter_gobj != NULL)
		{
			return efManagerNessPKThunderWaveMakeEffect(fighter_gobj);
		}
		break;
	default:
		break;
	}
	return NULL;
}

static void syNetRbSnapApplyEffectBlobToGObj(GObj *gobj, const SYNetRbSnapEffectBlob *blob)
{
	EFStruct *ep;
	GObj *fighter_gobj;

	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return;
	}
	if (gobj == NULL)
	{
		gobj = syNetRbSnapTryRespawnEffectFromBlob(blob);
		if (gobj == NULL)
		{
			return;
		}
	}
	if ((blob->snap_flags & SYNETRB_EFFECT_SNAP_NO_STRUCT) != 0U)
	{
		gobj->anim_frame = blob->anim_frame;
		return;
	}
	ep = efGetStruct(gobj);
	if (ep == NULL)
	{
		return;
	}
	gobj->anim_frame = blob->anim_frame;
	memcpy(&ep->effect_vars, blob->effect_vars, sizeof(ep->effect_vars));
	ep->xf = NULL;
	ep->effect_vars.common.xf = NULL;
	ep->effect_vars.dust_light.xf = NULL;
	ep->effect_vars.dust_heavy.xf = NULL;
	ep->bank_id = blob->bank_id;
	fighter_gobj = NULL;
	if (blob->fighter_gobj_id != 0U)
	{
		fighter_gobj = gcFindGObjByID(blob->fighter_gobj_id);
		if ((fighter_gobj != NULL) && (ftGetStruct(fighter_gobj) == NULL))
		{
			fighter_gobj = NULL;
		}
	}
	ep->fighter_gobj = fighter_gobj;
}

static GObj *syNetRbSnapResolveCoupledEffectGobj(u32 effect_gobj_id)
{
	GObj *eg;

	if (effect_gobj_id == 0U)
	{
		return NULL;
	}
	eg = gcFindGObjByID(effect_gobj_id);
	return ((eg != NULL) && (efGetStruct(eg) != NULL)) ? eg : NULL;
}

static void syNetRbSnapRebindFighterEffectGobjs(const SYNetRbSnapFighterBlob *blob, FTStruct *fp)
{
	if ((blob == NULL) || (fp == NULL) || (blob->is_valid == FALSE))
	{
		return;
	}
	fp->status_vars.common.guard.effect_gobj = syNetRbSnapResolveCoupledEffectGobj(blob->guard_effect_gobj_id);
	fp->status_vars.common.captureyoshi.effect_gobj =
	    syNetRbSnapResolveCoupledEffectGobj(blob->captureyoshi_effect_gobj_id);
	fp->status_vars.fox.speciallw.effect_gobj =
	    syNetRbSnapResolveCoupledEffectGobj(blob->fox_speciallw_effect_gobj_id);
}

static void syNetRbSnapResetParticlesForRollback(void)
{
	lbParticleEjectStructAll();
	lbParticleEjectGeneratorAll();
}

static sb32 syNetRbSnapGObjLinkAuditEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_GOBJ_LINK_AUDIT");
	s_env_cache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}

void syNetRbSnapshotGObjLinkAudit(u32 tick)
{
	s32 link_id;
	GObj *gobj;
	s32 counts[17];

	if (syNetRbSnapGObjLinkAuditEnabled() == FALSE)
	{
		return;
	}
	memset(counts, 0, sizeof(counts));
	for (link_id = 0; link_id < (s32)(sizeof(counts) / sizeof(counts[0])); link_id++)
	{
		for (gobj = gGCCommonLinks[link_id]; gobj != NULL; gobj = gobj->link_next)
		{
			counts[link_id]++;
		}
	}
	port_log(
	    "SSB64 NetRbSnapshot: gobj_link_audit tick=%u f=%d i=%d w=%d ef6=%d ef8=%d g=%d ia=%d l13=%d\n",
	    tick,
	    counts[nGCCommonLinkIDFighter],
	    counts[nGCCommonLinkIDItem],
	    counts[nGCCommonLinkIDWeapon],
	    counts[nGCCommonLinkIDEffect],
	    counts[nGCCommonLinkIDSpecialEffect],
	    counts[nGCCommonLinkIDGround],
	    counts[nGCCommonLinkIDItemActor],
	    counts[13]);
}

static sb32 syNetRbSnapLiveEffectListedInSnapshot(const SYNetRbSnapshotSlot *slot, u32 gobj_id)
{
	s32 ei;

	if (slot == NULL)
	{
		return FALSE;
	}
	for (ei = 0; ei < slot->effect_count; ei++)
	{
		if ((slot->effects[ei].is_valid != FALSE) && (slot->effects[ei].gobj_id == gobj_id))
		{
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * Apply snapshot effects, then eject predicted extras: free-floating EFStructs, fighter-attached effects not
 * listed in the snapshot, and no-struct particle-shell effect GObjs.
 */
static void syNetRbSnapReconcileSnapshotEffectsBeforeItems(const SYNetRbSnapshotSlot *slot)
{
	s32 pass;
	GObj *gobj;
	GObj *next;
	s32 ei;

	if (slot == NULL)
	{
		return;
	}
	for (ei = 0; ei < slot->effect_count; ei++)
	{
		const SYNetRbSnapEffectBlob *blob;

		blob = &slot->effects[ei];
		if (blob->is_valid == FALSE)
		{
			continue;
		}
		gobj = gcFindGObjByID(blob->gobj_id);
		syNetRbSnapApplyEffectBlobToGObj(gobj, blob);
	}
	syNetRbSnapPruneOrphanFighterAttachedEffects(slot);
	for (pass = 0; pass < 2; pass++)
	{
		GObj *link_head;

		link_head = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		for (gobj = link_head; gobj != NULL; gobj = next)
		{
			EFStruct *ep;

			next = gobj->link_next;
			ep = efGetStruct(gobj);
			if (ep != NULL)
			{
				if (ep->fighter_gobj != NULL)
				{
					continue;
				}
				if (syNetRbSnapLiveEffectListedInSnapshot(slot, gobj->id) == FALSE)
				{
					gcEjectGObj(gobj);
				}
				continue;
			}
			if ((gobj->user_data.p == NULL) && (gobj->obj_kind == nGCCommonKindEffect) &&
			    (syNetRbSnapLiveEffectListedInSnapshot(slot, gobj->id) == FALSE))
			{
				gcEjectGObj(gobj);
			}
		}
	}
}

static sb32 syNetRbSnapCaptureEffects(SYNetRbSnapshotSlot *slot)
{
	GObj *sorted[SYNETRB_SNAPSHOT_MAX_EFFECTS];
	s32 count;
	s32 i;
	sb32 truncated;

	truncated = FALSE;
	count = syNetRbEnumerateActiveEffectsSorted(sorted, SYNETRB_SNAPSHOT_MAX_EFFECTS, &truncated);
	for (i = 0; i < count; i++)
	{
		GObj *gobj_iter;
		EFStruct *ep;
		SYNetRbSnapEffectBlob *blob;

		gobj_iter = sorted[i];
		ep = efGetStruct(gobj_iter);
		blob = &slot->effects[i];
		memset(blob, 0, sizeof(*blob));
		blob->is_valid = TRUE;
		blob->gobj_id = gobj_iter->id;
		blob->link_id = gobj_iter->link_id;
		blob->anim_frame = gobj_iter->anim_frame;
		blob->quake_magnitude = 0xFFU;
		blob->respawn_kind = SYNETRB_EFFECT_RESPAWN_NONE;
		if (ep != NULL)
		{
			blob->bank_id = ep->bank_id;
			blob->fighter_gobj_id = (ep->fighter_gobj != NULL) ? (u32)ep->fighter_gobj->id : 0U;
			blob->proc_update_fingerprint = syNetRbSnapPointerFingerprintLow32((const void *)ep->proc_update);
			blob->respawn_kind = syNetRbSnapEffectRespawnKindFromLive(gobj_iter, ep);
			if (blob->respawn_kind == SYNETRB_EFFECT_RESPAWN_QUAKE)
			{
				blob->quake_magnitude = (u8)(3 - ep->effect_vars.quake.priority);
			}
			syNetRbSnapSanitizeEffectVarsBlob(blob->effect_vars, ep);
		}
		else
		{
			blob->snap_flags = SYNETRB_EFFECT_SNAP_NO_STRUCT;
			blob->proc_update_fingerprint = syNetRbSnapGObjFuncProcFingerprint(gobj_iter);
		}
	}
	slot->effect_count = count;
	if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
	{
		port_log("SSB64 NetRbSnapshot: effect save tick=%u effect_count=%d truncated=%d\n",
		         (unsigned int)slot->tick, (int)slot->effect_count, (int)truncated);
	}
	if (truncated != FALSE)
	{
		port_log("SSB64 NetRbSnapshot: effect cap overflow (max=%d) tick=%u — save failed\n",
		         SYNETRB_SNAPSHOT_MAX_EFFECTS, (unsigned int)slot->tick);
		return FALSE;
	}
	return TRUE;
}

#endif /* PORT */

static ub8 syNetRbSnapStarRodIsSmashFromBlob(const SYNetRbSnapWeaponBlob *blob)
{
	f32 vel_x;

	if (blob == NULL)
	{
		return FALSE;
	}
	vel_x = blob->physics.vel_air.x;
	if (vel_x < 0.0F)
	{
		vel_x = -vel_x;
	}
	return (vel_x >= (ITSTARROD_AMMO_SMASH_VEL_X - 1.0F)) ? TRUE : FALSE;
}

static GObj *syNetRbSnapSpawnWeaponFromBlob(const SYNetRbSnapWeaponBlob *blob)
{
	GObj *owner_gobj;
	GObj *parent_gobj;
	Vec3f spawn_pos;
	Vec3f vel;
	union wpStatusVars *vars;

	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return NULL;
	}
	owner_gobj = syNetRbSnapResolveWeaponOwnerFromBlob(blob);
	parent_gobj = syNetRbSnapResolveWeaponSpawnParent(blob);
	if (owner_gobj == NULL)
	{
		return NULL;
	}
	spawn_pos = blob->translate;
	vel = blob->physics.vel_air;
	vars = (union wpStatusVars *)blob->weapon_vars;
	switch (blob->kind)
	{
	case nWPKindFireball:
		return wpMarioFireballMakeWeapon(owner_gobj, &spawn_pos, vars->fireball.index);

	case nWPKindBlaster:
		return wpFoxBlasterMakeWeapon(owner_gobj, &spawn_pos);

	case nWPKindChargeShot:
		return wpSamusChargeShotMakeWeapon(owner_gobj, &spawn_pos, vars->charge_shot.charge_size,
		                                   vars->charge_shot.is_release);

	case nWPKindSamusBomb:
		return wpSamusBombMakeWeapon(owner_gobj, &spawn_pos);

	case nWPKindCutter:
		return wpKirbyCutterMakeWeapon(owner_gobj, &spawn_pos);

	case nWPKindEggThrow:
		return wpYoshiEggThrowMakeWeapon(owner_gobj, &spawn_pos);

	case nWPKindYoshiStar:
		return wpYoshiStarMakeWeapon(owner_gobj, &spawn_pos, blob->lr);

	case nWPKindBoomerang:
		return wpLinkBoomerangMakeWeapon(owner_gobj, &spawn_pos);

	case nWPKindSpinAttack:
		return wpLinkSpinAttackMakeWeapon(owner_gobj, &spawn_pos);

	case nWPKindThunderJoltAir:
		return wpPikachuThunderJoltAirMakeWeapon(owner_gobj, &spawn_pos, &vel);

	case nWPKindThunderJoltGround:
		if (parent_gobj == NULL)
		{
			return NULL;
		}
		return wpPikachuThunderJoltGroundMakeWeapon(parent_gobj, &spawn_pos, vars->thunder_jolt.line_type);

	case nWPKindThunderHead:
		return wpPikachuThunderHeadMakeWeapon(owner_gobj, &spawn_pos, &vel);

	case nWPKindThunderTrail:
		if (parent_gobj == NULL)
		{
			return NULL;
		}
		return wpPikachuThunderTrailMakeWeapon(parent_gobj, &spawn_pos);

	case nWPKindPKFire:
		return wpNessPKFireMakeWeapon(owner_gobj, &spawn_pos, &vel, syNetRbSnapPKFireAngleFromBlob(blob));

	case nWPKindPKThunderHead:
		if (blob->spawn_profile == SYNETRB_WEAPON_SPAWN_PK_REFLECT_HEAD)
		{
			if (parent_gobj == NULL)
			{
				return NULL;
			}
			return wpNessPKReflectHeadMakeWeapon(parent_gobj, &spawn_pos, vars->pkthunder.angle);
		}
		return wpNessPKThunderHeadMakeWeapon(owner_gobj, &spawn_pos, &vel);

	case nWPKindPKThunderTrail:
		if (parent_gobj == NULL)
		{
			return NULL;
		}
		if (blob->spawn_profile == SYNETRB_WEAPON_SPAWN_PK_REFLECT_TRAIL)
		{
			return wpNessPKReflectTrailMakeWeapon(parent_gobj, &spawn_pos, vars->pkthunder_trail.trail_id);
		}
		return wpNessPKThunderTrailMakeWeapon(parent_gobj, &spawn_pos, vars->pkthunder_trail.trail_id);

	case nWPKindLGunAmmo:
		return itLGunWeaponAmmoMakeWeapon(owner_gobj, &spawn_pos);

	case nWPKindFFlowerFlame:
		return itFFlowerWeaponFlameMakeWeapon(owner_gobj, &spawn_pos, &vel);

	case nWPKindStarRodStar:
		return itStarRodWeaponStarMakeWeapon(owner_gobj, &spawn_pos, syNetRbSnapStarRodIsSmashFromBlob(blob));

	case nWPKindIwarkRock:
		return itIwarkWeaponRockMakeWeapon(owner_gobj, &spawn_pos, 0U);

	case nWPKindNyarsCoin:
		return itNyarsWeaponCoinMakeWeapon(owner_gobj, 0U, 0.0F);

	case nWPKindLizardonFlame:
		return itLizardonWeaponFlameMakeWeapon(owner_gobj, &spawn_pos, &vel);

	case nWPKindSpearSwarm:
	{
		ITStruct *ip = itGetStruct(owner_gobj);
		s32 item_kind = (ip != NULL) ? ip->kind : nITKindSpear;

		return itSpearWeaponSwarmMakeWeapon(owner_gobj, &spawn_pos, item_kind);
	}

	case nWPKindKamexHydro:
		return itKamexWeaponHydroMakeWeapon(owner_gobj, &spawn_pos);

	case nWPKindStarmieSwift:
		return itStarmieWeaponSwiftMakeWeapon(owner_gobj, &spawn_pos);

	case nWPKindDogasSmog:
		return itDogasWeaponSmogMakeWeapon(owner_gobj, &spawn_pos, &vel);

	case nWPKindHitokageFlame:
		return itHitokageWeaponFlameMakeWeapon(owner_gobj, &spawn_pos, &vel);

	case nWPKindFushigibanaRazor:
		return itFushigibanaWeaponRazorMakeWeapon(owner_gobj, &spawn_pos);

	default:
#ifdef PORT
		port_log("SSB64 NetRbSnapshot: weapon respawn unsupported kind=%d\n", (int)blob->kind);
#endif
		return NULL;
	}
}

static void syNetRbSnapApplyWeaponBlobToGObj(GObj *gobj, const SYNetRbSnapWeaponBlob *blob, u32 tick)
{
	WPStruct *wp;
	DObj *dobj;
	Vec3f *topn;

	wp = wpGetStruct(gobj);
	if ((wp == NULL) || (blob == NULL))
	{
		return;
	}
	wp->kind = blob->kind;
	wp->instance_id = blob->instance_id;
	wp->team = blob->team;
	wp->player = blob->player;
	wp->player_num = blob->player_num;
	wp->lr = blob->lr;
	wp->physics = blob->physics;
	topn = NULL;
	dobj = DObjGetStruct(gobj);
	if (dobj != NULL)
	{
		topn = &dobj->translate.vec.f;
	}
	syNetRbSnapApplyMPColl(&wp->coll_data, &blob->coll, topn, &wp->lr);
	wp->ga = blob->ga;
	wp->attack_coll = blob->attack_coll;
	wp->lifetime = blob->lifetime;
	syNetRbSnapApplyWeaponOwnerFromBlob(wp, blob, tick);
	wp->reflect_gobj = syNetRbSnapResolveLiveGobj(blob->reflect_gobj_id);
	wp->absorb_gobj = syNetRbSnapResolveLiveGobj(blob->absorb_gobj_id);
	wp->group_id = blob->group_id;
	if (dobj != NULL)
	{
		dobj->translate.vec.f = blob->translate;
		dobj->rotate.vec.f = blob->rotate;
		dobj->scale.vec.f = blob->scale;
		syNetRbSnapApplyDObjAnim(dobj, &blob->anim);
	}
	memcpy(&wp->weapon_vars, blob->weapon_vars, sizeof(wp->weapon_vars));
	syNetRbSnapApplyWeaponBlobMeta(wp, blob);
}

static void syNetRbSnapApplyWeapons(const SYNetRbSnapshotSlot *slot)
{
	GObj *gobj;
	s32 si;
	s32 pass;
	sb32 matched[SYNETRB_SNAPSHOT_MAX_WEAPONS];
	s32 ejected_count;
	s32 matched_count;
	s32 respawned_count;
	s32 deferred_count;

	for (si = 0; si < SYNETRB_SNAPSHOT_MAX_WEAPONS; si++)
	{
		matched[si] = FALSE;
	}
	ejected_count = 0;
	matched_count = 0;
	respawned_count = 0;
	deferred_count = 0;

	for (gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; gobj != NULL;)
	{
		GObj *next_gobj;
		WPStruct *wp;
		s32 found;

		next_gobj = gobj->link_next;
		wp = wpGetStruct(gobj);
		if (wp == NULL)
		{
			syNetRbSnapLogSkippedGObj("apply", "weapon", gobj, slot->tick);
			gobj = next_gobj;
			continue;
		}
		found = syNetRbSnapFindWeaponBlobByInstanceId(slot, matched, wp->instance_id);
		if (found < 0)
		{
			DObj *dobj = DObjGetStruct(gobj);

			if (dobj != NULL)
			{
				found = syNetRbSnapFindWeaponBlobByIdentity(slot, matched, wp, &dobj->translate.vec.f, NULL);
			}
		}
		if (found < 0)
		{
#ifdef PORT
			deferred_count++;
#endif
			gobj = next_gobj;
			continue;
		}
		if (wp->kind != slot->weapons[found].kind)
		{
			gcEjectGObj(gobj);
			ejected_count++;
			gobj = next_gobj;
			continue;
		}
		matched[found] = TRUE;
		matched_count++;
		syNetRbSnapApplyWeaponBlobToGObj(gobj, &slot->weapons[found], slot->tick);
		gobj = next_gobj;
	}

	for (pass = 0; pass <= slot->weapon_count; pass++)
	{
		sb32 any_spawned;

		any_spawned = FALSE;
		for (si = 0; si < slot->weapon_count; si++)
		{
			const SYNetRbSnapWeaponBlob *blob;
			GObj *spawned;

			if ((matched[si] != FALSE) || (slot->weapons[si].is_valid == FALSE))
			{
				continue;
			}
			blob = &slot->weapons[si];
			spawned = syNetRbSnapSpawnWeaponFromBlob(blob);
			if (spawned != NULL)
			{
				respawned_count++;
				any_spawned = TRUE;
				syNetRbSnapApplyWeaponBlobToGObj(spawned, blob, slot->tick);
				matched[si] = TRUE;
			}
		}
		if (any_spawned == FALSE)
		{
			break;
		}
	}
	for (si = 0; si < slot->weapon_count; si++)
	{
		if ((matched[si] == FALSE) && (slot->weapons[si].is_valid != FALSE))
		{
#ifdef PORT
			port_log("SSB64 NetRbSnapshot: weapon respawn failed tick=%u kind=%d owner_id=%u parent_id=%u\n",
			         (unsigned int)slot->tick,
			         (int)slot->weapons[si].kind,
			         (unsigned int)slot->weapons[si].owner_gobj_id,
			         (unsigned int)slot->weapons[si].spawn_parent_gobj_id);
#endif
		}
	}

	for (gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; gobj != NULL; gobj = gobj->link_next)
	{
		WPStruct *wp = wpGetStruct(gobj);
		s32 found;

		if (wp == NULL)
		{
			continue;
		}
		found = syNetRbSnapFindLiveWeaponBlobIndex(slot, wp);
		if (found >= 0)
		{
			syNetRbSnapApplyWeaponOwnerFromBlob(wp, &slot->weapons[found], slot->tick);
			syNetRbSnapApplyWeaponBlobMeta(wp, &slot->weapons[found]);
		}
	}

#ifdef PORT
	memcpy(sSYNetRbSnapWeaponApplyMatched, matched, sizeof(matched));
	sSYNetRbSnapWeaponApplyTick = slot->tick;
	sSYNetRbSnapWeaponApplyPendingEject = (deferred_count > 0) ? TRUE : FALSE;
	sSYNetRbSnapWeaponApplyMatchedCount = matched_count;
	sSYNetRbSnapWeaponApplyRespawnedCount = respawned_count;
	if (syNetRbSnapWeaponDiagEnabled() != FALSE)
	{
		port_log(
		    "SSB64 NetRbSnapshot: weapon apply tick=%u ejected=%d matched=%d respawned=%d blob_count=%d deferred=%d\n",
		    (unsigned int)slot->tick,
		    ejected_count,
		    matched_count,
		    respawned_count,
		    (int)slot->weapon_count,
		    deferred_count);
	}
#endif
}

#ifdef PORT
static void syNetRbSnapEjectUnmatchedWeaponsAfterCoupling(const SYNetRbSnapshotSlot *slot)
{
	GObj *gobj;
	s32 ejected_count;
	s32 rematched_count;
	sb32 *matched;

	if ((slot == NULL) || (slot->is_valid == FALSE) || (sSYNetRbSnapWeaponApplyPendingEject == FALSE) ||
	    (sSYNetRbSnapWeaponApplyTick != slot->tick))
	{
		return;
	}
	matched = sSYNetRbSnapWeaponApplyMatched;
	ejected_count = 0;
	rematched_count = 0;

	for (gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; gobj != NULL;)
	{
		GObj *next_gobj;
		WPStruct *wp;
		s32 found;

		next_gobj = gobj->link_next;
		wp = wpGetStruct(gobj);
		if (wp == NULL)
		{
			gobj = next_gobj;
			continue;
		}
		if (syNetRbSnapFindLiveWeaponBlobIndex(slot, wp) >= 0)
		{
			gobj = next_gobj;
			continue;
		}
		found = syNetRbSnapFindWeaponBlobByInstanceId(slot, matched, wp->instance_id);
		if (found < 0)
		{
			DObj *dobj = DObjGetStruct(gobj);

			if (dobj != NULL)
			{
				found = syNetRbSnapFindWeaponBlobByIdentity(slot, matched, wp, &dobj->translate.vec.f, NULL);
			}
		}
		if (found >= 0)
		{
			if (wp->kind == slot->weapons[found].kind)
			{
				matched[found] = TRUE;
				rematched_count++;
				syNetRbSnapApplyWeaponBlobToGObj(gobj, &slot->weapons[found], slot->tick);
				syNetRbSnapApplyWeaponOwnerFromBlob(wp, &slot->weapons[found], slot->tick);
				syNetRbSnapApplyWeaponBlobMeta(wp, &slot->weapons[found]);
				gobj = next_gobj;
				continue;
			}
			syNetRbSnapPreEjectPKThunderWeapon(gobj, wp);
			gcEjectGObj(gobj);
			ejected_count++;
			gobj = next_gobj;
			continue;
		}
		if (syNetRbSnapLiveWeaponIsFighterCoupledReference(gobj) != FALSE)
		{
			gobj = next_gobj;
			continue;
		}
		if (syNetRbSnapLiveWeaponIsFireballThrowPreserve(gobj) != FALSE)
		{
			gobj = next_gobj;
			continue;
		}
		if (syNetRbSnapLiveWeaponIsThunderJoltThrowPreserve(gobj) != FALSE)
		{
			gobj = next_gobj;
			continue;
		}
		if (syNetRbSnapLiveWeaponIsPKThunderPreserve(gobj) != FALSE)
		{
			gobj = next_gobj;
			continue;
		}
		if (syNetRbSnapLiveWeaponIsPKFirePreserve(gobj) != FALSE)
		{
			gobj = next_gobj;
			continue;
		}
		syNetRbSnapPreEjectPKThunderWeapon(gobj, wp);
		gcEjectGObj(gobj);
		ejected_count++;
		gobj = next_gobj;
	}

	sSYNetRbSnapWeaponApplyPendingEject = FALSE;
	if (syNetRbSnapWeaponDiagEnabled() != FALSE)
	{
		port_log(
		    "SSB64 NetRbSnapshot: weapon eject deferred tick=%u ejected=%d rematched=%d matched=%d respawned=%d blob_count=%d\n",
		    (unsigned int)slot->tick,
		    ejected_count,
		    rematched_count,
		    sSYNetRbSnapWeaponApplyMatchedCount + rematched_count,
		    sSYNetRbSnapWeaponApplyRespawnedCount,
		    (int)slot->weapon_count);
	}
}
#endif /* PORT */

static void syNetRbSnapCaptureCamera(SYNetRbSnapCameraBlob *cam)
{
	extern f32 gGMCameraPauseCameraEyeX;
	extern f32 gGMCameraPauseCameraEyeY;

	cam->camera = gGMCameraStruct;
	cam->camera_gobj_id = syNetRbSnapGobjId(gGMCameraGObj);
	cam->pzoom_fighter_gobj_id = syNetRbSnapGobjId(gGMCameraStruct.pzoom_fighter_gobj);
	cam->pfollow_fighter_gobj_id = syNetRbSnapGobjId(gGMCameraStruct.pfollow_fighter_gobj);
	cam->pzoom_fighter_player = syNetRbSnapFighterPlayerFromGobj(gGMCameraStruct.pzoom_fighter_gobj);
	cam->pfollow_fighter_player = syNetRbSnapFighterPlayerFromGobj(gGMCameraStruct.pfollow_fighter_gobj);
	cam->pause_eye_x = gGMCameraPauseCameraEyeX;
	cam->pause_eye_y = gGMCameraPauseCameraEyeY;
}

static void syNetRbSnapApplyCamera(const SYNetRbSnapCameraBlob *cam)
{
	extern f32 gGMCameraPauseCameraEyeX;
	extern f32 gGMCameraPauseCameraEyeY;
	GObj *cg;

	gGMCameraStruct = cam->camera;
	cg = syNetRbSnapResolveGobj(cam->camera_gobj_id);
	if (cg != NULL)
	{
		gGMCameraGObj = cg;
	}
	if (cam->pzoom_fighter_player >= 0)
	{
		gGMCameraStruct.pzoom_fighter_gobj = syNetRbSnapResolveFighterGobjByPlayer(cam->pzoom_fighter_player);
	}
	else
	{
		gGMCameraStruct.pzoom_fighter_gobj = syNetRbSnapResolveLiveGobj(cam->pzoom_fighter_gobj_id);
	}
	if (cam->pfollow_fighter_player >= 0)
	{
		gGMCameraStruct.pfollow_fighter_gobj = syNetRbSnapResolveFighterGobjByPlayer(cam->pfollow_fighter_player);
	}
	else
	{
		gGMCameraStruct.pfollow_fighter_gobj = syNetRbSnapResolveLiveGobj(cam->pfollow_fighter_gobj_id);
	}
	gGMCameraPauseCameraEyeX = cam->pause_eye_x;
	gGMCameraPauseCameraEyeY = cam->pause_eye_y;
}

static SYNetRbSnapshotSlot *syNetRbSnapshotSlotForTick(u32 tick)
{
	return &sSYNetRbSnapshotRing[tick % sSYNetRbSnapshotRingLen];
}

sb32 syNetRbSnapshotSynctestProbeWeaponMismatch(u32 probe_tick)
{
	SYNetRbSnapshotSlot *slot;
	s32 live_count;

	slot = syNetRbSnapshotSlotForTick(probe_tick);
	if ((slot == NULL) || (slot->is_valid == FALSE) || (slot->tick != probe_tick))
	{
		return FALSE;
	}
	live_count = syNetRbSnapCountLiveWeapons();
	return (live_count != slot->weapon_count) ? TRUE : FALSE;
}

static s32 syNetRbSnapProbeCountEffectsWithTrunc(sb32 *truncated_out)
{
	sb32 local_truncated;
	GObj *sorted[SYNETRB_SNAPSHOT_MAX_EFFECTS + 1];

	local_truncated = FALSE;
	return syNetRbEnumerateActiveEffectsSorted(
	    sorted, (s32)(sizeof(sorted) / sizeof(sorted[0])), (truncated_out != NULL) ? truncated_out : &local_truncated);
}

sb32 syNetRbSnapshotSynctestProbeEffectMismatch(u32 probe_tick)
{
	SYNetRbSnapshotSlot *slot;
	sb32 truncated;
	s32 live_count;

	slot = syNetRbSnapshotSlotForTick(probe_tick);
	if ((slot == NULL) || (slot->is_valid == FALSE) || (slot->tick != probe_tick))
	{
		return FALSE;
	}
	live_count = syNetRbSnapProbeCountEffectsWithTrunc(&truncated);
	return ((truncated != FALSE) || (live_count != slot->effect_count)) ? TRUE : FALSE;
}

sb32 syNetRbSnapshotSynctestProbeMapMismatch(u32 probe_tick)
{
	SYNetRbSnapshotSlot *slot;
	s32 live_n;
	sb32 truncated;

	slot = syNetRbSnapshotSlotForTick(probe_tick);
	if ((slot == NULL) || (slot->is_valid == FALSE) || (slot->tick != probe_tick))
	{
		return FALSE;
	}
	if (slot->mp_bounds_captured == FALSE)
	{
		return FALSE;
	}
	live_n = gMPCollisionYakumonosNum;
	if (live_n < 0)
	{
		live_n = 0;
	}
	truncated = FALSE;
	if (live_n > SYNETRB_SNAPSHOT_MAX_YAKU)
	{
		truncated = TRUE;
		live_n = SYNETRB_SNAPSHOT_MAX_YAKU;
	}
	return ((truncated != FALSE) || (live_n != slot->mp_yakumono_count) ||
	        (slot->mp_yaku_captured == FALSE))
	           ? TRUE
	           : FALSE;
}

static u32 sSYNetRbSnapshotLastCommittedTick;
static u32 sSYNetRbSnapshotLastLoadSafeTick;

#endif /* PORT */

void syNetRbSnapshotSetRingFramesForSession(u32 frames)
{
#ifdef PORT
	if (frames < 1U)
	{
		frames = 1U;
	}
	if (frames > SYNETRB_SNAPSHOT_RING_MAX)
	{
		frames = SYNETRB_SNAPSHOT_RING_MAX;
	}
	sSYNetRbSnapshotRingLen = frames;
#else
	(void)frames;
#endif
}

void syNetRbSnapshotInit(void)
{
#ifdef PORT
	char *env;
	s32 v;

	sSYNetRbSnapshotRingLen = SYNETRB_SNAPSHOT_RING_DEFAULT;
	env = getenv("SSB64_NETPLAY_ROLLBACK_SNAPSHOT_FRAMES");
	if (env != NULL)
	{
		v = atoi(env);
		if (v >= 1)
		{
			sSYNetRbSnapshotRingLen = (u32)v;
		}
	}
	if (sSYNetRbSnapshotRingLen > SYNETRB_SNAPSHOT_RING_MAX)
	{
		sSYNetRbSnapshotRingLen = SYNETRB_SNAPSHOT_RING_MAX;
	}
	port_log("SSB64 NetRbSnapshot: ring_frames=%u (env SSB64_NETPLAY_ROLLBACK_SNAPSHOT_FRAMES)\n",
	         (unsigned int)sSYNetRbSnapshotRingLen);
#else
	(void)0;
#endif
}

void syNetRbSnapshotResetSession(void)
{
#ifdef PORT
	u32 i;

	for (i = 0; i < SYNETRB_SNAPSHOT_RING_MAX; i++)
	{
		sSYNetRbSnapshotRing[i].is_valid = FALSE;
		sSYNetRbSnapshotRing[i].is_load_safe = FALSE;
		sSYNetRbSnapshotRing[i].tick = ~(u32)0;
	}
	sSYNetRbSnapshotLastCommittedTick = ~(u32)0;
	sSYNetRbSnapshotLastLoadSafeTick = ~(u32)0;
	sSYNetRbSnapshotGuardLogBudget = 16;
	sSYNetRbEmergencyValid = FALSE;
	memset(&sSYNetRbEmergencySlot, 0, sizeof(sSYNetRbEmergencySlot));
	sSYNetRbSnapWeaponApplyTick = ~(u32)0;
	sSYNetRbSnapWeaponApplyPendingEject = FALSE;
	sSYNetRbSnapWeaponApplyMatchedCount = 0;
	sSYNetRbSnapWeaponApplyRespawnedCount = 0;
	memset(sSYNetRbSnapWeaponApplyMatched, 0, sizeof(sSYNetRbSnapWeaponApplyMatched));
	wpManagerResetInstanceIds();
#else
	(void)0;
#endif
}

u32 syNetRbSnapshotRingCapacity(void)
{
#ifdef PORT
	return sSYNetRbSnapshotRingLen;
#else
	return 0;
#endif
}

static sb32 syNetRbSnapFillSlotFromLive(SYNetRbSnapshotSlot *slot, u32 completed_sim_tick)
{
	GObj *fighter_gobj;

#ifdef PORT
	syNetSyncReconcileBattleTimePassedForSimTick(completed_sim_tick);
#endif
	memset(slot, 0, sizeof(*slot));
	slot->tick = completed_sim_tick;
	slot->is_valid = TRUE;
	slot->is_load_safe = TRUE;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		s32 slot_index;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			syNetRbSnapLogSkippedGObj("save", "fighter", fighter_gobj, completed_sim_tick);
			continue;
		}
		slot_index = fp->player;
		if ((slot_index >= 0) && (slot_index < GMCOMMON_PLAYERS_MAX))
		{
			syNetRbSnapCaptureFighter(&slot->fighters[slot_index], fp, fighter_gobj);
		}
	}

	syNetRbSnapCaptureMap(slot);
	syNetRbSnapCaptureGround(slot);
	syNetRbSnapCaptureWorld(&slot->world);
	if (syNetRbSnapCaptureItems(slot) == FALSE)
	{
		slot->is_valid = FALSE;
		return FALSE;
	}
	if (syNetRbSnapCaptureWeapons(slot) == FALSE)
	{
		slot->is_valid = FALSE;
		return FALSE;
	}
#ifdef PORT
	if (syNetRbSnapCaptureEffects(slot) == FALSE)
	{
		slot->is_valid = FALSE;
		return FALSE;
	}
#endif
	syNetRbSnapBackfillFighterCoupledIdsFromWeapons(slot);
	syNetRbSnapCaptureCamera(&slot->camera);

	slot->hash_fighter = syNetSyncHashBattleFightersFull();
	slot->hash_world = syNetSyncHashRollbackWorld();
	slot->hash_item = syNetSyncHashActiveItemsForRollback();
	slot->hash_weapon = syNetSyncHashActiveWeaponsForRollback();
	slot->hash_map = syNetSyncHashMapCollisionKinematics();
#ifdef PORT
	{
		u32 ground_hash = syNetRbSnapshotFoldGroundHash(slot);

		slot->hash_map = syNetRbSnapFnvAccumulateU32(slot->hash_map ^ ground_hash, 0x47524F55U);
	}
#endif
	slot->hash_rng = syNetSyncHashRNGSeed();
	slot->hash_camera = syNetSyncHashGMCamera();
	slot->hash_animation = syNetSyncHashFighterAnimationStateForRollback();
#ifdef PORT
	slot->hash_effect = syNetSyncHashActiveEffectsForRollback();
#endif

	return TRUE;
}

static void syNetRbSnapApplySlotToLive(const SYNetRbSnapshotSlot *slot)
{
	GObj *fighter_gobj;

	/*
	 * Mirror syNetRbSnapFillSlotFromLive capture order (fighters before map/world) so MPColl/floor state
	 * applied from map does not run before fighter joint/coll restore — avoids LOAD_HASH_DRIFT on figh.
	 * Geometry-dependent finalize (presentation → joint anim reapply → grab coupling geometry →
	 * coupled pointer rebind → weapon hit refresh) runs in syNetRbSnapshotFinalizeLoad before load-hash verify. Coupled weapon geometry refresh
	 * (Yoshi egg vectors, Samus charge shot position) is deferred until emergency restore; forward sim
	 * status physics restores geometry on the next tick. ftMainRebindStatusProcs runs only after successful
	 * verify (syNetRbSnapshotRebindAllFighters).
	 */
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		s32 slot_index;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			syNetRbSnapLogSkippedGObj("load", "fighter", fighter_gobj, slot->tick);
			continue;
		}
		slot_index = fp->player;
		if ((slot_index >= 0) && (slot_index < GMCOMMON_PLAYERS_MAX))
		{
			syNetRbSnapApplyFighter(&slot->fighters[slot_index], fp, fighter_gobj);
		}
	}

	syNetRbSnapApplyMap(slot);
	syNetRbSnapApplyGround(slot);
	syNetRbSnapApplyWorld(&slot->world, slot->tick);
#ifdef PORT
	syNetRbSnapResetParticlesForRollback();
	syNetRbSnapReconcileSnapshotEffectsBeforeItems(slot);
	{
		GObj *fighter_gobj_re;

		for (fighter_gobj_re = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj_re != NULL;
		     fighter_gobj_re = fighter_gobj_re->link_next)
		{
			FTStruct *fp_re;
			s32 pidx;

			fp_re = ftGetStruct(fighter_gobj_re);
			if (fp_re == NULL)
			{
				continue;
			}
			pidx = fp_re->player;
			if ((pidx >= 0) && (pidx < GMCOMMON_PLAYERS_MAX))
			{
				syNetRbSnapRebindFighterEffectGobjs(&slot->fighters[pidx], fp_re);
			}
		}
		syNetRbSnapFinalizeFighterEffectAttachFlags(slot);
	}
#endif
	syNetRbSnapApplyItems(slot);
	syNetRbSnapRebindAllFighterMPCollPointers();
	syNetRbSnapApplyWeapons(slot);
#ifdef PORT
	syNetRbSnapRebindFighterCoupledGObjs(slot, FALSE);
	syNetRbSnapRebindFighterGrabCoupling();
	syNetRbSnapRebindFighterItemHoldCoupling();
#endif
	syNetRbSnapApplyCamera(&slot->camera);
#ifdef PORT
	syNetRbSnapshotGObjLinkAudit(slot->tick);
#endif
}

sb32 syNetRbSnapshotCaptureLiveEmergency(void)
{
	if (syNetRbSnapFillSlotFromLive(&sSYNetRbEmergencySlot, 0xFFFFFFFFU) == FALSE)
	{
		sSYNetRbEmergencyValid = FALSE;
		return FALSE;
	}
	sSYNetRbEmergencyValid = TRUE;
	return TRUE;
}

sb32 	syNetRbSnapshotRestoreLiveEmergency(void)
{
	if (sSYNetRbEmergencyValid == FALSE)
	{
		return FALSE;
	}
	syNetRbSnapApplySlotToLive(&sSYNetRbEmergencySlot);
	syNetRbSnapshotFinalizeLoadFromSlot(&sSYNetRbEmergencySlot, TRUE, TRUE);
	syNetRbSnapshotRebindAllFighters();
	sSYNetRbEmergencyValid = FALSE;
	return TRUE;
}

sb32 syNetRbSnapshotSave(u32 completed_sim_tick)
{
	return syNetRbSnapshotSaveMarked(completed_sim_tick, TRUE);
}

sb32 syNetRbSnapshotSaveMarked(u32 completed_sim_tick, sb32 is_load_safe)
{
#ifdef PORT
	SYNetRbSnapshotSlot *slot;

	slot = syNetRbSnapshotSlotForTick(completed_sim_tick);
	if (syNetRbSnapFillSlotFromLive(slot, completed_sim_tick) == FALSE)
	{
		return FALSE;
	}
	slot->is_load_safe = is_load_safe;
	if (completed_sim_tick > sSYNetRbSnapshotLastCommittedTick)
	{
		sSYNetRbSnapshotLastCommittedTick = completed_sim_tick;
	}
	if ((is_load_safe != FALSE) && (completed_sim_tick > sSYNetRbSnapshotLastLoadSafeTick))
	{
		sSYNetRbSnapshotLastLoadSafeTick = completed_sim_tick;
	}
	return TRUE;
#else
	(void)completed_sim_tick;
	(void)is_load_safe;
	return FALSE;
#endif
}

sb32 syNetRbSnapshotLoad(u32 completed_sim_tick)
{
#ifdef PORT
	SYNetRbSnapshotSlot *slot;

	slot = syNetRbSnapshotSlotForTick(completed_sim_tick);
	if ((slot->is_valid == FALSE) || (slot->tick != completed_sim_tick))
	{
		return FALSE;
	}

	syNetRbSnapApplySlotToLive(slot);
	return TRUE;
#else
	(void)completed_sim_tick;
	return FALSE;
#endif
}

#ifdef PORT
static sb32 syNetRbSnapFighterCleanupForceLegacySetStatus(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (sb32)s_env_cache;
	}
	s_env_cache = 0;
	e = getenv("SSB64_NETPLAY_SNAPSHOT_FIGHTER_CLEANUP");
	if ((e != NULL) &&
	    ((strcmp(e, "force") == 0) || (strcmp(e, "full") == 0) || (strcmp(e, "1") == 0)))
	{
		s_env_cache = 1;
	}
	return (sb32)s_env_cache;
}
#endif /* PORT */

/*
 * Figatree presentation sync (default: ftMainRefreshFigatreeVisual only).
 * Invoked from syNetRbSnapshotFinalizeLoad before load-hash verify. Figatree attach may clobber joint AObj
 * chains; syNetRbSnapReapplyFighterJointAnimFromSlot restores blob anim before verify. Coupled weapon
 * geometry refresh is skipped pre-verify — weapon blobs already carry DObj transforms at save time.
 *
 * SSB64_NETPLAY_SNAPSHOT_FIGHTER_CLEANUP=force|full|1 — legacy ftMainSetStatus path for bisect only.
 */
static void syNetRbSnapInvalidateFighterPartTransformCaches(GObj *fighter_gobj)
{
	FTStruct *fp;
	s32 ji;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp == NULL)
	{
		return;
	}
	for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
	{
		DObj *joint;
		FTParts *parts;

		joint = fp->joints[ji];
		if (joint == NULL)
		{
			continue;
		}
		parts = ftGetParts(joint);
		if (parts != NULL)
		{
			parts->transform_update_mode = 0;
		}
	}
}

/*
 * lbCommonAddFighterPartsFigatree (via ftMainRefreshFigatreeVisual) installs figatree anim joints and
 * clobbers per-joint AObj chains restored from the snapshot blob. Re-apply blob joint anim before load-hash
 * verify so anim/wpn partitions match ring slot digests.
 */
static void syNetRbSnapReapplyFighterJointAnimFromSlot(const SYNetRbSnapshotSlot *slot)
{
	GObj *fighter_gobj;

	if (slot == NULL)
	{
		return;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		s32 slot_index;
		const SYNetRbSnapFighterBlob *blob;
		s32 ji;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			continue;
		}
		slot_index = fp->player;
		if ((slot_index < 0) || (slot_index >= GMCOMMON_PLAYERS_MAX))
		{
			continue;
		}
		blob = &slot->fighters[slot_index];
		if (blob->is_valid == FALSE)
		{
			continue;
		}
		for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
		{
			if (fp->joints[ji] != NULL)
			{
				syNetRbSnapApplyDObjAnim(fp->joints[ji], &blob->joint_anim[ji]);
				if (blob->joint_anim_joint_event32[ji] != 0U)
				{
					fp->joints[ji]->anim_joint.event32 = (AObjEvent32 *)blob->joint_anim_joint_event32[ji];
				}
				else
				{
					fp->joints[ji]->anim_joint.event32 = NULL;
				}
			}
		}
		fighter_gobj->anim_frame = blob->gobj_anim_frame;
		syNetRbSnapInvalidateFighterPartTransformCaches(fighter_gobj);
	}
}

void syNetRbSnapshotReapplyJointAnimAtTick(u32 completed_sim_tick)
{
#ifdef PORT
	SYNetRbSnapshotSlot *slot;

	slot = syNetRbSnapshotSlotForTick(completed_sim_tick);
	if ((slot != NULL) && (slot->is_valid != FALSE) && (slot->tick == completed_sim_tick))
	{
		syNetRbSnapReapplyFighterJointAnimFromSlot(slot);
	}
#else
	(void)completed_sim_tick;
#endif
}

void syNetRbSnapshotSyncFighterPresentation(void)
{
#ifdef PORT
	GObj *fighter_gobj;

	if (syNetRbSnapFighterCleanupForceLegacySetStatus() != FALSE)
	{
		u32 preserve_flags = FTSTATUS_PRESERVE_HIT | FTSTATUS_PRESERVE_COLANIM | FTSTATUS_PRESERVE_EFFECT |
		                     FTSTATUS_PRESERVE_FASTFALL | FTSTATUS_PRESERVE_HITSTATUS |
		                     FTSTATUS_PRESERVE_MODELPART | FTSTATUS_PRESERVE_SLOPECONTOUR |
		                     FTSTATUS_PRESERVE_TEXTUREPART | FTSTATUS_PRESERVE_PLAYERTAG |
		                     FTSTATUS_PRESERVE_THROWPOINTER | FTSTATUS_PRESERVE_SHUFFLETIME |
		                     FTSTATUS_PRESERVE_LOOPSFX | FTSTATUS_PRESERVE_DAMAGEPLAYER |
		                     FTSTATUS_PRESERVE_AFTERIMAGE | FTSTATUS_PRESERVE_RUMBLE;

		for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
		     fighter_gobj = fighter_gobj->link_next)
		{
			FTStruct *fp = ftGetStruct(fighter_gobj);

			if (fp != NULL)
			{
				ftMainSetStatus(fighter_gobj, fp->status_id, fighter_gobj->anim_frame, 1.0F, preserve_flags);
			}
		}
		return;
	}

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		ftMainRefreshFigatreeVisual(fighter_gobj);
	}
#else
	(void)0;
#endif
}

#ifdef PORT
static void syNetRbSnapshotFinalizeLoadCouplingFromSlot(const SYNetRbSnapshotSlot *slot,
                                                        sb32 refresh_coupled_weapon_geometry)
{
	if ((slot == NULL) || (slot->is_valid == FALSE))
	{
		return;
	}
	syNetRbSnapRebindFighterCoupledGObjs(slot, refresh_coupled_weapon_geometry);
	syNetRbSnapRefreshWeaponHitPositions();
}

static void syNetRbSnapshotFinalizeLoadFromSlot(const SYNetRbSnapshotSlot *slot, sb32 sync_presentation,
                                                sb32 refresh_coupled_weapon_geometry)
{
	if ((slot == NULL) || (slot->is_valid == FALSE))
	{
		return;
	}
	if (sync_presentation != FALSE)
	{
		syNetRbSnapshotSyncFighterPresentation();
		syNetRbSnapReapplyFighterJointAnimFromSlot(slot);
		syNetRbSnapshotRefreshGrabCouplingGeometry();
	}
	syNetRbSnapshotFinalizeLoadCouplingFromSlot(slot, refresh_coupled_weapon_geometry);
	syNetRbSnapEjectUnmatchedWeaponsAfterCoupling(slot);
}

void syNetRbSnapshotFinalizeLoadCoupling(u32 completed_sim_tick)
{
	SYNetRbSnapshotSlot *slot;

	slot = syNetRbSnapshotSlotForTick(completed_sim_tick);
	if ((slot == NULL) || (slot->is_valid == FALSE) || (slot->tick != completed_sim_tick))
	{
		return;
	}
	syNetRbSnapshotFinalizeLoadCouplingFromSlot(slot, TRUE);
}

void syNetRbSnapshotFinalizeLoad(u32 completed_sim_tick)
{
	SYNetRbSnapshotSlot *slot;

	slot = syNetRbSnapshotSlotForTick(completed_sim_tick);
	if ((slot == NULL) || (slot->is_valid == FALSE) || (slot->tick != completed_sim_tick))
	{
		return;
	}
	syNetRbSnapshotFinalizeLoadFromSlot(slot, TRUE, FALSE);
}

void syNetRbSnapCullYoshiChargeEggsForFighter(GObj *fighter_gobj, GObj *keep_egg_gobj)
{
	GObj *weapon_gobj;
	FTStruct *fp;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp == NULL)
	{
		return;
	}
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;)
	{
		GObj *next_gobj;
		WPStruct *wp;

		next_gobj = weapon_gobj->link_next;
		wp = wpGetStruct(weapon_gobj);
		if ((wp != NULL) && (wp->kind == nWPKindEggThrow) && (weapon_gobj != keep_egg_gobj) &&
		    (syNetRbSnapWeaponOwnedByFighterGObj(wp, fighter_gobj) != FALSE) &&
		    (syNetRbSnapWeaponEggIsCharging(wp, fighter_gobj) != FALSE))
		{
			if (fp->status_vars.yoshi.specialhi.egg_gobj == weapon_gobj)
			{
				fp->status_vars.yoshi.specialhi.egg_gobj = NULL;
			}
			wpMainDestroyWeapon(weapon_gobj);
		}
		weapon_gobj = next_gobj;
	}
}

GObj *syNetRbSnapReacquireYoshiChargeEgg(GObj *fighter_gobj)
{
	return syNetRbSnapFindLiveWeaponForOwner(fighter_gobj, nWPKindEggThrow, syNetRbSnapWeaponEggIsCharging);
}

static GObj *syNetRbSnapFighterGObjFromFP(const FTStruct *fp)
{
	GObj *fighter_gobj;

	if (fp == NULL)
	{
		return NULL;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		if (ftGetStruct(fighter_gobj) == fp)
		{
			return fighter_gobj;
		}
	}
	return NULL;
}

GObj *syNetRbSnapReacquireChargeShotForFP(FTStruct *fp)
{
	GObj *fighter_gobj;

	fighter_gobj = syNetRbSnapFighterGObjFromFP(fp);
	if (fighter_gobj == NULL)
	{
		return NULL;
	}
	return syNetRbSnapFindLiveWeaponForOwner(fighter_gobj, nWPKindChargeShot, syNetRbSnapWeaponChargeShotIsCharging);
}

GObj *syNetRbSnapReacquireFireballForFighter(GObj *fighter_gobj)
{
	return syNetRbSnapFindLiveWeaponForOwner(fighter_gobj, nWPKindFireball, NULL);
}

sb32 syNetRbSnapFireballOwnedByFighter(GObj *fighter_gobj)
{
	GObj *weapon_gobj;

	if (fighter_gobj == NULL)
	{
		return FALSE;
	}
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp = wpGetStruct(weapon_gobj);

		if ((wp != NULL) && (wp->kind == nWPKindFireball) &&
		    (syNetRbSnapWeaponOwnedByFighterGObj(wp, fighter_gobj) != FALSE))
		{
			return TRUE;
		}
	}
	return FALSE;
}

sb32 syNetRbSnapFireballNeedsSpawnAtHand(GObj *fighter_gobj, const Vec3f *spawn_pos)
{
	GObj *weapon_gobj;
	const f32 pos_thresh_sq = 3600.0F;

	if ((fighter_gobj == NULL) || (spawn_pos == NULL))
	{
		return TRUE;
	}
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp;
		DObj *dobj;
		f32 dx;
		f32 dy;
		f32 dz;
		f32 dist_sq;

		wp = wpGetStruct(weapon_gobj);
		if ((wp == NULL) || (wp->kind != nWPKindFireball) ||
		    (syNetRbSnapWeaponOwnedByFighterGObj(wp, fighter_gobj) == FALSE))
		{
			continue;
		}
		dobj = DObjGetStruct(weapon_gobj);
		if (dobj == NULL)
		{
			return FALSE;
		}
		dx = dobj->translate.vec.f.x - spawn_pos->x;
		dy = dobj->translate.vec.f.y - spawn_pos->y;
		dz = dobj->translate.vec.f.z - spawn_pos->z;
		dist_sq = (dx * dx) + (dy * dy) + (dz * dz);
		if (dist_sq <= pos_thresh_sq)
		{
			return FALSE;
		}
	}
	return TRUE;
}

GObj *syNetRbSnapReacquireFireballAtHand(GObj *fighter_gobj, const Vec3f *pos, f32 radius_sq)
{
	GObj *weapon_gobj;
	GObj *best_gobj;
	f32 best_dist_sq;

	if ((fighter_gobj == NULL) || (pos == NULL))
	{
		return NULL;
	}
	best_gobj = NULL;
	best_dist_sq = F32_MAX;
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp;
		DObj *dobj;
		f32 dx;
		f32 dy;
		f32 dz;
		f32 dist_sq;

		wp = wpGetStruct(weapon_gobj);
		if ((wp == NULL) || (wp->kind != nWPKindFireball) ||
		    (syNetRbSnapWeaponOwnedByFighterGObj(wp, fighter_gobj) == FALSE))
		{
			continue;
		}
		dobj = DObjGetStruct(weapon_gobj);
		if (dobj == NULL)
		{
			continue;
		}
		dx = dobj->translate.vec.f.x - pos->x;
		dy = dobj->translate.vec.f.y - pos->y;
		dz = dobj->translate.vec.f.z - pos->z;
		dist_sq = (dx * dx) + (dy * dy) + (dz * dz);
		if ((dist_sq <= radius_sq) && (dist_sq < best_dist_sq))
		{
			best_dist_sq = dist_sq;
			best_gobj = weapon_gobj;
		}
	}
	return best_gobj;
}

void syNetRbSnapCullOwnedFireballsNearPose(GObj *fighter_gobj, GObj *keep_fireball_gobj, const Vec3f *pos, f32 radius_sq)
{
	GObj *weapon_gobj;

	if ((fighter_gobj == NULL) || (pos == NULL))
	{
		return;
	}
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;)
	{
		GObj *next_gobj;
		WPStruct *wp;
		DObj *dobj;
		f32 dx;
		f32 dy;
		f32 dz;
		f32 dist_sq;

		next_gobj = weapon_gobj->link_next;
		wp = wpGetStruct(weapon_gobj);
		if ((wp == NULL) || (wp->kind != nWPKindFireball) || (weapon_gobj == keep_fireball_gobj) ||
		    (syNetRbSnapWeaponOwnedByFighterGObj(wp, fighter_gobj) == FALSE))
		{
			weapon_gobj = next_gobj;
			continue;
		}
		dobj = DObjGetStruct(weapon_gobj);
		if (dobj == NULL)
		{
			wpMainDestroyWeapon(weapon_gobj);
			weapon_gobj = next_gobj;
			continue;
		}
		dx = dobj->translate.vec.f.x - pos->x;
		dy = dobj->translate.vec.f.y - pos->y;
		dz = dobj->translate.vec.f.z - pos->z;
		dist_sq = (dx * dx) + (dy * dy) + (dz * dz);
		if (dist_sq <= radius_sq)
		{
			wpMainDestroyWeapon(weapon_gobj);
		}
		weapon_gobj = next_gobj;
	}
}

#define SYNETRB_SNAP_FIREBALL_EMERGENCY_FRAME       3.0F
#define SYNETRB_SNAP_FIREBALL_HAND_RADIUS_SQ        3600.0F
#define SYNETRB_SNAP_FIREBALL_THROW_PRESERVE_FRAMES 25.0F

static void syNetRbSnapFireballSpawnDiag(const char *reason, const FTStruct *fp, f32 anim_frame)
{
	if (syNetRbSnapWeaponDiagEnabled() != FALSE)
	{
		if (fp == NULL)
		{
			port_log("SSB64 NetRbSnapshot: fireball_spawn skip=%s\n", reason);
		}
		else if (fp->fkind == nFTKindKirby)
		{
			port_log(
			    "SSB64 NetRbSnapshot: fireball_spawn skip=%s owner_player=%d status=%d copy_id=%d anim_frame=%.1f\n",
			    reason, (int)fp->player, (int)fp->status_id, (int)fp->passive_vars.kirby.copy_id, anim_frame);
		}
		else
		{
			port_log("SSB64 NetRbSnapshot: fireball_spawn skip=%s owner_player=%d status=%d anim_frame=%.1f\n",
			         reason, (int)fp->player, (int)fp->status_id, anim_frame);
		}
	}
}

static sb32 syNetRbSnapFighterIsInFireballThrowStatus(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	switch (fp->fkind)
	{
	case nFTKindMario:
	case nFTKindMMario:
	case nFTKindNMario:
	case nFTKindLuigi:
	case nFTKindNLuigi:
		switch (fp->status_id)
		{
		/* Mario + Luigi SpecialN/AirN share numeric status IDs (parallel enums). */
		case nFTMarioStatusSpecialN:
		case nFTMarioStatusSpecialAirN:
			return TRUE;

		default:
			break;
		}
		break;

	case nFTKindKirby:
		switch (fp->status_id)
		{
		case nFTKirbyStatusCopyMarioSpecialN:
		case nFTKirbyStatusCopyMarioSpecialAirN:
		case nFTKirbyStatusCopyLuigiSpecialN:
		case nFTKirbyStatusCopyLuigiSpecialAirN:
			return TRUE;

		default:
			break;
		}
		break;

	default:
		break;
	}
	return FALSE;
}

static s32 syNetRbSnapFireballSpawnJointForFighter(const FTStruct *fp)
{
	if ((fp != NULL) && (fp->fkind == nFTKindKirby))
	{
		return FTKIRBY_COPYMARIO_FIREBALL_SPAWN_JOINT;
	}
	return FTMARIO_FIREBALL_SPAWN_JOINT;
}

static sb32 syNetRbSnapResolveFireballIndex(FTStruct *fp, s32 *fireball_index_out)
{
	if ((fp == NULL) || (fireball_index_out == NULL))
	{
		return FALSE;
	}
	switch (fp->fkind)
	{
	case nFTKindMario:
	case nFTKindMMario:
	case nFTKindNMario:
		*fireball_index_out = 0;
		return TRUE;

	case nFTKindLuigi:
	case nFTKindNLuigi:
		*fireball_index_out = 1;
		return TRUE;

	case nFTKindKirby:
		switch (fp->passive_vars.kirby.copy_id)
		{
		case nFTKindMario:
		case nFTKindMMario:
		case nFTKindNMario:
			*fireball_index_out = 0;
			return TRUE;

		case nFTKindLuigi:
		case nFTKindNLuigi:
			*fireball_index_out = 1;
			return TRUE;

		default:
			break;
		}
		switch (fp->status_id)
		{
		case nFTKirbyStatusCopyMarioSpecialN:
		case nFTKirbyStatusCopyMarioSpecialAirN:
			*fireball_index_out = 0;
			return TRUE;

		case nFTKirbyStatusCopyLuigiSpecialN:
		case nFTKirbyStatusCopyLuigiSpecialAirN:
			*fireball_index_out = 1;
			return TRUE;

		default:
			return FALSE;
		}

	default:
		return FALSE;
	}
}

static void syNetRbSnapCullFarthestOwnedFireballForFighter(GObj *fighter_gobj, const Vec3f *ref_pos)
{
	GObj *weapon_gobj;
	GObj *farthest_gobj;
	f32 farthest_dist_sq;

	if ((fighter_gobj == NULL) || (ref_pos == NULL))
	{
		return;
	}
	farthest_gobj = NULL;
	farthest_dist_sq = -1.0F;
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp;
		DObj *dobj;
		f32 dx;
		f32 dy;
		f32 dz;
		f32 dist_sq;

		wp = wpGetStruct(weapon_gobj);
		if ((wp == NULL) || (wp->kind != nWPKindFireball) ||
		    (syNetRbSnapWeaponOwnedByFighterGObj(wp, fighter_gobj) == FALSE))
		{
			continue;
		}
		dobj = DObjGetStruct(weapon_gobj);
		if (dobj == NULL)
		{
			continue;
		}
		dx = dobj->translate.vec.f.x - ref_pos->x;
		dy = dobj->translate.vec.f.y - ref_pos->y;
		dz = dobj->translate.vec.f.z - ref_pos->z;
		dist_sq = (dx * dx) + (dy * dy) + (dz * dz);
		if (dist_sq > farthest_dist_sq)
		{
			farthest_dist_sq = dist_sq;
			farthest_gobj = weapon_gobj;
		}
	}
	if (farthest_gobj != NULL)
	{
		wpMainDestroyWeapon(farthest_gobj);
	}
}

static sb32 syNetRbSnapFireballNearFighterHand(GObj *fighter_gobj, GObj *fireball_gobj, f32 radius_sq)
{
	FTStruct *fp;
	Vec3f hand_pos;
	DObj *hand_dobj;
	DObj *fireball_dobj;
	f32 dx;
	f32 dy;
	f32 dz;
	s32 spawn_joint;

	if ((fighter_gobj == NULL) || (fireball_gobj == NULL))
	{
		return FALSE;
	}
	fp = ftGetStruct(fighter_gobj);
	fireball_dobj = DObjGetStruct(fireball_gobj);
	if ((fp == NULL) || (fireball_dobj == NULL))
	{
		return FALSE;
	}
	spawn_joint = syNetRbSnapFireballSpawnJointForFighter(fp);
	hand_pos.x = 0.0F;
	hand_pos.y = 0.0F;
	hand_pos.z = 0.0F;
	hand_dobj = fp->joints[spawn_joint];
	if (hand_dobj == NULL)
	{
		return FALSE;
	}
	gmCollisionGetFighterPartsWorldPosition(hand_dobj, &hand_pos);
	dx = fireball_dobj->translate.vec.f.x - hand_pos.x;
	dy = fireball_dobj->translate.vec.f.y - hand_pos.y;
	dz = fireball_dobj->translate.vec.f.z - hand_pos.z;
	return (((dx * dx) + (dy * dy) + (dz * dz)) <= radius_sq) ? TRUE : FALSE;
}

static sb32 syNetRbSnapLiveWeaponIsFireballThrowPreserve(GObj *weapon_gobj)
{
	WPStruct *wp;
	GObj *owner_gobj;
	FTStruct *fp;

	wp = wpGetStruct(weapon_gobj);
	if ((wp == NULL) || (wp->kind != nWPKindFireball))
	{
		return FALSE;
	}
	owner_gobj = wp->owner_gobj;
	if ((owner_gobj == NULL) || (syNetRbSnapWeaponOwnedByFighterGObj(wp, owner_gobj) == FALSE))
	{
		return FALSE;
	}
	fp = ftGetStruct(owner_gobj);
	if (syNetRbSnapFighterIsInFireballThrowStatus(fp) == FALSE)
	{
		return FALSE;
	}
	if (fp->motion_vars.flags.flag1 == 0)
	{
		return TRUE;
	}
	if (owner_gobj->anim_frame < SYNETRB_SNAP_FIREBALL_THROW_PRESERVE_FRAMES)
	{
		return TRUE;
	}
	return syNetRbSnapFireballNearFighterHand(owner_gobj, weapon_gobj, SYNETRB_SNAP_FIREBALL_HAND_RADIUS_SQ);
}

sb32 syNetRbSnapFireballProcAccessoryWillRun(GObj *fighter_gobj)
{
	FTStruct *fp;

	if (fighter_gobj == NULL)
	{
		return FALSE;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp == NULL)
	{
		return FALSE;
	}
	if (((fp->capture_gobj == NULL) || (fp->is_catch_or_capture != FALSE)) &&
	    ((fp->catch_gobj == NULL) || (fp->is_catch_or_capture == FALSE)))
	{
		return TRUE;
	}
	if (((fp->capture_gobj != NULL) && (fp->is_catch_or_capture == FALSE)) ||
	    ((fp->catch_gobj != NULL) && (fp->is_catch_or_capture != FALSE)))
	{
		return TRUE;
	}
	return FALSE;
}

/*
 * Emergency spawn replaces missed anim flag0 after rollback load/resim, or when ProcUpdate
 * fallback runs because proc_accessory is skipped (physics-map gap). Forward sim with a
 * live accessory proc uses anim flag0 only — avoids emergency+anim double spawn per B.
 */
static sb32 syNetRbSnapFireballEmergencySpawnAllowed(GObj *fighter_gobj)
{
	FTStruct *fp;

	if (syNetRollbackIsResimulating() != FALSE)
	{
		return TRUE;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp != NULL) && (fp->proc_accessory == NULL))
	{
		return TRUE;
	}
	if (syNetRbSnapFireballProcAccessoryWillRun(fighter_gobj) == FALSE)
	{
		return TRUE;
	}
	return FALSE;
}

void syNetRbSnapTrySpawnFireballFromAccessory(GObj *fighter_gobj)
{
	FTStruct *fp;
	Vec3f pos;
	s32 fireball_index;
	s32 spawn_joint;
	sb32 should_spawn;
	GObj *fireball_gobj;
	const char *spawn_path;
	f32 anim_frame;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp == NULL)
	{
		return;
	}
	anim_frame = fighter_gobj->anim_frame;

	if (fp->motion_vars.flags.flag1 != 0)
	{
		syNetRbSnapFireballSpawnDiag("latched", fp, anim_frame);
		return;
	}

	should_spawn = FALSE;
	spawn_path = NULL;

	if (fp->motion_vars.flags.flag0 != 0)
	{
		fp->motion_vars.flags.flag0 = 0;
		should_spawn = TRUE;
		spawn_path = "anim";
	}
	else if ((anim_frame >= SYNETRB_SNAP_FIREBALL_EMERGENCY_FRAME) &&
	         (syNetRbSnapFireballEmergencySpawnAllowed(fighter_gobj) != FALSE))
	{
		should_spawn = TRUE;
		spawn_path = "emergency";
	}
	if (should_spawn == FALSE)
	{
		syNetRbSnapFireballSpawnDiag("wait_frame", fp, anim_frame);
		return;
	}
	if (syNetRbSnapResolveFireballIndex(fp, &fireball_index) == FALSE)
	{
		syNetRbSnapFireballSpawnDiag("resolve_index", fp, anim_frame);
		return;
	}

	spawn_joint = syNetRbSnapFireballSpawnJointForFighter(fp);
	if (fp->joints[spawn_joint] == NULL)
	{
		syNetRbSnapFireballSpawnDiag("spawn_joint_null", fp, anim_frame);
		return;
	}
	pos.x = 0.0F;
	pos.y = 0.0F;
	pos.z = 0.0F;
	gmCollisionGetFighterPartsWorldPosition(fp->joints[spawn_joint], &pos);

	if (syNetRbSnapFireballNeedsSpawnAtHand(fighter_gobj, &pos) == FALSE)
	{
		fireball_gobj = syNetRbSnapReacquireFireballAtHand(fighter_gobj, &pos, SYNETRB_SNAP_FIREBALL_HAND_RADIUS_SQ);
		if (fireball_gobj != NULL)
		{
			syNetRbSnapCullOwnedFireballsNearPose(fighter_gobj, fireball_gobj, &pos,
			                                      SYNETRB_SNAP_FIREBALL_HAND_RADIUS_SQ);
			fp->motion_vars.flags.flag1 = 1;
			spawn_path = "reacquire";
		}
		else
		{
			syNetRbSnapFireballSpawnDiag("skip_dedup", fp, anim_frame);
			if (syNetRbSnapWeaponDiagEnabled() != FALSE)
			{
				port_log("SSB64 NetRbSnapshot: fireball_spawn path=skip_dedup owner_player=%d joint=%d\n",
				         (int)fp->player, spawn_joint);
			}
			return;
		}
	}
	else
	{
		fireball_gobj = wpMarioFireballMakeWeapon(fighter_gobj, &pos, fireball_index);
		if (fireball_gobj == NULL)
		{
			syNetRbSnapCullFarthestOwnedFireballForFighter(fighter_gobj, &pos);
			fireball_gobj = wpMarioFireballMakeWeapon(fighter_gobj, &pos, fireball_index);
		}
		if (fireball_gobj == NULL)
		{
			syNetRbSnapFireballSpawnDiag("make_weapon_null", fp, anim_frame);
			return;
		}
		syNetRbSnapCullOwnedFireballsNearPose(fighter_gobj, fireball_gobj, &pos, SYNETRB_SNAP_FIREBALL_HAND_RADIUS_SQ);
		fp->motion_vars.flags.flag1 = 1;
	}
	if (syNetRbSnapWeaponDiagEnabled() != FALSE)
	{
		port_log("SSB64 NetRbSnapshot: fireball_spawn path=%s owner_player=%d joint=%d\n",
		         (spawn_path != NULL) ? spawn_path : "spawn", (int)fp->player, spawn_joint);
	}
}

#define SYNETRB_SNAP_THUNDERJOLT_EMERGENCY_FRAME          21.0F
#define SYNETRB_SNAP_THUNDER_SPAWN_EMERGENCY_FRAME        24.0F
#define SYNETRB_SNAP_THUNDERJOLT_HAND_RADIUS_SQ           3600.0F
#define SYNETRB_SNAP_THUNDERJOLT_THROW_PRESERVE_FRAMES    25.0F

static void syNetRbSnapThunderJoltSpawnDiag(const char *reason, const FTStruct *fp, f32 anim_frame)
{
	if (syNetRbSnapWeaponDiagEnabled() != FALSE)
	{
		if (fp == NULL)
		{
			port_log("SSB64 NetRbSnapshot: jolt_spawn skip=%s\n", reason);
		}
		else if (fp->fkind == nFTKindKirby)
		{
			port_log(
			    "SSB64 NetRbSnapshot: jolt_spawn skip=%s owner_player=%d status=%d copy_id=%d anim_frame=%.1f\n",
			    reason, (int)fp->player, (int)fp->status_id, (int)fp->passive_vars.kirby.copy_id, anim_frame);
		}
		else
		{
			port_log("SSB64 NetRbSnapshot: jolt_spawn skip=%s owner_player=%d status=%d anim_frame=%.1f\n", reason,
			         (int)fp->player, (int)fp->status_id, anim_frame);
		}
	}
}

static sb32 syNetRbSnapFighterIsInThunderJoltThrowStatus(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	switch (fp->fkind)
	{
	case nFTKindPikachu:
		switch (fp->status_id)
		{
		case nFTPikachuStatusSpecialN:
		case nFTPikachuStatusSpecialAirN:
			return TRUE;

		default:
			break;
		}
		break;

	case nFTKindKirby:
		switch (fp->status_id)
		{
		case nFTKirbyStatusCopyPikachuSpecialN:
		case nFTKirbyStatusCopyPikachuSpecialAirN:
			return TRUE;

		default:
			break;
		}
		break;

	default:
		break;
	}
	return FALSE;
}

static sb32 syNetRbSnapFighterIsInThunderSpecialLwStatus(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	switch (fp->status_id)
	{
	case nFTPikachuStatusSpecialLwStart:
	case nFTPikachuStatusSpecialAirLwStart:
		return TRUE;

	default:
		break;
	}
	return FALSE;
}

static s32 syNetRbSnapThunderJoltSpawnJointForFighter(const FTStruct *fp)
{
	if ((fp != NULL) && (fp->fkind == nFTKindKirby))
	{
		return FTKIRBY_COPYPIKACHU_THUNDERJOLT_SPAWN_JOINT;
	}
	return FTPIKACHU_THUNDERJOLT_SPAWN_JOINT;
}

static void syNetRbSnapThunderJoltSpawnPoseForFighter(const FTStruct *fp, Vec3f *pos, Vec3f *vel)
{
	if ((fp == NULL) || (pos == NULL) || (vel == NULL))
	{
		return;
	}
	pos->x = 0.0F;
	pos->y = 0.0F;
	pos->z = 0.0F;
	if (fp->fkind == nFTKindKirby)
	{
		gmCollisionGetFighterPartsWorldPosition(fp->joints[syNetRbSnapThunderJoltSpawnJointForFighter(fp)], pos);
		pos->x += FTKIRBY_COPYPIKACHU_THUNDERJOLT_SPAWN_OFF_X * fp->lr;
		pos->y += FTKIRBY_COPYPIKACHU_THUNDERJOLT_SPAWN_OFF_Y;
		vel->x = __cosf(FTKIRBY_COPYPIKACHU_THUNDERJOLT_SPAWN_ANGLE) * FTKIRBY_COPYPIKACHU_THUNDERJOLTVEL * fp->lr;
		vel->y = __sinf(FTKIRBY_COPYPIKACHU_THUNDERJOLT_SPAWN_ANGLE) * FTKIRBY_COPYPIKACHU_THUNDERJOLTVEL;
	}
	else
	{
		gmCollisionGetFighterPartsWorldPosition(fp->joints[syNetRbSnapThunderJoltSpawnJointForFighter(fp)], pos);
		vel->x = __cosf(FTPIKACHU_THUNDERJOLT_SPAWN_ANGLE) * FTPIKACHU_THUNDERJOLTVEL * fp->lr;
		vel->y = __sinf(FTPIKACHU_THUNDERJOLT_SPAWN_ANGLE) * FTPIKACHU_THUNDERJOLTVEL;
	}
	vel->z = 0.0F;
}

sb32 syNetRbSnapThunderJoltOwnedByFighter(GObj *fighter_gobj)
{
	GObj *weapon_gobj;

	if (fighter_gobj == NULL)
	{
		return FALSE;
	}
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp = wpGetStruct(weapon_gobj);

		if ((wp != NULL) &&
		    ((wp->kind == nWPKindThunderJoltAir) || (wp->kind == nWPKindThunderJoltGround)) &&
		    (syNetRbSnapWeaponOwnedByFighterGObj(wp, fighter_gobj) != FALSE))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapThunderJoltNeedsSpawnAtHand(GObj *fighter_gobj, const Vec3f *spawn_pos)
{
	GObj *weapon_gobj;
	const f32 pos_thresh_sq = SYNETRB_SNAP_THUNDERJOLT_HAND_RADIUS_SQ;

	if ((fighter_gobj == NULL) || (spawn_pos == NULL))
	{
		return TRUE;
	}
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp;
		DObj *dobj;
		f32 dx;
		f32 dy;
		f32 dz;
		f32 dist_sq;

		wp = wpGetStruct(weapon_gobj);
		if ((wp == NULL) ||
		    ((wp->kind != nWPKindThunderJoltAir) && (wp->kind != nWPKindThunderJoltGround)) ||
		    (syNetRbSnapWeaponOwnedByFighterGObj(wp, fighter_gobj) == FALSE))
		{
			continue;
		}
		dobj = DObjGetStruct(weapon_gobj);
		if (dobj == NULL)
		{
			return FALSE;
		}
		dx = dobj->translate.vec.f.x - spawn_pos->x;
		dy = dobj->translate.vec.f.y - spawn_pos->y;
		dz = dobj->translate.vec.f.z - spawn_pos->z;
		dist_sq = (dx * dx) + (dy * dy) + (dz * dz);
		if (dist_sq <= pos_thresh_sq)
		{
			return FALSE;
		}
	}
	return TRUE;
}

static GObj *syNetRbSnapReacquireThunderJoltAtHand(GObj *fighter_gobj, const Vec3f *pos, f32 radius_sq)
{
	GObj *weapon_gobj;
	GObj *best_gobj;
	f32 best_dist_sq;

	if ((fighter_gobj == NULL) || (pos == NULL))
	{
		return NULL;
	}
	best_gobj = NULL;
	best_dist_sq = F32_MAX;
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp;
		DObj *dobj;
		f32 dx;
		f32 dy;
		f32 dz;
		f32 dist_sq;

		wp = wpGetStruct(weapon_gobj);
		if ((wp == NULL) ||
		    ((wp->kind != nWPKindThunderJoltAir) && (wp->kind != nWPKindThunderJoltGround)) ||
		    (syNetRbSnapWeaponOwnedByFighterGObj(wp, fighter_gobj) == FALSE))
		{
			continue;
		}
		dobj = DObjGetStruct(weapon_gobj);
		if (dobj == NULL)
		{
			continue;
		}
		dx = dobj->translate.vec.f.x - pos->x;
		dy = dobj->translate.vec.f.y - pos->y;
		dz = dobj->translate.vec.f.z - pos->z;
		dist_sq = (dx * dx) + (dy * dy) + (dz * dz);
		if ((dist_sq <= radius_sq) && (dist_sq < best_dist_sq))
		{
			best_dist_sq = dist_sq;
			best_gobj = weapon_gobj;
		}
	}
	return best_gobj;
}

static void syNetRbSnapCullOwnedThunderJoltsNearPose(GObj *fighter_gobj, GObj *keep_jolt_gobj, const Vec3f *pos,
                                                      f32 radius_sq)
{
	GObj *weapon_gobj;

	if ((fighter_gobj == NULL) || (pos == NULL))
	{
		return;
	}
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;)
	{
		GObj *next_gobj;
		WPStruct *wp;
		DObj *dobj;
		f32 dx;
		f32 dy;
		f32 dz;
		f32 dist_sq;

		next_gobj = weapon_gobj->link_next;
		wp = wpGetStruct(weapon_gobj);
		if ((wp == NULL) ||
		    ((wp->kind != nWPKindThunderJoltAir) && (wp->kind != nWPKindThunderJoltGround)) ||
		    (weapon_gobj == keep_jolt_gobj) ||
		    (syNetRbSnapWeaponOwnedByFighterGObj(wp, fighter_gobj) == FALSE))
		{
			weapon_gobj = next_gobj;
			continue;
		}
		dobj = DObjGetStruct(weapon_gobj);
		if (dobj == NULL)
		{
			wpMainDestroyWeapon(weapon_gobj);
			weapon_gobj = next_gobj;
			continue;
		}
		dx = dobj->translate.vec.f.x - pos->x;
		dy = dobj->translate.vec.f.y - pos->y;
		dz = dobj->translate.vec.f.z - pos->z;
		dist_sq = (dx * dx) + (dy * dy) + (dz * dz);
		if (dist_sq <= radius_sq)
		{
			wpMainDestroyWeapon(weapon_gobj);
		}
		weapon_gobj = next_gobj;
	}
}

static void syNetRbSnapCullFarthestOwnedThunderJoltForFighter(GObj *fighter_gobj, const Vec3f *ref_pos)
{
	GObj *weapon_gobj;
	GObj *farthest_gobj;
	f32 farthest_dist_sq;

	if ((fighter_gobj == NULL) || (ref_pos == NULL))
	{
		return;
	}
	farthest_gobj = NULL;
	farthest_dist_sq = -1.0F;
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp;
		DObj *dobj;
		f32 dx;
		f32 dy;
		f32 dz;
		f32 dist_sq;

		wp = wpGetStruct(weapon_gobj);
		if ((wp == NULL) ||
		    ((wp->kind != nWPKindThunderJoltAir) && (wp->kind != nWPKindThunderJoltGround)) ||
		    (syNetRbSnapWeaponOwnedByFighterGObj(wp, fighter_gobj) == FALSE))
		{
			continue;
		}
		dobj = DObjGetStruct(weapon_gobj);
		if (dobj == NULL)
		{
			continue;
		}
		dx = dobj->translate.vec.f.x - ref_pos->x;
		dy = dobj->translate.vec.f.y - ref_pos->y;
		dz = dobj->translate.vec.f.z - ref_pos->z;
		dist_sq = (dx * dx) + (dy * dy) + (dz * dz);
		if (dist_sq > farthest_dist_sq)
		{
			farthest_dist_sq = dist_sq;
			farthest_gobj = weapon_gobj;
		}
	}
	if (farthest_gobj != NULL)
	{
		wpMainDestroyWeapon(farthest_gobj);
	}
}

static sb32 syNetRbSnapThunderJoltNearFighterHand(GObj *fighter_gobj, GObj *jolt_gobj, f32 radius_sq)
{
	FTStruct *fp;
	Vec3f hand_pos;
	DObj *hand_dobj;
	DObj *jolt_dobj;
	f32 dx;
	f32 dy;
	f32 dz;
	s32 spawn_joint;

	if ((fighter_gobj == NULL) || (jolt_gobj == NULL))
	{
		return FALSE;
	}
	fp = ftGetStruct(fighter_gobj);
	jolt_dobj = DObjGetStruct(jolt_gobj);
	if ((fp == NULL) || (jolt_dobj == NULL))
	{
		return FALSE;
	}
	spawn_joint = syNetRbSnapThunderJoltSpawnJointForFighter(fp);
	hand_pos.x = 0.0F;
	hand_pos.y = 0.0F;
	hand_pos.z = 0.0F;
	hand_dobj = fp->joints[spawn_joint];
	if (hand_dobj == NULL)
	{
		return FALSE;
	}
	gmCollisionGetFighterPartsWorldPosition(hand_dobj, &hand_pos);
	dx = jolt_dobj->translate.vec.f.x - hand_pos.x;
	dy = jolt_dobj->translate.vec.f.y - hand_pos.y;
	dz = jolt_dobj->translate.vec.f.z - hand_pos.z;
	return (((dx * dx) + (dy * dy) + (dz * dz)) <= radius_sq) ? TRUE : FALSE;
}

static sb32 syNetRbSnapLiveWeaponIsThunderJoltThrowPreserve(GObj *weapon_gobj)
{
	WPStruct *wp;
	GObj *fighter_gobj;
	FTStruct *fp;

	wp = wpGetStruct(weapon_gobj);
	if ((wp == NULL) ||
	    ((wp->kind != nWPKindThunderJoltAir) && (wp->kind != nWPKindThunderJoltGround)))
	{
		return FALSE;
	}
	if ((wp->player < 0) || (wp->player >= GMCOMMON_PLAYERS_MAX))
	{
		return FALSE;
	}
	fighter_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)wp->player);
	if ((fighter_gobj == NULL) || (syNetRbSnapWeaponOwnedByFighterGObj(wp, fighter_gobj) == FALSE))
	{
		return FALSE;
	}
	fp = ftGetStruct(fighter_gobj);
	if (syNetRbSnapFighterIsInThunderJoltThrowStatus(fp) == FALSE)
	{
		return FALSE;
	}
	if (fp->motion_vars.flags.flag1 == 0)
	{
		return TRUE;
	}
	if (fighter_gobj->anim_frame < SYNETRB_SNAP_THUNDERJOLT_THROW_PRESERVE_FRAMES)
	{
		return TRUE;
	}
	return syNetRbSnapThunderJoltNearFighterHand(fighter_gobj, weapon_gobj, SYNETRB_SNAP_THUNDERJOLT_HAND_RADIUS_SQ);
}

static sb32 syNetRbSnapFighterIsInPKThunderSpecialHiStatus(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	switch (fp->status_id)
	{
	case nFTNessStatusSpecialHiStart:
	case nFTNessStatusSpecialHiHold:
	case nFTNessStatusSpecialAirHiStart:
	case nFTNessStatusSpecialAirHiHold:
		return TRUE;

	default:
		break;
	}
	return FALSE;
}

static sb32 syNetRbSnapWeaponPKThunderHeadIsActive(const WPStruct *wp, GObj *owner_gobj)
{
	(void)owner_gobj;

	if (wp == NULL)
	{
		return FALSE;
	}
	return ((wp->weapon_vars.pkthunder.status & nWPNessPKThunderStatusDestroy) == 0) ? TRUE : FALSE;
}

static sb32 syNetRbSnapPKThunderTrailBelongsToHead(const WPStruct *wp, GObj *head_gobj)
{
	if ((wp == NULL) || (head_gobj == NULL))
	{
		return FALSE;
	}
	if (wp->weapon_vars.pkthunder_trail.head_gobj == head_gobj)
	{
		return TRUE;
	}
	return (wp->weapon_vars.pkthunder_trail.parent_gobj == head_gobj) ? TRUE : FALSE;
}

static sb32 syNetRbSnapLiveWeaponIsPKThunderPreserve(GObj *weapon_gobj)
{
	WPStruct *wp;
	GObj *owner_gobj;
	FTStruct *fp;
	GObj *coupled_head_gobj;

	wp = wpGetStruct(weapon_gobj);
	if ((wp == NULL) ||
	    ((wp->kind != nWPKindPKThunderHead) && (wp->kind != nWPKindPKThunderTrail)))
	{
		return FALSE;
	}
	owner_gobj = wp->owner_gobj;
	if ((owner_gobj == NULL) || (syNetRbSnapWeaponOwnedByFighterGObj(wp, owner_gobj) == FALSE))
	{
		if ((wp->player < 0) || (wp->player >= GMCOMMON_PLAYERS_MAX))
		{
			return FALSE;
		}
		owner_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)wp->player);
	}
	if (owner_gobj == NULL)
	{
		return FALSE;
	}
	fp = ftGetStruct(owner_gobj);
	if ((fp == NULL) || (fp->fkind != nFTKindNess) ||
	    (syNetRbSnapFighterIsInPKThunderSpecialHiStatus(fp) == FALSE))
	{
		return FALSE;
	}
	coupled_head_gobj = fp->status_vars.ness.specialhi.pkthunder_gobj;
	if (coupled_head_gobj == NULL)
	{
		coupled_head_gobj = syNetRbSnapReacquirePKThunderHeadForFighter(owner_gobj);
	}
	if (coupled_head_gobj == NULL)
	{
		return FALSE;
	}
	if (wp->kind == nWPKindPKThunderHead)
	{
		return (weapon_gobj == coupled_head_gobj) ? TRUE : FALSE;
	}
	return syNetRbSnapPKThunderTrailBelongsToHead(wp, coupled_head_gobj);
}

void syNetRbSnapCullOwnedPKThunderForFighter(GObj *fighter_gobj, GObj *keep_head_gobj)
{
	GObj *weapon_gobj;
	FTStruct *fp;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp == NULL)
	{
		return;
	}
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;)
	{
		GObj *next_gobj;
		WPStruct *wp;
		sb32 should_destroy;

		next_gobj = weapon_gobj->link_next;
		wp = wpGetStruct(weapon_gobj);
		should_destroy = FALSE;
		if ((wp == NULL) || (syNetRbSnapWeaponOwnedByFighterGObj(wp, fighter_gobj) == FALSE))
		{
			weapon_gobj = next_gobj;
			continue;
		}
		if (wp->kind == nWPKindPKThunderHead)
		{
			should_destroy = (weapon_gobj != keep_head_gobj) ? TRUE : FALSE;
		}
		else if (wp->kind == nWPKindPKThunderTrail)
		{
			should_destroy = ((keep_head_gobj == NULL) ||
			                  (syNetRbSnapPKThunderTrailBelongsToHead(wp, keep_head_gobj) == FALSE)) ?
			                     TRUE :
			                     FALSE;
		}
		if (should_destroy != FALSE)
		{
			if (fp->status_vars.ness.specialhi.pkthunder_gobj == weapon_gobj)
			{
				fp->status_vars.ness.specialhi.pkthunder_gobj = NULL;
			}
			if (wp->kind == nWPKindPKThunderHead)
			{
				wp->weapon_vars.pkthunder.status = nWPNessPKThunderStatusDestroy;
				wpNessPKThunderHeadSetDestroyTrails(weapon_gobj, TRUE);
			}
			else
			{
				wpNessPKThunderHeadOrphanTrailReference(weapon_gobj);
				wp->weapon_vars.pkthunder_trail.status = nWPNessPKThunderStatusDestroy;
			}
			wpMainDestroyWeapon(weapon_gobj);
		}
		weapon_gobj = next_gobj;
	}
}

GObj *syNetRbSnapReacquirePKThunderHeadForFighter(GObj *fighter_gobj)
{
	return syNetRbSnapFindLiveWeaponForOwner(fighter_gobj, nWPKindPKThunderHead,
	                                         syNetRbSnapWeaponPKThunderHeadIsActive);
}

static void syNetRbSnapPreEjectPKThunderWeapon(GObj *weapon_gobj, WPStruct *wp)
{
	if ((weapon_gobj == NULL) || (wp == NULL))
	{
		return;
	}
	wpNessPKThunderPreDestroyWeapon(weapon_gobj);
}

#define SYNETRB_SNAP_PKFIRE_EMERGENCY_FRAME          15.0F
#define SYNETRB_SNAP_PKFIRE_SPAWN_RADIUS_SQ           8100.0F
#define SYNETRB_SNAP_PKFIRE_THROW_PRESERVE_FRAMES     25.0F

static sb32 syNetRbSnapFighterIsInPKFireSpecialNStatus(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	return ((fp->fkind == nFTKindNess) &&
	        ((fp->status_id == nFTNessStatusSpecialN) || (fp->status_id == nFTNessStatusSpecialAirN))) ?
	           TRUE :
	           FALSE;
}

static void syNetRbSnapPKFireSpawnPoseForFighter(const FTStruct *fp, Vec3f *pos, Vec3f *vel, f32 *angle)
{
	pos->x = 0.0F;
	pos->y = 0.0F;
	pos->z = 0.0F;
	gmCollisionGetFighterPartsWorldPosition(fp->joints[FTNESS_PKFIRE_SPAWN_JOINT], pos);
	pos->x += FTNESS_PKFIRE_SPAWN_OFF_X * fp->lr;
	pos->y += FTNESS_PKFIRE_SPAWN_OFF_Y;
	pos->z = 0.0F;

	if (fp->ga == nMPKineticsAir)
	{
		vel->z = 0.0F;
		*angle = FTNESS_PKFIRE_SPARK_ANGLE_AIR;
		vel->x = __cosf(FTNESS_PKFIRE_SPARK_ANGLE_AIR) * FTNESS_PKFIRE_SPARK_VEL_AIR * fp->lr;
		vel->y = __sinf(FTNESS_PKFIRE_SPARK_ANGLE_AIR) * FTNESS_PKFIRE_SPARK_VEL_AIR;
	}
	else
	{
		vel->z = 0.0F;
		*angle = FTNESS_PKFIRE_SPARK_ANGLE_GROUND;
		vel->x = __cosf(FTNESS_PKFIRE_SPARK_ANGLE_GROUND) * FTNESS_PKFIRE_SPARK_VEL_GROUND * fp->lr;
		vel->y = __sinf(FTNESS_PKFIRE_SPARK_ANGLE_GROUND) * FTNESS_PKFIRE_SPARK_VEL_GROUND;
	}
}

static void syNetRbSnapPKFireSpawnDiag(const char *reason, const FTStruct *fp, f32 anim_frame)
{
	if (syNetRbSnapWeaponDiagEnabled() != FALSE)
	{
		if (fp == NULL)
		{
			port_log("SSB64 NetRbSnapshot: pkfire_spawn skip=%s\n", reason);
		}
		else
		{
			port_log("SSB64 NetRbSnapshot: pkfire_spawn skip=%s owner_player=%d status=%d anim_frame=%.1f\n",
			         reason, (int)fp->player, (int)fp->status_id, anim_frame);
		}
	}
}

sb32 syNetRbSnapPKFireOwnedByFighter(GObj *fighter_gobj)
{
	GObj *weapon_gobj;

	if (fighter_gobj == NULL)
	{
		return FALSE;
	}
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp = wpGetStruct(weapon_gobj);

		if ((wp != NULL) && (wp->kind == nWPKindPKFire) &&
		    (syNetRbSnapWeaponOwnedByFighterGObj(wp, fighter_gobj) != FALSE))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapPKFireNeedsSpawnAtPose(GObj *fighter_gobj, const Vec3f *spawn_pos)
{
	GObj *weapon_gobj;

	if ((fighter_gobj == NULL) || (spawn_pos == NULL))
	{
		return TRUE;
	}
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp;
		DObj *dobj;
		f32 dx;
		f32 dy;
		f32 dz;

		wp = wpGetStruct(weapon_gobj);
		if ((wp == NULL) || (wp->kind != nWPKindPKFire) ||
		    (syNetRbSnapWeaponOwnedByFighterGObj(wp, fighter_gobj) == FALSE))
		{
			continue;
		}
		dobj = DObjGetStruct(weapon_gobj);
		if (dobj == NULL)
		{
			return FALSE;
		}
		dx = dobj->translate.vec.f.x - spawn_pos->x;
		dy = dobj->translate.vec.f.y - spawn_pos->y;
		dz = dobj->translate.vec.f.z - spawn_pos->z;
		if (((dx * dx) + (dy * dy) + (dz * dz)) <= SYNETRB_SNAP_PKFIRE_SPAWN_RADIUS_SQ)
		{
			return FALSE;
		}
	}
	return TRUE;
}

static GObj *syNetRbSnapReacquirePKFireAtPose(GObj *fighter_gobj, const Vec3f *pos, f32 radius_sq)
{
	GObj *weapon_gobj;

	if ((fighter_gobj == NULL) || (pos == NULL))
	{
		return NULL;
	}
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp;
		DObj *dobj;
		f32 dx;
		f32 dy;
		f32 dz;

		wp = wpGetStruct(weapon_gobj);
		if ((wp == NULL) || (wp->kind != nWPKindPKFire) ||
		    (syNetRbSnapWeaponOwnedByFighterGObj(wp, fighter_gobj) == FALSE))
		{
			continue;
		}
		dobj = DObjGetStruct(weapon_gobj);
		if (dobj == NULL)
		{
			continue;
		}
		dx = dobj->translate.vec.f.x - pos->x;
		dy = dobj->translate.vec.f.y - pos->y;
		dz = dobj->translate.vec.f.z - pos->z;
		if (((dx * dx) + (dy * dy) + (dz * dz)) <= radius_sq)
		{
			wp->owner_gobj = fighter_gobj;
			return weapon_gobj;
		}
	}
	return NULL;
}

static void syNetRbSnapCullOwnedPKFiresNearPose(GObj *fighter_gobj, GObj *keep_pkfire_gobj, const Vec3f *pos,
                                                f32 radius_sq)
{
	GObj *weapon_gobj;

	if (fighter_gobj == NULL)
	{
		return;
	}
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;)
	{
		GObj *next_gobj;
		WPStruct *wp;
		DObj *dobj;
		f32 dx;
		f32 dy;
		f32 dz;

		next_gobj = weapon_gobj->link_next;
		wp = wpGetStruct(weapon_gobj);
		if ((wp == NULL) || (wp->kind != nWPKindPKFire) || (weapon_gobj == keep_pkfire_gobj) ||
		    (syNetRbSnapWeaponOwnedByFighterGObj(wp, fighter_gobj) == FALSE))
		{
			weapon_gobj = next_gobj;
			continue;
		}
		dobj = DObjGetStruct(weapon_gobj);
		if (dobj == NULL)
		{
			wpMainDestroyWeapon(weapon_gobj);
			weapon_gobj = next_gobj;
			continue;
		}
		dx = dobj->translate.vec.f.x - pos->x;
		dy = dobj->translate.vec.f.y - pos->y;
		dz = dobj->translate.vec.f.z - pos->z;
		if (((dx * dx) + (dy * dy) + (dz * dz)) <= radius_sq)
		{
			wpMainDestroyWeapon(weapon_gobj);
		}
		weapon_gobj = next_gobj;
	}
}

static sb32 syNetRbSnapLiveWeaponIsPKFirePreserve(GObj *weapon_gobj)
{
	WPStruct *wp;
	GObj *owner_gobj;
	FTStruct *fp;

	wp = wpGetStruct(weapon_gobj);
	if ((wp == NULL) || (wp->kind != nWPKindPKFire))
	{
		return FALSE;
	}
	owner_gobj = wp->owner_gobj;
	if ((owner_gobj == NULL) || (syNetRbSnapWeaponOwnedByFighterGObj(wp, owner_gobj) == FALSE))
	{
		if ((wp->player < 0) || (wp->player >= GMCOMMON_PLAYERS_MAX))
		{
			return FALSE;
		}
		owner_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)wp->player);
	}
	if (owner_gobj == NULL)
	{
		return FALSE;
	}
	fp = ftGetStruct(owner_gobj);
	if (syNetRbSnapFighterIsInPKFireSpecialNStatus(fp) == FALSE)
	{
		return FALSE;
	}
	if (fp->motion_vars.flags.flag1 == 0)
	{
		return TRUE;
	}
	return (owner_gobj->anim_frame < SYNETRB_SNAP_PKFIRE_THROW_PRESERVE_FRAMES) ? TRUE : FALSE;
}

void syNetRbSnapTrySpawnPKFireFromAccessory(GObj *fighter_gobj)
{
	FTStruct *fp;
	Vec3f pos;
	Vec3f vel;
	f32 angle;
	sb32 should_spawn;
	const char *spawn_path;
	f32 anim_frame;
	GObj *pkfire_gobj;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (syNetRbSnapFighterIsInPKFireSpecialNStatus(fp) == FALSE))
	{
		return;
	}
	anim_frame = fighter_gobj->anim_frame;

	if (fp->motion_vars.flags.flag1 != 0)
	{
		syNetRbSnapPKFireSpawnDiag("latched", fp, anim_frame);
		return;
	}

	should_spawn = FALSE;
	spawn_path = NULL;
	if (fp->motion_vars.flags.flag0 != 0)
	{
		fp->motion_vars.flags.flag0 = 0;
		should_spawn = TRUE;
		spawn_path = "anim";
	}
	else if (anim_frame >= SYNETRB_SNAP_PKFIRE_EMERGENCY_FRAME)
	{
		should_spawn = TRUE;
		spawn_path = "emergency";
	}
	if (should_spawn == FALSE)
	{
		syNetRbSnapPKFireSpawnDiag("wait_frame", fp, anim_frame);
		return;
	}
	if (fp->joints[FTNESS_PKFIRE_SPAWN_JOINT] == NULL)
	{
		syNetRbSnapPKFireSpawnDiag("spawn_joint_null", fp, anim_frame);
		return;
	}
	syNetRbSnapPKFireSpawnPoseForFighter(fp, &pos, &vel, &angle);
	if (syNetRbSnapPKFireNeedsSpawnAtPose(fighter_gobj, &pos) == FALSE)
	{
		pkfire_gobj = syNetRbSnapReacquirePKFireAtPose(fighter_gobj, &pos, SYNETRB_SNAP_PKFIRE_SPAWN_RADIUS_SQ);
		if (pkfire_gobj != NULL)
		{
			syNetRbSnapCullOwnedPKFiresNearPose(fighter_gobj, pkfire_gobj, &pos, SYNETRB_SNAP_PKFIRE_SPAWN_RADIUS_SQ);
			fp->motion_vars.flags.flag1 = 1;
			spawn_path = "reacquire";
		}
		else
		{
			syNetRbSnapPKFireSpawnDiag("skip_dedup", fp, anim_frame);
			return;
		}
	}
	else
	{
		pkfire_gobj = wpNessPKFireMakeWeapon(fighter_gobj, &pos, &vel, angle);
		if (pkfire_gobj == NULL)
		{
			syNetRbSnapPKFireSpawnDiag("make_weapon_null", fp, anim_frame);
			return;
		}
		syNetRbSnapCullOwnedPKFiresNearPose(fighter_gobj, pkfire_gobj, &pos, SYNETRB_SNAP_PKFIRE_SPAWN_RADIUS_SQ);
		fp->motion_vars.flags.flag1 = 1;
	}
	if (syNetRbSnapWeaponDiagEnabled() != FALSE)
	{
		port_log("SSB64 NetRbSnapshot: pkfire_spawn path=%s owner_player=%d anim_frame=%.1f\n",
		         (spawn_path != NULL) ? spawn_path : "spawn", (int)fp->player, anim_frame);
	}
}

sb32 syNetRbSnapThunderJoltProcAccessoryWillRun(GObj *fighter_gobj)
{
	return syNetRbSnapFireballProcAccessoryWillRun(fighter_gobj);
}

void syNetRbSnapTrySpawnThunderJoltFromAccessory(GObj *fighter_gobj)
{
	FTStruct *fp;
	Vec3f pos;
	Vec3f vel;
	s32 spawn_joint;
	sb32 should_spawn;
	const char *spawn_path;
	f32 anim_frame;
	GObj *jolt_gobj;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (syNetRbSnapFighterIsInThunderJoltThrowStatus(fp) == FALSE))
	{
		return;
	}
	anim_frame = fighter_gobj->anim_frame;

	if (fp->motion_vars.flags.flag1 != 0)
	{
		if (syNetRbSnapThunderJoltOwnedByFighter(fighter_gobj) != FALSE)
		{
			syNetRbSnapThunderJoltSpawnDiag("latched", fp, anim_frame);
			return;
		}
		if (anim_frame < SYNETRB_SNAP_THUNDERJOLT_THROW_PRESERVE_FRAMES)
		{
			syNetRbSnapThunderJoltSpawnDiag("throw_preserve", fp, anim_frame);
			return;
		}
		/* Spawn committed for this throw; deferred eject can destroy the jolt mid-recovery. */
		syNetRbSnapThunderJoltSpawnDiag("spawn_done", fp, anim_frame);
		return;
	}

	should_spawn = FALSE;
	spawn_path = NULL;
	if (fp->motion_vars.flags.flag0 != 0)
	{
		fp->motion_vars.flags.flag0 = 0;
		should_spawn = TRUE;
		spawn_path = "anim";
	}
	else if (anim_frame >= SYNETRB_SNAP_THUNDERJOLT_EMERGENCY_FRAME)
	{
		should_spawn = TRUE;
		spawn_path = "emergency";
	}
	if (should_spawn == FALSE)
	{
		syNetRbSnapThunderJoltSpawnDiag("wait_frame", fp, anim_frame);
		return;
	}

	spawn_joint = syNetRbSnapThunderJoltSpawnJointForFighter(fp);
	if (fp->joints[spawn_joint] == NULL)
	{
		syNetRbSnapThunderJoltSpawnDiag("spawn_joint_null", fp, anim_frame);
		return;
	}
	syNetRbSnapThunderJoltSpawnPoseForFighter(fp, &pos, &vel);
	if (syNetRbSnapThunderJoltNeedsSpawnAtHand(fighter_gobj, &pos) == FALSE)
	{
		jolt_gobj = syNetRbSnapReacquireThunderJoltAtHand(fighter_gobj, &pos, SYNETRB_SNAP_THUNDERJOLT_HAND_RADIUS_SQ);
		if (jolt_gobj != NULL)
		{
			syNetRbSnapCullOwnedThunderJoltsNearPose(fighter_gobj, jolt_gobj, &pos,
			                                         SYNETRB_SNAP_THUNDERJOLT_HAND_RADIUS_SQ);
			fp->motion_vars.flags.flag1 = 1;
			spawn_path = "reacquire";
		}
		else
		{
			syNetRbSnapThunderJoltSpawnDiag("skip_dedup", fp, anim_frame);
			if (syNetRbSnapWeaponDiagEnabled() != FALSE)
			{
				port_log("SSB64 NetRbSnapshot: jolt_spawn path=skip_dedup owner_player=%d joint=%d\n",
				         (int)fp->player, spawn_joint);
			}
			return;
		}
	}
	else
	{
		jolt_gobj = wpPikachuThunderJoltAirMakeWeapon(fighter_gobj, &pos, &vel);
		if (jolt_gobj == NULL)
		{
			syNetRbSnapCullFarthestOwnedThunderJoltForFighter(fighter_gobj, &pos);
			jolt_gobj = wpPikachuThunderJoltAirMakeWeapon(fighter_gobj, &pos, &vel);
		}
		if (jolt_gobj == NULL)
		{
			syNetRbSnapThunderJoltSpawnDiag("make_weapon_null", fp, anim_frame);
			return;
		}
		syNetRbSnapCullOwnedThunderJoltsNearPose(fighter_gobj, jolt_gobj, &pos,
		                                         SYNETRB_SNAP_THUNDERJOLT_HAND_RADIUS_SQ);
		fp->motion_vars.flags.flag1 = 1;
		ftParamCheckSetFighterColAnimID(fighter_gobj, nGMColAnimFighterPikachuSpecialN, 0);
	}
	if (syNetRbSnapWeaponDiagEnabled() != FALSE)
	{
		port_log("SSB64 NetRbSnapshot: jolt_spawn path=%s owner_player=%d joint=%d\n",
		         (spawn_path != NULL) ? spawn_path : "spawn", (int)fp->player, spawn_joint);
	}
}

void syNetRbSnapTrySpawnThunderFromSpecialLw(GObj *fighter_gobj)
{
	FTStruct *fp;
	Vec3f pos;
	Vec3f vel;
	f32 anim_frame;
	GObj *thunder_gobj;
	sb32 should_spawn;
	const char *spawn_path;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (syNetRbSnapFighterIsInThunderSpecialLwStatus(fp) == FALSE))
	{
		return;
	}
	if (fp->status_vars.pikachu.speciallw.thunder_gobj != NULL)
	{
		return;
	}
	anim_frame = fighter_gobj->anim_frame;
	should_spawn = FALSE;
	spawn_path = NULL;
	if (fp->motion_vars.flags.flag0 != 0)
	{
		should_spawn = TRUE;
		spawn_path = "anim";
	}
	else if (anim_frame >= SYNETRB_SNAP_THUNDER_SPAWN_EMERGENCY_FRAME)
	{
		should_spawn = TRUE;
		spawn_path = "emergency";
	}
	if (should_spawn == FALSE)
	{
		return;
	}

	if (gMPCollisionGroundData == NULL)
	{
		syNetRbSnapThunderJoltSpawnDiag("ground_data_null", fp, anim_frame);
		return;
	}
	if (fp->joints[FTPIKACHU_THUNDER_SPAWN_JOINT] == NULL)
	{
		syNetRbSnapThunderJoltSpawnDiag("thunder_joint_null", fp, anim_frame);
		return;
	}

	pos.x = 0.0F;
	pos.y = 0.0F;
	pos.z = 0.0F;
	gmCollisionGetFighterPartsWorldPosition(fp->joints[FTPIKACHU_THUNDER_SPAWN_JOINT], &pos);
	pos.y = gMPCollisionGroundData->map_bound_top - FTPIKACHU_THUNDER_SPAWN_OFF_Y;
	vel.x = 0.0F;
	vel.z = 0.0F;
	vel.y = FTPIKACHU_THUNDER_VEL_Y;

	thunder_gobj = wpPikachuThunderHeadMakeWeapon(fighter_gobj, &pos, &vel);
	if (thunder_gobj == NULL)
	{
		syNetRbSnapThunderJoltSpawnDiag("thunder_make_null", fp, anim_frame);
		return;
	}
	fp->status_vars.pikachu.speciallw.thunder_gobj = thunder_gobj;
	fp->motion_vars.flags.flag0 = 1;
	if (syNetRbSnapWeaponDiagEnabled() != FALSE)
	{
		port_log("SSB64 NetRbSnapshot: thunder_spawn path=%s owner_player=%d status=%d anim_frame=%.1f\n",
		         (spawn_path != NULL) ? spawn_path : "spawn", (int)fp->player, (int)fp->status_id, anim_frame);
	}
}

void syNetRbSnapCullSamusChargeShotsForFighter(GObj *fighter_gobj, GObj *keep_charge_gobj)
{
	GObj *weapon_gobj;
	FTStruct *fp;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp == NULL)
	{
		return;
	}
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;)
	{
		GObj *next_gobj;
		WPStruct *wp;

		next_gobj = weapon_gobj->link_next;
		wp = wpGetStruct(weapon_gobj);
		if ((wp != NULL) && (wp->kind == nWPKindChargeShot) && (weapon_gobj != keep_charge_gobj) &&
		    (syNetRbSnapWeaponOwnedByFighterGObj(wp, fighter_gobj) != FALSE) &&
		    (syNetRbSnapWeaponChargeShotIsCharging(wp, fighter_gobj) != FALSE))
		{
			if ((fp->fkind == nFTKindSamus) && (fp->status_vars.samus.specialn.charge_gobj == weapon_gobj))
			{
				fp->status_vars.samus.specialn.charge_gobj = NULL;
			}
			if ((fp->fkind == nFTKindKirby) &&
			    (fp->status_vars.kirby.copysamus_specialn.charge_gobj == weapon_gobj))
			{
				fp->status_vars.kirby.copysamus_specialn.charge_gobj = NULL;
			}
			wpMainDestroyWeapon(weapon_gobj);
		}
		weapon_gobj = next_gobj;
	}
}

sb32 syNetRbSnapHeldItemWeaponNeedsSpawn(GObj *owner_gobj, s32 kind, const Vec3f *spawn_pos, const Vec3f *spawn_vel)
{
	GObj *weapon_gobj;
	const f32 pos_thresh_sq = 3600.0F;
	const f32 vel_thresh_sq = 400.0F;

	if (owner_gobj == NULL)
	{
		return TRUE;
	}
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp;
		DObj *dobj;
		f32 dx;
		f32 dy;
		f32 dz;
		f32 dist_sq;

		wp = wpGetStruct(weapon_gobj);
		if ((wp == NULL) || (wp->kind != kind) || (wp->owner_gobj != owner_gobj))
		{
			continue;
		}
		if (spawn_pos == NULL)
		{
			return FALSE;
		}
		dobj = DObjGetStruct(weapon_gobj);
		if (dobj == NULL)
		{
			return FALSE;
		}
		dx = dobj->translate.vec.f.x - spawn_pos->x;
		dy = dobj->translate.vec.f.y - spawn_pos->y;
		dz = dobj->translate.vec.f.z - spawn_pos->z;
		dist_sq = (dx * dx) + (dy * dy) + (dz * dz);
		if (dist_sq > pos_thresh_sq)
		{
			continue;
		}
		if (spawn_vel != NULL)
		{
			f32 dvx;
			f32 dvy;
			f32 dvz;
			f32 vel_sq;

			dvx = wp->physics.vel_air.x - spawn_vel->x;
			dvy = wp->physics.vel_air.y - spawn_vel->y;
			dvz = wp->physics.vel_air.z - spawn_vel->z;
			vel_sq = (dvx * dvx) + (dvy * dvy) + (dvz * dvz);
			if (vel_sq > vel_thresh_sq)
			{
				continue;
			}
		}
		return FALSE;
	}
	return TRUE;
}
#endif /* PORT */

#ifdef PORT
u32 syNetRbSnapshotGetSlotHashFighter(u32 tick)
{
	SYNetRbSnapshotSlot *slot = syNetRbSnapshotSlotForTick(tick);
	return slot->hash_fighter;
}
u32 syNetRbSnapshotGetSlotHashWorld(u32 tick)
{
	SYNetRbSnapshotSlot *slot = syNetRbSnapshotSlotForTick(tick);
	return slot->hash_world;
}
u32 syNetRbSnapshotGetSlotHashItem(u32 tick)
{
	SYNetRbSnapshotSlot *slot = syNetRbSnapshotSlotForTick(tick);
	return slot->hash_item;
}
u32 syNetRbSnapshotGetSlotHashWeapon(u32 tick)
{
	SYNetRbSnapshotSlot *slot = syNetRbSnapshotSlotForTick(tick);
	return slot->hash_weapon;
}
u32 syNetRbSnapshotGetSlotHashMap(u32 tick)
{
	SYNetRbSnapshotSlot *slot = syNetRbSnapshotSlotForTick(tick);
	return slot->hash_map;
}

s32 syNetRbSnapshotGetSlotMapYakumonoCount(u32 tick)
{
	SYNetRbSnapshotSlot *slot = syNetRbSnapshotSlotForTick(tick);

	if ((slot->is_valid == FALSE) || (slot->tick != tick) || (slot->mp_yaku_captured == FALSE))
	{
		return -1;
	}
	return slot->mp_yakumono_count;
}

u32 syNetRbSnapshotGetSlotHashRng(u32 tick)
{
	SYNetRbSnapshotSlot *slot = syNetRbSnapshotSlotForTick(tick);
	return slot->hash_rng;
}
u32 syNetRbSnapshotGetSlotHashCamera(u32 tick)
{
	SYNetRbSnapshotSlot *slot = syNetRbSnapshotSlotForTick(tick);
	return slot->hash_camera;
}
u32 syNetRbSnapshotGetSlotHashAnimation(u32 tick)
{
	SYNetRbSnapshotSlot *slot = syNetRbSnapshotSlotForTick(tick);
	return slot->hash_animation;
}

u32 syNetRbSnapshotGetSlotHashEffect(u32 tick)
{
	SYNetRbSnapshotSlot *slot = syNetRbSnapshotSlotForTick(tick);
	return slot->hash_effect;
}

sb32 syNetRbSnapshotGetStoredSubsystemHashes(u32 tick, u32 *figh, u32 *world, u32 *item, u32 *rng)
{
	SYNetRbSnapshotSlot *slot;

	if ((figh == NULL) || (world == NULL) || (item == NULL) || (rng == NULL))
	{
		return FALSE;
	}
	slot = syNetRbSnapshotSlotForTick(tick);
	if ((slot->is_valid == FALSE) || (slot->tick != tick))
	{
		return FALSE;
	}
	*figh = slot->hash_fighter;
	*world = slot->hash_world;
	*item = slot->hash_item;
	*rng = slot->hash_rng;
	return TRUE;
}

sb32 syNetRbSnapshotIsTickCommitted(u32 tick)
{
	SYNetRbSnapshotSlot *slot;

	if (tick == 0U)
	{
		return FALSE;
	}
	slot = syNetRbSnapshotSlotForTick(tick);
	if ((slot->is_valid == FALSE) || (slot->tick != tick))
	{
		return FALSE;
	}
	if ((sSYNetRbSnapshotLastCommittedTick != ~(u32)0) && (tick > sSYNetRbSnapshotLastCommittedTick))
	{
		return FALSE;
	}
	return TRUE;
}

u32 syNetRbSnapshotFindLatestValidTickAtOrBefore(u32 tick, u32 min_tick)
{
	u32 t;

	if (tick == 0U)
	{
		return ~(u32)0;
	}
	for (t = tick; t >= min_tick; t--)
	{
		SYNetRbSnapshotSlot *slot = syNetRbSnapshotSlotForTick(t);

		if ((slot->is_valid != FALSE) && (slot->tick == t))
		{
			return t;
		}
		if (t == 0U)
		{
			break;
		}
	}
	return ~(u32)0;
}

u32 syNetRbSnapshotFindLatestLoadSafeTickAtOrBefore(u32 tick, u32 min_tick)
{
	u32 t;

	if (tick == 0U)
	{
		return ~(u32)0;
	}
	for (t = tick; t >= min_tick; t--)
	{
		SYNetRbSnapshotSlot *slot = syNetRbSnapshotSlotForTick(t);

		if ((slot->is_valid != FALSE) && (slot->tick == t) && (slot->is_load_safe != FALSE))
		{
			return t;
		}
		if (t == 0U)
		{
			break;
		}
	}
	return ~(u32)0;
}

u32 syNetRbSnapshotGetLastLoadSafeTick(void)
{
	return sSYNetRbSnapshotLastLoadSafeTick;
}

void syNetRbSnapshotMarkLoadUnsafe(u32 tick)
{
	SYNetRbSnapshotSlot *slot;
	u32 probe;

	if (tick == 0U)
	{
		return;
	}
	slot = syNetRbSnapshotSlotForTick(tick);
	if ((slot->is_valid == FALSE) || (slot->tick != tick))
	{
		return;
	}
	slot->is_load_safe = FALSE;
	if (tick == sSYNetRbSnapshotLastLoadSafeTick)
	{
		probe = (tick > 0U) ? (tick - 1U) : 0U;
		sSYNetRbSnapshotLastLoadSafeTick = syNetRbSnapshotFindLatestLoadSafeTickAtOrBefore(probe, 0U);
	}
}

void syNetRbSnapshotPinLoadSafeAtTick(u32 tick)
{
	SYNetRbSnapshotSlot *slot;

	if (tick == 0U)
	{
		return;
	}
	slot = syNetRbSnapshotSlotForTick(tick);
	if ((slot->is_valid == FALSE) || (slot->tick != tick))
	{
		return;
	}
	slot->is_load_safe = TRUE;
	if (tick > sSYNetRbSnapshotLastLoadSafeTick)
	{
		sSYNetRbSnapshotLastLoadSafeTick = tick;
	}
}
#endif
