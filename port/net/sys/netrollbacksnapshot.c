#include <sys/netrollbacksnapshot.h>

#include <sys/netsync.h>
#include <sys/netplay_fox_firefox_gate.h>
#include <sys/netplay_ness_pkthunder_gate.h>
#include <sys/netplay_pikachu_quickattack_gate.h>
#if defined(PORT)
#include <sys/netplay_sim_quantize.h>
#endif
#if defined(SSB64_NETMENU)
#include <sys/netplay_rebirth_gate.h>
#endif
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
#include <ft/ftcommon.h>
#include <ft/ftmain.h>
#include <ft/ftparam.h>
#include <ft/ftcommon/ftcommonfunctions.h>
#include <ft/fttypes.h>
#include <sys/controller.h>
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
#include <gm/gmcollision.h>
#include <it/item.h>
#include <it/itdef.h>
#include <it/itmanager.h>
#include <it/itground/ithitokage.h>
#include <wp/weapon.h>
#include <it/ittypes.h>
#include <mp/map.h>
#include <mp/mpcommon.h>
#include <mp/mptypes.h>
#include <sc/scmanager.h>
#include <sc/sctypes.h>
#include <wp/wpprocess.h>
#include <wp/wpvars.h>
#include <wp/wpmanager.h>
#include <wp/wptypes.h>

#ifdef PORT
#include <stddef.h>
#include <sys/netrollback.h>
#if defined(SSB64_NETMENU)
#include <sys/netinput.h>
#include <sys/netrollback_episode.h>
#endif
#include <stdint.h>
#include <stdio.h>
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
#include <it/itground/itmarumine.h>

#include <ef/effect.h>
#include <ef/efmanager.h>
#include <ef/eftypes.h>
#include <gr/ground.h>
#include <gr/grdef.h>
#include <gr/grcommon/grhyrule.h>
#include <gr/grcommon/grpupupu.h>
#include <gr/grcommon/gryoster.h>
#include <gr/grcommon/grjungle.h>
#include <gr/grcommon/grsector.h>
#include <gr/grcommon/gryamabuki.h>
#include <ef/efdef.h>
#include <mp/map.h>
#include <mp/mpcollision.h>
#include <lb/lbparticle.h>

#if defined(SSB64_NETMENU)
#define SYNETRB_YOSHI_EGG_LAY_HATCH_COSMETIC_BANK_ID ((u16)0xFFFFU)
#endif

extern ITDesc dItLinkBombItemDesc;
extern ITStatusDesc dItLinkBombStatusDescs[];
extern ITStatusDesc dITMarumineStatusDescs[];

extern void portFixupStructU16(void *base, unsigned int byte_offset, unsigned int num_words);

extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);

#define SYNETRB_ITEM_FLAG_LINK_BOMB_STATUS_VALID 0x08U
#define SYNETRB_ITEM_FLAG_LINK_BOMB_STATUS_SHIFT 4U
#define SYNETRB_ITEM_FLAG_GROUND_MONSTER_ANIM_VALID 0x10U
#define SYNETRB_ITEM_FLAG_MARUMINE_EXPLODE         0x20U
#define SYNETRB_ITEM_FLAG_GBUMPER_PRESENTATION_VALID 0x40U
/* LBParticle sparkle outlives the 6-tick explode item GObj; replay from ring history after load. */
#define SYNETRB_LINK_BOMB_SPARKLE_REPLAY_WINDOW 48U
#define SYNETRB_LINK_BOMB_SPARKLE_REPLAY_MAX     8
#define SYNETRB_LINK_BOMB_SPARKLE_DEDUP_DIST2    (40.0f * 40.0f)
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
	u8 coll_p_translate_valid;
	u8 coll_pad[3];

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
	s32 breakout_wait;
	s8 breakout_lr;
	s8 breakout_ud;
	s16 dead_gate_wait;

	ub32 is_attack_active;
	ub32 is_hitstatus_nodamage;
	ub32 is_fastfall;
	ub32 is_hitstun;
	ub32 is_shield;
	ub32 is_damage_resist;
	ub32 is_cliff_hold;
	ub32 is_catchstatus;
	ub32 is_catch_or_capture;
	u8 is_effect_attach;
	u8 tap_stick_x;
	u8 tap_stick_y;
	u8 hold_stick_x;
	u8 hold_stick_y;
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
	/* Per-joint rotate/scale, so a rollback restores the full joint pose. Facing yaw lives in
	   joints[TopN]->rotate.y (= fp->lr * 90deg, set on status entry / Appear physics, not by the figatree),
	   so without rotate rewind a post-GO rollback leaves the fighter facing the camera. */
	Vec3f joint_rotate[FTPARTS_JOINT_NUM_MAX];
	Vec3f joint_scale[FTPARTS_JOINT_NUM_MAX];
	SYNetRbSnapDObjAnimBlob joint_anim[FTPARTS_JOINT_NUM_MAX];
	u8 joint_is_valid[FTPARTS_JOINT_NUM_MAX];
	u8 joint_dobj_flags[FTPARTS_JOINT_NUM_MAX];
	u8 joint_event32_pad[3];
	uintptr_t joint_anim_joint_event32[FTPARTS_JOINT_NUM_MAX];

	FTModelPartStatus modelpart_status[FTPARTS_JOINT_NUM_MAX - nFTPartsJointCommonStart];
	FTTexturePartStatus texturepart_status[2];

	u8 status_vars[sizeof(union FTStatusVars)];
	u8 passive_vars[sizeof(union FTPassiveVars)];

	f32 gobj_anim_frame;
	Vec3f gobj_translate;
	Vec3f gobj_rotate;
	u8 is_invisible;
	u8 is_ghost;
	u8 is_rebirth;
	u8 is_shadow_hide;
	u8 is_menu_ignore;
	u8 is_playertag_hide;
	u8 is_limit_map_bounds;
	u8 is_ignore_dead;
	u8 gobj_control_pad;

	/* Fighter-owned weapon GObjs (resolved after weapon apply; never trust memcpy'd pointers). */
	u32 coupled_egg_weapon_gobj_id;
	u32 coupled_boomerang_weapon_gobj_id;
	u32 coupled_spin_attack_weapon_gobj_id;
	u32 coupled_charge_weapon_gobj_id;
	u32 coupled_pkthunder_weapon_gobj_id;
	u32 coupled_thunder_weapon_gobj_id;
	u32 coupled_twister_gobj_id;
	/* Tornado DObj world translate, captured raw when caught in Twister so the blob light hash can mirror
	   the live fold of *DObjGetStruct(tornado_gobj) (the tornado is a weapon GObj outside this blob). */
	Vec3f twister_tornado_translate;
	u8 twister_tornado_dobj_valid;
	u8 twister_tornado_pad[3];

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

/*
 * DK Jungle barrel cannon (TaruCann) anim partition.
 *
 * The visual barrel is its own ground GObj (set up with `dobjs == NULL`), so it is NOT one of the
 * `gMPCollisionYakumonoDObjs` the map partition snapshots — nothing else rewinds its animation. The
 * legacy jungle ground blob only saved `anim_wait`, leaving `anim_frame` / the figatree cursor / the
 * AObj chain un-rewound on rollback; the hand-rolled restore then re-seated the default joint to its
 * base pose every restore, pinning the slide (the "barrel stuck under the stage" regression).
 *
 * Capture the full DObj anim runtime for root + child (same mechanism fighters/yakumonos use) so the
 * slide replays deterministically across rollback. Lives in the slot (local ring memory, never sent),
 * not the 128-byte ground payload — two anim blobs do not fit there.
 */
typedef struct SYNetRbSnapBarrelBlob
{
	sb32 captured;
	sb32 has_child;
	Vec3f root_translate;
	Vec3f root_rotate;
	Vec3f child_translate;
	Vec3f child_rotate;
	SYNetRbSnapDObjAnimBlob root_anim;
	SYNetRbSnapDObjAnimBlob child_anim;

} SYNetRbSnapBarrelBlob;

#define SYNETRB_SNAP_YAMABUKI_GATE_DOBJ_MAX 8U

/*
 * Saffron (Yamabuki) tower gate door anim runtime. The collision (yakumono id 3) is restored from
 * the ground-payload scalars, but the door *mesh* is a played-out anim joint — re-seating it from a
 * single root frame leaves the mesh closed (child door DObj carries its own cursor; the live
 * gcPlayAnimAll step may not advance it during resim). Mirror the DK Jungle barrel: capture the full
 * per-DObj anim runtime (cursor + AObj chain) for the whole gate tree and restore it verbatim.
 * Lives in the slot (local ring memory, never sent), not the 128-byte ground payload.
 */
typedef struct SYNetRbSnapYamabukiGateBlob
{
	sb32 captured;
	u8 dobj_count;
	SYNetRbSnapDObjAnimBlob dobj_anim[SYNETRB_SNAP_YAMABUKI_GATE_DOBJ_MAX];

} SYNetRbSnapYamabukiGateBlob;

#define SYNETRB_SNAP_ARWING_DOBJ_NUM 12U

/*
 * Sector Z Great Fox Arwing presentation partition (map_gobj DObj tree).
 *
 * Scalars live in SYNetRbSnapGroundSector; this blob rewinds anim cursors and visibility so rollback
 * does not leave arwing_status in Patrol with GOBJ_FLAG_HIDDEN or map_dobjs[0]->anim_wait == NULL
 * (which instantly cancels patrol and resets appear_timer to ~2000).
 */
typedef struct SYNetRbSnapArwingBlob
{
	sb32 captured;
	u32 map_gobj_flags;
	u16 dobj_valid_mask;
	s8 flight_pattern_idx;
	u8 pad;
	SYNetRbSnapDObjAnimBlob dobj_anim[SYNETRB_SNAP_ARWING_DOBJ_NUM];
	Vec3f dobj_translate[SYNETRB_SNAP_ARWING_DOBJ_NUM];
	Vec3f dobj_rotate[SYNETRB_SNAP_ARWING_DOBJ_NUM];

} SYNetRbSnapArwingBlob;

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
	f32 present_anim_frame;
	f32 present_anim_wait;
	u8 present_texture_id_curr;
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

#define SYNETRB_EFFECT_SNAP_NO_STRUCT   (1U << 0)
#define SYNETRB_EFFECT_SNAP_TRANSLATE   (1U << 1)

#define SYNETRB_EFFECT_RESPAWN_NONE          0U
#define SYNETRB_EFFECT_RESPAWN_QUAKE           1U
#define SYNETRB_EFFECT_RESPAWN_SHIELD          2U
#define SYNETRB_EFFECT_RESPAWN_YOSHI_SHIELD    3U
#define SYNETRB_EFFECT_RESPAWN_FOX_REFLECTOR   4U
#define SYNETRB_EFFECT_RESPAWN_NESS_PK_WAVE    5U
#define SYNETRB_EFFECT_RESPAWN_REBIRTH_HALO    6U
#define SYNETRB_EFFECT_RESPAWN_PIKACHU_THUNDER_SHOCK 7U
#define SYNETRB_EFFECT_RESPAWN_YOSHI_EGG_LAY         8U
#define SYNETRB_EFFECT_RESPAWN_NESS_PSYCHIC_MAGNET   9U

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
	Vec3f twister_pos;

} SYNetRbSnapGroundHyrule;

typedef struct SYNetRbSnapGroundJungle
{
	u32 tarucann_gobj_id;
	u8 tarucann_status;
	u8 tarucann_dobj_valid_mask;
	u16 tarucann_wait;
	f32 tarucann_rotate_step;
	Vec3f tarucann_translate;
	f32 tarucann_rotate_z;
	u32 root_anim_wait_bits;
	u32 child_anim_wait_bits;
	/* v3: rider occupancy + shoot countdown at capture (map hash + restore diag). */
	u8 tarucann_rider_count;
	u8 tarucann_rider_player_mask;
	u8 tarucann_shoot_anim_active;
	u8 tarucann_rider_pad;
	s16 tarucann_rider_shoot_wait;
	s16 tarucann_rider_release_wait;

} SYNetRbSnapGroundJungle;

#define SYNETRB_SNAP_GROUND_JUNGLE_LEGACY_PAYLOAD_LEN offsetof(SYNetRbSnapGroundJungle, tarucann_translate)
#define SYNETRB_SNAP_GROUND_JUNGLE_V1_PAYLOAD_LEN offsetof(SYNetRbSnapGroundJungle, root_anim_wait_bits)
#define SYNETRB_SNAP_GROUND_JUNGLE_V3_PAYLOAD_LEN sizeof(SYNetRbSnapGroundJungle)
#define SYNETRB_SNAP_GROUND_JUNGLE_DOBJ_ROOT_MOBA 1U
#define SYNETRB_SNAP_GROUND_JUNGLE_DOBJ_CHILD_MOBA 2U

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
	u8 gate_anim_phase;
	u16 monster_wait;
	u16 gate_wait;
	u8 monster_id_prev;
	f32 gate_anim_frame;
	f32 gate_anim_wait;

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
	u8 dobj_valid_mask;
	u32 dobj0_anim_wait_bits;
	Vec3f translate;

} SYNetRbSnapGroundYosterCloud;

typedef struct SYNetRbSnapGroundYosterCloudLegacy
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

} SYNetRbSnapGroundYosterCloudLegacy;

typedef struct SYNetRbSnapGroundYoster
{
	uintptr_t map_head;
	SYNetRbSnapGroundYosterCloud clouds[3];

} SYNetRbSnapGroundYoster;

#define SYNETRB_SNAP_GROUND_YOSTER_LEGACY_PAYLOAD_LEN \
	((u16)(sizeof(SYNetRbSnapGroundYosterCloudLegacy) * 3U))

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
	u32 map_gobj_flags;
	s8 unk_sector_0x4C;
	s8 unk_sector_0x4D;
	u16 unk_sector_0x4E;
	u8 unk_sector_0x52;
	s8 arwing_last_flight_pattern;

} SYNetRbSnapGroundSector;

#define SYNETRB_SNAP_GROUND_SECTOR_V1_PAYLOAD_LEN \
	((u16)offsetof(SYNetRbSnapGroundSector, map_gobj_flags))

#define SYNETRB_SNAP_GROUND_SECTOR_V2_PAYLOAD_LEN \
	((u16)offsetof(SYNetRbSnapGroundSector, arwing_last_flight_pattern))

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
	/* World DObj translate at save (quantized on netmenu capture when applicable). */
	Vec3f translate;
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
	sb32 cobj_valid;
	Vec3f cobj_eye;
	Vec3f cobj_at;
	Vec3f cobj_up;
	f32 cobj_fovy;

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
	SYNetRbSnapBarrelBlob barrel;
	SYNetRbSnapYamabukiGateBlob yamabuki_gate;
	SYNetRbSnapArwingBlob arwing;
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
static sb32 s_syNetRbSnapRepairStageVerifyOnly = FALSE;
#if defined(SSB64_NETMENU)
static sb32 sSYNetRbSnapDeferNetplayCatchUpDuringApply;
static sb32 sSYNetRbSnapDeferWeaponEjectUntilVerify;
static u32 sSYNetRbSnapApplyCameraTick;
static u32 s_syNetRbSnapSectorArwingRepairTick = UINT32_MAX;
#endif

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

/*
 * Reconcile paths eject effect GObjs without running efManagerDefaultProcDead.
 * Route particle-coupled effects through efManagerDestroyParticleGObj so LBTransform
 * teardown runs before the GObj is recycled. Return EFStruct to the pool only after
 * gcEjectGObj ends procs (avoids pool reuse while the GObj still runs DefaultProcUpdate).
 */
static sb32 syNetRbSnapEffectProcUsesDetachedXf(void (*proc)(GObj *))
{
	return (proc == efManagerDefaultProcUpdate) || (proc == efManagerDustLightProcUpdate) ||
	       (proc == efManagerDustHeavyDoubleProcUpdate);
}

static void syNetRbSnapEndEffectXfFuncProcs(GObj *gobj)
{
	GObjProcess *gobjproc;
	GObjProcess *next;

	if (gobj == NULL)
	{
		return;
	}
	for (gobjproc = gobj->gobjproc_head; gobjproc != NULL; gobjproc = next)
	{
		next = gobjproc->link_next;
		if ((gobjproc->kind == nGCProcessKindFunc) &&
		    (syNetRbSnapEffectProcUsesDetachedXf(gobjproc->exec.func) != FALSE))
		{
			gcEndGObjProcess(gobjproc);
		}
	}
}

static sb32 syNetRbSnapEffectGObjHasUpdateProc(const GObj *gobj, void (*proc_update)(GObj *))
{
	GObjProcess *gobjproc;

	if ((gobj == NULL) || (proc_update == NULL))
	{
		return FALSE;
	}
	for (gobjproc = gobj->gobjproc_head; gobjproc != NULL; gobjproc = gobjproc->link_next)
	{
		if ((gobjproc->kind == nGCProcessKindFunc) && (gobjproc->exec.func == proc_update))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static void syNetRbSnapEndEffectProcUpdate(GObj *gobj, EFStruct *ep)
{
	GObjProcess *gobjproc;
	GObjProcess *next;

	if ((gobj == NULL) || (ep == NULL) || (ep->proc_update == NULL))
	{
		return;
	}
	for (gobjproc = gobj->gobjproc_head; gobjproc != NULL; gobjproc = next)
	{
		next = gobjproc->link_next;
		if ((gobjproc->kind == nGCProcessKindFunc) && (gobjproc->exec.func == ep->proc_update))
		{
			gcEndGObjProcess(gobjproc);
		}
	}
}

static void syNetRbSnapEndQuakeProcUpdate(GObj *gobj)
{
	GObjProcess *gobjproc;
	GObjProcess *next;

	if (gobj == NULL)
	{
		return;
	}
	for (gobjproc = gobj->gobjproc_head; gobjproc != NULL; gobjproc = next)
	{
		next = gobjproc->link_next;
		if ((gobjproc->kind == nGCProcessKindFunc) && (gobjproc->exec.func == efManagerQuakeProcUpdate))
		{
			gcEndGObjProcess(gobjproc);
		}
	}
}

static void syNetRbSnapClearFighterEffectPointerIfMatch(FTStruct *fp, GObj *ejected_gobj);
static void syNetRbSnapSweepZombieKirbyInhaleWindEffects(void);

/*
 * Rollback particle reset returns every LBTransform to the free list. Live effect GObjs can still
 * carry stale xf pointers and DefaultProcUpdate procs until snapshot apply runs — strip coupling here.
 */
static void syNetRbSnapStripEffectXfCouplingAfterParticleReset(void)
{
	s32 pass;
	GObj *gobj;
	GObj *next;

	for (pass = 0; pass < 2; pass++)
	{
		gobj = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		for (; gobj != NULL; gobj = next)
		{
			EFStruct *ep;

			next = gobj->link_next;
			ep = efGetStruct(gobj);
			if (ep == NULL)
			{
				continue;
			}
			syNetRbSnapEndEffectXfFuncProcs(gobj);
			ep->xf = NULL;
			ep->effect_vars.common.xf = NULL;
			ep->effect_vars.dust_light.xf = NULL;
			ep->effect_vars.dust_heavy.xf = NULL;
		}
	}
	syNetRbSnapSweepZombieKirbyInhaleWindEffects();
}

/* Match gcEjectGObj GOBJ_PORT_EJECTED_SENTINEL (objman.c). Reconcile can still hold link_next
 * refs after a prior eject bailed with DOUBLE-EJECT; scrub zombies so pool reuse cannot UAF. */
static GObj *syNetRbSnapResolveFighterGobjByPlayer(s8 player);
static GObj *syNetRbSnapFindLiveShieldEffectForFighter(const GObj *fighter_gobj);
static s32 syNetRbSnapShieldPlayerFromEffectVars(const EFStruct *ep);
static sb32 syNetRbSnapFighterGuardEffectUnionOwned(const FTStruct *fp);

static void syNetRbSnapUnlinkSentinelGObjFromLists(GObj *gobj)
{
	if ((gobj == NULL) || (gobj->obj_kind != 0xFE))
	{
		return;
	}
	if (gobj->dl_link_id != ARRAY_COUNT(gGCCommonDLLinks))
	{
		gcRemoveGObjFromDLLinkedList(gobj);
	}
	gcRemoveGObjFromLinkedList(gobj);
	gobj->link_prev = NULL;
	gobj->link_next = NULL;
}

static sb32 syNetRbSnapTryRebindOrphanShieldEffect(GObj *gobj, EFStruct *ep)
{
	s32 shield_player;
	GObj *owner_gobj;
	FTStruct *fp;

	if ((gobj == NULL) || (ep == NULL))
	{
		return FALSE;
	}
	shield_player = syNetRbSnapShieldPlayerFromEffectVars(ep);
	if (shield_player < 0)
	{
		return FALSE;
	}
	owner_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)shield_player);
	if ((owner_gobj == NULL) || ((fp = ftGetStruct(owner_gobj)) == NULL))
	{
		return FALSE;
	}
	if (fp->is_shield == FALSE)
	{
		return FALSE;
	}
	ep->fighter_gobj = owner_gobj;
	ep->effect_vars.shield.player = fp->player;
	if (syNetRbSnapFighterGuardEffectUnionOwned(fp) != FALSE)
	{
		ftStatusVarsGuard(fp)->effect_gobj = gobj;
		fp->is_effect_attach = TRUE;
	}
	return TRUE;
}

static void syNetRbSnapEjectGObj(GObj *gobj)
{
	EFStruct *ep;
	LBParticle *pc;

	if (gobj == NULL)
	{
		return;
	}
	if (gobj->obj_kind == 0xFE) /* GOBJ_PORT_EJECTED_SENTINEL — match gcEjectGObj double-eject guard */
	{
		syNetRbSnapUnlinkSentinelGObjFromLists(gobj);
		return;
	}
#if defined(SSB64_NETMENU)
	pc = lbParticleFindStructForEffectGobj(gobj);
	if (pc != NULL)
	{
		efManagerDestroyParticleGObj(pc, gobj);
		return;
	}
#endif
	ep = efGetStruct(gobj);
	if (ep != NULL)
	{
		gcEjectGObj(gobj);
		efManagerSetPrevStructAlloc(ep);
		return;
	}
	gcEjectGObj(gobj);
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

static sb32 syNetRbSnapItemKindUsesFighterOwner(s32 kind);
static GObj *syNetRbSnapResolveItemOwnerFromBlob(const SYNetRbSnapItemBlob *blob);
static void syNetRbSnapReconcileFighterOwnedItemOwners(void);

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
#if defined(SSB64_NETMENU)
static void syNetRbSnapQuantizeFighterRebirthStatusVars(const FTStruct *fp, union FTStatusVars *status_vars);
#endif
static sb32 syNetRbSnapIsValidHyruleTwisterGObj(GObj *gobj);
#endif
#ifdef PORT
static void syNetRbSnapVerifyDeadWaitInvariant(const SYNetRbSnapFighterBlob *blob, const FTStruct *fp);
static sb32 syNetRbSnapFighterFieldDiffEnabled(void);
#endif
static void syNetRbSnapApplySlotToLive(const SYNetRbSnapshotSlot *slot);
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
	blob->coupled_twister_gobj_id = 0U;
	blob->twister_tornado_translate.x = 0.0F;
	blob->twister_tornado_translate.y = 0.0F;
	blob->twister_tornado_translate.z = 0.0F;
	blob->twister_tornado_dobj_valid = 0U;
	blob->guard_effect_gobj_id = 0U;
	blob->captureyoshi_effect_gobj_id = 0U;
	blob->fox_speciallw_effect_gobj_id = 0U;

	if ((fp->is_shield != FALSE) || ((fp->status_id >= nFTCommonStatusGuardStart) &&
	                               (fp->status_id <= nFTCommonStatusGuardEnd)))
	{
		/*
		 * Yoshi: vanilla spawns the egg bubble only when is_shield is set (Guard onward), not at GuardOn
		 * alone — do not snapshot a bubble id during the GuardOn window without is_shield.
		 */
		if ((fp->fkind != nFTKindYoshi) || (fp->is_shield != FALSE))
		{
			GObj *fighter_gobj;
			GObj *live_shield;

			{
				GObj *coupled_shield;
				EFStruct *coupled_ep;

				blob->guard_effect_gobj_id = syNetRbSnapGobjId(ftStatusVarsGuard(fp)->effect_gobj);
				/*
				 * Stale guard->effect_gobj can alias another player's bubble after pool reuse /
				 * live-forward rebind — reject coupled ids whose shield.player != this slot.
				 */
				coupled_shield = (blob->guard_effect_gobj_id != 0U) ? gcFindGObjByID(blob->guard_effect_gobj_id)
				                                                     : NULL;
				coupled_ep = (coupled_shield != NULL) ? efGetStruct(coupled_shield) : NULL;
				if ((coupled_ep == NULL) ||
				    (syNetRbSnapShieldPlayerFromEffectVars(coupled_ep) != fp->player))
				{
					blob->guard_effect_gobj_id = 0U;
				}
			}
			/*
			 * Live-forward reconcile can leave the bubble on the effect list (shield.player) while
			 * guard->effect_gobj is decoupled — eff hash sees the GObj but blob capture read NULL.
			 */
			if (blob->guard_effect_gobj_id == 0U)
			{
				fighter_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)fp->player);
				live_shield = syNetRbSnapFindLiveShieldEffectForFighter(fighter_gobj);
				if (live_shield != NULL)
				{
					blob->guard_effect_gobj_id = syNetRbSnapGobjId(live_shield);
				}
			}
		}
	}
	if (fp->status_id == nFTCommonStatusYoshiEgg)
	{
		blob->captureyoshi_effect_gobj_id =
		    syNetRbSnapGobjId(ftStatusVarsCaptureYoshi(fp)->effect_gobj);
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
	if (fp->status_id == nFTCommonStatusTwister)
	{
		GObj *tornado_gobj = ftStatusVarsTwister(fp)->tornado_gobj;

		blob->coupled_twister_gobj_id = syNetRbSnapGobjId(tornado_gobj);
		if (tornado_gobj != NULL)
		{
			DObj *tornado_dobj = DObjGetStruct(tornado_gobj);

			if (tornado_dobj != NULL)
			{
				blob->twister_tornado_translate = tornado_dobj->translate.vec.f;
				blob->twister_tornado_dobj_valid = 1U;
			}
		}
	}
}

/*
 * Coupled GObj pointers in status_vars/passive_vars are never trusted from memcpy — ids on the
 * fighter blob are authoritative.
 *
 * CRITICAL: status_vars is one big union, so every coupled slot below ALIASES the storage of
 * whatever status is actually active. Nulling a slot unconditionally corrupts an unrelated status
 * that happens to keep a load-bearing raw pointer at the same union offset. Concretely,
 * common.guard.effect_gobj, common.twister.tornado_gobj, and common.tarucann.tarucann_gobj all live
 * at union offset 0x08, so the old unconditional scrub zeroed the Barrel Cannon / Hyrule Tornado
 * pointer while riding them — and ftCommonTaruCannProcPhysics then dereferenced NULL (fault @ 0xC8,
 * the offset of GObj::obj). Gate every clear by the same fkind/status condition the capture-id path
 * (syNetRbSnapCaptureFighterCoupledIds) uses, so a slot is only cleared when its owning status is
 * genuinely active; otherwise the bytes belong to a different status and must be preserved verbatim.
 */
static void syNetRbSnapClearCoupledGObjPointersInStatusPassive(union FTStatusVars *status_vars,
							       union FTPassiveVars *passive_vars,
							       s32 fkind, s32 status_id, sb32 is_shield)
{
	if ((is_shield != FALSE) ||
	    ((status_id >= nFTCommonStatusGuardStart) && (status_id <= nFTCommonStatusGuardEnd)))
	{
		/*
		 * guard.effect_gobj aliases twister.tornado_gobj and tarucann.tarucann_gobj at union offset 0x08.
		 * Never scrub through the guard slot while those statuses own the union.
		 */
		if ((status_id != nFTCommonStatusTwister) && (status_id != nFTCommonStatusTaruCann))
		{
			status_vars->common.guard.effect_gobj = NULL;
		}
	}
	if ((status_id == nFTCommonStatusCaptureYoshi) || (status_id == nFTCommonStatusYoshiEgg))
	{
		status_vars->common.captureyoshi.effect_gobj = NULL;
	}
	if (fkind == nFTKindFox)
	{
		if (((status_id >= nFTFoxStatusSpecialLwScopeStart) &&
		     (status_id <= nFTFoxStatusSpecialLwScopeEnd)) ||
		    ((status_id >= nFTFoxStatusSpecialAirLwStart) && (status_id <= nFTFoxStatusSpecialAirLwTurn)))
		{
			status_vars->fox.speciallw.effect_gobj = NULL;
		}
	}
	if (fkind == nFTKindYoshi)
	{
		if ((status_id == nFTYoshiStatusSpecialHi) || (status_id == nFTYoshiStatusSpecialAirHi))
		{
			status_vars->yoshi.specialhi.egg_gobj = NULL;
		}
	}
	if (fkind == nFTKindLink)
	{
		passive_vars->link.boomerang_gobj = NULL;
		if ((status_id == nFTLinkStatusSpecialHi) || (status_id == nFTLinkStatusSpecialAirHi))
		{
			status_vars->link.specialhi.spin_attack_gobj = NULL;
		}
	}
	if (fkind == nFTKindKirby)
	{
		passive_vars->kirby.copylink_boomerang_gobj = NULL;
		if ((status_id == nFTKirbyStatusCopySamusSpecialNStart) ||
		    (status_id == nFTKirbyStatusCopySamusSpecialNLoop) ||
		    (status_id == nFTKirbyStatusCopySamusSpecialAirNStart))
		{
			status_vars->kirby.copysamus_specialn.charge_gobj = NULL;
		}
	}
	if (fkind == nFTKindSamus)
	{
		if ((status_id == nFTSamusStatusSpecialNStart) || (status_id == nFTSamusStatusSpecialNLoop) ||
		    (status_id == nFTSamusStatusSpecialAirNStart))
		{
			status_vars->samus.specialn.charge_gobj = NULL;
		}
	}
	if (fkind == nFTKindNess)
	{
		if ((status_id == nFTNessStatusSpecialHiStart) || (status_id == nFTNessStatusSpecialHiHold) ||
		    (status_id == nFTNessStatusSpecialAirHiStart) || (status_id == nFTNessStatusSpecialAirHiHold))
		{
			status_vars->ness.specialhi.pkthunder_gobj = NULL;
		}
	}
	if (fkind == nFTKindPikachu)
	{
		if ((status_id == nFTPikachuStatusSpecialLwStart) || (status_id == nFTPikachuStatusSpecialLwLoop) ||
		    (status_id == nFTPikachuStatusSpecialAirLwStart) || (status_id == nFTPikachuStatusSpecialAirLwLoop))
		{
			status_vars->pikachu.speciallw.thunder_gobj = NULL;
		}
	}
	if (status_id == nFTCommonStatusTwister)
	{
		status_vars->common.twister.tornado_gobj = NULL;
	}
}

static void syNetRbSnapScrubCoupledPointersInBlob(SYNetRbSnapFighterBlob *blob)
{
	syNetRbSnapClearCoupledGObjPointersInStatusPassive((union FTStatusVars *)blob->status_vars,
							   (union FTPassiveVars *)blob->passive_vars,
							   blob->fkind, blob->status_id, (sb32)blob->is_shield);
}

static sb32 syNetRbSnapFighterStatusIsAttackAir(s32 status_id)
{
	if ((status_id >= nFTCommonStatusAttackAirN) && (status_id <= nFTCommonStatusAttackAirLw))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapBlobInNessPKThunderScope(const SYNetRbSnapFighterBlob *blob);

/*
 * status_vars is a per-status union; memcpy captures inactive overlay bytes (e.g. attackair.rehit
 * garbage while in JumpAerialF). Scrub known stale overlays before save so blob diagnostics and any
 * future status_vars folds stay deterministic.
 */
static void syNetRbSnapScrubInactiveStatusVarsInBlob(SYNetRbSnapFighterBlob *blob)
{
	union FTStatusVars *status_vars;
	s32 status_id;

	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return;
	}
	status_vars = (union FTStatusVars *)blob->status_vars;
	status_id = blob->status_id;
	/*
	 * Kirby Stone (SpecialLw) owns status_vars.kirby.speciallw, whose hash-folded `duration`
	 * (see syNetSyncHashBattleFightersFull) sits at union offset 0 and aliases the
	 * attackair/dead/rebirth/captureyoshi/tarucann common overlays scrubbed below. Zeroing those
	 * here destroys live Stone state in the blob, so blob_figh diverges from the slot hash and the
	 * fighter mismatches on synctest load. The live overlay is the authoritative owner here, so
	 * leave the captured bytes intact. See docs/bugs/netplay_guard_shield_tap_churn_2026-06-05.md.
	 */
	if ((blob->fkind == nFTKindKirby) && (status_id >= nFTKirbyStatusSpecialLwStart) &&
	    (status_id <= nFTKirbyStatusSpecialAirLwEnd))
	{
		return;
	}
	if ((syNetRbSnapFighterStatusIsAttackAir(status_id) == FALSE) &&
	    (syNetRbSnapBlobInNessPKThunderScope(blob) == FALSE))
	{
		memset(&status_vars->common.attackair, 0, sizeof(status_vars->common.attackair));
	}
	if ((status_id < nFTCommonStatusDeadDown) || (status_id > nFTCommonStatusDeadUpStar))
	{
		memset(&status_vars->common.dead, 0, sizeof(status_vars->common.dead));
	}
	if ((status_id < nFTCommonStatusRebirthDown) || (status_id > nFTCommonStatusRebirthWait))
	{
		memset(&status_vars->common.rebirth, 0, sizeof(status_vars->common.rebirth));
	}
	if ((status_id != nFTCommonStatusCaptureYoshi) && (status_id != nFTCommonStatusYoshiEgg))
	{
		memset(&status_vars->common.captureyoshi, 0, sizeof(status_vars->common.captureyoshi));
	}
	if (status_id != nFTCommonStatusTaruCann)
	{
		memset(&status_vars->common.tarucann, 0, sizeof(status_vars->common.tarucann));
	}
}

static sb32 syNetRbSnapFighterInYoshiEggLayAttackScope(const FTStruct *fp);

static void syNetRbSnapScrubFighterGrabCouplingState(FTStruct *fp)
{
	if (fp == NULL)
	{
		return;
	}
	if ((fp->is_catch_or_capture != FALSE) && (fp->catch_gobj == NULL))
	{
		fp->is_catch_or_capture = FALSE;
	}
	if ((fp->is_catch_or_capture == FALSE) && (fp->catch_gobj != NULL))
	{
		/*
		 * Yoshi neutral-B SpecialN/NCatch/Release keeps catch_gobj with is_catch_or_capture=FALSE
		 * (see ftmain catch/capture validity). Do not scrub while the attack coupling is live.
		 */
		if (syNetRbSnapFighterInYoshiEggLayAttackScope(fp) == FALSE)
		{
			fp->catch_gobj = NULL;
		}
	}
	if ((fp->is_catch_or_capture != FALSE) && (fp->capture_gobj != NULL))
	{
		fp->capture_gobj = NULL;
	}
}

static void syNetRbSnapScrubCoupledPointersInFighter(FTStruct *fp, const SYNetRbSnapFighterBlob *blob)
{
	(void)blob;
	syNetRbSnapClearCoupledGObjPointersInStatusPassive(&fp->status_vars, &fp->passive_vars,
							   fp->fkind, fp->status_id, (sb32)fp->is_shield);
	syNetRbSnapScrubFighterGrabCouplingState(fp);
}

static sb32 syNetRbSnapBlobFoxInReflectorScope(const SYNetRbSnapFighterBlob *blob);
static u32 syNetRbSnapFoxSpecialLwEffectIdFromBlob(const SYNetRbSnapFighterBlob *blob);
static sb32 syNetRbSnapFighterInFoxReflectorScope(const FTStruct *fp);
static sb32 syNetRbSnapFighterInNessPKThunderScope(const FTStruct *fp);
static sb32 syNetRbSnapFighterIsInPKThunderSpecialHiStatus(const FTStruct *fp);
static sb32 syNetRbSnapFighterIsInPKFireSpecialNStatus(const FTStruct *fp);
static sb32 syNetRbSnapFighterInNessSpecialLwScope(const FTStruct *fp);
static sb32 syNetRbSnapFighterInNessShockFxScope(const FTStruct *fp);
static sb32 syNetRbSnapshotAnyFighterNessPKThunderScopeActive(void);
static sb32 syNetRbSnapshotAnyFighterNessSpecialLwScopeActive(void);
static sb32 syNetRbSnapLiveHasNessPKThunderScope(void);
static void syNetRbSnapPruneStaleShockSmallEffects(const SYNetRbSnapshotSlot *slot);
static sb32 syNetRbSnapLiveEffectIsQuake(const GObj *gobj, const EFStruct *ep);
static void syNetRbSnapFreezeSlotQuakeEffectsFromSlot(const SYNetRbSnapshotSlot *slot);
static void syNetRbSnapPruneOrphanQuakeAndDeadEffects(const SYNetRbSnapshotSlot *slot,
                                                      const u32 *reconciled_ids, s32 reconciled_count);
static void syNetRbSnapReapplyEffectBlobsFromSlot(const SYNetRbSnapshotSlot *slot);
static void syNetRbSnapApplyEffectBlobAnimFrame(GObj *gobj, f32 anim_frame, EFStruct *ep);
static void syNetRbSnapApplyEffectBlobTranslate(GObj *gobj, const SYNetRbSnapEffectBlob *blob,
                                                sb32 skip_quantize);
static sb32 syNetRbSnapLiveEffectMatchesBlob(const SYNetRbSnapshotSlot *slot, const SYNetRbSnapEffectBlob *blob,
                                             GObj *gobj, EFStruct *ep);
static sb32 syNetRbSnapLiveEffectMatchesAnyBlobInSlot(const SYNetRbSnapshotSlot *slot, GObj *gobj, EFStruct *ep);
static sb32 syNetRbSnapLiveEffectIsNessPKWave(const GObj *gobj, const EFStruct *ep);
static sb32 syNetRbSnapLiveFighterHasNessPKWave(GObj *fighter_gobj);
static void syNetRbSnapEnsureNessPKWaveEffectsFromSlot(const SYNetRbSnapshotSlot *slot);
static void syNetRbSnapPruneStaleNessPKWaveEffects(const SYNetRbSnapshotSlot *slot);
static sb32 syNetRbSnapFighterInNessPsychicMagnetEffectScope(const FTStruct *fp);
static sb32 syNetRbSnapBlobInNessSpecialLwScope(const SYNetRbSnapFighterBlob *blob);
static sb32 syNetRbSnapLiveFighterHasNessPsychicMagnet(GObj *fighter_gobj);
static void syNetRbSnapEnsureNessPsychicMagnetEffectsFromSlot(const SYNetRbSnapshotSlot *slot);
static void syNetRbSnapPruneStaleNessPsychicMagnetEffects(const SYNetRbSnapshotSlot *slot);
static sb32 syNetRbSnapFighterBlobNessPsychicMagnetAttachPending(const SYNetRbSnapFighterBlob *blob);
static sb32 syNetRbSnapFighterBlobNessPKWaveAttachPending(const SYNetRbSnapFighterBlob *blob);
static sb32 syNetRbSnapFighterInPikachuAttackS4Scope(const FTStruct *fp);
static sb32 syNetRbSnapBlobInPikachuAttackS4Scope(const SYNetRbSnapFighterBlob *blob);
static sb32 syNetRbSnapshotAnyFighterPikachuAttackS4ScopeActive(void);
static sb32 syNetRbSnapLiveEffectIsPikachuThunderShock(const GObj *gobj, const EFStruct *ep);
static sb32 syNetRbSnapLiveFighterHasPikachuThunderShock(GObj *fighter_gobj);
static sb32 syNetRbSnapFighterBlobPikachuThunderShockAttachPending(const SYNetRbSnapFighterBlob *blob);
static GObj *syNetRbSnapMakePikachuThunderShockForFighter(GObj *fighter_gobj, s32 frame);
static void syNetRbSnapEnsurePikachuThunderShockEffectsFromSlot(const SYNetRbSnapshotSlot *slot);
static void syNetRbSnapPruneStalePikachuThunderShockEffects(const SYNetRbSnapshotSlot *slot);
static sb32 syNetRbSnapFighterInKirbySpecialNInhaleScope(const FTStruct *fp);
static sb32 syNetRbSnapFighterInKirbySpecialNInhaleDeferScope(const FTStruct *fp);
static sb32 syNetRbSnapBlobInKirbySpecialNInhaleDeferScope(const SYNetRbSnapFighterBlob *blob);
static sb32 syNetRbSnapshotAnyFighterKirbySpecialNInhaleDeferActive(void);
static sb32 syNetRbSnapLiveHasKirbyInhaleWindEffect(void);
static void syNetRbSnapEndKirbyInhaleWindProc(GObj *gobj);
static void syNetRbSnapEjectKirbyInhaleWindEffectGObj(GObj *gobj, FTStruct *owner_fp);
static void syNetRbSnapEjectKirbyInhaleWindEffectsForFighter(GObj *fighter_gobj);
static void syNetRbSnapSweepZombieKirbyInhaleWindEffects(void);
static void syNetRbSnapPruneStaleKirbyInhaleWindEffects(const SYNetRbSnapshotSlot *slot);
static GObj *syNetRbSnapTryRespawnEffectFromBlob(const SYNetRbSnapshotSlot *slot,
                                                 const SYNetRbSnapEffectBlob *blob);
static sb32 syNetRbSnapBlobInGuardScope(const SYNetRbSnapFighterBlob *blob);
static sb32 syNetRbSnapFighterInGuardScope(const FTStruct *fp);
static sb32 syNetRbSnapFighterGuardEffectUnionOwned(const FTStruct *fp);
static sb32 syNetRbSnapFighterShieldInputHeld(const FTStruct *fp);
static sb32 syNetRbSnapFighterShieldInputHeldAuthoritative(const FTStruct *fp);
static sb32 syNetRbSnapLiveShieldEffectOwnedByFighter(const EFStruct *ep, const GObj *fighter_gobj, s32 player);
static s32 syNetRbSnapCountLiveShieldEffectsForPlayer(s32 player);
static sb32 syNetRbSnapFighterInActiveGuardStatus(const FTStruct *fp);
static sb32 syNetRbSnapFighterVanillaShieldReleaseDefer(const FTStruct *fp);
static void syNetRbSnapFighterShieldScheduleAuthoritativeRelease(FTStruct *fp);
static void syNetRbSnapApplyShieldReleaseScheduleLive(const SYNetRbSnapshotSlot *slot);
static s32 syNetRbSnapShieldPlayerFromEffectVars(const EFStruct *ep);
static GObj *syNetRbSnapResolveShieldParentFromEffectBlob(const SYNetRbSnapEffectBlob *blob);
static void syNetRbSnapAuditLiveShieldEffectOwner(GObj *shield_gobj, const FTStruct *fp);
static void syNetRbSnapReconcileLiveShieldEffectOwners(void);
static void syNetRbSnapResetShieldReleaseScheduleState(void);
static sb32 syNetRbSnapFighterShieldReleasePending(const FTStruct *fp);
static sb32 syNetRbSnapFighterShieldEnsureLiveEligible(FTStruct *fp, const char **skip_reason_out);
static GObj *syNetRbSnapTryEnsureLiveShieldEffectForFighter(GObj *fighter_gobj, const SYNetRbSnapshotSlot *slot);
static void syNetRbSnapEnsureLiveShieldEffectsOnAuthHold(const SYNetRbSnapshotSlot *slot);
static void syNetRbSnapPruneWaitOrphanShieldEffects(const SYNetRbSnapshotSlot *slot);
static sb32 syNetRbSnapGuardShieldLiveForwardPolicy(const SYNetRbSnapshotSlot *slot);
static void syNetRbSnapFighterShieldReleaseTeardown(FTStruct *fp, GObj *shield_gobj);
static void syNetRbSnapApplyYoshiShieldPresentation(GObj *fighter_gobj, FTStruct *fp);
static void syNetRbSnapTeardownYoshiShieldPresentation(GObj *fighter_gobj, FTStruct *fp, sb32 replay_egg_break);
static void syNetRbSnapCoupleYoshiShieldEffect(GObj *fighter_gobj, FTStruct *fp, GObj *effect_gobj);
static sb32 syNetRbSnapFighterShieldHealEligible(const FTStruct *fp, const char **skip_reason_out);
static sb32 syNetRbSnapFighterShieldApplyHealEligible(const FTStruct *fp, const SYNetRbSnapFighterBlob *blob,
                                                      const char **skip_reason_out);
static void syNetRbSnapPerformFighterShieldHeal(GObj *fighter_gobj, FTStruct *fp, const SYNetRbSnapshotSlot *slot,
                                                const char *heal_reason);
static void syNetRbSnapHealFighterShieldWithoutEffect(const SYNetRbSnapshotSlot *slot);
static void syNetRbSnapHealFighterShieldOnApply(const SYNetRbSnapshotSlot *slot);
static void syNetRbSnapDiagGuardWaitShieldHeld(const SYNetRbSnapshotSlot *slot);
static void syNetRbSnapDiagGuardShieldWithoutBubbleGap(const SYNetRbSnapshotSlot *slot);
static void syNetRbSnapDiagGuardShieldBubbleLinger(const SYNetRbSnapshotSlot *slot);
static void syNetRbSnapPruneStaleShields(const SYNetRbSnapshotSlot *slot, sb32 live_forward_policy);
static void syNetRbSnapReconcileFighterShieldCoupling(sb32 live_forward_policy);
static u32 syNetRbSnapGuardShieldDiagTick(const SYNetRbSnapshotSlot *slot);
static u32 syNetRbSnapGuardEffectIdFromBlob(const SYNetRbSnapFighterBlob *blob);
static sb32 syNetRbSnapLiveEffectIsShield(const GObj *gobj, const EFStruct *ep);
static GObj *syNetRbSnapFindLiveShieldEffectForFighter(const GObj *fighter_gobj);
static GObj *syNetRbSnapResolveCoupledGobj(GObj *coupled_gobj);
static sb32 syNetRbSnapLiveEffectIsYoshiEggLay(const GObj *gobj, const EFStruct *ep);
static sb32 syNetRbSnapYoshiEggLayEffectOwnedByFighter(const GObj *effect_gobj, const GObj *fighter_gobj);
static GObj *syNetRbSnapFindLiveYoshiEggLayEffectForFighter(const GObj *fighter_gobj);
#if defined(SSB64_NETMENU)
static void syNetRbSnapReplayCosmeticYoshiEggExplode(const Vec3f *pos);
static void syNetRbSnapYoshiEggLayHatchCosmeticProcUpdate(GObj *effect_gobj);
static GObj *syNetRbSnapReplayYoshiEggLayHatchShell(GObj *fighter_gobj, const Vec3f *pos);
static void syNetRbSnapQueueYoshiEggLayHatchCosmetics(s32 player, u32 tick, const Vec3f *pos, sb32 replay_shell,
                                                     sb32 defer_particles);
#endif
static sb32 syNetRbSnapshotSlotAnyFighterYoshiEggLayScope(const SYNetRbSnapshotSlot *slot);
static GObj *syNetRbSnapFindYoshiEggLayOwnerGobjFromSlot(const SYNetRbSnapshotSlot *slot, u32 effect_gobj_id);
static GObj *syNetRbSnapResolveYoshiEggLayParentGobj(const SYNetRbSnapshotSlot *slot,
                                                     const SYNetRbSnapEffectBlob *blob);
static void syNetRbSnapReconcileYoshiEggLayEffectsCore(const SYNetRbSnapshotSlot *slot);
static void syNetRbSnapReconcileYoshiEggLayEffectsInternal(const SYNetRbSnapshotSlot *slot);
static void syNetRbSnapReplayYoshiEggLayHatchCosmeticsFromSlot(const SYNetRbSnapshotSlot *slot);

static sb32 syNetRbSnapFighterInRebirthScope(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	return ((fp->status_id >= nFTCommonStatusRebirthDown) && (fp->status_id <= nFTCommonStatusRebirthWait))
	           ? TRUE
	           : FALSE;
}

static sb32 syNetRbSnapFighterInDeadScope(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	return ((fp->status_id >= nFTCommonStatusDeadDown) && (fp->status_id <= nFTCommonStatusDeadUpFall)) ? TRUE
	                                                                                                 : FALSE;
}

static sb32 syNetRbSnapshotAnyFighterDeadScopeActive(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		if (syNetRbSnapFighterInDeadScope(ftGetStruct(fighter_gobj)) != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapshotAnyFighterRebirthScopeActive(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		if (syNetRbSnapFighterInRebirthScope(ftGetStruct(fighter_gobj)) != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapshotAnyFighterFoxReflectorScopeActive(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp != NULL) && (fp->fkind == nFTKindFox) &&
		    (((fp->status_id >= nFTFoxStatusSpecialLwScopeStart) &&
		      (fp->status_id <= nFTFoxStatusSpecialLwScopeEnd)) ||
		     ((fp->status_id >= nFTFoxStatusSpecialAirLwStart) &&
		      (fp->status_id <= nFTFoxStatusSpecialAirLwTurn))))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapshotAnyFighterGuardScopeActive(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		if (syNetRbSnapFighterInGuardScope(ftGetStruct(fighter_gobj)) != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapBlobInGrabThrowSynctestFragileScope(const SYNetRbSnapFighterBlob *blob)
{
	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return FALSE;
	}
	if ((blob->catch_gobj_id != 0U) || (blob->capture_gobj_id != 0U))
	{
		return TRUE;
	}
	if ((blob->status_id >= nFTCommonStatusCatch) && (blob->status_id <= nFTCommonStatusThrowB))
	{
		return TRUE;
	}
	if ((blob->status_id >= nFTCommonStatusCapturePulled) &&
	    (blob->status_id <= nFTCommonStatusThrownDonkeyUnk))
	{
		return TRUE;
	}
	if ((blob->status_id >= nFTCommonStatusThrownStart) && (blob->status_id <= nFTCommonStatusThrownEnd))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapFighterInGrabThrowSynctestFragileScope(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	if ((fp->catch_gobj != NULL) || (fp->capture_gobj != NULL))
	{
		return TRUE;
	}
	/* Pointer coupling clears on throw release before throw anim ends; synctest probe
	 * during hidden-part teardown SIGSEGVs in ftMainEjectHiddenPartID. See
	 * docs/bugs/netplay_grab_throw_hiddenpart_synctest_segv_2026-06-07.md. */
	if ((fp->status_id >= nFTCommonStatusCatch) && (fp->status_id <= nFTCommonStatusThrowB))
	{
		return TRUE;
	}
	if ((fp->status_id >= nFTCommonStatusCapturePulled) &&
	    (fp->status_id <= nFTCommonStatusThrownDonkeyUnk))
	{
		return TRUE;
	}
	if ((fp->status_id >= nFTCommonStatusThrownStart) && (fp->status_id <= nFTCommonStatusThrownEnd))
	{
		return TRUE;
	}
	return FALSE;
}

/* Any live fighter with catch/capture GObj coupling (all link slots, not just human players). */
sb32 syNetRbSnapshotAnyFighterGrabCouplingActive(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		if (syNetRbSnapFighterInGrabThrowSynctestFragileScope(ftGetStruct(fighter_gobj)) != FALSE)
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

static s32 syNetRbSnapCountLiveItems(void)
{
	GObj *gobj;
	s32 count;

	count = 0;
	for (gobj = gGCCommonLinks[nGCCommonLinkIDItem]; gobj != NULL; gobj = gobj->link_next)
	{
		if (itGetStruct(gobj) != NULL)
		{
			count++;
		}
	}
	return count;
}

#define SYNETRB_FRAME_COMMIT_ITEM_STRESS_INTERVAL 40U

u32 syNetRbSnapshotFrameCommitIntervalCap(u32 default_interval)
{
	u32 cap;

	cap = default_interval;
	if (default_interval <= SYNETRB_FRAME_COMMIT_ITEM_STRESS_INTERVAL)
	{
		return cap;
	}
	if ((syNetRbSnapshotAnyItemHoldCouplingActive() != FALSE) ||
	    (syNetRbSnapshotAnyItemThrowInFlight() != FALSE) ||
	    (syNetRbSnapCountLiveItems() >= 2))
	{
		cap = SYNETRB_FRAME_COMMIT_ITEM_STRESS_INTERVAL;
	}
#if defined(SSB64_NETMENU)
	if (cap > SYNETRB_FRAME_COMMIT_ITEM_STRESS_INTERVAL)
	{
		if (syNetplayNessAnyLiveFighterInFcResimDeferScope() != FALSE)
		{
			cap = SYNETRB_FRAME_COMMIT_ITEM_STRESS_INTERVAL;
		}
	}
#endif
	return cap;
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

#ifdef PORT
/*
 * The Hyrule twister couples a Ground-link mesh gobj, an LBParticle effect (twister_xf), a ground
 * obstacle slot, and any riding fighters. The synctest probe's verify-load + emergency-restore cycle
 * tears down the particle and reshuffles the mesh; it cannot reproduce that coupling bit-for-bit, so
 * the probe perturbs live state every cadence. Skip the diagnostic probe while a twister is live,
 * exactly like held items / grabs / throws / fox reflector — the real FRAME_COMMIT divergence path
 * still covers these ticks.
 */
static sb32 syNetRbSnapshotHyruleTwisterActive(void)
{
	const GRCommonGroundVarsHyrule *hy;
	GObj *fighter_gobj;

	if ((gSCManagerBattleState == NULL) || (gSCManagerBattleState->gkind != (u8)nGRKindHyrule))
	{
		return FALSE;
	}
	hy = &gGRCommonStruct.hyrule;
	if ((hy->twister_status >= (u8)nGRHyruleTwisterStatusSummon) &&
	    (hy->twister_status <= (u8)nGRHyruleTwisterStatusSubside))
	{
		return TRUE;
	}
	if (hy->twister_gobj != NULL)
	{
		return TRUE;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp != NULL) && (fp->status_id == nFTCommonStatusTwister))
		{
			return TRUE;
		}
	}
	return FALSE;
}
#endif

#ifdef PORT
static GObj *syNetRbSnapFindLiveYamabukiMonsterGObj(void);
static sb32 syNetRbSnapItemIsGroundMonster(s32 kind);

static sb32 syNetRbSnapshotYamabukiGateSynctestFragile(void)
{
	const GRCommonGroundVarsYamabuki *ya;

	if ((gSCManagerBattleState == NULL) || (gSCManagerBattleState->gkind != nGRKindYamabuki))
	{
		return FALSE;
	}
	ya = &gGRCommonStruct.yamabuki;
	if ((ya->gate_status == (u8)nGRYamabukiGateStatusOpen) && (ya->monster_gobj != NULL) &&
	    (itGetStruct(ya->monster_gobj) != NULL))
	{
		return TRUE;
	}
	if ((ya->gate_status == (u8)nGRYamabukiGateStatusOpen) && (ya->gate_pos.x >= 1280.0F))
	{
		return TRUE;
	}
	if ((ya->gate_status == (u8)nGRYamabukiGateStatusWait) && (ya->gate_pos.x >= 1280.0F))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapshotYamabukiGateSlotSynctestFragile(const SYNetRbSnapshotSlot *slot)
{
	const SYNetRbSnapGroundYamabuki *ya;

	if ((slot == NULL) || (slot->ground_captured == FALSE) || (slot->ground.gkind != nGRKindYamabuki))
	{
		return FALSE;
	}
	if (slot->ground.payload_len < (u16)offsetof(SYNetRbSnapGroundYamabuki, gate_anim_frame))
	{
		return FALSE;
	}
	ya = (const SYNetRbSnapGroundYamabuki *)slot->ground.payload;
	if ((ya->gate_status == (u8)nGRYamabukiGateStatusOpen) && (ya->monster_gobj_id != 0U))
	{
		return TRUE;
	}
	if ((ya->gate_status == (u8)nGRYamabukiGateStatusOpen) && (ya->gate_pos.x >= 1280.0F))
	{
		return TRUE;
	}
	if ((ya->gate_status == (u8)nGRYamabukiGateStatusWait) && (ya->gate_pos.x >= 1280.0F))
	{
		return TRUE;
	}
	return FALSE;
}

/* Charmander flame (0x1E) and Venusaur razor (0x1F) are the contiguous tower-monster weapon kinds. */
static sb32 syNetRbSnapWeaponIsMonsterWeapon(s32 kind)
{
	return ((kind >= nWPKindMonsterStart) && (kind <= nWPKindMonsterEnd)) ? TRUE : FALSE;
}

static u32 syNetRbSnapshotCountLiveMonsterWeapons(void)
{
	GObj *weapon_gobj;
	u32 count;

	count = 0U;
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp = wpGetStruct(weapon_gobj);

		if ((wp != NULL) && (syNetRbSnapWeaponIsMonsterWeapon(wp->kind) != FALSE))
		{
			count++;
		}
	}
	return count;
}

/*
 * Tower Pokémon walk-out continues after gate_status leaves Open (tracked close, projectile window).
 * Skip synctest/probe while any live ground monster or tower-monster projectile (Charmander flame /
 * Venusaur razor) is active so a slot with item_count=0 or fewer effects does not eject the monster
 * or desync its in-flight projectiles via emergency restore.
 */
static sb32 syNetRbSnapshotYamabukiMonsterLiveSynctestFragile(void)
{
	if ((gSCManagerBattleState == NULL) || (gSCManagerBattleState->gkind != nGRKindYamabuki))
	{
		return FALSE;
	}
	if (syNetRbSnapFindLiveYamabukiMonsterGObj() != NULL)
	{
		return TRUE;
	}
	return (syNetRbSnapshotCountLiveMonsterWeapons() != 0U) ? TRUE : FALSE;
}

static sb32 syNetRbSnapshotYamabukiMonsterSlotSynctestFragile(const SYNetRbSnapshotSlot *slot)
{
	const SYNetRbSnapGroundYamabuki *ya;
	s32 si;

	if ((slot == NULL) || (slot->ground_captured == FALSE) || (slot->ground.gkind != nGRKindYamabuki))
	{
		return FALSE;
	}
	if (slot->ground.payload_len >= (u16)offsetof(SYNetRbSnapGroundYamabuki, monster_gobj_id))
	{
		ya = (const SYNetRbSnapGroundYamabuki *)slot->ground.payload;
		if (ya->monster_gobj_id != 0U)
		{
			return TRUE;
		}
	}
	for (si = 0; si < slot->item_count; si++)
	{
		if ((slot->items[si].is_valid != FALSE) &&
		    (syNetRbSnapItemIsGroundMonster(slot->items[si].kind) != FALSE))
		{
			return TRUE;
		}
	}
	for (si = 0; si < slot->weapon_count; si++)
	{
		if ((slot->weapons[si].is_valid != FALSE) &&
		    (syNetRbSnapWeaponIsMonsterWeapon(slot->weapons[si].kind) != FALSE))
		{
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * Probe slot captured item_count=0 while the live item link still has the tower Pokémon.
 * Applying the empty slot ejects Charmander and restarts the close anim mid-egress (soak ~584).
 */
static sb32 syNetRbSnapshotYamabukiMonsterProbeCaptureGapFragile(const SYNetRbSnapshotSlot *slot)
{
	if ((gSCManagerBattleState == NULL) || (gSCManagerBattleState->gkind != nGRKindYamabuki))
	{
		return FALSE;
	}
	if ((slot == NULL) || (slot->item_count != 0))
	{
		return FALSE;
	}
	return (syNetRbSnapFindLiveYamabukiMonsterGObj() != NULL) ? TRUE : FALSE;
}

static sb32 syNetRbSnapYamabukiGroundMonsterEgressActive(const SYNetRbSnapGroundYamabuki *ya)
{
	if (ya == NULL)
	{
		return FALSE;
	}
	if (ya->monster_gobj_id == 0U)
	{
		return FALSE;
	}
	if (ya->gate_status == nGRYamabukiGateStatusOpen)
	{
		return TRUE;
	}
	if ((ya->gate_status == nGRYamabukiGateStatusWait) && (ya->gate_pos.x >= 1280.0F))
	{
		return TRUE;
	}
	return FALSE;
}

/*
 * Ring slot item_count=0 during Charmander egress still carries monster_gobj_id in the ground blob.
 * Applying the empty item list would eject the live tower Pokémon and hollow its DObj (soak ~543).
 */
static sb32 syNetRbSnapShouldPreserveYamabukiGroundMonsterOnApply(const SYNetRbSnapshotSlot *slot, GObj *gobj,
                                                                ITStruct *ip)
{
	const SYNetRbSnapGroundYamabuki *ya;

	if ((slot == NULL) || (gobj == NULL) || (ip == NULL))
	{
		return FALSE;
	}
	if ((gSCManagerBattleState == NULL) || (gSCManagerBattleState->gkind != nGRKindYamabuki))
	{
		return FALSE;
	}
	if (syNetRbSnapItemIsGroundMonster(ip->kind) == FALSE)
	{
		return FALSE;
	}
	if ((slot->ground_captured == FALSE) || (slot->ground.gkind != nGRKindYamabuki))
	{
		return FALSE;
	}
	if (slot->ground.payload_len < (u16)sizeof(SYNetRbSnapGroundYamabuki))
	{
		return FALSE;
	}
	ya = (const SYNetRbSnapGroundYamabuki *)slot->ground.payload;
	if (syNetRbSnapYamabukiGroundMonsterEgressActive(ya) == FALSE)
	{
		return FALSE;
	}
	if ((ya->monster_gobj_id != 0U) && (gobj->id == ya->monster_gobj_id))
	{
		return TRUE;
	}
	if (gGRCommonStruct.yamabuki.monster_gobj == gobj)
	{
		return TRUE;
	}
	return FALSE;
}
#endif

#ifdef PORT
static sb32 syNetRbSnapBlobInYoshiEggLayScope(const SYNetRbSnapFighterBlob *blob)
{
	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return FALSE;
	}
	return ((blob->status_id == nFTCommonStatusCaptureYoshi) || (blob->status_id == nFTCommonStatusYoshiEgg))
	           ? TRUE
	           : FALSE;
}

static sb32 syNetRbSnapFighterInYoshiEggLayScope(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	return ((fp->status_id == nFTCommonStatusCaptureYoshi) || (fp->status_id == nFTCommonStatusYoshiEgg))
	           ? TRUE
	           : FALSE;
}

static sb32 syNetRbSnapStatusInYoshiEggLayAttackScope(s32 status_id)
{
	return ((status_id == nFTYoshiStatusSpecialN) || (status_id == nFTYoshiStatusSpecialNCatch) ||
	        (status_id == nFTYoshiStatusSpecialNRelease) || (status_id == nFTYoshiStatusSpecialAirN) ||
	        (status_id == nFTYoshiStatusSpecialAirNCatch) || (status_id == nFTYoshiStatusSpecialAirNRelease) ||
	        (status_id == nFTKirbyStatusCopyYoshiSpecialN) || (status_id == nFTKirbyStatusCopyYoshiSpecialNCatch) ||
	        (status_id == nFTKirbyStatusCopyYoshiSpecialNRelease) ||
	        (status_id == nFTKirbyStatusCopyYoshiSpecialAirN) ||
	        (status_id == nFTKirbyStatusCopyYoshiSpecialAirNCatch) ||
	        (status_id == nFTKirbyStatusCopyYoshiSpecialAirNRelease))
	           ? TRUE
	           : FALSE;
}

static sb32 syNetRbSnapFighterInYoshiEggLayAttackScope(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	return syNetRbSnapStatusInYoshiEggLayAttackScope(fp->status_id);
}

static sb32 syNetRbSnapBlobInYoshiEggLayAttackScope(const SYNetRbSnapFighterBlob *blob)
{
	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return FALSE;
	}
	return syNetRbSnapStatusInYoshiEggLayAttackScope(blob->status_id);
}

static sb32 syNetRbSnapshotAnyFighterCaptureYoshiActive(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp != NULL) && (fp->status_id == nFTCommonStatusCaptureYoshi))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapshotSlotAnyFighterCaptureYoshi(const SYNetRbSnapshotSlot *slot)
{
	s32 pidx;

	if (slot == NULL)
	{
		return FALSE;
	}
	for (pidx = 0; pidx < GMCOMMON_PLAYERS_MAX; pidx++)
	{
		if ((slot->fighters[pidx].is_valid != FALSE) &&
		    (slot->fighters[pidx].status_id == nFTCommonStatusCaptureYoshi))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapshotAnyFighterYoshiEggLayScopeActive(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		if (syNetRbSnapFighterInYoshiEggLayScope(ftGetStruct(fighter_gobj)) != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapshotAnyYoshiEggLayAttackerScopeActive(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		if (syNetRbSnapFighterInYoshiEggLayAttackScope(ftGetStruct(fighter_gobj)) != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapCaptureYoshiSwallowInProgressLive(void)
{
	return ((syNetRbSnapshotAnyFighterCaptureYoshiActive() != FALSE) &&
	        (syNetRbSnapshotAnyYoshiEggLayAttackerScopeActive() != FALSE))
	           ? TRUE
	           : FALSE;
}

static sb32 syNetRbSnapCaptureYoshiSwallowInProgressSlot(const SYNetRbSnapshotSlot *slot)
{
	s32 pidx;
	sb32 attack;

	if (slot == NULL)
	{
		return FALSE;
	}
	if (syNetRbSnapshotSlotAnyFighterCaptureYoshi(slot) == FALSE)
	{
		return FALSE;
	}
	attack = syNetRbSnapshotAnyYoshiEggLayAttackerScopeActive();
	for (pidx = 0; (attack == FALSE) && (pidx < GMCOMMON_PLAYERS_MAX); pidx++)
	{
		if (syNetRbSnapBlobInYoshiEggLayAttackScope(&slot->fighters[pidx]) != FALSE)
		{
			attack = TRUE;
		}
	}
	return attack ? TRUE : FALSE;
}

static sb32 syNetRbSnapFighterInYoshiEggStatusOnly(const FTStruct *fp)
{
	return ((fp != NULL) && (fp->status_id == nFTCommonStatusYoshiEgg)) ? TRUE : FALSE;
}

static sb32 syNetRbSnapshotAnyFighterYoshiEggStatusActive(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		if (syNetRbSnapFighterInYoshiEggStatusOnly(ftGetStruct(fighter_gobj)) != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

sb32 syNetRbSnapYoshiEggLayCaptureWindowActiveWithoutEgg(void)
{
	/* Victim CaptureYoshi + attacker NCatch: never run full egg ensure/prune (vanilla has no shell yet). */
	if (syNetRbSnapCaptureYoshiSwallowInProgressLive() != FALSE)
	{
		return TRUE;
	}
	if (syNetRbSnapshotAnyFighterYoshiEggStatusActive() != FALSE)
	{
		return FALSE;
	}
	return ((syNetRbSnapshotAnyFighterYoshiEggLayScopeActive() != FALSE) ||
	        (syNetRbSnapshotAnyYoshiEggLayAttackerScopeActive() != FALSE))
	           ? TRUE
	           : FALSE;
}

static sb32 syNetRbSnapshotSlotAnyFighterYoshiEggStatus(const SYNetRbSnapshotSlot *slot)
{
	s32 pidx;

	if (slot == NULL)
	{
		return FALSE;
	}
	for (pidx = 0; pidx < GMCOMMON_PLAYERS_MAX; pidx++)
	{
		if (slot->fighters[pidx].status_id == nFTCommonStatusYoshiEgg)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapshotSlotAnyFighterYoshiEggLayScope(const SYNetRbSnapshotSlot *slot);

static sb32 syNetRbSnapYoshiEggLaySlotCaptureWindowWithoutEgg(const SYNetRbSnapshotSlot *slot)
{
	s32 pidx;

	if (slot == NULL)
	{
		return FALSE;
	}
	if (syNetRbSnapCaptureYoshiSwallowInProgressSlot(slot) != FALSE)
	{
		return TRUE;
	}
	if (syNetRbSnapshotSlotAnyFighterYoshiEggStatus(slot) != FALSE)
	{
		return FALSE;
	}
	if (syNetRbSnapshotSlotAnyFighterYoshiEggLayScope(slot) != FALSE)
	{
		return TRUE;
	}
	for (pidx = 0; pidx < GMCOMMON_PLAYERS_MAX; pidx++)
	{
		if (syNetRbSnapBlobInYoshiEggLayAttackScope(&slot->fighters[pidx]) != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapshotSlotAnyFighterYoshiEggLayScope(const SYNetRbSnapshotSlot *slot)
{
	s32 pidx;

	if (slot == NULL)
	{
		return FALSE;
	}
	for (pidx = 0; pidx < GMCOMMON_PLAYERS_MAX; pidx++)
	{
		if (syNetRbSnapBlobInYoshiEggLayScope(&slot->fighters[pidx]) != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapshotAnyFighterYoshiEggScopeActive(void)
{
	GObj *fighter_gobj;
	GObj *weapon_gobj;

	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp = wpGetStruct(weapon_gobj);

		if ((wp != NULL) && (wp->kind == nWPKindEggThrow))
		{
			return TRUE;
		}
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp != NULL) && (fp->fkind == nFTKindYoshi) &&
		    ((fp->status_id == nFTYoshiStatusSpecialHi) || (fp->status_id == nFTYoshiStatusSpecialAirHi)))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapshotAnyFighterYoshiAerialLandingFragile(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);
		s32 status;

		if ((fp == NULL) || (fp->fkind != nFTKindYoshi))
		{
			continue;
		}
		status = fp->status_id;
		if ((status == nFTCommonStatusFallAerial) || (status == nFTCommonStatusLandingLight) ||
		    (status == nFTCommonStatusLandingHeavy) ||
		    ((status >= nFTCommonStatusAttackAirStart) && (status <= nFTCommonStatusLandingAirEnd)))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapshotSectorArwingDeckCollisionLive(void)
{
	if ((gSCManagerBattleState == NULL) || (gSCManagerBattleState->gkind != nGRKindSector))
	{
		return FALSE;
	}
	if (gGRCommonStruct.sector.is_arwing_line_active == FALSE)
	{
		return FALSE;
	}
	return (gGRCommonStruct.sector.is_arwing_z_near != FALSE) ? TRUE : FALSE;
}

static sb32 syNetRbSnapshotFighterOnSectorArwingDeck(const FTStruct *fp)
{
	if ((fp == NULL) || (syNetRbSnapshotSectorArwingDeckCollisionLive() == FALSE))
	{
		return FALSE;
	}
	if (fp->coll_data.floor_line_id != 1)
	{
		return FALSE;
	}
	return ((fp->coll_data.floor_flags & MAP_FLAG_FLOOR) != 0U) ? TRUE : FALSE;
}

static sb32 syNetRbSnapshotFighterSectorArwingDeckJumpArcFragile(s32 status)
{
	if ((status >= nFTCommonStatusKneeBend) && (status <= nFTCommonStatusFallAerial))
	{
		return TRUE;
	}
	if ((status >= nFTCommonStatusAttackAirStart) && (status <= nFTCommonStatusLandingAirEnd))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapshotFighterSectorArwingDeckFragile(const FTStruct *fp)
{
	s32 status;

	if (syNetRbSnapshotSectorArwingDeckCollisionLive() == FALSE)
	{
		return FALSE;
	}
	status = fp->status_id;
	if (syNetRbSnapshotFighterOnSectorArwingDeck(fp) != FALSE)
	{
		if ((status >= nFTCommonStatusWalkSlow) && (status <= nFTCommonStatusWalkEnd))
		{
			return TRUE;
		}
		if ((status >= nFTCommonStatusKneeBend) && (status <= nFTCommonStatusLandingHeavy))
		{
			return TRUE;
		}
		if ((status == nFTCommonStatusFall) || (status == nFTCommonStatusFallAerial))
		{
			return TRUE;
		}
	}
	if (syNetRbSnapshotFighterSectorArwingDeckJumpArcFragile(status) != FALSE)
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapshotAnySectorArwingDeckFragile(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		if (syNetRbSnapshotFighterSectorArwingDeckFragile(ftGetStruct(fighter_gobj)) != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapshotFighterBlobSectorArwingDeckFragile(const SYNetRbSnapFighterBlob *blob)
{
	s32 status;

	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return FALSE;
	}
	if ((gSCManagerBattleState == NULL) || (gSCManagerBattleState->gkind != nGRKindSector))
	{
		return FALSE;
	}
	if (syNetRbSnapshotSectorArwingDeckCollisionLive() == FALSE)
	{
		return FALSE;
	}
	status = blob->status_id;
	if ((blob->coll.floor_line_id == 1) && ((blob->coll.floor_flags & MAP_FLAG_FLOOR) != 0U))
	{
		if ((status >= nFTCommonStatusWalkSlow) && (status <= nFTCommonStatusWalkEnd))
		{
			return TRUE;
		}
		if ((status >= nFTCommonStatusKneeBend) && (status <= nFTCommonStatusLandingHeavy))
		{
			return TRUE;
		}
		if ((status == nFTCommonStatusFall) || (status == nFTCommonStatusFallAerial))
		{
			return TRUE;
		}
	}
	if (syNetRbSnapshotFighterSectorArwingDeckJumpArcFragile(status) != FALSE)
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapshotSectorArwingDeckSlotProbeFragile(const SYNetRbSnapshotSlot *slot)
{
	s32 pidx;

	if ((slot == NULL) || (slot->ground_captured == FALSE) || (slot->ground.gkind != nGRKindSector))
	{
		return FALSE;
	}
	for (pidx = 0; pidx < GMCOMMON_PLAYERS_MAX; pidx++)
	{
		if (syNetRbSnapshotFighterBlobSectorArwingDeckFragile(&slot->fighters[pidx]) != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * Periodic synctest loads probe_tick (= completed_tick - 1) into live, verifies, then emergency-restores.
 * During Sector patrol that is always a visible ~1-frame backward mesh snap (sector_arwing_restore at
 * frame N-1 while live was at N) even when map hash matches and rollbacks=0. Skip the probe while the
 * Arwing is actively patrolling; real rollback loads still run full ApplyArwing. Deck-coupled fighters
 * remain covered by syNetRbSnapshotAnySectorArwingDeckFragile.
 */
static sb32 syNetRbSnapshotSectorArwingPatrolLiveSynctestFragile(void)
{
	if ((gSCManagerBattleState == NULL) || (gSCManagerBattleState->gkind != nGRKindSector))
	{
		return FALSE;
	}
	if (gGRCommonStruct.sector.arwing_status != nGRSectorArwingStatusPatrol)
	{
		return FALSE;
	}
	if (gGRCommonStruct.sector.arwing_pilot_curr == -2)
	{
		return FALSE;
	}
	return TRUE;
}

static sb32 syNetRbSnapshotSectorArwingPatrolSlotSynctestFragile(const SYNetRbSnapshotSlot *slot)
{
	const SYNetRbSnapGroundSector *sec;

	if ((slot == NULL) || (slot->ground_captured == FALSE) || (slot->ground.gkind != nGRKindSector))
	{
		return FALSE;
	}
	if (slot->ground.payload_len < SYNETRB_SNAP_GROUND_SECTOR_V1_PAYLOAD_LEN)
	{
		return FALSE;
	}
	sec = (const SYNetRbSnapGroundSector *)slot->ground.payload;
	if (sec->arwing_status != (u8)nGRSectorArwingStatusPatrol)
	{
		return FALSE;
	}
	if (sec->arwing_pilot_curr == (u8)-2)
	{
		return FALSE;
	}
	return TRUE;
}

static void syNetRbSnapshotCanonicalizeSectorArwingDeckFighter(GObj *fighter_gobj)
{
	FTStruct *fp;
	DObj *root_dobj;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if ((fp == NULL) || (syNetRbSnapFighterInRebirthScope(fp) != FALSE))
	{
		return;
	}
	if (syNetRbSnapshotFighterSectorArwingDeckFragile(fp) == FALSE)
	{
		return;
	}
	syNetplayQuantizeFighterPhysics(&fp->physics);
	syNetplayQuantizeMPCollData(&fp->coll_data);
#if defined(SSB64_NETMENU)
	syNetplayQuantizeVec3f(&fp->coll_data.vel_speed);
#endif
	if (fp->joints[nFTPartsJointTopN] != NULL)
	{
		syNetplayQuantizeDObjTranslate(fp->joints[nFTPartsJointTopN]);
	}
	if (((fp->coll_data.floor_flags & MAP_VERTEX_COLL_PASS) != 0U) ||
	    ((fp->coll_data.floor_flags & MAP_VERTEX_COLL_CLIFF) != 0U))
	{
		root_dobj = DObjGetStruct(fighter_gobj);
		if (root_dobj != NULL)
		{
			syNetplayQuantizeDObjTranslate(root_dobj);
		}
	}
}

/*
 * Yakumono line 1 (Arwing deck) is derived from the flight DObj tree each frame. On rollback apply the
 * map partition and Arwing pose partition can disagree; re-derive line 1 from restored flight state.
 */
static void syNetRbSnapReconcileSectorArwingDeckYakumonoFromFlightTree(void)
{
	if ((gSCManagerBattleState == NULL) || (gSCManagerBattleState->gkind != nGRKindSector))
	{
		return;
	}
	if (gGRCommonStruct.sector.arwing_pilot_curr == -2)
	{
		return;
	}
	grSectorArwingReconcileDeckYakumonoFromFlightTree();
	grSectorArwingUpdateCollisions();
}

static sb32 syNetRbSnapSectorArwingDeckYakumonoDerivedFromGround(const SYNetRbSnapGroundSector *sec)
{
	if (sec == NULL)
	{
		return FALSE;
	}
	return ((sec->arwing_status == (u8)nGRSectorArwingStatusPatrol) && (sec->arwing_pilot_curr != (u8)-2)) ? TRUE
	                                                                                                   : FALSE;
}

/*
 * Sector Z deck line 1 is derived from the Arwing flight tree during patrol; skip restoring mp_yaku[1]
 * from the map blob and always re-capture it from live after reconcile at the hash boundary.
 */
static sb32 syNetRbSnapSectorArwingDeckYakumonoDerivedFromSlot(const SYNetRbSnapshotSlot *slot)
{
	const SYNetRbSnapGroundSector *sec;

	if ((slot == NULL) || (slot->ground_captured == FALSE) || (slot->ground.gkind != nGRKindSector))
	{
		return FALSE;
	}
	if (slot->ground.payload_len < SYNETRB_SNAP_GROUND_SECTOR_V1_PAYLOAD_LEN)
	{
		return FALSE;
	}
	sec = (const SYNetRbSnapGroundSector *)slot->ground.payload;
	return syNetRbSnapSectorArwingDeckYakumonoDerivedFromGround(sec);
}

static sb32 syNetRbSnapSectorArwingDeckYakumonoDerivedLive(void)
{
	if ((gSCManagerBattleState == NULL) || (gSCManagerBattleState->gkind != nGRKindSector))
	{
		return FALSE;
	}
	return ((gGRCommonStruct.sector.arwing_status == nGRSectorArwingStatusPatrol) &&
	        (gGRCommonStruct.sector.arwing_pilot_curr != -2))
	           ? TRUE
	           : FALSE;
}

/*
 * Hash-boundary canonicalization: yakumono line 1 status carries transient enum states
 * (show/hidden) from stage-process ordering. For Sector's deck-derived line, map hash should
 * fold the authoritative collision gate (line_active && z_near), not prior-frame hysteresis.
 */
static void syNetRbSnapNormalizeSectorArwingDeckYakumonoStatusForHash(void)
{
	DObj *deck_dobj;
	s32 canonical_status;

	if (syNetRbSnapSectorArwingDeckYakumonoDerivedLive() == FALSE)
	{
		return;
	}
	if (gMPCollisionYakumonoDObjs == NULL)
	{
		return;
	}
	deck_dobj = gMPCollisionYakumonoDObjs->dobjs[1];
	if (deck_dobj == NULL)
	{
		return;
	}
	canonical_status =
	    ((gGRCommonStruct.sector.is_arwing_line_active != FALSE) &&
	     (gGRCommonStruct.sector.is_arwing_z_near != FALSE))
	        ? nMPYakumonoStatusOn
	        : nMPYakumonoStatusOff;
	deck_dobj->user_data.s = canonical_status;
}

static void syNetRbSnapshotPrepareMapStateForHash(void)
{
	syNetRbSnapReconcileSectorArwingDeckYakumonoFromFlightTree();
	syNetRbSnapNormalizeSectorArwingDeckYakumonoStatusForHash();
}

static void syNetRbSnapRefreshSectorArwingDeckFighterPlatformCoupling(void)
{
	GObj *fighter_gobj;

	if (syNetRbSnapshotSectorArwingDeckCollisionLive() == FALSE)
	{
		return;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp == NULL) || (fp->ga != nMPKineticsGround) || (fp->coll_data.floor_line_id != 1))
		{
			continue;
		}
		/*
		 * Skip only a fighter that is itself respawning: rebirth pose/coll is
		 * canonicalized on its own apply path and must not inherit live deck
		 * velocity. A grounded, living fighter standing on deck line 1 still
		 * gets platform carry while another player is in their rebirth window —
		 * a global rebirth-scope bail here drops the Arwing's carry for everyone.
		 */
		if (syNetRbSnapFighterInRebirthScope(fp) != FALSE)
		{
			continue;
		}
		if (mpCollisionCheckExistLineID(1) == FALSE)
		{
			continue;
		}
		mpCollisionGetSpeedLineID(1, &fp->coll_data.vel_speed);
#if defined(SSB64_NETMENU)
		syNetplayQuantizeVec3f(&fp->coll_data.vel_speed);
#endif
	}
}
#endif

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
#ifdef PORT
	if (syNetRbSnapshotHyruleTwisterActive() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "hyrule_twister";
		}
		return TRUE;
	}
	if (syNetRbSnapshotYamabukiGateSynctestFragile() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "yamabuki_gate";
		}
		return TRUE;
	}
	if (syNetRbSnapshotYamabukiMonsterLiveSynctestFragile() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "yamabuki_monster";
		}
		return TRUE;
	}
#endif
	if (syNetRbSnapshotAnyItemHoldCouplingActive() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "item_hold";
		}
		return TRUE;
	}
	if (syNetRbSnapshotAnyFighterGrabCouplingActive() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "grab_coupling";
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
	if (syNetRbSnapshotAnyFighterDeadScopeActive() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "dead";
		}
		return TRUE;
	}
	if (syNetRbSnapshotAnyFighterRebirthScopeActive() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "rebirth";
		}
		return TRUE;
	}
	if (syNetRbSnapshotAnyFighterFoxReflectorScopeActive() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "fox_reflector";
		}
		return TRUE;
	}
	if (syNetRbSnapshotAnyFighterNessSpecialLwScopeActive() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "ness_speciallw";
		}
		return TRUE;
	}
	if (syNetplayFoxLiveHasFirefoxSynctestDeferScope() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "fox_firefox";
		}
		return TRUE;
	}
	if (syNetplayPikachuLiveHasQuickAttackSynctestDeferScope() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "pikachu_quickattack";
		}
		return TRUE;
	}
	if (syNetRbSnapshotAnyFighterPikachuAttackS4ScopeActive() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "pikachu_attacks4";
		}
		return TRUE;
	}
	if (syNetRbSnapshotAnyFighterKirbySpecialNInhaleDeferActive() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "kirby_specialn_inhale";
		}
		return TRUE;
	}
#ifdef PORT
	if (syNetRbSnapshotAnyFighterYoshiEggLayScopeActive() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "yoshi_egg_lay";
		}
		return TRUE;
	}
	if (syNetRbSnapshotAnyYoshiEggLayAttackerScopeActive() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "yoshi_egg_lay_attack";
		}
		return TRUE;
	}
	if (syNetRbSnapshotAnyFighterYoshiEggScopeActive() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "yoshi_egg";
		}
		return TRUE;
	}
	if (syNetRbSnapshotAnyFighterYoshiAerialLandingFragile() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "yoshi_aerial_landing";
		}
		return TRUE;
	}
	if (syNetRbSnapshotAnySectorArwingDeckFragile() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "sector_arwing_deck";
		}
		return TRUE;
	}
	if (syNetRbSnapshotSectorArwingPatrolLiveSynctestFragile() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "sector_arwing_patrol";
		}
		return TRUE;
	}
#endif
#if defined(SSB64_NETMENU)
	if (syNetplayNessAnyLiveFighterInJibakuBurstScope() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "ness_jibaku";
		}
		return TRUE;
	}
#endif
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
	syNetRbSnapReconcileFighterOwnedItemOwners();
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
#if defined(SSB64_NETMENU)
/*
 * Cross-ISA: the grab-coupling re-derivation below re-runs the vanilla capture/throw proc-map math
 * (sinf/cosf/sqrtf, matrix concat) which overwrites the just-restored, quantized joint/root geometry
 * with freshly-computed values that are not bit-identical across aarch64/x86_64. Re-snap the coupled
 * fighter's root translate and every joint translate back onto the shared grid so the anim/figh hash
 * partitions agree post-finalize (observed: joint0_ty garbage during grab_coupling).
 */
static void syNetRbSnapQuantizeFighterCoupledGeometry(GObj *fighter_gobj, sb32 skip_root_y)
{
	FTStruct *fp;
	DObj *root_dobj;
	s32 ji;

	if (fighter_gobj == NULL)
	{
		return;
	}
	root_dobj = DObjGetStruct(fighter_gobj);
	if (root_dobj != NULL)
	{
		if (skip_root_y != FALSE)
		{
			f32 saved_y = root_dobj->translate.vec.f.y;

			syNetplayQuantizeDObjTranslate(root_dobj);
			root_dobj->translate.vec.f.y = saved_y;
		}
		else
		{
			syNetplayQuantizeDObjTranslate(root_dobj);
		}
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp == NULL)
	{
		return;
	}
	for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
	{
		if (fp->joints[ji] != NULL)
		{
			syNetplayQuantizeVec3f(&fp->joints[ji]->translate.vec.f);
		}
	}
}
#endif

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
		if (grabber_fp->joints[nFTPartsJointTopN] != NULL)
		{
			ftParamsUpdateFighterPartsTransformAll(grabber_fp->joints[nFTPartsJointTopN]);
		}
		if (grabber_fp->proc_map != NULL)
		{
			grabber_fp->proc_map(grabber_gobj);
		}
		else
		{
			mpCommonRunFighterCollisionDefault(grabber_gobj, &DObjGetStruct(grabber_gobj)->translate.vec.f,
			                                   &grabber_fp->coll_data);
		}

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

#if defined(SSB64_NETMENU)
		syNetRbSnapQuantizeFighterCoupledGeometry(fighter_gobj, FALSE);
		syNetRbSnapQuantizeFighterCoupledGeometry(grabber_gobj, TRUE);
#endif
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

		if ((fp->fkind == nFTKindNess) || (fp->fkind == nFTKindNNess))
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
				syNetplayNessReconcilePKThunderWeaponsAfterApply(fighter_gobj);
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

		if (fp->status_id == nFTCommonStatusTwister)
		{
			GObj *twister_gobj = NULL;

			if (blob->coupled_twister_gobj_id != 0U)
			{
				twister_gobj = gcFindGObjByID(blob->coupled_twister_gobj_id);
				if ((twister_gobj != NULL) &&
				    (syNetRbSnapIsValidHyruleTwisterGObj(twister_gobj) == FALSE))
				{
					twister_gobj = NULL;
				}
			}
			if (twister_gobj == NULL)
			{
				twister_gobj = grHyruleGetTwisterGobj();
			}
			if ((twister_gobj != NULL) && (syNetRbSnapIsValidHyruleTwisterGObj(twister_gobj) != FALSE))
			{
				ftStatusVarsTwister(fp)->tornado_gobj = twister_gobj;
#if defined(SSB64_NETMENU)
				ftCommonTwisterReconcileRiderAfterRollback(fighter_gobj);
#endif
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
#if defined(SSB64_NETMENU)
	{
		MPCollData coll_scratch;

		coll_scratch.pos_prev = dst->pos_prev;
		coll_scratch.pos_diff = dst->pos_diff;
		coll_scratch.vel_speed = dst->vel_speed;
		coll_scratch.vel_push = dst->vel_push;
		coll_scratch.line_coll_dist = dst->line_coll_dist;
		coll_scratch.floor_dist = dst->floor_dist;
		coll_scratch.floor_angle = dst->floor_angle;
		coll_scratch.ceil_angle = dst->ceil_angle;
		coll_scratch.lwall_angle = dst->lwall_angle;
		coll_scratch.rwall_angle = dst->rwall_angle;
		syNetplayQuantizeMPCollData(&coll_scratch);
		dst->pos_prev = coll_scratch.pos_prev;
		dst->pos_diff = coll_scratch.pos_diff;
		dst->vel_speed = coll_scratch.vel_speed;
		dst->vel_push = coll_scratch.vel_push;
		dst->line_coll_dist = coll_scratch.line_coll_dist;
		dst->floor_dist = coll_scratch.floor_dist;
		dst->floor_angle = coll_scratch.floor_angle;
		dst->ceil_angle = coll_scratch.ceil_angle;
		dst->lwall_angle = coll_scratch.lwall_angle;
		dst->rwall_angle = coll_scratch.rwall_angle;
	}
#endif
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
#if defined(SSB64_NETMENU)
	syNetplayQuantizeMPCollData(dst);
#endif
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
#if defined(SSB64_NETMENU)
	dst->length_invert = syNetplayQuantizeF32(dst->length_invert);
	dst->length = syNetplayQuantizeF32(dst->length);
	dst->value_base = syNetplayQuantizeF32(dst->value_base);
	dst->value_target = syNetplayQuantizeF32(dst->value_target);
	dst->rate_base = syNetplayQuantizeF32(dst->rate_base);
	dst->rate_target = syNetplayQuantizeF32(dst->rate_target);
#endif
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
#if defined(SSB64_NETMENU)
	aobj->length_invert = syNetplayQuantizeF32(aobj->length_invert);
	aobj->length = syNetplayQuantizeF32(aobj->length);
	aobj->value_base = syNetplayQuantizeF32(aobj->value_base);
	aobj->value_target = syNetplayQuantizeF32(aobj->value_target);
	aobj->rate_base = syNetplayQuantizeF32(aobj->rate_base);
	aobj->rate_target = syNetplayQuantizeF32(aobj->rate_target);
#endif
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
#if defined(SSB64_NETMENU)
	dst->anim_wait = syNetplayQuantizeAnimScalar(dst->anim_wait);
	dst->anim_speed = syNetplayQuantizeAnimScalar(dst->anim_speed);
	dst->anim_frame = syNetplayQuantizeAnimScalar(dst->anim_frame);
#endif
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
#if defined(SSB64_NETMENU)
	dobj->anim_wait = syNetplayQuantizeAnimScalar(dobj->anim_wait);
	dobj->anim_speed = syNetplayQuantizeAnimScalar(dobj->anim_speed);
	dobj->anim_frame = syNetplayQuantizeAnimScalar(dobj->anim_frame);
	if (dobj->parent_gobj != NULL)
	{
		dobj->parent_gobj->anim_frame = dobj->anim_frame;
	}
#endif
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
#if defined(SSB64_NETMENU)
	{
		FTAttackColl quant;

		quant.attack_state = dst->attack_state;
		quant.pos_curr = dst->pos_curr;
		quant.pos_prev = dst->pos_prev;
		syNetplayQuantizeFTAttackColl(&quant);
		dst->pos_curr = quant.pos_curr;
		dst->pos_prev = quant.pos_prev;
	}
#endif
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
#if defined(SSB64_NETMENU)
	{
		FTAttackColl quant;

		quant.attack_state = dst->attack_state;
		quant.pos_curr = dst->pos_curr;
		quant.pos_prev = dst->pos_prev;
		syNetplayQuantizeFTAttackColl(&quant);
		dst->pos_curr = quant.pos_curr;
		dst->pos_prev = quant.pos_prev;
	}
#endif
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

static sb32 syNetRbSnapFighterStatusIsDeadOrRebirth(s32 status_id)
{
	if ((status_id >= nFTCommonStatusDeadDown) && (status_id <= nFTCommonStatusDeadUpStar))
	{
		return TRUE;
	}
	if ((status_id >= nFTCommonStatusRebirthDown) && (status_id <= nFTCommonStatusRebirthWait))
	{
		return TRUE;
	}
	return FALSE;
}

static void syNetRbSnapCaptureFighterGobjPose(SYNetRbSnapFighterBlob *blob, const FTStruct *fp, GObj *fighter_gobj)
{
	if ((blob == NULL) || (fp == NULL))
	{
		return;
	}
	blob->is_invisible = (u8)(fp->is_invisible != FALSE);
	blob->is_ghost = (u8)(fp->is_ghost != FALSE);
	blob->is_rebirth = (u8)(fp->is_rebirth != FALSE);
	blob->is_shadow_hide = (u8)(fp->is_shadow_hide != FALSE);
	blob->is_menu_ignore = (u8)(fp->is_menu_ignore != FALSE);
	blob->is_playertag_hide = (u8)(fp->is_playertag_hide != FALSE);
	blob->is_limit_map_bounds = (u8)(fp->is_limit_map_bounds != FALSE);
	blob->is_ignore_dead = (u8)(fp->is_ignore_dead != FALSE);
	if (fighter_gobj != NULL)
	{
		blob->gobj_translate = DObjGetStruct(fighter_gobj)->translate.vec.f;
		blob->gobj_rotate = DObjGetStruct(fighter_gobj)->rotate.vec.f;
	}
}

static void syNetRbSnapApplyFighterGobjPose(const SYNetRbSnapFighterBlob *blob, FTStruct *fp, GObj *fighter_gobj)
{
	if ((blob == NULL) || (fp == NULL))
	{
		return;
	}
	fp->is_invisible = (blob->is_invisible != 0U) ? TRUE : FALSE;
	fp->is_ghost = (blob->is_ghost != 0U) ? TRUE : FALSE;
	fp->is_rebirth = (blob->is_rebirth != 0U) ? TRUE : FALSE;
	fp->is_shadow_hide = (blob->is_shadow_hide != 0U) ? TRUE : FALSE;
	fp->is_menu_ignore = (blob->is_menu_ignore != 0U) ? TRUE : FALSE;
	fp->is_playertag_hide = (blob->is_playertag_hide != 0U) ? TRUE : FALSE;
	fp->is_limit_map_bounds = (blob->is_limit_map_bounds != 0U) ? TRUE : FALSE;
	fp->is_ignore_dead = (blob->is_ignore_dead != 0U) ? TRUE : FALSE;
	if (fighter_gobj != NULL)
	{
		DObjGetStruct(fighter_gobj)->translate.vec.f = blob->gobj_translate;
		DObjGetStruct(fighter_gobj)->rotate.vec.f = blob->gobj_rotate;
#if defined(SSB64_NETMENU)
		syNetplayQuantizeVec3f(&DObjGetStruct(fighter_gobj)->translate.vec.f);
		syNetplayQuantizeVec3f(&DObjGetStruct(fighter_gobj)->rotate.vec.f);
#endif
	}
}

#if defined(SSB64_NETMENU)
/*
 * Make the ring-save full-hash oracle (ring_save_player full_ok) meaningful.
 *
 * syNetSyncHashBattleFightersFull() is the cross-peer frame-commit enforcement token; it folds the RAW live
 * FTStruct (physics velocities, map-collision pos_prev/pos_diff/velocities, all joint translates). The blob
 * is consumed only by syNetRbSnapApplyFighter, which re-canonicalizes the whole fighter through
 * syNetplayCanonicalizeFighterSimState on apply, so the blob never needs to be pre-quantized at capture.
 * Pre-quantizing it (formerly inline above + in syNetRbSnapCaptureMPColl) only made blob_full != live_full,
 * pinning `full_ok=0` on every tick even with zero divergence and blinding the oracle to real desyncs.
 *
 * Physics, root GObj pose, and joint transforms are now captured raw (the inline quantize was removed).
 * syNetRbSnapCaptureMPColl is shared with items/weapons, so rather than change that helper, mirror the raw live
 * map-collision scalars back into the fighter blob here. No live state is mutated (rendered geometry is
 * untouched); the blob is a faithful raw image of the committed state the FC token hashes, and quantization stays
 * at apply only.
 */
static void syNetRbSnapRefreshFighterBlobSimScalarsFromLive(SYNetRbSnapFighterBlob *blob, const FTStruct *fp)
{
	if ((blob == NULL) || (fp == NULL))
	{
		return;
	}
	blob->coll.pos_prev = fp->coll_data.pos_prev;
	blob->coll.pos_diff = fp->coll_data.pos_diff;
	blob->coll.vel_speed = fp->coll_data.vel_speed;
	blob->coll.vel_push = fp->coll_data.vel_push;
	blob->coll.line_coll_dist = fp->coll_data.line_coll_dist;
	blob->coll.floor_dist = fp->coll_data.floor_dist;
	blob->coll.floor_angle = fp->coll_data.floor_angle;
	blob->coll.ceil_angle = fp->coll_data.ceil_angle;
	blob->coll.lwall_angle = fp->coll_data.lwall_angle;
	blob->coll.rwall_angle = fp->coll_data.rwall_angle;
}
#endif

static void syNetRbSnapCaptureFighter(SYNetRbSnapFighterBlob *blob, FTStruct *fp, GObj *fighter_gobj)
{
	s32 ji;

#ifdef PORT
	syNetRbSnapSanitizeCaptureYoshiEffectGobj(fp);
#endif
#if defined(SSB64_NETMENU)
#if defined(PORT)
	if (fp->status_id == nFTCommonStatusTwister)
	{
		ftCommonTwisterReconcileRiderAfterRollback(fighter_gobj);
	}
#endif
#endif
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
	blob->coll_p_translate_valid = (fp->coll_data.p_translate != NULL) ? TRUE : FALSE;

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
	blob->breakout_wait = fp->breakout_wait;
	blob->breakout_lr = fp->breakout_lr;
	blob->breakout_ud = fp->breakout_ud;
	blob->dead_gate_wait = fp->dead_gate_wait;

	blob->is_attack_active = fp->is_attack_active;
	blob->is_hitstatus_nodamage = fp->is_hitstatus_nodamage;
	blob->is_fastfall = fp->is_fastfall;
	blob->is_hitstun = fp->is_hitstun;
	blob->is_shield = fp->is_shield;
	blob->is_damage_resist = fp->is_damage_resist;
	blob->is_cliff_hold = fp->is_cliff_hold;
	blob->is_catchstatus = fp->is_catchstatus;
	blob->is_catch_or_capture = fp->is_catch_or_capture;
	blob->is_effect_attach = (u8)(fp->is_effect_attach != FALSE);
	blob->tap_stick_x = fp->tap_stick_x;
	blob->tap_stick_y = fp->tap_stick_y;
	blob->hold_stick_x = fp->hold_stick_x;
	blob->hold_stick_y = fp->hold_stick_y;
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
#if defined(SSB64_NETMENU)
	syNetplayNessSanitizePKJibakuStatusVars(fp);
	syNetplayPikachuSanitizeQuickAttackStatusVars(fp);
#endif
	memcpy(blob->status_vars, &fp->status_vars, sizeof(blob->status_vars));
#if defined(SSB64_NETMENU)
	syNetplayNessRefreshPKThunderPosInBlobFromHead(fighter_gobj, fp, (union FTStatusVars *)blob->status_vars);
#endif
	if ((fp->status_id >= nFTCommonStatusDeadDown) && (fp->status_id <= nFTCommonStatusDeadUpFall))
	{
		((union FTStatusVars *)blob->status_vars)->common.dead.wait = fp->dead_gate_wait;
	}
	memcpy(blob->passive_vars, &fp->passive_vars, sizeof(blob->passive_vars));
#if defined(SSB64_NETMENU)
	syNetRbSnapQuantizeFighterRebirthStatusVars(fp, (union FTStatusVars *)blob->status_vars);
	syNetplayQuantizeNessPKJibakuStatusVars(fp, (union FTStatusVars *)blob->status_vars);
	syNetplayQuantizeNessPKThunderHoldStatusVars(fp, (union FTStatusVars *)blob->status_vars);
	syNetplayQuantizeNessPKThunderLandingStatusVars(fp, (union FTStatusVars *)blob->status_vars);
	syNetplayQuantizeNessPKThunderHoldPassiveVars(fp, &((union FTPassiveVars *)blob->passive_vars)->ness);
	syNetplayQuantizePikachuQuickAttackStatusVars(fp, (union FTStatusVars *)blob->status_vars);
	syNetplayQuantizePikachuQuickAttackLandingStatusVars(fp, (union FTStatusVars *)blob->status_vars);
#endif
#ifdef PORT
	syNetRbSnapScrubInactiveStatusVarsInBlob(blob);
	syNetRbSnapCaptureFighterCoupledIds(blob, fp);
	syNetRbSnapScrubCoupledPointersInBlob(blob);
#endif
	memcpy(blob->modelpart_status, fp->modelpart_status, sizeof(blob->modelpart_status));
	memcpy(blob->texturepart_status, fp->texturepart_status, sizeof(blob->texturepart_status));

	for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
	{
		if (fp->joints[ji] != NULL)
		{
			blob->joint_is_valid[ji] = TRUE;
			blob->joint_translate[ji] = fp->joints[ji]->translate.vec.f;
			blob->joint_rotate[ji] = fp->joints[ji]->rotate.vec.f;
			blob->joint_scale[ji] = fp->joints[ji]->scale.vec.f;
			syNetRbSnapCaptureDObjAnim(&blob->joint_anim[ji], fp->joints[ji]);
			blob->joint_dobj_flags[ji] = fp->joints[ji]->flags;
			blob->joint_anim_joint_event32[ji] =
			    (fp->joints[ji]->anim_joint.event32 != NULL) ? (uintptr_t)fp->joints[ji]->anim_joint.event32 : 0U;
		}
	}
#if defined(SSB64_NETMENU)
	if ((syNetplayFighterInNessPKThunderHoldSimScope(fp) != FALSE) ||
	    (syNetplayFighterInNessSpecialLwSimScope(fp) != FALSE))
	{
		syNetplayQuantizeFighterPhysics(&blob->physics);
		for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
		{
			if (blob->joint_is_valid[ji] == FALSE)
			{
				continue;
			}
			syNetplayQuantizeVec3f(&blob->joint_translate[ji]);
			syNetplayQuantizeVec3f(&blob->joint_rotate[ji]);
			syNetplayQuantizeVec3f(&blob->joint_scale[ji]);
		}
	}
#endif
	if (fighter_gobj != NULL)
	{
#if defined(SSB64_NETMENU)
		blob->gobj_anim_frame = syNetplayQuantizeAnimScalar(fighter_gobj->anim_frame);
#else
		blob->gobj_anim_frame = fighter_gobj->anim_frame;
#endif
	}
	syNetRbSnapCaptureFighterGobjPose(blob, fp, fighter_gobj);
#if defined(SSB64_NETMENU)
	syNetRbSnapRefreshFighterBlobSimScalarsFromLive(blob, fp);
#endif
}

static void syNetRbSnapRebindFighterStatusProcs(GObj *fighter_gobj, FTStruct *fp);
#if defined(SSB64_NETMENU)
static void syNetRbSnapApplyFighterNetplayPost(GObj *fighter_gobj, FTStruct *fp,
					       const SYNetRbSnapFighterBlob *blob);
#endif

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
#if defined(SSB64_NETMENU)
	syNetplayQuantizeFighterPhysics(&fp->physics);
#endif

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
	fp->breakout_wait = blob->breakout_wait;
	fp->breakout_lr = blob->breakout_lr;
	fp->breakout_ud = blob->breakout_ud;
	fp->dead_gate_wait = blob->dead_gate_wait;

	fp->is_attack_active = blob->is_attack_active;
	fp->is_hitstatus_nodamage = blob->is_hitstatus_nodamage;
	fp->is_fastfall = blob->is_fastfall;
	fp->is_hitstun = blob->is_hitstun;
	fp->is_shield = blob->is_shield;
	fp->is_damage_resist = (blob->is_damage_resist != 0U) ? TRUE : FALSE;
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
#if defined(SSB64_NETMENU)
	syNetplayNessSanitizePKJibakuStatusVars(fp);
	syNetplayNessSanitizePKThunderThrowStatusVars(fp);
#endif
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
			fp->joints[ji]->rotate.vec.f = blob->joint_rotate[ji];
			fp->joints[ji]->scale.vec.f = blob->joint_scale[ji];
#if defined(SSB64_NETMENU)
			syNetplayQuantizeVec3f(&fp->joints[ji]->translate.vec.f);
			syNetplayQuantizeVec3f(&fp->joints[ji]->rotate.vec.f);
			syNetplayQuantizeVec3f(&fp->joints[ji]->scale.vec.f);
#endif
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
#if defined(SSB64_NETMENU)
		fighter_gobj->anim_frame = syNetplayQuantizeAnimScalar(blob->gobj_anim_frame);
#else
		fighter_gobj->anim_frame = blob->gobj_anim_frame;
#endif
	}
	syNetRbSnapApplyFighterGobjPose(blob, fp, fighter_gobj);
#if defined(SSB64_NETMENU)
	syNetRbSnapQuantizeFighterRebirthStatusVars(fp, &fp->status_vars);
	syNetplayQuantizeNessPKJibakuStatusVars(fp, &fp->status_vars);
	syNetplayQuantizeNessPKThunderHoldStatusVars(fp, &fp->status_vars);
	syNetplayQuantizeNessPKThunderLandingStatusVars(fp, &fp->status_vars);
	syNetplayQuantizeNessPKThunderHoldPassiveVars(fp, &fp->passive_vars.ness);
	syNetplayQuantizePikachuQuickAttackStatusVars(fp, &fp->status_vars);
	syNetplayQuantizePikachuQuickAttackLandingStatusVars(fp, &fp->status_vars);
	syNetplayCanonicalizeFighterSimState(fighter_gobj);
	syNetRbSnapshotCanonicalizeSectorArwingDeckFighter(fighter_gobj);
	fp->tap_stick_x = blob->tap_stick_x;
	fp->tap_stick_y = blob->tap_stick_y;
	fp->hold_stick_x = blob->hold_stick_x;
	fp->hold_stick_y = blob->hold_stick_y;
#endif
#ifdef PORT
	syNetRbSnapVerifyDeadWaitInvariant(blob, fp);
	if ((blob->status_id >= nFTCommonStatusDeadDown) && (blob->status_id <= nFTCommonStatusDeadUpFall))
	{
		const union FTStatusVars *blob_sv = (const union FTStatusVars *)blob->status_vars;

		*ftStatusVarsDead(fp) = blob_sv->common.dead;
		ftCommonDeadSetWait(fp, fp->dead_gate_wait);
	}
#endif
#if defined(SSB64_NETMENU)
	syNetRbSnapApplyFighterNetplayPost(fighter_gobj, fp, blob);
	if ((syNetRbSnapFighterInRebirthScope(fp) != FALSE) &&
	    (blob->status_id >= nFTCommonStatusRebirthDown) && (blob->status_id <= nFTCommonStatusRebirthWait))
	{
		const union FTStatusVars *blob_sv = (const union FTStatusVars *)blob->status_vars;

		syNetplayRestoreRebirthStatusVars(fp, blob_sv);
		syNetRbSnapQuantizeFighterRebirthStatusVars(fp, &fp->status_vars);
		syNetplayRepairRebirthApexIfInverted(fp);
		syNetplayCanonicalizeRebirthFighterMapPose(fighter_gobj);
	}
#endif
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

static void syNetRbSnapVerifyDeadWaitInvariant(const SYNetRbSnapFighterBlob *blob, const FTStruct *fp)
{
	if ((blob == NULL) || (fp == NULL) || (syNetRbSnapFighterFieldDiffEnabled() == FALSE))
	{
		return;
	}
	if ((blob->status_id < nFTCommonStatusDeadDown) || (blob->status_id > nFTCommonStatusDeadUpFall))
	{
		return;
	}
	if (fp->dead_gate_wait != blob->dead_gate_wait)
	{
		port_log(
		    "SSB64 NetRbSnapshot: dead_gate_wait_invariant_fail player=%d status=%d live=%d blob=%d\n",
		    (int)fp->player, (int)fp->status_id, (int)fp->dead_gate_wait, (int)blob->dead_gate_wait);
	}
	if (ftStatusVarsDead(fp)->wait != fp->dead_gate_wait)
	{
		port_log(
		    "SSB64 NetRbSnapshot: dead_wait_union_mismatch player=%d status=%d union=%d bridge=%d\n",
		    (int)fp->player, (int)fp->status_id, (int)ftStatusVarsDead(fp)->wait, (int)fp->dead_gate_wait);
	}
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
	/*
	 * Link/NLink down-air bounce is driven by proc_hit (ftCommonAttackAirLwProcHit), which is
	 * installed on dair entry (ftCommonAttackAirCheckInterrupt) rather than from the status table.
	 * ftMainRebindStatusProcs leaves proc_hit NULL, and the function pointer is not part of the
	 * fighter blob, so a rollback/synctest rebind while Link is mid-dair drops the bounce handler:
	 * the next contact still registers damage (search-hit sets attack_damage) but ftMainProcParams
	 * has no proc_hit to call, so vel_air.y is never rebounded and Link tunnels through the victim.
	 */
	if ((fp->status_id == nFTCommonStatusAttackAirLw) &&
	    ((fp->fkind == nFTKindLink) || (fp->fkind == nFTKindNLink)))
	{
		fp->proc_hit = ftCommonAttackAirLwProcHit;
	}
	/*
	 * Ness/Yoshi double jump installs custom proc_physics on entry (ftCommonJumpAerialSetStatus),
	 * not via the status table. ftMainRebindStatusProcs restores generic JumpAerial physics and
	 * breaks animation-coupled Y velocity after rollback/synctest load.
	 */
	if (((fp->fkind == nFTKindNess) || (fp->fkind == nFTKindNNess)) &&
	    ((fp->status_id == nFTCommonStatusJumpAerialF) || (fp->status_id == nFTCommonStatusJumpAerialB)))
	{
		fp->proc_physics = ftNessJumpAerialProcPhysics;
	}
	else if (((fp->fkind == nFTKindYoshi) || (fp->fkind == nFTKindNYoshi)) &&
	         ((fp->status_id == nFTCommonStatusJumpAerialF) || (fp->status_id == nFTCommonStatusJumpAerialB)))
	{
		fp->proc_physics = ftYoshiJumpAerialProcPhysics;
	}
	/*
	 * SpecialHiEnd clears proc_damage on entry (ftNessSpecialHiClearProcDamage); status table leaves
	 * proc_damage NULL but ftMainRebindStatusProcs can restore a stale handler after snapshot load.
	 */
	if (((fp->fkind == nFTKindNess) || (fp->fkind == nFTKindNNess)) &&
	    ((fp->status_id == nFTNessStatusSpecialHiEnd) || (fp->status_id == nFTNessStatusSpecialAirHiEnd)))
	{
		fp->proc_damage = NULL;
	}
	syNetRbSnapRebindNessPKJibakuProcs(fighter_gobj, fp);
}

#if defined(SSB64_NETMENU)
static sb32 syNetRbSnapFighterStatusNeedsDeadProcRebind(s32 status_id)
{
	if ((status_id >= nFTCommonStatusDeadDown) && (status_id <= nFTCommonStatusDeadUpFall))
	{
		return TRUE;
	}
	if ((status_id >= nFTCommonStatusRebirthDown) && (status_id <= nFTCommonStatusRebirthWait))
	{
		return TRUE;
	}
	return FALSE;
}

static void syNetRbSnapApplyFighterNetplayPost(GObj *fighter_gobj, FTStruct *fp,
					       const SYNetRbSnapFighterBlob *blob)
{
	if (fp == NULL)
	{
		return;
	}
	syNetplayRebirthSnapSyncBattleStock(fp);
	syNetplayRebirthSanitizeIsRebirthFlag(fp);
	if (syNetplayRebirthShouldForceSleepSetStatus(fp) != FALSE)
	{
		if (fighter_gobj != NULL)
		{
			ftCommonSleepSetStatus(fighter_gobj);
			syNetRbSnapRebindFighterStatusProcs(fighter_gobj, fp);
		}
		return;
	}
	else if ((fp->stock_count == -1) || (fp->status_id == nFTCommonStatusSleep))
	{
		syNetplayRebirthApplyEliminationPresentation(fighter_gobj, fp);
	}
	if ((fighter_gobj != NULL) && (syNetRbSnapFighterStatusNeedsDeadProcRebind(fp->status_id) != FALSE))
	{
		syNetRbSnapRebindFighterStatusProcs(fighter_gobj, fp);
	}
	if ((fighter_gobj != NULL) && (sSYNetRbSnapDeferNetplayCatchUpDuringApply == FALSE))
	{
		syNetplayRebirthCatchUpDeadGateIfDue(fighter_gobj, fp);
		syNetplayRebirthCatchUpLifecycleIfDue(fighter_gobj, fp);
	}
	if ((fighter_gobj != NULL) && (fp->fkind == nFTKindFox) &&
	    (syNetplayFoxFighterInFirefoxSynctestDeferScope(fp->status_id) != FALSE))
	{
		syNetRbSnapRebindFighterStatusProcs(fighter_gobj, fp);
	}
	if (fighter_gobj != NULL)
	{
		syNetplayFoxCatchUpFirefoxLaunchIfDue(fighter_gobj, fp);
		syNetplayFoxCatchUpFirefoxEndIfDue(fighter_gobj, fp);
	}
	if ((fighter_gobj != NULL) &&
	    ((fp->fkind == nFTKindPikachu) || (fp->fkind == nFTKindNPikachu)) &&
	    ((syNetplayPikachuFighterInQuickAttackScope(fp->status_id) != FALSE) ||
	     (syNetplayPikachuFighterInQuickAttackEndScope(fp->status_id) != FALSE) ||
	     (syNetplayPikachuFighterInQuickAttackLandingFallScope(fp) != FALSE)))
	{
		syNetRbSnapRebindFighterStatusProcs(fighter_gobj, fp);
		if (sSYNetRbSnapDeferNetplayCatchUpDuringApply == FALSE)
		{
			syNetplayPikachuCatchUpQuickAttackIfDue(fighter_gobj, fp);
		}
	}
	if ((fighter_gobj != NULL) && (fp->status_id == nFTCommonStatusTwister))
	{
		ftCommonTwisterReconcileRiderAfterRollback(fighter_gobj);
	}
	if ((fighter_gobj != NULL) && (fp->fkind == nFTKindKirby) && (blob != NULL))
	{
		const ftKirbySpecialLwStatusVars *speciallw =
		    &((const union FTStatusVars *)blob->status_vars)->kirby.speciallw;

		ftKirbySpecialLwReconcileStoneAfterRollback(fighter_gobj, speciallw->duration,
							    (sb32)blob->is_damage_resist);
	}
	if ((fighter_gobj != NULL) && (syNetRbSnapFighterInYoshiEggLayScope(fp) != FALSE))
	{
		syNetRbSnapRebindFighterStatusProcs(fighter_gobj, fp);
	}
	if ((fighter_gobj != NULL) && ((fp->fkind == nFTKindNess) || (fp->fkind == nFTKindNNess)))
	{
		if (syNetRbSnapFighterInNessPKThunderScope(fp) != FALSE)
		{
			syNetplayNessSanitizePKThunderThrowStatusVars(fp);
			syNetRbSnapRebindFighterStatusProcs(fighter_gobj, fp);
			if (syNetRbSnapFighterIsInPKThunderSpecialHiStatus(fp) != FALSE)
			{
				GObj *pkthunder_gobj = fp->status_vars.ness.specialhi.pkthunder_gobj;

				if ((pkthunder_gobj == NULL) || (wpGetStruct(pkthunder_gobj) == NULL))
				{
					pkthunder_gobj = syNetRbSnapReacquirePKThunderHeadForFighter(fighter_gobj);
					fp->status_vars.ness.specialhi.pkthunder_gobj = pkthunder_gobj;
				}
				syNetRbSnapCullOwnedPKThunderForFighter(fighter_gobj, pkthunder_gobj);
			}
			else
			{
				fp->status_vars.ness.specialhi.pkthunder_gobj = NULL;
				syNetRbSnapCullOwnedPKThunderForFighter(fighter_gobj, NULL);
				if ((fp->is_effect_attach != FALSE) &&
				    (syNetRbSnapLiveFighterHasNessPKWave(fighter_gobj) == FALSE))
				{
					fp->is_effect_attach = FALSE;
				}
			}
		}
		if (syNetRbSnapFighterIsInPKFireSpecialNStatus(fp) != FALSE)
		{
			syNetRbSnapRebindFighterStatusProcs(fighter_gobj, fp);
			syNetRbSnapTrySpawnPKFireFromAccessory(fighter_gobj);
		}
		if (syNetplayNessFighterInPKJibakuCatchUpScope(fp) != FALSE)
		{
			syNetRbSnapRebindFighterStatusProcs(fighter_gobj, fp);
			syNetplayNessCatchUpPKJibakuIfDue(fighter_gobj, fp);
		}
	}
}
#endif

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
#ifdef PORT
			if ((fp->pkind == nFTPlayerKindMan) && (fp->player >= 0) && (fp->player < MAXCONTROLLERS))
			{
				fp->input.controller = &gSYControllerDevices[fp->player];
			}
			if (fp->item_gobj != NULL)
			{
				if (itGetStruct(fp->item_gobj) == NULL)
				{
					port_log(
					    "SSB64 NetRbSnapshot: prune stale item_gobj player=%d status=%d\n",
					    (int)fp->player,
					    (int)fp->status_id);
					fp->item_gobj = NULL;
				}
			}
#endif
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

static sb32 syNetRbSnapRingSaveDiagEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_SNAPSHOT_RING_SAVE_DIAG");
	s_env_cache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}

#if defined(PORT)
static u32 syNetRbSnapFnvAccumulateU32(u32 hash, u32 value);

static u32 syNetRbSnapHashF32ForFold(f32 value)
{
#if defined(SSB64_NETMENU)
	value = syNetplayQuantizeF32(value);
#endif
	return syNetRbSnapF32DiagBits(value);
}

/*
 * Mirror syNetSyncHashFighterStructLight / syNetSyncFoldFighterSlotFullContribution using blob fields only.
 */
static u32 syNetRbSnapHashFighterBlobLight(const SYNetRbSnapFighterBlob *blob)
{
	u32 h;
	const Vec3f *top;

	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return 2166136261U;
	}
	h = 2166136261U;
	h = syNetRbSnapFnvAccumulateU32(h, (u32)blob->player);
	h = syNetRbSnapFnvAccumulateU32(h, (u32)blob->fkind);
	h = syNetRbSnapFnvAccumulateU32(h, (u32)blob->status_id);
	h = syNetRbSnapFnvAccumulateU32(h, (u32)blob->motion_id);
	h = syNetRbSnapFnvAccumulateU32(h, (u32)blob->percent_damage);
	h = syNetRbSnapFnvAccumulateU32(h, (u32)blob->stock_count);
	h = syNetRbSnapFnvAccumulateU32(h, (u32)blob->lr);
	h = syNetRbSnapFnvAccumulateU32(h, (u32)(blob->ga != FALSE));

	h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->physics.vel_air.x));
	h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->physics.vel_air.y));
	h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->physics.vel_air.z));
	h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->physics.vel_ground.x));
	h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->physics.vel_ground.z));
	h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->physics.vel_damage_ground));

	h = syNetRbSnapFnvAccumulateU32(h, blob->hitlag_tics);
	h = syNetRbSnapFnvAccumulateU32(h, blob->status_total_tics);
	top = &blob->joint_translate[nFTPartsJointTopN];
	if (blob->joint_is_valid[nFTPartsJointTopN] != FALSE)
	{
		h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(top->x));
		h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(top->y));
		h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(top->z));
	}
	h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->physics.vel_damage_air.x));
	h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->physics.vel_damage_air.y));
	h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->physics.vel_damage_air.z));

	h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->coll.pos_prev.x));
	h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->coll.pos_prev.y));
	h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->coll.pos_prev.z));
	h = syNetRbSnapFnvAccumulateU32(h, (u32)blob->tap_stick_x);
	h = syNetRbSnapFnvAccumulateU32(h, (u32)blob->tap_stick_y);
	h = syNetRbSnapFnvAccumulateU32(h, (u32)blob->hold_stick_x);
	h = syNetRbSnapFnvAccumulateU32(h, (u32)blob->hold_stick_y);
	h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->coll.pos_diff.x));
	h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->coll.pos_diff.y));
	h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->coll.pos_diff.z));
	if (blob->coll_p_translate_valid != FALSE)
	{
		/* Live folds *coll_data.p_translate == &DObjGetStruct(fighter_gobj)->translate (the fighter root
		   GObj DObj, captured as blob->gobj_translate), NOT the TopN joint translate. */
		h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->gobj_translate.x));
		h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->gobj_translate.y));
		h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->gobj_translate.z));
	}
	h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->anim_vel.x));
	h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->anim_vel.y));
	h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->anim_vel.z));
	if (blob->status_id == nFTCommonStatusTwister)
	{
		const ftCommonTwisterStatusVars *twister_vars =
		    &((const union FTStatusVars *)blob->status_vars)->common.twister;

		h = syNetRbSnapFnvAccumulateU32(h, (u32)twister_vars->release_wait);
		/* Live folds (tornado_gobj != NULL) ? tornado_gobj->id : 0 (== coupled_twister_gobj_id). */
		h = syNetRbSnapFnvAccumulateU32(h, blob->coupled_twister_gobj_id);
		h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->physics.vel_air.x));
		h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->physics.vel_air.y));
		h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->physics.vel_air.z));
		/* Live folds DObjGetStruct(tornado_gobj)->translate only when that DObj exists (the tornado is a
		   weapon GObj, not part of this fighter blob), captured as twister_tornado_translate. */
		if (blob->twister_tornado_dobj_valid != 0U)
		{
			h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->twister_tornado_translate.x));
			h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->twister_tornado_translate.y));
			h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->twister_tornado_translate.z));
		}
	}
	if ((blob->status_id == nFTCommonStatusCaptureYoshi) || (blob->status_id == nFTCommonStatusYoshiEgg))
	{
		const ftCommonCaptureYoshiStatusVars *yoshi_vars =
		    &((const union FTStatusVars *)blob->status_vars)->common.captureyoshi;

		h = syNetRbSnapFnvAccumulateU32(h, (u32)blob->breakout_wait);
		h = syNetRbSnapFnvAccumulateU32(h, (u32)(u8)blob->breakout_lr);
		h = syNetRbSnapFnvAccumulateU32(h, (u32)(u8)blob->breakout_ud);
		h = syNetRbSnapFnvAccumulateU32(h, (u32)blob->motion_vars_flags[0]);
		h = syNetRbSnapFnvAccumulateU32(h, (u32)yoshi_vars->stage);
		h = syNetRbSnapFnvAccumulateU32(h, (u32)(u16)yoshi_vars->breakout_wait);
		h = syNetRbSnapFnvAccumulateU32(h, (u32)(u8)yoshi_vars->lr);
		h = syNetRbSnapFnvAccumulateU32(h, (u32)(yoshi_vars->is_damagefloor != FALSE));
		/* Live folds syNetRbSnapHashCaptureYoshiEffectGobjId(fp); the egg-lay effect id captured into the
		   blob equals that during CaptureYoshi/YoshiEgg (effect_gobj is the egg-lay effect or NULL). */
		h = syNetRbSnapFnvAccumulateU32(h, blob->captureyoshi_effect_gobj_id);
	}
	if (blob->status_id == nFTCommonStatusTaruCann)
	{
		const ftCommonTaruCannStatusVars *tarucann_vars =
		    &((const union FTStatusVars *)blob->status_vars)->common.tarucann;

		h = syNetRbSnapFnvAccumulateU32(h, (u32)tarucann_vars->release_wait);
		h = syNetRbSnapFnvAccumulateU32(h, (u32)tarucann_vars->shoot_wait);
	}
	if ((blob->fkind == nFTKindKirby) && (blob->status_id >= nFTKirbyStatusSpecialLwStart) &&
	    (blob->status_id <= nFTKirbyStatusSpecialAirLwEnd))
	{
		const ftKirbySpecialLwStatusVars *speciallw =
		    &((const union FTStatusVars *)blob->status_vars)->kirby.speciallw;

		h = syNetRbSnapFnvAccumulateU32(h, (u32)speciallw->duration);
		h = syNetRbSnapFnvAccumulateU32(h, (u32)(blob->is_damage_resist != FALSE));
	}
	if ((blob->status_id >= nFTCommonStatusDeadDown) && (blob->status_id <= nFTCommonStatusDeadUpFall))
	{
		h = syNetRbSnapFnvAccumulateU32(h, (u32)(s16)blob->dead_gate_wait);
	}
#if defined(SSB64_NETMENU)
	if ((syNetRbSnapshotSectorArwingDeckCollisionLive() != FALSE) && (blob->coll.floor_line_id == 1))
	{
		h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->coll.vel_speed.x));
		h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->coll.vel_speed.y));
		h = syNetRbSnapFnvAccumulateU32(h, syNetRbSnapHashF32ForFold(blob->coll.vel_speed.z));
		h = syNetRbSnapFnvAccumulateU32(h, (u32)blob->coll.floor_flags);
	}
#endif
	return h;
}

static u32 syNetRbSnapHashFighterBlobFull(const SYNetRbSnapFighterBlob *blob)
{
	u32 contribution;
	s32 ji;

	contribution = syNetRbSnapHashFighterBlobLight(blob);
	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return contribution;
	}
	contribution = syNetRbSnapFnvAccumulateU32(contribution, (u32)blob->shield_health);
	contribution = syNetRbSnapFnvAccumulateU32(contribution, (u32)blob->jumps_used);
	contribution = syNetRbSnapFnvAccumulateU32(contribution, syNetRbSnapHashF32ForFold(blob->physics.vel_jostle_x));
	contribution = syNetRbSnapFnvAccumulateU32(contribution, syNetRbSnapHashF32ForFold(blob->physics.vel_jostle_z));
	contribution = syNetRbSnapFnvAccumulateU32(contribution, (u32)blob->motion_attack_id);
	contribution = syNetRbSnapFnvAccumulateU32(contribution, (u32)blob->hitstatus);
	contribution = syNetRbSnapFnvAccumulateU32(contribution, (u32)blob->invincible_tics);
	contribution = syNetRbSnapFnvAccumulateU32(contribution, (u32)(blob->is_hitstun != FALSE));
	contribution = syNetRbSnapFnvAccumulateU32(contribution, (u32)(blob->is_shield != FALSE));
	for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
	{
		if (blob->joint_is_valid[ji] == FALSE)
		{
			continue;
		}
		contribution =
		    syNetRbSnapFnvAccumulateU32(contribution, syNetRbSnapHashF32ForFold(blob->joint_translate[ji].x));
		contribution =
		    syNetRbSnapFnvAccumulateU32(contribution, syNetRbSnapHashF32ForFold(blob->joint_translate[ji].y));
		contribution =
		    syNetRbSnapFnvAccumulateU32(contribution, syNetRbSnapHashF32ForFold(blob->joint_translate[ji].z));
		/* Mirror the live fold (syNetSyncFoldFighterSlotFullContribution): joint rotate after translate. */
		contribution =
		    syNetRbSnapFnvAccumulateU32(contribution, syNetRbSnapHashF32ForFold(blob->joint_rotate[ji].x));
		contribution =
		    syNetRbSnapFnvAccumulateU32(contribution, syNetRbSnapHashF32ForFold(blob->joint_rotate[ji].y));
		contribution =
		    syNetRbSnapFnvAccumulateU32(contribution, syNetRbSnapHashF32ForFold(blob->joint_rotate[ji].z));
	}
	return contribution;
}

static u32 syNetRbSnapFoldBlobAnimJoint(const SYNetRbSnapDObjAnimBlob *joint_anim, u32 fold)
{
	u32 aobj_steps;
	u32 chain_total;
	u32 apply_count;

	if (joint_anim == NULL)
	{
		return fold;
	}
	fold = syNetRbSnapFnvAccumulateU32(fold, syNetRbSnapHashF32ForFold(joint_anim->anim_frame));
	fold = syNetRbSnapFnvAccumulateU32(fold, syNetRbSnapHashF32ForFold(joint_anim->anim_wait));
	fold = syNetRbSnapFnvAccumulateU32(fold, syNetRbSnapHashF32ForFold(joint_anim->anim_speed));
	chain_total = (u32)joint_anim->aobj_chain_total;
	fold = syNetRbSnapFnvAccumulateU32(fold, chain_total);
	apply_count = (u32)joint_anim->aobj_count;
	if (apply_count > (u32)SYNETROLLBACK_SNAPSHOT_AOBJ_CHAIN_MAX)
	{
		apply_count = (u32)SYNETROLLBACK_SNAPSHOT_AOBJ_CHAIN_MAX;
	}
	for (aobj_steps = 0U; aobj_steps < apply_count; aobj_steps++)
	{
		const SYNetRbSnapAObjNodeBlob *aobj = &joint_anim->aobj[aobj_steps];

		fold = syNetRbSnapFnvAccumulateU32(fold, (u32)aobj->track);
		fold = syNetRbSnapFnvAccumulateU32(fold, (u32)aobj->kind);
		fold = syNetRbSnapFnvAccumulateU32(fold, syNetRbSnapHashF32ForFold(aobj->length_invert));
		fold = syNetRbSnapFnvAccumulateU32(fold, syNetRbSnapHashF32ForFold(aobj->length));
		fold = syNetRbSnapFnvAccumulateU32(fold, syNetRbSnapHashF32ForFold(aobj->value_base));
		fold = syNetRbSnapFnvAccumulateU32(fold, syNetRbSnapHashF32ForFold(aobj->value_target));
		fold = syNetRbSnapFnvAccumulateU32(fold, syNetRbSnapHashF32ForFold(aobj->rate_base));
		fold = syNetRbSnapFnvAccumulateU32(fold, syNetRbSnapHashF32ForFold(aobj->rate_target));
	}
	return fold;
}

static u32 syNetRbSnapHashFighterBlobAnim(const SYNetRbSnapFighterBlob *blob)
{
	u32 fold;
	s32 ji;

	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return 2166136261U;
	}
	fold = 2166136261U;
	fold = syNetRbSnapFnvAccumulateU32(fold, (u32)blob->status_id);
	fold = syNetRbSnapFnvAccumulateU32(fold, (u32)blob->motion_id);
	fold = syNetRbSnapFnvAccumulateU32(fold, syNetRbSnapHashF32ForFold(blob->gobj_anim_frame));
	for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
	{
		if (blob->joint_is_valid[ji] == FALSE)
		{
			continue;
		}
		fold = syNetRbSnapFoldBlobAnimJoint(&blob->joint_anim[ji], fold);
	}
	return fold;
}

static u32 syNetRbSnapMergeFighterBlobHashes(const SYNetRbSnapshotSlot *slot)
{
	u32 slot_hash[GMCOMMON_PLAYERS_MAX];
	u32 merged;
	s32 si;
	s32 pidx;

	for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
	{
		slot_hash[si] = 2166136261U;
	}
	if (slot == NULL)
	{
		return 2166136261U;
	}
	for (pidx = 0; pidx < GMCOMMON_PLAYERS_MAX; pidx++)
	{
		const SYNetRbSnapFighterBlob *blob;
		u32 contribution;

		blob = &slot->fighters[pidx];
		if (blob->is_valid == FALSE)
		{
			continue;
		}
		contribution = syNetRbSnapHashFighterBlobFull(blob);
		slot_hash[pidx] = syNetRbSnapFnvAccumulateU32(slot_hash[pidx] ^ contribution, (u32)pidx ^ 0x9E3779B9U);
	}
	merged = 2166136261U;
	for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
	{
		merged = syNetRbSnapFnvAccumulateU32(merged ^ slot_hash[si], (u32)si);
	}
	return merged;
}

static u32 syNetRbSnapMergeFighterBlobAnimHashes(const SYNetRbSnapshotSlot *slot)
{
	u32 slot_hash[GMCOMMON_PLAYERS_MAX];
	u32 merged;
	s32 si;
	s32 pidx;

	for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
	{
		slot_hash[si] = 2166136261U;
	}
	if (slot == NULL)
	{
		return 2166136261U;
	}
	for (pidx = 0; pidx < GMCOMMON_PLAYERS_MAX; pidx++)
	{
		const SYNetRbSnapFighterBlob *blob;
		u32 contribution;

		blob = &slot->fighters[pidx];
		if (blob->is_valid == FALSE)
		{
			continue;
		}
		contribution = syNetRbSnapHashFighterBlobAnim(blob);
		slot_hash[pidx] = syNetRbSnapFnvAccumulateU32(slot_hash[pidx] ^ contribution, (u32)pidx ^ 0x9E3779B9U);
	}
	merged = 2166136261U;
	for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
	{
		merged = syNetRbSnapFnvAccumulateU32(merged ^ slot_hash[si], (u32)si);
	}
	return merged;
}

static void syNetRbSnapLogRingSaveDiag(const SYNetRbSnapshotSlot *slot, u32 completed_sim_tick)
{
	GObj *fighter_gobj;
	u32 live_figh_full;
	u32 live_figh_light;
	u32 live_anim;
	u32 live_eff;
	u32 blob_figh;
	u32 blob_anim;
	u32 figh_ok;
	u32 anim_ok;
	u32 blob_ok;

	if ((slot == NULL) || (syNetRbSnapRingSaveDiagEnabled() == FALSE))
	{
		return;
	}
	live_figh_full = syNetSyncHashBattleFightersFull();
	live_figh_light = syNetSyncHashBattleFighters();
	live_anim = syNetSyncHashFighterAnimationStateForRollback();
	live_eff = syNetSyncHashActiveEffectsForRollback();
	blob_figh = syNetRbSnapMergeFighterBlobHashes(slot);
	blob_anim = syNetRbSnapMergeFighterBlobAnimHashes(slot);
	figh_ok = (slot->hash_fighter == live_figh_full) ? 1U : 0U;
	anim_ok = (slot->hash_animation == live_anim) ? 1U : 0U;
	blob_ok = (slot->hash_fighter == blob_figh) ? 1U : 0U;
	port_log(
	    "SSB64 NetRbSnapshot: ring_save_diag tick=%u ring_figh=0x%08X live_figh_full=0x%08X live_figh_light=0x%08X "
	    "figh_ok=%u ring_anim=0x%08X live_anim=0x%08X anim_ok=%u blob_figh=0x%08X blob_anim=0x%08X blob_ok=%u "
	    "ring_eff=0x%08X live_eff=0x%08X\n",
	    completed_sim_tick, slot->hash_fighter, live_figh_full, live_figh_light, figh_ok, slot->hash_animation,
	    live_anim, anim_ok, blob_figh, blob_anim, blob_ok, slot->hash_effect, live_eff);
	syNetSyncLogActiveEffectsFoldDiag("save", completed_sim_tick);

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		const SYNetRbSnapFighterBlob *blob;
		s32 pidx;
		u32 live_full;
		u32 live_light;
		u32 live_slot_anim;
		u32 blob_full;
		u32 blob_light;
		u32 blob_anim_slot;
		u32 full_ok;
		u32 anim_slot_ok;
		u32 log_player;
		u32 atk_y_bits;
		u32 vel_y_live;
		u32 vel_y_blob;
		u32 proc_hit_code;
		s32 rehit_timer;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			continue;
		}
		pidx = fp->player;
		if ((pidx < 0) || (pidx >= GMCOMMON_PLAYERS_MAX))
		{
			continue;
		}
		blob = &slot->fighters[pidx];
		if (blob->is_valid == FALSE)
		{
			continue;
		}
		live_full = syNetSyncHashFighterSlotFull(fp);
		live_light = syNetSyncHashFighterStructLight(fp);
		live_slot_anim = syNetSyncHashFighterSlotAnim(fp, fighter_gobj);
		blob_full = syNetRbSnapHashFighterBlobFull(blob);
		blob_light = syNetRbSnapHashFighterBlobLight(blob);
		blob_anim_slot = syNetRbSnapHashFighterBlobAnim(blob);
		full_ok = (live_full == blob_full) ? 1U : 0U;
		anim_slot_ok = (live_slot_anim == blob_anim_slot) ? 1U : 0U;
		log_player =
		    ((full_ok == 0U) || (anim_slot_ok == 0U) || (fp->status_id == nFTCommonStatusAttackAirLw)) ? 1U : 0U;
		if (log_player == 0U)
		{
			continue;
		}
		atk_y_bits = 0U;
		if (blob->attack_colls[0].attack_state != nGMAttackStateOff)
		{
			atk_y_bits = syNetRbSnapHashF32ForFold(blob->attack_colls[0].pos_curr.y);
		}
		/*
		 * Link/NLink down-air bounce (ftCommonAttackAirLwProcHit) is the hit-without-bounce
		 * failure mode: a synced hit still sets attack_damage, but if proc_hit was lost (e.g.
		 * cleared by a rollback rebind) ftMainProcParams never rebounds vel_air.y. Surface the
		 * live vs blob vel_air.y, the live attack_damage, and the proc_hit identity so a soak can
		 * pin the exact pass-through frame instead of inferring it from the figh hash drift.
		 *   proc_hit_code: 0 = NULL, 1 = ftCommonAttackAirLwProcHit, 2 = other non-NULL handler.
		 */
		vel_y_live = syNetRbSnapF32DiagBits(fp->physics.vel_air.y);
		vel_y_blob = syNetRbSnapF32DiagBits(blob->physics.vel_air.y);
		if (fp->proc_hit == NULL)
		{
			proc_hit_code = 0U;
		}
		else if (fp->proc_hit == ftCommonAttackAirLwProcHit)
		{
			proc_hit_code = 1U;
		}
		else
		{
			proc_hit_code = 2U;
		}
		rehit_timer = 0;
		if (syNetRbSnapFighterInYoshiEggLayScope(fp) != FALSE)
		{
			rehit_timer = (s32)ftStatusVarsCaptureYoshi(fp)->breakout_wait;
		}
		else if (syNetRbSnapFighterStatusIsAttackAir(fp->status_id) != FALSE)
		{
			rehit_timer = (s32)ftStatusVarsAttackAir(fp)->rehit_timer;
		}
		port_log(
		    "SSB64 NetRbSnapshot: ring_save_player tick=%u player=%d fkind=%d status=%d motion=%d "
		    "live_full=0x%08X blob_full=0x%08X full_ok=%u live_anim=0x%08X blob_anim=0x%08X anim_ok=%u "
		    "live_light=0x%08X blob_light=0x%08X atk0_y=0x%08X vel_y_live=0x%08X vel_y_blob=0x%08X "
		    "atk_dmg=%d proc_hit=%u rehit=%d\n",
		    completed_sim_tick, (int)pidx, (int)fp->fkind, (int)fp->status_id, (int)fp->motion_id, live_full,
		    blob_full, full_ok, live_slot_anim, blob_anim_slot, anim_slot_ok, live_light, blob_light, atk_y_bits,
		    vel_y_live, vel_y_blob, (int)fp->attack_damage, proc_hit_code, rehit_timer);
	}
}
#endif /* PORT */

static void syNetRbSnapLogFieldDiffScalar(const char *tag, u32 tick, s32 player, const char *field, u32 live_bits,
                                          u32 blob_bits)
{
	if (live_bits != blob_bits)
	{
		port_log("SSB64 NetRbSnapshot: fighter_field_diff tag=%s tick=%u player=%d field=%s live=0x%08X blob=0x%08X\n",
		         tag, tick, (int)player, field, live_bits, blob_bits);
	}
}

static void syNetRbSnapLogMultiRebirthSummary(u32 tick, const char *tag, const SYNetRbSnapshotSlot *slot)
{
	s32 rebirth_count;
	s32 pidx;

	if ((slot == NULL) || (tag == NULL))
	{
		return;
	}
	rebirth_count = 0;
	for (pidx = 0; pidx < GMCOMMON_PLAYERS_MAX; pidx++)
	{
		const SYNetRbSnapFighterBlob *blob;
		GObj *fighter_gobj;
		FTStruct *fp;

		blob = &slot->fighters[pidx];
		if (blob->is_valid == FALSE)
		{
			continue;
		}
		fighter_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)pidx);
		fp = (fighter_gobj != NULL) ? ftGetStruct(fighter_gobj) : NULL;
		if ((fp != NULL) && (syNetRbSnapFighterInRebirthScope(fp) != FALSE))
		{
			rebirth_count++;
		}
	}
	if (rebirth_count < 2)
	{
		return;
	}
	port_log(
	    "SSB64 NetRbSnapshot: multi_rebirth_summary tag=%s tick=%u effect_count=%d rebirth_count=%d\n",
	    tag, tick, (int)slot->effect_count, (int)rebirth_count);
	for (pidx = 0; pidx < GMCOMMON_PLAYERS_MAX; pidx++)
	{
		const SYNetRbSnapFighterBlob *blob;
		GObj *fighter_gobj;
		FTStruct *fp;

		blob = &slot->fighters[pidx];
		if (blob->is_valid == FALSE)
		{
			continue;
		}
		fighter_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)pidx);
		fp = (fighter_gobj != NULL) ? ftGetStruct(fighter_gobj) : NULL;
		if ((fp == NULL) || (syNetRbSnapFighterInRebirthScope(fp) == FALSE))
		{
			continue;
		}
		port_log(
		    "SSB64 NetRbSnapshot: multi_rebirth_player tag=%s tick=%u player=%d status=%d halo_num=%d halo_lower=%d\n",
		    tag, tick, (int)pidx, (int)fp->status_id, (int)ftStatusVarsRebirth(fp)->halo_number,
		    (int)ftStatusVarsRebirth(fp)->halo_lower_wait);
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
	syNetRbSnapLogMultiRebirthSummary(tick, reason, slot);
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
		syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "is_damage_resist",
		                              (u32)(fp->is_damage_resist != FALSE), (u32)(blob->is_damage_resist != FALSE));
		if ((syNetRbSnapFighterInGuardScope(fp) != FALSE) || (syNetRbSnapBlobInGuardScope(blob) != FALSE))
		{
			u32 live_guard_eff_id;
			u32 blob_guard_eff_id;

			live_guard_eff_id = syNetRbSnapGobjId(ftStatusVarsGuard(fp)->effect_gobj);
			if (syNetRbSnapFighterInGuardScope(fp) == FALSE)
			{
				live_guard_eff_id = 0U;
			}
			blob_guard_eff_id = syNetRbSnapGuardEffectIdFromBlob(blob);
			syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "guard_effect_gobj_id", live_guard_eff_id,
			                              blob_guard_eff_id);
		}
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
		dobj = (fighter_gobj != NULL) ? DObjGetStruct(fighter_gobj) : NULL;
		if (dobj != NULL)
		{
			syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "gobj_translate_x",
			                              syNetRbSnapF32DiagBits(dobj->translate.vec.f.x),
			                              syNetRbSnapF32DiagBits(blob->gobj_translate.x));
			syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "gobj_translate_y",
			                              syNetRbSnapF32DiagBits(dobj->translate.vec.f.y),
			                              syNetRbSnapF32DiagBits(blob->gobj_translate.y));
			syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "gobj_translate_z",
			                              syNetRbSnapF32DiagBits(dobj->translate.vec.f.z),
			                              syNetRbSnapF32DiagBits(blob->gobj_translate.z));
			syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "gobj_rotate_y",
			                              syNetRbSnapF32DiagBits(dobj->rotate.vec.f.y),
			                              syNetRbSnapF32DiagBits(blob->gobj_rotate.y));
		}
		if (fp->joints[nFTPartsJointTopN] != NULL)
		{
			syNetRbSnapLogFieldDiffScalar(
			    reason, tick, slot_index, "top_joint_y",
			    syNetRbSnapF32DiagBits(fp->joints[nFTPartsJointTopN]->translate.vec.f.y),
			    syNetRbSnapF32DiagBits(blob->joint_translate[nFTPartsJointTopN].y));
			syNetRbSnapLogFieldDiffScalar(
			    reason, tick, slot_index, "top_joint_ry",
			    syNetRbSnapF32DiagBits(fp->joints[nFTPartsJointTopN]->rotate.vec.f.y),
			    syNetRbSnapF32DiagBits(blob->joint_rotate[nFTPartsJointTopN].y));
		}
		syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "coll_pos_prev_y",
		                              syNetRbSnapF32DiagBits(fp->coll_data.pos_prev.y),
		                              syNetRbSnapF32DiagBits(blob->coll.pos_prev.y));
		if (syNetRbSnapFighterStatusIsDeadOrRebirth(fp->status_id) != FALSE)
		{
			syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "stock_count", (u32)fp->stock_count,
			                              (u32)blob->stock_count);
			syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "hitlag_tics", fp->hitlag_tics,
			                              blob->hitlag_tics);
			syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "is_invisible",
			                              (u32)(fp->is_invisible != FALSE), (u32)blob->is_invisible);
			syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "is_ghost", (u32)(fp->is_ghost != FALSE),
			                              (u32)blob->is_ghost);
			syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "is_rebirth", (u32)(fp->is_rebirth != FALSE),
			                              (u32)blob->is_rebirth);
			syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "is_shadow_hide",
			                              (u32)(fp->is_shadow_hide != FALSE), (u32)blob->is_shadow_hide);
			syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "is_menu_ignore",
			                              (u32)(fp->is_menu_ignore != FALSE), (u32)blob->is_menu_ignore);
			syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "is_playertag_hide",
			                              (u32)(fp->is_playertag_hide != FALSE), (u32)blob->is_playertag_hide);
			if ((fp->status_id >= nFTCommonStatusDeadDown) && (fp->status_id <= nFTCommonStatusDeadUpFall))
			{
				syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "dead_gate_wait",
				                              (u32)fp->dead_gate_wait, (u32)blob->dead_gate_wait);
				syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "dead_wait",
				                              (u32)ftStatusVarsDead(fp)->wait,
				                              (u32)blob->dead_gate_wait);
			}
			if ((fp->status_id >= nFTCommonStatusRebirthDown) && (fp->status_id <= nFTCommonStatusRebirthWait))
			{
				union FTStatusVars blob_rebirth_vars;

				memcpy(&blob_rebirth_vars, blob->status_vars, sizeof(blob_rebirth_vars));
				syNetRbSnapLogFieldDiffScalar(
				    reason, tick, slot_index, "rebirth_pos_y",
				    syNetRbSnapF32DiagBits(ftStatusVarsRebirth(fp)->pos.y),
				    syNetRbSnapF32DiagBits(blob_rebirth_vars.common.rebirth.pos.y));
				syNetRbSnapLogFieldDiffScalar(
				    reason, tick, slot_index, "rebirth_halo_offset_y",
				    syNetRbSnapF32DiagBits(ftStatusVarsRebirth(fp)->halo_offset.y),
				    syNetRbSnapF32DiagBits(blob_rebirth_vars.common.rebirth.halo_offset.y));
				syNetRbSnapLogFieldDiffScalar(
				    reason, tick, slot_index, "halo_lower_wait",
				    (u32)ftStatusVarsRebirth(fp)->halo_lower_wait,
				    (u32)blob_rebirth_vars.common.rebirth.halo_lower_wait);
				syNetRbSnapLogFieldDiffScalar(
				    reason, tick, slot_index, "halo_despawn_wait",
				    (u32)ftStatusVarsRebirth(fp)->halo_despawn_wait,
				    (u32)blob_rebirth_vars.common.rebirth.halo_despawn_wait);
				syNetRbSnapLogFieldDiffScalar(
				    reason, tick, slot_index, "halo_number",
				    (u32)ftStatusVarsRebirth(fp)->halo_number,
				    (u32)blob_rebirth_vars.common.rebirth.halo_number);
			}
		}
		if (fp->fkind == nFTKindFox)
		{
			u32 live_fox_eff_id;
			u32 blob_fox_eff_id;

			live_fox_eff_id = syNetRbSnapGobjId(fp->status_vars.fox.speciallw.effect_gobj);
			if (syNetRbSnapFighterInFoxReflectorScope(fp) == FALSE)
			{
				live_fox_eff_id = 0U;
			}
			blob_fox_eff_id = syNetRbSnapFoxSpecialLwEffectIdFromBlob(blob);
			syNetRbSnapLogFieldDiffScalar(reason, tick, slot_index, "fox_speciallw_effect_gobj_id",
			                              live_fox_eff_id, blob_fox_eff_id);
			if (syNetplayFoxFighterInFirefoxHoldScope(fp->status_id) != FALSE)
			{
				union FTStatusVars blob_fox_vars;

				memcpy(&blob_fox_vars, blob->status_vars, sizeof(blob_fox_vars));
				syNetRbSnapLogFieldDiffScalar(
				    reason, tick, slot_index, "fox_launch_delay",
				    (u32)fp->status_vars.fox.specialhi.launch_delay,
				    (u32)blob_fox_vars.fox.specialhi.launch_delay);
			}
			if (syNetplayFoxFighterInFirefoxTravelScope(fp->status_id) != FALSE)
			{
				union FTStatusVars blob_fox_vars;

				memcpy(&blob_fox_vars, blob->status_vars, sizeof(blob_fox_vars));
				syNetRbSnapLogFieldDiffScalar(
				    reason, tick, slot_index, "fox_anim_frames",
				    (u32)fp->status_vars.fox.specialhi.anim_frames,
				    (u32)blob_fox_vars.fox.specialhi.anim_frames);
				syNetRbSnapLogFieldDiffScalar(
				    reason, tick, slot_index, "fox_decelerate_wait",
				    (u32)fp->status_vars.fox.specialhi.decelerate_wait,
				    (u32)blob_fox_vars.fox.specialhi.decelerate_wait);
				syNetRbSnapLogFieldDiffScalar(
				    reason, tick, slot_index, "fox_angle",
				    syNetRbSnapF32DiagBits(fp->status_vars.fox.specialhi.angle),
				    syNetRbSnapF32DiagBits(blob_fox_vars.fox.specialhi.angle));
			}
		}
		for (ji = 0; ji < 4; ji++)
		{
			if (fp->joints[ji] != NULL)
			{
				char field_name[24];

				snprintf(field_name, sizeof(field_name), "joint%u_tx", (unsigned int)ji);
				syNetRbSnapLogFieldDiffScalar(
				    reason, tick, slot_index, field_name,
				    syNetRbSnapF32DiagBits(fp->joints[ji]->translate.vec.f.x),
				    syNetRbSnapF32DiagBits(blob->joint_translate[ji].x));
				snprintf(field_name, sizeof(field_name), "joint%u_ty", (unsigned int)ji);
				syNetRbSnapLogFieldDiffScalar(
				    reason, tick, slot_index, field_name,
				    syNetRbSnapF32DiagBits(fp->joints[ji]->translate.vec.f.y),
				    syNetRbSnapF32DiagBits(blob->joint_translate[ji].y));
				snprintf(field_name, sizeof(field_name), "joint%u_tz", (unsigned int)ji);
				syNetRbSnapLogFieldDiffScalar(
				    reason, tick, slot_index, field_name,
				    syNetRbSnapF32DiagBits(fp->joints[ji]->translate.vec.f.z),
				    syNetRbSnapF32DiagBits(blob->joint_translate[ji].z));
				snprintf(field_name, sizeof(field_name), "joint%u_rx", (unsigned int)ji);
				syNetRbSnapLogFieldDiffScalar(
				    reason, tick, slot_index, field_name,
				    syNetRbSnapF32DiagBits(fp->joints[ji]->rotate.vec.f.x),
				    syNetRbSnapF32DiagBits(blob->joint_rotate[ji].x));
				snprintf(field_name, sizeof(field_name), "joint%u_ry", (unsigned int)ji);
				syNetRbSnapLogFieldDiffScalar(
				    reason, tick, slot_index, field_name,
				    syNetRbSnapF32DiagBits(fp->joints[ji]->rotate.vec.f.y),
				    syNetRbSnapF32DiagBits(blob->joint_rotate[ji].y));
				snprintf(field_name, sizeof(field_name), "joint%u_rz", (unsigned int)ji);
				syNetRbSnapLogFieldDiffScalar(
				    reason, tick, slot_index, field_name,
				    syNetRbSnapF32DiagBits(fp->joints[ji]->rotate.vec.f.z),
				    syNetRbSnapF32DiagBits(blob->joint_rotate[ji].z));
			}
		}
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
	if (speed_out != NULL)
	{
		*speed_out = yaku->speed;
	}
	dobj->user_data.s = yaku->user_data_s;
}

static void syNetRbSnapCaptureMap(SYNetRbSnapshotSlot *slot)
{
	s32 i;
	s32 n;
	s32 cap;
	DObj *dobj;

#ifdef PORT
	syNetRbSnapReconcileSectorArwingDeckYakumonoFromFlightTree();
#endif
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
#ifdef PORT
	/* Patrol deck is flight-tree derived; re-seat line 1 after reconcile so ring blob matches kin hash. */
	if ((cap > 1) && (syNetRbSnapSectorArwingDeckYakumonoDerivedLive() != FALSE) &&
	    (gMPCollisionYakumonoDObjs->dobjs[1] != NULL))
	{
		syNetRbSnapCaptureYakuDObj(&slot->mp_yaku[1], gMPCollisionYakumonoDObjs->dobjs[1], &gMPCollisionSpeeds[1]);
	}
#endif
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
#ifdef PORT
		if ((i == 1) && (syNetRbSnapSectorArwingDeckYakumonoDerivedFromSlot(slot) != FALSE))
		{
			continue;
		}
#endif
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
}

#if defined(SSB64_NETMENU)
static s32 syNetRbCompareF32ForItemSort(f32 a, f32 b)
{
	f32 qa;
	f32 qb;

	qa = syNetplayQuantizeF32(a);
	qb = syNetplayQuantizeF32(b);
	if (qa < qb)
	{
		return -1;
	}
	if (qa > qb)
	{
		return 1;
	}
	return 0;
}

/*
 * Stable sort key aligned with syNetSyncFoldActiveItemGobjForRollback fields (excludes gobj->id).
 * gobj->id is a final tiebreaker only so equal semantic items have deterministic order.
 */
static s32 syNetRbCompareItemGobjsForRollbackHash(GObj *a, GObj *b)
{
	ITStruct *ip_a;
	ITStruct *ip_b;
	DObj *da;
	DObj *db;
	s32 cmp;

	if (a == b)
	{
		return 0;
	}
	if (a == NULL)
	{
		return -1;
	}
	if (b == NULL)
	{
		return 1;
	}
	ip_a = itGetStruct(a);
	ip_b = itGetStruct(b);
	if (ip_a == NULL)
	{
		return (ip_b == NULL) ? 0 : -1;
	}
	if (ip_b == NULL)
	{
		return 1;
	}
	if (ip_a->kind != ip_b->kind)
	{
		return (ip_a->kind < ip_b->kind) ? -1 : 1;
	}
	if (ip_a->player != ip_b->player)
	{
		return (ip_a->player < ip_b->player) ? -1 : 1;
	}
	if (ip_a->type != ip_b->type)
	{
		return (ip_a->type < ip_b->type) ? -1 : 1;
	}
	if (ip_a->team != ip_b->team)
	{
		return (ip_a->team < ip_b->team) ? -1 : 1;
	}
	if (ip_a->multi != ip_b->multi)
	{
		return (ip_a->multi < ip_b->multi) ? -1 : 1;
	}
	if (ip_a->event_id != ip_b->event_id)
	{
		return (ip_a->event_id < ip_b->event_id) ? -1 : 1;
	}
	da = DObjGetStruct(a);
	db = DObjGetStruct(b);
	if ((da != NULL) && (db != NULL))
	{
		cmp = syNetRbCompareF32ForItemSort(da->translate.vec.f.x, db->translate.vec.f.x);
		if (cmp != 0)
		{
			return cmp;
		}
		cmp = syNetRbCompareF32ForItemSort(da->translate.vec.f.y, db->translate.vec.f.y);
		if (cmp != 0)
		{
			return cmp;
		}
		cmp = syNetRbCompareF32ForItemSort(da->translate.vec.f.z, db->translate.vec.f.z);
		if (cmp != 0)
		{
			return cmp;
		}
	}
	cmp = syNetRbCompareF32ForItemSort(ip_a->physics.vel_air.x, ip_b->physics.vel_air.x);
	if (cmp != 0)
	{
		return cmp;
	}
	cmp = syNetRbCompareF32ForItemSort(ip_a->physics.vel_air.y, ip_b->physics.vel_air.y);
	if (cmp != 0)
	{
		return cmp;
	}
	cmp = syNetRbCompareF32ForItemSort(ip_a->physics.vel_air.z, ip_b->physics.vel_air.z);
	if (cmp != 0)
	{
		return cmp;
	}
	if (ip_a->lifetime != ip_b->lifetime)
	{
		return (ip_a->lifetime < ip_b->lifetime) ? -1 : 1;
	}
	if (a->id != b->id)
	{
		return (a->id < b->id) ? -1 : 1;
	}
	return 0;
}

static void syNetRbSnapCanonicalizeLiveItemForNetplay(GObj *gobj)
{
	ITStruct *ip;
	DObj *dobj;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return;
	}
	ip = itGetStruct(gobj);
	dobj = DObjGetStruct(gobj);
	if (dobj != NULL)
	{
		syNetplayQuantizeDObjTranslate(dobj);
	}
	if (ip != NULL)
	{
		syNetplayQuantizeVec3f(&ip->physics.vel_air);
		if (ip->kind == nITKindHitokage)
		{
			syNetplayQuantizeVec3f(&ip->item_vars.hitokage.offset);
		}
	}
}

void syNetRbSnapshotCanonicalizeActiveItemsForNetplay(void)
{
	GObj *gobj;

	for (gobj = gGCCommonLinks[nGCCommonLinkIDItem]; gobj != NULL; gobj = gobj->link_next)
	{
		if (itGetStruct(gobj) != NULL)
		{
			syNetRbSnapCanonicalizeLiveItemForNetplay(gobj);
		}
	}
}
#endif

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

		key_gobj = out[i];
		j = i;
#if defined(SSB64_NETMENU)
		while ((j > 0) && (syNetRbCompareItemGobjsForRollbackHash(out[j - 1], key_gobj) > 0))
#else
		while ((j > 0) && (out[j - 1]->id > key_gobj->id))
#endif
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

static sb32 syNetRbSnapEffectHiddenFromRollback(const GObj *gobj, const EFStruct *ep)
{
#if defined(SSB64_NETMENU)
	if ((gobj == NULL) || (ep == NULL))
	{
		return FALSE;
	}
	return ((ep->bank_id == SYNETRB_YOSHI_EGG_LAY_HATCH_COSMETIC_BANK_ID) &&
	        (ep->proc_update == syNetRbSnapYoshiEggLayHatchCosmeticProcUpdate))
	           ? TRUE
	           : FALSE;
#else
	(void)gobj;
	(void)ep;
	return FALSE;
#endif
}

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
			EFStruct *ep;

			ep = efGetStruct(gobj);
			if (ep != NULL)
			{
				if (syNetRbSnapEffectHiddenFromRollback(gobj, ep) != FALSE)
				{
					continue;
				}
				/*
				 * Dead camera-quake shells (anim exhausted) are presentation-only; they cannot survive
				 * snapshot load and would desync eff hash vs verify when mixed with respawnable effects.
				 */
				if ((syNetRbSnapLiveEffectIsQuake(gobj, ep) != FALSE) && (gobj->anim_frame <= 0.0F))
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
static sb32 syNetRbSnapItemIsGroundMonster(s32 kind);
static void syNetRbSnapCaptureItemPresentation(SYNetRbSnapItemBlob *blob, GObj *gobj, const ITStruct *ip);
static void syNetRbSnapApplyItemPresentation(GObj *gobj, ITStruct *ip, const SYNetRbSnapItemBlob *blob);
static void syNetRbSnapApplyItemBlobToGObj(GObj *gobj, const SYNetRbSnapItemBlob *blob);
#ifdef PORT
static u8 syNetRbSnapLinkBombStatusFromLive(const ITStruct *ip);
static void syNetRbSnapReapplyLinkBombStatusAfterBlob(GObj *item_gobj, const SYNetRbSnapItemBlob *blob);
static void syNetRbSnapReapplyMarumineStatusAfterBlob(GObj *item_gobj, const SYNetRbSnapItemBlob *blob);
static void syNetRbSnapReplayExplodeSparklesFromRing(const SYNetRbSnapshotSlot *load_slot);
static sb32 syNetRbSnapItemBlobWantsExplodeSparkleReplay(const SYNetRbSnapItemBlob *blob, f32 *scale_out);
static sb32 syNetRbSnapWeaponBlobWantsEggExplodeParticleReplay(const SYNetRbSnapWeaponBlob *blob);
static sb32 syNetRbSnapSlotTickHasExplodeSparkleReplay(u32 tick);
static void syNetRbSnapReplayCosmeticYoshiEggExplode(const Vec3f *pos);
static void syNetRbSnapReapplyYoshiEggExplodeAfterBlob(GObj *weapon_gobj, const SYNetRbSnapWeaponBlob *blob);
#if defined(SSB64_NETMENU)
static void syNetRbSnapCanonicalizeLiveItemForNetplay(GObj *gobj);
#endif
#endif

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
#if defined(SSB64_NETMENU)
		syNetRbSnapCanonicalizeLiveItemForNetplay(gobj);
#endif
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
#ifdef PORT
		if (ip->kind == nITKindLinkBomb)
		{
			u8 link_status;

			link_status = syNetRbSnapLinkBombStatusFromLive(ip);
			if (link_status <= (u8)nITLinkBombStatusExplode)
			{
				blob->item_flags |= SYNETRB_ITEM_FLAG_LINK_BOMB_STATUS_VALID;
				blob->item_flags |= (u8)(link_status << SYNETRB_ITEM_FLAG_LINK_BOMB_STATUS_SHIFT);
			}
		}
		if ((ip->kind == nITKindMarumine) && (ip->proc_update == itMarumineExplodeProcUpdate))
		{
			blob->item_flags |= SYNETRB_ITEM_FLAG_MARUMINE_EXPLODE;
		}
#endif
		memcpy(blob->item_vars, &ip->item_vars, sizeof(blob->item_vars));
#ifdef PORT
		syNetRbSnapCaptureItemBlobMeta(blob, ip);
		syNetRbSnapCaptureItemPresentation(blob, gobj, ip);
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

static sb32 syNetRbSnapItemIsGroundMonster(s32 kind)
{
	return ((kind >= nITKindGroundMonsterStart) && (kind <= nITKindGroundMonsterEnd)) ? TRUE : FALSE;
}

static void syNetRbSnapCaptureItemPresentation(SYNetRbSnapItemBlob *blob, GObj *gobj, const ITStruct *ip)
{
	DObj *dobj;

	if ((blob == NULL) || (ip == NULL))
	{
		return;
	}
	blob->present_anim_frame = 0.0F;
	blob->present_anim_wait = 0.0F;
	blob->present_texture_id_curr = 0U;
	if (ip->kind == nITKindGBumper)
	{
		dobj = DObjGetStruct(gobj);
		if (dobj != NULL)
		{
			blob->item_flags |= SYNETRB_ITEM_FLAG_GBUMPER_PRESENTATION_VALID;
			blob->present_anim_frame = dobj->scale.vec.f.x;
			if (dobj->mobj != NULL)
			{
				blob->present_anim_wait = dobj->mobj->palette_id;
			}
#if defined(SSB64_NETMENU)
			blob->present_anim_frame = syNetplayQuantizeAnimScalar(blob->present_anim_frame);
			blob->present_anim_wait = syNetplayQuantizeAnimScalar(blob->present_anim_wait);
#endif
		}
		return;
	}
	if (syNetRbSnapItemIsGroundMonster(ip->kind) == FALSE)
	{
		return;
	}
	dobj = DObjGetStruct(gobj);
	if (dobj == NULL)
	{
		return;
	}
	blob->item_flags |= SYNETRB_ITEM_FLAG_GROUND_MONSTER_ANIM_VALID;
	blob->present_anim_frame = dobj->anim_frame;
	blob->present_anim_wait = dobj->anim_wait;
	if (dobj->mobj != NULL)
	{
		blob->present_texture_id_curr = dobj->mobj->texture_id_curr;
	}
#if defined(SSB64_NETMENU)
	blob->present_anim_frame = syNetplayQuantizeAnimScalar(blob->present_anim_frame);
	blob->present_anim_wait = syNetplayQuantizeAnimScalar(blob->present_anim_wait);
#endif
}

static void syNetRbSnapApplyItemPresentation(GObj *gobj, ITStruct *ip, const SYNetRbSnapItemBlob *blob)
{
	DObj *dobj;

	if ((gobj == NULL) || (ip == NULL) || (blob == NULL))
	{
		return;
	}
	if ((blob->item_flags & SYNETRB_ITEM_FLAG_GBUMPER_PRESENTATION_VALID) != 0U)
	{
		if (ip->kind == nITKindGBumper)
		{
			dobj = DObjGetStruct(gobj);
			if (dobj != NULL)
			{
				dobj->scale.vec.f.x = blob->present_anim_frame;
				dobj->scale.vec.f.y = blob->present_anim_frame;
#if defined(SSB64_NETMENU)
				dobj->scale.vec.f.x = syNetplayQuantizeAnimScalar(dobj->scale.vec.f.x);
				dobj->scale.vec.f.y = dobj->scale.vec.f.x;
#endif
				if (dobj->mobj != NULL)
				{
					dobj->mobj->palette_id = blob->present_anim_wait;
#if defined(SSB64_NETMENU)
					dobj->mobj->palette_id = syNetplayQuantizeAnimScalar(dobj->mobj->palette_id);
#endif
				}
			}
		}
		return;
	}
	if ((blob->item_flags & SYNETRB_ITEM_FLAG_GROUND_MONSTER_ANIM_VALID) == 0U)
	{
		return;
	}
	if (syNetRbSnapItemIsGroundMonster(ip->kind) == FALSE)
	{
		return;
	}
	dobj = DObjGetStruct(gobj);
	if (dobj == NULL)
	{
		return;
	}
	dobj->anim_frame = blob->present_anim_frame;
	dobj->anim_wait = blob->present_anim_wait;
#if defined(SSB64_NETMENU)
	dobj->anim_frame = syNetplayQuantizeAnimScalar(dobj->anim_frame);
	dobj->anim_wait = syNetplayQuantizeAnimScalar(dobj->anim_wait);
#endif
	if (gobj->anim_frame != dobj->anim_frame)
	{
		gobj->anim_frame = dobj->anim_frame;
	}
	if (dobj->mobj != NULL)
	{
		dobj->mobj->texture_id_curr = blob->present_texture_id_curr;
	}
	gcApplyDObjAnimJointPoseAtFrame(
	    dobj, dobj->anim_frame, (blob->present_anim_wait == AOBJ_ANIM_NULL) ? TRUE : FALSE);
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

static sb32 syNetRbSnapItemKindUsesFighterOwner(s32 kind)
{
	return ((kind >= nITKindFighterStart) && (kind <= nITKindFighterEnd)) ? TRUE : FALSE;
}

/* Fighter GObjs share kind id nGCCommonKindFighter (1000); owner must restore by sim slot. */
static GObj *syNetRbSnapResolveItemOwnerFromBlob(const SYNetRbSnapItemBlob *blob)
{
	if (blob == NULL)
	{
		return NULL;
	}
	if (syNetRbSnapItemKindUsesFighterOwner(blob->kind) != FALSE)
	{
		if ((blob->player >= 0) && (blob->player < GMCOMMON_PLAYERS_MAX))
		{
			return syNetRbSnapResolveFighterGobjByPlayer((s8)blob->player);
		}
	}
	return syNetRbSnapResolveLiveGobj(blob->owner_gobj_id);
}

static void syNetRbSnapReconcileFighterOwnedItemOwners(void)
{
	GObj *item_gobj;

	for (item_gobj = gGCCommonLinks[nGCCommonLinkIDItem]; item_gobj != NULL; item_gobj = item_gobj->link_next)
	{
		ITStruct *ip;
		GObj *owner_gobj;

		ip = itGetStruct(item_gobj);
		if ((ip == NULL) || (syNetRbSnapItemKindUsesFighterOwner(ip->kind) == FALSE))
		{
			continue;
		}
		if ((ip->player < 0) || (ip->player >= GMCOMMON_PLAYERS_MAX))
		{
			continue;
		}
		owner_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)ip->player);
		if (owner_gobj == NULL)
		{
			continue;
		}
		if (ip->owner_gobj != owner_gobj)
		{
			ip->owner_gobj = owner_gobj;
		}
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
	ip->owner_gobj = syNetRbSnapResolveItemOwnerFromBlob(blob);
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
#if defined(SSB64_NETMENU)
	syNetRbSnapCanonicalizeLiveItemForNetplay(gobj);
#endif
	syNetRbSnapApplyItemPresentation(gobj, ip, blob);
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

static sb32 syNetRbSnapItemBlobIsHold(const SYNetRbSnapItemBlob *blob)
{
	if (blob == NULL)
	{
		return FALSE;
	}
	return ((blob->item_flags & 0x01U) != 0U) ? TRUE : FALSE;
}

static s32 syNetRbSnapFindLinkBombHoldBlob(const SYNetRbSnapshotSlot *slot, sb32 *matched, const ITStruct *ip)
{
	s32 si;
	s32 fallback;
	s32 by_fighter;
	u32 fighter_item_id;
	s32 player_slot;

	if ((slot == NULL) || (matched == NULL) || (ip == NULL))
	{
		return -1;
	}
	fallback = -1;
	by_fighter = -1;
	fighter_item_id = 0U;
	player_slot = ip->player;
	if ((player_slot >= 0) && (player_slot < GMCOMMON_PLAYERS_MAX) && (slot->fighters[player_slot].is_valid != FALSE))
	{
		fighter_item_id = slot->fighters[player_slot].item_gobj_id;
	}
	for (si = 0; si < slot->item_count; si++)
	{
		const SYNetRbSnapItemBlob *blob;
		u32 live_owner_id;

		if ((matched[si] != FALSE) || (slot->items[si].is_valid == FALSE) || (slot->items[si].kind != nITKindLinkBomb))
		{
			continue;
		}
		blob = &slot->items[si];
		if (syNetRbSnapItemBlobIsHold(blob) == FALSE)
		{
			continue;
		}
		if (blob->player != ip->player)
		{
			continue;
		}
		live_owner_id = syNetRbSnapGobjId(ip->owner_gobj);
		if ((blob->owner_gobj_id != 0U) || (live_owner_id != 0U))
		{
			if (blob->owner_gobj_id != live_owner_id)
			{
				continue;
			}
		}
		if ((fighter_item_id != 0U) && (blob->gobj_id == fighter_item_id))
		{
			by_fighter = si;
		}
		if ((blob->multi == ip->multi) && (blob->event_id == ip->event_id))
		{
			return si;
		}
		if (fallback < 0)
		{
			fallback = si;
		}
	}
	if (by_fighter >= 0)
	{
		return by_fighter;
	}
	return fallback;
}

static s32 syNetRbSnapFindLinkBombBlob(const SYNetRbSnapshotSlot *slot, sb32 *matched, const ITStruct *ip, const Vec3f *pos)
{
	s32 si;
	s32 best = -1;
	f32 best_score = F32_MAX;
	u32 live_owner_id;

	if ((slot == NULL) || (matched == NULL) || (ip == NULL) || (pos == NULL))
	{
		return -1;
	}
	if (ip->is_hold != FALSE)
	{
		return syNetRbSnapFindLinkBombHoldBlob(slot, matched, ip);
	}
	live_owner_id = syNetRbSnapGobjId(ip->owner_gobj);
	for (si = 0; si < slot->item_count; si++)
	{
		const SYNetRbSnapItemBlob *blob;
		f32 dx;
		f32 dy;
		f32 dz;
		f32 dist_sq;
		f32 vel_dx;
		f32 vel_dy;
		f32 vel_dz;
		f32 vel_sq;
		f32 score;

		if ((matched[si] != FALSE) || (slot->items[si].is_valid == FALSE) || (slot->items[si].kind != nITKindLinkBomb))
		{
			continue;
		}
		blob = &slot->items[si];
		if (syNetRbSnapItemBlobIsHold(blob) != FALSE)
		{
			continue;
		}
		if (blob->player != ip->player)
		{
			continue;
		}
		if ((blob->owner_gobj_id != 0U) || (live_owner_id != 0U))
		{
			if (blob->owner_gobj_id != live_owner_id)
			{
				continue;
			}
		}
		dx = blob->translate.x - pos->x;
		dy = blob->translate.y - pos->y;
		dz = blob->translate.z - pos->z;
		dist_sq = (dx * dx) + (dy * dy) + (dz * dz);
		vel_dx = blob->physics.vel_air.x - ip->physics.vel_air.x;
		vel_dy = blob->physics.vel_air.y - ip->physics.vel_air.y;
		vel_dz = blob->physics.vel_air.z - ip->physics.vel_air.z;
		vel_sq = (vel_dx * vel_dx) + (vel_dy * vel_dy) + (vel_dz * vel_dz);
		score = dist_sq + vel_sq;
		if (blob->multi == ip->multi)
		{
			score -= 1.0F;
		}
		if (blob->event_id == ip->event_id)
		{
			score -= 1.0F;
		}
		if ((score < best_score) || ((score == best_score) && (best >= 0) && (blob->gobj_id < slot->items[best].gobj_id)))
		{
			best_score = score;
			best = si;
		}
	}
	return best;
}

static s32 syNetRbSnapFindItemBlobForLiveGobj(const SYNetRbSnapshotSlot *slot, sb32 *matched, GObj *gobj, ITStruct *ip)
{
	DObj *dobj;
	s32 found;

	if ((slot == NULL) || (matched == NULL) || (gobj == NULL) || (ip == NULL))
	{
		return -1;
	}
	found = syNetRbSnapFindItemBlobByGobjId(slot, matched, gobj->id);
	if (found >= 0)
	{
		return found;
	}
	dobj = DObjGetStruct(gobj);
	if (dobj == NULL)
	{
		return -1;
	}
	if (ip->kind == nITKindLinkBomb)
	{
		return syNetRbSnapFindLinkBombBlob(slot, matched, ip, &dobj->translate.vec.f);
	}
	return syNetRbSnapFindItemBlobByKindPos(slot, matched, ip->kind, &dobj->translate.vec.f);
}

static void syNetRbSnapApplyItemBlobToGObjPort(GObj *item_gobj, const SYNetRbSnapItemBlob *blob)
{
	ITStruct *ip;

	if ((item_gobj == NULL) || (blob == NULL))
	{
		return;
	}
	syNetRbSnapApplyItemBlobToGObj(item_gobj, blob);
	ip = itGetStruct(item_gobj);
	if (ip == NULL)
	{
		return;
	}
	if (ip->kind == nITKindLinkBomb)
	{
		syNetRbSnapReapplyLinkBombStatusAfterBlob(item_gobj, blob);
	}
	else if (ip->kind == nITKindMarumine)
	{
		syNetRbSnapReapplyMarumineStatusAfterBlob(item_gobj, blob);
	}
#if defined(SSB64_NETMENU)
	syNetRbSnapCanonicalizeLiveItemForNetplay(item_gobj);
#endif
}

static void syNetRbSnapReconcileItemsToSlotBlobs(const SYNetRbSnapshotSlot *slot)
{
	GObj *gobj;
	sb32 matched[SYNETRB_SNAPSHOT_MAX_ITEMS];
	s32 found;

	if (slot == NULL)
	{
		return;
	}
	memset(matched, 0, sizeof(matched));
	for (gobj = gGCCommonLinks[nGCCommonLinkIDItem]; gobj != NULL; gobj = gobj->link_next)
	{
		ITStruct *ip;

		ip = itGetStruct(gobj);
		if (ip == NULL)
		{
			continue;
		}
		found = syNetRbSnapFindItemBlobForLiveGobj(slot, matched, gobj, ip);
		if (found < 0)
		{
			continue;
		}
		matched[found] = TRUE;
		syNetRbSnapApplyItemBlobToGObjPort(gobj, &slot->items[found]);
	}
}

void syNetRbSnapshotReconcileLoadedItemsForVerify(u32 tick)
{
	SYNetRbSnapshotSlot *slot;

	slot = syNetRbSnapshotSlotForTick(tick);
	if ((slot == NULL) || (slot->is_valid == FALSE) || (slot->tick != tick))
	{
		return;
	}
	syNetRbSnapReconcileItemsToSlotBlobs(slot);
#if defined(SSB64_NETMENU)
	syNetRbSnapshotCanonicalizeActiveItemsForNetplay();
#endif
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
static u8 syNetRbSnapLinkBombStatusFromLive(const ITStruct *ip)
{
	if (ip == NULL)
	{
		return 0xFFU;
	}
	if (ip->proc_update == itLinkBombExplodeProcUpdate)
	{
		return (u8)nITLinkBombStatusExplode;
	}
	if (ip->proc_update == itLinkBombHoldProcUpdate)
	{
		return (u8)nITLinkBombStatusHold;
	}
	if (ip->proc_update == itLinkBombDroppedProcUpdate)
	{
		return (u8)nITLinkBombStatusDropped;
	}
	if ((ip->proc_update == itLinkBombFallProcUpdate) && (ip->proc_map == itLinkBombThrownProcMap) &&
	    (ip->proc_hit == itLinkBombThrownProcHit))
	{
		return (u8)nITLinkBombStatusThrown;
	}
	if (ip->proc_update == itLinkBombWaitProcUpdate)
	{
		return (u8)nITLinkBombStatusWait;
	}
	if (ip->proc_update == itLinkBombFallProcUpdate)
	{
		return (u8)nITLinkBombStatusFall;
	}
	return 0xFFU;
}

static void syNetRbSnapRebindLinkBombProcsFromStatus(ITStruct *ip, u8 link_status)
{
	const ITStatusDesc *desc;

	if (ip == NULL)
	{
		return;
	}
	if (link_status > (u8)nITLinkBombStatusExplode)
	{
		return;
	}
	desc = &dItLinkBombStatusDescs[link_status];
	ip->proc_update = desc->proc_update;
	ip->proc_map = desc->proc_map;
	ip->proc_hit = desc->proc_hit;
	ip->proc_shield = desc->proc_shield;
	ip->proc_hop = desc->proc_hop;
	ip->proc_setoff = desc->proc_setoff;
	ip->proc_reflector = desc->proc_reflector;
	ip->proc_damage = desc->proc_damage;
}

static void syNetRbSnapReapplyLinkBombHoldCoupling(GObj *item_gobj, ITStruct *ip)
{
	FTStruct *fp;

	if ((ip == NULL) || (ip->is_hold == FALSE) || (ip->owner_gobj == NULL))
	{
		return;
	}
	fp = ftGetStruct(ip->owner_gobj);
	if (fp != NULL)
	{
		fp->item_gobj = item_gobj;
		ip->owner_gobj = fp->fighter_gobj;
	}
}

static u8 syNetRbSnapInferLinkBombStatusFromBlob(const ITStruct *ip, const SYNetRbSnapItemBlob *blob)
{
	u8 link_status;
	sb32 was_thrown;

	if ((blob->item_flags & SYNETRB_ITEM_FLAG_LINK_BOMB_STATUS_VALID) != 0U)
	{
		link_status = (blob->item_flags >> SYNETRB_ITEM_FLAG_LINK_BOMB_STATUS_SHIFT) & 0x0FU;
		if (link_status <= (u8)nITLinkBombStatusExplode)
		{
			return link_status;
		}
	}
	if (ip->is_hold != FALSE)
	{
		return (u8)nITLinkBombStatusHold;
	}
	was_thrown = ((blob->item_flags & 0x04U) != 0U) ? TRUE : FALSE;
	if (was_thrown != FALSE)
	{
		if (ip->item_vars.linkbomb.drop_update_wait > 0)
		{
			return (u8)nITLinkBombStatusDropped;
		}
		return (u8)nITLinkBombStatusThrown;
	}
	if (ip->ga == nMPKineticsGround)
	{
		return (u8)nITLinkBombStatusWait;
	}
	return (u8)nITLinkBombStatusFall;
}

/* Presentational only — LBParticles are not snapshotted; replay after rollback load. */
static void syNetRbSnapReplayCosmeticExplodeSparkle(const Vec3f *pos, f32 scale)
{
	LBParticle *pc;
	Vec3f pos_copy;

	if (pos == NULL)
	{
		return;
	}
	pos_copy = *pos;
	pc = efManagerSparkleWhiteMultiExplodeMakeEffect(&pos_copy);
	if ((pc != NULL) && (pc->xf != NULL))
	{
		pc->xf->scale.x = scale;
		pc->xf->scale.y = scale;
		pc->xf->scale.z = scale;
	}
}

static void syNetRbSnapReapplyLinkBombStatusAfterBlob(GObj *item_gobj, const SYNetRbSnapItemBlob *blob)
{
	ITStruct *ip;
	u8 link_status;
	DObj *dobj;

	ip = itGetStruct(item_gobj);
	if ((ip == NULL) || (blob == NULL))
	{
		return;
	}
	link_status = syNetRbSnapInferLinkBombStatusFromBlob(ip, blob);
	/*
	 * Blob apply already restored attack_coll, flags, physics, and item_vars.
	 * itLinkBomb*SetStatus / itMainSetStatus would clobber hashed fields (attack_state,
	 * is_allow_pickup, vel, is_thrown, is_damage_all, drop_update_wait).
	 */
	syNetRbSnapRebindLinkBombProcsFromStatus(ip, link_status);
	if (link_status == (u8)nITLinkBombStatusHold)
	{
		syNetRbSnapReapplyLinkBombHoldCoupling(item_gobj, ip);
		dobj = DObjGetStruct(item_gobj);
		if (dobj != NULL)
		{
			dobj->translate.vec.f = blob->translate;
		}
		ip->physics.vel_air = blob->physics.vel_air;
#if defined(SSB64_NETMENU)
		if (dobj != NULL)
		{
			syNetplayQuantizeDObjTranslate(dobj);
		}
		syNetplayQuantizeVec3f(&ip->physics.vel_air);
#endif
	}
	else if ((link_status == (u8)nITLinkBombStatusThrown) || (link_status == (u8)nITLinkBombStatusDropped) ||
	         (link_status == (u8)nITLinkBombStatusFall) || (link_status == (u8)nITLinkBombStatusWait))
	{
		dobj = DObjGetStruct(item_gobj);
		if (dobj != NULL)
		{
			dobj->translate.vec.f = blob->translate;
		}
		ip->physics.vel_air = blob->physics.vel_air;
#if defined(SSB64_NETMENU)
		if (dobj != NULL)
		{
			syNetplayQuantizeDObjTranslate(dobj);
		}
		syNetplayQuantizeVec3f(&ip->physics.vel_air);
#endif
	}
	else if (link_status == (u8)nITLinkBombStatusExplode)
	{
		dobj = DObjGetStruct(item_gobj);
		if (dobj != NULL)
		{
			dobj->flags |= DOBJ_FLAG_HIDDEN;
		}
		/*
		 * Blob already restored multi/event_id/attack_coll; avoid ExplodeSetStatus /
		 * ExplodeInitAttackColl. Refresh collider binding so ExplodeProcUpdate can
		 * emit hitboxes after rollback (sparkle particles are LBParticle and are not snapshotted).
		 */
		itLinkBombCommonSetHitStatusNone(item_gobj);
		itMainRefreshAttackColl(item_gobj);
		if (dobj != NULL)
		{
			syNetRbSnapReplayCosmeticExplodeSparkle(&dobj->translate.vec.f, ITLINKBOMB_EXPLODE_EFFECT_SCALE);
		}
	}
}

static void syNetRbSnapRebindMarumineExplodeProcs(ITStruct *ip)
{
	const ITStatusDesc *desc;

	if (ip == NULL)
	{
		return;
	}
	desc = &dITMarumineStatusDescs[0];
	ip->proc_update = desc->proc_update;
	ip->proc_map = desc->proc_map;
	ip->proc_hit = desc->proc_hit;
	ip->proc_shield = desc->proc_shield;
	ip->proc_hop = desc->proc_hop;
	ip->proc_setoff = desc->proc_setoff;
	ip->proc_reflector = desc->proc_reflector;
	ip->proc_damage = desc->proc_damage;
}

static sb32 syNetRbSnapMarumineBlobIsExplode(const SYNetRbSnapItemBlob *blob)
{
	if ((blob == NULL) || (blob->is_valid == FALSE) || (blob->kind != nITKindMarumine))
	{
		return FALSE;
	}
	if ((blob->item_flags & SYNETRB_ITEM_FLAG_MARUMINE_EXPLODE) != 0U)
	{
		return TRUE;
	}
	/*
	 * Blobs captured before SYNETRB_ITEM_FLAG_MARUMINE_EXPLODE existed: multi counts up during the
	 * 6-tick explode window while the item GObj still exists in the snapshot.
	 */
	if ((blob->multi > 0) && (blob->multi < ITMARUMINE_EXPLODE_LIFETIME))
	{
		return TRUE;
	}
	return FALSE;
}

static void syNetRbSnapReapplyMarumineStatusAfterBlob(GObj *item_gobj, const SYNetRbSnapItemBlob *blob)
{
	ITStruct *ip;
	DObj *dobj;

	ip = itGetStruct(item_gobj);
	if ((ip == NULL) || (blob == NULL))
	{
		return;
	}
	if ((syNetRbSnapMarumineBlobIsExplode(blob) == FALSE) && (ip->proc_update != itMarumineExplodeProcUpdate))
	{
		return;
	}
	/*
	 * Blob apply already restored multi/event_id/attack_coll. Avoid itMarumineExplodeSetStatus /
	 * itMarumineExplodeMakeEffectGotoSetStatus — they reset hashed fields and re-spawn particles
	 * through the full init path. Proc-only rebind + collider refresh matches Link bomb explode repair.
	 */
	syNetRbSnapRebindMarumineExplodeProcs(ip);
	dobj = DObjGetStruct(item_gobj);
	if (dobj != NULL)
	{
		dobj->flags |= DOBJ_FLAG_HIDDEN;
	}
	ip->damage_coll.hitstatus = nGMHitStatusNone;
	itMainRefreshAttackColl(item_gobj);
	if (dobj != NULL)
	{
		syNetRbSnapReplayCosmeticExplodeSparkle(&dobj->translate.vec.f, ITMARUMINE_EXPLODE_EFFECT_SCALE);
	}
}

static GObj *syNetRbSnapResolveLinkBombOwnerGobj(const SYNetRbSnapshotSlot *slot, const SYNetRbSnapItemBlob *blob)
{
	GObj *owner_gobj;
	s32 player_slot;

	if (blob == NULL)
	{
		return NULL;
	}
	owner_gobj = syNetRbSnapResolveLiveGobj(blob->owner_gobj_id);
	if (owner_gobj != NULL)
	{
		return owner_gobj;
	}
	if (slot == NULL)
	{
		return NULL;
	}
	player_slot = blob->player;
	if ((player_slot >= 0) && (player_slot < GMCOMMON_PLAYERS_MAX) &&
	    (slot->fighters[player_slot].is_valid != FALSE))
	{
		owner_gobj = syNetRbSnapResolveLiveGobj(slot->fighters[player_slot].gobj_id);
		if (owner_gobj != NULL)
		{
			return owner_gobj;
		}
	}
	return NULL;
}

static GObj *syNetRbSnapRespawnLinkBombFromBlob(const SYNetRbSnapshotSlot *slot, const SYNetRbSnapItemBlob *blob,
                                                u32 tick, Vec3f *pos, Vec3f *vel)
{
	GObj *item_gobj;
	ITStruct *ip;
	DObj *dobj;

	if (syNetRbSnapItemBlobIsHold(blob) != FALSE)
	{
		GObj *fighter_gobj;

		fighter_gobj = syNetRbSnapResolveLinkBombOwnerGobj(slot, blob);
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
	}
	else
	{
		item_gobj = itManagerMakeItemSetupCommon(NULL, nITKindLinkBomb, pos, vel, ITEM_FLAG_PARENT_DEFAULT);
	}
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
		ip->attack_coll.can_rehit_shield = TRUE;
	}
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
		if (syNetRbSnapItemBlobIsHold(blob) == FALSE)
		{
			vel = blob->physics.vel_air;
		}
		spawned = syNetRbSnapRespawnLinkBombFromBlob(slot, blob, slot->tick, &pos, &vel);
		if (spawned == NULL)
		{
			port_log("SSB64 NetRbSnapshot: item respawn failed kind=%d tick=%u gobj_id=%u\n",
			         (int)blob->kind,
			         (unsigned int)slot->tick,
			         (unsigned int)blob->gobj_id);
			return NULL;
		}
		syNetRbSnapApplyItemBlobToGObjPort(spawned, blob);
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
	syNetRbSnapApplyItemBlobToGObjPort(spawned, blob);
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
		found = syNetRbSnapFindItemBlobForLiveGobj(slot, matched, gobj, ip);
		if (found < 0)
		{
#ifdef PORT
			if (syNetRbSnapShouldPreserveYamabukiGroundMonsterOnApply(slot, gobj, ip) != FALSE)
			{
				gobj = next_gobj;
				continue;
			}
#endif
			syNetRbSnapEjectGObj(gobj);
#ifdef PORT
			ejected_count++;
#endif
			gobj = next_gobj;
			continue;
		}
		matched[found] = TRUE;
#ifdef PORT
		syNetRbSnapApplyItemBlobToGObjPort(gobj, &slot->items[found]);
		matched_count++;
#else
		syNetRbSnapApplyItemBlobToGObj(gobj, &slot->items[found]);
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
#if defined(SSB64_NETMENU)
			/*
			 * Cross-ISA: snap the weapon DObj transform to the shared grid (mirrors the fighter
			 * gobj-pose capture). sinf/cosf/sqrtf in projectile sim are not bit-identical across
			 * aarch64/x86_64 even with FMA off, so the rollback weapon hash (translate/rotate/
			 * scale) drifts unless re-snapped here and on apply.
			 */
			syNetplayQuantizeVec3f(&blob->translate);
			syNetplayQuantizeVec3f(&blob->rotate);
			syNetplayQuantizeVec3f(&blob->scale);
#endif
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
static sb32 syNetRbSnapReconciledEffectGobjIdListed(const u32 *reconciled_ids, s32 reconciled_count, u32 gobj_id);
static sb32 syNetRbSnapLiveEffectKeptAfterReconcile(const SYNetRbSnapshotSlot *slot, u32 gobj_id,
                                                    const u32 *reconciled_ids, s32 reconciled_count);
static GObj *syNetRbSnapFindUnreconciledLiveEffectForBlob(const SYNetRbSnapshotSlot *slot,
                                                            const SYNetRbSnapEffectBlob *blob,
                                                            const u32 *reconciled_ids, s32 reconciled_count);

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

/*
 * Rebirth halo lifecycle: only when rebirth union is authoritative (is_rebirth or RebirthDown..Wait).
 * Do not read status_vars.common.rebirth timers on unrelated statuses — status_vars is a per-fkind union.
 */
static sb32 syNetRbSnapFighterRebirthHaloLifecycleActive(const FTStruct *fp)
{
	return syNetRbSnapFighterInRebirthScope(fp);
}

#if defined(SSB64_NETMENU)
/*
 * Quantize rebirth union fields on the blob copy only (capture) or after memcpy on restore.
 * Gated on InRebirthScope so dead.wait / other integer union members are never float-quantized.
 */
static void syNetRbSnapQuantizeFighterRebirthStatusVars(const FTStruct *fp, union FTStatusVars *status_vars)
{
	if ((fp == NULL) || (status_vars == NULL) || (syNetRbSnapFighterInRebirthScope(fp) == FALSE))
	{
		return;
	}
	syNetplayQuantizeVec3f(&status_vars->common.rebirth.pos);
	syNetplayQuantizeVec3f(&status_vars->common.rebirth.halo_offset);
}
#endif

static sb32 syNetRbSnapFighterBlobRebirthHaloPending(const SYNetRbSnapFighterBlob *blob)
{
	if ((blob == NULL) || (blob->is_valid == FALSE) || (blob->is_effect_attach == 0U))
	{
		return FALSE;
	}
	if ((syNetRbSnapGuardEffectIdFromBlob(blob) != 0U) || (blob->captureyoshi_effect_gobj_id != 0U) ||
	    (syNetRbSnapFoxSpecialLwEffectIdFromBlob(blob) != 0U))
	{
		return FALSE;
	}
	return ((blob->status_id >= nFTCommonStatusRebirthDown) && (blob->status_id <= nFTCommonStatusRebirthWait))
	           ? TRUE
	           : FALSE;
}

static sb32 syNetRbSnapSlotListsRebirthHaloForFighter(const SYNetRbSnapshotSlot *slot, u32 fighter_gobj_id,
                                                      u32 effect_gobj_id)
{
	s32 ei;

	if (slot == NULL)
	{
		return FALSE;
	}
	for (ei = 0; ei < slot->effect_count; ei++)
	{
		const SYNetRbSnapEffectBlob *eb = &slot->effects[ei];

		if (eb->is_valid == FALSE)
		{
			continue;
		}
		if (eb->respawn_kind != SYNETRB_EFFECT_RESPAWN_REBIRTH_HALO)
		{
			continue;
		}
		if (eb->fighter_gobj_id != fighter_gobj_id)
		{
			continue;
		}
		if ((effect_gobj_id != 0U) && (eb->gobj_id != effect_gobj_id))
		{
			continue;
		}
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapFighterInNessPKWaveScope(const FTStruct *fp)
{
	if ((fp == NULL) || (fp->fkind != nFTKindNess))
	{
		return FALSE;
	}
	if ((fp->status_id == nFTNessStatusSpecialHiStart) || (fp->status_id == nFTNessStatusSpecialHiHold) ||
	    (fp->status_id == nFTNessStatusSpecialAirHiStart) || (fp->status_id == nFTNessStatusSpecialAirHiHold))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapFighterInNessPKThunderScope(const FTStruct *fp)
{
	if ((fp == NULL) || ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess)))
	{
		return FALSE;
	}
	if ((fp->status_id == nFTNessStatusSpecialHiStart) || (fp->status_id == nFTNessStatusSpecialHiHold) ||
	    (fp->status_id == nFTNessStatusSpecialHiEnd) || (fp->status_id == nFTNessStatusSpecialHiJibaku) ||
	    (fp->status_id == nFTNessStatusSpecialAirHiStart) || (fp->status_id == nFTNessStatusSpecialAirHiHold) ||
	    (fp->status_id == nFTNessStatusSpecialAirHiEnd) || (fp->status_id == nFTNessStatusSpecialAirHiBound) ||
	    (fp->status_id == nFTNessStatusSpecialAirHiJibaku))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapFighterInNessSpecialLwScope(const FTStruct *fp)
{
	if ((fp == NULL) || ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess)))
	{
		return FALSE;
	}
	if ((fp->status_id >= nFTNessStatusSpecialLwScopeStart) && (fp->status_id <= nFTNessStatusSpecialLwScopeEnd))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapFighterInTrailShockFxScope(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	if (syNetRbSnapFighterInNessPKThunderScope(fp) != FALSE)
	{
		return TRUE;
	}
	if (syNetplayPikachuFighterInQuickAttackShockFxScope(fp) != FALSE)
	{
		return TRUE;
	}
	if ((fp->status_id == nFTCommonStatusDamageE1) || (fp->status_id == nFTCommonStatusDamageE2))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapFighterInNessShockFxScope(const FTStruct *fp)
{
	return syNetRbSnapFighterInTrailShockFxScope(fp);
}

static sb32 syNetRbSnapshotAnyFighterNessPKThunderScopeActive(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		if (syNetRbSnapFighterInNessPKThunderScope(ftGetStruct(fighter_gobj)) != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapshotAnyFighterNessSpecialLwScopeActive(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		if (syNetRbSnapFighterInNessSpecialLwScope(ftGetStruct(fighter_gobj)) != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapLiveHasNessPKThunderScope(void)
{
	return syNetRbSnapshotAnyFighterNessPKThunderScopeActive();
}

static sb32 syNetRbSnapLiveEffectIsShockSmall(const GObj *gobj, const EFStruct *ep)
{
	return ((gobj != NULL) && (ep != NULL) && (ep->proc_update == efManagerVelAddDestroyAnimEnd)) ? TRUE : FALSE;
}

static sb32 syNetRbSnapLiveEffectIsQuake(const GObj *gobj, const EFStruct *ep)
{
	if (gobj == NULL)
	{
		return FALSE;
	}
	if (syNetRbSnapEffectGObjHasUpdateProc(gobj, efManagerQuakeProcUpdate) != FALSE)
	{
		return TRUE;
	}
	return ((ep != NULL) && (ep->proc_update == efManagerQuakeProcUpdate)) ? TRUE : FALSE;
}

#if defined(SSB64_NETMENU)
static void syNetRbSnapRescheduleQuakeProcIfActive(GObj *gobj, EFStruct *ep)
{
	if ((gobj == NULL) || (ep == NULL) || (gobj->anim_frame <= 0.0F))
	{
		return;
	}
	if (s_syNetRbSnapRepairStageVerifyOnly != FALSE)
	{
		return;
	}
	if (syNetRbSnapEffectGObjHasUpdateProc(gobj, efManagerQuakeProcUpdate) == FALSE)
	{
		gcAddGObjProcess(gobj, efManagerQuakeProcUpdate, nGCProcessKindFunc, ep->effect_vars.quake.priority);
	}
}
#endif

static void syNetRbSnapStampQuakeEffectFromBlob(GObj *gobj, EFStruct *ep, const SYNetRbSnapEffectBlob *blob)
{
	if ((gobj == NULL) || (blob == NULL))
	{
		return;
	}
	syNetRbSnapEndQuakeProcUpdate(gobj);
	syNetRbSnapApplyEffectBlobAnimFrame(gobj, blob->anim_frame, ep);
	syNetRbSnapApplyEffectBlobTranslate(gobj, blob, FALSE);
#if defined(SSB64_NETMENU)
	syNetRbSnapRescheduleQuakeProcIfActive(gobj, ep);
#endif
}

static void syNetRbSnapFreezeSlotQuakeEffectsFromSlot(const SYNetRbSnapshotSlot *slot)
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
			s32 ei;
			const SYNetRbSnapEffectBlob *blob_match;

			next = gobj->link_next;
			ep = efGetStruct(gobj);
			if (syNetRbSnapLiveEffectIsQuake(gobj, ep) == FALSE)
			{
				continue;
			}
			blob_match = NULL;
			for (ei = 0; ei < slot->effect_count; ei++)
			{
				const SYNetRbSnapEffectBlob *blob;

				blob = &slot->effects[ei];
				if ((blob->is_valid == FALSE) || (blob->respawn_kind != SYNETRB_EFFECT_RESPAWN_QUAKE))
				{
					continue;
				}
				if (syNetRbSnapLiveEffectMatchesBlob(slot, blob, gobj, ep) != FALSE)
				{
					blob_match = blob;
					break;
				}
			}
			if (blob_match == NULL)
			{
				continue;
			}
			syNetRbSnapStampQuakeEffectFromBlob(gobj, ep, blob_match);
		}
	}
}

static sb32 syNetRbSnapLiveHasNessShockFxScope(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		if (syNetRbSnapFighterInNessShockFxScope(ftGetStruct(fighter_gobj)) != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static void syNetRbSnapPruneStaleShockSmallEffects(const SYNetRbSnapshotSlot *slot)
{
	s32 pass;
	GObj *gobj;
	GObj *next;
	sb32 shock_scope;

	shock_scope = syNetRbSnapLiveHasNessShockFxScope();
	for (pass = 0; pass < 2; pass++)
	{
		GObj *link_head;

		link_head = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		for (gobj = link_head; gobj != NULL; gobj = next)
		{
			EFStruct *ep;

			next = gobj->link_next;
			ep = efGetStruct(gobj);
			if (syNetRbSnapLiveEffectIsShockSmall(gobj, ep) == FALSE)
			{
				continue;
			}
			if ((shock_scope != FALSE) && (slot != NULL) &&
			    (syNetRbSnapLiveEffectListedInSnapshot(slot, gobj->id) != FALSE))
			{
				continue;
			}
			syNetRbSnapEjectGObj(gobj);
		}
	}
}

static void syNetRbSnapPruneOrphanQuakeAndDeadEffects(const SYNetRbSnapshotSlot *slot,
                                                      const u32 *reconciled_ids, s32 reconciled_count)
{
	s32 pass;
	GObj *gobj;
	GObj *next;

	for (pass = 0; pass < 2; pass++)
	{
		GObj *link_head;

		link_head = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		for (gobj = link_head; gobj != NULL; gobj = next)
		{
			EFStruct *ep;
			sb32 eject;

			next = gobj->link_next;
			ep = efGetStruct(gobj);
			if (syNetRbSnapEffectHiddenFromRollback(gobj, ep) != FALSE)
			{
				continue;
			}
			if (syNetRbSnapLiveEffectKeptAfterReconcile(slot, gobj->id, reconciled_ids, reconciled_count) != FALSE)
			{
				continue;
			}
			if ((slot != NULL) && (syNetRbSnapLiveEffectMatchesAnyBlobInSlot(slot, gobj, ep) != FALSE))
			{
				continue;
			}
			eject = FALSE;
			if ((ep != NULL) && (ep->fighter_gobj == NULL))
			{
				if (syNetRbSnapLiveEffectIsQuake(gobj, ep) != FALSE)
				{
					eject = TRUE;
				}
				else if (gobj->anim_frame <= 0.0F)
				{
					eject = TRUE;
				}
			}
			else if ((ep == NULL) && (gobj->user_data.p == NULL) && (gobj->obj_kind == nGCCommonKindEffect) &&
			         (gobj->anim_frame <= 0.0F))
			{
				eject = TRUE;
			}
			if (eject != FALSE)
			{
				syNetRbSnapEjectGObj(gobj);
			}
		}
	}
}

#define SYNETRB_NESS_PKWAVE_JOINT 5

static sb32 syNetRbSnapBlobInNessPKWaveScope(const SYNetRbSnapFighterBlob *blob)
{
	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return FALSE;
	}
	if ((blob->fkind != nFTKindNess) && (blob->fkind != nFTKindNNess))
	{
		return FALSE;
	}
	switch (blob->status_id)
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

static sb32 syNetRbSnapBlobInNessPKThunderScope(const SYNetRbSnapFighterBlob *blob)
{
	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return FALSE;
	}
	if ((blob->fkind != nFTKindNess) && (blob->fkind != nFTKindNNess))
	{
		return FALSE;
	}
	switch (blob->status_id)
	{
	case nFTNessStatusSpecialHiStart:
	case nFTNessStatusSpecialHiHold:
	case nFTNessStatusSpecialHiEnd:
	case nFTNessStatusSpecialHiJibaku:
	case nFTNessStatusSpecialAirHiStart:
	case nFTNessStatusSpecialAirHiHold:
	case nFTNessStatusSpecialAirHiEnd:
	case nFTNessStatusSpecialAirHiBound:
	case nFTNessStatusSpecialAirHiJibaku:
		return TRUE;

	default:
		break;
	}
	return FALSE;
}

static sb32 syNetRbSnapFighterBlobNessPKWaveAttachPending(const SYNetRbSnapFighterBlob *blob)
{
	if ((blob == NULL) || (blob->is_valid == FALSE) || (blob->is_effect_attach == 0U))
	{
		return FALSE;
	}
	if ((syNetRbSnapGuardEffectIdFromBlob(blob) != 0U) || (blob->captureyoshi_effect_gobj_id != 0U) ||
	    (syNetRbSnapFoxSpecialLwEffectIdFromBlob(blob) != 0U))
	{
		return FALSE;
	}
	if (syNetRbSnapBlobInNessPKWaveScope(blob) != FALSE)
	{
		return TRUE;
	}
	return (syNetRbSnapBlobInNessPKThunderScope(blob) != FALSE) ? TRUE : FALSE;
}

static sb32 syNetRbSnapLiveEffectIsNessPKWave(const GObj *gobj, const EFStruct *ep)
{
	FTStruct *fp;
	DObj *dobj;

	if ((gobj == NULL) || (ep == NULL) || (ep->fighter_gobj == NULL) || (ep->proc_update != gcPlayAnimAll))
	{
		return FALSE;
	}
	fp = ftGetStruct(ep->fighter_gobj);
	if ((fp == NULL) || ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess)))
	{
		return FALSE;
	}
	dobj = DObjGetStruct(gobj);
	if ((dobj == NULL) || (dobj->user_data.p != fp->joints[SYNETRB_NESS_PKWAVE_JOINT]))
	{
		return FALSE;
	}
	return TRUE;
}

static sb32 syNetRbSnapLiveFighterHasNessPKWave(GObj *fighter_gobj)
{
	s32 pass;

	if (fighter_gobj == NULL)
	{
		return FALSE;
	}
	for (pass = 0; pass < 2; pass++)
	{
		GObj *gobj;

		for (gobj = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		     gobj != NULL; gobj = gobj->link_next)
		{
			EFStruct *ep = efGetStruct(gobj);

			if ((syNetRbSnapLiveEffectIsNessPKWave(gobj, ep) != FALSE) && (ep->fighter_gobj == fighter_gobj))
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

static void syNetRbSnapEnsureNessPKWaveEffectsFromSlot(const SYNetRbSnapshotSlot *slot)
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
		const SYNetRbSnapFighterBlob *fb;
		s32 ei;
		s32 pi;

		fp = ftGetStruct(fighter_gobj);
		if ((fp == NULL) || (syNetRbSnapFighterInNessPKWaveScope(fp) == FALSE))
		{
			continue;
		}
		pi = fp->player;
		if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX))
		{
			continue;
		}
		fb = &slot->fighters[pi];
		if ((fb->is_valid == FALSE) || (fb->is_effect_attach == 0U))
		{
			continue;
		}
		if (syNetRbSnapLiveFighterHasNessPKWave(fighter_gobj) != FALSE)
		{
			continue;
		}
		for (ei = 0; ei < slot->effect_count; ei++)
		{
			const SYNetRbSnapEffectBlob *eb = &slot->effects[ei];
			GObj *eg;

			if ((eb->is_valid == FALSE) || (eb->fighter_gobj_id != (u32)fighter_gobj->id))
			{
				continue;
			}
			if (eb->respawn_kind != SYNETRB_EFFECT_RESPAWN_NESS_PK_WAVE)
			{
				continue;
			}
			eg = gcFindGObjByID(eb->gobj_id);
			if ((eg != NULL) && (efGetStruct(eg) != NULL))
			{
				continue;
			}
			(void)syNetRbSnapTryRespawnEffectFromBlob(slot, eb);
			break;
		}
		if (syNetRbSnapLiveFighterHasNessPKWave(fighter_gobj) == FALSE)
		{
			SYNetRbSnapEffectBlob synth;

			memset(&synth, 0, sizeof(synth));
			synth.is_valid = TRUE;
			synth.fighter_gobj_id = (u32)fighter_gobj->id;
			synth.respawn_kind = SYNETRB_EFFECT_RESPAWN_NESS_PK_WAVE;
			(void)syNetRbSnapTryRespawnEffectFromBlob(slot, &synth);
		}
	}
}

static void syNetRbSnapPruneStaleNessPKWaveEffects(const SYNetRbSnapshotSlot *slot)
{
	s32 pass;
	GObj *gobj;
	GObj *next;

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
			if (syNetRbSnapLiveEffectIsNessPKWave(gobj, ep) == FALSE)
			{
				continue;
			}
			if (ep->fighter_gobj == NULL)
			{
				syNetRbSnapEjectGObj(gobj);
				continue;
			}
			fp = ftGetStruct(ep->fighter_gobj);
			if ((fp == NULL) || (syNetRbSnapFighterInNessPKWaveScope(fp) == FALSE))
			{
				if (fp != NULL)
				{
					fp->is_effect_attach = FALSE;
				}
				syNetRbSnapEjectGObj(gobj);
				continue;
			}
			if ((slot != NULL) &&
			    (syNetRbSnapLiveEffectListedInSnapshot(slot, gobj->id) == FALSE) &&
			    (syNetRbSnapFighterInNessPKWaveScope(fp) == FALSE))
			{
				fp->is_effect_attach = FALSE;
				syNetRbSnapEjectGObj(gobj);
			}
		}
	}
}

static sb32 syNetRbSnapBlobInNessSpecialLwScope(const SYNetRbSnapFighterBlob *blob)
{
	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return FALSE;
	}
	if ((blob->fkind != nFTKindNess) && (blob->fkind != nFTKindNNess))
	{
		return FALSE;
	}
	if ((blob->status_id >= nFTNessStatusSpecialLwScopeStart) && (blob->status_id <= nFTNessStatusSpecialLwScopeEnd))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapFighterInNessPsychicMagnetEffectScope(const FTStruct *fp)
{
	if (syNetRbSnapFighterInNessSpecialLwScope(fp) == FALSE)
	{
		return FALSE;
	}
	switch (fp->status_id)
	{
	case nFTNessStatusSpecialLwHold:
	case nFTNessStatusSpecialAirLwHold:
	case nFTNessStatusSpecialLwHit:
	case nFTNessStatusSpecialAirLwHit:
		return TRUE;
	default:
		return FALSE;
	}
}

static sb32 syNetRbSnapFighterBlobNessPsychicMagnetAttachPending(const SYNetRbSnapFighterBlob *blob)
{
	if ((blob == NULL) || (blob->is_valid == FALSE) || (blob->is_effect_attach == 0U))
	{
		return FALSE;
	}
	if ((syNetRbSnapGuardEffectIdFromBlob(blob) != 0U) || (blob->captureyoshi_effect_gobj_id != 0U) ||
	    (syNetRbSnapFoxSpecialLwEffectIdFromBlob(blob) != 0U))
	{
		return FALSE;
	}
	if (syNetRbSnapBlobInNessSpecialLwScope(blob) == FALSE)
	{
		return FALSE;
	}
	switch (blob->status_id)
	{
	case nFTNessStatusSpecialLwHold:
	case nFTNessStatusSpecialAirLwHold:
	case nFTNessStatusSpecialLwHit:
	case nFTNessStatusSpecialAirLwHit:
		return TRUE;
	default:
		return FALSE;
	}
}

static sb32 syNetRbSnapLiveFighterHasNessPsychicMagnet(GObj *fighter_gobj)
{
	s32 pass;

	if (fighter_gobj == NULL)
	{
		return FALSE;
	}
	for (pass = 0; pass < 2; pass++)
	{
		GObj *gobj;

		for (gobj = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		     gobj != NULL; gobj = gobj->link_next)
		{
			EFStruct *ep = efGetStruct(gobj);

			if ((syNetplayLiveEffectIsNessPsychicMagnet(gobj, ep) != FALSE) && (ep->fighter_gobj == fighter_gobj))
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

static void syNetRbSnapEnsureNessPsychicMagnetEffectsFromSlot(const SYNetRbSnapshotSlot *slot)
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
		const SYNetRbSnapFighterBlob *fb;
		s32 ei;
		s32 pi;

		fp = ftGetStruct(fighter_gobj);
		if ((fp == NULL) || (syNetRbSnapFighterInNessPsychicMagnetEffectScope(fp) == FALSE))
		{
			continue;
		}
		pi = fp->player;
		if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX))
		{
			continue;
		}
		fb = &slot->fighters[pi];
		if (syNetRbSnapFighterBlobNessPsychicMagnetAttachPending(fb) == FALSE)
		{
			continue;
		}
		if (syNetRbSnapLiveFighterHasNessPsychicMagnet(fighter_gobj) != FALSE)
		{
			continue;
		}
		for (ei = 0; ei < slot->effect_count; ei++)
		{
			const SYNetRbSnapEffectBlob *eb = &slot->effects[ei];
			GObj *eg;

			if ((eb->is_valid == FALSE) || (eb->fighter_gobj_id != (u32)fighter_gobj->id))
			{
				continue;
			}
			if (eb->respawn_kind != SYNETRB_EFFECT_RESPAWN_NESS_PSYCHIC_MAGNET)
			{
				continue;
			}
			eg = gcFindGObjByID(eb->gobj_id);
			if ((eg != NULL) && (efGetStruct(eg) != NULL))
			{
				continue;
			}
			(void)syNetRbSnapTryRespawnEffectFromBlob(slot, eb);
			break;
		}
		if (syNetRbSnapLiveFighterHasNessPsychicMagnet(fighter_gobj) == FALSE)
		{
			SYNetRbSnapEffectBlob synth;

			memset(&synth, 0, sizeof(synth));
			synth.is_valid = TRUE;
			synth.fighter_gobj_id = (u32)fighter_gobj->id;
			synth.respawn_kind = SYNETRB_EFFECT_RESPAWN_NESS_PSYCHIC_MAGNET;
			(void)syNetRbSnapTryRespawnEffectFromBlob(slot, &synth);
		}
	}
}

static void syNetRbSnapPruneStaleNessPsychicMagnetEffects(const SYNetRbSnapshotSlot *slot)
{
	s32 pass;
	GObj *gobj;
	GObj *next;

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
			if (syNetplayLiveEffectIsNessPsychicMagnet(gobj, ep) == FALSE)
			{
				continue;
			}
			if (ep->fighter_gobj == NULL)
			{
				syNetRbSnapEjectGObj(gobj);
				continue;
			}
			fp = ftGetStruct(ep->fighter_gobj);
			if ((fp == NULL) || (syNetRbSnapFighterInNessPsychicMagnetEffectScope(fp) == FALSE))
			{
				if (fp != NULL)
				{
					fp->is_effect_attach = FALSE;
				}
				syNetRbSnapEjectGObj(gobj);
				continue;
			}
			if ((slot != NULL) && (syNetRbSnapLiveEffectListedInSnapshot(slot, gobj->id) == FALSE) &&
			    (syNetRbSnapFighterInNessPsychicMagnetEffectScope(fp) == FALSE))
			{
				fp->is_effect_attach = FALSE;
				syNetRbSnapEjectGObj(gobj);
			}
		}
	}
}

static sb32 syNetRbSnapFighterInPikachuAttackS4Scope(const FTStruct *fp)
{
	if ((fp == NULL) || ((fp->fkind != nFTKindPikachu) && (fp->fkind != nFTKindNPikachu)))
	{
		return FALSE;
	}
	if ((fp->status_id >= nFTCommonStatusAttackS4Hi) && (fp->status_id <= nFTCommonStatusAttackS4Lw))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapBlobInPikachuAttackS4Scope(const SYNetRbSnapFighterBlob *blob)
{
	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return FALSE;
	}
	if ((blob->fkind != nFTKindPikachu) && (blob->fkind != nFTKindNPikachu))
	{
		return FALSE;
	}
	if ((blob->status_id >= nFTCommonStatusAttackS4Hi) && (blob->status_id <= nFTCommonStatusAttackS4Lw))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapshotAnyFighterPikachuAttackS4ScopeActive(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		if (syNetRbSnapFighterInPikachuAttackS4Scope(ftGetStruct(fighter_gobj)) != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapLiveEffectIsPikachuThunderShock(const GObj *gobj, const EFStruct *ep)
{
	FTStruct *fp;
	DObj *dobj;

	if ((gobj == NULL) || (ep == NULL) || (ep->fighter_gobj == NULL) ||
	    (ep->proc_update != efManagerHaveStructProcUpdate))
	{
		return FALSE;
	}
	fp = ftGetStruct(ep->fighter_gobj);
	if ((fp == NULL) || ((fp->fkind != nFTKindPikachu) && (fp->fkind != nFTKindNPikachu)))
	{
		return FALSE;
	}
	dobj = DObjGetStruct(gobj);
	if ((dobj == NULL) || (dobj->user_data.p != fp->joints[nFTPartsJointTopN]))
	{
		return FALSE;
	}
	return (dobj->child != NULL) ? TRUE : FALSE;
}

static sb32 syNetRbSnapLiveFighterHasPikachuThunderShock(GObj *fighter_gobj)
{
	s32 pass;

	if (fighter_gobj == NULL)
	{
		return FALSE;
	}
	for (pass = 0; pass < 2; pass++)
	{
		GObj *gobj;

		for (gobj = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		     gobj != NULL; gobj = gobj->link_next)
		{
			EFStruct *ep = efGetStruct(gobj);

			if ((syNetRbSnapLiveEffectIsPikachuThunderShock(gobj, ep) != FALSE) &&
			    (ep->fighter_gobj == fighter_gobj))
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapFighterBlobPikachuThunderShockAttachPending(const SYNetRbSnapFighterBlob *blob)
{
	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return FALSE;
	}
	if (syNetRbSnapBlobInPikachuAttackS4Scope(blob) == FALSE)
	{
		return FALSE;
	}
	if ((blob->is_effect_attach != 0U) || (blob->is_attack_active != FALSE))
	{
		return TRUE;
	}
	if ((blob->motion_vars_flags[1] != 0U) || (blob->motion_vars_flags[2] != 0U))
	{
		return TRUE;
	}
	return FALSE;
}

static GObj *syNetRbSnapMakePikachuThunderShockForFighter(GObj *fighter_gobj, s32 frame)
{
	FTStruct *fp;
	Vec3f offset;
	GObj *effect_gobj;

	if (fighter_gobj == NULL)
	{
		return NULL;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp == NULL)
	{
		return NULL;
	}
	if (fp->motion_vars.flags.flag1 != 0)
	{
		offset.x = -FTCOMMON_ATTACKS4_THUNDERSHOCK_GFX_OFF_X;
		offset.z = FTCOMMON_ATTACKS4_THUNDERSHOCK_GFX_OFF_Z;
		offset.y = FTCOMMON_ATTACKS4_THUNDERSHOCK_GFX_OFF_Y;
	}
	else if (fp->motion_vars.flags.flag2 != 0)
	{
		offset.x = FTCOMMON_ATTACKS4_THUNDERSHOCK_GFX_OFF_X;
		offset.z = FTCOMMON_ATTACKS4_THUNDERSHOCK_GFX_OFF_Z;
		offset.y = FTCOMMON_ATTACKS4_THUNDERSHOCK_GFX_OFF_Y;
	}
	else
	{
		offset.x = (fp->lr == -1) ? -FTCOMMON_ATTACKS4_THUNDERSHOCK_GFX_OFF_X : FTCOMMON_ATTACKS4_THUNDERSHOCK_GFX_OFF_X;
		offset.z = FTCOMMON_ATTACKS4_THUNDERSHOCK_GFX_OFF_Z;
		offset.y = FTCOMMON_ATTACKS4_THUNDERSHOCK_GFX_OFF_Y;
	}
	gmCollisionGetFighterPartsWorldPosition(fp->joints[11], &offset);
	func_ovl2_800EE018(fp->joints[nFTPartsJointTopN], &offset);
	effect_gobj = efManagerPikachuThunderShockMakeEffect(fighter_gobj, &offset, frame);
	if (effect_gobj != NULL)
	{
		fp->is_effect_attach = TRUE;
	}
	return effect_gobj;
}

static void syNetRbSnapEnsurePikachuThunderShockEffectsFromSlot(const SYNetRbSnapshotSlot *slot)
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
		const SYNetRbSnapFighterBlob *fb;
		s32 ei;
		s32 pi;
		s32 pending;

		fp = ftGetStruct(fighter_gobj);
		if ((fp == NULL) || (syNetRbSnapFighterInPikachuAttackS4Scope(fp) == FALSE))
		{
			continue;
		}
		pi = fp->player;
		if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX))
		{
			continue;
		}
		fb = &slot->fighters[pi];
		pending = syNetRbSnapFighterBlobPikachuThunderShockAttachPending(fb);
		if (pending == FALSE)
		{
			continue;
		}
		for (ei = 0; ei < slot->effect_count; ei++)
		{
			const SYNetRbSnapEffectBlob *eb = &slot->effects[ei];
			GObj *eg;

			if ((eb->is_valid == FALSE) || (eb->fighter_gobj_id != (u32)fighter_gobj->id))
			{
				continue;
			}
			if (eb->respawn_kind != SYNETRB_EFFECT_RESPAWN_PIKACHU_THUNDER_SHOCK)
			{
				continue;
			}
			eg = gcFindGObjByID(eb->gobj_id);
			if ((eg != NULL) && (efGetStruct(eg) != NULL))
			{
				continue;
			}
			(void)syNetRbSnapTryRespawnEffectFromBlob(slot, eb);
		}
		if ((syNetRbSnapLiveFighterHasPikachuThunderShock(fighter_gobj) == FALSE) &&
		    ((fp->is_attack_active != FALSE) || (fp->motion_vars.flags.flag1 != 0) ||
		     (fp->motion_vars.flags.flag2 != 0)))
		{
			s32 frame;

			frame = ftStatusVarsAttack4(fp)->gfx_id;
			if ((frame < 0) || (frame >= FTCOMMON_ATTACKS4_THUNDERSHOCK_GFX_ID_MAX))
			{
				frame = 0;
			}
			(void)syNetRbSnapMakePikachuThunderShockForFighter(fighter_gobj, frame);
		}
	}
}

static void syNetRbSnapPruneStalePikachuThunderShockEffects(const SYNetRbSnapshotSlot *slot)
{
	s32 pass;
	GObj *gobj;
	GObj *next;

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
			if (syNetRbSnapLiveEffectIsPikachuThunderShock(gobj, ep) == FALSE)
			{
				continue;
			}
			if (ep->fighter_gobj == NULL)
			{
				syNetRbSnapEjectGObj(gobj);
				continue;
			}
			fp = ftGetStruct(ep->fighter_gobj);
			if ((fp == NULL) || (syNetRbSnapFighterInPikachuAttackS4Scope(fp) == FALSE))
			{
				if (fp != NULL)
				{
					fp->is_effect_attach = FALSE;
				}
				syNetRbSnapEjectGObj(gobj);
				continue;
			}
			if ((slot != NULL) && (syNetRbSnapLiveEffectListedInSnapshot(slot, gobj->id) == FALSE) &&
			    (syNetRbSnapFighterInPikachuAttackS4Scope(fp) == FALSE))
			{
				fp->is_effect_attach = FALSE;
				syNetRbSnapEjectGObj(gobj);
			}
		}
	}
}

static sb32 syNetRbSnapFighterInKirbySpecialNInhaleScope(const FTStruct *fp)
{
	if ((fp == NULL) || ((fp->fkind != nFTKindKirby) && (fp->fkind != nFTKindNKirby)))
	{
		return FALSE;
	}
	switch (fp->status_id)
	{
	case nFTKirbyStatusSpecialNStart:
	case nFTKirbyStatusSpecialNLoop:
	case nFTKirbyStatusSpecialAirNStart:
	case nFTKirbyStatusSpecialAirNLoop:
		return TRUE;
	default:
		return FALSE;
	}
}

static sb32 syNetRbSnapFighterInKirbySpecialNInhaleDeferScope(const FTStruct *fp)
{
	if ((fp == NULL) || ((fp->fkind != nFTKindKirby) && (fp->fkind != nFTKindNKirby)))
	{
		return FALSE;
	}
	if ((fp->status_id >= nFTKirbyStatusSpecialNStart) && (fp->status_id <= nFTKirbyStatusSpecialNCopy))
	{
		return TRUE;
	}
	if ((fp->status_id >= nFTKirbyStatusSpecialAirNStart) && (fp->status_id <= nFTKirbyStatusSpecialAirNCopy))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapBlobInKirbySpecialNInhaleDeferScope(const SYNetRbSnapFighterBlob *blob)
{
	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return FALSE;
	}
	if ((blob->fkind != nFTKindKirby) && (blob->fkind != nFTKindNKirby))
	{
		return FALSE;
	}
	if ((blob->status_id >= nFTKirbyStatusSpecialNStart) && (blob->status_id <= nFTKirbyStatusSpecialNCopy))
	{
		return TRUE;
	}
	if ((blob->status_id >= nFTKirbyStatusSpecialAirNStart) && (blob->status_id <= nFTKirbyStatusSpecialAirNCopy))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapLiveHasKirbyInhaleWindEffect(void)
{
	s32 pass;
	GObj *gobj;

	for (pass = 0; pass < 2; pass++)
	{
		for (gobj = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		     gobj != NULL; gobj = gobj->link_next)
		{
			if (syNetRbSnapEffectGObjHasUpdateProc(gobj, efManagerKirbyInhaleWindProcUpdate) != FALSE)
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapshotAnyFighterKirbySpecialNInhaleDeferActive(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		if (syNetRbSnapFighterInKirbySpecialNInhaleDeferScope(ftGetStruct(fighter_gobj)) != FALSE)
		{
			return TRUE;
		}
	}
	return (syNetRbSnapLiveHasKirbyInhaleWindEffect() != FALSE) ? TRUE : FALSE;
}

static void syNetRbSnapEndKirbyInhaleWindProc(GObj *gobj)
{
	GObjProcess *gobjproc;
	GObjProcess *next;

	if (gobj == NULL)
	{
		return;
	}
	for (gobjproc = gobj->gobjproc_head; gobjproc != NULL; gobjproc = next)
	{
		next = gobjproc->link_next;
		if ((gobjproc->kind == nGCProcessKindFunc) &&
		    (gobjproc->exec.func == efManagerKirbyInhaleWindProcUpdate))
		{
			gcEndGObjProcess(gobjproc);
		}
	}
}

static void syNetRbSnapEjectKirbyInhaleWindEffectGObj(GObj *gobj, FTStruct *owner_fp)
{
	EFStruct *ep;

	if (gobj == NULL)
	{
		return;
	}
	ep = efGetStruct(gobj);
	if (owner_fp == NULL)
	{
		if ((ep != NULL) && (ep->fighter_gobj != NULL))
		{
			owner_fp = ftGetStruct(ep->fighter_gobj);
		}
	}
	syNetRbSnapEndKirbyInhaleWindProc(gobj);
	if (owner_fp != NULL)
	{
		syNetRbSnapClearFighterEffectPointerIfMatch(owner_fp, gobj);
		if (owner_fp->is_effect_attach != FALSE)
		{
			owner_fp->is_effect_attach = FALSE;
		}
	}
	syNetRbSnapEjectGObj(gobj);
}

static void syNetRbSnapEjectKirbyInhaleWindEffectsForFighter(GObj *fighter_gobj)
{
	s32 pass;
	GObj *gobj;
	GObj *next;

	if (fighter_gobj == NULL)
	{
		return;
	}
	for (pass = 0; pass < 2; pass++)
	{
		for (gobj = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		     gobj != NULL; gobj = next)
		{
			EFStruct *ep;
			sb32 is_inhale_wind;

			next = gobj->link_next;
			ep = efGetStruct(gobj);
			is_inhale_wind =
			    (syNetRbSnapEffectGObjHasUpdateProc(gobj, efManagerKirbyInhaleWindProcUpdate) != FALSE) ? TRUE
			                                                                                           : FALSE;
			if (is_inhale_wind == FALSE)
			{
				if ((ep == NULL) || (ep->fighter_gobj != fighter_gobj))
				{
					continue;
				}
			}
			else if ((ep != NULL) && (ep->fighter_gobj != NULL) && (ep->fighter_gobj != fighter_gobj))
			{
				continue;
			}
			else if ((ep == NULL) && (is_inhale_wind == FALSE))
			{
				continue;
			}
			syNetRbSnapEjectKirbyInhaleWindEffectGObj(gobj, ftGetStruct(fighter_gobj));
		}
	}
}

static void syNetRbSnapSweepZombieKirbyInhaleWindEffects(void)
{
	s32 pass;
	GObj *gobj;
	GObj *next;

	for (pass = 0; pass < 2; pass++)
	{
		for (gobj = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		     gobj != NULL; gobj = next)
		{
			EFStruct *ep;
			FTStruct *fp;

			next = gobj->link_next;
			if (syNetRbSnapEffectGObjHasUpdateProc(gobj, efManagerKirbyInhaleWindProcUpdate) == FALSE)
			{
				continue;
			}
			ep = efGetStruct(gobj);
			if (ep == NULL)
			{
				syNetRbSnapEjectKirbyInhaleWindEffectGObj(gobj, NULL);
				continue;
			}
			fp = (ep->fighter_gobj != NULL) ? ftGetStruct(ep->fighter_gobj) : NULL;
			if ((fp == NULL) || (syNetRbSnapFighterInKirbySpecialNInhaleScope(fp) == FALSE) ||
			    (fp->is_effect_attach == FALSE))
			{
				syNetRbSnapEjectKirbyInhaleWindEffectGObj(gobj, fp);
			}
		}
	}
}

static void syNetRbSnapPruneStaleKirbyInhaleWindEffects(const SYNetRbSnapshotSlot *slot)
{
	s32 pass;
	GObj *gobj;
	GObj *next;

	for (pass = 0; pass < 2; pass++)
	{
		GObj *link_head;

		link_head = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		for (gobj = link_head; gobj != NULL; gobj = next)
		{
			EFStruct *ep;
			FTStruct *fp;
			s32 pi;
			const SYNetRbSnapFighterBlob *fb;

			next = gobj->link_next;
			if (syNetRbSnapEffectGObjHasUpdateProc(gobj, efManagerKirbyInhaleWindProcUpdate) == FALSE)
			{
				continue;
			}
			ep = efGetStruct(gobj);
			if (ep == NULL)
			{
				syNetRbSnapEjectKirbyInhaleWindEffectGObj(gobj, NULL);
				continue;
			}
			if (ep->fighter_gobj == NULL)
			{
				syNetRbSnapEjectKirbyInhaleWindEffectGObj(gobj, NULL);
				continue;
			}
			fp = ftGetStruct(ep->fighter_gobj);
			if ((fp == NULL) || (syNetRbSnapFighterInKirbySpecialNInhaleScope(fp) == FALSE) ||
			    (fp->is_effect_attach == FALSE))
			{
				syNetRbSnapEjectKirbyInhaleWindEffectGObj(gobj, fp);
				continue;
			}
			if (slot == NULL)
			{
				continue;
			}
			pi = fp->player;
			if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX))
			{
				continue;
			}
			fb = &slot->fighters[pi];
			if ((fb->is_valid != FALSE) && (fb->is_effect_attach == 0U))
			{
				syNetRbSnapEjectKirbyInhaleWindEffectGObj(gobj, fp);
			}
		}
	}
}

static sb32 syNetRbSnapEffectIsRebirthHaloCoupling(const GObj *effect_gobj, const EFStruct *ep, const FTStruct *fp)
{
	DObj *dobj;

	if ((effect_gobj == NULL) || (ep == NULL) || (fp == NULL) || (ep->fighter_gobj == NULL))
	{
		return FALSE;
	}
	if (ep->proc_update != gcPlayAnimAll)
	{
		return FALSE;
	}
	dobj = DObjGetStruct(effect_gobj);
	return ((dobj != NULL) && (dobj->user_data.p == fp->joints[nFTPartsJointTopN])) ? TRUE : FALSE;
}

static sb32 syNetRbSnapLiveFighterHasRebirthHalo(GObj *fighter_gobj)
{
	s32 pass;
	GObj *gobj;

	if (fighter_gobj == NULL)
	{
		return FALSE;
	}
	for (pass = 0; pass < 2; pass++)
	{
		GObj *link_head;

		link_head = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		for (gobj = link_head; gobj != NULL; gobj = gobj->link_next)
		{
			EFStruct *ep;
			FTStruct *fp;

			ep = efGetStruct(gobj);
			if ((ep == NULL) || (ep->fighter_gobj != fighter_gobj))
			{
				continue;
			}
			fp = ftGetStruct(fighter_gobj);
			if (syNetRbSnapEffectIsRebirthHaloCoupling(gobj, ep, fp) != FALSE)
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

u8 syNetRbSnapEffectRespawnKindFromLive(const GObj *gobj, const EFStruct *ep)
{
	FTStruct *fp;

	if ((gobj == NULL) || (ep == NULL))
	{
		return SYNETRB_EFFECT_RESPAWN_NONE;
	}
	if (syNetRbSnapLiveEffectIsQuake(gobj, ep) != FALSE)
	{
		return SYNETRB_EFFECT_RESPAWN_QUAKE;
	}
	if (ep->proc_update == efManagerFoxReflectorProcUpdate)
	{
		return SYNETRB_EFFECT_RESPAWN_FOX_REFLECTOR;
	}
	if (ep->proc_update == efManagerYoshiEggLayProcUpdate)
	{
		return SYNETRB_EFFECT_RESPAWN_YOSHI_EGG_LAY;
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
		if ((fp != NULL) && (syNetRbSnapEffectIsRebirthHaloCoupling(gobj, ep, fp) != FALSE))
		{
			return SYNETRB_EFFECT_RESPAWN_REBIRTH_HALO;
		}
		if ((fp != NULL) && (syNetRbSnapFighterInNessPKWaveScope(fp) != FALSE))
		{
			return SYNETRB_EFFECT_RESPAWN_NESS_PK_WAVE;
		}
		if ((fp != NULL) && (syNetplayLiveEffectIsNessPsychicMagnet(gobj, ep) != FALSE))
		{
			return SYNETRB_EFFECT_RESPAWN_NESS_PSYCHIC_MAGNET;
		}
	}
	if ((ep->proc_update == efManagerHaveStructProcUpdate) && (ep->fighter_gobj != NULL))
	{
		if (syNetRbSnapLiveEffectIsPikachuThunderShock(gobj, ep) != FALSE)
		{
			fp = ftGetStruct(ep->fighter_gobj);
			if ((fp != NULL) && (syNetRbSnapFighterInPikachuAttackS4Scope(fp) != FALSE))
			{
				return SYNETRB_EFFECT_RESPAWN_PIKACHU_THUNDER_SHOCK;
			}
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
		if ((syNetRbSnapGuardEffectIdFromBlob(fb) == gobj_id) || (fb->captureyoshi_effect_gobj_id == gobj_id) ||
		    (syNetRbSnapFoxSpecialLwEffectIdFromBlob(fb) == gobj_id))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static void syNetRbSnapClearCaptureYoshiEffectPointerIfMatch(FTStruct *fp, GObj *ejected_gobj)
{
	if ((fp == NULL) || (ejected_gobj == NULL))
	{
		return;
	}
	if ((fp->status_id == nFTCommonStatusYoshiEgg) &&
	    (ftStatusVarsCaptureYoshi(fp)->effect_gobj == ejected_gobj))
	{
		ftStatusVarsCaptureYoshi(fp)->effect_gobj = NULL;
	}
}

static void syNetRbSnapClearFighterEffectPointerIfMatch(FTStruct *fp, GObj *ejected_gobj)
{
	if ((fp == NULL) || (ejected_gobj == NULL))
	{
		return;
	}
	if ((syNetRbSnapFighterGuardEffectUnionOwned(fp) != FALSE) &&
	    (ftStatusVarsGuard(fp)->effect_gobj == ejected_gobj))
	{
		ftStatusVarsGuard(fp)->effect_gobj = NULL;
	}
	if ((fp->status_id == nFTCommonStatusYoshiEgg) &&
	    (ftStatusVarsCaptureYoshi(fp)->effect_gobj == ejected_gobj))
	{
		ftStatusVarsCaptureYoshi(fp)->effect_gobj = NULL;
	}
	if ((syNetRbSnapFighterInFoxReflectorScope(fp) != FALSE) &&
	    (fp->status_vars.fox.speciallw.effect_gobj == ejected_gobj))
	{
		fp->status_vars.fox.speciallw.effect_gobj = NULL;
	}
}

static void syNetRbSnapClearAllFightersCaptureYoshiEffectPointerIfMatch(GObj *ejected_gobj)
{
	GObj *fighter_gobj;

	if (ejected_gobj == NULL)
	{
		return;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		syNetRbSnapClearCaptureYoshiEffectPointerIfMatch(ftGetStruct(fighter_gobj), ejected_gobj);
	}
}

static void syNetRbSnapClearAllFightersEffectPointerIfMatch(GObj *ejected_gobj)
{
	GObj *fighter_gobj;

	if (ejected_gobj == NULL)
	{
		return;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		syNetRbSnapClearFighterEffectPointerIfMatch(ftGetStruct(fighter_gobj), ejected_gobj);
	}
}

static sb32 syNetRbSnapLiveGuardClaimsEffectId(u32 effect_gobj_id)
{
	GObj *fighter_gobj;

	if (effect_gobj_id == 0U)
	{
		return FALSE;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		GObj *guard_gobj;

		fp = ftGetStruct(fighter_gobj);
		if ((fp == NULL) || (syNetRbSnapFighterGuardEffectUnionOwned(fp) == FALSE))
		{
			continue;
		}
		guard_gobj = ftStatusVarsGuard(fp)->effect_gobj;
		if ((guard_gobj != NULL) && ((u32)guard_gobj->id == effect_gobj_id))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static void syNetRbSnapPruneOrphanFighterAttachedEffects(const SYNetRbSnapshotSlot *slot,
                                                         const u32 *reconciled_ids, s32 reconciled_count)
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
				if (syNetRbSnapEffectGObjHasUpdateProc(gobj, efManagerKirbyInhaleWindProcUpdate) != FALSE)
				{
					syNetRbSnapEjectKirbyInhaleWindEffectGObj(gobj, NULL);
				}
				continue;
			}
			if (syNetRbSnapEffectHiddenFromRollback(gobj, ep) != FALSE)
			{
				continue;
			}
			if (syNetRbSnapLiveEffectKeptAfterReconcile(slot, gobj->id, reconciled_ids, reconciled_count) != FALSE)
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
			syNetRbSnapEjectGObj(gobj);
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
			syNetRbSnapEjectKirbyInhaleWindEffectsForFighter(fighter_gobj);
			continue;
		}
		if ((syNetRbSnapGuardEffectIdFromBlob(blob) == 0U) && (blob->captureyoshi_effect_gobj_id == 0U) &&
		    (syNetRbSnapFoxSpecialLwEffectIdFromBlob(blob) == 0U))
		{
			sb32 keep_attach = FALSE;

			if (syNetRbSnapLiveFighterHasRebirthHalo(fighter_gobj) != FALSE)
			{
				keep_attach = TRUE;
			}
			else if (syNetRbSnapFighterRebirthHaloLifecycleActive(fp) != FALSE)
			{
				keep_attach = TRUE;
			}
			else if (syNetRbSnapFighterBlobRebirthHaloPending(blob) != FALSE)
			{
				keep_attach = TRUE;
			}
			else if (syNetRbSnapSlotListsRebirthHaloForFighter(slot, (u32)fighter_gobj->id, 0U) != FALSE)
			{
				keep_attach = TRUE;
			}
			else if (syNetRbSnapFighterBlobNessPKWaveAttachPending(blob) != FALSE)
			{
				keep_attach = TRUE;
			}
			else if (syNetRbSnapLiveFighterHasNessPKWave(fighter_gobj) != FALSE)
			{
				keep_attach = TRUE;
			}
			else if (syNetRbSnapFighterBlobPikachuThunderShockAttachPending(blob) != FALSE)
			{
				keep_attach = TRUE;
			}
			else if (syNetRbSnapLiveFighterHasPikachuThunderShock(fighter_gobj) != FALSE)
			{
				keep_attach = TRUE;
			}
			if (keep_attach == FALSE)
			{
				fp->is_effect_attach = FALSE;
				syNetRbSnapEjectKirbyInhaleWindEffectsForFighter(fighter_gobj);
			}
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

/*
 * Ground blobs store raw sim scalars. Rollback map hash folds a hash-grid view of known F32 payload fields
 * so save and verify agree without quantizing stored snapshot bytes.
 */
static u32 syNetRbSnapFoldGroundPayloadHashForRollback(const SYNetRbSnapGroundBlob *ground)
{
	u8 payload_scratch[SYNETRB_SNAPSHOT_GROUND_PAYLOAD_MAX];
	u32 hash;
	u32 n;

	if ((ground == NULL) || (ground->payload_len == 0U))
	{
		return 2166136261U;
	}
	n = (u32)ground->payload_len;
	if (n > SYNETRB_SNAPSHOT_GROUND_PAYLOAD_MAX)
	{
		n = SYNETRB_SNAPSHOT_GROUND_PAYLOAD_MAX;
	}
	memcpy(payload_scratch, ground->payload, n);
#if defined(SSB64_NETMENU)
	switch (ground->gkind)
	{
	case nGRKindHyrule:
		if (n >= (u32)sizeof(SYNetRbSnapGroundHyrule))
		{
			SYNetRbSnapGroundHyrule *hy = (SYNetRbSnapGroundHyrule *)payload_scratch;

			hy->twister_leftedge_x = syNetplayQuantizeF32ForRollbackHash(hy->twister_leftedge_x);
			hy->twister_rightedge_x = syNetplayQuantizeF32ForRollbackHash(hy->twister_rightedge_x);
			hy->twister_vel = syNetplayQuantizeF32ForRollbackHash(hy->twister_vel);
			hy->twister_pos.x = syNetplayQuantizeF32ForRollbackHash(hy->twister_pos.x);
			hy->twister_pos.y = syNetplayQuantizeF32ForRollbackHash(hy->twister_pos.y);
			hy->twister_pos.z = syNetplayQuantizeF32ForRollbackHash(hy->twister_pos.z);
		}
		break;
	case nGRKindJungle:
		if (n >= (u32)sizeof(SYNetRbSnapGroundJungle))
		{
			SYNetRbSnapGroundJungle *jg = (SYNetRbSnapGroundJungle *)payload_scratch;

			jg->tarucann_rotate_step = syNetplayQuantizeF32ForRollbackHash(jg->tarucann_rotate_step);
			jg->tarucann_rotate_z = syNetplayQuantizeF32ForRollbackHash(jg->tarucann_rotate_z);
			jg->tarucann_translate.x = syNetplayQuantizeF32ForRollbackHash(jg->tarucann_translate.x);
			jg->tarucann_translate.y = syNetplayQuantizeF32ForRollbackHash(jg->tarucann_translate.y);
			jg->tarucann_translate.z = syNetplayQuantizeF32ForRollbackHash(jg->tarucann_translate.z);
		}
		break;
	case nGRKindSector:
		if (n >= (u32)sizeof(SYNetRbSnapGroundSector))
		{
			SYNetRbSnapGroundSector *sec = (SYNetRbSnapGroundSector *)payload_scratch;

			sec->arwing_target_x = syNetplayQuantizeF32ForRollbackHash(sec->arwing_target_x);
		}
		break;
	default:
		break;
	}
#endif
	hash = 2166136261U;
	hash = syNetRbSnapFnvAccumulateU32(hash, (u32)ground->gkind);
	{
		u32 i;

		for (i = 0; i < n; i++)
		{
			hash = syNetRbSnapFnvAccumulateU32(hash, (u32)payload_scratch[i]);
		}
	}
	return hash;
}

static u32 syNetRbSnapshotComputeMapHashWithGround(const SYNetRbSnapGroundBlob *ground)
{
	u32 hash;

	hash = syNetSyncHashMapCollisionKinematicsForRollback();
#ifdef PORT
	if ((ground != NULL) && (ground->payload_len > 0U))
	{
		u32 ground_hash = syNetRbSnapFoldGroundPayloadHashForRollback(ground);

		hash = syNetRbSnapFnvAccumulateU32(hash ^ ground_hash, 0x47524F55U);
	}
#endif
	return hash;
}

static sb32 syNetRbSnapHyruleTwisterStatusNeedsGObj(u8 twister_status);
static void syNetRbSnapHyruleTwisterNormalizeAtCapture(SYNetRbSnapGroundHyrule *dst,
							 const GRCommonGroundVarsHyrule *src);
static void syNetRbSnapHyruleTwisterRebindMeshFromLink(GRCommonGroundVarsHyrule *hy,
						       const SYNetRbSnapGroundHyrule *src);
#ifdef PORT
static sb32 syNetRbSnapHyruleTwisterDiagEnabled(void);
static void syNetRbSnapHyruleTwisterLogCapture(u32 tick, u8 live_status, const SYNetRbSnapGroundHyrule *dst,
					       const char *note);
static void syNetRbSnapCaptureJungleTaruCannRiderState(SYNetRbSnapGroundJungle *dst, GObj *tarucann_gobj);
#endif

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
		dst->twister_leftedge_x = src->twister_leftedge_x;
		dst->twister_rightedge_x = src->twister_rightedge_x;
		dst->twister_vel = src->twister_vel;
		dst->twister_wait = src->twister_wait;
		dst->twister_speed_wait = src->twister_speed_wait;
		dst->twister_turn_wait = src->twister_turn_wait;
		dst->twister_line_id = src->twister_line_id;
		dst->twister_status = src->twister_status;
		dst->twister_pos_count = src->twister_pos_count;
		if (src->twister_gobj != NULL)
		{
			DObj *twister_dobj = DObjGetStruct(src->twister_gobj);

			dst->twister_pos =
			    (twister_dobj != NULL) ? twister_dobj->translate.vec.f : (Vec3f){0.0F, 0.0F, 0.0F};
		}
		else
		{
			dst->twister_pos.x = 0.0F;
			dst->twister_pos.y = 0.0F;
			dst->twister_pos.z = 0.0F;
		}
		syNetRbSnapHyruleTwisterNormalizeAtCapture(dst, src);
#ifdef PORT
		if (syNetRbSnapHyruleTwisterDiagEnabled() != FALSE)
		{
			const char *capture_note;

			capture_note = "save";
			if (syNetRbSnapHyruleTwisterStatusNeedsGObj(src->twister_status) != FALSE)
			{
				if (src->twister_gobj == NULL)
				{
					capture_note = "live_active_no_gobj";
				}
				else if (src->twister_status == (u8)nGRHyruleTwisterStatusSubside)
				{
					capture_note = "live_subside";
				}
				else if (src->twister_status == (u8)nGRHyruleTwisterStatusStop)
				{
					capture_note = "live_stop";
				}
				else if (src->twister_status == (u8)nGRHyruleTwisterStatusSummon)
				{
					capture_note = "live_summon";
				}
				else if (src->twister_status == (u8)nGRHyruleTwisterStatusMove)
				{
					capture_note = "live_move";
				}
			}
			syNetRbSnapHyruleTwisterLogCapture(slot->tick, src->twister_status, dst, capture_note);
		}
#endif
#if defined(SSB64_NETMENU)
		dst->twister_leftedge_x = syNetplayQuantizeF32(dst->twister_leftedge_x);
		dst->twister_rightedge_x = syNetplayQuantizeF32(dst->twister_rightedge_x);
		dst->twister_vel = syNetplayQuantizeF32(dst->twister_vel);
		syNetplayQuantizeVec3f(&dst->twister_pos);
#endif
		slot->ground_captured = TRUE;
		break;
	}
	case nGRKindJungle:
	{
		const GRCommonGroundVarsJungle *src = &gGRCommonStruct.jungle;
		SYNetRbSnapGroundJungle *dst = (SYNetRbSnapGroundJungle *)slot->ground.payload;
		DObj *root;
		DObj *child;

		slot->ground.payload_len = (u16)sizeof(*dst);
		dst->tarucann_gobj_id = syNetRbSnapGobjId(src->tarucann_gobj);
		dst->tarucann_status = src->tarucann_status;
		dst->tarucann_wait = src->tarucann_wait;
		dst->tarucann_rotate_step = src->tarucann_rotate_step;
		dst->tarucann_dobj_valid_mask = 0U;
		dst->root_anim_wait_bits = 0U;
		dst->child_anim_wait_bits = 0U;
		root = (src->tarucann_gobj != NULL) ? DObjGetStruct(src->tarucann_gobj) : NULL;
		if (root != NULL)
		{
			dst->tarucann_translate = root->translate.vec.f;
			dst->tarucann_rotate_z = root->rotate.vec.f.z;
#if defined(SSB64_NETMENU)
			syNetplayQuantizeVec3f(&dst->tarucann_translate);
			dst->tarucann_rotate_z = syNetplayQuantizeF32(dst->tarucann_rotate_z);
			dst->tarucann_rotate_step = syNetplayQuantizeF32(dst->tarucann_rotate_step);
#endif
#ifdef PORT
			dst->tarucann_dobj_valid_mask |= (u8)SYNETRB_SNAP_GROUND_JUNGLE_DOBJ_ROOT_MOBA;
			memcpy(&dst->root_anim_wait_bits, &root->anim_wait, sizeof(dst->root_anim_wait_bits));
			child = root->child;
			if (child != NULL)
			{
				dst->tarucann_dobj_valid_mask |= (u8)SYNETRB_SNAP_GROUND_JUNGLE_DOBJ_CHILD_MOBA;
				memcpy(&dst->child_anim_wait_bits, &child->anim_wait, sizeof(dst->child_anim_wait_bits));
			}
#else
			if (root->mobj != NULL)
			{
				dst->tarucann_dobj_valid_mask |= (u8)SYNETRB_SNAP_GROUND_JUNGLE_DOBJ_ROOT_MOBA;
				memcpy(&dst->root_anim_wait_bits, &root->mobj->anim_wait, sizeof(dst->root_anim_wait_bits));
			}
			child = root->child;
			if ((child != NULL) && (child->mobj != NULL))
			{
				dst->tarucann_dobj_valid_mask |= (u8)SYNETRB_SNAP_GROUND_JUNGLE_DOBJ_CHILD_MOBA;
				memcpy(&dst->child_anim_wait_bits, &child->mobj->anim_wait, sizeof(dst->child_anim_wait_bits));
			}
#endif
		}
		else
		{
			dst->tarucann_translate.x = 0.0F;
			dst->tarucann_translate.y = 0.0F;
			dst->tarucann_translate.z = 0.0F;
			dst->tarucann_rotate_z = 0.0F;
		}
		syNetRbSnapCaptureJungleTaruCannRiderState(dst, src->tarucann_gobj);
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
#if defined(SSB64_NETMENU)
		dst->acid_level_curr = syNetplayQuantizeF32(dst->acid_level_curr);
		dst->acid_level_step = syNetplayQuantizeF32(dst->acid_level_step);
#endif
		slot->ground_captured = TRUE;
		break;
	}
	case nGRKindYamabuki:
	{
		const GRCommonGroundVarsYamabuki *src = &gGRCommonStruct.yamabuki;
		SYNetRbSnapGroundYamabuki *dst = (SYNetRbSnapGroundYamabuki *)slot->ground.payload;
		DObj *gate_dobj;

		slot->ground.payload_len = (u16)sizeof(*dst);
		dst->monster_gobj_id = syNetRbSnapGobjId(src->monster_gobj);
		dst->gate_gobj_id = syNetRbSnapGobjId(src->gate_gobj);
		dst->gate_pos = src->gate_pos;
		dst->gate_status = src->gate_status;
		dst->gate_noentry = src->gate_noentry;
		dst->gate_anim_phase = src->gate_anim_phase;
		dst->monster_wait = src->monster_wait;
		dst->gate_wait = src->gate_wait;
		dst->monster_id_prev = src->monster_id_prev;
		dst->gate_anim_frame = 0.0F;
		dst->gate_anim_wait = 0.0F;
		if (src->gate_gobj != NULL)
		{
			DObj *child;

			gate_dobj = DObjGetStruct(src->gate_gobj);
			if (gate_dobj != NULL)
			{
				child = gate_dobj->child;
				if (child != NULL)
				{
					dst->gate_anim_frame = child->anim_frame;
					dst->gate_anim_wait = child->anim_wait;
				}
				else
				{
					dst->gate_anim_frame = gate_dobj->anim_frame;
					dst->gate_anim_wait = gate_dobj->anim_wait;
				}
			}
		}
#if defined(SSB64_NETMENU)
		syNetplayQuantizeVec3f(&dst->gate_pos);
		dst->gate_anim_frame = syNetplayQuantizeAnimScalar(dst->gate_anim_frame);
		dst->gate_anim_wait = syNetplayQuantizeAnimScalar(dst->gate_anim_wait);
#endif
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
		dst->pblock_gobj_id = syNetRbSnapGobjId(src->pblock_gobj);
		dst->pblock_appear_wait = src->pblock_appear_wait;
		dst->pblock_pos_count = src->pblock_pos_count;
		memcpy(dst->players_tt, src->players_tt, sizeof(dst->players_tt));
		memcpy(dst->players_ga, src->players_ga, sizeof(dst->players_ga));
		dst->pakkun_gobj_id[0] = syNetRbSnapGobjId(src->pakkun_gobj[0]);
		dst->pakkun_gobj_id[1] = syNetRbSnapGobjId(src->pakkun_gobj[1]);
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
		s32 dj;

		slot->ground.payload_len = (u16)sizeof(*dst);
		dst->map_head = (uintptr_t)src->map_head;
		for (ci = 0; ci < 3; ci++)
		{
			const GRYosterCloud *lc = &src->clouds[ci];
			SYNetRbSnapGroundYosterCloud *sc = &dst->clouds[ci];
			DObj *root_dobj;

			sc->gobj_id = syNetRbSnapGobjId(lc->gobj);
			sc->altitude = lc->altitude;
			sc->pressure = lc->pressure;
			sc->status = lc->status;
			sc->anim_id = lc->anim_id;
			sc->is_cloud_line_active = lc->is_cloud_line_active;
			sc->pressure_timer = lc->pressure_timer;
			sc->evaporate_wait = lc->evaporate_wait;
			sc->dobj_valid_mask = 0U;
			sc->dobj0_anim_wait_bits = 0U;
			sc->translate.x = 0.0F;
			sc->translate.y = 0.0F;
			sc->translate.z = 0.0F;
			for (dj = 0; dj < 3; dj++)
			{
				if (lc->dobj[dj] != NULL)
				{
					sc->dobj_valid_mask |= (u8)(1U << dj);
				}
			}
			if (lc->gobj != NULL)
			{
				root_dobj = DObjGetStruct(lc->gobj);
				if (root_dobj != NULL)
				{
					sc->translate = root_dobj->translate.vec.f;
				}
			}
			if ((lc->dobj[0] != NULL) && (lc->dobj[0]->mobj != NULL))
			{
				memcpy(&sc->dobj0_anim_wait_bits, &lc->dobj[0]->mobj->anim_wait, sizeof(sc->dobj0_anim_wait_bits));
			}
		}
		slot->ground_captured = TRUE;
		break;
	}
	case nGRKindSector:
	{
		const GRCommonGroundVarsSector *src = &gGRCommonStruct.sector;
		SYNetRbSnapGroundSector *dst = (SYNetRbSnapGroundSector *)slot->ground.payload;

		slot->ground.payload_len = (u16)sizeof(*dst);
		dst->map_gobj_id = syNetRbSnapGobjId(src->map_gobj);
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
		if (src->map_gobj != NULL)
		{
			dst->map_gobj_flags = src->map_gobj->flags;
		}
		else
		{
			dst->map_gobj_flags = GOBJ_FLAG_HIDDEN;
		}
		dst->unk_sector_0x4C = src->unk_sector_0x4C;
		dst->unk_sector_0x4D = src->unk_sector_0x4D;
		dst->unk_sector_0x4E = src->unk_sector_0x4E;
		dst->unk_sector_0x52 = src->unk_sector_0x52;
		dst->arwing_last_flight_pattern = src->arwing_last_flight_pattern;
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
		dst->bumper_gobj_id = syNetRbSnapGobjId(src->bumper_gobj);
		dst->bumper_pos = src->bumper_pos;
		slot->ground_captured = TRUE;
		break;
	}
	default:
		break;
	}
}

static GObj *syNetRbSnapResolveCastleBumperGObj(const SYNetRbSnapGroundCastle *src);
static void syNetRbSnapRestoreYosterCloudPresentation(s32 cloud_id, const SYNetRbSnapGroundYosterCloud *sc,
                                                      GRYosterCloud *lc);
static sb32 syNetRbSnapHyruleTwisterStatusNeedsGObj(u8 twister_status);
static sb32 syNetRbSnapHyruleGObjIsStageController(GObj *gobj);
static sb32 syNetRbSnapIsValidHyruleTwisterGObj(GObj *gobj);
static void syNetRbSnapHyruleTwisterClearGObj(GRCommonGroundVarsHyrule *hy);
static void syNetRbSnapHyruleTwisterNormalizeAtCapture(SYNetRbSnapGroundHyrule *dst,
							 const GRCommonGroundVarsHyrule *src);
static void syNetRbSnapHyruleTwisterNormalizeFromBlob(GRCommonGroundVarsHyrule *hy,
						      const SYNetRbSnapGroundHyrule *src);
#ifdef PORT
static void syNetRbSnapResyncFighterTaruCannGobjs(u32 snap_tick);
static void syNetRbSnapResyncFighterTwisterGobjs(u32 snap_tick);
static void syNetRbSnapRestoreJungleGround(const SYNetRbSnapGroundJungle *src, u16 payload_len, u32 snap_tick);
static void syNetRbSnapEnsureJungleTaruCannAfterParticleReset(const SYNetRbSnapshotSlot *slot);
static void syNetRbSnapCaptureArwing(SYNetRbSnapshotSlot *slot);
static void syNetRbSnapApplyArwing(const SYNetRbSnapshotSlot *slot);
static void syNetRbSnapEnsureSectorArwingAfterParticleReset(const SYNetRbSnapshotSlot *slot);
static void syNetRbSnapEnsureYamabukiGateAfterParticleReset(const SYNetRbSnapshotSlot *slot);
static void syNetRbSnapHyruleTwisterLogApplyDrift(u32 tick, const GRCommonGroundVarsHyrule *hy,
						  const SYNetRbSnapGroundHyrule *src);
static void syNetRbSnapHyruleTwisterLogObstacleFail(u32 tick, u8 twister_status, const char *fail_reason);
#endif

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
			if ((dst->twister_gobj != NULL) && (syNetRbSnapHyruleGObjIsStageController(dst->twister_gobj) != FALSE))
			{
				dst->twister_gobj = NULL;
			}
			syNetRbSnapHyruleTwisterRebindMeshFromLink(dst, src);
			dst->twister_leftedge_x = src->twister_leftedge_x;
			dst->twister_rightedge_x = src->twister_rightedge_x;
			dst->twister_vel = src->twister_vel;
			dst->twister_wait = src->twister_wait;
			dst->twister_speed_wait = src->twister_speed_wait;
			dst->twister_turn_wait = src->twister_turn_wait;
			dst->twister_line_id = src->twister_line_id;
			dst->twister_status = src->twister_status;
			dst->twister_pos_count = src->twister_pos_count;
#if defined(SSB64_NETMENU)
			dst->twister_leftedge_x = syNetplayQuantizeF32(dst->twister_leftedge_x);
			dst->twister_rightedge_x = syNetplayQuantizeF32(dst->twister_rightedge_x);
			dst->twister_vel = syNetplayQuantizeF32(dst->twister_vel);
#endif
			syNetRbSnapHyruleTwisterNormalizeFromBlob(dst, src);
			if ((dst->twister_status == (u8)nGRHyruleTwisterStatusSleep) ||
			    (dst->twister_status == (u8)nGRHyruleTwisterStatusWait))
			{
				if (syNetRbSnapIsValidHyruleTwisterGObj(dst->twister_gobj) == FALSE)
				{
					syNetRbSnapHyruleTwisterClearGObj(dst);
				}
			}
			else if (dst->twister_status == (u8)nGRHyruleTwisterStatusSubside)
			{
				syNetRbSnapHyruleTwisterClearGObj(dst);
			}
			else if (syNetRbSnapIsValidHyruleTwisterGObj(dst->twister_gobj) == FALSE)
			{
				syNetRbSnapHyruleTwisterClearGObj(dst);
			}
			if ((dst->twister_gobj != NULL) &&
			    (syNetRbSnapHyruleTwisterStatusNeedsGObj(dst->twister_status) != FALSE))
			{
				Vec3f twister_pos = src->twister_pos;

#if defined(SSB64_NETMENU)
				syNetplayQuantizeVec3f(&twister_pos);
#endif
				(void)grHyruleTwisterRestorePoseFromPos(dst->twister_gobj, &twister_pos);
			}
#ifdef PORT
			syNetRbSnapHyruleTwisterLogApplyDrift(slot->tick, dst, src);
#endif
		}
		break;
	case nGRKindJungle:
		if (ground->payload_len >= (u16)SYNETRB_SNAP_GROUND_JUNGLE_LEGACY_PAYLOAD_LEN)
		{
			const SYNetRbSnapGroundJungle *src = (const SYNetRbSnapGroundJungle *)ground->payload;

#ifdef PORT
			syNetRbSnapRestoreJungleGround(src, ground->payload_len, slot->tick);
#else
			GRCommonGroundVarsJungle *dst = &gGRCommonStruct.jungle;

			dst->tarucann_gobj = (src->tarucann_gobj_id != 0U) ? gcFindGObjByID(src->tarucann_gobj_id) : NULL;
			dst->tarucann_status = src->tarucann_status;
			dst->tarucann_wait = src->tarucann_wait;
			dst->tarucann_rotate_step = src->tarucann_rotate_step;
#endif
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
#if defined(SSB64_NETMENU)
			dst->acid_level_curr = syNetplayQuantizeF32(dst->acid_level_curr);
			dst->acid_level_step = syNetplayQuantizeF32(dst->acid_level_step);
			if (dst->map_gobj != NULL)
			{
				DObjGetStruct(dst->map_gobj)->translate.vec.f.y = dst->acid_level_curr;
			}
#endif
		}
		break;
	case nGRKindYamabuki:
		if (ground->payload_len >= (u16)offsetof(SYNetRbSnapGroundYamabuki, gate_anim_frame))
		{
			const SYNetRbSnapGroundYamabuki *src = (const SYNetRbSnapGroundYamabuki *)ground->payload;
			GRCommonGroundVarsYamabuki *dst = &gGRCommonStruct.yamabuki;

			dst->monster_gobj = (src->monster_gobj_id != 0U) ? gcFindGObjByID(src->monster_gobj_id) : NULL;
#ifdef PORT
			/*
			 * gate_gobj_id is gobj->id == nGCCommonKindGround for ALL ground GObjs, so gcFindGObjByID
			 * can return the bare ground controller instead of the gate (static-closed-door bug).
			 * Re-derive from the live ground link by display proc; fall back to the id only if the
			 * gate GObj is not currently on the link.
			 */
			dst->gate_gobj = grYamabukiGateResolveLiveGObj();
			if ((dst->gate_gobj == NULL) && (src->gate_gobj_id != 0U))
			{
				dst->gate_gobj = gcFindGObjByID(src->gate_gobj_id);
			}
#else
			dst->gate_gobj = (src->gate_gobj_id != 0U) ? gcFindGObjByID(src->gate_gobj_id) : NULL;
#endif
			dst->gate_pos = src->gate_pos;
			dst->gate_status = src->gate_status;
			dst->gate_noentry = src->gate_noentry;
			dst->gate_anim_phase = src->gate_anim_phase;
			/* Open + pending rooftop monster: gate_noentry is re-derived after item apply
			 * (latched TRUE from blob deadlocks forward UpdateOpen on restore). */
			if ((src->gate_status == (u8)nGRYamabukiGateStatusOpen) && (src->monster_gobj_id != 0U))
			{
				dst->gate_noentry = FALSE;
			}
			else if ((src->gate_status == (u8)nGRYamabukiGateStatusWait) && (src->gate_pos.x >= 1280.0F))
			{
				dst->gate_noentry = FALSE;
			}
			dst->monster_wait = src->monster_wait;
			dst->gate_wait = src->gate_wait;
			dst->monster_id_prev = src->monster_id_prev;
#if defined(SSB64_NETMENU)
			syNetplayQuantizeVec3f(&dst->gate_pos);
#endif
			/* Anim/collision re-seat runs in syNetRbSnapEnsureYamabukiGateAfterParticleReset
			 * after particle reset + item apply (gate_gobj DObj tree may be hollow until then). */
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
		if ((ground->payload_len >= (u16)sizeof(SYNetRbSnapGroundYoster)) ||
		    (ground->payload_len >= SYNETRB_SNAP_GROUND_YOSTER_LEGACY_PAYLOAD_LEN))
		{
			GRCommonGroundVarsYoster *dst = &gGRCommonStruct.yoster;
			sb32 has_extended_blob;
			s32 ci;

			has_extended_blob = (ground->payload_len >= (u16)sizeof(SYNetRbSnapGroundYoster)) ? TRUE : FALSE;
			if ((has_extended_blob != FALSE) && (dst->map_head == NULL))
			{
				const SYNetRbSnapGroundYoster *src_full = (const SYNetRbSnapGroundYoster *)ground->payload;

				if (src_full->map_head != 0U)
				{
					dst->map_head = (void *)src_full->map_head;
				}
			}
			for (ci = 0; ci < 3; ci++)
			{
				u32 gobj_id;
				ub8 is_cloud_line_active;
				GRYosterCloud *lc = &dst->clouds[ci];

				if (has_extended_blob != FALSE)
				{
					const SYNetRbSnapGroundYoster *src_full = (const SYNetRbSnapGroundYoster *)ground->payload;
					const SYNetRbSnapGroundYosterCloud *sc = &src_full->clouds[ci];

					gobj_id = sc->gobj_id;
					lc->altitude = sc->altitude;
					lc->pressure = sc->pressure;
					lc->status = sc->status;
					lc->anim_id = sc->anim_id;
					is_cloud_line_active = sc->is_cloud_line_active;
					lc->pressure_timer = sc->pressure_timer;
					lc->evaporate_wait = sc->evaporate_wait;
				}
				else
				{
					const SYNetRbSnapGroundYosterCloudLegacy *sc =
					    &((const SYNetRbSnapGroundYosterCloudLegacy *)ground->payload)[ci];

					gobj_id = sc->gobj_id;
					lc->altitude = sc->altitude;
					lc->pressure = sc->pressure;
					lc->status = sc->status;
					lc->anim_id = sc->anim_id;
					is_cloud_line_active = sc->is_cloud_line_active;
					lc->pressure_timer = sc->pressure_timer;
					lc->evaporate_wait = sc->evaporate_wait;
				}
				/*
				 * All three cloud GObjs share gobj->id == nGCCommonKindGround, so
				 * gcFindGObjByID(gobj_id) collapses clouds[0/1/2].gobj onto the first
				 * ground GObj. Resolve by slot from the init-time table instead (vanilla
				 * identifies clouds purely by array index). gobj_id is kept in the blob
				 * only for diagnostics/back-compat.
				 */
				(void)gobj_id;
				lc->gobj = grYosterGetCloudGobj(ci);
				lc->is_cloud_line_active = is_cloud_line_active;
				if (has_extended_blob != FALSE)
				{
					const SYNetRbSnapGroundYoster *src_full = (const SYNetRbSnapGroundYoster *)ground->payload;

					syNetRbSnapRestoreYosterCloudPresentation(ci, &src_full->clouds[ci], lc);
				}
				else
				{
					syNetRbSnapRestoreYosterCloudPresentation(ci, NULL, lc);
				}
			}
		}
		break;
	case nGRKindSector:
		if (ground->payload_len >= SYNETRB_SNAP_GROUND_SECTOR_V1_PAYLOAD_LEN)
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
			if (ground->payload_len >= (u16)sizeof(SYNetRbSnapGroundSector))
			{
				dst->unk_sector_0x4C = src->unk_sector_0x4C;
				dst->unk_sector_0x4D = src->unk_sector_0x4D;
				dst->unk_sector_0x4E = src->unk_sector_0x4E;
				dst->unk_sector_0x52 = src->unk_sector_0x52;
			}
			if (ground->payload_len >= SYNETRB_SNAP_GROUND_SECTOR_V2_PAYLOAD_LEN)
			{
				dst->arwing_last_flight_pattern = src->arwing_last_flight_pattern;
			}
			else
			{
				dst->arwing_last_flight_pattern = -1;
			}
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

			/* Re-derive from the live item list rather than trusting the
			 * captured gobj id (reused ids can alias an unrelated item). */
			dst->bumper_gobj = syNetRbSnapResolveCastleBumperGObj(src);
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

u32 syNetRbSnapshotComputeMapHashLive(void)
{
	SYNetRbSnapshotSlot scratch;

	syNetRbSnapshotPrepareMapStateForHash();
	memset(&scratch, 0, sizeof(scratch));
	syNetRbSnapCaptureGround(&scratch);
	if (scratch.ground_captured == FALSE)
	{
		return syNetRbSnapshotComputeMapHashWithGround(NULL);
	}
	return syNetRbSnapshotComputeMapHashWithGround(&scratch.ground);
}

static sb32 syNetRbSnapMapHashDiagEnabled(void)
{
	static int s_env_cache = -999;
	const char *env;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	env = getenv("SSB64_NETPLAY_SNAPSHOT_MAP_HASH_DIAG");
	s_env_cache = (env != NULL && env[0] != '\0' && atoi(env) != 0) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}

static u32 syNetRbSnapMapHashCombineKinGround(u32 kin, const SYNetRbSnapGroundBlob *ground, u32 ground_fold)
{
	u32 hash = kin;

	if ((ground != NULL) && (ground->payload_len > 0U))
	{
		hash = syNetRbSnapFnvAccumulateU32(hash ^ ground_fold, 0x47524F55U);
	}
	return hash;
}

static s32 syNetRbSnapGroundPayloadFirstDiff(const SYNetRbSnapGroundBlob *a, const SYNetRbSnapGroundBlob *b)
{
	u32 n;
	u32 i;

	if ((a == NULL) || (b == NULL))
	{
		return -1;
	}
	if (a->gkind != b->gkind)
	{
		return 0;
	}
	n = (u32)a->payload_len;
	if ((u32)b->payload_len < n)
	{
		n = (u32)b->payload_len;
	}
	for (i = 0; i < n; i++)
	{
		if (a->payload[i] != b->payload[i])
		{
			return (s32)i;
		}
	}
	if (a->payload_len != b->payload_len)
	{
		return (s32)n;
	}
	return -1;
}

static void syNetRbSnapLogMapHashSectorGroundDiff(u32 tick, const SYNetRbSnapGroundSector *slot_sec,
						  const SYNetRbSnapGroundSector *live_sec)
{
	if ((slot_sec == NULL) || (live_sec == NULL))
	{
		return;
	}
	if (slot_sec->is_arwing_z_near != live_sec->is_arwing_z_near)
	{
		port_log(
		    "SSB64 NetRbSnapshot: map_hash_sector_field tick=%u field=is_arwing_z_near slot=%u live=%u\n",
		    (unsigned int)tick, (unsigned int)slot_sec->is_arwing_z_near, (unsigned int)live_sec->is_arwing_z_near);
	}
	if (slot_sec->is_arwing_line_active != live_sec->is_arwing_line_active)
	{
		port_log(
		    "SSB64 NetRbSnapshot: map_hash_sector_field tick=%u field=is_arwing_line_active slot=%u live=%u\n",
		    (unsigned int)tick, (unsigned int)slot_sec->is_arwing_line_active,
		    (unsigned int)live_sec->is_arwing_line_active);
	}
	if (slot_sec->is_arwing_z_collision != live_sec->is_arwing_z_collision)
	{
		port_log(
		    "SSB64 NetRbSnapshot: map_hash_sector_field tick=%u field=is_arwing_z_collision slot=%u live=%u\n",
		    (unsigned int)tick, (unsigned int)slot_sec->is_arwing_z_collision,
		    (unsigned int)live_sec->is_arwing_z_collision);
	}
	if (slot_sec->is_arwing_line_collision != live_sec->is_arwing_line_collision)
	{
		port_log(
		    "SSB64 NetRbSnapshot: map_hash_sector_field tick=%u field=is_arwing_line_collision slot=%u live=%u\n",
		    (unsigned int)tick, (unsigned int)slot_sec->is_arwing_line_collision,
		    (unsigned int)live_sec->is_arwing_line_collision);
	}
	if (slot_sec->arwing_target_x != live_sec->arwing_target_x)
	{
		port_log(
		    "SSB64 NetRbSnapshot: map_hash_sector_field tick=%u field=arwing_target_x slot=%f live=%f\n",
		    (unsigned int)tick, (double)slot_sec->arwing_target_x, (double)live_sec->arwing_target_x);
	}
	if (slot_sec->arwing_status != live_sec->arwing_status)
	{
		port_log(
		    "SSB64 NetRbSnapshot: map_hash_sector_field tick=%u field=arwing_status slot=%u live=%u\n",
		    (unsigned int)tick, (unsigned int)slot_sec->arwing_status, (unsigned int)live_sec->arwing_status);
	}
}

void syNetRbSnapshotLogMapHashDriftDiag(u32 tick)
{
	SYNetRbSnapshotSlot *slot;
	SYNetRbSnapshotSlot scratch;
	u32 kin;
	u32 ground_fold_slot;
	u32 ground_fold_scratch;
	u32 hash_slot_formula;
	u32 hash_scratch_formula;
	u32 hash_live;
	s32 payload_first_diff;

	if (syNetRbSnapMapHashDiagEnabled() == FALSE)
	{
		return;
	}
	slot = syNetRbSnapshotSlotForTick(tick);
	if ((slot == NULL) || (slot->is_valid == FALSE) || (slot->tick != tick))
	{
		return;
	}
	syNetRbSnapshotPrepareMapStateForHash();
	kin = syNetSyncHashMapCollisionKinematicsForRollback();
	ground_fold_slot = (slot->ground_captured != FALSE)
	                       ? syNetRbSnapFoldGroundPayloadHashForRollback(&slot->ground)
	                       : 2166136261U;
	memset(&scratch, 0, sizeof(scratch));
	syNetRbSnapCaptureGround(&scratch);
	ground_fold_scratch = (scratch.ground_captured != FALSE)
	                          ? syNetRbSnapFoldGroundPayloadHashForRollback(&scratch.ground)
	                          : 2166136261U;
	hash_slot_formula =
	    syNetRbSnapMapHashCombineKinGround(kin, (slot->ground_captured != FALSE) ? &slot->ground : NULL,
	                                       ground_fold_slot);
	hash_scratch_formula = syNetRbSnapMapHashCombineKinGround(
	    kin, (scratch.ground_captured != FALSE) ? &scratch.ground : NULL, ground_fold_scratch);
	hash_live = (scratch.ground_captured == FALSE) ? syNetRbSnapshotComputeMapHashWithGround(NULL)
	                                               : syNetRbSnapshotComputeMapHashWithGround(&scratch.ground);
	port_log(
	    "SSB64 NetRbSnapshot: map_hash_drift tick=%u slot_stored=0x%08X live_full=0x%08X kin=0x%08X "
	    "ground_fold_slot=0x%08X ground_fold_scratch=0x%08X hash_kin_plus_slot_ground=0x%08X "
	    "hash_kin_plus_scratch_ground=0x%08X\n",
	    (unsigned int)tick, slot->hash_map, hash_live, kin, ground_fold_slot, ground_fold_scratch,
	    hash_slot_formula, hash_scratch_formula);
	if ((slot->ground_captured != FALSE) && (scratch.ground_captured != FALSE))
	{
		payload_first_diff = syNetRbSnapGroundPayloadFirstDiff(&slot->ground, &scratch.ground);
		port_log(
		    "SSB64 NetRbSnapshot: map_hash_ground_payload tick=%u gkind=%u slot_len=%u scratch_len=%u "
		    "match=%d first_off=%d\n",
		    (unsigned int)tick, (unsigned int)slot->ground.gkind, (unsigned int)slot->ground.payload_len,
		    (unsigned int)scratch.ground.payload_len, (payload_first_diff < 0) ? 1 : 0,
		    (int)payload_first_diff);
		if ((slot->ground.gkind == nGRKindSector) &&
		    (slot->ground.payload_len >= (u16)sizeof(SYNetRbSnapGroundSector)) &&
		    (scratch.ground.payload_len >= (u16)sizeof(SYNetRbSnapGroundSector)))
		{
			syNetRbSnapLogMapHashSectorGroundDiff(
			    tick, (const SYNetRbSnapGroundSector *)slot->ground.payload,
			    (const SYNetRbSnapGroundSector *)scratch.ground.payload);
		}
	}
	if ((slot->mp_yaku_captured != FALSE) && (slot->mp_yakumono_count > 1) &&
	    (gMPCollisionYakumonoDObjs != NULL) && (gMPCollisionSpeeds != NULL))
	{
		DObj *yaku_dobj = gMPCollisionYakumonoDObjs->dobjs[1];

		if (yaku_dobj != NULL)
		{
			const SYNetRbSnapYakuBlob *blob = &slot->mp_yaku[1];

			port_log(
			    "SSB64 NetRbSnapshot: map_hash_yaku1 tick=%u live_tx=%f ty=%f tz=%f live_sx=%f sy=%f sz=%f "
			    "blob_tx=%f ty=%f tz=%f blob_sx=%f sy=%f sz=%f user_live=%d user_blob=%d mp_tic_live=%u "
			    "mp_tic_slot=%u\n",
			    (unsigned int)tick, (double)yaku_dobj->translate.vec.f.x, (double)yaku_dobj->translate.vec.f.y,
			    (double)yaku_dobj->translate.vec.f.z, (double)gMPCollisionSpeeds[1].x,
			    (double)gMPCollisionSpeeds[1].y, (double)gMPCollisionSpeeds[1].z, (double)blob->translate.x,
			    (double)blob->translate.y, (double)blob->translate.z, (double)blob->speed.x,
			    (double)blob->speed.y, (double)blob->speed.z, (int)yaku_dobj->user_data.s,
			    (int)blob->user_data_s, (unsigned int)gMPCollisionUpdateTic,
			    (unsigned int)slot->mp_collision_tic);
		}
	}
	if ((gSCManagerBattleState != NULL) && (gSCManagerBattleState->gkind == nGRKindSector) &&
	    (slot->arwing.captured != FALSE))
	{
		GRCommonGroundVarsSector *sec = &gGRCommonStruct.sector;
		DObj *d0 = (sec->map_dobjs != NULL) ? sec->map_dobjs[0] : NULL;

		if (d0 != NULL)
		{
			const Vec3f *blob_t = &slot->arwing.dobj_translate[0];

			port_log(
			    "SSB64 NetRbSnapshot: map_hash_arwing_d0 tick=%u live_frame=%f wait=%f tx=%f ty=%f "
			    "blob_tx=%f ty=%f status=%u deck_derived=%d\n",
			    (unsigned int)tick, (double)d0->anim_frame, (double)d0->anim_wait,
			    (double)d0->translate.vec.f.x, (double)d0->translate.vec.f.y, (double)blob_t->x,
			    (double)blob_t->y, (unsigned int)sec->arwing_status,
			    (int)syNetRbSnapSectorArwingDeckYakumonoDerivedFromSlot(slot));
		}
	}
}

void syNetRbSnapshotLogMapHashSaveSelfTest(u32 tick)
{
	SYNetRbSnapshotSlot *slot;
	u32 immediate;

	if (syNetRbSnapMapHashDiagEnabled() == FALSE)
	{
		return;
	}
	slot = syNetRbSnapshotSlotForTick(tick);
	if ((slot == NULL) || (slot->is_valid == FALSE) || (slot->tick != tick))
	{
		return;
	}
	immediate = syNetRbSnapshotComputeMapHashLive();
	if (immediate == slot->hash_map)
	{
		return;
	}
	port_log(
	    "SSB64 NetRbSnapshot: map_hash_save_self_test FAIL tick=%u stored=0x%08X immediate=0x%08X\n",
	    (unsigned int)tick, slot->hash_map, immediate);
	syNetRbSnapshotLogMapHashDriftDiag(tick);
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

static sb32 syNetRbSnapBlobInGuardScope(const SYNetRbSnapFighterBlob *blob)
{
	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return FALSE;
	}
	if (blob->is_shield != FALSE)
	{
		return TRUE;
	}
	if ((blob->status_id >= nFTCommonStatusGuardStart) && (blob->status_id <= nFTCommonStatusGuardEnd))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapFighterInGuardScope(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	if (fp->is_shield != FALSE)
	{
		return TRUE;
	}
	if ((fp->status_id >= nFTCommonStatusGuardStart) && (fp->status_id <= nFTCommonStatusGuardEnd))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapFighterGuardEffectUnionOwned(const FTStruct *fp)
{
	if (syNetRbSnapFighterInGuardScope(fp) == FALSE)
	{
		return FALSE;
	}
	/*
	 * guard.effect_gobj aliases twister.tornado_gobj and tarucann.tarucann_gobj at union offset 0x08.
	 */
	if ((fp->status_id == nFTCommonStatusTwister) || (fp->status_id == nFTCommonStatusTaruCann))
	{
		return FALSE;
	}
	return TRUE;
}

static u32 syNetRbSnapGuardShieldDiagTick(const SYNetRbSnapshotSlot *slot)
{
	if ((slot != NULL) && (slot->is_valid != FALSE) && (slot->tick != ~(u32)0))
	{
		return slot->tick;
	}
#if defined(PORT) && defined(SSB64_NETMENU)
	return syNetInputGetTick();
#else
	return 0U;
#endif
}

static sb32 syNetRbSnapGuardShieldLiveForwardPolicy(const SYNetRbSnapshotSlot *slot)
{
	/* NULL slot = live forward reconcile after battle update; non-NULL = snapshot apply / verify. */
	return (slot == NULL) ? TRUE : FALSE;
}

static sb32 syNetRbSnapFighterShieldInputHeld(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	return ((fp->input.pl.button_hold & fp->input.button_mask_z) != 0) ? TRUE : FALSE;
}

static sb32 syNetRbSnapFighterShieldInputHeldAuthoritative(const FTStruct *fp)
{
	/*
	 * Phase 34 correction (2026-06-07): the shield-hold predicate returns the fighter's APPLIED input
	 * (z_live = fp->input.button_hold & mask_z) — nothing else.
	 *
	 * History / why the indirection is gone:
	 *   - Phase 13/17 read a separate "authoritative" copy of the button from the published/sealed input
	 *     stream at syNetInputGetTick(), intending cross-peer symmetry for the Z-not-held release.
	 *   - That stream lookup is offset from the input actually applied to the fighter by the input-delay
	 *     buffer (the sim-tick counter advances inside the battle update before this live-forward reconcile
	 *     runs), so it is timing-dependent garbage in BOTH directions:
	 *       * false-negative on a sustained hold  -> reads 0 (not held) while z_live=1, force-releases the
	 *         shield -> GuardOn<->GuardOff spam (observed: 505x "z_auth=0 z_live=1" lines, status 152<->154
	 *         oscillation under a continuous R hold).
	 *       * false-positive at a release edge during fast tap-spam -> reads a stale 1 while z_live=0, masks
	 *         the release so is_shield never clears -> the next R press re-enters shield instead of opening
	 *         a clean grab window (the original "hold R to grab" regression).
	 *   - Phase 32 OR-folded z_live back in to mask the false-negative; Phase 34's first attempt swapped the
	 *     lookup to syNetInputGetPublishedFrame (== last_published) assuming it equalled the applied input —
	 *     it does NOT, it carries the same input-delay offset, so it reproduced the false-negative spam.
	 *
	 * The applied input is the only edge-faithful source: it reads held exactly while the button is held
	 * and released exactly when it is released. It is also the synced sim input (deterministic, identical
	 * on both peers — figh matches; transient prediction-edge drift is resim-corrected, per Phase 29), and
	 * during resim fp->input is the replayed sealed input, so z_live == the sealed authoritative value
	 * there too. is_release is not hashed, so feeding the release/teardown path from z_live cannot diverge
	 * figh. This matches vanilla ftCommonGuardCheckScheduleRelease (ftcommonguard1.c:30), which already
	 * owns is_release from this same applied button.
	 */
	return syNetRbSnapFighterShieldInputHeld(fp);
}

static sb32 syNetRbSnapLiveShieldEffectOwnedByFighter(const EFStruct *ep, const GObj *fighter_gobj, s32 player)
{
	GObj *owner_gobj;

	if (ep == NULL)
	{
		return FALSE;
	}
	if ((fighter_gobj != NULL) && (ep->fighter_gobj == fighter_gobj))
	{
		return TRUE;
	}
	/*
	 * shield.player is authoritative when ep->fighter_gobj is NULL or decoupled after reconcile;
	 * do not reject player-slot match solely because ep->fighter_gobj points elsewhere.
	 */
	if ((player >= 0) && ((u32)ep->effect_vars.shield.player == (u32)player))
	{
		owner_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)player);
		return ((owner_gobj != NULL) && (owner_gobj == fighter_gobj)) ? TRUE : FALSE;
	}
	return FALSE;
}

static sb32 syNetRbSnapYoshiEggLayVictimOwnsEffect(const FTStruct *fp, const EFStruct *ep)
{
	GObj *owner_gobj;

	if ((fp == NULL) || (ep == NULL) || (fp->status_id != nFTCommonStatusYoshiEgg))
	{
		return FALSE;
	}
	if (ep->fighter_gobj == NULL)
	{
		return FALSE;
	}
	owner_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)fp->player);
	return ((owner_gobj != NULL) && (ep->fighter_gobj == owner_gobj)) ? TRUE : FALSE;
}

static s32 syNetRbSnapCountLiveShieldEffectsForPlayer(s32 player)
{
	s32 count;
	s32 pass;

	if (player < 0)
	{
		return 0;
	}
	count = 0;
	for (pass = 0; pass < 2; pass++)
	{
		GObj *gobj;

		for (gobj = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		     gobj != NULL; gobj = gobj->link_next)
		{
			EFStruct *ep = efGetStruct(gobj);

			if (syNetRbSnapLiveEffectIsShield(gobj, ep) == FALSE)
			{
				continue;
			}
			if ((u32)ep->effect_vars.shield.player == (u32)player)
			{
				count++;
				continue;
			}
			if ((ep->fighter_gobj != NULL) && (ftGetStruct(ep->fighter_gobj) != NULL) &&
			    (ftGetStruct(ep->fighter_gobj)->player == player))
			{
				count++;
			}
		}
	}
	return count;
}

static sb32 syNetRbSnapFighterInActiveGuardStatus(const FTStruct *fp)
{
	if (fp == NULL)
	{
		return FALSE;
	}
	return ((fp->status_id >= nFTCommonStatusGuardStart) && (fp->status_id <= nFTCommonStatusGuardOff)) ? TRUE
	                                                                                                  : FALSE;
}

static sb32 syNetRbSnapFighterVanillaShieldReleaseDefer(const FTStruct *fp)
{
	if ((fp == NULL) || (syNetRbSnapFighterGuardEffectUnionOwned(fp) == FALSE))
	{
		return FALSE;
	}
	/*
	 * Phase 25: match offline — bubble persists while release_lag drains; vanilla lag_end owns teardown.
	 * Wait never defers (vanilla already cleared shield; orphan eject is safe).
	 */
	if (fp->status_id == nFTCommonStatusWait)
	{
		return FALSE;
	}
	if ((ftStatusVarsGuard(fp)->is_release != FALSE) && (ftStatusVarsGuard(fp)->release_lag > 0))
	{
		return TRUE;
	}
	return FALSE;
}

static void syNetRbSnapFighterShieldScheduleAuthoritativeRelease(FTStruct *fp)
{
	if ((fp == NULL) || (syNetRbSnapFighterGuardEffectUnionOwned(fp) == FALSE))
	{
		return;
	}
	/*
	 * Phase 34: the "held" predicate reads the applied input at the correct (applied) tick
	 * (syNetRbSnapFighterShieldInputHeldAuthoritative), so this single check declines to schedule a
	 * release whenever the player is actually holding the shield button, and fires on the true release
	 * edge during fast tap-spam — no separate live gate or OR-fold needed here.
	 */
	if (syNetRbSnapFighterShieldInputHeldAuthoritative(fp) != FALSE)
	{
		return;
	}
	ftStatusVarsGuard(fp)->is_release = TRUE;
}

#if defined(SSB64_NETMENU)
static u8 s_guard_shield_z_auth_prev[MAXCONTROLLERS];
#endif

static void syNetRbSnapResetShieldReleaseScheduleState(void)
{
#if defined(SSB64_NETMENU)
	s32 player;

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		s_guard_shield_z_auth_prev[player] = 0U;
	}
#endif
}

static void syNetRbSnapApplyShieldReleaseScheduleLive(const SYNetRbSnapshotSlot *slot)
{
#if defined(SSB64_NETMENU)
	GObj *fighter_gobj;

	if (slot != NULL)
	{
		return;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		sb32 z_auth;
		sb32 z_falling;
		s32 player;

		fp = ftGetStruct(fighter_gobj);
		if ((fp == NULL) || (syNetRbSnapFighterGuardEffectUnionOwned(fp) == FALSE))
		{
			continue;
		}
		player = fp->player;
		if ((player < 0) || (player >= MAXCONTROLLERS))
		{
			continue;
		}
		z_auth = syNetRbSnapFighterShieldInputHeldAuthoritative(fp);
		z_falling = (z_auth == FALSE) && (s_guard_shield_z_auth_prev[player] != 0U);
		if (z_falling != FALSE)
		{
			if ((fp->is_shield != FALSE) || (syNetRbSnapFindLiveShieldEffectForFighter(fighter_gobj) != NULL))
			{
				/*
				 * Phase 29: sim-faithful release. Schedule is_release from authoritative Z-off so the bubble
				 * defers exactly as long as vanilla's pre-drained release_lag remainder — do NOT reset
				 * release_lag (Phase 28 reset diverged from offline drain and made re-shield feel snappy).
				 * Vanilla ftCommonGuardUpdateShieldVars owns the lag countdown and is_shield teardown.
				 */
				syNetRbSnapFighterShieldScheduleAuthoritativeRelease(fp);
			}
		}
		s_guard_shield_z_auth_prev[player] = (z_auth != FALSE) ? 1U : 0U;
	}
#endif
	(void)slot;
}

static sb32 syNetRbSnapFighterShieldReleasePending(const FTStruct *fp)
{
	if ((fp == NULL) || (fp->is_shield == FALSE) || (syNetRbSnapFighterInGuardScope(fp) == FALSE) ||
	    (syNetRbSnapFighterGuardEffectUnionOwned(fp) == FALSE))
	{
		return FALSE;
	}
	if (ftStatusVarsGuard(fp)->is_release != FALSE)
	{
		if (syNetRbSnapFighterVanillaShieldReleaseDefer(fp) != FALSE)
		{
			return FALSE;
		}
		/*
		 * Phase 26: auth Z held — do not prune-teardown on stale is_release from micro-gap re-schedule
		 * after lag_retap cleared the prior release (tick 471→479 class).
		 */
		if (syNetRbSnapFighterShieldInputHeldAuthoritative(fp) != FALSE)
		{
			return FALSE;
		}
		return TRUE;
	}
	/*
	 * During quick tap-off the fighter may leave the guard status overlay before release_lag drains;
	 * guard.is_release can be union-stomped while is_shield and the bubble remain. Input is authoritative.
	 * Teardown always clears is_shield so a one-frame Z-off prediction cannot leave guard-without-bubble.
	 * Live-forward only — snapshot load/verify must not consult live input (Phase 11).
	 * Phase 13: Z-not-held uses published/sealed history on both peers (symmetric eff hash).
	 * Phase 17: schedule is_release from authoritative Z-off, then defer only while lag drains.
	 */
	if (syNetRbSnapFighterShieldInputHeldAuthoritative(fp) == FALSE)
	{
		syNetRbSnapFighterShieldScheduleAuthoritativeRelease((FTStruct *) fp);
		if (syNetRbSnapFighterVanillaShieldReleaseDefer(fp) != FALSE)
		{
			return FALSE;
		}
		return TRUE;
	}
	return FALSE;
}

static void syNetRbSnapApplyYoshiShieldPresentation(GObj *fighter_gobj, FTStruct *fp)
{
	if ((fighter_gobj == NULL) || (fp == NULL) || (fp->fkind != nFTKindYoshi))
	{
		return;
	}
	if (fp->status_id == nFTCommonStatusGuardOn)
	{
		ftCommonGuardOnSetHitStatusYoshi(fighter_gobj);
	}
	else if ((fp->status_id >= nFTCommonStatusGuard) && (fp->status_id <= nFTCommonStatusGuardOff))
	{
		ftParamHideModelPartAll(fighter_gobj);
		ftCommonGuardSetHitStatusYoshi(fighter_gobj);
	}
}

static void syNetRbSnapTeardownYoshiShieldPresentation(GObj *fighter_gobj, FTStruct *fp, sb32 replay_egg_break)
{
	if ((fighter_gobj == NULL) || (fp == NULL) || (fp->fkind != nFTKindYoshi))
	{
		return;
	}
	ftParamResetModelPartAll(fighter_gobj);
	ftCommonGuardOffSetHitStatusYoshi(fighter_gobj);
	if ((replay_egg_break != FALSE) && (fp->is_effect_attach != FALSE) &&
	    (fp->joints[nFTPartsJointYRotN] != NULL))
	{
		Vec3f egg_effect_offset = { 0.0F, 0.0F, 0.0F };

		gmCollisionGetFighterPartsWorldPosition(fp->joints[nFTPartsJointYRotN], &egg_effect_offset);
		(void)efManagerEggBreakMakeEffect(&egg_effect_offset);
	}
}

static void syNetRbSnapCoupleYoshiShieldEffect(GObj *fighter_gobj, FTStruct *fp, GObj *effect_gobj)
{
	if ((fighter_gobj == NULL) || (fp == NULL) || (effect_gobj == NULL))
	{
		return;
	}
	syNetRbSnapAuditLiveShieldEffectOwner(effect_gobj, fp);
	syNetRbSnapApplyYoshiShieldPresentation(fighter_gobj, fp);
	ftStatusVarsGuard(fp)->effect_gobj = effect_gobj;
	fp->is_effect_attach = TRUE;
	fp->is_shield = TRUE;
}

static sb32 syNetRbSnapFighterShieldEnsureLiveEligible(FTStruct *fp, const char **skip_reason_out)
{
	if (skip_reason_out != NULL)
	{
		*skip_reason_out = NULL;
	}
	if ((fp == NULL) || (fp->is_shield == FALSE) || (syNetRbSnapFighterInGuardScope(fp) == FALSE) ||
	    (syNetRbSnapFighterGuardEffectUnionOwned(fp) == FALSE))
	{
		return FALSE;
	}
	if (fp->shield_health == 0)
	{
		if (skip_reason_out != NULL)
		{
			*skip_reason_out = "stamina_exhausted";
		}
		return FALSE;
	}
	/*
	 * Phase 29: spawn-repair is driven by the deterministic, already-synced fp->is_shield (required above)
	 * plus the active guard status window — both fold identically into fhash_full on every peer, so the
	 * bubble lifecycle matches offline vanilla and converges across peers. The bubble exists in vanilla
	 * throughout GuardStart..GuardOff while is_shield is set (including the release-lag defer), so no
	 * authoritative-input or is_release/release_lag gating is needed here; vanilla owns those transitions.
	 */
	if (syNetRbSnapFighterInActiveGuardStatus(fp) == FALSE)
	{
		if (skip_reason_out != NULL)
		{
			*skip_reason_out = "not_active_guard";
		}
		return FALSE;
	}
	return TRUE;
}

static GObj *syNetRbSnapTryEnsureLiveShieldEffectForFighter(GObj *fighter_gobj, const SYNetRbSnapshotSlot *slot)
{
	FTStruct *fp;
	GObj *effect_gobj;
	const char *ensure_skip_reason;
	const char *ensure_reason;

	if (fighter_gobj == NULL)
	{
		return NULL;
	}
	fp = ftGetStruct(fighter_gobj);
	if (syNetRbSnapFighterShieldEnsureLiveEligible(fp, &ensure_skip_reason) == FALSE)
	{
		if ((syNetRbSnapSnapshotEffectDiagEnabled() != FALSE) && (ensure_skip_reason != NULL) &&
		    (strcmp(ensure_skip_reason, "z_not_held") != 0))
		{
			port_log(
			    "SSB64 NetRbSnapshot: guard_shield_ensure_skip tick=%u player=%d status=%d "
			    "reason=%s shield_health=%d z_auth=%d is_release=%d release_lag=%d\n",
			    (unsigned int)syNetRbSnapGuardShieldDiagTick(slot), (int)fp->player, (int)fp->status_id,
			    ensure_skip_reason, (int)fp->shield_health,
			    (int)syNetRbSnapFighterShieldInputHeldAuthoritative(fp),
			    (int)ftStatusVarsGuard(fp)->is_release, (int)ftStatusVarsGuard(fp)->release_lag);
		}
		return NULL;
	}
	if (syNetRbSnapFindLiveShieldEffectForFighter(fighter_gobj) != NULL)
	{
		return NULL;
	}
	{
		GObj *guard_coupled;

		guard_coupled = syNetRbSnapResolveCoupledGobj(ftStatusVarsGuard(fp)->effect_gobj);
		if ((guard_coupled != NULL) &&
		    (syNetRbSnapLiveEffectIsShield(guard_coupled, efGetStruct(guard_coupled)) != FALSE))
		{
			EFStruct *guard_ep = efGetStruct(guard_coupled);

			if ((guard_ep != NULL) &&
			    (syNetRbSnapLiveShieldEffectOwnedByFighter(guard_ep, fighter_gobj, fp->player) != FALSE))
			{
				syNetRbSnapAuditLiveShieldEffectOwner(guard_coupled, fp);
				ftStatusVarsGuard(fp)->effect_gobj = guard_coupled;
				fp->is_effect_attach = TRUE;
				return guard_coupled;
			}
		}
	}
	ensure_reason = (fp->status_id == nFTCommonStatusGuardOn) ? "guard_on_missing" : "z_retap_recovery";
	if (fp->fkind == nFTKindYoshi)
	{
		effect_gobj = efManagerYoshiShieldMakeEffect(fighter_gobj);
	}
	else
	{
		effect_gobj = efManagerShieldMakeEffect(fighter_gobj);
	}
	if (effect_gobj == NULL)
	{
		if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
		{
			port_log(
			    "SSB64 NetRbSnapshot: guard_shield_ensure tick=%u player=%d status=%d path=fail "
			    "reason=spawn_fail shield_health=%d\n",
			    (unsigned int)syNetRbSnapGuardShieldDiagTick(slot), (int)fp->player, (int)fp->status_id,
			    (int)fp->shield_health);
		}
		return NULL;
	}
	if (fp->fkind == nFTKindYoshi)
	{
		syNetRbSnapCoupleYoshiShieldEffect(fighter_gobj, fp, effect_gobj);
	}
	else
	{
		syNetRbSnapAuditLiveShieldEffectOwner(effect_gobj, fp);
		ftStatusVarsGuard(fp)->effect_gobj = effect_gobj;
		fp->is_effect_attach = TRUE;
		fp->is_shield = TRUE;
	}
	if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
	{
		port_log(
		    "SSB64 NetRbSnapshot: guard_shield_ensure tick=%u player=%d status=%d path=ok "
		    "reason=%s effect_gobj_id=%u release_lag=%d is_release=%d\n",
		    (unsigned int)syNetRbSnapGuardShieldDiagTick(slot), (int)fp->player, (int)fp->status_id,
		    ensure_reason, (unsigned int)effect_gobj->id, (int)ftStatusVarsGuard(fp)->release_lag,
		    (int)ftStatusVarsGuard(fp)->is_release);
	}
	return effect_gobj;
}

static void syNetRbSnapEnsureLiveShieldEffectsOnAuthHold(const SYNetRbSnapshotSlot *slot)
{
	GObj *fighter_gobj;

	if (slot != NULL)
	{
		return;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		syNetRbSnapTryEnsureLiveShieldEffectForFighter(fighter_gobj, slot);
	}
}

static sb32 syNetRbSnapFighterShieldHealEligible(const FTStruct *fp, const char **skip_reason_out)
{
	if (skip_reason_out != NULL)
	{
		*skip_reason_out = NULL;
	}
	if ((fp == NULL) || (fp->is_shield == FALSE))
	{
		return FALSE;
	}
	/*
	 * Wait / release-complete GuardOff / GuardSetOff: stale is_shield with no bubble blocks grab even
	 * when Z/R is held. Heal before active-guard and z_held gates so spam recovery does not trap R→shield.
	 */
	if (fp->status_id == nFTCommonStatusWait)
	{
		return TRUE;
	}
	if (fp->status_id == nFTCommonStatusGuardSetOff)
	{
		return TRUE;
	}
	if ((fp->status_id == nFTCommonStatusGuardOff) && (ftStatusVarsGuard(fp)->release_lag == 0) &&
	    (ftStatusVarsGuard(fp)->is_release != FALSE))
	{
		return TRUE;
	}
	/*
	 * Vanilla owns release-lag teardown in GuardOn/Guard/GuardOff (152-154). Live-forward heal clears
	 * stale fp->is_shield only outside that window when the bubble is gone and Z/R is not held.
	 */
	if (syNetRbSnapFighterInActiveGuardStatus(fp) != FALSE)
	{
		if (skip_reason_out != NULL)
		{
			*skip_reason_out = "active_guard_window";
		}
		return FALSE;
	}
	if ((syNetRbSnapFighterShieldInputHeldAuthoritative(fp) != FALSE) ||
	    (syNetRbSnapFighterShieldInputHeld(fp) != FALSE))
	{
		if (skip_reason_out != NULL)
		{
			*skip_reason_out = "z_held";
		}
		return FALSE;
	}
	/*
	 * Special/attack exit after shield spam: lag_end never drained is_shield before status change.
	 */
	if ((fp->status_id > nFTCommonStatusGuardEnd) || (fp->status_id < nFTCommonStatusGuardStart))
	{
		return TRUE;
	}
	if (skip_reason_out != NULL)
	{
		*skip_reason_out = "in_guard_status";
	}
	return FALSE;
}

static void syNetRbSnapDiagGuardWaitShieldHeld(const SYNetRbSnapshotSlot *slot)
{
#if defined(SSB64_NETMENU)
	GObj *fighter_gobj;
	static sb32 s_stamina_logged[MAXCONTROLLERS];
	static sb32 s_reentry_logged[MAXCONTROLLERS];
	static u32 s_reentry_last_tick[MAXCONTROLLERS];

	if ((slot != NULL) || (syNetRbSnapSnapshotEffectDiagEnabled() == FALSE))
	{
		return;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		sb32 z_auth;
		sb32 z_live;
		s32 player;
		u32 tick;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			continue;
		}
		player = fp->player;
		if ((player < 0) || (player >= MAXCONTROLLERS))
		{
			continue;
		}
		if (fp->status_id != nFTCommonStatusWait)
		{
			s_stamina_logged[player] = FALSE;
			s_reentry_logged[player] = FALSE;
			continue;
		}
		z_auth = syNetRbSnapFighterShieldInputHeldAuthoritative(fp);
		z_live = syNetRbSnapFighterShieldInputHeld(fp);
		if ((z_auth == FALSE) && (z_live == FALSE))
		{
			s_stamina_logged[player] = FALSE;
			s_reentry_logged[player] = FALSE;
			continue;
		}
		tick = syNetRbSnapGuardShieldDiagTick(slot);
		if (fp->shield_health == 0)
		{
			if (s_stamina_logged[player] == FALSE)
			{
				port_log(
				    "SSB64 NetRbSnapshot: guard_wait_blocked tick=%u player=%d reason=stamina_exhausted "
				    "shield_health=0 z_auth=%d z_live=%d\n",
				    (unsigned int)tick, player, (int)z_auth, (int)z_live);
				s_stamina_logged[player] = TRUE;
			}
			continue;
		}
		if ((fp->is_shield != FALSE) || ((z_auth == FALSE) && (z_live == FALSE)))
		{
			continue;
		}
		if ((s_reentry_logged[player] == FALSE) || ((tick - s_reentry_last_tick[player]) >= 120U))
		{
			port_log(
			    "SSB64 NetRbSnapshot: guard_wait_blocked tick=%u player=%d reason=reentry_blocked "
			    "shield_health=%d z_auth=%d z_live=%d status=%d release_lag=%d is_release=%d\n",
			    (unsigned int)tick, player, (int)fp->shield_health, (int)z_auth, (int)z_live,
			    (int)fp->status_id, (int)ftStatusVarsGuard(fp)->release_lag,
			    (int)ftStatusVarsGuard(fp)->is_release);
			s_reentry_logged[player] = TRUE;
			s_reentry_last_tick[player] = tick;
		}
	}
#endif
}

static void syNetRbSnapFighterShieldReleaseTeardown(FTStruct *fp, GObj *shield_gobj)
{
	GObj *fighter_gobj;

	if (fp == NULL)
	{
		return;
	}
	fighter_gobj = NULL;
	if (shield_gobj != NULL)
	{
		EFStruct *ep;

		ep = efGetStruct(shield_gobj);
		if (ep != NULL)
		{
			fighter_gobj = ep->fighter_gobj;
		}
		syNetRbSnapClearFighterEffectPointerIfMatch(fp, shield_gobj);
	}
	if ((fp->fkind == nFTKindYoshi) && (fighter_gobj != NULL))
	{
		syNetRbSnapTeardownYoshiShieldPresentation(fighter_gobj, fp, TRUE);
	}
	ftStatusVarsGuard(fp)->effect_gobj = NULL;
	fp->is_effect_attach = FALSE;
	fp->is_shield = FALSE;
}

static u32 syNetRbSnapGuardEffectIdFromBlob(const SYNetRbSnapFighterBlob *blob)
{
	if (syNetRbSnapBlobInGuardScope(blob) == FALSE)
	{
		return 0U;
	}
	/*
	 * Yoshi egg shield exists only while is_shield is set (Guard onward). GuardOn without is_shield
	 * must not restore or respawn a bubble from a stale coupled id.
	 */
	if ((blob->fkind == nFTKindYoshi) && (blob->is_shield == FALSE))
	{
		return 0U;
	}
	return blob->guard_effect_gobj_id;
}

static sb32 syNetRbSnapBlobFoxInReflectorScope(const SYNetRbSnapFighterBlob *blob)
{
	if ((blob == NULL) || (blob->is_valid == FALSE) || (blob->fkind != nFTKindFox))
	{
		return FALSE;
	}
	if (((blob->status_id >= nFTFoxStatusSpecialLwScopeStart) && (blob->status_id <= nFTFoxStatusSpecialLwScopeEnd)) ||
	    ((blob->status_id >= nFTFoxStatusSpecialAirLwStart) && (blob->status_id <= nFTFoxStatusSpecialAirLwTurn)))
	{
		return TRUE;
	}
	return FALSE;
}

static u32 syNetRbSnapFoxSpecialLwEffectIdFromBlob(const SYNetRbSnapFighterBlob *blob)
{
	if (syNetRbSnapBlobFoxInReflectorScope(blob) == FALSE)
	{
		return 0U;
	}
	return blob->fox_speciallw_effect_gobj_id;
}

static GObj *syNetRbSnapFindShieldOwnerGobjFromSlot(const SYNetRbSnapshotSlot *slot, u32 effect_gobj_id)
{
	s32 pi;

	if ((slot == NULL) || (effect_gobj_id == 0U))
	{
		return NULL;
	}
	for (pi = 0; pi < GMCOMMON_PLAYERS_MAX; pi++)
	{
		const SYNetRbSnapFighterBlob *fb;
		GObj *fighter_gobj;

		fb = &slot->fighters[pi];
		if (fb->is_valid == FALSE)
		{
			continue;
		}
		if (syNetRbSnapGuardEffectIdFromBlob(fb) != effect_gobj_id)
		{
			continue;
		}
		fighter_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)pi);
		if ((fighter_gobj != NULL) && (ftGetStruct(fighter_gobj) != NULL))
		{
			return fighter_gobj;
		}
	}
	return NULL;
}

static GObj *syNetRbSnapFindYoshiEggLayOwnerGobjFromSlot(const SYNetRbSnapshotSlot *slot, u32 effect_gobj_id)
{
	s32 pi;

	if ((slot == NULL) || (effect_gobj_id == 0U))
	{
		return NULL;
	}
	for (pi = 0; pi < GMCOMMON_PLAYERS_MAX; pi++)
	{
		const SYNetRbSnapFighterBlob *fb;
		GObj *fighter_gobj;

		fb = &slot->fighters[pi];
		if (fb->is_valid == FALSE)
		{
			continue;
		}
		if (fb->status_id != nFTCommonStatusYoshiEgg)
		{
			continue;
		}
		if (fb->captureyoshi_effect_gobj_id != effect_gobj_id)
		{
			continue;
		}
		fighter_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)pi);
		if ((fighter_gobj != NULL) && (ftGetStruct(fighter_gobj) != NULL))
		{
			return fighter_gobj;
		}
	}
	return NULL;
}

static GObj *syNetRbSnapResolveYoshiEggLayParentGobj(const SYNetRbSnapshotSlot *slot,
                                                      const SYNetRbSnapEffectBlob *blob)
{
	GObj *fighter_gobj;

	if (blob == NULL)
	{
		return NULL;
	}
	if (slot != NULL)
	{
		fighter_gobj = syNetRbSnapFindYoshiEggLayOwnerGobjFromSlot(slot, blob->gobj_id);
		if (fighter_gobj != NULL)
		{
			return fighter_gobj;
		}
	}
	if (blob->fighter_gobj_id != 0U)
	{
		FTStruct *fp;

		fighter_gobj = gcFindGObjByID(blob->fighter_gobj_id);
		fp = (fighter_gobj != NULL) ? ftGetStruct(fighter_gobj) : NULL;
		if ((fp != NULL) && (fp->status_id == nFTCommonStatusYoshiEgg))
		{
			return fighter_gobj;
		}
	}
	return NULL;
}

static sb32 syNetRbSnapSlotGuardClaimsEffectId(const SYNetRbSnapshotSlot *slot, u32 effect_gobj_id)
{
	s32 pi;

	if ((slot == NULL) || (effect_gobj_id == 0U))
	{
		return FALSE;
	}
	for (pi = 0; pi < GMCOMMON_PLAYERS_MAX; pi++)
	{
		const SYNetRbSnapFighterBlob *fb;

		fb = &slot->fighters[pi];
		if ((fb->is_valid != FALSE) && (syNetRbSnapGuardEffectIdFromBlob(fb) == effect_gobj_id))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapEffectIdClaimedByGuard(const SYNetRbSnapshotSlot *slot, u32 effect_gobj_id)
{
	const SYNetRbSnapshotSlot *guard_slot;

	if (effect_gobj_id == 0U)
	{
		return FALSE;
	}
	if ((slot != NULL) && (syNetRbSnapSlotGuardClaimsEffectId(slot, effect_gobj_id) != FALSE))
	{
		return TRUE;
	}
	guard_slot = slot;
	if (guard_slot == NULL)
	{
		guard_slot = syNetRbSnapshotSlotForTick(syNetInputGetTick());
	}
	if ((guard_slot != NULL) && (guard_slot->is_valid != FALSE) &&
	    (syNetRbSnapSlotGuardClaimsEffectId(guard_slot, effect_gobj_id) != FALSE))
	{
		return TRUE;
	}
	return syNetRbSnapLiveGuardClaimsEffectId(effect_gobj_id);
}

static s32 syNetRbSnapShieldPlayerFromEffectVars(const EFStruct *ep)
{
	if (ep == NULL)
	{
		return -1;
	}
	if ((ep->effect_vars.shield.player < 0) || (ep->effect_vars.shield.player >= GMCOMMON_PLAYERS_MAX))
	{
		return -1;
	}
	return (s32)ep->effect_vars.shield.player;
}

static s32 syNetRbSnapShieldPlayerFromEffectBlob(const SYNetRbSnapEffectBlob *blob)
{
	EFStruct scratch;

	if (blob == NULL)
	{
		return -1;
	}
	memset(&scratch, 0, sizeof(scratch));
	memcpy(&scratch.effect_vars, blob->effect_vars, sizeof(scratch.effect_vars));
	return syNetRbSnapShieldPlayerFromEffectVars(&scratch);
}

/* Fighter GObjs share kind id nGCCommonKindFighter (1000); shield parent must restore by sim slot. */
static GObj *syNetRbSnapResolveShieldParentFromEffectBlob(const SYNetRbSnapEffectBlob *blob)
{
	s32 shield_player;
	GObj *fighter_gobj;
	FTStruct *fp;

	if (blob == NULL)
	{
		return NULL;
	}
	shield_player = syNetRbSnapShieldPlayerFromEffectBlob(blob);
	if (shield_player < 0)
	{
		return NULL;
	}
	fighter_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)shield_player);
	if ((fighter_gobj == NULL) || (ftGetStruct(fighter_gobj) == NULL))
	{
		return NULL;
	}
	fp = ftGetStruct(fighter_gobj);
	if (syNetRbSnapFighterInGuardScope(fp) == FALSE)
	{
		return NULL;
	}
	return fighter_gobj;
}

static void syNetRbSnapAuditLiveShieldEffectOwner(GObj *shield_gobj, const FTStruct *fp)
{
	EFStruct *ep;
	GObj *owner_gobj;

	if ((shield_gobj == NULL) || (fp == NULL) || (fp->player < 0) || (fp->player >= GMCOMMON_PLAYERS_MAX))
	{
		return;
	}
	ep = efGetStruct(shield_gobj);
	if (ep == NULL)
	{
		return;
	}
	owner_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)fp->player);
	if (owner_gobj == NULL)
	{
		return;
	}
	ep->effect_vars.shield.player = fp->player;
	ep->fighter_gobj = owner_gobj;
}

static void syNetRbSnapReconcileLiveShieldEffectOwners(void)
{
	s32 pass;

	for (pass = 0; pass < 2; pass++)
	{
		GObj *gobj;

		for (gobj = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		     gobj != NULL; gobj = gobj->link_next)
		{
			EFStruct *ep;
			s32 shield_player;
			GObj *owner_gobj;
			FTStruct *fp;

			ep = efGetStruct(gobj);
			if (syNetRbSnapLiveEffectIsShield(gobj, ep) == FALSE)
			{
				continue;
			}
			shield_player = syNetRbSnapShieldPlayerFromEffectVars(ep);
			if (shield_player < 0)
			{
				continue;
			}
			owner_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)shield_player);
			if ((owner_gobj == NULL) || (ftGetStruct(owner_gobj) == NULL))
			{
				continue;
			}
			if (ep->fighter_gobj != owner_gobj)
			{
				ep->fighter_gobj = owner_gobj;
			}
			fp = ftGetStruct(owner_gobj);
			if ((fp != NULL) && ((s32)fp->player != shield_player))
			{
				ep->effect_vars.shield.player = fp->player;
			}
		}
	}
}

static GObj *syNetRbSnapResolveShieldParentGobj(const SYNetRbSnapshotSlot *slot, const SYNetRbSnapEffectBlob *blob)
{
	GObj *fighter_gobj;

	if (blob == NULL)
	{
		return NULL;
	}
	if (slot != NULL)
	{
		fighter_gobj = syNetRbSnapFindShieldOwnerGobjFromSlot(slot, blob->gobj_id);
		if (fighter_gobj != NULL)
		{
			return fighter_gobj;
		}
	}
	fighter_gobj = syNetRbSnapResolveShieldParentFromEffectBlob(blob);
	if (fighter_gobj != NULL)
	{
		return fighter_gobj;
	}
	/*
	 * Do not fall back to gcFindGObjByID(blob->fighter_gobj_id): fighter GObjs share kind id 1000 and
	 * the stored id is ambiguous after rollback (same class as weapon/item owner bugs).
	 */
	return NULL;
}

static sb32 syNetRbSnapLiveEffectIsShield(const GObj *gobj, const EFStruct *ep)
{
	if ((gobj == NULL) || (ep == NULL) || (ep->proc_update != efManagerShieldProcUpdate))
	{
		return FALSE;
	}
	/*
	 * Particle hit VFX reuse EFStruct pool slots without updating proc_update; after shield spam the
	 * stale field can match efManagerShieldProcUpdate while DefaultProcUpdate is the live process.
	 */
	return syNetRbSnapEffectGObjHasUpdateProc(gobj, ep->proc_update);
}

static GObj *syNetRbSnapFindFoxReflectorOwnerGobjFromSlot(const SYNetRbSnapshotSlot *slot, u32 effect_gobj_id)
{
	s32 pi;

	if ((slot == NULL) || (effect_gobj_id == 0U))
	{
		return NULL;
	}
	for (pi = 0; pi < GMCOMMON_PLAYERS_MAX; pi++)
	{
		const SYNetRbSnapFighterBlob *fb;
		GObj *fighter_gobj;

		fb = &slot->fighters[pi];
		if (fb->is_valid == FALSE)
		{
			continue;
		}
		if (syNetRbSnapFoxSpecialLwEffectIdFromBlob(fb) != effect_gobj_id)
		{
			continue;
		}
		fighter_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)pi);
		if ((fighter_gobj != NULL) && (ftGetStruct(fighter_gobj) != NULL))
		{
			return fighter_gobj;
		}
	}
	return NULL;
}

static GObj *syNetRbSnapResolveFoxReflectorParentGobj(const SYNetRbSnapshotSlot *slot,
                                                      const SYNetRbSnapEffectBlob *blob)
{
	GObj *fighter_gobj;
	FTStruct *fp;

	if (blob == NULL)
	{
		return NULL;
	}
	if (slot != NULL)
	{
		fighter_gobj = syNetRbSnapFindFoxReflectorOwnerGobjFromSlot(slot, blob->gobj_id);
		if (fighter_gobj != NULL)
		{
			return fighter_gobj;
		}
	}
	if (blob->fighter_gobj_id != 0U)
	{
		fighter_gobj = gcFindGObjByID(blob->fighter_gobj_id);
		if ((fighter_gobj != NULL) && (ftGetStruct(fighter_gobj) != NULL))
		{
			fp = ftGetStruct(fighter_gobj);
			if ((fp->fkind == nFTKindFox) && (syNetRbSnapFighterInFoxReflectorScope(fp) != FALSE))
			{
				return fighter_gobj;
			}
		}
	}
	return NULL;
}

static sb32 syNetRbSnapLiveEffectIsFoxReflector(const GObj *gobj, const EFStruct *ep)
{
	return ((gobj != NULL) && (ep != NULL) && (ep->proc_update == efManagerFoxReflectorProcUpdate)) ? TRUE : FALSE;
}

static GObj *syNetRbSnapTryRespawnEffectFromBlob(const SYNetRbSnapshotSlot *slot, const SYNetRbSnapEffectBlob *blob)
{
	GObj *fighter_gobj;
	GObj *effect_gobj;

	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return NULL;
	}
	fighter_gobj = NULL;
	if (blob->respawn_kind == SYNETRB_EFFECT_RESPAWN_FOX_REFLECTOR)
	{
		fighter_gobj = syNetRbSnapResolveFoxReflectorParentGobj(slot, blob);
	}
	else if ((blob->respawn_kind == SYNETRB_EFFECT_RESPAWN_SHIELD) ||
	         (blob->respawn_kind == SYNETRB_EFFECT_RESPAWN_YOSHI_SHIELD))
	{
		fighter_gobj = syNetRbSnapResolveShieldParentGobj(slot, blob);
	}
	else if (blob->respawn_kind == SYNETRB_EFFECT_RESPAWN_YOSHI_EGG_LAY)
	{
		fighter_gobj = syNetRbSnapResolveYoshiEggLayParentGobj(slot, blob);
	}
	else if (blob->fighter_gobj_id != 0U)
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
			if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 NetRbSnapshot: effect_respawn kind=SHIELD blob_gobj_id=%u fighter_gobj_id=%u "
				    "resolved_parent=%u\n",
				    blob->gobj_id, blob->fighter_gobj_id, (unsigned int)fighter_gobj->id);
			}
			effect_gobj = efManagerShieldMakeEffect(fighter_gobj);
			if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
			{
				port_log("SSB64 NetRbSnapshot: effect_respawn kind=SHIELD result=%s new_gobj_id=%u\n",
				         (effect_gobj != NULL) ? "ok" : "fail",
				         (effect_gobj != NULL) ? syNetRbSnapGobjId(effect_gobj) : 0U);
			}
			return effect_gobj;
		}
		if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
		{
			port_log(
			    "SSB64 NetRbSnapshot: effect_respawn kind=SHIELD result=fail reason=no_fighter blob_gobj_id=%u "
			    "fighter_gobj_id=%u\n",
			    blob->gobj_id, blob->fighter_gobj_id);
		}
		break;
	case SYNETRB_EFFECT_RESPAWN_YOSHI_SHIELD:
		if (fighter_gobj != NULL)
		{
			if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 NetRbSnapshot: effect_respawn kind=YOSHI_SHIELD blob_gobj_id=%u fighter_gobj_id=%u "
				    "resolved_parent=%u\n",
				    blob->gobj_id, blob->fighter_gobj_id, (unsigned int)fighter_gobj->id);
			}
			effect_gobj = efManagerYoshiShieldMakeEffect(fighter_gobj);
			if (effect_gobj != NULL)
			{
				FTStruct *fp_yoshi;

				fp_yoshi = ftGetStruct(fighter_gobj);
				if (fp_yoshi != NULL)
				{
					syNetRbSnapCoupleYoshiShieldEffect(fighter_gobj, fp_yoshi, effect_gobj);
				}
			}
			if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
			{
				port_log("SSB64 NetRbSnapshot: effect_respawn kind=YOSHI_SHIELD result=%s new_gobj_id=%u\n",
				         (effect_gobj != NULL) ? "ok" : "fail",
				         (effect_gobj != NULL) ? syNetRbSnapGobjId(effect_gobj) : 0U);
			}
			return effect_gobj;
		}
		if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
		{
			port_log(
			    "SSB64 NetRbSnapshot: effect_respawn kind=YOSHI_SHIELD result=fail reason=no_fighter "
			    "blob_gobj_id=%u fighter_gobj_id=%u\n",
			    blob->gobj_id, blob->fighter_gobj_id);
		}
		break;
	case SYNETRB_EFFECT_RESPAWN_FOX_REFLECTOR:
		if (fighter_gobj != NULL)
		{
			if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 NetRbSnapshot: effect_respawn kind=FOX_REFLECTOR blob_gobj_id=%u fighter_gobj_id=%u "
				    "resolved_parent=%u\n",
				    blob->gobj_id, blob->fighter_gobj_id, (unsigned int)fighter_gobj->id);
			}
			effect_gobj = efManagerFoxReflectorMakeEffect(fighter_gobj);
			if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
			{
				port_log("SSB64 NetRbSnapshot: effect_respawn kind=FOX_REFLECTOR result=%s new_gobj_id=%u\n",
				         (effect_gobj != NULL) ? "ok" : "fail",
				         (effect_gobj != NULL) ? syNetRbSnapGobjId(effect_gobj) : 0U);
			}
			return effect_gobj;
		}
		if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
		{
			port_log(
			    "SSB64 NetRbSnapshot: effect_respawn kind=FOX_REFLECTOR result=fail reason=no_fighter blob_gobj_id=%u "
			    "fighter_gobj_id=%u\n",
			    blob->gobj_id, blob->fighter_gobj_id);
		}
		break;
	case SYNETRB_EFFECT_RESPAWN_NESS_PK_WAVE:
		if (fighter_gobj != NULL)
		{
			return efManagerNessPKThunderWaveMakeEffect(fighter_gobj);
		}
		break;
	case SYNETRB_EFFECT_RESPAWN_NESS_PSYCHIC_MAGNET:
		if (fighter_gobj != NULL)
		{
			FTStruct *fp_mag;

			effect_gobj = efManagerNessPsychicMagnetMakeEffect(fighter_gobj);
			if (effect_gobj != NULL)
			{
				fp_mag = ftGetStruct(fighter_gobj);
				if (fp_mag != NULL)
				{
					fp_mag->is_effect_attach = TRUE;
				}
			}
			return effect_gobj;
		}
		break;
	case SYNETRB_EFFECT_RESPAWN_PIKACHU_THUNDER_SHOCK:
		if (fighter_gobj != NULL)
		{
			s32 frame;

			frame = (s32)blob->quake_magnitude;
			if (frame == 0xFF)
			{
				FTStruct *fp_ts;

				fp_ts = ftGetStruct(fighter_gobj);
				frame = ((fp_ts != NULL) && (syNetRbSnapFighterInPikachuAttackS4Scope(fp_ts) != FALSE))
				            ? ftStatusVarsAttack4(fp_ts)->gfx_id
				            : 0;
			}
			if ((frame < 0) || (frame >= FTCOMMON_ATTACKS4_THUNDERSHOCK_GFX_ID_MAX))
			{
				frame = 0;
			}
			return syNetRbSnapMakePikachuThunderShockForFighter(fighter_gobj, frame);
		}
		break;
	case SYNETRB_EFFECT_RESPAWN_REBIRTH_HALO:
		if (fighter_gobj != NULL)
		{
			FTStruct *fp;

			fp = ftGetStruct(fighter_gobj);
			if (fp != NULL)
			{
				if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
				{
					port_log(
					    "SSB64 NetRbSnapshot: effect_respawn kind=REBIRTH_HALO blob_gobj_id=%u fighter_gobj_id=%u\n",
					    blob->gobj_id, blob->fighter_gobj_id);
				}
				effect_gobj = efManagerRebirthHaloMakeEffect(fighter_gobj, fp->attr->halo_size);
				if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
				{
					port_log("SSB64 NetRbSnapshot: effect_respawn kind=REBIRTH_HALO result=%s new_gobj_id=%u\n",
					         (effect_gobj != NULL) ? "ok" : "fail",
					         (effect_gobj != NULL) ? syNetRbSnapGobjId(effect_gobj) : 0U);
				}
				return effect_gobj;
			}
		}
		break;
	case SYNETRB_EFFECT_RESPAWN_YOSHI_EGG_LAY:
		if (fighter_gobj != NULL)
		{
			EFStruct *ep_lay;

			if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 NetRbSnapshot: effect_respawn kind=YOSHI_EGG_LAY blob_gobj_id=%u fighter_gobj_id=%u\n",
				    blob->gobj_id, blob->fighter_gobj_id);
			}
			effect_gobj = efManagerYoshiEggLayMakeEffect(fighter_gobj);
			if (effect_gobj != NULL)
			{
				ep_lay = efGetStruct(effect_gobj);
				if (ep_lay != NULL)
				{
					memcpy(&ep_lay->effect_vars, blob->effect_vars, sizeof(ep_lay->effect_vars));
					if (ep_lay->effect_vars.yoshi_egg_lay.force_index != ep_lay->effect_vars.yoshi_egg_lay.index)
					{
						efManagerYoshiEggLaySetAnim(effect_gobj,
						                            ep_lay->effect_vars.yoshi_egg_lay.force_index);
					}
				}
				effect_gobj->anim_frame = blob->anim_frame;
			}
			if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
			{
				port_log("SSB64 NetRbSnapshot: effect_respawn kind=YOSHI_EGG_LAY result=%s new_gobj_id=%u\n",
				         (effect_gobj != NULL) ? "ok" : "fail",
				         (effect_gobj != NULL) ? syNetRbSnapGobjId(effect_gobj) : 0U);
			}
			return effect_gobj;
		}
		break;
	default:
		break;
	}
	return NULL;
}

static void syNetRbSnapApplyEffectBlobTranslate(GObj *gobj, const SYNetRbSnapEffectBlob *blob, sb32 skip_quantize)
{
	DObj *dobj;

	if ((gobj == NULL) || (blob == NULL) || ((blob->snap_flags & SYNETRB_EFFECT_SNAP_TRANSLATE) == 0U))
	{
		return;
	}
	dobj = DObjGetStruct(gobj);
	if (dobj == NULL)
	{
		return;
	}
	dobj->translate.vec.f = blob->translate;
#if defined(SSB64_NETMENU)
	if (skip_quantize == FALSE)
	{
		syNetplayQuantizeDObjTranslate(dobj);
	}
#endif
}

static void syNetRbSnapApplyEffectBlobAnimFrame(GObj *gobj, f32 anim_frame, EFStruct *ep)
{
#if defined(SSB64_NETMENU)
	f32 q;

	q = syNetplayQuantizeF32(anim_frame);
	if ((ep != NULL) && (syNetplayLiveEffectIsNessPsychicMagnet(gobj, ep) != FALSE))
	{
		q = syNetplayQuantizeAnimScalar(anim_frame);
	}
	gobj->anim_frame = q;
	if (DObjGetStruct(gobj) != NULL)
	{
		DObjGetStruct(gobj)->anim_frame = q;
		if ((ep != NULL) && (syNetplayLiveEffectIsNessPsychicMagnet(gobj, ep) != FALSE))
		{
			syNetplayQuantizeDObjAnimPose(DObjGetStruct(gobj));
			syNetplayQuantizeDObjAnimScalars(DObjGetStruct(gobj));
		}
	}
#else
	gobj->anim_frame = anim_frame;
#endif
}

static GObj *syNetRbSnapApplyEffectBlobToGObj(const SYNetRbSnapshotSlot *slot, GObj *gobj,
                                              const SYNetRbSnapEffectBlob *blob)
{
	EFStruct *ep;
	GObj *fighter_gobj;

	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return NULL;
	}
	if (gobj == NULL)
	{
		gobj = syNetRbSnapTryRespawnEffectFromBlob(slot, blob);
		if (gobj == NULL)
		{
			return NULL;
		}
	}
	if ((blob->snap_flags & SYNETRB_EFFECT_SNAP_NO_STRUCT) != 0U)
	{
		syNetRbSnapApplyEffectBlobAnimFrame(gobj, blob->anim_frame, NULL);
		syNetRbSnapApplyEffectBlobTranslate(gobj, blob, FALSE);
		return gobj;
	}
	ep = efGetStruct(gobj);
	if (ep == NULL)
	{
		syNetRbSnapApplyEffectBlobAnimFrame(gobj, blob->anim_frame, NULL);
		syNetRbSnapApplyEffectBlobTranslate(gobj, blob, FALSE);
		return gobj;
	}
	memcpy(&ep->effect_vars, blob->effect_vars, sizeof(ep->effect_vars));
	ep->xf = NULL;
	ep->effect_vars.common.xf = NULL;
	ep->effect_vars.dust_light.xf = NULL;
	ep->effect_vars.dust_heavy.xf = NULL;
	ep->bank_id = blob->bank_id;
	syNetRbSnapEndEffectXfFuncProcs(gobj);
	fighter_gobj = NULL;
	if (blob->respawn_kind == SYNETRB_EFFECT_RESPAWN_FOX_REFLECTOR)
	{
		fighter_gobj = syNetRbSnapResolveFoxReflectorParentGobj(slot, blob);
	}
	else if ((blob->respawn_kind == SYNETRB_EFFECT_RESPAWN_SHIELD) ||
	         (blob->respawn_kind == SYNETRB_EFFECT_RESPAWN_YOSHI_SHIELD))
	{
		fighter_gobj = syNetRbSnapResolveShieldParentGobj(slot, blob);
	}
	else if (blob->respawn_kind == SYNETRB_EFFECT_RESPAWN_YOSHI_EGG_LAY)
	{
		fighter_gobj = syNetRbSnapResolveYoshiEggLayParentGobj(slot, blob);
	}
	else if (blob->fighter_gobj_id != 0U)
	{
		fighter_gobj = gcFindGObjByID(blob->fighter_gobj_id);
		if ((fighter_gobj != NULL) && (ftGetStruct(fighter_gobj) == NULL))
		{
			fighter_gobj = NULL;
		}
	}
	ep->fighter_gobj = fighter_gobj;
	if ((syNetRbSnapLiveEffectIsFoxReflector(gobj, ep) != FALSE) && (fighter_gobj != NULL))
	{
		FTStruct *fp_ref;

		fp_ref = ftGetStruct(fighter_gobj);
		if ((fp_ref != NULL) && (fp_ref->joints[nFTPartsJointTopN] != NULL))
		{
			DObjGetStruct(gobj)->user_data.p = fp_ref->joints[nFTPartsJointTopN];
		}
	}
	if ((syNetRbSnapLiveEffectIsShield(gobj, ep) != FALSE) && (fighter_gobj != NULL))
	{
		FTStruct *fp_sh;

		fp_sh = ftGetStruct(fighter_gobj);
		if ((fp_sh != NULL) && (fp_sh->joints[nFTPartsJointYRotN] != NULL))
		{
			DObjGetStruct(gobj)->user_data.p = fp_sh->joints[nFTPartsJointYRotN];
		}
	}
	if ((syNetRbSnapLiveEffectIsYoshiEggLay(gobj, ep) != FALSE) && (fighter_gobj != NULL))
	{
		FTStruct *fp_egg;
		DObj *dobj_egg;

		fp_egg = ftGetStruct(fighter_gobj);
		dobj_egg = DObjGetStruct(gobj);
		if ((fp_egg != NULL) && (dobj_egg != NULL) && (fp_egg->joints[nFTPartsJointTopN] != NULL))
		{
			dobj_egg->user_data.p = fp_egg->joints[nFTPartsJointTopN];
			if (syNetRbSnapFighterInYoshiEggLayScope(fp_egg) != FALSE)
			{
				ftStatusVarsCaptureYoshi(fp_egg)->effect_gobj = gobj;
				fp_egg->is_effect_attach = TRUE;
			}
		}
	}
	if ((syNetplayLiveEffectIsNessPsychicMagnet(gobj, ep) != FALSE) && (fighter_gobj != NULL))
	{
		FTStruct *fp_mag;

		fp_mag = ftGetStruct(fighter_gobj);
		if ((fp_mag != NULL) && (fp_mag->joints[nFTPartsJointTopN] != NULL))
		{
			DObjGetStruct(gobj)->user_data.p = fp_mag->joints[nFTPartsJointTopN];
			fp_mag->is_effect_attach = TRUE;
		}
	}
	syNetRbSnapApplyEffectBlobAnimFrame(gobj, blob->anim_frame, ep);
	syNetRbSnapApplyEffectBlobTranslate(
	    gobj, blob,
	    ((ep != NULL) && (syNetRbSnapLiveEffectIsYoshiEggLay(gobj, ep) != FALSE)) ? TRUE : FALSE);
	if ((ep != NULL) && (syNetRbSnapLiveEffectIsQuake(gobj, ep) != FALSE))
	{
		syNetRbSnapStampQuakeEffectFromBlob(gobj, ep, blob);
	}
	return gobj;
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

/*
 * Reverse lookup: find the live Fox reflector effect bound to `fighter_gobj` via its back-pointer
 * (ep->fighter_gobj). On rollback the reflector is respawned through efManagerFoxReflectorMakeEffect,
 * which mints a fresh GObj id, so the blob's captured fox_speciallw_effect_gobj_id no longer resolves
 * by id (gcFindGObjByID misses). The effect-apply path sets the effect->fighter back-pointer, so we
 * recover the forward (fighter->effect) pointer from that instead of the stale id.
 */
static GObj *syNetRbSnapFindLiveFoxReflectorEffectForFighter(const GObj *fighter_gobj)
{
	s32 pass;

	if (fighter_gobj == NULL)
	{
		return NULL;
	}
	for (pass = 0; pass < 2; pass++)
	{
		GObj *gobj;

		for (gobj = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		     gobj != NULL; gobj = gobj->link_next)
		{
			EFStruct *ep = efGetStruct(gobj);

			if (syNetRbSnapLiveEffectIsFoxReflector(gobj, ep) == FALSE)
			{
				continue;
			}
			if (ep->fighter_gobj == fighter_gobj)
			{
				return gobj;
			}
		}
	}
	return NULL;
}

static GObj *syNetRbSnapFindLiveShieldEffectForFighter(const GObj *fighter_gobj)
{
	FTStruct *fp;
	s32 pass;
	s32 player;

	if (fighter_gobj == NULL)
	{
		return NULL;
	}
	fp = ftGetStruct(fighter_gobj);
	player = (fp != NULL) ? fp->player : -1;
	for (pass = 0; pass < 2; pass++)
	{
		GObj *gobj;

		for (gobj = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		     gobj != NULL; gobj = gobj->link_next)
		{
			EFStruct *ep = efGetStruct(gobj);

			if (syNetRbSnapLiveEffectIsShield(gobj, ep) == FALSE)
			{
				continue;
			}
			if (syNetRbSnapLiveShieldEffectOwnedByFighter(ep, fighter_gobj, player) != FALSE)
			{
				return gobj;
			}
		}
	}
	return NULL;
}

static sb32 syNetRbSnapShieldEffectMatchesKeep(const GObj *gobj, const GObj *keep_gobj)
{
	if (gobj == NULL)
	{
		return FALSE;
	}
	if (keep_gobj == NULL)
	{
		return FALSE;
	}
	/*
	 * Pointer identity only. Shield (and Yoshi egg-lay shell) effects share a fixed pool link id
	 * (e.g. 1011); multiple live GObjs can carry the same id after rollback respawn/ensure churn.
	 * Treating id-equality as "matches keep" made PruneDuplicateShieldEffects and guard_id_rebind_live
	 * keep every duplicate, so stacked 0xC0 bubbles rendered opaque. See
	 * docs/bugs/netplay_guard_shield_presentation_reconcile_2026-06-07.md (Phase 37).
	 */
	return (gobj == keep_gobj) ? TRUE : FALSE;
}

static sb32 syNetRbSnapYoshiEggLayEffectMatchesKeep(const GObj *gobj, const GObj *keep_gobj)
{
	return syNetRbSnapShieldEffectMatchesKeep(gobj, keep_gobj);
}

static s32 syNetRbSnapCountLiveYoshiEggLayEffectsForFighter(const GObj *fighter_gobj)
{
	s32 pass;
	s32 count;

	if (fighter_gobj == NULL)
	{
		return 0;
	}
	count = 0;
	for (pass = 0; pass < 2; pass++)
	{
		GObj *gobj;

		for (gobj = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		     gobj != NULL; gobj = gobj->link_next)
		{
			EFStruct *ep = efGetStruct(gobj);

			if ((syNetRbSnapLiveEffectIsYoshiEggLay(gobj, ep) != FALSE) && (ep->fighter_gobj == fighter_gobj))
			{
				count++;
			}
		}
	}
	return count;
}

static s32 syNetRbSnapCountLiveShieldEffectsForFighter(const GObj *fighter_gobj)
{
	FTStruct *fp;

	if (fighter_gobj == NULL)
	{
		return 0;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp != NULL)
	{
		return syNetRbSnapCountLiveShieldEffectsForPlayer(fp->player);
	}
	return 0;
}

static void syNetRbSnapRebindFighterEffectGobjs(const SYNetRbSnapFighterBlob *blob, FTStruct *fp, GObj *fighter_gobj)
{
	GObj *guard_gobj;
	GObj *reflector_gobj;

	if ((blob == NULL) || (fp == NULL) || (blob->is_valid == FALSE))
	{
		return;
	}
	if ((syNetRbSnapBlobInGuardScope(blob) != FALSE) && (syNetRbSnapFighterGuardEffectUnionOwned(fp) != FALSE))
	{
		guard_gobj = syNetRbSnapResolveCoupledEffectGobj(syNetRbSnapGuardEffectIdFromBlob(blob));
		if (guard_gobj != NULL)
		{
			EFStruct *guard_ep;

			guard_ep = efGetStruct(guard_gobj);
			if ((guard_ep == NULL) ||
			    (syNetRbSnapShieldPlayerFromEffectVars(guard_ep) != fp->player))
			{
				guard_gobj = NULL;
			}
		}
		if (guard_gobj == NULL)
		{
			guard_gobj = syNetRbSnapFindLiveShieldEffectForFighter(fighter_gobj);
		}
		ftStatusVarsGuard(fp)->effect_gobj = guard_gobj;
		if ((guard_gobj != NULL) && (fp->fkind == nFTKindYoshi) && (fp->is_shield != FALSE))
		{
			syNetRbSnapApplyYoshiShieldPresentation(fighter_gobj, fp);
		}
	}
	if ((blob->status_id == nFTCommonStatusYoshiEgg) && (fp->status_id == nFTCommonStatusYoshiEgg))
	{
		GObj *egg_lay_gobj;

		egg_lay_gobj = syNetRbSnapFindLiveYoshiEggLayEffectForFighter(fighter_gobj);
		if ((egg_lay_gobj == NULL) && (blob->captureyoshi_effect_gobj_id != 0U) &&
		    (syNetRbSnapEffectIdClaimedByGuard(NULL, blob->captureyoshi_effect_gobj_id) == FALSE))
		{
			GObj *id_gobj;

			id_gobj = gcFindGObjByID(blob->captureyoshi_effect_gobj_id);
			if (syNetRbSnapYoshiEggLayEffectOwnedByFighter(id_gobj, fighter_gobj) != FALSE)
			{
				egg_lay_gobj = id_gobj;
			}
		}
		if ((egg_lay_gobj != NULL) &&
		    (syNetRbSnapEffectIdClaimedByGuard(NULL, (u32)egg_lay_gobj->id) == FALSE))
		{
			ftStatusVarsCaptureYoshi(fp)->effect_gobj = egg_lay_gobj;
		}
	}
	else if (syNetRbSnapFighterInYoshiEggLayScope(fp) != FALSE)
	{
		ftStatusVarsCaptureYoshi(fp)->effect_gobj = NULL;
	}
	syNetRbSnapSanitizeCaptureYoshiEffectGobj(fp);
	reflector_gobj = syNetRbSnapResolveCoupledEffectGobj(syNetRbSnapFoxSpecialLwEffectIdFromBlob(blob));
	if ((reflector_gobj == NULL) && (syNetRbSnapBlobFoxInReflectorScope(blob) != FALSE))
	{
		/* Respawned reflector has a new GObj id; recover via the effect->fighter back-pointer. */
		reflector_gobj = syNetRbSnapFindLiveFoxReflectorEffectForFighter(fighter_gobj);
	}
	fp->status_vars.fox.speciallw.effect_gobj = reflector_gobj;
}

#ifdef PORT
static u32 s_syNetRbSnapParticleResetGen;
#endif

static void syNetRbSnapResetParticlesForRollback(void)
{
	lbParticleEjectStructAll();
	lbParticleEjectGeneratorAll();
#ifdef PORT
	syNetRbSnapStripEffectXfCouplingAfterParticleReset();
	s_syNetRbSnapParticleResetGen++;
#endif
}

#ifdef PORT
u32 syNetRbSnapGetParticleResetGeneration(void)
{
	return s_syNetRbSnapParticleResetGen;
}
#endif

static sb32 syNetRbSnapHyruleTwisterStatusNeedsGObj(u8 twister_status)
{
	return (twister_status >= (u8)nGRHyruleTwisterStatusSummon) &&
	       (twister_status <= (u8)nGRHyruleTwisterStatusStop);
}

#ifdef PORT
void syNetRbSnapRepairStageSetVerifyOnly(sb32 verify_only)
{
	s_syNetRbSnapRepairStageVerifyOnly = (verify_only != FALSE) ? TRUE : FALSE;
}
#endif

/*
 * Finished twister cycles capture twister_gobj_id==0 while raw status can still read Stop until
 * Subside completes. Repair must treat those blobs as idle Wait, not resurrect Summon..Stop.
 */
static u8 syNetRbSnapHyruleTwisterEffectiveBlobStatus(const SYNetRbSnapGroundHyrule *src)
{
	if (src == NULL)
	{
		return (u8)nGRHyruleTwisterStatusSleep;
	}
	if ((src->twister_gobj_id == 0U) &&
	    (syNetRbSnapHyruleTwisterStatusNeedsGObj(src->twister_status) != FALSE))
	{
		return (u8)nGRHyruleTwisterStatusWait;
	}
	return src->twister_status;
}

static sb32 syNetRbSnapHyruleTwisterStatusNeedsParticle(u8 twister_status)
{
	return (twister_status >= (u8)nGRHyruleTwisterStatusSummon) &&
	       (twister_status <= (u8)nGRHyruleTwisterStatusSubside);
}

static sb32 syNetRbSnapIsValidHyruleTwisterGObj(GObj *gobj)
{
	/*
	 * gcMakeGObjSPAfter(id, ...) stores nGCCommonKindGround (1010) in gobj->id, NOT gobj->obj_kind.
	 * obj_kind is the DObj/SObj/CObj append marker (nGCCommonAppendDObj==1 once the mesh DObj is added),
	 * so the old "obj_kind != nGCCommonKindGround" test was 1 != 1010 and ALWAYS failed — every real
	 * twister mesh was rejected, breaking rebind-from-link, obstacle re-registration, and rider resync.
	 * The Hyrule ground controller shares id 1010 but carries grHyruleTwisterProcUpdate and has no DObj,
	 * so the stage-controller + DObj checks below distinguish the collidable mesh from the controller.
	 */
	if ((gobj == NULL) || (gobj->id != (u32)nGCCommonKindGround) || (gobj->link_id != nGCCommonLinkIDGround))
	{
		return FALSE;
	}
	if (syNetRbSnapHyruleGObjIsStageController(gobj) != FALSE)
	{
		return FALSE;
	}
	if (DObjGetStruct(gobj) == NULL)
	{
		return FALSE;
	}
	return TRUE;
}

static void syNetRbSnapHyruleTwisterClearGObj(GRCommonGroundVarsHyrule *hy)
{
	if (hy->twister_gobj != NULL)
	{
		ftMainClearGroundObstacle(hy->twister_gobj);
		syNetRbSnapEjectGObj(hy->twister_gobj);
		hy->twister_gobj = NULL;
	}
	hy->twister_xf = NULL;
}

/*
 * Ring slots must not carry Summon/Move/Turn/Stop with a stale gobj id after UpdateStop ejected the
 * mesh, or active status with no live gobj pointer at capture time.
 */
static void syNetRbSnapHyruleTwisterNormalizeAtCapture(SYNetRbSnapGroundHyrule *dst,
						       const GRCommonGroundVarsHyrule *src)
{
	u8 live_status;

	if ((dst == NULL) || (src == NULL))
	{
		return;
	}
	live_status = dst->twister_status;
	if (src->twister_gobj != NULL)
	{
		dst->twister_gobj_id = syNetRbSnapGobjId(src->twister_gobj);
	}
	else
	{
		dst->twister_gobj_id = 0U;
	}
	if ((dst->twister_status == (u8)nGRHyruleTwisterStatusSleep) ||
	    (dst->twister_status == (u8)nGRHyruleTwisterStatusWait) ||
	    (dst->twister_status == (u8)nGRHyruleTwisterStatusSubside))
	{
		dst->twister_gobj_id = 0U;
	}
	if ((src->twister_gobj == NULL) &&
	    ((dst->twister_status == (u8)nGRHyruleTwisterStatusStop) ||
	     (dst->twister_status == (u8)nGRHyruleTwisterStatusSubside)))
	{
		dst->twister_gobj_id = 0U;
		if (dst->twister_status == (u8)nGRHyruleTwisterStatusStop)
		{
			dst->twister_status = (u8)nGRHyruleTwisterStatusWait;
		}
	}
	if ((dst->twister_gobj_id == 0U) &&
	    (syNetRbSnapHyruleTwisterStatusNeedsGObj(dst->twister_status) != FALSE))
	{
		dst->twister_status = (u8)nGRHyruleTwisterStatusWait;
	}
#ifdef PORT
	if ((live_status != dst->twister_status) &&
	    (syNetRbSnapHyruleTwisterStatusNeedsGObj(live_status) != FALSE))
	{
		syNetRbSnapHyruleTwisterLogCapture(0U, live_status, dst, "normalize_active_to_wait");
	}
#endif
}

static void syNetRbSnapHyruleTwisterCopyActiveFromBlob(GRCommonGroundVarsHyrule *hy,
						       const SYNetRbSnapGroundHyrule *src)
{
	if ((hy == NULL) || (src == NULL))
	{
		return;
	}
	hy->twister_status = src->twister_status;
	hy->twister_wait = src->twister_wait;
	hy->twister_vel = src->twister_vel;
	hy->twister_speed_wait = src->twister_speed_wait;
	hy->twister_turn_wait = src->twister_turn_wait;
}

/*
 * Snapshots taken after UpdateStop can carry Move/Turn/Stop/Summon status with twister_gobj_id==0
 * (gobj ejected, xf subsiding). Repair must not resurrect a full tornado from that stale status.
 */
static void syNetRbSnapHyruleTwisterNormalizeFromBlob(GRCommonGroundVarsHyrule *hy,
						      const SYNetRbSnapGroundHyrule *src)
{
	if ((hy == NULL) || (src == NULL))
	{
		return;
	}
	if ((src->twister_gobj_id != 0U) && (hy->twister_gobj == NULL))
	{
		/*
		 * gcFindGObjByID miss after particle teardown: keep blob active scalars so repair can
		 * respawn the mesh. Do not downgrade to Wait while blob still claims Summon..Stop.
		 */
		if (syNetRbSnapHyruleTwisterStatusNeedsGObj(src->twister_status) != FALSE)
		{
			syNetRbSnapHyruleTwisterCopyActiveFromBlob(hy, src);
			return;
		}
		if (syNetRbSnapHyruleTwisterStatusNeedsGObj(hy->twister_status) != FALSE)
		{
			hy->twister_status = (u8)nGRHyruleTwisterStatusWait;
			hy->twister_wait = src->twister_wait;
			syNetRbSnapHyruleTwisterClearGObj(hy);
		}
		return;
	}
	if (src->twister_gobj_id != 0U)
	{
		return;
	}
	if (syNetRbSnapHyruleTwisterStatusNeedsGObj(src->twister_status) != FALSE)
	{
		hy->twister_status = (u8)nGRHyruleTwisterStatusWait;
		hy->twister_wait = src->twister_wait;
		syNetRbSnapHyruleTwisterClearGObj(hy);
		return;
	}
	if (hy->twister_status == (u8)nGRHyruleTwisterStatusSubside)
	{
		syNetRbSnapHyruleTwisterClearGObj(hy);
		return;
	}
	if (syNetRbSnapHyruleTwisterStatusNeedsGObj(hy->twister_status) != FALSE)
	{
		hy->twister_status = (u8)nGRHyruleTwisterStatusWait;
		hy->twister_wait = src->twister_wait;
		syNetRbSnapHyruleTwisterClearGObj(hy);
	}
}

static sb32 syNetRbSnapHyruleTwisterStatusNeedsObstacle(u8 twister_status)
{
	return (twister_status == (u8)nGRHyruleTwisterStatusMove) ||
	       (twister_status == (u8)nGRHyruleTwisterStatusTurn) ||
	       (twister_status == (u8)nGRHyruleTwisterStatusStop);
}

static sb32 syNetRbSnapHyruleTwisterStatusNeedsObstacleForRepair(u8 twister_status)
{
	return (twister_status >= (u8)nGRHyruleTwisterStatusSummon) &&
	       (twister_status <= (u8)nGRHyruleTwisterStatusStop);
}

/*
 * Rollback repair reloads Summon with a valid gobj but never runs grHyruleTwisterUpdateSummon's
 * wait==0 transition, leaving visible VFX with no collision volume. When the blob already captured
 * Move/Turn/Stop scalars, restore from blob instead of rolling fresh RNG (which desyncs peers).
 */
static sb32 syNetRbSnapHyruleTwisterCompleteSummonIfReady(GRCommonGroundVarsHyrule *hy,
							  const SYNetRbSnapGroundHyrule *src)
{
	if ((hy == NULL) || (hy->twister_status != (u8)nGRHyruleTwisterStatusSummon) || (hy->twister_wait != 0U))
	{
		return FALSE;
	}
	if ((src != NULL) && (src->twister_status >= (u8)nGRHyruleTwisterStatusMove))
	{
		syNetRbSnapHyruleTwisterCopyActiveFromBlob(hy, src);
#if defined(SSB64_NETMENU)
		hy->twister_vel = syNetplayQuantizeF32(hy->twister_vel);
#endif
#ifdef PORT
		if (syNetRbSnapHyruleTwisterDiagEnabled() != FALSE)
		{
			port_log(
			    "SSB64 NetRbSnapshot: hyrule_twister_summon_complete tick=%u note=blob_resume status=%u wait=%u vel=%f speed_wait=%u\n",
			    (unsigned int)0U, (unsigned int)hy->twister_status, (unsigned int)hy->twister_wait,
			    (double)hy->twister_vel, (unsigned int)hy->twister_speed_wait);
		}
#endif
		return TRUE;
	}
	/* Defer Summon->Move RNG to the next vanilla grHyruleTwisterUpdateSummon tick. */
	return FALSE;
}

static sb32 syNetRbSnapHyruleTwisterEnsureObstacle(GRCommonGroundVarsHyrule *hy, u8 twister_status,
						   sb32 needs_obstacle, const char **fail_reason)
{
	if (fail_reason != NULL)
	{
		*fail_reason = NULL;
	}
	if ((hy == NULL) || (hy->twister_gobj == NULL) ||
	    (syNetRbSnapIsValidHyruleTwisterGObj(hy->twister_gobj) == FALSE))
	{
		if (fail_reason != NULL)
		{
			*fail_reason = "invalid_gobj";
		}
		return FALSE;
	}
	if (needs_obstacle == FALSE)
	{
		if (fail_reason != NULL)
		{
			*fail_reason = "status_skip";
		}
		return FALSE;
	}
	if (ftMainEnsureGroundObstacle(hy->twister_gobj, grHyruleTwisterCheckGetDamageKind) != FALSE)
	{
		return TRUE;
	}
	if (fail_reason != NULL)
	{
		*fail_reason = "table_full";
	}
	return FALSE;
}

static sb32 syNetRbSnapHyruleTwisterReregisterObstacle(GRCommonGroundVarsHyrule *hy, u8 twister_status)
{
	return syNetRbSnapHyruleTwisterEnsureObstacle(hy, twister_status,
						      syNetRbSnapHyruleTwisterStatusNeedsObstacle(twister_status), NULL);
}

static sb32 syNetRbSnapHyruleTwisterReregisterObstacleForRepair(GRCommonGroundVarsHyrule *hy, u8 twister_status,
								const char **fail_reason)
{
	return syNetRbSnapHyruleTwisterEnsureObstacle(
	    hy, twister_status, syNetRbSnapHyruleTwisterStatusNeedsObstacleForRepair(twister_status), fail_reason);
}

static void syNetRbSnapHyruleTwisterRecreateEffect(GRCommonGroundVarsHyrule *hy, const Vec3f *pos, s32 effect_id)
{
	LBParticle *pc;
	Vec3f effect_pos;
	DObj *twister_dobj;

	if ((hy == NULL) || (pos == NULL) || (hy->twister_xf != NULL))
	{
		return;
	}
	effect_pos = *pos;
	if (hy->twister_gobj != NULL)
	{
		twister_dobj = DObjGetStruct(hy->twister_gobj);
		if (twister_dobj != NULL)
		{
			effect_pos = twister_dobj->translate.vec.f;
		}
	}
	pc = grHyruleTwisterMakeEffect(&effect_pos, effect_id);
	if ((pc != NULL) && (pc->xf != NULL))
	{
		hy->twister_xf = pc->xf;
	}
}

#ifdef PORT
static sb32 syNetRbSnapHyruleTwisterDiagEnabled(void)
{
	const char *e = getenv("SSB64_NETPLAY_HYRULE_TWISTER_DIAG");

	return (e != NULL) && (e[0] != '\0') && (strcmp(e, "0") != 0);
}

static void syNetRbSnapHyruleTwisterLogRepair(const SYNetRbSnapshotSlot *slot, const SYNetRbSnapGroundHyrule *src,
					      const GRCommonGroundVarsHyrule *hy, const char *reason, sb32 obstacle_added,
					      sb32 summon_done)
{
	if (syNetRbSnapHyruleTwisterDiagEnabled() == FALSE)
	{
		return;
	}
	port_log(
	    "SSB64 NetRbSnapshot: hyrule_twister_repair tick=%u reason=%s status=%u blob_status=%u wait=%u blob_wait=%u gobj_id=%u gobj=%p xf=%p obstacle=%d summon_done=%d\n",
	    (unsigned int)((slot != NULL) ? slot->tick : 0U), (reason != NULL) ? reason : "?",
	    (unsigned int)((hy != NULL) ? hy->twister_status : 0U),
	    (unsigned int)((src != NULL) ? src->twister_status : 0U),
	    (unsigned int)((hy != NULL) ? hy->twister_wait : 0U),
	    (unsigned int)((src != NULL) ? src->twister_wait : 0U),
	    (unsigned int)((src != NULL) ? src->twister_gobj_id : 0U), (void *)((hy != NULL) ? hy->twister_gobj : NULL),
	    (void *)((hy != NULL) ? hy->twister_xf : NULL), (int)obstacle_added, (int)summon_done);
}

static void syNetRbSnapHyruleTwisterLogCapture(u32 tick, u8 live_status, const SYNetRbSnapGroundHyrule *dst,
					      const char *note)
{
	if ((syNetRbSnapHyruleTwisterDiagEnabled() == FALSE) || (dst == NULL))
	{
		return;
	}
	port_log(
	    "SSB64 NetRbSnapshot: hyrule_twister_capture tick=%u note=%s live_status=%u blob_status=%u wait=%u gobj_id=%u vel=%f speed_wait=%u turn_wait=%u\n",
	    (unsigned int)tick, (note != NULL) ? note : "?", (unsigned int)live_status, (unsigned int)dst->twister_status,
	    (unsigned int)dst->twister_wait, (unsigned int)dst->twister_gobj_id, (double)dst->twister_vel,
	    (unsigned int)dst->twister_speed_wait, (unsigned int)dst->twister_turn_wait);
}

static void syNetRbSnapHyruleTwisterLogApplyDrift(u32 tick, const GRCommonGroundVarsHyrule *hy,
						const SYNetRbSnapGroundHyrule *src)
{
	if ((syNetRbSnapHyruleTwisterDiagEnabled() == FALSE) || (hy == NULL) || (src == NULL))
	{
		return;
	}
	if ((hy->twister_status == src->twister_status) && (hy->twister_wait == src->twister_wait) &&
	    (hy->twister_speed_wait == src->twister_speed_wait) && (hy->twister_turn_wait == src->twister_turn_wait) &&
	    (syNetRbSnapGobjId(hy->twister_gobj) == src->twister_gobj_id))
	{
		return;
	}
	port_log(
	    "SSB64 NetRbSnapshot: hyrule_twister_apply_drift tick=%u live_status=%u blob_status=%u live_wait=%u blob_wait=%u live_gobj_id=%u blob_gobj_id=%u live_vel=%f blob_vel=%f\n",
	    (unsigned int)tick, (unsigned int)hy->twister_status, (unsigned int)src->twister_status,
	    (unsigned int)hy->twister_wait, (unsigned int)src->twister_wait,
	    (unsigned int)syNetRbSnapGobjId(hy->twister_gobj), (unsigned int)src->twister_gobj_id, (double)hy->twister_vel,
	    (double)src->twister_vel);
}

static void syNetRbSnapHyruleTwisterLogObstacleFail(u32 tick, u8 twister_status, const char *fail_reason)
{
	if ((syNetRbSnapHyruleTwisterDiagEnabled() == FALSE) || (fail_reason == NULL))
	{
		return;
	}
	port_log(
	    "SSB64 NetRbSnapshot: hyrule_twister_obstacle_fail tick=%u status=%u reason=%s slots_used=%d slots_max=%d\n",
	    (unsigned int)tick, (unsigned int)twister_status, fail_reason, (int)ftMainGroundObstacleSlotsUsed(),
	    (int)2);
}
#endif

static sb32 syNetRbSnapHyruleGObjIsStageController(GObj *gobj)
{
	GObjProcess *proc;

	if (gobj == NULL)
	{
		return FALSE;
	}
	for (proc = gobj->gobjproc_head; proc != NULL; proc = proc->link_next)
	{
		if ((proc->kind == nGCProcessKindFunc) && (proc->exec.func == grHyruleTwisterProcUpdate))
		{
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * gcFindGObjByID can miss after particle reset / synctest verify while the mesh still lives on the
 * ground link. Rebind before orphan eject or MakeTwister so emergency restore keeps the live gobj.
 */
static GObj *syNetRbSnapFindHyruleTwisterMeshGObj(const GRCommonGroundVarsHyrule *hy)
{
	GObj *gobj;

	for (gobj = gGCCommonLinks[nGCCommonLinkIDGround]; gobj != NULL; gobj = gobj->link_next)
	{
		if ((hy != NULL) && (gobj == hy->twister_gobj))
		{
			continue;
		}
		if (syNetRbSnapHyruleGObjIsStageController(gobj) != FALSE)
		{
			continue;
		}
		if (syNetRbSnapIsValidHyruleTwisterGObj(gobj) != FALSE)
		{
			return gobj;
		}
	}
	return NULL;
}

static void syNetRbSnapHyruleTwisterRebindMeshFromLink(GRCommonGroundVarsHyrule *hy,
						       const SYNetRbSnapGroundHyrule *src)
{
	GObj *mesh;

	if ((hy == NULL) || (hy->twister_gobj != NULL))
	{
		return;
	}
	if ((src == NULL) || (src->twister_gobj_id == 0U) ||
	    (syNetRbSnapHyruleTwisterStatusNeedsGObj(src->twister_status) == FALSE))
	{
		return;
	}
	mesh = syNetRbSnapFindHyruleTwisterMeshGObj(hy);
	if (mesh != NULL)
	{
		hy->twister_gobj = mesh;
	}
}

/*
 * Rollback invalid-gobj repair used to null hy->twister_gobj without gcEjectGObj, leaving a
 * non-collidable orphan mesh/particle on the ground link while grHyruleMakeTwister spawned a second
 * collidable twister. Eject every ground GObj that is not the stage controller and not the live slot.
 */
static void syNetRbSnapEjectOrphanHyruleTwisterGObjs(GRCommonGroundVarsHyrule *hy)
{
	GObj *gobj;
	GObj *next;

	if (hy == NULL)
	{
		return;
	}
	for (gobj = gGCCommonLinks[nGCCommonLinkIDGround]; gobj != NULL; gobj = next)
	{
		next = gobj->link_next;
		if ((gobj == hy->twister_gobj) || (syNetRbSnapHyruleGObjIsStageController(gobj) != FALSE))
		{
			continue;
		}
		/* Orphan twister meshes are id==nGCCommonKindGround (1010); obj_kind is the append marker, not 1010. */
		if (gobj->id != (u32)nGCCommonKindGround)
		{
			continue;
		}
		ftMainClearGroundObstacle(gobj);
		syNetRbSnapEjectGObj(gobj);
	}
}

#ifdef PORT
static void syNetRbSnapResyncFighterTwisterGobjs(u32 snap_tick)
{
	GObj *fighter_gobj;
	GObj *twister_gobj;
	DObj *twister_root;
	u32 rebound_count;
	u32 eject_count;

	twister_gobj = grHyruleGetTwisterGobj();
	twister_root = (twister_gobj != NULL) ? DObjGetStruct(twister_gobj) : NULL;
	rebound_count = 0U;
	eject_count = 0U;
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if (fp->status_id != nFTCommonStatusTwister)
		{
			continue;
		}
		if ((twister_gobj != NULL) && (syNetRbSnapIsValidHyruleTwisterGObj(twister_gobj) != FALSE))
		{
			ftStatusVarsTwister(fp)->tornado_gobj = twister_gobj;
			rebound_count++;
#if defined(SSB64_NETMENU)
			ftCommonTwisterReconcileRiderAfterRollback(fighter_gobj);
#else
			{
				DObj *fighter_root = DObjGetStruct(fighter_gobj);

				if ((twister_root != NULL) && (fighter_root != NULL))
				{
					fighter_root->translate.vec.f = twister_root->translate.vec.f;
				}
			}
#endif
		}
		else
		{
			ftCommonTwisterShootFighter(fighter_gobj);
			eject_count++;
		}
	}
	if (syNetRbSnapHyruleTwisterDiagEnabled() != FALSE)
	{
		port_log(
		    "SSB64 NetRbSnapshot: hyrule_twister_rider_resync tick=%u rebound=%u eject=%u gobj=%p\n",
		    (unsigned int)snap_tick, (unsigned int)rebound_count, (unsigned int)eject_count,
		    (void *)twister_gobj);
	}
}
#endif

/*
 * syNetRbSnapResetParticlesForRollback() tears down all LBParticles, including the Hyrule
 * twister VFX (twister_xf). Ground blobs restore twister scalars + gobj id but not xf.
 * Recreate the effect (and respawn the gobj if the id lookup failed) before the next
 * grHyruleTwisterUpdateMove tick — otherwise twister_xf->translate SIGSEGVs at fault ~0x38.
 */
static void syNetRbSnapEnsureHyruleTwisterAfterParticleReset(const SYNetRbSnapshotSlot *slot)
{
#ifdef PORT
	static u32 s_syNetRbSnapHyruleTwisterRepairTick = UINT32_MAX;
#endif
	const SYNetRbSnapGroundHyrule *src;
	GRCommonGroundVarsHyrule *hy;
	DObj *twister_dobj;
	GObj *twister_gobj;
	Vec3f twister_pos;
	u8 twister_status;
	sb32 obstacle_added;
	sb32 summon_completed;

	if ((slot == NULL) || (slot->ground_captured == FALSE) || (slot->ground.gkind != nGRKindHyrule))
	{
		return;
	}
	if (slot->ground.payload_len < (u16)offsetof(SYNetRbSnapGroundHyrule, twister_pos))
	{
		return;
	}
#ifdef PORT
	if (slot->tick == s_syNetRbSnapHyruleTwisterRepairTick)
	{
		return;
	}
	s_syNetRbSnapHyruleTwisterRepairTick = slot->tick;
#endif
#ifdef PORT
	/*
	 * Synctest loads a ring slot into live, then restores the emergency snapshot. Any orphan eject,
	 * idle_clear, or MakeTwister here can destroy the mesh the emergency blob still references by id.
	 */
	if (s_syNetRbSnapRepairStageVerifyOnly != FALSE)
	{
		if (syNetRbSnapHyruleTwisterDiagEnabled() != FALSE)
		{
			port_log("SSB64 NetRbSnapshot: hyrule_twister_repair tick=%u reason=verify_skip_all\n",
				 (unsigned int)slot->tick);
		}
		return;
	}
#endif
	src = (const SYNetRbSnapGroundHyrule *)slot->ground.payload;
	hy = &gGRCommonStruct.hyrule;
	syNetRbSnapHyruleTwisterRebindMeshFromLink(hy, src);
	syNetRbSnapHyruleTwisterNormalizeFromBlob(hy, src);
	twister_status = hy->twister_status;
	summon_completed = syNetRbSnapHyruleTwisterCompleteSummonIfReady(hy, src);
	if (summon_completed != FALSE)
	{
		twister_status = hy->twister_status;
	}
	hy->twister_xf = NULL;
	twister_pos.x = 0.0F;
	twister_pos.y = 0.0F;
	twister_pos.z = 0.0F;
	if (slot->ground.payload_len >= (u16)sizeof(SYNetRbSnapGroundHyrule))
	{
		twister_pos = src->twister_pos;
	}
	else if (hy->twister_gobj != NULL)
	{
		twister_dobj = DObjGetStruct(hy->twister_gobj);
		if (twister_dobj != NULL)
		{
			twister_pos = twister_dobj->translate.vec.f;
		}
	}

	if ((twister_status == (u8)nGRHyruleTwisterStatusSleep) ||
	    (twister_status == (u8)nGRHyruleTwisterStatusWait))
	{
		u8 blob_status = syNetRbSnapHyruleTwisterEffectiveBlobStatus(src);

		if (syNetRbSnapIsValidHyruleTwisterGObj(hy->twister_gobj) == FALSE)
		{
			syNetRbSnapHyruleTwisterRebindMeshFromLink(hy, src);
			if ((src->twister_gobj_id != 0U) &&
			    (syNetRbSnapHyruleTwisterStatusNeedsGObj(blob_status) != FALSE))
			{
				syNetRbSnapHyruleTwisterCopyActiveFromBlob(hy, src);
				twister_status = hy->twister_status;
#ifdef PORT
				syNetRbSnapHyruleTwisterLogRepair(slot, src, hy, "blob_active_respawn", FALSE,
								  summon_completed);
#endif
			}
			else
			{
#ifdef PORT
				syNetRbSnapHyruleTwisterLogRepair(slot, src, hy, "idle_clear", FALSE,
								  summon_completed);
#endif
				syNetRbSnapEjectOrphanHyruleTwisterGObjs(hy);
				syNetRbSnapHyruleTwisterClearGObj(hy);
				return;
			}
		}
		else if (syNetRbSnapHyruleTwisterStatusNeedsGObj(blob_status) != FALSE)
		{
			syNetRbSnapHyruleTwisterCopyActiveFromBlob(hy, src);
			twister_status = hy->twister_status;
#ifdef PORT
			syNetRbSnapHyruleTwisterLogRepair(slot, src, hy, "blob_active_resume", FALSE, summon_completed);
#endif
		}
		else
		{
#ifdef PORT
			syNetRbSnapHyruleTwisterLogRepair(slot, src, hy, "idle_orphan_clear", FALSE, summon_completed);
#endif
			syNetRbSnapEjectOrphanHyruleTwisterGObjs(hy);
			syNetRbSnapHyruleTwisterClearGObj(hy);
			return;
		}
	}
	if (twister_status == (u8)nGRHyruleTwisterStatusSubside)
	{
		syNetRbSnapHyruleTwisterClearGObj(hy);
		/*
		 * Vanilla Subside keeps the id-3 swirl (twister_xf) alive for its 32-tick window with spawn
		 * halted at Stop (lbParticleBeginVortexSoftFadeID), then tears down the generator at Subside
		 * end (lbParticleEjectGeneratorID) after rings expire bottom-up.
		 * Rollback mid-Subside recreates the swirl + dissipation puff, then re-enters soft-fade.
		 */
		syNetRbSnapHyruleTwisterRecreateEffect(hy, &twister_pos, 3);
#ifdef PORT
		if (hy->twister_xf != NULL)
		{
			lbParticleBeginVortexSoftFadeID(hy->twister_xf->generator_id);
			(void)grHyruleTwisterMakeEffect(&hy->twister_xf->translate, 7);
		}
		syNetRbSnapHyruleTwisterLogRepair(slot, src, hy, "subside_fade", FALSE, summon_completed);
#endif
		return;
	}
	syNetRbSnapHyruleTwisterRebindMeshFromLink(hy, src);
	syNetRbSnapEjectOrphanHyruleTwisterGObjs(hy);
	if (syNetRbSnapIsValidHyruleTwisterGObj(hy->twister_gobj) == FALSE)
	{
		syNetRbSnapHyruleTwisterClearGObj(hy);
	}
	obstacle_added = FALSE;
	{
		const char *obstacle_fail_reason = NULL;

		if (syNetRbSnapHyruleTwisterStatusNeedsGObj(twister_status) != FALSE)
		{
			if (hy->twister_gobj == NULL)
			{
				syNetRbSnapHyruleTwisterRebindMeshFromLink(hy, src);
				syNetRbSnapEjectOrphanHyruleTwisterGObjs(hy);
				twister_gobj = grHyruleMakeTwister(&twister_pos);
				if (twister_gobj != NULL)
				{
					hy->twister_gobj = twister_gobj;
					hy->twister_xf = gGRCommonStruct.hyrule.twister_xf;
					summon_completed = syNetRbSnapHyruleTwisterCompleteSummonIfReady(hy, src);
					if (summon_completed != FALSE)
					{
						twister_status = hy->twister_status;
					}
				}
				else
				{
					hy->twister_status = (u8)nGRHyruleTwisterStatusWait;
					hy->twister_wait = src->twister_wait;
#ifdef PORT
					syNetRbSnapHyruleTwisterLogRepair(slot, src, hy, "make_twister_fail", FALSE,
									  summon_completed);
#endif
					return;
				}
			}
			else
			{
				summon_completed = syNetRbSnapHyruleTwisterCompleteSummonIfReady(hy, src);
				if (summon_completed != FALSE)
				{
					twister_status = hy->twister_status;
				}
				(void)grHyruleTwisterRestorePoseFromPos(hy->twister_gobj, &twister_pos);
			}
			obstacle_added =
			    syNetRbSnapHyruleTwisterReregisterObstacleForRepair(hy, twister_status, &obstacle_fail_reason);
#ifdef PORT
			if ((obstacle_added == FALSE) && (obstacle_fail_reason != NULL) &&
			    (syNetRbSnapHyruleTwisterStatusNeedsObstacleForRepair(twister_status) != FALSE))
			{
				if (strcmp(obstacle_fail_reason, "invalid_gobj") == 0)
				{
					syNetRbSnapHyruleTwisterRebindMeshFromLink(hy, src);
					if (syNetRbSnapIsValidHyruleTwisterGObj(hy->twister_gobj) == FALSE)
					{
						syNetRbSnapHyruleTwisterClearGObj(hy);
						syNetRbSnapEjectOrphanHyruleTwisterGObjs(hy);
						twister_gobj = grHyruleMakeTwister(&twister_pos);
						if (twister_gobj != NULL)
						{
							hy->twister_gobj = twister_gobj;
							hy->twister_xf = gGRCommonStruct.hyrule.twister_xf;
							summon_completed =
							    syNetRbSnapHyruleTwisterCompleteSummonIfReady(hy, src);
							if (summon_completed != FALSE)
							{
								twister_status = hy->twister_status;
							}
						}
					}
					if (syNetRbSnapIsValidHyruleTwisterGObj(hy->twister_gobj) != FALSE)
					{
						obstacle_added = syNetRbSnapHyruleTwisterReregisterObstacleForRepair(
						    hy, twister_status, &obstacle_fail_reason);
					}
				}
			}
#endif
#ifdef PORT
			if ((obstacle_added == FALSE) && (obstacle_fail_reason != NULL))
			{
				syNetRbSnapHyruleTwisterLogObstacleFail(slot->tick, twister_status, obstacle_fail_reason);
			}
#endif
		}
	}
	if (syNetRbSnapHyruleTwisterStatusNeedsParticle(twister_status) != FALSE)
	{
		if (hy->twister_xf == NULL)
		{
			syNetRbSnapHyruleTwisterRecreateEffect(hy, &twister_pos, 3);
		}
	}
#ifdef PORT
	syNetRbSnapHyruleTwisterLogRepair(slot, src, hy, "active_repair", obstacle_added, summon_completed);
#endif
}

static void syNetRbSnapEnsurePupupuWhispyAfterParticleReset(const SYNetRbSnapshotSlot *slot)
{
	GRCommonGroundVarsPupupu *pu;

	if ((slot == NULL) || (slot->ground_captured == FALSE) || (slot->ground.gkind != nGRKindPupupu))
	{
		return;
	}
	if (slot->ground.payload_len < (u16)sizeof(SYNetRbSnapGroundPupupu))
	{
		return;
	}
	pu = &gGRCommonStruct.pupupu;
	pu->leaves_xf = NULL;
	pu->dust_xf = NULL;
	if (pu->whispy_status == (u8)nGRPupupuWhispyWindStatusBlow)
	{
		grPupupuWhispyLeavesMakeEffect();
		grPupupuWhispyDustMakeEffect();
	}
}

#ifdef PORT
static sb32 syNetRbSnapJungleTaruCannDiagEnabled(void)
{
	const char *e = getenv("SSB64_NETPLAY_JUNGLE_TARUCANN_DIAG");

	return (e != NULL) && (e[0] != '\0') && (strcmp(e, "0") != 0);
}

static s32 syNetRbSnapCountTaruCannRiders(u8 *player_mask_out)
{
	GObj *fighter_gobj;
	s32 count = 0;
	u8 mask = 0U;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp != NULL) && (fp->status_id == nFTCommonStatusTaruCann))
		{
			count++;
			if ((fp->player >= 0) && (fp->player < GMCOMMON_PLAYERS_MAX))
			{
				mask |= (u8)(1U << fp->player);
			}
		}
	}
	if (player_mask_out != NULL)
	{
		*player_mask_out = mask;
	}
	return count;
}

static void syNetRbSnapCaptureJungleTaruCannRiderState(SYNetRbSnapGroundJungle *dst, GObj *tarucann_gobj)
{
	GObj *fighter_gobj;
	u8 mask = 0U;
	s32 count = 0;

	if (dst == NULL)
	{
		return;
	}
	dst->tarucann_rider_count = 0U;
	dst->tarucann_rider_player_mask = 0U;
	dst->tarucann_shoot_anim_active = 0U;
	dst->tarucann_rider_pad = 0U;
	dst->tarucann_rider_shoot_wait = 0;
	dst->tarucann_rider_release_wait = 0;
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if ((fp != NULL) && (fp->status_id == nFTCommonStatusTaruCann))
		{
			count++;
			if ((fp->player >= 0) && (fp->player < GMCOMMON_PLAYERS_MAX))
			{
				mask |= (u8)(1U << fp->player);
			}
			if (count == 1)
			{
				dst->tarucann_rider_shoot_wait = (s16)ftStatusVarsTaruCann(fp)->shoot_wait;
				dst->tarucann_rider_release_wait = (s16)ftStatusVarsTaruCann(fp)->release_wait;
			}
		}
	}
	dst->tarucann_rider_count = (u8)count;
	dst->tarucann_rider_player_mask = mask;
	if ((tarucann_gobj != NULL) && (grJungleTaruCannIsChildShootAnimActive(tarucann_gobj) != FALSE))
	{
		dst->tarucann_shoot_anim_active = 1U;
	}
}

static void syNetRbSnapLogJungleTaruCannOccupancy(u32 tick, const char *context, s32 live_rider_count,
                                                  u8 live_rider_mask, sb32 live_shoot_anim,
                                                  const SYNetRbSnapGroundJungle *snap_jungle)
{
	s32 snap_rider_count = -1;
	u32 snap_rider_mask = 0U;
	s32 snap_shoot_wait = -1;
	s32 snap_release_wait = -1;
	sb32 snap_shoot_anim = FALSE;

	if (snap_jungle != NULL)
	{
		snap_rider_count = (s32)snap_jungle->tarucann_rider_count;
		snap_rider_mask = (u32)snap_jungle->tarucann_rider_player_mask;
		snap_shoot_wait = (s32)snap_jungle->tarucann_rider_shoot_wait;
		snap_release_wait = (s32)snap_jungle->tarucann_rider_release_wait;
		snap_shoot_anim = (snap_jungle->tarucann_shoot_anim_active != 0U) ? TRUE : FALSE;
	}
	port_log(
	    "SSB64 NetRbSnapshot: jungle_tarucann_occupancy tick=%u ctx=%s live_riders=%d live_mask=0x%02X "
	    "live_shoot_anim=%d snap_riders=%d snap_mask=0x%02X snap_shoot_wait=%d snap_release_wait=%d "
	    "snap_shoot_anim=%d\n",
	    (unsigned int)tick, (context != NULL) ? context : "?", (int)live_rider_count,
	    (unsigned int)live_rider_mask, (int)(live_shoot_anim != FALSE), (int)snap_rider_count,
	    (unsigned int)snap_rider_mask, snap_shoot_wait, snap_release_wait, (int)snap_shoot_anim);
}

static void syNetRbSnapResyncFighterTaruCannGobjs(u32 snap_tick)
{
	GObj *fighter_gobj;
	GObj *tarucann_gobj;
	DObj *barrel_root;
	u8 live_rider_mask;
	s32 live_rider_count;
	sb32 live_shoot_anim;

	tarucann_gobj = grJungleGetTaruCannGobj();
	if (tarucann_gobj == NULL)
	{
		return;
	}
	live_rider_count = syNetRbSnapCountTaruCannRiders(&live_rider_mask);
	live_shoot_anim = grJungleTaruCannIsChildShootAnimActive(tarucann_gobj);
	if (syNetRbSnapJungleTaruCannDiagEnabled() != FALSE)
	{
		syNetRbSnapLogJungleTaruCannOccupancy(snap_tick, "resync_pre", live_rider_count, live_rider_mask,
		                                      live_shoot_anim, NULL);
	}
	if (live_rider_count == 0)
	{
		if (live_shoot_anim != FALSE)
		{
			grJungleTaruCannAddAnimFill(tarucann_gobj);
			if (syNetRbSnapJungleTaruCannDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 NetRbSnapshot: jungle_tarucann_orphan_shoot_suppressed tick=%u ctx=resync_no_rider\n",
				    (unsigned int)snap_tick);
			}
		}
		return;
	}
	barrel_root = DObjGetStruct(tarucann_gobj);
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);
		DObj *fighter_root;

		if (fp->status_id == nFTCommonStatusTaruCann)
		{
			ftStatusVarsTaruCann(fp)->tarucann_gobj = tarucann_gobj;
			if (barrel_root != NULL)
			{
				fighter_root = DObjGetStruct(fighter_gobj);
				if (fighter_root != NULL)
				{
					fighter_root->translate.vec.f = barrel_root->translate.vec.f;
				}
			}
			ftCommonTaruCannReconcileShootStateAfterRollback(fighter_gobj, snap_tick);
#if defined(SSB64_NETMENU)
			if (barrel_root != NULL)
			{
				syNetplayQuantizeDObjTranslate(barrel_root);
				if (barrel_root->child != NULL)
				{
					syNetplayQuantizeDObjTranslate(barrel_root->child);
				}
			}
			syNetRbSnapQuantizeFighterCoupledGeometry(fighter_gobj, FALSE);
#endif
			if (syNetRbSnapJungleTaruCannDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 NetRbSnapshot: jungle_tarucann_rider tick=%u player=%d shoot_wait=%d release_wait=%d\n",
				    (unsigned int)snap_tick, (int)fp->player,
				    (int)ftStatusVarsTaruCann(fp)->shoot_wait,
				    (int)ftStatusVarsTaruCann(fp)->release_wait);
			}
		}
	}
}

static void syNetRbSnapRestoreJungleGround(const SYNetRbSnapGroundJungle *src, u16 payload_len, u32 snap_tick)
{
	GRCommonGroundVarsJungle *dst;
	Vec3f pose;
	sb32 has_pose;
	sb32 has_anim;

	if (src == NULL)
	{
		return;
	}
	has_pose = (payload_len >= (u16)SYNETRB_SNAP_GROUND_JUNGLE_V1_PAYLOAD_LEN) ? TRUE : FALSE;
	has_anim = (payload_len >= (u16)sizeof(SYNetRbSnapGroundJungle)) ? TRUE : FALSE;
	dst = &gGRCommonStruct.jungle;
	dst->tarucann_gobj = grJungleGetTaruCannGobj();
	dst->tarucann_status = src->tarucann_status;
	dst->tarucann_wait = src->tarucann_wait;
	dst->tarucann_rotate_step = src->tarucann_rotate_step;
#if defined(SSB64_NETMENU)
	dst->tarucann_rotate_step = syNetplayQuantizeF32(dst->tarucann_rotate_step);
#endif
	if (has_pose != FALSE)
	{
		f32 rotate_z;

		pose = src->tarucann_translate;
		rotate_z = src->tarucann_rotate_z;
#if defined(SSB64_NETMENU)
		syNetplayQuantizeVec3f(&pose);
		rotate_z = syNetplayQuantizeF32(rotate_z);
#endif
		grJungleRepairTaruCannPresentation(&pose, rotate_z);
	}
	else
	{
		grJungleRepairTaruCannPresentation(NULL, 0.0F);
	}
	/*
	 * Barrel anim runtime (anim_frame / figatree cursor / AObj chain / speeds) is now restored by the
	 * dedicated barrel partition (syNetRbSnapApplyBarrel) after the stage/particle-reset rebuild. The
	 * old anim_wait-only restore + default-joint re-seat here pinned the Move-phase slide under the
	 * stage, so it is intentionally gone. This path keeps only the pose/status scalars, the hollow-tree
	 * rebuild (grJungleRepairTaruCannPresentation above). Rider resync runs after ApplyBarrel.
	 */
	(void)has_anim;
	if (syNetRbSnapJungleTaruCannDiagEnabled() != FALSE)
	{
		DObj *root = (dst->tarucann_gobj != NULL) ? DObjGetStruct(dst->tarucann_gobj) : NULL;
		u8 live_rider_mask = 0U;
		s32 live_rider_count = syNetRbSnapCountTaruCannRiders(&live_rider_mask);
		sb32 live_shoot_anim =
		    (root != NULL) ? grJungleTaruCannIsChildShootAnimActive(dst->tarucann_gobj) : FALSE;
		const SYNetRbSnapGroundJungle *snap_jungle =
		    (payload_len >= (u16)SYNETRB_SNAP_GROUND_JUNGLE_V3_PAYLOAD_LEN) ? src : NULL;

		port_log(
		    "SSB64 NetRbSnapshot: jungle_tarucann_restore status=%u wait=%u mask=0x%02X root=%p child=%p tx=%f ty=%f rot=%f\n",
		    (unsigned int)dst->tarucann_status, (unsigned int)dst->tarucann_wait,
		    (unsigned int)src->tarucann_dobj_valid_mask, (void *)root,
		    (void *)((root != NULL) ? root->child : NULL),
		    (root != NULL) ? (f64)root->translate.vec.f.x : 0.0,
		    (root != NULL) ? (f64)root->translate.vec.f.y : 0.0,
		    (root != NULL) ? (f64)root->rotate.vec.f.z : 0.0);
		syNetRbSnapLogJungleTaruCannOccupancy(snap_tick, "restore", live_rider_count, live_rider_mask,
		                                      live_shoot_anim, snap_jungle);
	}
}

/*
 * syNetRbSnapResetParticlesForRollback() can hollow the barrel GObj (gobj->obj == NULL) while the
 * shell survives. Entering the cannon only needs ProcPhysics (guarded); firing calls
 * grJungleTaruCannAddAnimOffset (->child) and grJungleTaruCannGetRotate, which SIGSEGV on a hollow
 * tree. Re-establish the DObj tree from the init-time cache + snapshotted pose before the next sim
 * tick after every particle reset / ground restore.
 */
static void syNetRbSnapEnsureJungleTaruCannAfterParticleReset(const SYNetRbSnapshotSlot *slot)
{
	const SYNetRbSnapGroundJungle *src;

	if ((slot == NULL) || (slot->ground_captured == FALSE) || (slot->ground.gkind != nGRKindJungle))
	{
		return;
	}
	if (slot->ground.payload_len < (u16)SYNETRB_SNAP_GROUND_JUNGLE_LEGACY_PAYLOAD_LEN)
	{
		return;
	}
	src = (const SYNetRbSnapGroundJungle *)slot->ground.payload;
	syNetRbSnapRestoreJungleGround(src, slot->ground.payload_len, slot->tick);
}

/*
 * Capture the barrel cannon's full DObj anim runtime (root + child) so rollback resim reproduces the
 * Move-phase slide deterministically. Only meaningful on DK Jungle; a no-op (captured = FALSE) elsewhere.
 */
static void syNetRbSnapCaptureBarrel(SYNetRbSnapshotSlot *slot)
{
	GObj *tarucann_gobj;
	DObj *root;

	memset(&slot->barrel, 0, sizeof(slot->barrel));
	if ((gSCManagerBattleState == NULL) || (gSCManagerBattleState->gkind != nGRKindJungle))
	{
		return;
	}
	tarucann_gobj = grJungleGetTaruCannGobj();
	if (tarucann_gobj == NULL)
	{
		return;
	}
	root = DObjGetStruct(tarucann_gobj);
	if (root == NULL)
	{
		return;
	}
	slot->barrel.root_translate = root->translate.vec.f;
	slot->barrel.root_rotate = root->rotate.vec.f;
	syNetRbSnapCaptureDObjAnim(&slot->barrel.root_anim, root);
#if defined(SSB64_NETMENU)
	syNetplayQuantizeVec3f(&slot->barrel.root_translate);
	syNetplayQuantizeVec3f(&slot->barrel.root_rotate);
#endif
	if (root->child != NULL)
	{
		slot->barrel.has_child = TRUE;
		slot->barrel.child_translate = root->child->translate.vec.f;
		slot->barrel.child_rotate = root->child->rotate.vec.f;
		syNetRbSnapCaptureDObjAnim(&slot->barrel.child_anim, root->child);
#if defined(SSB64_NETMENU)
		syNetplayQuantizeVec3f(&slot->barrel.child_translate);
		syNetplayQuantizeVec3f(&slot->barrel.child_rotate);
#endif
	}
	slot->barrel.captured = TRUE;
}

/*
 * Restore the barrel anim runtime captured above. Runs after the stage / particle-reset repair so the
 * DObj tree is guaranteed live, and is authoritative over the legacy jungle-ground anim handling
 * (which no longer touches the barrel's anim cursor — see syNetRbSnapRestoreJungleGround).
 */
static void syNetRbSnapApplyBarrel(const SYNetRbSnapshotSlot *slot)
{
	GObj *tarucann_gobj;
	DObj *root;

	if ((slot == NULL) || (slot->barrel.captured == FALSE))
	{
		return;
	}
	tarucann_gobj = grJungleGetTaruCannGobj();
	if (tarucann_gobj == NULL)
	{
		return;
	}
	root = DObjGetStruct(tarucann_gobj);
	if (root == NULL)
	{
		return;
	}
	root->translate.vec.f = slot->barrel.root_translate;
	root->rotate.vec.f = slot->barrel.root_rotate;
	syNetRbSnapApplyDObjAnim(root, &slot->barrel.root_anim);
#if defined(SSB64_NETMENU)
	syNetplayQuantizeVec3f(&root->translate.vec.f);
	syNetplayQuantizeVec3f(&root->rotate.vec.f);
#endif
	if ((slot->barrel.has_child != FALSE) && (root->child != NULL))
	{
		root->child->translate.vec.f = slot->barrel.child_translate;
		root->child->rotate.vec.f = slot->barrel.child_rotate;
		syNetRbSnapApplyDObjAnim(root->child, &slot->barrel.child_anim);
#if defined(SSB64_NETMENU)
		syNetplayQuantizeVec3f(&root->child->translate.vec.f);
		syNetplayQuantizeVec3f(&root->child->rotate.vec.f);
#endif
	}
	if (syNetRbSnapJungleTaruCannDiagEnabled() != FALSE)
	{
		port_log(
		    "SSB64 NetRbSnapshot: jungle_barrel_anim_restore root=%p child=%p tx=%f ty=%f rotz=%f frame=%f wait=%f\n",
		    (void *)root, (void *)root->child, (f64)root->translate.vec.f.x, (f64)root->translate.vec.f.y,
		    (f64)root->rotate.vec.f.z, (f64)slot->barrel.root_anim.anim_frame,
		    (f64)slot->barrel.root_anim.anim_wait);
	}
}

/*
 * Capture the full Yamabuki gate DObj tree anim runtime (cursor + AObj chain per node), walked in the
 * same order as gcAddAnimJointAll / gcPlayAnimAll so the apply walk lines up index-for-index. Runs at
 * snapshot capture (post sim step).
 */
static void syNetRbSnapCaptureYamabukiGate(SYNetRbSnapshotSlot *slot)
{
	GObj *gate_gobj;
	DObj *dobj;
	u8 count;

	memset(&slot->yamabuki_gate, 0, sizeof(slot->yamabuki_gate));
	if ((gSCManagerBattleState == NULL) || (gSCManagerBattleState->gkind != nGRKindYamabuki))
	{
		return;
	}
	gate_gobj = gGRCommonStruct.yamabuki.gate_gobj;
	if (gate_gobj == NULL)
	{
		return;
	}
	count = 0U;
	for (dobj = DObjGetStruct(gate_gobj);
	     (dobj != NULL) && (count < SYNETRB_SNAP_YAMABUKI_GATE_DOBJ_MAX);
	     dobj = gcGetTreeDObjNext(dobj))
	{
		syNetRbSnapCaptureDObjAnim(&slot->yamabuki_gate.dobj_anim[count], dobj);
		count++;
	}
	slot->yamabuki_gate.dobj_count = count;
	slot->yamabuki_gate.captured = TRUE;
}

/*
 * Restore the gate door anim runtime captured above. Runs after grYamabukiGateRestoreAfterRollback
 * has rebuilt any hollow DObj tree and re-seated the open/close joint (baseline), then overwrites the
 * per-DObj cursor + AObj chain with the exact captured state so the door mesh holds its true pose.
 */
static void syNetRbSnapApplyYamabukiGate(const SYNetRbSnapshotSlot *slot)
{
	GObj *gate_gobj;
	DObj *dobj;
	u8 count;

	if ((slot == NULL) || (slot->yamabuki_gate.captured == FALSE))
	{
		return;
	}
	gate_gobj = gGRCommonStruct.yamabuki.gate_gobj;
	if (gate_gobj == NULL)
	{
		return;
	}
	count = 0U;
	for (dobj = DObjGetStruct(gate_gobj);
	     (dobj != NULL) && (count < slot->yamabuki_gate.dobj_count);
	     dobj = gcGetTreeDObjNext(dobj))
	{
		syNetRbSnapApplyDObjAnim(dobj, &slot->yamabuki_gate.dobj_anim[count]);
		if (dobj->anim_joint.event32 != NULL)
		{
			gcApplyDObjAnimJointPoseAtFrame(
			    dobj, dobj->anim_frame, (dobj->anim_wait == AOBJ_ANIM_NULL) ? TRUE : FALSE);
		}
		count++;
	}
}

static sb32 syNetRbSnapSectorArwingDiagEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_SNAPSHOT_ARWING_DIAG");
	s_env_cache = (e != NULL && e[0] != '\0' && atoi(e) != 0) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}

/*
 * A/B disambiguation switch (default OFF = current behavior). When set, syNetRbSnapApplyArwing skips
 * the per-DObj anim-cursor restore on the arwing visual tree (still rebuilds a hollow tree and syncs
 * map_gobj visibility). If the live flight anim then advances (capture probe shows anim_wait counting
 * down and patrol completing), the anim apply was pinning dobj[0] to frame 0 (Phase-5c class). If it
 * still freezes at the initial wait, the live gcPlayAnimAll step never advances dobj[0] in netplay —
 * a live-sim bug independent of snapshotting.
 */
static sb32 syNetRbSnapSectorArwingNoAnimApplyEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_SNAPSHOT_ARWING_NO_ANIM_APPLY");
	s_env_cache = (e != NULL && e[0] != '\0' && atoi(e) != 0) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}

static void syNetRbSnapCaptureArwing(SYNetRbSnapshotSlot *slot)
{
	GRCommonGroundVarsSector *sec;
	u32 di;

	memset(&slot->arwing, 0, sizeof(slot->arwing));
	if ((gSCManagerBattleState == NULL) || (gSCManagerBattleState->gkind != nGRKindSector))
	{
		return;
	}
	sec = &gGRCommonStruct.sector;
	if (sec->map_gobj == NULL)
	{
		return;
	}
	slot->arwing.map_gobj_flags = sec->map_gobj->flags;
	if (sec->arwing_last_flight_pattern >= 0)
	{
		slot->arwing.flight_pattern_idx = sec->arwing_last_flight_pattern;
	}
	else
	{
		slot->arwing.flight_pattern_idx = grSectorInferFlightPatternIdx();
	}
	for (di = 0; di < SYNETRB_SNAP_ARWING_DOBJ_NUM; di++)
	{
		if (sec->map_dobjs[di] != NULL)
		{
			slot->arwing.dobj_valid_mask |= (u16)(1U << di);
			syNetRbSnapCaptureDObjAnim(&slot->arwing.dobj_anim[di], sec->map_dobjs[di]);
			slot->arwing.dobj_translate[di] = sec->map_dobjs[di]->translate.vec.f;
			slot->arwing.dobj_rotate[di] = sec->map_dobjs[di]->rotate.vec.f;
#if defined(SSB64_NETMENU)
			syNetplayQuantizeVec3f(&slot->arwing.dobj_translate[di]);
			syNetplayQuantizeVec3f(&slot->arwing.dobj_rotate[di]);
#endif
		}
	}
	slot->arwing.captured = TRUE;

	/*
	 * Live-step probe: capture runs once per completed sim tick (post-step). Log only when dobj[0]'s
	 * anim cursor *changed* since the previous capture, so a quiet log == the live flight anim is frozen.
	 * This is independent of whether an apply ran this tick, which is what disambiguates pin-vs-freeze.
	 */
	if (syNetRbSnapSectorArwingDiagEnabled() != FALSE)
	{
		static f32 s_last_wait;
		static f32 s_last_frame;
		static sb32 s_have_last;
		static u8 s_last_status = 0xFFU;
		DObj *d0 = sec->map_dobjs[0];

		if (d0 != NULL)
		{
			if ((s_have_last == FALSE) || (d0->anim_wait != s_last_wait) || (d0->anim_frame != s_last_frame) ||
			    (sec->arwing_status != s_last_status))
			{
				GRSectorArwingPresentationDiag diag;

				grSectorArwingFillPresentationDiag(&diag);
				port_log("SSB64 NetRbSnapshot: sector_arwing_live_step status=%u wait %f->%f frame %f->%f "
				         "speed=%f aobj=%d gflags=0x%X tx=%f ty=%f d0f=0x%X d1f=0x%X mobj=%d "
				         "root_eq=%d draw=%d dl0=%d dlm=%d nodes=%u disp=%p dllk=%u lfp=%d appear=%u\n",
				         (unsigned int)sec->arwing_status, (f64)s_last_wait, (f64)d0->anim_wait, (f64)s_last_frame,
				         (f64)d0->anim_frame, (f64)d0->anim_speed, (d0->aobj != NULL) ? 1 : 0,
				         (d0->parent_gobj != NULL) ? (unsigned int)d0->parent_gobj->flags : 0xFFFFFFFFU,
				         (f64)d0->translate.vec.f.x, (f64)d0->translate.vec.f.y, (unsigned int)d0->flags,
				         (sec->map_dobjs[1] != NULL) ? (unsigned int)sec->map_dobjs[1]->flags : 0U,
				         (d0->mobj != NULL) ? 1 : 0, (int)diag.root_matches_d0, (int)diag.drawable_dobj_count,
				         (int)diag.dl_valid_root, (int)diag.dl_valid_mesh, (unsigned int)diag.tree_child_count,
				         diag.proc_display, (unsigned int)diag.dl_link_id, (int)sec->arwing_last_flight_pattern,
				         (unsigned int)sec->arwing_appear_timer);
				s_last_wait = d0->anim_wait;
				s_last_frame = d0->anim_frame;
				s_last_status = sec->arwing_status;
				s_have_last = TRUE;
			}
		}
	}
}

static void syNetRbSnapApplyArwing(const SYNetRbSnapshotSlot *slot)
{
	GRCommonGroundVarsSector *sec;
	GObj *map_gobj;
	s8 flight_pattern_idx;
	sb32 tree_was_reestablished;
	u32 di;

	if ((slot == NULL) || (slot->arwing.captured == FALSE))
	{
		return;
	}
	if ((gSCManagerBattleState == NULL) || (gSCManagerBattleState->gkind != nGRKindSector))
	{
		return;
	}
	sec = &gGRCommonStruct.sector;
	map_gobj = sec->map_gobj;
	if (map_gobj == NULL)
	{
		return;
	}
	tree_was_reestablished = grSectorReestablishArwingVisualTree();
	flight_pattern_idx = slot->arwing.flight_pattern_idx;
	if (flight_pattern_idx < 0)
	{
		flight_pattern_idx = grSectorInferFlightPatternIdx();
	}
	if (syNetRbSnapSectorArwingNoAnimApplyEnabled() == FALSE)
	{
		for (di = 0; di < SYNETRB_SNAP_ARWING_DOBJ_NUM; di++)
		{
			if ((slot->arwing.dobj_valid_mask & (u16)(1U << di)) == 0U)
			{
				continue;
			}
			if (sec->map_dobjs[di] == NULL)
			{
				continue;
			}
			syNetRbSnapApplyDObjAnim(sec->map_dobjs[di], &slot->arwing.dobj_anim[di]);
		}
	}
	grSectorRepairArwingPresentation(tree_was_reestablished, flight_pattern_idx, slot->arwing.dobj_translate,
	                                 slot->arwing.dobj_rotate, slot->arwing.dobj_valid_mask);
	grSectorSyncArwingMapGObjFlags(slot->arwing.map_gobj_flags);
	if (syNetRbSnapSectorArwingDiagEnabled() != FALSE)
	{
		DObj *d0 = sec->map_dobjs[0];
		GRSectorArwingPresentationDiag diag;

		grSectorArwingFillPresentationDiag(&diag);
		port_log(
		    "SSB64 NetRbSnapshot: sector_arwing_restore map_gobj=%p status=%u flags=0x%X d0=%p frame=%f wait=%f "
		    "tx=%f ty=%f d0f=0x%X d1f=0x%X mobj=%d fp=%d lfp=%d reest=%d root_eq=%d draw=%d dl0=%d dlm=%d "
		    "nodes=%u disp=%p dllk=%u no_anim=%d appear=%u\n",
		    (void *)map_gobj, (unsigned int)sec->arwing_status, (unsigned int)map_gobj->flags, (void *)d0,
		    (d0 != NULL) ? (f64)d0->anim_frame : 0.0, (d0 != NULL) ? (f64)d0->anim_wait : 0.0,
		    (d0 != NULL) ? (f64)d0->translate.vec.f.x : 0.0, (d0 != NULL) ? (f64)d0->translate.vec.f.y : 0.0,
		    (d0 != NULL) ? (unsigned int)d0->flags : 0U,
		    (sec->map_dobjs[1] != NULL) ? (unsigned int)sec->map_dobjs[1]->flags : 0U,
		    (d0 != NULL && d0->mobj != NULL) ? 1 : 0, (int)flight_pattern_idx,
		    (int)sec->arwing_last_flight_pattern, (int)tree_was_reestablished, (int)diag.root_matches_d0,
		    (int)diag.drawable_dobj_count, (int)diag.dl_valid_root, (int)diag.dl_valid_mesh,
		    (unsigned int)diag.tree_child_count, diag.proc_display, (unsigned int)diag.dl_link_id,
		    (int)syNetRbSnapSectorArwingNoAnimApplyEnabled(), (unsigned int)sec->arwing_appear_timer);
	}
	syNetRbSnapReconcileSectorArwingDeckYakumonoFromFlightTree();
#if defined(SSB64_NETMENU)
	syNetplayCanonicalizeSectorArwingIntroMapPose();
#endif
}

static void syNetRbSnapEnsureSectorArwingAfterParticleReset(const SYNetRbSnapshotSlot *slot)
{
	if ((slot == NULL) || (slot->ground_captured == FALSE) || (slot->ground.gkind != nGRKindSector))
	{
		return;
	}
	if (slot->ground.payload_len < SYNETRB_SNAP_GROUND_SECTOR_V1_PAYLOAD_LEN)
	{
		return;
	}
#if defined(SSB64_NETMENU)
	/*
	 * Synctest probe loads an old ring slot then emergency-restores live. ApplyArwing mutates the live
	 * flight DObj tree (~one patrol frame behind current live) and is not reliably reversed by restore
	 * once gcPlayDObjAnimJoint has re-derived translate from the spline. Mirror Hyrule twister / Yamabuki
	 * gate verify-only skip; syNetRbSnapApplyGround in ApplySlotToLive still supplies sim scalars for hash
	 * verify during probes that reach finalize without the live patrol skip gates above.
	 */
	if (s_syNetRbSnapRepairStageVerifyOnly != FALSE)
	{
		if (syNetRbSnapSectorArwingDiagEnabled() != FALSE)
		{
			port_log("SSB64 NetRbSnapshot: sector_arwing_repair tick=%u reason=verify_skip_all\n",
			         (unsigned int)slot->tick);
		}
		return;
	}
	/* ApplyGround + ApplyArwing + reconcile are not idempotent; run once per slot load (finalize). */
	if (slot->tick == s_syNetRbSnapSectorArwingRepairTick)
	{
		return;
	}
	s_syNetRbSnapSectorArwingRepairTick = slot->tick;
#endif
	syNetRbSnapApplyGround(slot);
	syNetRbSnapApplyArwing(slot);
	syNetRbSnapRefreshSectorArwingDeckFighterPlatformCoupling();
}

void syNetRbSnapResetSectorArwingRepairDedup(void)
{
#if defined(SSB64_NETMENU)
	s_syNetRbSnapSectorArwingRepairTick = UINT32_MAX;
#endif
}
#endif

static void syNetRbSnapRestoreYosterCloudPresentation(s32 cloud_id, const SYNetRbSnapGroundYosterCloud *sc,
                                                      GRYosterCloud *lc)
{
	MObj *mobj;

	if ((lc == NULL) || (cloud_id < 0) || (cloud_id >= 3))
	{
		return;
	}
	grYosterRepairCloudPresentation(cloud_id);
	if (lc->gobj == NULL)
	{
		return;
	}
	if (DObjGetStruct(lc->gobj) == NULL)
	{
		return;
	}
	if ((sc != NULL) && ((sc->dobj_valid_mask & 1U) != 0U) && (lc->dobj[0] != NULL) &&
	    ((mobj = lc->dobj[0]->mobj) != NULL))
	{
		memcpy(&mobj->anim_wait, &sc->dobj0_anim_wait_bits, sizeof(mobj->anim_wait));
		if (mobj->anim_wait == AOBJ_ANIM_END)
		{
			mobj->anim_wait = AOBJ_ANIM_NULL;
		}
	}
	grYosterAnchorCloudRootTranslate(cloud_id);
}

static void syNetRbSnapEnsureYosterCloudsAfterParticleReset(const SYNetRbSnapshotSlot *slot)
{
	const SYNetRbSnapGroundBlob *ground;
	GRCommonGroundVarsYoster *dst;
	sb32 has_extended_blob;
	s32 ci;

	if ((slot == NULL) || (slot->ground_captured == FALSE) || (slot->ground.gkind != nGRKindYoster))
	{
		return;
	}
	ground = &slot->ground;
	if ((ground->payload_len < (u16)sizeof(SYNetRbSnapGroundYoster)) &&
	    (ground->payload_len < SYNETRB_SNAP_GROUND_YOSTER_LEGACY_PAYLOAD_LEN))
	{
		return;
	}
	dst = &gGRCommonStruct.yoster;
	has_extended_blob = (ground->payload_len >= (u16)sizeof(SYNetRbSnapGroundYoster)) ? TRUE : FALSE;
	for (ci = 0; ci < 3; ci++)
	{
		GRYosterCloud *lc = &dst->clouds[ci];
		const SYNetRbSnapGroundYosterCloud *sc = NULL;

		if (has_extended_blob != FALSE)
		{
			const SYNetRbSnapGroundYoster *src_full = (const SYNetRbSnapGroundYoster *)ground->payload;

			sc = &src_full->clouds[ci];
			lc->is_cloud_line_active = sc->is_cloud_line_active;
		}
		else
		{
			const SYNetRbSnapGroundYosterCloudLegacy *legacy_sc =
			    &((const SYNetRbSnapGroundYosterCloudLegacy *)ground->payload)[ci];

			lc->is_cloud_line_active = legacy_sc->is_cloud_line_active;
		}
		syNetRbSnapRestoreYosterCloudPresentation(ci, sc, lc);
	}
}

/*
 * Tower Pokémon are stage items on the item link. Like the Castle bumper, a captured
 * monster_gobj id can alias an unrelated item after eject; prefer the live ground-monster scan.
 */
static GObj *syNetRbSnapFindLiveYamabukiMonsterGObj(void)
{
	GObj *gobj;

	for (gobj = gGCCommonLinks[nGCCommonLinkIDItem]; gobj != NULL; gobj = gobj->link_next)
	{
		ITStruct *ip = itGetStruct(gobj);

		if ((ip != NULL) && (syNetRbSnapItemIsGroundMonster(ip->kind) != FALSE))
		{
			return gobj;
		}
	}
	return NULL;
}

static GObj *syNetRbSnapResolveYamabukiMonsterGObj(const SYNetRbSnapGroundYamabuki *src)
{
	GObj *monster_gobj;

	monster_gobj = syNetRbSnapFindLiveYamabukiMonsterGObj();
	if (monster_gobj != NULL)
	{
		return monster_gobj;
	}
	if ((src == NULL) || (src->monster_gobj_id == 0U))
	{
		return NULL;
	}
	monster_gobj = gcFindGObjByID(src->monster_gobj_id);
	if (monster_gobj != NULL)
	{
		ITStruct *ip = itGetStruct(monster_gobj);

		if ((ip == NULL) || (syNetRbSnapItemIsGroundMonster(ip->kind) == FALSE))
		{
			monster_gobj = NULL;
		}
	}
	return monster_gobj;
}

static void syNetRbSnapEnsureYamabukiGateAfterParticleReset(const SYNetRbSnapshotSlot *slot)
{
#ifdef PORT
	static u32 s_syNetRbSnapYamabukiGateRepairTick = UINT32_MAX;
#endif
	const SYNetRbSnapGroundYamabuki *src;
	GRCommonGroundVarsYamabuki *dst;
	sb32 has_gate_anim;
	f32 gate_anim_frame;
	f32 gate_anim_wait;
	u8 gate_anim_phase;

	if ((slot == NULL) || (slot->ground_captured == FALSE) || (slot->ground.gkind != nGRKindYamabuki))
	{
		return;
	}
	if (slot->ground.payload_len < (u16)offsetof(SYNetRbSnapGroundYamabuki, gate_anim_frame))
	{
		return;
	}
#ifdef PORT
	if (slot->tick == s_syNetRbSnapYamabukiGateRepairTick)
	{
		return;
	}
	s_syNetRbSnapYamabukiGateRepairTick = slot->tick;
	/*
	 * ANIMATION FIX (synctest-only door freeze): the periodic synctest probe captures a live
	 * emergency snapshot, loads an OLD probe slot, verifies its hash, then restores the emergency
	 * snapshot (see syNetRollbackUpdate). During that probe load `s_syNetRbSnapRepairStageVerifyOnly`
	 * is TRUE. This repair is presentation-only and heavily mutates the LIVE gate DObj tree —
	 * grYamabukiGateRestoreAfterRollback can rebuild the tree (gcRemoveDObjAll + gcSetupCustomDObjs,
	 * changing DObj pointers) and re-seat the door to the OLD probe pose, and the emergency restore
	 * does not reliably reverse it once the gate has latched OpenHeld (MeshIsHeldOpen trusts the
	 * restored anim_frame scalar while translate stays at the rebuilt closed pose). Net effect: with
	 * SSB64_NETPLAY_ROLLBACK_SYNCTEST=1 the tower door never visibly opens; offline / synctest=0 it
	 * opens fine. Mirror the Hyrule twister repair and skip the live-tree mutation during verify-only.
	 *
	 * Hash-safe: every gate field folded into world= (gate_status/noentry/waits/gate_pos/phase/
	 * monster id) is applied by syNetRbSnapApplyGround, which still runs; only the live child DObj
	 * anim_frame/anim_wait are sourced here, and those are static outside the ~9-tick open/close
	 * windows (already inside the yamabuki_gate synctest-skip window), so verify precision is intact.
	 */
	if (s_syNetRbSnapRepairStageVerifyOnly != FALSE)
	{
		const char *e = getenv("SSB64_NETPLAY_YAMABUKI_GATE_DIAG");

		if ((e != NULL) && (e[0] != '\0') && (strcmp(e, "0") != 0))
		{
			port_log("SSB64 NetRbSnapshot: yamabuki_gate_repair tick=%u reason=verify_skip_all\n",
			         (unsigned int)slot->tick);
		}
		return;
	}
#endif
	src = (const SYNetRbSnapGroundYamabuki *)slot->ground.payload;
	dst = &gGRCommonStruct.yamabuki;
#ifdef PORT
	/*
	 * Prefer the live-link resolver over the shared-id lookup: gate_gobj_id == nGCCommonKindGround is
	 * not unique, so gcFindGObjByID can bind the bare ground controller and the door anim then runs on
	 * an invisible GObj (static-closed-door bug). See grYamabukiGateResolveLiveGObj.
	 */
	{
		GObj *live_gate = grYamabukiGateResolveLiveGObj();

		if (live_gate != NULL)
		{
			dst->gate_gobj = live_gate;
		}
		else if ((dst->gate_gobj == NULL) && (src->gate_gobj_id != 0U))
		{
			dst->gate_gobj = gcFindGObjByID(src->gate_gobj_id);
		}
	}
#else
	if (dst->gate_gobj == NULL && src->gate_gobj_id != 0U)
	{
		dst->gate_gobj = gcFindGObjByID(src->gate_gobj_id);
	}
#endif
	dst->monster_gobj = syNetRbSnapResolveYamabukiMonsterGObj(src);
	if ((dst->gate_status == (u8)nGRYamabukiGateStatusOpen) && (dst->monster_gobj != NULL) &&
	    (itGetStruct(dst->monster_gobj) != NULL))
	{
		dst->gate_noentry = FALSE;
	}
	else if ((dst->gate_status == (u8)nGRYamabukiGateStatusWait) && (dst->gate_pos.x >= 1280.0F))
	{
		dst->gate_noentry = FALSE;
	}
	has_gate_anim = (slot->ground.payload_len >= (u16)sizeof(SYNetRbSnapGroundYamabuki)) ? TRUE : FALSE;
	gate_anim_frame = 0.0F;
	gate_anim_wait = 0.0F;
	gate_anim_phase = (u8)nGRYamabukiGateAnimPhaseClosed;
	if (has_gate_anim != FALSE)
	{
		gate_anim_frame = src->gate_anim_frame;
		gate_anim_wait = src->gate_anim_wait;
		gate_anim_phase = src->gate_anim_phase;
#if defined(SSB64_NETMENU)
		gate_anim_frame = syNetplayQuantizeAnimScalar(gate_anim_frame);
		gate_anim_wait = syNetplayQuantizeAnimScalar(gate_anim_wait);
#endif
	}
#ifdef PORT
	grYamabukiGateRestoreAfterRollback(gate_anim_frame, gate_anim_wait, gate_anim_phase, has_gate_anim);
	/* Overwrite the re-seated baseline with the exact captured per-DObj anim runtime (door mesh pose). */
	syNetRbSnapApplyYamabukiGate(slot);
	grYamabukiGateFinalizeAfterSnapshotRestore();
#endif
}

/*
 * The Peach's Castle bumper is a singleton stage-hazard item (nITKindGBumper)
 * living on the item link. grCastleBumperProcUpdate dereferences
 * gGRCommonStruct.castle.bumper_gobj every tick, so a stale pointer there is a
 * hard crash (SIGSEGV fault 0x38 = NULL DObj.translate). gcFindGObjByID is
 * unsafe for re-resolving it because gobj ids are reused after eject — a
 * captured bumper id can resolve to an unrelated transient item that the
 * item-apply phase then ejects, leaving the pointer dangling. Re-derive the
 * bumper from the live item list (authoritative) and accept the captured id
 * only when it still validates as the bumper.
 */
static GObj *syNetRbSnapFindLiveCastleBumperGObj(void)
{
	GObj *gobj;

	for (gobj = gGCCommonLinks[nGCCommonLinkIDItem]; gobj != NULL; gobj = gobj->link_next)
	{
		ITStruct *ip = itGetStruct(gobj);

		if ((ip != NULL) && (ip->kind == nITKindGBumper))
		{
			return gobj;
		}
	}
	return NULL;
}

static GObj *syNetRbSnapResolveCastleBumperGObj(const SYNetRbSnapGroundCastle *src)
{
	GObj *bumper_gobj;

	bumper_gobj = syNetRbSnapFindLiveCastleBumperGObj();
	if (bumper_gobj != NULL)
	{
		return bumper_gobj;
	}
	if ((src == NULL) || (src->bumper_gobj_id == 0U))
	{
		return NULL;
	}
	bumper_gobj = gcFindGObjByID(src->bumper_gobj_id);
	if (bumper_gobj != NULL)
	{
		ITStruct *ip = itGetStruct(bumper_gobj);

		if ((ip == NULL) || (ip->kind != nITKindGBumper))
		{
			bumper_gobj = NULL;
		}
	}
	return bumper_gobj;
}

#ifdef PORT
/*
 * Tower-monster flames (Charmander 0x1E / Charizard 0x19) are collision-only weapons whose only visual is
 * lbParticles spawned once at creation; syNetRbSnapResetParticlesForRollback wipes them on every load with
 * no rebuild, so after a rollback the flame keeps hitting fighters (the weapon GObj is restored by
 * syNetRbSnapApplyWeapons) while rendering nothing. Re-emit each live flame's particle at its restored
 * pose. Hash-safe: particles use the cosmetic RNG and are not folded into any rollback hash (wpn=/eff= are
 * FNV-empty). The other monster projectiles (rock/coin/spear/hydro/swift/smog/razor) render via their own
 * meshes/particles and are intentionally left alone.
 */
static void syNetRbSnapEnsureMonsterFlameParticlesAfterParticleReset(void)
{
	GObj *weapon_gobj;

	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp = wpGetStruct(weapon_gobj);

		if ((wp != NULL) &&
		    ((wp->kind == nWPKindHitokageFlame) || (wp->kind == nWPKindLizardonFlame)))
		{
			itHitokageReemitFlameParticles(weapon_gobj);
		}
	}
}
#endif

static void syNetRbSnapEnsureCastleBumperAfterParticleReset(const SYNetRbSnapshotSlot *slot)
{
	const SYNetRbSnapGroundCastle *src;
	GRCommonGroundVarsCastle *dst;
	DObj *bumper_dobj;

	if ((slot == NULL) || (slot->ground_captured == FALSE) || (slot->ground.gkind != nGRKindCastle))
	{
		return;
	}
	if (slot->ground.payload_len < (u16)sizeof(SYNetRbSnapGroundCastle))
	{
		return;
	}
	src = (const SYNetRbSnapGroundCastle *)slot->ground.payload;
	dst = &gGRCommonStruct.castle;
	dst->bumper_gobj = syNetRbSnapResolveCastleBumperGObj(src);
	if (dst->bumper_gobj == NULL)
	{
		return;
	}
	bumper_dobj = DObjGetStruct(dst->bumper_gobj);
	if (bumper_dobj != NULL)
	{
		bumper_dobj->translate.vec.f = src->bumper_pos;
	}
}

static void syNetRbSnapRepairStageAfterParticleResetInternal(const SYNetRbSnapshotSlot *slot,
                                                             sb32 include_sector_arwing)
{
	if ((slot == NULL) || (slot->ground_captured == FALSE))
	{
		return;
	}
	switch (slot->ground.gkind)
	{
	case nGRKindHyrule:
		syNetRbSnapEnsureHyruleTwisterAfterParticleReset(slot);
#ifdef PORT
		syNetRbSnapResyncFighterTwisterGobjs(slot->tick);
#endif
		break;
	case nGRKindPupupu:
		syNetRbSnapEnsurePupupuWhispyAfterParticleReset(slot);
		break;
	case nGRKindYoster:
		syNetRbSnapEnsureYosterCloudsAfterParticleReset(slot);
		break;
#ifdef PORT
	case nGRKindJungle:
		syNetRbSnapEnsureJungleTaruCannAfterParticleReset(slot);
		syNetRbSnapApplyBarrel(slot);
		syNetRbSnapResyncFighterTaruCannGobjs(slot->tick);
		break;
#endif
	case nGRKindCastle:
		syNetRbSnapEnsureCastleBumperAfterParticleReset(slot);
		break;
#ifdef PORT
	case nGRKindSector:
		if (include_sector_arwing != FALSE)
		{
			syNetRbSnapEnsureSectorArwingAfterParticleReset(slot);
		}
		break;
#endif
	default:
		break;
	}
}

void syNetRbSnapRepairStageAfterParticleReset(const SYNetRbSnapshotSlot *slot)
{
	syNetRbSnapRepairStageAfterParticleResetInternal(slot, TRUE);
}

void syNetRbSnapRepairStageAfterParticleResetForTick(u32 tick)
{
	SYNetRbSnapshotSlot *slot;

	slot = syNetRbSnapshotSlotForTick(tick);
	if ((slot == NULL) || (slot->is_valid == FALSE) || (slot->tick != tick))
	{
		return;
	}
	syNetRbSnapRepairStageAfterParticleReset(slot);
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

static sb32 syNetRbSnapReconciledEffectGobjIdListed(const u32 *reconciled_ids, s32 reconciled_count, u32 gobj_id)
{
	s32 ri;

	if ((reconciled_ids == NULL) || (gobj_id == 0U))
	{
		return FALSE;
	}
	for (ri = 0; ri < reconciled_count; ri++)
	{
		if (reconciled_ids[ri] == gobj_id)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapLiveEffectKeptAfterReconcile(const SYNetRbSnapshotSlot *slot, u32 gobj_id,
                                                    const u32 *reconciled_ids, s32 reconciled_count)
{
	if (syNetRbSnapLiveEffectListedInSnapshot(slot, gobj_id) != FALSE)
	{
		return TRUE;
	}
	return syNetRbSnapReconciledEffectGobjIdListed(reconciled_ids, reconciled_count, gobj_id);
}

static void syNetRbSnapTrackReconciledEffectGobj(u32 *reconciled_ids, s32 *reconciled_count, GObj *gobj)
{
	s32 count;

	if ((reconciled_ids == NULL) || (reconciled_count == NULL) || (gobj == NULL) || (gobj->id == 0U))
	{
		return;
	}
	count = *reconciled_count;
	if (count >= SYNETRB_SNAPSHOT_MAX_EFFECTS)
	{
		return;
	}
	if (syNetRbSnapReconciledEffectGobjIdListed(reconciled_ids, count, gobj->id) != FALSE)
	{
		return;
	}
	reconciled_ids[count] = gobj->id;
	*reconciled_count = count + 1;
}

static s32 syNetRbSnapPlayerForFighterGobjId(const SYNetRbSnapshotSlot *slot, u32 fighter_gobj_id)
{
	s32 pi;

	if (fighter_gobj_id == 0U)
	{
		return -1;
	}
	if (slot != NULL)
	{
		for (pi = 0; pi < GMCOMMON_PLAYERS_MAX; pi++)
		{
			const SYNetRbSnapFighterBlob *fb = &slot->fighters[pi];

			if ((fb->is_valid != FALSE) && (fb->gobj_id == fighter_gobj_id))
			{
				return pi;
			}
		}
	}
	{
		GObj *fg = gcFindGObjByID(fighter_gobj_id);
		FTStruct *fp;

		if ((fg != NULL) && ((fp = ftGetStruct(fg)) != NULL))
		{
			return fp->player;
		}
	}
	return -1;
}

static sb32 syNetRbSnapLiveEffectMatchesBlob(const SYNetRbSnapshotSlot *slot, const SYNetRbSnapEffectBlob *blob,
                                             GObj *gobj, EFStruct *ep)
{
	s32 blob_player;
	FTStruct *fp_live;

	if ((blob == NULL) || (gobj == NULL))
	{
		return FALSE;
	}
	if ((blob->snap_flags & SYNETRB_EFFECT_SNAP_NO_STRUCT) != 0U)
	{
		if (ep != NULL)
		{
			return FALSE;
		}
		if ((gobj->link_id != blob->link_id) || (gobj->obj_kind != nGCCommonKindEffect))
		{
			return FALSE;
		}
		if ((blob->proc_update_fingerprint != 0U) &&
		    (syNetRbSnapGObjFuncProcFingerprint(gobj) != blob->proc_update_fingerprint))
		{
			return FALSE;
		}
		return TRUE;
	}
	if (ep == NULL)
	{
		return FALSE;
	}
	if (blob->respawn_kind == SYNETRB_EFFECT_RESPAWN_QUAKE)
	{
		if (syNetRbSnapLiveEffectIsQuake(gobj, ep) == FALSE)
		{
			return FALSE;
		}
		if (blob->quake_magnitude != 0xFFU)
		{
			u8 live_mag = (u8)(3 - ep->effect_vars.quake.priority);

			if (live_mag != blob->quake_magnitude)
			{
				return FALSE;
			}
		}
		if ((blob->proc_update_fingerprint != 0U) &&
		    (syNetRbSnapGObjFuncProcFingerprint(gobj) != blob->proc_update_fingerprint))
		{
			return FALSE;
		}
		return TRUE;
	}
	if (ep->bank_id != blob->bank_id)
	{
		return FALSE;
	}
	if ((blob->respawn_kind != SYNETRB_EFFECT_RESPAWN_NONE) &&
	    (blob->respawn_kind != syNetRbSnapEffectRespawnKindFromLive(gobj, ep)))
	{
		return FALSE;
	}
	if (blob->fighter_gobj_id == 0U)
	{
		if (ep->fighter_gobj != NULL)
		{
			return FALSE;
		}
	}
	else
	{
		if (ep->fighter_gobj == NULL)
		{
			return FALSE;
		}
		fp_live = ftGetStruct(ep->fighter_gobj);
		if (fp_live == NULL)
		{
			return FALSE;
		}
		blob_player = syNetRbSnapPlayerForFighterGobjId(slot, blob->fighter_gobj_id);
		if ((blob_player >= 0) && (fp_live->player == blob_player))
		{
			; /* player slot match — ring id stale after emergency / pool reuse */
		}
		else if (ep->fighter_gobj->id == blob->fighter_gobj_id)
		{
			; /* captured parent id still live */
		}
		else
		{
			return FALSE;
		}
	}
	if ((blob->proc_update_fingerprint != 0U) &&
	    (syNetRbSnapPointerFingerprintLow32((const void *)ep->proc_update) != blob->proc_update_fingerprint))
	{
		if ((syNetRbSnapLiveEffectIsQuake(gobj, ep) == FALSE) ||
		    (syNetRbSnapGObjFuncProcFingerprint(gobj) != blob->proc_update_fingerprint))
		{
			return FALSE;
		}
	}
	return TRUE;
}

static sb32 syNetRbSnapLiveEffectMatchesAnyBlobInSlot(const SYNetRbSnapshotSlot *slot, GObj *gobj, EFStruct *ep)
{
	s32 ei;

	if (slot == NULL)
	{
		return FALSE;
	}
	for (ei = 0; ei < slot->effect_count; ei++)
	{
		if (syNetRbSnapLiveEffectMatchesBlob(slot, &slot->effects[ei], gobj, ep) != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static void syNetRbSnapReapplyEffectBlobsFromSlot(const SYNetRbSnapshotSlot *slot)
{
	s32 ei;

	if (slot == NULL)
	{
		return;
	}
	for (ei = 0; ei < slot->effect_count; ei++)
	{
		const SYNetRbSnapEffectBlob *blob;
		GObj *gobj;
		GObj *match;
		s32 pass;

		blob = &slot->effects[ei];
		if (blob->is_valid == FALSE)
		{
			continue;
		}
		gobj = gcFindGObjByID(blob->gobj_id);
		if ((gobj != NULL) && (syNetRbSnapEffectHiddenFromRollback(gobj, efGetStruct(gobj)) == FALSE))
		{
			(void)syNetRbSnapApplyEffectBlobToGObj(slot, gobj, blob);
			continue;
		}
		match = NULL;
		for (pass = 0; pass < 2; pass++)
		{
			GObj *link_head;

			link_head = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
			for (gobj = link_head; gobj != NULL; gobj = gobj->link_next)
			{
				EFStruct *ep;

				ep = efGetStruct(gobj);
				if (syNetRbSnapEffectHiddenFromRollback(gobj, ep) != FALSE)
				{
					continue;
				}
				if (syNetRbSnapLiveEffectMatchesBlob(slot, blob, gobj, ep) == FALSE)
				{
					continue;
				}
				match = gobj;
				break;
			}
			if (match != NULL)
			{
				break;
			}
		}
		if (match != NULL)
		{
			(void)syNetRbSnapApplyEffectBlobToGObj(slot, match, blob);
		}
	}
}

static GObj *syNetRbSnapFindUnreconciledLiveEffectForBlob(const SYNetRbSnapshotSlot *slot,
                                                          const SYNetRbSnapEffectBlob *blob,
                                                          const u32 *reconciled_ids, s32 reconciled_count)
{
	s32 pass;
	GObj *gobj;
	GObj *match;
	s32 match_count;

	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return NULL;
	}
	match = NULL;
	match_count = 0;
	for (pass = 0; pass < 2; pass++)
	{
		GObj *link_head;

		link_head = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		for (gobj = link_head; gobj != NULL; gobj = gobj->link_next)
		{
			EFStruct *ep;

			if (syNetRbSnapReconciledEffectGobjIdListed(reconciled_ids, reconciled_count, gobj->id) != FALSE)
			{
				continue;
			}
			ep = efGetStruct(gobj);
			if (syNetRbSnapEffectHiddenFromRollback(gobj, ep) != FALSE)
			{
				continue;
			}
			if (syNetRbSnapLiveEffectMatchesBlob(slot, blob, gobj, ep) == FALSE)
			{
				continue;
			}
			match = gobj;
			match_count++;
		}
	}
	return (match_count == 1) ? match : NULL;
}

static sb32 syNetRbSnapFighterInFoxReflectorScope(const FTStruct *fp)
{
	if ((fp == NULL) || (fp->fkind != nFTKindFox))
	{
		return FALSE;
	}
	if (((fp->status_id >= nFTFoxStatusSpecialLwScopeStart) && (fp->status_id <= nFTFoxStatusSpecialLwScopeEnd)) ||
	    ((fp->status_id >= nFTFoxStatusSpecialAirLwStart) && (fp->status_id <= nFTFoxStatusSpecialAirLwTurn)))
	{
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapLiveHasFoxReflectorScope(void)
{
	return syNetRbSnapshotAnyFighterFoxReflectorScopeActive();
}

static const SYNetRbSnapEffectBlob *syNetRbSnapFindEffectBlobByGobjId(const SYNetRbSnapshotSlot *slot, u32 gobj_id)
{
	s32 ei;

	if ((slot == NULL) || (gobj_id == 0U))
	{
		return NULL;
	}
	for (ei = 0; ei < slot->effect_count; ei++)
	{
		const SYNetRbSnapEffectBlob *blob = &slot->effects[ei];

		if ((blob->is_valid != FALSE) && (blob->gobj_id == gobj_id))
		{
			return blob;
		}
	}
	return NULL;
}

static const SYNetRbSnapEffectBlob *syNetRbSnapFindShieldEffectBlobForPlayer(const SYNetRbSnapshotSlot *slot,
                                                                             s32 player)
{
	s32 ei;

	if ((slot == NULL) || (player < 0) || (player >= GMCOMMON_PLAYERS_MAX))
	{
		return NULL;
	}
	for (ei = 0; ei < slot->effect_count; ei++)
	{
		const SYNetRbSnapEffectBlob *blob = &slot->effects[ei];

		if (blob->is_valid == FALSE)
		{
			continue;
		}
		if ((blob->respawn_kind != SYNETRB_EFFECT_RESPAWN_SHIELD) &&
		    (blob->respawn_kind != SYNETRB_EFFECT_RESPAWN_YOSHI_SHIELD))
		{
			continue;
		}
		if (syNetRbSnapShieldPlayerFromEffectBlob(blob) == player)
		{
			return blob;
		}
	}
	return NULL;
}

static void syNetRbSnapPatchLiveShieldFromSlot(const SYNetRbSnapshotSlot *slot, GObj *fighter_gobj, FTStruct *fp,
                                               u32 eff_id, GObj *live_shield)
{
	const SYNetRbSnapEffectBlob *eb;

	if ((slot == NULL) || (fighter_gobj == NULL) || (fp == NULL) || (live_shield == NULL))
	{
		return;
	}
	eb = syNetRbSnapFindEffectBlobByGobjId(slot, eff_id);
	if (eb == NULL)
	{
		eb = syNetRbSnapFindShieldEffectBlobForPlayer(slot, fp->player);
	}
	if (eb != NULL)
	{
		(void)syNetRbSnapApplyEffectBlobToGObj(slot, live_shield, eb);
	}
	ftStatusVarsGuard(fp)->effect_gobj = live_shield;
	if (fp->is_shield != FALSE)
	{
		fp->is_effect_attach = TRUE;
	}
}

static sb32 syNetRbSnapFighterBlobAllowsShieldRebind(const SYNetRbSnapFighterBlob *fb, const FTStruct *fp)
{
	if ((fb == NULL) || (fp == NULL) || (fb->is_valid == FALSE))
	{
		return FALSE;
	}
	if (fp->is_shield != FALSE)
	{
		return TRUE;
	}
	if (syNetRbSnapBlobInGuardScope(fb) == FALSE)
	{
		return FALSE;
	}
	/* GuardOff / release-lag: blob still owns is_shield while vanilla drains release_lag. */
	return (fb->is_shield != FALSE) ? TRUE : FALSE;
}

static sb32 syNetRbSnapTryRebindOrphanShieldEffectForLoad(GObj *gobj, EFStruct *ep, const SYNetRbSnapshotSlot *slot)
{
	s32 shield_player;
	GObj *owner_gobj;
	FTStruct *fp;
	const SYNetRbSnapFighterBlob *fb;

	if ((gobj == NULL) || (ep == NULL))
	{
		return FALSE;
	}
	shield_player = syNetRbSnapShieldPlayerFromEffectVars(ep);
	if ((shield_player < 0) && (slot != NULL))
	{
		const SYNetRbSnapEffectBlob *eb;

		eb = syNetRbSnapFindEffectBlobByGobjId(slot, (u32)gobj->id);
		if (eb != NULL)
		{
			shield_player = syNetRbSnapShieldPlayerFromEffectBlob(eb);
		}
	}
	if (shield_player < 0)
	{
		return FALSE;
	}
	owner_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)shield_player);
	if ((owner_gobj == NULL) || ((fp = ftGetStruct(owner_gobj)) == NULL))
	{
		return FALSE;
	}
	fb = (slot != NULL) ? &slot->fighters[shield_player] : NULL;
	if (syNetRbSnapFighterBlobAllowsShieldRebind(fb, fp) == FALSE)
	{
		return FALSE;
	}
	ep->fighter_gobj = owner_gobj;
	ep->effect_vars.shield.player = fp->player;
	if (syNetRbSnapFighterGuardEffectUnionOwned(fp) != FALSE)
	{
		ftStatusVarsGuard(fp)->effect_gobj = gobj;
		fp->is_effect_attach = TRUE;
	}
	if (slot != NULL)
	{
		syNetRbSnapPatchLiveShieldFromSlot(slot, owner_gobj, fp, syNetRbSnapGuardEffectIdFromBlob(fb), gobj);
	}
	return TRUE;
}

static void syNetRbSnapPatchAllGuardShieldsFromSlot(const SYNetRbSnapshotSlot *slot)
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
		const SYNetRbSnapFighterBlob *fb;
		GObj *live_shield;
		s32 pi;

		fp = ftGetStruct(fighter_gobj);
		if (syNetRbSnapFighterInGuardScope(fp) == FALSE)
		{
			continue;
		}
		pi = fp->player;
		if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX))
		{
			continue;
		}
		fb = &slot->fighters[pi];
		if (fb->is_valid == FALSE)
		{
			continue;
		}
		if ((fp->fkind == nFTKindYoshi) && (fp->is_shield == FALSE))
		{
			continue;
		}
		live_shield = syNetRbSnapFindLiveShieldEffectForFighter(fighter_gobj);
		if (live_shield == NULL)
		{
			continue;
		}
		syNetRbSnapPatchLiveShieldFromSlot(slot, fighter_gobj, fp, syNetRbSnapGuardEffectIdFromBlob(fb),
		                                   live_shield);
	}
}

static void syNetRbSnapEnsureFoxReflectorEffectsFromSlot(const SYNetRbSnapshotSlot *slot)
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
		const SYNetRbSnapFighterBlob *fb;
		u32 eff_id;
		GObj *eg;
		const SYNetRbSnapEffectBlob *eb;
		SYNetRbSnapEffectBlob synth;
		s32 pi;

		fp = ftGetStruct(fighter_gobj);
		if (syNetRbSnapFighterInFoxReflectorScope(fp) == FALSE)
		{
			continue;
		}
		pi = fp->player;
		if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX))
		{
			continue;
		}
		fb = &slot->fighters[pi];
		if (fb->is_valid == FALSE)
		{
			continue;
		}
		eff_id = syNetRbSnapFoxSpecialLwEffectIdFromBlob(fb);
		if (eff_id == 0U)
		{
			continue;
		}
		eg = gcFindGObjByID(eff_id);
		if ((eg != NULL) && (efGetStruct(eg) != NULL))
		{
			continue;
		}
		eb = syNetRbSnapFindEffectBlobByGobjId(slot, eff_id);
		if ((eb != NULL) && (eb->respawn_kind == SYNETRB_EFFECT_RESPAWN_FOX_REFLECTOR))
		{
			(void)syNetRbSnapTryRespawnEffectFromBlob(slot, eb);
		}
		else
		{
			memset(&synth, 0, sizeof(synth));
			synth.is_valid = TRUE;
			synth.gobj_id = eff_id;
			synth.fighter_gobj_id = (u32)fighter_gobj->id;
			synth.respawn_kind = SYNETRB_EFFECT_RESPAWN_FOX_REFLECTOR;
			(void)syNetRbSnapTryRespawnEffectFromBlob(slot, &synth);
		}
	}
}

static sb32 syNetRbSnapLiveHasGuardScope(void)
{
	return syNetRbSnapshotAnyFighterGuardScopeActive();
}

static sb32 syNetRbSnapFighterInYoshiEggShellScope(const FTStruct *fp)
{
	return ((fp != NULL) && (fp->status_id == nFTCommonStatusYoshiEgg)) ? TRUE : FALSE;
}

static sb32 syNetRbSnapLiveEffectIsYoshiEggLay(const GObj *gobj, const EFStruct *ep)
{
	if ((gobj == NULL) || (ep == NULL) || (ep->proc_update != efManagerYoshiEggLayProcUpdate))
	{
		return FALSE;
	}
	if (syNetRbSnapLiveEffectIsShield(gobj, ep) != FALSE)
	{
		return FALSE;
	}
	return syNetRbSnapEffectGObjHasUpdateProc(gobj, ep->proc_update);
}

#if defined(SSB64_NETMENU)
static sb32 s_syNetRbSnapYoshiEggLayHatchShellParticlePending[GMCOMMON_PLAYERS_MAX];
static Vec3f s_syNetRbSnapYoshiEggLayHatchShellParticlePos[GMCOMMON_PLAYERS_MAX];

static GObj *syNetRbSnapFindYoshiEggLayHatchCosmeticForFighter(const GObj *fighter_gobj)
{
	s32 pass;

	if (fighter_gobj == NULL)
	{
		return NULL;
	}
	for (pass = 0; pass < 2; pass++)
	{
		GObj *gobj;

		for (gobj = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		     gobj != NULL; gobj = gobj->link_next)
		{
			EFStruct *ep;

			ep = efGetStruct(gobj);
			if ((syNetRbSnapEffectHiddenFromRollback(gobj, ep) != FALSE) && (ep->fighter_gobj == fighter_gobj))
			{
				return gobj;
			}
		}
	}
	return NULL;
}

static void syNetRbSnapEjectNonHatchYoshiEggLayEffectsForFighterExcept(GObj *fighter_gobj, GObj *keep_gobj)
{
	s32 pass;

	if (fighter_gobj == NULL)
	{
		return;
	}
	for (pass = 0; pass < 2; pass++)
	{
		GObj *gobj;
		GObj *next;

		for (gobj = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect]; gobj != NULL;
		     gobj = next)
		{
			EFStruct *ep;

			next = gobj->link_next;
			ep = efGetStruct(gobj);
			if ((ep == NULL) || (ep->fighter_gobj != fighter_gobj))
			{
				continue;
			}
			if (gobj == keep_gobj)
			{
				continue;
			}
			if (syNetRbSnapEffectHiddenFromRollback(gobj, ep) != FALSE)
			{
				gcEjectGObj(gobj);
				continue;
			}
			if (syNetRbSnapLiveEffectIsYoshiEggLay(gobj, ep) == FALSE)
			{
				continue;
			}
			gcEjectGObj(gobj);
		}
	}
}

static void syNetRbSnapEjectNonHatchYoshiEggLayEffectsForFighter(GObj *fighter_gobj)
{
	syNetRbSnapEjectNonHatchYoshiEggLayEffectsForFighterExcept(fighter_gobj, NULL);
}

static void syNetRbSnapPrepareYoshiEggLayHatchCosmeticShell(GObj *fighter_gobj, GObj *effect_gobj)
{
	FTStruct *fp;
	EFStruct *ep;
	DObj *dobj;

	if ((fighter_gobj == NULL) || (effect_gobj == NULL))
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	ep = efGetStruct(effect_gobj);
	if ((fp == NULL) || (ep == NULL))
	{
		return;
	}
	dobj = DObjGetStruct(effect_gobj);
	if (dobj == NULL)
	{
		return;
	}
	dobj->user_data.p = fp->joints[nFTPartsJointTopN];
	dobj->translate.vec.f.x = dobj->translate.vec.f.y = dobj->translate.vec.f.z = 0.0F;
	if (dobj->child != NULL)
	{
		dobj->child->translate.vec.f.x = 0.0F;
		dobj->child->translate.vec.f.y = 0.0F;
	}
	ep->fighter_gobj = fighter_gobj;
	ep->bank_id = SYNETRB_YOSHI_EGG_LAY_HATCH_COSMETIC_BANK_ID;
	ep->proc_update = syNetRbSnapYoshiEggLayHatchCosmeticProcUpdate;
	ep->effect_vars.yoshi_egg_lay.force_index = 1;
	efManagerYoshiEggLaySetAnim(effect_gobj, 1);
	ep->effect_vars.yoshi_egg_lay.force_index = 1;
	ep->effect_vars.yoshi_egg_lay.index = 1;
	gcSetAnimSpeed(effect_gobj, 1.0F);
}

static void syNetRbSnapYoshiEggLayHatchCosmeticProcUpdate(GObj *effect_gobj)
{
	EFStruct *ep;
	s32 player;
	f32 anim_frame_before;

	if (effect_gobj == NULL)
	{
		return;
	}
	ep = efGetStruct(effect_gobj);
	if (ep == NULL)
	{
		gcEjectGObj(effect_gobj);
		return;
	}
	anim_frame_before = effect_gobj->anim_frame;
	efManagerYoshiEggLayProcUpdate(effect_gobj);
	if ((ep->effect_vars.yoshi_egg_lay.index == 1) && (anim_frame_before > 0.0F) &&
	    (effect_gobj->anim_frame <= 0.0F))
	{
		player = -1;
		if (ep->fighter_gobj != NULL)
		{
			FTStruct *fp = ftGetStruct(ep->fighter_gobj);

			if (fp != NULL)
			{
				player = fp->player;
			}
		}
		if ((player >= 0) && (player < GMCOMMON_PLAYERS_MAX) &&
		    (s_syNetRbSnapYoshiEggLayHatchShellParticlePending[player] != FALSE))
		{
			syNetRbSnapReplayCosmeticYoshiEggExplode(&s_syNetRbSnapYoshiEggLayHatchShellParticlePos[player]);
			s_syNetRbSnapYoshiEggLayHatchShellParticlePending[player] = FALSE;
		}
		efManagerSetPrevStructAlloc(ep);
		gcEjectGObj(effect_gobj);
	}
}

static sb32 syNetRbSnapYoshiEggLayHatchAlreadyComplete(const GObj *effect_gobj, const EFStruct *ep)
{
	if ((effect_gobj == NULL) || (ep == NULL))
	{
		return FALSE;
	}
	if (ep->proc_update == syNetRbSnapYoshiEggLayHatchCosmeticProcUpdate)
	{
		return FALSE;
	}
	return ((syNetRbSnapLiveEffectIsYoshiEggLay(effect_gobj, ep) != FALSE) &&
	        (ep->effect_vars.yoshi_egg_lay.index == 1) && (effect_gobj->anim_frame <= 0.0F))
	           ? TRUE
	           : FALSE;
}

static sb32 syNetRbSnapStartYoshiEggLayHatchCosmeticLive(GObj *fighter_gobj, const Vec3f *pos)
{
	GObj *effect_gobj;
	EFStruct *ep;
	FTStruct *fp;
	s32 player;

	if ((fighter_gobj == NULL) || (pos == NULL))
	{
		return FALSE;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp == NULL)
	{
		return FALSE;
	}
	player = fp->player;
	effect_gobj = syNetRbSnapFindYoshiEggLayHatchCosmeticForFighter(fighter_gobj);
	if (effect_gobj == NULL)
	{
		effect_gobj = syNetRbSnapFindLiveYoshiEggLayEffectForFighter(fighter_gobj);
	}
	if (effect_gobj == NULL)
	{
		return FALSE;
	}
	ep = efGetStruct(effect_gobj);
	if (ep == NULL)
	{
		return FALSE;
	}
	if (ep->proc_update == syNetRbSnapYoshiEggLayHatchCosmeticProcUpdate)
	{
		return FALSE;
	}
	if (syNetRbSnapYoshiEggLayHatchAlreadyComplete(effect_gobj, ep) != FALSE)
	{
		return FALSE;
	}
	syNetRbSnapEjectNonHatchYoshiEggLayEffectsForFighterExcept(fighter_gobj, effect_gobj);
	syNetRbSnapPrepareYoshiEggLayHatchCosmeticShell(fighter_gobj, effect_gobj);
	if ((player >= 0) && (player < GMCOMMON_PLAYERS_MAX))
	{
		s_syNetRbSnapYoshiEggLayHatchShellParticlePos[player] = *pos;
		s_syNetRbSnapYoshiEggLayHatchShellParticlePending[player] = TRUE;
	}
	return TRUE;
}

static GObj *syNetRbSnapReplayYoshiEggLayHatchShell(GObj *fighter_gobj, const Vec3f *pos)
{
	GObj *effect_gobj;
	EFStruct *ep;

	if ((fighter_gobj == NULL) || (pos == NULL))
	{
		return NULL;
	}
	effect_gobj = syNetRbSnapFindYoshiEggLayHatchCosmeticForFighter(fighter_gobj);
	if (effect_gobj != NULL)
	{
		ep = efGetStruct(effect_gobj);
		if ((ep != NULL) && (ep->proc_update == syNetRbSnapYoshiEggLayHatchCosmeticProcUpdate) &&
		    (ep->effect_vars.yoshi_egg_lay.index == 1) && (effect_gobj->anim_frame > 0.0F))
		{
			return effect_gobj;
		}
	}
	if (syNetRbSnapStartYoshiEggLayHatchCosmeticLive(fighter_gobj, pos) != FALSE)
	{
		return syNetRbSnapFindYoshiEggLayHatchCosmeticForFighter(fighter_gobj);
	}
	effect_gobj = syNetRbSnapFindLiveYoshiEggLayEffectForFighter(fighter_gobj);
	if ((effect_gobj != NULL) && (syNetRbSnapYoshiEggLayHatchAlreadyComplete(effect_gobj, efGetStruct(effect_gobj)) != FALSE))
	{
		return NULL;
	}
	effect_gobj = syNetRbSnapFindYoshiEggLayHatchCosmeticForFighter(fighter_gobj);
	if (effect_gobj == NULL)
	{
		effect_gobj = syNetRbSnapFindLiveYoshiEggLayEffectForFighter(fighter_gobj);
	}
	if (effect_gobj == NULL)
	{
		effect_gobj = efManagerYoshiEggLayMakeEffect(fighter_gobj);
		if (effect_gobj == NULL)
		{
			return NULL;
		}
	}
	syNetRbSnapEjectNonHatchYoshiEggLayEffectsForFighterExcept(fighter_gobj, effect_gobj);
	syNetRbSnapPrepareYoshiEggLayHatchCosmeticShell(fighter_gobj, effect_gobj);
	return effect_gobj;
}
#endif

static sb32 syNetRbSnapYoshiEggLayEffectOwnedByFighter(const GObj *effect_gobj, const GObj *fighter_gobj)
{
	EFStruct *ep;

	if ((effect_gobj == NULL) || (fighter_gobj == NULL) || (syNetRbSnapGobjId(effect_gobj) == 0U))
	{
		return FALSE;
	}
	ep = efGetStruct(effect_gobj);
	if (syNetRbSnapLiveEffectIsYoshiEggLay(effect_gobj, ep) == FALSE)
	{
		return FALSE;
	}
	return (ep->fighter_gobj == fighter_gobj) ? TRUE : FALSE;
}

static void syNetRbSnapSanitizeGuardEffectGobj(FTStruct *fp)
{
	GObj *effect_gobj;
	EFStruct *ep;

	if ((fp == NULL) || (syNetRbSnapFighterGuardEffectUnionOwned(fp) == FALSE))
	{
		return;
	}
	if ((fp->is_shield != FALSE) && (syNetRbSnapFighterInGuardScope(fp) != FALSE))
	{
		effect_gobj = ftStatusVarsGuard(fp)->effect_gobj;
		if (effect_gobj == NULL)
		{
			return;
		}
		ep = efGetStruct(effect_gobj);
		if ((ep != NULL) && (syNetRbSnapLiveEffectIsShield(effect_gobj, ep) != FALSE))
		{
			return;
		}
	}
	effect_gobj = ftStatusVarsGuard(fp)->effect_gobj;
	if (effect_gobj == NULL)
	{
		return;
	}
	{
		u32 effect_id;

		effect_id = syNetRbSnapGobjId(effect_gobj);
		if ((effect_id == 0U) || (gcFindGObjByID(effect_id) != effect_gobj))
		{
			ftStatusVarsGuard(fp)->effect_gobj = NULL;
			if (fp->is_effect_attach != FALSE)
			{
				fp->is_effect_attach = FALSE;
			}
			return;
		}
	}
	ep = efGetStruct(effect_gobj);
	if ((ep == NULL) || (syNetRbSnapLiveEffectIsShield(effect_gobj, ep) == FALSE))
	{
		ftStatusVarsGuard(fp)->effect_gobj = NULL;
		if (fp->is_effect_attach != FALSE)
		{
			fp->is_effect_attach = FALSE;
		}
	}
}

static void syNetRbSnapSanitizeAllFightersGuardEffectGobjs(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if (fp != NULL)
		{
			syNetRbSnapSanitizeGuardEffectGobj(fp);
		}
	}
}

static sb32 syNetRbSnapYoshiEggLayReconcileCaptureWindowActive(const SYNetRbSnapshotSlot *slot)
{
	if (syNetRbSnapYoshiEggLayCaptureWindowActiveWithoutEgg() != FALSE)
	{
		return TRUE;
	}
	if ((slot != NULL) && (syNetRbSnapYoshiEggLaySlotCaptureWindowWithoutEgg(slot) != FALSE))
	{
		return TRUE;
	}
	return FALSE;
}

u32 syNetRbSnapHashCaptureYoshiEffectGobjId(const FTStruct *fp)
{
	GObj *effect_gobj;
	EFStruct *ep;

	if (fp == NULL)
	{
		return 0U;
	}
	if (syNetRbSnapFighterInYoshiEggLayScope(fp) == FALSE)
	{
		return 0U;
	}
	if (fp->status_id != nFTCommonStatusYoshiEgg)
	{
		return 0U;
	}
	effect_gobj = ftStatusVarsCaptureYoshi(fp)->effect_gobj;
	if (effect_gobj == NULL)
	{
		return 0U;
	}
	if (syNetRbSnapGobjId(effect_gobj) == 0U)
	{
		return 0U;
	}
	ep = efGetStruct(effect_gobj);
	if (syNetRbSnapLiveEffectIsYoshiEggLay(effect_gobj, ep) == FALSE)
	{
		return 0U;
	}
	return (u32)effect_gobj->id;
}

void syNetRbSnapSanitizeCaptureYoshiEffectGobj(FTStruct *fp)
{
	GObj *effect_gobj;
	GObj *fighter_gobj;
	EFStruct *ep;

	if (fp == NULL)
	{
		return;
	}
	/*
	 * captureyoshi aliases entry/catchwait/... at union byte 0. Never read or write
	 * effect_gobj unless CaptureYoshi/YoshiEgg is live — entry_wait=1 reinterprets as GObj* 0x1.
	 */
	if (syNetRbSnapFighterInYoshiEggLayScope(fp) == FALSE)
	{
		return;
	}
	effect_gobj = ftStatusVarsCaptureYoshi(fp)->effect_gobj;
	if (effect_gobj == NULL)
	{
		return;
	}
	fighter_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)fp->player);
	if (syNetRbSnapYoshiEggLayEffectOwnedByFighter(effect_gobj, fighter_gobj) == FALSE)
	{
		ftStatusVarsCaptureYoshi(fp)->effect_gobj = NULL;
		if (fp->is_effect_attach != FALSE)
		{
			fp->is_effect_attach = FALSE;
		}
		return;
	}
	ep = efGetStruct(effect_gobj);
	if (syNetRbSnapLiveEffectIsYoshiEggLay(effect_gobj, ep) == FALSE)
	{
		ftStatusVarsCaptureYoshi(fp)->effect_gobj = NULL;
		if (fp->is_effect_attach != FALSE)
		{
			fp->is_effect_attach = FALSE;
		}
		return;
	}
	if (fp->status_id == nFTCommonStatusYoshiEgg)
	{
		DObj *egg_dobj;

		egg_dobj = DObjGetStruct(effect_gobj);
		if ((egg_dobj == NULL) || (egg_dobj->child == NULL))
		{
			ftStatusVarsCaptureYoshi(fp)->effect_gobj = NULL;
			if (fp->is_effect_attach != FALSE)
			{
				fp->is_effect_attach = FALSE;
			}
		}
	}
}

void syNetRbSnapSanitizeAllFightersCaptureYoshiEffectGobjs(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(fighter_gobj);

		if (fp != NULL)
		{
			syNetRbSnapSanitizeCaptureYoshiEffectGobj(fp);
		}
	}
}

static void syNetRbSnapEnsureYoshiEggLayEffectsFromSlot(const SYNetRbSnapshotSlot *slot)
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
		const SYNetRbSnapFighterBlob *fb;
		u32 eff_id;
		GObj *eg;
		const SYNetRbSnapEffectBlob *eb;
		SYNetRbSnapEffectBlob synth;
		s32 pi;

		fp = ftGetStruct(fighter_gobj);
		if ((fp == NULL) || (fp->status_id != nFTCommonStatusYoshiEgg))
		{
			continue;
		}
		syNetRbSnapSanitizeCaptureYoshiEffectGobj(fp);
		pi = fp->player;
		if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX))
		{
			continue;
		}
		fb = &slot->fighters[pi];
		if (fb->is_valid == FALSE)
		{
			continue;
		}
		eff_id = fb->captureyoshi_effect_gobj_id;
		if ((eff_id != 0U) && (syNetRbSnapEffectIdClaimedByGuard(slot, eff_id) != FALSE))
		{
			continue;
		}
		if (ftStatusVarsCaptureYoshi(fp)->effect_gobj != NULL)
		{
			continue;
		}
		eg = syNetRbSnapFindLiveYoshiEggLayEffectForFighter(fighter_gobj);
		if (eg != NULL)
		{
			ftStatusVarsCaptureYoshi(fp)->effect_gobj = eg;
			fp->is_effect_attach = TRUE;
			if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 NetRbSnapshot: yoshi_egg_lay_ensure path=live_owner player=%d fighter_gobj_id=%u "
				    "effect_gobj_id=%u\n",
				    (int)pi, (unsigned int)fighter_gobj->id, (unsigned int)eg->id);
			}
			continue;
		}
		if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
		{
			port_log(
			    "SSB64 NetRbSnapshot: yoshi_egg_lay_ensure tick=%u player=%d status=%d fighter_gobj_id=%u "
			    "blob_effect_id=%u effect_count=%d\n",
			    (unsigned int)slot->tick, (int)pi, (int)fp->status_id, (unsigned int)fighter_gobj->id, eff_id,
			    (int)slot->effect_count);
		}
		if (eff_id != 0U)
		{
			eg = gcFindGObjByID(eff_id);
			if (syNetRbSnapYoshiEggLayEffectOwnedByFighter(eg, fighter_gobj) != FALSE)
			{
				ftStatusVarsCaptureYoshi(fp)->effect_gobj = eg;
				if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
				{
					port_log(
					    "SSB64 NetRbSnapshot: yoshi_egg_lay_ensure path=live_id player=%d fighter_gobj_id=%u "
					    "effect_gobj_id=%u\n",
					    (int)pi, (unsigned int)fighter_gobj->id, eff_id);
				}
				continue;
			}
		}
		eb = (eff_id != 0U) ? syNetRbSnapFindEffectBlobByGobjId(slot, eff_id) : NULL;
		if ((eb != NULL) && (eb->respawn_kind == SYNETRB_EFFECT_RESPAWN_YOSHI_EGG_LAY))
		{
			eg = syNetRbSnapTryRespawnEffectFromBlob(slot, eb);
			if (eg != NULL)
			{
				ftStatusVarsCaptureYoshi(fp)->effect_gobj = eg;
			}
			if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 NetRbSnapshot: yoshi_egg_lay_ensure path=blob_respawn player=%d fighter_gobj_id=%u "
				    "blob_gobj_id=%u result=%s\n",
				    (int)pi, (unsigned int)fighter_gobj->id, eb->gobj_id, (eg != NULL) ? "ok" : "fail");
			}
			continue;
		}
		if (fp->status_id != nFTCommonStatusYoshiEgg)
		{
			continue;
		}
		memset(&synth, 0, sizeof(synth));
		synth.is_valid = TRUE;
		/* Egg-lay GObj ids are effect-kind ids, so synthetic restore is keyed by owner only. */
		synth.gobj_id = 0U;
		synth.fighter_gobj_id = (u32)fighter_gobj->id;
		synth.respawn_kind = SYNETRB_EFFECT_RESPAWN_YOSHI_EGG_LAY;
		eg = syNetRbSnapTryRespawnEffectFromBlob(slot, &synth);
		if (eg != NULL)
		{
			ftStatusVarsCaptureYoshi(fp)->effect_gobj = eg;
		}
		if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
		{
			port_log(
			    "SSB64 NetRbSnapshot: yoshi_egg_lay_ensure path=synth player=%d fighter_gobj_id=%u result=%s "
			    "new_gobj_id=%u\n",
			    (int)pi, (unsigned int)fighter_gobj->id, (eg != NULL) ? "ok" : "fail",
			    (eg != NULL) ? syNetRbSnapGobjId(eg) : 0U);
		}
	}
}

static GObj *syNetRbSnapResolveCoupledGobj(GObj *coupled_gobj)
{
	u32 gobj_id;

	if (coupled_gobj == NULL)
	{
		return NULL;
	}
	gobj_id = syNetRbSnapGobjId(coupled_gobj);
	if (gobj_id == 0U)
	{
		return NULL;
	}
	return gcFindGObjByID(gobj_id);
}

static GObj *syNetRbSnapFindLiveYoshiEggLayEffectForFighter(const GObj *fighter_gobj)
{
	s32 pass;

	if (fighter_gobj == NULL)
	{
		return NULL;
	}
	for (pass = 0; pass < 2; pass++)
	{
		GObj *gobj;

		for (gobj = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		     gobj != NULL; gobj = gobj->link_next)
		{
			EFStruct *ep = efGetStruct(gobj);

			if (syNetRbSnapLiveEffectIsYoshiEggLay(gobj, ep) == FALSE)
			{
				continue;
			}
			if (ep->fighter_gobj == fighter_gobj)
			{
				return gobj;
			}
		}
	}
	return NULL;
}

static void syNetRbSnapRebindCaptureYoshiEffectGobjsFromLive(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		GObj *coupled;
		GObj *live;

		fp = ftGetStruct(fighter_gobj);
		if ((fp == NULL) || (fp->status_id != nFTCommonStatusYoshiEgg))
		{
			continue;
		}
		coupled = ftStatusVarsCaptureYoshi(fp)->effect_gobj;
		if (syNetRbSnapYoshiEggLayEffectOwnedByFighter(coupled, fighter_gobj) != FALSE)
		{
			fp->is_effect_attach = TRUE;
			continue;
		}
		live = syNetRbSnapFindLiveYoshiEggLayEffectForFighter(fighter_gobj);
		if (live != NULL)
		{
			ftStatusVarsCaptureYoshi(fp)->effect_gobj = live;
			fp->is_effect_attach = TRUE;
		}
		else if (coupled != NULL)
		{
			ftStatusVarsCaptureYoshi(fp)->effect_gobj = NULL;
			if (fp->is_effect_attach != FALSE)
			{
				fp->is_effect_attach = FALSE;
			}
		}
	}
}

/*
 * Vanilla never parents an egg-lay shell during CaptureYoshi or Yoshi SpecialN/NCatch — only after YoshiEgg.
 * Stray respawns during the capture window corrupt GObj ids and freeze the swallow animation; drop them all.
 */
static void syNetRbSnapPruneStrayYoshiEggLayEffectsDuringCaptureWindow(const SYNetRbSnapshotSlot *slot)
{
	s32 pass;
	GObj *gobj;
	GObj *next;

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
			if (syNetRbSnapLiveEffectIsShield(gobj, ep) != FALSE)
			{
				continue;
			}
			if (syNetRbSnapEffectIdClaimedByGuard(slot, (u32)gobj->id) != FALSE)
			{
				continue;
			}
			if (syNetRbSnapLiveEffectIsYoshiEggLay(gobj, ep) == FALSE)
			{
				continue;
			}
			if (ep->fighter_gobj != NULL)
			{
				fp = ftGetStruct(ep->fighter_gobj);
				if (fp != NULL)
				{
					syNetRbSnapClearCaptureYoshiEffectPointerIfMatch(fp, gobj);
					if (fp->is_effect_attach != FALSE)
					{
						fp->is_effect_attach = FALSE;
					}
				}
			}
			else
			{
				syNetRbSnapClearAllFightersCaptureYoshiEffectPointerIfMatch(gobj);
			}
			if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 NetRbSnapshot: yoshi_egg_lay_prune tick=%u path=eject reason=capture_window_stray "
				    "effect_gobj_id=%u\n",
				    (unsigned int)((slot != NULL) ? slot->tick : syNetInputGetTick()), (unsigned int)gobj->id);
			}
			syNetRbSnapEjectGObj(gobj);
		}
	}
}

static void syNetRbSnapPruneStaleYoshiEggLayEffects(const SYNetRbSnapshotSlot *slot)
{
	s32 pass;
	GObj *gobj;
	GObj *next;

	for (pass = 0; pass < 2; pass++)
	{
		GObj *link_head;

		link_head = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		for (gobj = link_head; gobj != NULL; gobj = next)
		{
			EFStruct *ep;
			FTStruct *fp;
			const SYNetRbSnapFighterBlob *fb;
			s32 pi;
			u32 expected_id;

			next = gobj->link_next;
			ep = efGetStruct(gobj);
			if (syNetRbSnapLiveEffectIsShield(gobj, ep) != FALSE)
			{
				continue;
			}
			if (syNetRbSnapEffectIdClaimedByGuard(slot, (u32)gobj->id) != FALSE)
			{
				continue;
			}
			if (syNetRbSnapLiveEffectIsYoshiEggLay(gobj, ep) == FALSE)
			{
				continue;
			}
			if (ep->fighter_gobj == NULL)
			{
				if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
				{
					port_log(
					    "SSB64 NetRbSnapshot: yoshi_egg_lay_prune tick=%u path=eject reason=no_fighter "
					    "effect_gobj_id=%u\n",
					    (unsigned int)((slot != NULL) ? slot->tick : 0U), (unsigned int)gobj->id);
				}
				syNetRbSnapClearAllFightersCaptureYoshiEffectPointerIfMatch(gobj);
				syNetRbSnapEjectGObj(gobj);
				continue;
			}
			fp = ftGetStruct(ep->fighter_gobj);
			if ((fp == NULL) || (fp->status_id != nFTCommonStatusYoshiEgg) ||
			    (syNetRbSnapFighterInYoshiEggLayAttackScope(fp) != FALSE))
			{
				syNetRbSnapClearCaptureYoshiEffectPointerIfMatch(fp, gobj);
				if ((fp != NULL) && (fp->is_effect_attach != FALSE))
				{
					fp->is_effect_attach = FALSE;
				}
				if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
				{
					port_log(
					    "SSB64 NetRbSnapshot: yoshi_egg_lay_prune tick=%u path=eject reason=orphan_fighter "
					    "effect_gobj_id=%u fighter_gobj_id=%u fighter_status=%d\n",
					    (unsigned int)((slot != NULL) ? slot->tick : 0U), (unsigned int)gobj->id,
					    (unsigned int)ep->fighter_gobj->id, (fp != NULL) ? (int)fp->status_id : -1);
				}
				syNetRbSnapEjectGObj(gobj);
				continue;
			}
			if (syNetRbSnapYoshiEggLayVictimOwnsEffect(fp, ep) == FALSE)
			{
				syNetRbSnapClearCaptureYoshiEffectPointerIfMatch(fp, gobj);
				if (fp->is_effect_attach != FALSE)
				{
					fp->is_effect_attach = FALSE;
				}
				if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
				{
					port_log(
					    "SSB64 NetRbSnapshot: yoshi_egg_lay_prune tick=%u path=eject reason=wrong_parent "
					    "player=%d effect_gobj_id=%u fighter_gobj_id=%u\n",
					    (unsigned int)((slot != NULL) ? slot->tick : syNetInputGetTick()), (int)fp->player,
					    (unsigned int)gobj->id, (unsigned int)ep->fighter_gobj->id);
				}
				syNetRbSnapEjectGObj(gobj);
				continue;
			}
			pi = fp->player;
			{
				GObj *keep_gobj;

				keep_gobj = ftStatusVarsCaptureYoshi(fp)->effect_gobj;
				if (syNetRbSnapYoshiEggLayEffectOwnedByFighter(keep_gobj, ep->fighter_gobj) == FALSE)
				{
					keep_gobj = syNetRbSnapFindLiveYoshiEggLayEffectForFighter(ep->fighter_gobj);
				}
				if (keep_gobj != NULL)
				{
					if (keep_gobj == gobj)
					{
						ftStatusVarsCaptureYoshi(fp)->effect_gobj = gobj;
						fp->is_effect_attach = TRUE;
						continue;
					}
					if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
					{
						port_log(
						    "SSB64 NetRbSnapshot: yoshi_egg_lay_prune tick=%u path=eject reason=owner_duplicate "
						    "player=%d effect_gobj_id=%u keep_gobj_id=%u\n",
						    (unsigned int)((slot != NULL) ? slot->tick : 0U), (int)pi, (unsigned int)gobj->id,
						    syNetRbSnapGobjId(keep_gobj));
					}
					syNetRbSnapClearCaptureYoshiEffectPointerIfMatch(fp, gobj);
					syNetRbSnapEjectGObj(gobj);
					continue;
				}
			}
			{
				GObj *canonical;
				GObj *coupled;

				coupled = ftStatusVarsCaptureYoshi(fp)->effect_gobj;
				canonical = syNetRbSnapResolveCoupledGobj(coupled);
				if (canonical == gobj)
				{
					continue;
				}
				if (canonical != NULL)
				{
					if ((syNetRbSnapYoshiEggLayEffectMatchesKeep(gobj, coupled) != FALSE) ||
					    (syNetRbSnapYoshiEggLayEffectMatchesKeep(gobj, canonical) != FALSE))
					{
						ftStatusVarsCaptureYoshi(fp)->effect_gobj = gobj;
						if (fp->is_effect_attach != FALSE)
						{
							fp->is_effect_attach = TRUE;
						}
						if (syNetRbSnapCountLiveYoshiEggLayEffectsForFighter(ep->fighter_gobj) <= 1)
						{
							if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
							{
								port_log(
								    "SSB64 NetRbSnapshot: yoshi_egg_lay_prune tick=%u path=keep reason=egg_lay_id_rebind "
								    "player=%d effect_gobj_id=%u\n",
								    (unsigned int)((slot != NULL) ? slot->tick : syNetInputGetTick()), (int)pi,
								    (unsigned int)gobj->id);
							}
							continue;
						}
						if (slot != NULL)
						{
							if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
							{
								port_log(
								    "SSB64 NetRbSnapshot: yoshi_egg_lay_prune tick=%u path=keep reason=egg_lay_id_rebind_resim_defer "
								    "player=%d effect_gobj_id=%u\n",
								    (unsigned int)slot->tick, (int)pi, (unsigned int)gobj->id);
							}
							continue;
						}
					}
					if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
					{
						port_log(
						    "SSB64 NetRbSnapshot: yoshi_egg_lay_prune tick=%u path=eject reason=duplicate "
						    "player=%d effect_gobj_id=%u canonical_id=%u\n",
						    (unsigned int)((slot != NULL) ? slot->tick : 0U), (int)pi, (unsigned int)gobj->id,
						    syNetRbSnapGobjId(canonical));
					}
					syNetRbSnapClearCaptureYoshiEffectPointerIfMatch(fp, gobj);
					syNetRbSnapEjectGObj(gobj);
					continue;
				}
			}
			if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX))
			{
				if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
				{
					port_log(
					    "SSB64 NetRbSnapshot: yoshi_egg_lay_prune tick=%u path=eject reason=bad_player "
					    "effect_gobj_id=%u fighter_gobj_id=%u\n",
					    (slot != NULL) ? (unsigned int)slot->tick : 0U, (unsigned int)gobj->id,
					    (unsigned int)ep->fighter_gobj->id);
				}
				syNetRbSnapEjectGObj(gobj);
				continue;
			}
			if (slot != NULL)
			{
				fb = &slot->fighters[pi];
				expected_id = (fb->is_valid != FALSE) ? fb->captureyoshi_effect_gobj_id : 0U;
				if ((expected_id != 0U) && (gobj->id == expected_id))
				{
					ftStatusVarsCaptureYoshi(fp)->effect_gobj = gobj;
					if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
					{
						port_log(
						    "SSB64 NetRbSnapshot: yoshi_egg_lay_prune tick=%u path=adopt player=%d "
						    "effect_gobj_id=%u fighter_gobj_id=%u\n",
						    (unsigned int)slot->tick, (int)pi, (unsigned int)gobj->id,
						    (unsigned int)ep->fighter_gobj->id);
					}
					continue;
				}
				if ((expected_id != 0U) && (expected_id != (u32)gobj->id))
				{
					syNetRbSnapClearCaptureYoshiEffectPointerIfMatch(fp, gobj);
					if (fp->is_effect_attach != FALSE)
					{
						fp->is_effect_attach = FALSE;
					}
					if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
					{
						port_log(
						    "SSB64 NetRbSnapshot: yoshi_egg_lay_prune tick=%u path=eject reason=unexpected_id "
						    "player=%d effect_gobj_id=%u expected_id=%u\n",
						    (unsigned int)slot->tick, (int)pi, (unsigned int)gobj->id, expected_id);
					}
					syNetRbSnapEjectGObj(gobj);
					continue;
				}
			}
			{
				GObj *coupled;
				GObj *resolved;

				coupled = ftStatusVarsCaptureYoshi(fp)->effect_gobj;
				resolved = syNetRbSnapResolveCoupledGobj(coupled);
				if (resolved == gobj)
				{
					continue;
				}
				if (resolved != NULL)
				{
					if ((syNetRbSnapYoshiEggLayEffectMatchesKeep(gobj, coupled) != FALSE) ||
					    (syNetRbSnapYoshiEggLayEffectMatchesKeep(gobj, resolved) != FALSE))
					{
						ftStatusVarsCaptureYoshi(fp)->effect_gobj = gobj;
						if (fp->is_effect_attach != FALSE)
						{
							fp->is_effect_attach = TRUE;
						}
						if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
						{
							port_log(
							    "SSB64 NetRbSnapshot: yoshi_egg_lay_prune tick=%u path=keep reason=egg_lay_id_rebind_solo "
							    "player=%d effect_gobj_id=%u\n",
							    (unsigned int)((slot != NULL) ? slot->tick : syNetInputGetTick()), (int)pi,
							    (unsigned int)gobj->id);
						}
						continue;
					}
					if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
					{
						port_log(
						    "SSB64 NetRbSnapshot: yoshi_egg_lay_prune tick=%u path=eject reason=duplicate "
						    "player=%d effect_gobj_id=%u canonical_id=%u\n",
						    (unsigned int)((slot != NULL) ? slot->tick : 0U), (int)pi, (unsigned int)gobj->id,
						    syNetRbSnapGobjId(resolved));
					}
					syNetRbSnapClearCaptureYoshiEffectPointerIfMatch(fp, gobj);
					syNetRbSnapEjectGObj(gobj);
					continue;
				}
			}
			if (syNetRbSnapEffectIdClaimedByGuard(slot, (u32)gobj->id) != FALSE)
			{
				continue;
			}
			if ((fp->status_id != nFTCommonStatusYoshiEgg) ||
			    (syNetRbSnapYoshiEggLayVictimOwnsEffect(fp, ep) == FALSE))
			{
				continue;
			}
			ftStatusVarsCaptureYoshi(fp)->effect_gobj = gobj;
			if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 NetRbSnapshot: yoshi_egg_lay_prune tick=%u path=adopt_solo player=%d "
				    "effect_gobj_id=%u fighter_gobj_id=%u\n",
				    (unsigned int)((slot != NULL) ? slot->tick : 0U), (int)pi, (unsigned int)gobj->id,
				    (unsigned int)ep->fighter_gobj->id);
			}
		}
	}
}

static void syNetRbSnapEnsureShieldEffectsFromSlot(const SYNetRbSnapshotSlot *slot)
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
		const SYNetRbSnapFighterBlob *fb;
		u32 eff_id;
		GObj *eg;
		const SYNetRbSnapEffectBlob *eb;
		SYNetRbSnapEffectBlob synth;
		s32 pi;

		fp = ftGetStruct(fighter_gobj);
		if (syNetRbSnapFighterInGuardScope(fp) == FALSE)
		{
			continue;
		}
		if ((fp->fkind == nFTKindYoshi) && (fp->is_shield == FALSE))
		{
			continue;
		}
		pi = fp->player;
		if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX))
		{
			continue;
		}
		fb = &slot->fighters[pi];
		if (fb->is_valid == FALSE)
		{
			continue;
		}
		eff_id = syNetRbSnapGuardEffectIdFromBlob(fb);
		if (eff_id == 0U)
		{
			continue;
		}
		eg = gcFindGObjByID(eff_id);
		if ((eg != NULL) && (efGetStruct(eg) != NULL))
		{
			continue;
		}
		/*
		 * Stable per-player shield identity: the bubble is identified by the fighter's player slot, not the
		 * volatile pool GObj id that efManagerShieldMakeEffect allocates fresh on every GuardOn entry. If the
		 * resim already re-created this player's bubble under a different id, adopt it instead of spawning a
		 * second copy at the blob's id (which the duplicate/unexpected_id prune would then churn — the source
		 * of the residual eff drift on fast re-tap). See docs/bugs/netplay_guard_shield_tap_churn.
		 */
		{
			GObj *live_shield;

			live_shield = syNetRbSnapFindLiveShieldEffectForFighter(fighter_gobj);
			if (live_shield != NULL)
			{
				/*
				 * Synctest / resim load: a bubble from the emergency snapshot or a prior tick can
				 * already be on the effect list under a fresh pool id. Adopt it but still patch
				 * hashed fields (anim_frame, translate, shield vars) from the slot blob so eff
				 * verify converges without a second respawn churn.
				 */
				syNetRbSnapPatchLiveShieldFromSlot(slot, fighter_gobj, fp, eff_id, live_shield);
				continue;
			}
		}
		eb = syNetRbSnapFindEffectBlobByGobjId(slot, eff_id);
		if ((eb != NULL) &&
		    ((eb->respawn_kind == SYNETRB_EFFECT_RESPAWN_SHIELD) ||
		     (eb->respawn_kind == SYNETRB_EFFECT_RESPAWN_YOSHI_SHIELD)))
		{
			(void)syNetRbSnapTryRespawnEffectFromBlob(slot, eb);
		}
		else
		{
			memset(&synth, 0, sizeof(synth));
			synth.is_valid = TRUE;
			synth.gobj_id = eff_id;
			synth.fighter_gobj_id = (u32)fighter_gobj->id;
			synth.respawn_kind = (fp->fkind == nFTKindYoshi) ? SYNETRB_EFFECT_RESPAWN_YOSHI_SHIELD
			                                                  : SYNETRB_EFFECT_RESPAWN_SHIELD;
			(void)syNetRbSnapTryRespawnEffectFromBlob(slot, &synth);
		}
	}
}

static void syNetRbSnapPruneStaleFoxReflectors(const SYNetRbSnapshotSlot *slot)
{
	s32 pass;
	GObj *gobj;
	GObj *next;

	for (pass = 0; pass < 2; pass++)
	{
		GObj *link_head;

		link_head = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		for (gobj = link_head; gobj != NULL; gobj = next)
		{
			EFStruct *ep;
			FTStruct *fp;
			const SYNetRbSnapFighterBlob *fb;
			s32 pi;
			u32 expected_id;

			next = gobj->link_next;
			ep = efGetStruct(gobj);
			if (syNetRbSnapLiveEffectIsFoxReflector(gobj, ep) == FALSE)
			{
				continue;
			}
			if (ep->fighter_gobj == NULL)
			{
				syNetRbSnapEjectGObj(gobj);
				continue;
			}
			fp = ftGetStruct(ep->fighter_gobj);
			if ((fp == NULL) || (fp->fkind != nFTKindFox) ||
			    (syNetRbSnapFighterInFoxReflectorScope(fp) == FALSE))
			{
				syNetRbSnapClearFighterEffectPointerIfMatch(fp, gobj);
				if ((fp != NULL) && (fp->is_effect_attach != FALSE))
				{
					fp->is_effect_attach = FALSE;
				}
				syNetRbSnapEjectGObj(gobj);
				continue;
			}
			if (slot == NULL)
			{
				continue;
			}
			pi = fp->player;
			if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX))
			{
				continue;
			}
			fb = &slot->fighters[pi];
			expected_id = syNetRbSnapFoxSpecialLwEffectIdFromBlob(fb);
			if ((expected_id != 0U) && (expected_id != (u32)gobj->id))
			{
				syNetRbSnapClearFighterEffectPointerIfMatch(fp, gobj);
				if (fp->is_effect_attach != FALSE)
				{
					fp->is_effect_attach = FALSE;
				}
				syNetRbSnapEjectGObj(gobj);
			}
		}
	}
}

static void syNetRbSnapPruneStaleShields(const SYNetRbSnapshotSlot *slot, sb32 live_forward_policy)
{
	s32 pass;
	GObj *gobj;
	GObj *next;

	for (pass = 0; pass < 2; pass++)
	{
		GObj *link_head;

		link_head = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		for (gobj = link_head; gobj != NULL; gobj = next)
		{
			EFStruct *ep;
			FTStruct *fp;
			const SYNetRbSnapFighterBlob *fb;
			s32 pi;
			u32 expected_id;
			sb32 shielding;

			next = gobj->link_next;
			if (gobj->obj_kind == 0xFE)
			{
				syNetRbSnapUnlinkSentinelGObjFromLists(gobj);
				continue;
			}
			ep = efGetStruct(gobj);
			if (syNetRbSnapLiveEffectIsShield(gobj, ep) == FALSE)
			{
				continue;
			}
			if (ep->fighter_gobj == NULL)
			{
				if (syNetRbSnapTryRebindOrphanShieldEffect(gobj, ep) != FALSE)
				{
					if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
					{
						port_log(
						    "SSB64 NetRbSnapshot: guard_shield_prune tick=%u path=keep reason=no_fighter_rebind "
						    "effect_gobj_id=%u shield_player=%d\n",
						    (unsigned int)syNetInputGetTick(), (unsigned int)gobj->id,
						    (int)syNetRbSnapShieldPlayerFromEffectVars(ep));
					}
					continue;
				}
				if ((slot != NULL) &&
				    (syNetRbSnapTryRebindOrphanShieldEffectForLoad(gobj, ep, slot) != FALSE))
				{
					if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
					{
						port_log(
						    "SSB64 NetRbSnapshot: guard_shield_prune tick=%u path=keep reason=slot_orphan_rebind "
						    "effect_gobj_id=%u shield_player=%d\n",
						    (unsigned int)syNetRbSnapGuardShieldDiagTick(slot), (unsigned int)gobj->id,
						    (int)syNetRbSnapShieldPlayerFromEffectVars(ep));
					}
					continue;
				}
				static u32 s_guard_no_fighter_log_budget = 32U;

				if ((syNetRbSnapSnapshotEffectDiagEnabled() != FALSE) || (s_guard_no_fighter_log_budget > 0U))
				{
					if (s_guard_no_fighter_log_budget > 0U)
					{
						s_guard_no_fighter_log_budget--;
					}
					port_log(
					    "SSB64 NetRbSnapshot: guard_shield_prune tick=%u path=eject reason=no_fighter "
					    "effect_gobj_id=%u\n",
					    (unsigned int)syNetInputGetTick(), (unsigned int)gobj->id);
				}
				syNetRbSnapEndEffectProcUpdate(gobj, ep);
				syNetRbSnapEndEffectXfFuncProcs(gobj);
				ep->xf = NULL;
				syNetRbSnapClearAllFightersEffectPointerIfMatch(gobj);
				if (gobj->obj_kind == 0xFE)
				{
					syNetRbSnapUnlinkSentinelGObjFromLists(gobj);
					continue;
				}
				syNetRbSnapEjectGObj(gobj);
				continue;
			}
			fp = ftGetStruct(ep->fighter_gobj);
			if (fp == NULL)
			{
				if (syNetRbSnapTryRebindOrphanShieldEffect(gobj, ep) != FALSE)
				{
					fp = ftGetStruct(ep->fighter_gobj);
				}
				if ((fp == NULL) && (slot != NULL) &&
				    (syNetRbSnapTryRebindOrphanShieldEffectForLoad(gobj, ep, slot) != FALSE))
				{
					fp = ftGetStruct(ep->fighter_gobj);
				}
				if (fp == NULL)
				{
					syNetRbSnapClearAllFightersEffectPointerIfMatch(gobj);
					if (gobj->obj_kind == 0xFE)
					{
						syNetRbSnapUnlinkSentinelGObjFromLists(gobj);
						continue;
					}
					syNetRbSnapEjectGObj(gobj);
					continue;
				}
			}
			{
				s32 shield_player;
				GObj *owner_gobj;

				shield_player = syNetRbSnapShieldPlayerFromEffectVars(ep);
				if (shield_player >= 0)
				{
					owner_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)shield_player);
					if (owner_gobj != NULL)
					{
						if (ep->fighter_gobj != owner_gobj)
						{
							ep->fighter_gobj = owner_gobj;
							fp = ftGetStruct(owner_gobj);
							if (fp == NULL)
							{
								syNetRbSnapClearAllFightersEffectPointerIfMatch(gobj);
								syNetRbSnapEjectGObj(gobj);
								continue;
							}
						}
					}
				}
				if ((shield_player >= 0) && ((u32)shield_player != (u32)fp->player))
				{
					syNetRbSnapClearFighterEffectPointerIfMatch(fp, gobj);
					if (fp->is_effect_attach != FALSE)
					{
						fp->is_effect_attach = FALSE;
					}
					if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
					{
						port_log(
						    "SSB64 NetRbSnapshot: guard_shield_prune tick=%u path=eject reason=player_mismatch "
						    "player=%d shield_player=%d effect_gobj_id=%u\n",
						    (unsigned int)syNetRbSnapGuardShieldDiagTick(slot), (int)fp->player, (int)shield_player,
						    (unsigned int)gobj->id);
					}
					syNetRbSnapEjectGObj(gobj);
					continue;
				}
			}
			if ((live_forward_policy != FALSE) && (syNetRbSnapFighterGuardEffectUnionOwned(fp) != FALSE) &&
			    (syNetRbSnapFighterShieldReleasePending(fp) != FALSE))
			{
				const char *release_reason;
				GObj *owner_gobj;

				owner_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)fp->player);
				if ((owner_gobj == NULL) || (owner_gobj != ep->fighter_gobj) ||
				    ((u32)ep->effect_vars.shield.player != (u32)fp->player))
				{
					continue;
				}
				if (ftStatusVarsGuard(fp)->is_release != FALSE)
				{
					release_reason = "is_release";
				}
				else
				{
					ftStatusVarsGuard(fp)->is_release = TRUE;
					release_reason = "z_not_held";
				}
				syNetRbSnapFighterShieldReleaseTeardown(fp, gobj);
				if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
				{
					port_log(
					    "SSB64 NetRbSnapshot: guard_shield_prune tick=%u path=eject reason=%s "
					    "player=%d effect_gobj_id=%u status=%d release_lag=%d is_release=%d\n",
					    (unsigned int)syNetRbSnapGuardShieldDiagTick(slot), release_reason, (int)fp->player,
					    (unsigned int)gobj->id, (int)fp->status_id,
					    (int)ftStatusVarsGuard(fp)->release_lag, (int)ftStatusVarsGuard(fp)->is_release);
				}
				syNetRbSnapEjectGObj(gobj);
				continue;
			}
			if ((live_forward_policy != FALSE) && (syNetRbSnapFighterGuardEffectUnionOwned(fp) != FALSE) &&
			    (fp->is_shield != FALSE) && (syNetRbSnapFighterShieldInputHeldAuthoritative(fp) == FALSE))
			{
				syNetRbSnapFighterShieldScheduleAuthoritativeRelease(fp);
				if (syNetRbSnapFighterVanillaShieldReleaseDefer(fp) != FALSE)
				{
					if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
					{
						port_log(
						    "SSB64 NetRbSnapshot: guard_shield_release_defer tick=%u player=%d status=%d "
						    "release_lag=%d is_release=%d z_auth=0 z_live=%d\n",
						    (unsigned int)syNetRbSnapGuardShieldDiagTick(slot), (int)fp->player,
						    (int)fp->status_id, (int)ftStatusVarsGuard(fp)->release_lag,
						    (int)ftStatusVarsGuard(fp)->is_release,
						    (int)syNetRbSnapFighterShieldInputHeld(fp));
					}
				}
			}
			if (syNetRbSnapFighterGuardEffectUnionOwned(fp) != FALSE)
			{
				GObj *guard_eff;

				guard_eff = ftStatusVarsGuard(fp)->effect_gobj;
				if ((fp->is_shield != FALSE) && (guard_eff == NULL))
				{
					GObj *owner_gobj;

					owner_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)fp->player);
					if ((owner_gobj == NULL) || (owner_gobj != ep->fighter_gobj))
					{
						continue;
					}
					ftStatusVarsGuard(fp)->effect_gobj = gobj;
					fp->is_effect_attach = TRUE;
					if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
					{
						port_log(
						    "SSB64 NetRbSnapshot: guard_shield_prune tick=%u path=keep reason=guard_rebind "
						    "player=%d effect_gobj_id=%u fighter_gobj_id=%u\n",
						    (unsigned int)syNetRbSnapGuardShieldDiagTick(slot), (int)fp->player,
						    (unsigned int)gobj->id, (unsigned int)ep->fighter_gobj->id);
					}
					continue;
				}
				if ((guard_eff != NULL) && (guard_eff != gobj))
				{
					if (syNetRbSnapShieldEffectMatchesKeep(gobj, guard_eff) != FALSE)
					{
						ftStatusVarsGuard(fp)->effect_gobj = gobj;
						if (fp->is_shield != FALSE)
						{
							fp->is_effect_attach = TRUE;
						}
						if (syNetRbSnapCountLiveShieldEffectsForPlayer(fp->player) <= 1)
						{
							if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
							{
								port_log(
								    "SSB64 NetRbSnapshot: guard_shield_prune tick=%u path=keep reason=guard_id_rebind "
								    "player=%d effect_gobj_id=%u\n",
								    (unsigned int)syNetRbSnapGuardShieldDiagTick(slot), (int)fp->player,
								    (unsigned int)gobj->id);
							}
							continue;
						}
						/*
						 * Phase 20: during resim load defer inline duplicate eject — the same tick runs
						 * PruneDuplicateShieldEffects once with blob-authoritative keep (avoids 7× burst).
						 * Synctest verify-only: patch blob fields immediately instead of deferring.
						 */
						if (slot != NULL)
						{
							if (s_syNetRbSnapRepairStageVerifyOnly != FALSE)
							{
								const SYNetRbSnapFighterBlob *patch_fb;

								patch_fb = &slot->fighters[fp->player];
								syNetRbSnapPatchLiveShieldFromSlot(
								    slot, ep->fighter_gobj, fp,
								    syNetRbSnapGuardEffectIdFromBlob(patch_fb), gobj);
								ftStatusVarsGuard(fp)->effect_gobj = gobj;
								if (fp->is_shield != FALSE)
								{
									fp->is_effect_attach = TRUE;
								}
								if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
								{
									port_log(
									    "SSB64 NetRbSnapshot: guard_shield_prune tick=%u path=keep reason=verify_patch "
									    "player=%d effect_gobj_id=%u\n",
									    (unsigned int)syNetRbSnapGuardShieldDiagTick(slot), (int)fp->player,
									    (unsigned int)gobj->id);
								}
								continue;
							}
							ftStatusVarsGuard(fp)->effect_gobj = gobj;
							if (fp->is_shield != FALSE)
							{
								fp->is_effect_attach = TRUE;
							}
							if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
							{
								port_log(
								    "SSB64 NetRbSnapshot: guard_shield_prune tick=%u path=keep reason=guard_id_rebind_resim_defer "
								    "player=%d effect_gobj_id=%u\n",
								    (unsigned int)syNetRbSnapGuardShieldDiagTick(slot), (int)fp->player,
								    (unsigned int)gobj->id);
							}
							continue;
						}
						if (syNetRbSnapShieldEffectMatchesKeep(gobj, guard_eff) != FALSE)
						{
							ftStatusVarsGuard(fp)->effect_gobj = gobj;
							if (fp->is_shield != FALSE)
							{
								fp->is_effect_attach = TRUE;
							}
							if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
							{
								port_log(
								    "SSB64 NetRbSnapshot: guard_shield_prune tick=%u path=keep reason=guard_id_rebind_live "
								    "player=%d effect_gobj_id=%u\n",
								    (unsigned int)syNetRbSnapGuardShieldDiagTick(slot), (int)fp->player,
								    (unsigned int)gobj->id);
							}
							continue;
						}
						if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
						{
							port_log(
							    "SSB64 NetRbSnapshot: guard_shield_prune tick=%u path=eject reason=guard_id_rebind_duplicate "
							    "player=%d effect_gobj_id=%u\n",
							    (unsigned int)syNetRbSnapGuardShieldDiagTick(slot), (int)fp->player,
							    (unsigned int)gobj->id);
						}
						syNetRbSnapClearFighterEffectPointerIfMatch(fp, gobj);
						syNetRbSnapEjectGObj(gobj);
						continue;
					}
					if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
					{
						port_log(
						    "SSB64 NetRbSnapshot: guard_shield_prune tick=%u path=eject reason=guard_ptr_mismatch "
						    "player=%d effect_gobj_id=%u guard_gobj_id=%u\n",
						    (unsigned int)syNetRbSnapGuardShieldDiagTick(slot), (int)fp->player,
						    (unsigned int)gobj->id, syNetRbSnapGobjId(guard_eff));
					}
					syNetRbSnapClearFighterEffectPointerIfMatch(fp, gobj);
					syNetRbSnapEjectGObj(gobj);
					continue;
				}
			}
			shielding = fp->is_shield;
			if (slot != NULL)
			{
				pi = fp->player;
				if ((pi >= 0) && (pi < GMCOMMON_PLAYERS_MAX))
				{
					fb = &slot->fighters[pi];
					if ((fb->is_valid != FALSE) && (fb->is_shield != FALSE))
					{
						shielding = TRUE;
					}
					else if (fb->is_valid != FALSE)
					{
						shielding = FALSE;
					}
				}
			}
			if (shielding == FALSE)
			{
				syNetRbSnapClearFighterEffectPointerIfMatch(fp, gobj);
				if (fp->is_effect_attach != FALSE)
				{
					fp->is_effect_attach = FALSE;
				}
				if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
				{
					port_log(
					    "SSB64 NetRbSnapshot: guard_shield_prune tick=%u path=eject reason=not_shielding "
					    "player=%d effect_gobj_id=%u fighter_gobj_id=%u status=%d is_shield=%d\n",
					    (unsigned int)((slot != NULL) ? slot->tick : 0U), (int)fp->player,
					    (unsigned int)gobj->id, (unsigned int)ep->fighter_gobj->id, (int)fp->status_id,
					    (int)(fp->is_shield != FALSE));
				}
				syNetRbSnapEjectGObj(gobj);
				continue;
			}
			if (slot == NULL)
			{
				continue;
			}
			pi = fp->player;
			if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX))
			{
				continue;
			}
			fb = &slot->fighters[pi];
			if (fb->is_shield == FALSE)
			{
				syNetRbSnapClearFighterEffectPointerIfMatch(fp, gobj);
				if (fp->is_effect_attach != FALSE)
				{
					fp->is_effect_attach = FALSE;
				}
				if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
				{
					port_log(
					    "SSB64 NetRbSnapshot: guard_shield_prune tick=%u path=eject reason=blob_not_shielding "
					    "player=%d effect_gobj_id=%u\n",
					    (unsigned int)slot->tick, (int)pi, (unsigned int)gobj->id);
				}
				syNetRbSnapEjectGObj(gobj);
				continue;
			}
			expected_id = syNetRbSnapGuardEffectIdFromBlob(fb);
			if ((expected_id != 0U) && (expected_id != (u32)gobj->id))
			{
				GObj *expected_live = gcFindGObjByID(expected_id);

				if ((expected_live != NULL) && (efGetStruct(expected_live) != NULL) &&
				    (syNetRbSnapLiveEffectIsShield(expected_live, efGetStruct(expected_live)) != FALSE))
				{
					/*
					 * The blob's bubble also exists live, so this is a genuine duplicate — drop the extra.
					 */
					syNetRbSnapClearFighterEffectPointerIfMatch(fp, gobj);
					if (fp->is_effect_attach != FALSE)
					{
						fp->is_effect_attach = FALSE;
					}
					if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
					{
						port_log(
						    "SSB64 NetRbSnapshot: guard_shield_prune tick=%u path=eject reason=unexpected_id_duplicate "
						    "player=%d effect_gobj_id=%u expected_id=%u\n",
						    (unsigned int)slot->tick, (int)pi, (unsigned int)gobj->id, expected_id);
					}
					syNetRbSnapEjectGObj(gobj);
				}
				else
				{
					/*
					 * Stable per-player identity: the player IS shielding (fb->is_shield checked above) and
					 * this is their only live bubble — the resim simply re-created it under a fresh pool id.
					 * Adopt it as the guard effect rather than ejecting (the old id-eject + respawn churn was
					 * the residual eff-drift source on fast re-tap). Pool id is presentation, not identity.
					 */
					ftStatusVarsGuard(fp)->effect_gobj = gobj;
					fp->is_effect_attach = TRUE;
					if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
					{
						port_log(
						    "SSB64 NetRbSnapshot: guard_shield_prune tick=%u path=keep reason=slot_id_adopt "
						    "player=%d effect_gobj_id=%u expected_id=%u\n",
						    (unsigned int)slot->tick, (int)pi, (unsigned int)gobj->id, expected_id);
					}
				}
			}
		}
	}
}

static void syNetRbSnapPruneDuplicateShieldEffects(const SYNetRbSnapshotSlot *slot)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		GObj *keep_gobj;
		s32 pass;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			continue;
		}
		if (syNetRbSnapCountLiveShieldEffectsForPlayer(fp->player) <= 1)
		{
			continue;
		}
		keep_gobj = NULL;
		if ((slot != NULL) && (fp->player >= 0) && (fp->player < GMCOMMON_PLAYERS_MAX))
		{
			const SYNetRbSnapFighterBlob *fb = &slot->fighters[fp->player];
			u32 expected_id;

			if ((fb->is_valid != FALSE) && (syNetRbSnapBlobInGuardScope(fb) != FALSE))
			{
				expected_id = syNetRbSnapGuardEffectIdFromBlob(fb);
				if (expected_id != 0U)
				{
					keep_gobj = gcFindGObjByID(expected_id);
				}
				if (keep_gobj == NULL)
				{
					const SYNetRbSnapEffectBlob *shield_blob;

					shield_blob = syNetRbSnapFindShieldEffectBlobForPlayer(slot, fp->player);
					if (shield_blob != NULL)
					{
						keep_gobj = gcFindGObjByID(shield_blob->gobj_id);
					}
				}
			}
		}
		if (keep_gobj == NULL)
		{
			keep_gobj = syNetRbSnapFindLiveShieldEffectForFighter(fighter_gobj);
		}
		if ((keep_gobj == NULL) && (slot != NULL) && (syNetRbSnapFighterInGuardScope(fp) != FALSE) &&
		    (syNetRbSnapFighterGuardEffectUnionOwned(fp) != FALSE))
		{
			GObj *coupled;
			EFStruct *keep_ep;

			/*
			 * Snapshot apply: prefer the canonical live shield lookup over guard->effect_gobj.
			 * Early slot_id_adopt can point at an emergency bubble that lacks blob fields until
			 * ReconcileSnapshotEffectsBeforeItems runs; guard coupling is the fallback only.
			 */
			coupled = ftStatusVarsGuard(fp)->effect_gobj;
			coupled = syNetRbSnapResolveCoupledGobj(coupled);
			keep_ep = (coupled != NULL) ? efGetStruct(coupled) : NULL;
			if ((keep_ep != NULL) && ((u32)keep_ep->effect_vars.shield.player == (u32)fp->player) &&
			    syNetRbSnapLiveShieldEffectOwnedByFighter(keep_ep, fighter_gobj, fp->player) != FALSE)
			{
				keep_gobj = coupled;
			}
		}
		else if ((keep_gobj == NULL) && (slot == NULL) && (syNetRbSnapFighterInGuardScope(fp) != FALSE) &&
		         (syNetRbSnapFighterGuardEffectUnionOwned(fp) != FALSE))
		{
			GObj *coupled;
			EFStruct *keep_ep;

			coupled = ftStatusVarsGuard(fp)->effect_gobj;
			coupled = syNetRbSnapResolveCoupledGobj(coupled);
			keep_ep = (coupled != NULL) ? efGetStruct(coupled) : NULL;
			if ((keep_ep != NULL) && ((u32)keep_ep->effect_vars.shield.player == (u32)fp->player) &&
			    syNetRbSnapLiveShieldEffectOwnedByFighter(keep_ep, fighter_gobj, fp->player) != FALSE)
			{
				keep_gobj = coupled;
			}
		}
		keep_gobj = syNetRbSnapResolveCoupledGobj(keep_gobj);
		for (pass = 0; pass < 2; pass++)
		{
			GObj *gobj;
			GObj *next;

			for (gobj = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
			     gobj != NULL; gobj = next)
			{
				EFStruct *ep;

				next = gobj->link_next;
				ep = efGetStruct(gobj);

				if (syNetRbSnapLiveEffectIsShield(gobj, ep) == FALSE)
				{
					continue;
				}
				if (syNetRbSnapLiveShieldEffectOwnedByFighter(ep, fighter_gobj, fp->player) == FALSE)
				{
					continue;
				}
				if (syNetRbSnapShieldEffectMatchesKeep(gobj, keep_gobj) != FALSE)
				{
					continue;
				}
				if (keep_gobj == NULL)
				{
					keep_gobj = gobj;
					continue;
				}
				syNetRbSnapClearFighterEffectPointerIfMatch(fp, gobj);
				if (fp->is_effect_attach != FALSE)
				{
					fp->is_effect_attach = FALSE;
				}
				if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
				{
					port_log(
					    "SSB64 NetRbSnapshot: guard_shield_prune tick=%u path=eject reason=duplicate "
					    "player=%d effect_gobj_id=%u keep_gobj_id=%u\n",
					    (unsigned int)((slot != NULL) ? slot->tick : 0U), (int)fp->player,
					    (unsigned int)gobj->id, (keep_gobj != NULL) ? syNetRbSnapGobjId(keep_gobj) : 0U);
				}
				syNetRbSnapEjectGObj(gobj);
			}
		}
	}
}

static void syNetRbSnapPruneGuardOnExtraShieldEffects(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		GObj *keep_gobj;
		s32 pass;

		fp = ftGetStruct(fighter_gobj);
		if ((fp == NULL) || (fp->status_id != nFTCommonStatusGuardOn) ||
		    (syNetRbSnapFighterGuardEffectUnionOwned(fp) == FALSE))
		{
			continue;
		}
		/*
		 * Vanilla Yoshi has no egg bubble at GuardOn (only invincible hurtboxes). Eject any orphan bubble
		 * left by rollback repair while is_shield is still clear.
		 */
		if ((fp->fkind == nFTKindYoshi) && (fp->is_shield == FALSE))
		{
			s32 orphan_pass;

			for (orphan_pass = 0; orphan_pass < 2; orphan_pass++)
			{
				GObj *gobj;
				GObj *next;

				for (gobj = gGCCommonLinks[(orphan_pass == 0) ? nGCCommonLinkIDEffect
				                                              : nGCCommonLinkIDSpecialEffect];
				     gobj != NULL; gobj = next)
				{
					EFStruct *ep;

					next = gobj->link_next;
					ep = efGetStruct(gobj);
					if (syNetRbSnapLiveEffectIsShield(gobj, ep) == FALSE)
					{
						continue;
					}
					if (ep->fighter_gobj != fighter_gobj)
					{
						continue;
					}
					syNetRbSnapClearFighterEffectPointerIfMatch(fp, gobj);
					ftStatusVarsGuard(fp)->effect_gobj = NULL;
					fp->is_effect_attach = FALSE;
					if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
					{
						port_log(
						    "SSB64 NetRbSnapshot: guard_shield_prune tick=0 path=eject "
						    "reason=yoshi_guard_on_orphan player=%d effect_gobj_id=%u\n",
						    (int)fp->player, (unsigned int)gobj->id);
					}
					syNetRbSnapEjectGObj(gobj);
				}
			}
			continue;
		}
		if (syNetRbSnapCountLiveShieldEffectsForFighter(fighter_gobj) <= 1)
		{
			continue;
		}
		keep_gobj = ftStatusVarsGuard(fp)->effect_gobj;
		if (keep_gobj == NULL)
		{
			keep_gobj = syNetRbSnapFindLiveShieldEffectForFighter(fighter_gobj);
			if (keep_gobj != NULL)
			{
				ftStatusVarsGuard(fp)->effect_gobj = keep_gobj;
			}
		}
		for (pass = 0; pass < 2; pass++)
		{
			GObj *gobj;
			GObj *next;

			for (gobj = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
			     gobj != NULL; gobj = next)
			{
				EFStruct *ep;

				next = gobj->link_next;
				ep = efGetStruct(gobj);
				if (syNetRbSnapLiveEffectIsShield(gobj, ep) == FALSE)
				{
					continue;
				}
				if (ep->fighter_gobj != fighter_gobj)
				{
					continue;
				}
				if (syNetRbSnapShieldEffectMatchesKeep(gobj, keep_gobj) != FALSE)
				{
					continue;
				}
				if (keep_gobj == NULL)
				{
					keep_gobj = gobj;
					ftStatusVarsGuard(fp)->effect_gobj = gobj;
					if (fp->is_shield != FALSE)
					{
						fp->is_effect_attach = TRUE;
					}
					continue;
				}
				syNetRbSnapClearFighterEffectPointerIfMatch(fp, gobj);
				if (fp->is_effect_attach != FALSE)
				{
					fp->is_effect_attach = FALSE;
				}
				if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
				{
					port_log(
					    "SSB64 NetRbSnapshot: guard_shield_prune tick=0 path=eject reason=guard_on_duplicate "
					    "player=%d effect_gobj_id=%u keep_gobj_id=%u\n",
					    (int)fp->player, (unsigned int)gobj->id, syNetRbSnapGobjId(keep_gobj));
				}
				syNetRbSnapEjectGObj(gobj);
			}
		}
	}
}

static void syNetRbSnapClearGuardShieldCouplingWhenOutOfScope(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;

		fp = ftGetStruct(fighter_gobj);
		if ((fp == NULL) || (fp->is_shield != FALSE) || (syNetRbSnapFighterGuardEffectUnionOwned(fp) == FALSE))
		{
			continue;
		}
		if (syNetRbSnapFindLiveShieldEffectForFighter(fighter_gobj) != NULL)
		{
			continue;
		}
		ftStatusVarsGuard(fp)->effect_gobj = NULL;
	}
}

static void syNetRbSnapReconcileFighterShieldCoupling(sb32 live_forward_policy)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		GObj *guard_gobj;
		GObj *live_shield;
		u32 guard_id;
		sb32 release_pending;

		fp = ftGetStruct(fighter_gobj);
		if ((fp == NULL) || (syNetRbSnapFighterGuardEffectUnionOwned(fp) == FALSE))
		{
			continue;
		}
		release_pending =
		    (live_forward_policy != FALSE) ? syNetRbSnapFighterShieldReleasePending(fp) : FALSE;
		guard_gobj = syNetRbSnapResolveCoupledGobj(ftStatusVarsGuard(fp)->effect_gobj);
		if (guard_gobj != ftStatusVarsGuard(fp)->effect_gobj)
		{
			ftStatusVarsGuard(fp)->effect_gobj = guard_gobj;
		}
		if (guard_gobj != NULL)
		{
			guard_id = syNetRbSnapGobjId(guard_gobj);
			if ((guard_id == 0U) || (gcFindGObjByID(guard_id) != guard_gobj) ||
			    (syNetRbSnapLiveEffectIsShield(guard_gobj, efGetStruct(guard_gobj)) == FALSE))
			{
				ftStatusVarsGuard(fp)->effect_gobj = NULL;
				if (fp->is_effect_attach != FALSE)
				{
					fp->is_effect_attach = FALSE;
				}
				guard_gobj = NULL;
			}
		}
		live_shield = syNetRbSnapFindLiveShieldEffectForFighter(fighter_gobj);
		if ((fp->is_shield != FALSE) && (guard_gobj == NULL) && (live_shield == NULL))
		{
			continue;
		}
		if ((fp->is_shield != FALSE) && (guard_gobj != NULL) && (live_shield == NULL))
		{
			EFStruct *guard_ep = efGetStruct(guard_gobj);

			if ((guard_ep != NULL) && (syNetRbSnapLiveEffectIsShield(guard_gobj, guard_ep) != FALSE) &&
			    (syNetRbSnapLiveShieldEffectOwnedByFighter(guard_ep, fighter_gobj, fp->player) != FALSE))
			{
				syNetRbSnapAuditLiveShieldEffectOwner(guard_gobj, fp);
				live_shield = guard_gobj;
				ftStatusVarsGuard(fp)->effect_gobj = guard_gobj;
				if (fp->is_effect_attach == FALSE)
				{
					fp->is_effect_attach = TRUE;
				}
			}
			else
			{
				ftStatusVarsGuard(fp)->effect_gobj = NULL;
				if (fp->is_effect_attach != FALSE)
				{
					fp->is_effect_attach = FALSE;
				}
				guard_gobj = NULL;
			}
		}
		if ((fp->is_shield != FALSE) && (release_pending == FALSE))
		{
			GObj *shield_gobj = (guard_gobj != NULL) ? guard_gobj : live_shield;

			if (shield_gobj != NULL)
			{
				EFStruct *ep = efGetStruct(shield_gobj);

				if ((ep != NULL) && ((ep->fighter_gobj != fighter_gobj) ||
				                     ((u32)ep->effect_vars.shield.player != (u32)fp->player)))
				{
					/*
					 * Stale guard->effect_gobj can resolve to another fighter's bubble after GObj id reuse;
					 * decouple locally only — never eject a peer's shield from this pass.
					 */
					ftStatusVarsGuard(fp)->effect_gobj = NULL;
					if (fp->is_effect_attach != FALSE)
					{
						fp->is_effect_attach = FALSE;
					}
					guard_gobj = NULL;
				}
			}
		}
		if ((live_shield != NULL) && (guard_gobj != live_shield) && (release_pending == FALSE))
		{
			ftStatusVarsGuard(fp)->effect_gobj = live_shield;
			if (fp->is_shield != FALSE)
			{
				fp->is_effect_attach = TRUE;
			}
		}
	}
}

static void syNetRbSnapPruneWaitOrphanShieldEffects(const SYNetRbSnapshotSlot *slot)
{
	s32 pass;

	if (slot != NULL)
	{
		return;
	}
	for (pass = 0; pass < 2; pass++)
	{
		GObj *gobj;
		GObj *next;

		for (gobj = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect]; gobj != NULL;
		     gobj = next)
		{
			EFStruct *ep;
			FTStruct *fp;
			GObj *owner_gobj;

			next = gobj->link_next;
			ep = efGetStruct(gobj);
			if (syNetRbSnapLiveEffectIsShield(gobj, ep) == FALSE)
			{
				continue;
			}
			if (ep->fighter_gobj == NULL)
			{
				continue;
			}
			fp = ftGetStruct(ep->fighter_gobj);
			if ((fp == NULL) || (fp->status_id != nFTCommonStatusWait))
			{
				continue;
			}
			if (syNetRbSnapFighterShieldInputHeldAuthoritative(fp) != FALSE)
			{
				continue;
			}
			owner_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)fp->player);
			if ((owner_gobj == NULL) || (owner_gobj != ep->fighter_gobj) ||
			    ((u32)ep->effect_vars.shield.player != (u32)fp->player))
			{
				continue;
			}
			if (fp->is_shield != FALSE)
			{
				syNetRbSnapFighterShieldReleaseTeardown(fp, gobj);
			}
			else
			{
				syNetRbSnapClearFighterEffectPointerIfMatch(fp, gobj);
				ftStatusVarsGuard(fp)->effect_gobj = NULL;
				fp->is_effect_attach = FALSE;
			}
			if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 NetRbSnapshot: guard_shield_prune tick=%u path=eject reason=wait_z_not_held_orphan "
				    "player=%d effect_gobj_id=%u is_shield=%d release_lag=%d\n",
				    (unsigned int)syNetRbSnapGuardShieldDiagTick(slot), (int)fp->player, (unsigned int)gobj->id,
				    (int)fp->is_shield, (int)ftStatusVarsGuard(fp)->release_lag);
			}
			syNetRbSnapEjectGObj(gobj);
		}
	}
}

static sb32 syNetRbSnapFighterShieldApplyHealEligible(const FTStruct *fp, const SYNetRbSnapFighterBlob *blob,
                                                      const char **skip_reason_out)
{
	if (skip_reason_out != NULL)
	{
		*skip_reason_out = NULL;
	}
	if ((fp == NULL) || (blob == NULL) || (blob->is_valid == FALSE) || (fp->is_shield == FALSE))
	{
		return FALSE;
	}
	/*
	 * Snapshot apply: blob is authoritative for verify. Never clear fp->is_shield when the ring slot
	 * captured is_shield=1 (e.g. GuardOff release-lag defer with effect_count=0).
	 */
	if (blob->is_shield != FALSE)
	{
		if (skip_reason_out != NULL)
		{
			*skip_reason_out = "blob_is_shield";
		}
		return FALSE;
	}
	/*
	 * Apply path: no z_held gate — heal stale live is_shield when the slot blob says not shielding.
	 * GuardOff release-complete is included so rollback load does not trap R→grab after bubble teardown.
	 */
	if (fp->status_id == nFTCommonStatusWait)
	{
		return TRUE;
	}
	if (fp->status_id == nFTCommonStatusGuardSetOff)
	{
		return TRUE;
	}
	if ((fp->status_id == nFTCommonStatusGuardOff) && (ftStatusVarsGuard(fp)->release_lag == 0) &&
	    (ftStatusVarsGuard(fp)->is_release != FALSE))
	{
		return TRUE;
	}
	if (syNetRbSnapFighterInActiveGuardStatus(fp) != FALSE)
	{
		if (skip_reason_out != NULL)
		{
			*skip_reason_out = "active_guard_window";
		}
		return FALSE;
	}
	if ((fp->status_id > nFTCommonStatusGuardEnd) || (fp->status_id < nFTCommonStatusGuardStart))
	{
		return TRUE;
	}
	if (skip_reason_out != NULL)
	{
		*skip_reason_out = "in_guard_status";
	}
	return FALSE;
}

static void syNetRbSnapPerformFighterShieldHeal(GObj *fighter_gobj, FTStruct *fp, const SYNetRbSnapshotSlot *slot,
                                                const char *heal_reason)
{
	sb32 had_release_lag;
	sb32 z_auth;
	sb32 z_live;

	if ((fighter_gobj == NULL) || (fp == NULL) || (heal_reason == NULL))
	{
		return;
	}
	had_release_lag =
	    (ftStatusVarsGuard(fp)->is_release != FALSE) || (ftStatusVarsGuard(fp)->release_lag > 0);
	if (fp->fkind == nFTKindYoshi)
	{
		syNetRbSnapTeardownYoshiShieldPresentation(fighter_gobj, fp, FALSE);
	}
	ftStatusVarsGuard(fp)->effect_gobj = NULL;
	fp->is_effect_attach = FALSE;
	fp->is_shield = FALSE;
	if (had_release_lag != FALSE)
	{
		ftStatusVarsGuard(fp)->is_release = FALSE;
		ftStatusVarsGuard(fp)->release_lag = 0;
	}
	if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
	{
		z_auth = syNetRbSnapFighterShieldInputHeldAuthoritative(fp);
		z_live = syNetRbSnapFighterShieldInputHeld(fp);
		port_log(
		    "SSB64 NetRbSnapshot: guard_shield_heal tick=%u player=%d status=%d reason=%s "
		    "shield_health=%d z_auth=%d z_live=%d is_release=%d release_lag=%d\n",
		    (unsigned int)syNetRbSnapGuardShieldDiagTick(slot), (int)fp->player, (int)fp->status_id, heal_reason,
		    (int)fp->shield_health, (int)z_auth, (int)z_live, (int)ftStatusVarsGuard(fp)->is_release,
		    (int)ftStatusVarsGuard(fp)->release_lag);
	}
}

static sb32 syNetRbSnapFighterShieldHealCandidateReady(GObj *fighter_gobj, FTStruct *fp)
{
	GObj *guard_gobj;
	GObj *live_shield;

	if ((fighter_gobj == NULL) || (fp == NULL) || (fp->is_shield == FALSE) ||
	    (syNetRbSnapFighterInGuardScope(fp) == FALSE) ||
	    (syNetRbSnapFighterGuardEffectUnionOwned(fp) == FALSE))
	{
		return FALSE;
	}
	live_shield = syNetRbSnapFindLiveShieldEffectForFighter(fighter_gobj);
	if (live_shield != NULL)
	{
		return FALSE;
	}
	guard_gobj = syNetRbSnapResolveCoupledGobj(ftStatusVarsGuard(fp)->effect_gobj);
	if ((guard_gobj != NULL) && (syNetRbSnapLiveEffectIsShield(guard_gobj, efGetStruct(guard_gobj)) != FALSE))
	{
		EFStruct *guard_ep = efGetStruct(guard_gobj);

		if ((guard_ep != NULL) &&
		    (syNetRbSnapLiveShieldEffectOwnedByFighter(guard_ep, fighter_gobj, fp->player) != FALSE))
		{
			return FALSE;
		}
	}
	return TRUE;
}

static void syNetRbSnapHealFighterShieldWithoutEffect(const SYNetRbSnapshotSlot *slot)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		const char *heal_skip_reason;
		const char *heal_reason;

		fp = ftGetStruct(fighter_gobj);
		if (syNetRbSnapFighterShieldHealCandidateReady(fighter_gobj, fp) == FALSE)
		{
			continue;
		}
		if (syNetRbSnapFighterShieldHealEligible(fp, &heal_skip_reason) == FALSE)
		{
			if ((syNetRbSnapSnapshotEffectDiagEnabled() != FALSE) && (heal_skip_reason != NULL))
			{
				sb32 z_auth = syNetRbSnapFighterShieldInputHeldAuthoritative(fp);
				sb32 z_live = syNetRbSnapFighterShieldInputHeld(fp);

				port_log(
				    "SSB64 NetRbSnapshot: guard_shield_heal_skip tick=%u player=%d status=%d "
				    "reason=%s shield_health=%d z_auth=%d z_live=%d is_release=%d release_lag=%d\n",
				    (unsigned int)syNetRbSnapGuardShieldDiagTick(slot), (int)fp->player,
				    (int)fp->status_id, heal_skip_reason, (int)fp->shield_health, (int)z_auth,
				    (int)z_live, (int)ftStatusVarsGuard(fp)->is_release,
				    (int)ftStatusVarsGuard(fp)->release_lag);
			}
			continue;
		}
		if (fp->status_id == nFTCommonStatusWait)
		{
			heal_reason = "wait_no_effect";
		}
		else if (fp->status_id == nFTCommonStatusGuardSetOff)
		{
			heal_reason = "guard_setoff_stale";
		}
		else
		{
			heal_reason = "post_guard_stale";
		}
		syNetRbSnapPerformFighterShieldHeal(fighter_gobj, fp, slot, heal_reason);
	}
}

static void syNetRbSnapHealFighterShieldOnApply(const SYNetRbSnapshotSlot *slot)
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
		const SYNetRbSnapFighterBlob *fb;
		const char *heal_skip_reason;
		const char *heal_reason;
		s32 pi;

		fp = ftGetStruct(fighter_gobj);
		if (syNetRbSnapFighterShieldHealCandidateReady(fighter_gobj, fp) == FALSE)
		{
			continue;
		}
		pi = fp->player;
		if ((pi < 0) || (pi >= GMCOMMON_PLAYERS_MAX))
		{
			continue;
		}
		fb = &slot->fighters[pi];
		if (syNetRbSnapFighterShieldApplyHealEligible(fp, fb, &heal_skip_reason) == FALSE)
		{
			if ((syNetRbSnapSnapshotEffectDiagEnabled() != FALSE) && (heal_skip_reason != NULL))
			{
				port_log(
				    "SSB64 NetRbSnapshot: guard_shield_heal_apply_skip tick=%u player=%d status=%d "
				    "reason=%s blob_is_shield=%d guard_effect_id=%u\n",
				    (unsigned int)syNetRbSnapGuardShieldDiagTick(slot), (int)fp->player, (int)fp->status_id,
				    heal_skip_reason, (int)(fb->is_shield != FALSE),
				    (unsigned int)syNetRbSnapGuardEffectIdFromBlob(fb));
			}
			continue;
		}
		if (fp->status_id == nFTCommonStatusWait)
		{
			heal_reason = "apply_wait_stale";
		}
		else if (fp->status_id == nFTCommonStatusGuardSetOff)
		{
			heal_reason = "apply_guard_setoff_stale";
		}
		else if (fp->status_id == nFTCommonStatusGuardOff)
		{
			heal_reason = "apply_guardoff_stale";
		}
		else
		{
			heal_reason = "apply_post_guard_stale";
		}
		syNetRbSnapPerformFighterShieldHeal(fighter_gobj, fp, slot, heal_reason);
	}
}

static void syNetRbSnapDiagGuardShieldWithoutBubbleGap(const SYNetRbSnapshotSlot *slot)
{
#if defined(SSB64_NETMENU)
	GObj *fighter_gobj;
	static u32 s_no_bubble_streak[MAXCONTROLLERS];
	static u32 s_no_bubble_last_log_tick[MAXCONTROLLERS];

	if ((slot != NULL) || (syNetRbSnapSnapshotEffectDiagEnabled() == FALSE))
	{
		return;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		s32 player;
		sb32 z_auth;
		u32 tick;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			continue;
		}
		player = fp->player;
		if ((player < 0) || (player >= MAXCONTROLLERS))
		{
			continue;
		}
		tick = syNetRbSnapGuardShieldDiagTick(slot);
		z_auth = syNetRbSnapFighterShieldInputHeldAuthoritative(fp);
		if ((fp->is_shield != FALSE) && (z_auth != FALSE) &&
		    (syNetRbSnapFighterInActiveGuardStatus(fp) != FALSE) &&
		    (syNetRbSnapFindLiveShieldEffectForFighter(fighter_gobj) == NULL))
		{
			s_no_bubble_streak[player]++;
			if (s_no_bubble_streak[player] == 3U)
			{
				port_log(
				    "SSB64 NetRbSnapshot: guard_shield_gap tick=%u player=%d status=%d streak=%u "
				    "shield_health=%d is_release=%d release_lag=%d z_live=%d\n",
				    (unsigned int)tick, player, (int)fp->status_id, (unsigned int)s_no_bubble_streak[player],
				    (int)fp->shield_health, (int)ftStatusVarsGuard(fp)->is_release,
				    (int)ftStatusVarsGuard(fp)->release_lag, (int)syNetRbSnapFighterShieldInputHeld(fp));
				s_no_bubble_last_log_tick[player] = tick;
			}
			else if ((s_no_bubble_streak[player] > 3U) && ((tick - s_no_bubble_last_log_tick[player]) >= 5U))
			{
				port_log(
				    "SSB64 NetRbSnapshot: guard_shield_gap tick=%u player=%d status=%d streak=%u "
				    "shield_health=%d is_release=%d release_lag=%d z_live=%d\n",
				    (unsigned int)tick, player, (int)fp->status_id, (unsigned int)s_no_bubble_streak[player],
				    (int)fp->shield_health, (int)ftStatusVarsGuard(fp)->is_release,
				    (int)ftStatusVarsGuard(fp)->release_lag, (int)syNetRbSnapFighterShieldInputHeld(fp));
				s_no_bubble_last_log_tick[player] = tick;
			}
		}
		else if (s_no_bubble_streak[player] >= 3U)
		{
			port_log(
			    "SSB64 NetRbSnapshot: guard_shield_gap_end tick=%u player=%d streak=%u status=%d "
			    "is_shield=%d z_auth=%d\n",
			    (unsigned int)tick, player, (unsigned int)s_no_bubble_streak[player], (int)fp->status_id,
			    (int)(fp->is_shield != FALSE), (int)z_auth);
			s_no_bubble_streak[player] = 0U;
			s_no_bubble_last_log_tick[player] = 0U;
		}
		else
		{
			s_no_bubble_streak[player] = 0U;
			s_no_bubble_last_log_tick[player] = 0U;
		}
	}
#endif
}

static void syNetRbSnapDiagGuardShieldBubbleLinger(const SYNetRbSnapshotSlot *slot)
{
#if defined(SSB64_NETMENU)
	GObj *fighter_gobj;
	static u32 s_linger_streak[MAXCONTROLLERS];
	static u32 s_linger_last_log_tick[MAXCONTROLLERS];

	if ((slot != NULL) || (syNetRbSnapSnapshotEffectDiagEnabled() == FALSE))
	{
		return;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		GObj *live_bubble;
		sb32 z_auth;
		sb32 linger;
		s32 player;
		u32 tick;

		fp = ftGetStruct(fighter_gobj);
		if (fp == NULL)
		{
			continue;
		}
		player = fp->player;
		if ((player < 0) || (player >= MAXCONTROLLERS))
		{
			continue;
		}
		live_bubble = syNetRbSnapFindLiveShieldEffectForFighter(fighter_gobj);
		if (live_bubble == NULL)
		{
			if (s_linger_streak[player] >= 3U)
			{
				tick = syNetRbSnapGuardShieldDiagTick(slot);
				port_log(
				    "SSB64 NetRbSnapshot: guard_shield_linger_end tick=%u player=%d streak=%u status=%d "
				    "is_shield=%d z_auth=%d\n",
				    (unsigned int)tick, player, (unsigned int)s_linger_streak[player], (int)fp->status_id,
				    (int)(fp->is_shield != FALSE), (int)syNetRbSnapFighterShieldInputHeldAuthoritative(fp));
			}
			s_linger_streak[player] = 0U;
			s_linger_last_log_tick[player] = 0U;
			continue;
		}
		z_auth = syNetRbSnapFighterShieldInputHeldAuthoritative(fp);
		linger = (z_auth == FALSE) || (syNetRbSnapFighterInActiveGuardStatus(fp) == FALSE);
		if (linger == FALSE)
		{
			if (s_linger_streak[player] >= 3U)
			{
				tick = syNetRbSnapGuardShieldDiagTick(slot);
				port_log(
				    "SSB64 NetRbSnapshot: guard_shield_linger_end tick=%u player=%d streak=%u status=%d "
				    "is_shield=%d z_auth=%d\n",
				    (unsigned int)tick, player, (unsigned int)s_linger_streak[player], (int)fp->status_id,
				    (int)(fp->is_shield != FALSE), (int)z_auth);
			}
			s_linger_streak[player] = 0U;
			s_linger_last_log_tick[player] = 0U;
			continue;
		}
		tick = syNetRbSnapGuardShieldDiagTick(slot);
		s_linger_streak[player]++;
		if (s_linger_streak[player] == 3U)
		{
			port_log(
			    "SSB64 NetRbSnapshot: guard_shield_linger tick=%u player=%d status=%d streak=%u "
			    "is_shield=%d z_auth=%d is_release=%d release_lag=%d z_live=%d\n",
			    (unsigned int)tick, player, (int)fp->status_id, (unsigned int)s_linger_streak[player],
			    (int)(fp->is_shield != FALSE), (int)z_auth, (int)ftStatusVarsGuard(fp)->is_release,
			    (int)ftStatusVarsGuard(fp)->release_lag, (int)syNetRbSnapFighterShieldInputHeld(fp));
			s_linger_last_log_tick[player] = tick;
		}
		else if ((s_linger_streak[player] > 3U) && ((tick - s_linger_last_log_tick[player]) >= 5U))
		{
			port_log(
			    "SSB64 NetRbSnapshot: guard_shield_linger tick=%u player=%d status=%d streak=%u "
			    "is_shield=%d z_auth=%d is_release=%d release_lag=%d z_live=%d\n",
			    (unsigned int)tick, player, (int)fp->status_id, (unsigned int)s_linger_streak[player],
			    (int)(fp->is_shield != FALSE), (int)z_auth, (int)ftStatusVarsGuard(fp)->is_release,
			    (int)ftStatusVarsGuard(fp)->release_lag, (int)syNetRbSnapFighterShieldInputHeld(fp));
			s_linger_last_log_tick[player] = tick;
		}
	}
#endif
}

static void syNetRbSnapReconcileGuardShieldEffectsCore(const SYNetRbSnapshotSlot *slot)
{
	sb32 live_forward_policy;

	live_forward_policy = syNetRbSnapGuardShieldLiveForwardPolicy(slot);
	if (slot != NULL)
	{
		syNetRbSnapResetShieldReleaseScheduleState();
		syNetRbSnapEnsureShieldEffectsFromSlot(slot);
	}
	if (live_forward_policy != FALSE)
	{
		/*
		 * Phase 29: sim-faithful live-forward. Only schedule is_release from authoritative Z-off (vanilla
		 * lag_end owns release_lag drain and is_shield teardown); the auth-retap cancel/resync and the
		 * z-rising stale eject were retired because they mutated sim/hashed state on local input timing.
		 * Bubble spawn-repair (ensure) keys on the deterministic fp->is_shield.
		 */
		syNetRbSnapApplyShieldReleaseScheduleLive(slot);
		syNetRbSnapReconcileLiveShieldEffectOwners();
		syNetRbSnapEnsureLiveShieldEffectsOnAuthHold(slot);
	}
	syNetRbSnapReconcileLiveShieldEffectOwners();
	syNetRbSnapReconcileFighterShieldCoupling(live_forward_policy);
	syNetRbSnapPruneStaleShields(slot, live_forward_policy);
	if (live_forward_policy != FALSE)
	{
		syNetRbSnapPruneWaitOrphanShieldEffects(slot);
	}
	syNetRbSnapPruneDuplicateShieldEffects(slot);
	syNetRbSnapPruneGuardOnExtraShieldEffects();
	syNetRbSnapClearGuardShieldCouplingWhenOutOfScope();
	if (slot != NULL)
	{
		syNetRbSnapHealFighterShieldOnApply(slot);
	}
	if (live_forward_policy != FALSE)
	{
		syNetRbSnapReconcileLiveShieldEffectOwners();
		syNetRbSnapEnsureLiveShieldEffectsOnAuthHold(slot);
		syNetRbSnapHealFighterShieldWithoutEffect(slot);
		syNetRbSnapDiagGuardWaitShieldHeld(slot);
		syNetRbSnapDiagGuardShieldWithoutBubbleGap(slot);
		syNetRbSnapDiagGuardShieldBubbleLinger(slot);
	}
}

static void syNetRbSnapReconcileYoshiEggLayEffectsCore(const SYNetRbSnapshotSlot *slot)
{
	if (syNetRbSnapYoshiEggLayReconcileCaptureWindowActive(slot) != FALSE)
	{
		syNetRbSnapSanitizeAllFightersGuardEffectGobjs();
		syNetRbSnapSanitizeAllFightersCaptureYoshiEffectGobjs();
		syNetRbSnapPruneStrayYoshiEggLayEffectsDuringCaptureWindow(slot);
		syNetRbSnapRebindFighterGrabCoupling();
		return;
	}
	syNetRbSnapSanitizeAllFightersGuardEffectGobjs();
	syNetRbSnapSanitizeAllFightersCaptureYoshiEffectGobjs();
	if (slot != NULL)
	{
		syNetRbSnapEnsureYoshiEggLayEffectsFromSlot(slot);
	}
	else
	{
		syNetRbSnapRebindCaptureYoshiEffectGobjsFromLive();
	}
	syNetRbSnapPruneStaleYoshiEggLayEffects(slot);
	syNetRbSnapSanitizeAllFightersCaptureYoshiEffectGobjs();
	syNetRbSnapRebindCaptureYoshiEffectGobjsFromLive();
}

static void syNetRbSnapReconcileYoshiEggLayEffectsInternal(const SYNetRbSnapshotSlot *slot)
{
	syNetRbSnapReconcileYoshiEggLayEffectsCore(slot);
	if ((slot != NULL) && (syNetRbSnapYoshiEggLayReconcileCaptureWindowActive(slot) == FALSE))
	{
		GObj *fighter_gobj;

		for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
		     fighter_gobj = fighter_gobj->link_next)
		{
			FTStruct *fp;
			s32 pidx;

			fp = ftGetStruct(fighter_gobj);
			if (fp == NULL)
			{
				continue;
			}
			pidx = fp->player;
			if ((pidx >= 0) && (pidx < GMCOMMON_PLAYERS_MAX))
			{
				syNetRbSnapRebindFighterEffectGobjs(&slot->fighters[pidx], fp, fighter_gobj);
			}
		}
	}
}

#ifdef PORT
static u32 s_syNetRbSnapYoshiEggLayHatchReplayTick[GMCOMMON_PLAYERS_MAX];
#endif
#if defined(SSB64_NETMENU)
typedef struct SYNetRbSnapYoshiEggLayHatchReplay
{
	sb32 is_pending;
	sb32 replay_shell;
	sb32 defer_particles;
	u32 tick;
	Vec3f pos;
} SYNetRbSnapYoshiEggLayHatchReplay;

static SYNetRbSnapYoshiEggLayHatchReplay s_syNetRbSnapYoshiEggLayHatchReplay[GMCOMMON_PLAYERS_MAX];
#endif

#ifdef PORT
static void syNetRbSnapYoshiEggLayHatchPosFromCapture(GObj *fighter_gobj, FTStruct *fp, Vec3f *pos)
{
	if ((pos == NULL) || (fighter_gobj == NULL) || (fp == NULL))
	{
		return;
	}
	*pos = DObjGetStruct(fighter_gobj)->translate.vec.f;
	pos->z = 0.0F;
	if (fp->status_id == nFTCommonStatusFall)
	{
		pos->y -= FTCOMMON_YOSHIEGG_ESCAPE_OFF_Y;
	}
}
#endif

#if defined(SSB64_NETMENU)
static void syNetRbSnapQueueYoshiEggLayHatchCosmetics(s32 player, u32 tick, const Vec3f *pos, sb32 replay_shell,
                                                     sb32 defer_particles)
{
	if ((player < 0) || (player >= GMCOMMON_PLAYERS_MAX) || (pos == NULL))
	{
		return;
	}
	s_syNetRbSnapYoshiEggLayHatchReplay[player].is_pending = TRUE;
	s_syNetRbSnapYoshiEggLayHatchReplay[player].replay_shell = replay_shell;
	s_syNetRbSnapYoshiEggLayHatchReplay[player].defer_particles = defer_particles;
	s_syNetRbSnapYoshiEggLayHatchReplay[player].tick = tick;
	s_syNetRbSnapYoshiEggLayHatchReplay[player].pos = *pos;
#ifdef PORT
	s_syNetRbSnapYoshiEggLayHatchReplayTick[player] = tick;
#endif
}

void syNetRbSnapQueueYoshiEggLayHatchCosmeticsLive(GObj *fighter_gobj)
{
	FTStruct *fp;
	Vec3f pos;
	GObj *effect_gobj;
	EFStruct *ep;
	s32 player;

	if (fighter_gobj == NULL)
	{
		return;
	}
	fp = ftGetStruct(fighter_gobj);
	if (fp == NULL)
	{
		return;
	}
	player = fp->player;
	if ((player < 0) || (player >= GMCOMMON_PLAYERS_MAX))
	{
		return;
	}
	syNetRbSnapYoshiEggLayHatchPosFromCapture(fighter_gobj, fp, &pos);
	if (syNetRbSnapStartYoshiEggLayHatchCosmeticLive(fighter_gobj, &pos) != FALSE)
	{
		return;
	}
	effect_gobj = syNetRbSnapFindLiveYoshiEggLayEffectForFighter(fighter_gobj);
	if (effect_gobj == NULL)
	{
		effect_gobj = syNetRbSnapFindYoshiEggLayHatchCosmeticForFighter(fighter_gobj);
	}
	if (effect_gobj != NULL)
	{
		ep = efGetStruct(effect_gobj);
		if (syNetRbSnapYoshiEggLayHatchAlreadyComplete(effect_gobj, ep) != FALSE)
		{
			syNetRbSnapReplayCosmeticYoshiEggExplode(&pos);
			syNetRbSnapEjectNonHatchYoshiEggLayEffectsForFighterExcept(fighter_gobj, NULL);
			return;
		}
	}
	syNetRbSnapQueueYoshiEggLayHatchCosmetics(player, syNetInputGetTick(), &pos, TRUE, TRUE);
}

void syNetRbSnapshotFlushDeferredYoshiEggLayHatchCosmetics(void)
{
	s32 pi;

	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	for (pi = 0; pi < GMCOMMON_PLAYERS_MAX; pi++)
	{
		GObj *fighter_gobj;
		GObj *hatch_shell_gobj;
		const Vec3f *pos;

		if (s_syNetRbSnapYoshiEggLayHatchReplay[pi].is_pending == FALSE)
		{
			continue;
		}
		pos = &s_syNetRbSnapYoshiEggLayHatchReplay[pi].pos;
		fighter_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)pi);
		hatch_shell_gobj = NULL;
		if (s_syNetRbSnapYoshiEggLayHatchReplay[pi].replay_shell != FALSE)
		{
			if ((fighter_gobj != NULL) &&
			    (syNetRbSnapStartYoshiEggLayHatchCosmeticLive(fighter_gobj, pos) != FALSE))
			{
				hatch_shell_gobj = syNetRbSnapFindYoshiEggLayHatchCosmeticForFighter(fighter_gobj);
			}
			else
			{
				s_syNetRbSnapYoshiEggLayHatchShellParticlePos[pi] = *pos;
				s_syNetRbSnapYoshiEggLayHatchShellParticlePending[pi] = TRUE;
				hatch_shell_gobj =
				    (fighter_gobj != NULL) ? syNetRbSnapReplayYoshiEggLayHatchShell(fighter_gobj, pos) : NULL;
				if (hatch_shell_gobj == NULL)
				{
					s_syNetRbSnapYoshiEggLayHatchShellParticlePending[pi] = FALSE;
					syNetRbSnapReplayCosmeticYoshiEggExplode(pos);
				}
			}
		}
		else if (s_syNetRbSnapYoshiEggLayHatchReplay[pi].defer_particles != FALSE)
		{
			syNetRbSnapReplayCosmeticYoshiEggExplode(pos);
		}
		if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
		{
			port_log(
			    "SSB64 NetRbSnapshot: yoshi_egg_lay_hatch_replay_flush tick=%u player=%d fighter_gobj_id=%u "
			    "shell_gobj_id=%u replay_shell=%d defer_particles=%d\n",
			    (unsigned int)s_syNetRbSnapYoshiEggLayHatchReplay[pi].tick, (int)pi,
			    (fighter_gobj != NULL) ? (unsigned int)fighter_gobj->id : 0U,
			    (hatch_shell_gobj != NULL) ? (unsigned int)hatch_shell_gobj->id : 0U,
			    (int)(s_syNetRbSnapYoshiEggLayHatchReplay[pi].replay_shell != FALSE),
			    (int)(s_syNetRbSnapYoshiEggLayHatchReplay[pi].defer_particles != FALSE));
		}
		s_syNetRbSnapYoshiEggLayHatchReplay[pi].is_pending = FALSE;
	}
}
#endif

static void syNetRbSnapReplayYoshiEggLayHatchCosmeticsFromSlot(const SYNetRbSnapshotSlot *slot)
{
	s32 pi;

	if (slot == NULL)
	{
		return;
	}
#ifdef PORT
	if (s_syNetRbSnapRepairStageVerifyOnly != FALSE)
	{
		return;
	}
#endif
	for (pi = 0; pi < GMCOMMON_PLAYERS_MAX; pi++)
	{
		const SYNetRbSnapFighterBlob *fb;
		GObj *fighter_gobj;
		FTStruct *fp;
		Vec3f pos;
		f32 vel_y;
#if defined(SSB64_NETMENU)
		sb32 deferred_replay;
#endif

		fb = &slot->fighters[pi];
		if (fb->is_valid == FALSE)
		{
			continue;
		}
		if (fb->status_id != nFTCommonStatusFall)
		{
			continue;
		}
		vel_y = fb->physics.vel_air.y;
		if ((vel_y < (FTCOMMON_YOSHIEGG_ESCAPE_VEL_Y - 4.0F)) || (vel_y > (FTCOMMON_YOSHIEGG_ESCAPE_VEL_Y + 4.0F)))
		{
			continue;
		}
		fighter_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)pi);
		if (fighter_gobj == NULL)
		{
			continue;
		}
		fp = ftGetStruct(fighter_gobj);
		if ((fp == NULL) || (fp->status_id != nFTCommonStatusFall))
		{
			continue;
		}
		syNetRbSnapYoshiEggLayHatchPosFromCapture(fighter_gobj, fp, &pos);
#ifdef PORT
		if (s_syNetRbSnapYoshiEggLayHatchReplayTick[pi] == slot->tick)
		{
			continue;
		}
#endif
#if defined(SSB64_NETMENU)
		deferred_replay = TRUE;
		syNetRbSnapQueueYoshiEggLayHatchCosmetics(pi, slot->tick, &pos, TRUE, TRUE);
#else
		syNetRbSnapReplayCosmeticYoshiEggExplode(&pos);
#endif
		if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
		{
			port_log(
			    "SSB64 NetRbSnapshot: yoshi_egg_lay_hatch_replay tick=%u player=%d fighter_gobj_id=%u "
			    "vel_y=%.2f deferred=%d shell_gobj_id=%u\n",
			    (unsigned int)slot->tick, (int)pi, (unsigned int)fighter_gobj->id, fp->physics.vel_air.y,
#if defined(SSB64_NETMENU)
			    (int)deferred_replay,
			    0U
#else
			    0,
			    0U
#endif
			);
		}
	}
}

static void syNetRbSnapReconcileGuardShieldEffectsInternal(const SYNetRbSnapshotSlot *slot)
{
	GObj *fighter_gobj;

	syNetRbSnapReconcileGuardShieldEffectsCore(slot);
	if (slot != NULL)
	{
		for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
		     fighter_gobj = fighter_gobj->link_next)
		{
			FTStruct *fp;
			s32 pidx;

			fp = ftGetStruct(fighter_gobj);
			if (fp == NULL)
			{
				continue;
			}
			pidx = fp->player;
			if ((pidx >= 0) && (pidx < GMCOMMON_PLAYERS_MAX))
			{
				syNetRbSnapRebindFighterEffectGobjs(&slot->fighters[pidx], fp, fighter_gobj);
			}
		}
		syNetRbSnapFinalizeFighterEffectAttachFlags(slot);
	}
}

static void syNetRbSnapEnsureRebirthHaloEffectsFromSlot(const SYNetRbSnapshotSlot *slot)
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
		const SYNetRbSnapFighterBlob *fb;
		s32 ei;
		s32 pi;

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
		if ((syNetRbSnapFighterRebirthHaloLifecycleActive(fp) == FALSE) &&
		    (syNetRbSnapFighterBlobRebirthHaloPending(&slot->fighters[pi]) == FALSE))
		{
			continue;
		}
		fb = &slot->fighters[pi];
		if ((fb->is_valid == FALSE) || (fb->is_effect_attach == 0U))
		{
			continue;
		}
		if (syNetRbSnapLiveFighterHasRebirthHalo(fighter_gobj) != FALSE)
		{
			continue;
		}
		if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
		{
			port_log(
			    "SSB64 NetRbSnapshot: rebirth_halo_ensure tick=%u player=%d halo_num=%d fighter_gobj_id=%u effect_count=%d\n",
			    (unsigned int)slot->tick,
			    (int)pi,
			    (int)ftStatusVarsRebirth(fp)->halo_number,
			    (unsigned int)fighter_gobj->id,
			    (int)slot->effect_count);
		}
		for (ei = 0; ei < slot->effect_count; ei++)
		{
			const SYNetRbSnapEffectBlob *eb = &slot->effects[ei];
			GObj *eg;

			if ((eb->is_valid == FALSE) || (eb->fighter_gobj_id != (u32)fighter_gobj->id))
			{
				continue;
			}
			if (eb->respawn_kind != SYNETRB_EFFECT_RESPAWN_REBIRTH_HALO)
			{
				continue;
			}
			eg = gcFindGObjByID(eb->gobj_id);
			if ((eg != NULL) && (efGetStruct(eg) != NULL))
			{
				continue;
			}
			(void)syNetRbSnapTryRespawnEffectFromBlob(slot, eb);
			if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 NetRbSnapshot: rebirth_halo_ensure path=blob_respawn player=%d fighter_gobj_id=%u blob_gobj_id=%u\n",
				    (int)pi, (unsigned int)fighter_gobj->id, eb->gobj_id);
			}
			break;
		}
		if (syNetRbSnapLiveFighterHasRebirthHalo(fighter_gobj) == FALSE)
		{
			SYNetRbSnapEffectBlob synth;

			memset(&synth, 0, sizeof(synth));
			synth.is_valid = TRUE;
			synth.fighter_gobj_id = (u32)fighter_gobj->id;
			synth.respawn_kind = SYNETRB_EFFECT_RESPAWN_REBIRTH_HALO;
			(void)syNetRbSnapTryRespawnEffectFromBlob(slot, &synth);
			if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 NetRbSnapshot: rebirth_halo_ensure path=synth player=%d fighter_gobj_id=%u\n",
				    (int)pi, (unsigned int)fighter_gobj->id);
			}
		}
	}
}

static void syNetRbSnapPruneStaleRebirthHalos(const SYNetRbSnapshotSlot *slot)
{
	s32 pass;
	GObj *gobj;
	GObj *next;

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
			fp = ftGetStruct(ep->fighter_gobj);
			if ((fp == NULL) || (syNetRbSnapFighterRebirthHaloLifecycleActive(fp) != FALSE))
			{
				continue;
			}
			if ((slot != NULL) &&
			    (syNetRbSnapSlotListsRebirthHaloForFighter(slot, (u32)ep->fighter_gobj->id, gobj->id) != FALSE))
			{
				continue;
			}
			if (syNetRbSnapEffectIsRebirthHaloCoupling(gobj, ep, fp) == FALSE)
			{
				continue;
			}
			syNetRbSnapClearFighterEffectPointerIfMatch(fp, gobj);
			if (fp->is_effect_attach != FALSE)
			{
				fp->is_effect_attach = FALSE;
			}
			syNetRbSnapEjectGObj(gobj);
		}
	}
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
	u32 reconciled_ids[SYNETRB_SNAPSHOT_MAX_EFFECTS];
	s32 reconciled_count;
	sb32 blob_applied[SYNETRB_SNAPSHOT_MAX_EFFECTS];

	if (slot == NULL)
	{
		return;
	}
	reconciled_count = 0;
	for (ei = 0; ei < SYNETRB_SNAPSHOT_MAX_EFFECTS; ei++)
	{
		blob_applied[ei] = FALSE;
	}
	for (ei = 0; ei < slot->effect_count; ei++)
	{
		const SYNetRbSnapEffectBlob *blob;
		GObj *gobj_before;

		blob = &slot->effects[ei];
		if (blob->is_valid == FALSE)
		{
			continue;
		}
		gobj = gcFindGObjByID(blob->gobj_id);
		gobj_before = gobj;
		gobj = syNetRbSnapApplyEffectBlobToGObj(slot, gobj, blob);
		if (gobj != NULL)
		{
			blob_applied[ei] = TRUE;
		}
		if ((syNetRbSnapSnapshotEffectDiagEnabled() != FALSE) &&
		    (blob->respawn_kind == SYNETRB_EFFECT_RESPAWN_YOSHI_EGG_LAY))
		{
			EFStruct *ep_apply;
			u32 resolved_parent_id;

			ep_apply = (gobj != NULL) ? efGetStruct(gobj) : NULL;
			resolved_parent_id =
			    ((ep_apply != NULL) && (ep_apply->fighter_gobj != NULL)) ? (u32)ep_apply->fighter_gobj->id : 0U;
			port_log(
			    "SSB64 NetRbSnapshot: effect_apply kind=YOSHI_EGG_LAY tick=%u blob_gobj_id=%u "
			    "fighter_gobj_id=%u resolved_parent=%u matched=%d respawned=%d result_gobj_id=%u\n",
			    (unsigned int)slot->tick, blob->gobj_id, blob->fighter_gobj_id, resolved_parent_id,
			    (gobj_before != NULL) ? 1 : 0, ((gobj_before == NULL) && (gobj != NULL)) ? 1 : 0,
			    (gobj != NULL) ? syNetRbSnapGobjId(gobj) : 0U);
		}
		syNetRbSnapTrackReconciledEffectGobj(reconciled_ids, &reconciled_count, gobj);
	}
	/*
	 * Pass 2: ring gobj ids are often stale during synctest verify (emergency live @ T+1 loaded over
	 * slot @ T) and after rollback pool reuse. Adopt unclaimed live effects that match blob identity
	 * (bank, player parent, proc fingerprint) before orphan prune would eject them.
	 */
	for (ei = 0; ei < slot->effect_count; ei++)
	{
		const SYNetRbSnapEffectBlob *blob;
		GObj *gobj;

		if (blob_applied[ei] != FALSE)
		{
			continue;
		}
		blob = &slot->effects[ei];
		if (blob->is_valid == FALSE)
		{
			continue;
		}
		gobj = syNetRbSnapFindUnreconciledLiveEffectForBlob(slot, blob, reconciled_ids, reconciled_count);
		if (gobj == NULL)
		{
			continue;
		}
		gobj = syNetRbSnapApplyEffectBlobToGObj(slot, gobj, blob);
		if (gobj == NULL)
		{
			continue;
		}
		blob_applied[ei] = TRUE;
		syNetRbSnapTrackReconciledEffectGobj(reconciled_ids, &reconciled_count, gobj);
		if (syNetRbSnapSnapshotEffectDiagEnabled() != FALSE)
		{
			port_log(
			    "SSB64 NetRbSnapshot: effect_reconcile_adopt tick=%u blob_gobj_id=%u live_gobj_id=%u bank=%u "
			    "fighter_gobj_id=%u respawn_kind=%u\n",
			    (unsigned int)slot->tick, blob->gobj_id, (unsigned int)gobj->id, (unsigned int)blob->bank_id,
			    blob->fighter_gobj_id, (unsigned int)blob->respawn_kind);
		}
	}
	syNetRbSnapPruneStaleFoxReflectors(slot);
	syNetRbSnapPruneStaleShields(slot, FALSE);
	syNetRbSnapPruneDuplicateShieldEffects(slot);
	syNetRbSnapReconcileYoshiEggLayEffectsCore(slot);
	syNetRbSnapPruneStaleShockSmallEffects(slot);
	syNetRbSnapPruneStaleNessPKWaveEffects(slot);
	syNetRbSnapPruneStaleNessPsychicMagnetEffects(slot);
	syNetRbSnapPruneStalePikachuThunderShockEffects(slot);
	syNetRbSnapPruneStaleKirbyInhaleWindEffects(slot);
	syNetRbSnapPruneOrphanQuakeAndDeadEffects(slot, reconciled_ids, reconciled_count);
	syNetRbSnapPruneOrphanFighterAttachedEffects(slot, reconciled_ids, reconciled_count);
	for (pass = 0; pass < 2; pass++)
	{
		GObj *link_head;

		link_head = gGCCommonLinks[(pass == 0) ? nGCCommonLinkIDEffect : nGCCommonLinkIDSpecialEffect];
		for (gobj = link_head; gobj != NULL; gobj = next)
		{
			EFStruct *ep;

			next = gobj->link_next;
			ep = efGetStruct(gobj);
			if (syNetRbSnapEffectHiddenFromRollback(gobj, ep) != FALSE)
			{
				continue;
			}
			if (ep != NULL)
			{
				if (ep->fighter_gobj != NULL)
				{
					continue;
				}
				if (syNetRbSnapLiveEffectKeptAfterReconcile(slot, gobj->id, reconciled_ids, reconciled_count) ==
				    FALSE)
				{
					syNetRbSnapEjectGObj(gobj);
				}
				continue;
			}
			if (syNetRbSnapEffectGObjHasUpdateProc(gobj, efManagerKirbyInhaleWindProcUpdate) != FALSE)
			{
				syNetRbSnapEjectKirbyInhaleWindEffectGObj(gobj, NULL);
				continue;
			}
			if ((gobj->user_data.p == NULL) && (gobj->obj_kind == nGCCommonKindEffect) &&
			    (syNetRbSnapLiveEffectKeptAfterReconcile(slot, gobj->id, reconciled_ids, reconciled_count) ==
			     FALSE))
			{
				syNetRbSnapEjectGObj(gobj);
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
#if defined(SSB64_NETMENU)
		/*
		 * Cross-ISA: snap the live effect DObj translate to the shared grid before storing it in the
		 * blob (syNetRbSnapApplyEffectBlobTranslate restores + re-quantizes on load). Parent-attached
		 * kinds still store translate for load-hash verify; joint rebind runs before apply.
		 */
		if ((ep == NULL) || (syNetRbSnapLiveEffectIsYoshiEggLay(gobj_iter, ep) == FALSE))
		{
			syNetplayQuantizeDObjTranslate(DObjGetStruct(gobj_iter));
		}
		if ((ep != NULL) && (syNetplayLiveEffectIsNessPsychicMagnet(gobj_iter, ep) != FALSE))
		{
			gobj_iter->anim_frame = syNetplayQuantizeAnimScalar(gobj_iter->anim_frame);
			if (DObjGetStruct(gobj_iter) != NULL)
			{
				DObjGetStruct(gobj_iter)->anim_frame = gobj_iter->anim_frame;
				syNetplayQuantizeDObjAnimPose(DObjGetStruct(gobj_iter));
				syNetplayQuantizeDObjAnimScalars(DObjGetStruct(gobj_iter));
			}
			blob->anim_frame = gobj_iter->anim_frame;
		}
		else
		{
			blob->anim_frame = syNetplayQuantizeF32(blob->anim_frame);
			gobj_iter->anim_frame = blob->anim_frame;
			if (DObjGetStruct(gobj_iter) != NULL)
			{
				DObjGetStruct(gobj_iter)->anim_frame = blob->anim_frame;
			}
		}
#endif
		{
			DObj *dobj_cap = DObjGetStruct(gobj_iter);

			if (dobj_cap != NULL)
			{
				blob->translate = dobj_cap->translate.vec.f;
				blob->snap_flags |= SYNETRB_EFFECT_SNAP_TRANSLATE;
			}
		}
		blob->quake_magnitude = 0xFFU;
		blob->respawn_kind = SYNETRB_EFFECT_RESPAWN_NONE;
		if (ep != NULL)
		{
			blob->bank_id = ep->bank_id;
			blob->fighter_gobj_id = (ep->fighter_gobj != NULL) ? (u32)ep->fighter_gobj->id : 0U;
			blob->respawn_kind = syNetRbSnapEffectRespawnKindFromLive(gobj_iter, ep);
			if (syNetRbSnapLiveEffectIsQuake(gobj_iter, ep) != FALSE)
			{
				blob->proc_update_fingerprint = syNetRbSnapGObjFuncProcFingerprint(gobj_iter);
			}
			else
			{
				blob->proc_update_fingerprint =
				    syNetRbSnapPointerFingerprintLow32((const void *)ep->proc_update);
			}
			if ((blob->respawn_kind == SYNETRB_EFFECT_RESPAWN_YOSHI_EGG_LAY) && (blob->fighter_gobj_id == 0U))
			{
				GObj *egg_owner;

				egg_owner = syNetRbSnapFindYoshiEggLayOwnerGobjFromSlot(slot, gobj_iter->id);
				if (egg_owner != NULL)
				{
					blob->fighter_gobj_id = (u32)egg_owner->id;
				}
			}
			if (blob->respawn_kind == SYNETRB_EFFECT_RESPAWN_QUAKE)
			{
				blob->quake_magnitude = (u8)(3 - ep->effect_vars.quake.priority);
			}
			else if ((blob->respawn_kind == SYNETRB_EFFECT_RESPAWN_PIKACHU_THUNDER_SHOCK) &&
			         (ep->fighter_gobj != NULL))
			{
				FTStruct *fp_cap;

				fp_cap = ftGetStruct(ep->fighter_gobj);
				if ((fp_cap != NULL) && (syNetRbSnapFighterInPikachuAttackS4Scope(fp_cap) != FALSE))
				{
					blob->quake_magnitude = (u8)(ftStatusVarsAttack4(fp_cap)->gfx_id & 0xFFU);
				}
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
#if defined(SSB64_NETMENU)
		/* Re-snap restored weapon geometry to the shared grid (matches capture quantization). */
		syNetplayQuantizeDObjTranslate(dobj);
		syNetplayQuantizeVec3f(&dobj->rotate.vec.f);
		syNetplayQuantizeVec3f(&dobj->scale.vec.f);
#endif
		syNetRbSnapApplyDObjAnim(dobj, &blob->anim);
	}
	memcpy(&wp->weapon_vars, blob->weapon_vars, sizeof(wp->weapon_vars));
	syNetRbSnapApplyWeaponBlobMeta(wp, blob);
	syNetRbSnapReapplyYoshiEggExplodeAfterBlob(gobj, blob);
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
			syNetRbSnapEjectGObj(gobj);
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
			syNetRbSnapEjectGObj(gobj);
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
		syNetRbSnapEjectGObj(gobj);
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

static sb32 syNetRbSnapCameraLoadDiagEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_CAMERA_LOAD_DIAG");
	s_env_cache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}

static void syNetRbSnapCaptureCameraCObj(SYNetRbSnapCameraBlob *cam)
{
	CObj *cobj;

	cam->cobj_valid = FALSE;
	if ((cam == NULL) || (gGMCameraGObj == NULL))
	{
		return;
	}
	cobj = CObjGetStruct(gGMCameraGObj);
	if (cobj == NULL)
	{
		return;
	}
	cam->cobj_valid = TRUE;
	cam->cobj_eye = cobj->vec.eye;
	cam->cobj_at = cobj->vec.at;
	cam->cobj_up = cobj->vec.up;
	cam->cobj_fovy = cobj->projection.persp.fovy;
#if defined(SSB64_NETMENU)
	if (syNetplaySimQuantizeActive() != FALSE)
	{
		syNetplayQuantizeVec3f(&cam->cobj_eye);
		syNetplayQuantizeVec3f(&cam->cobj_at);
		syNetplayQuantizeVec3f(&cam->cobj_up);
		cam->cobj_fovy = syNetplayQuantizeF32(cam->cobj_fovy);
	}
#endif
}

static void syNetRbSnapRestoreCameraCObjFromBlob(const SYNetRbSnapCameraBlob *cam)
{
	CObj *cobj;

	if ((cam == NULL) || (cam->cobj_valid == FALSE) || (gGMCameraGObj == NULL))
	{
		return;
	}
	cobj = CObjGetStruct(gGMCameraGObj);
	if (cobj == NULL)
	{
		return;
	}
	cobj->vec.eye = cam->cobj_eye;
	cobj->vec.at = cam->cobj_at;
	cobj->vec.up = cam->cobj_up;
	cobj->projection.persp.fovy = cam->cobj_fovy;
}

static void syNetRbSnapLogCameraApplyDiag(const SYNetRbSnapCameraBlob *cam, u32 tick, u32 hash_before, u32 hash_after,
                                          sb32 used_camera_run)
{
	CObj *cobj;

	if ((cam == NULL) || (syNetRbSnapCameraLoadDiagEnabled() == FALSE))
	{
		return;
	}
	cobj = (gGMCameraGObj != NULL) ? CObjGetStruct(gGMCameraGObj) : NULL;
	port_log(
	    "SSB64 NetRbSnapshot: camera_apply_diag tick=%u hash_before=0x%08X hash_after=0x%08X slot_cam=0x%08X "
	    "cobj_valid=%d used_run=%d status=%d target_dist=%.4f fovy=%.4f pzoom_dist=%.4f pfollow_dist=%.4f "
	    "cobj_eye=(%.2f,%.2f,%.2f) cobj_at=(%.2f,%.2f,%.2f)\n",
	    tick,
	    hash_before,
	    hash_after,
	    syNetRbSnapshotGetSlotHashCamera(tick),
	    (cam->cobj_valid != FALSE) ? 1 : 0,
	    (used_camera_run != FALSE) ? 1 : 0,
	    gGMCameraStruct.status_curr,
	    gGMCameraStruct.target_dist,
	    gGMCameraStruct.fovy,
	    gGMCameraStruct.pzoom_dist,
	    gGMCameraStruct.pfollow_dist,
	    (cobj != NULL) ? cobj->vec.eye.x : 0.0F,
	    (cobj != NULL) ? cobj->vec.eye.y : 0.0F,
	    (cobj != NULL) ? cobj->vec.eye.z : 0.0F,
	    (cobj != NULL) ? cobj->vec.at.x : 0.0F,
	    (cobj != NULL) ? cobj->vec.at.y : 0.0F,
	    (cobj != NULL) ? cobj->vec.at.z : 0.0F);
}

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
	syNetRbSnapCaptureCameraCObj(cam);
#if defined(SSB64_NETMENU)
	syNetplayQuantizeGMCameraState(&cam->camera, &cam->pause_eye_x, &cam->pause_eye_y);
#endif
}

static void syNetRbSnapApplyCamera(const SYNetRbSnapCameraBlob *cam)
{
	extern f32 gGMCameraPauseCameraEyeX;
	extern f32 gGMCameraPauseCameraEyeY;
	GObj *cg;
	u32 hash_before = 0U;
	u32 hash_after = 0U;
	sb32 used_camera_run = FALSE;

	if (syNetRbSnapCameraLoadDiagEnabled() != FALSE)
	{
		hash_before = syNetSyncHashGMCamera();
	}

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
#if defined(SSB64_NETMENU)
	syNetplayQuantizeGMCameraState(&gGMCameraStruct, &gGMCameraPauseCameraEyeX, &gGMCameraPauseCameraEyeY);
#endif
	if (cam->cobj_valid != FALSE)
	{
		syNetRbSnapRestoreCameraCObjFromBlob(cam);
	}
	else if (gGMCameraGObj != NULL)
	{
		/* Legacy slot without CObj partition: one integration step (may drift load-hash verify). */
		gmCameraRunFuncCamera(gGMCameraGObj);
		used_camera_run = TRUE;
	}
#if defined(SSB64_NETMENU)
	syNetplayCanonicalizeGMCameraSimState();
#endif
	if (syNetRbSnapCameraLoadDiagEnabled() != FALSE)
	{
		hash_after = syNetSyncHashGMCamera();
		syNetRbSnapLogCameraApplyDiag(cam, sSYNetRbSnapApplyCameraTick, hash_before, hash_after, used_camera_run);
	}
}

static SYNetRbSnapshotSlot *syNetRbSnapshotSlotForTick(u32 tick)
{
	return &sSYNetRbSnapshotRing[tick % sSYNetRbSnapshotRingLen];
}

static sb32 syNetRbSnapSnapshotParticleDiagEnabled(void)
{
	static int s_env_cache = -999;
	const char *e;

	if (s_env_cache != -999)
	{
		return (s_env_cache != 0) ? TRUE : FALSE;
	}
	e = getenv("SSB64_NETPLAY_SNAPSHOT_PARTICLE_DIAG");
	s_env_cache = ((e != NULL) && (e[0] != '\0') && (atoi(e) != 0)) ? 1 : 0;
	return (s_env_cache != 0) ? TRUE : FALSE;
}

static u8 syNetRbSnapLinkBombStatusFromItemBlob(const SYNetRbSnapItemBlob *blob)
{
	u8 link_status;

	if ((blob == NULL) || (blob->is_valid == FALSE) || (blob->kind != nITKindLinkBomb))
	{
		return 0xFFU;
	}
	if ((blob->item_flags & SYNETRB_ITEM_FLAG_LINK_BOMB_STATUS_VALID) == 0U)
	{
		return 0xFFU;
	}
	link_status = (blob->item_flags >> SYNETRB_ITEM_FLAG_LINK_BOMB_STATUS_SHIFT) & 0x0FU;
	if (link_status > (u8)nITLinkBombStatusExplode)
	{
		return 0xFFU;
	}
	return link_status;
}

static sb32 syNetRbSnapItemBlobWantsExplodeSparkleReplay(const SYNetRbSnapItemBlob *blob, f32 *scale_out)
{
	u8 link_status;

	if (scale_out != NULL)
	{
		*scale_out = 0.0F;
	}
	if ((blob == NULL) || (blob->is_valid == FALSE))
	{
		return FALSE;
	}
	link_status = syNetRbSnapLinkBombStatusFromItemBlob(blob);
	if (link_status == (u8)nITLinkBombStatusExplode)
	{
		if (scale_out != NULL)
		{
			*scale_out = ITLINKBOMB_EXPLODE_EFFECT_SCALE;
		}
		return TRUE;
	}
	if (syNetRbSnapMarumineBlobIsExplode(blob) != FALSE)
	{
		if (scale_out != NULL)
		{
			*scale_out = ITMARUMINE_EXPLODE_EFFECT_SCALE;
		}
		return TRUE;
	}
	return FALSE;
}

static sb32 syNetRbSnapWeaponBlobWantsEggExplodeParticleReplay(const SYNetRbSnapWeaponBlob *blob)
{
	if ((blob == NULL) || (blob->is_valid == FALSE) || (blob->kind != nWPKindEggThrow))
	{
		return FALSE;
	}
	if (blob->attack_coll.attack_state == nGMAttackStateOff)
	{
		return FALSE;
	}
	if (blob->attack_coll.size < (WPEGGTHROW_EXPLODE_SIZE - 1.0F))
	{
		return FALSE;
	}
	if (blob->lifetime > WPEGGTHROW_EXPLODE_LIFETIME)
	{
		return FALSE;
	}
	return TRUE;
}

static void syNetRbSnapReplayCosmeticYoshiEggExplode(const Vec3f *pos)
{
	Vec3f pos_copy;

	if (pos == NULL)
	{
		return;
	}
	pos_copy = *pos;
	(void)efManagerYoshiEggExplodeMakeEffect(&pos_copy);
	(void)efManagerEggBreakMakeEffect(&pos_copy);
}

static void syNetRbSnapReapplyYoshiEggExplodeAfterBlob(GObj *weapon_gobj, const SYNetRbSnapWeaponBlob *blob)
{
	WPStruct *wp;
	DObj *dobj;

	if ((weapon_gobj == NULL) || (syNetRbSnapWeaponBlobWantsEggExplodeParticleReplay(blob) == FALSE))
	{
		return;
	}
	wp = wpGetStruct(weapon_gobj);
	if (wp == NULL)
	{
		return;
	}
	wp->proc_update = wpYoshiEggExplodeProcUpdate;
	wp->proc_map = NULL;
	wp->proc_hit = NULL;
	wp->proc_shield = NULL;
	wp->proc_hop = NULL;
	wp->proc_setoff = NULL;
	wp->proc_reflector = NULL;
	dobj = DObjGetStruct(weapon_gobj);
	if (dobj != NULL)
	{
		dobj->dl = NULL;
		syNetRbSnapReplayCosmeticYoshiEggExplode(&dobj->translate.vec.f);
	}
	wpProcessUpdateHitPositions(weapon_gobj);
}

static sb32 syNetRbSnapSlotTickHasExplodeSparkleReplay(u32 tick)
{
	SYNetRbSnapshotSlot *slot;
	s32 i;

	slot = syNetRbSnapshotSlotForTick(tick);
	if ((slot == NULL) || (slot->is_valid == FALSE) || (slot->tick != tick))
	{
		return FALSE;
	}
	for (i = 0; i < slot->item_count; i++)
	{
		if (syNetRbSnapItemBlobWantsExplodeSparkleReplay(&slot->items[i], NULL) != FALSE)
		{
			return TRUE;
		}
	}
	for (i = 0; i < slot->weapon_count; i++)
	{
		if (syNetRbSnapWeaponBlobWantsEggExplodeParticleReplay(&slot->weapons[i]) != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetRbSnapSparklePosNearExisting(const Vec3f *pos, const Vec3f *existing, s32 existing_count)
{
	s32 i;

	if (pos == NULL)
	{
		return FALSE;
	}
	for (i = 0; i < existing_count; i++)
	{
		f32 dx;
		f32 dy;
		f32 dz;

		dx = pos->x - existing[i].x;
		dy = pos->y - existing[i].y;
		dz = pos->z - existing[i].z;
		if (((dx * dx) + (dy * dy) + (dz * dz)) <= SYNETRB_LINK_BOMB_SPARKLE_DEDUP_DIST2)
		{
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * Explode sparkle / Yoshi egg shatter are LBParticle (not snapshotted). After
 * syNetRbSnapResetParticlesForRollback the short-lived explode GObj may already be gone from this
 * slot; scan ring history for recent explode blobs and replay cosmetics. Current-tick explode blobs
 * are handled in the per-kind reapply paths.
 */
static void syNetRbSnapReplayExplodeSparklesFromRing(const SYNetRbSnapshotSlot *load_slot)
{
	u32 load_tick;
	u32 start_tick;
	u32 t;
	Vec3f replay_pos[SYNETRB_LINK_BOMB_SPARKLE_REPLAY_MAX];
	s32 replay_count;

	if (load_slot == NULL)
	{
		return;
	}
	load_tick = load_slot->tick;
	if (load_tick < SYNETRB_LINK_BOMB_SPARKLE_REPLAY_WINDOW)
	{
		start_tick = 0U;
	}
	else
	{
		start_tick = load_tick - SYNETRB_LINK_BOMB_SPARKLE_REPLAY_WINDOW;
	}
	replay_count = 0;
	for (t = start_tick; (t < load_tick) && (replay_count < SYNETRB_LINK_BOMB_SPARKLE_REPLAY_MAX); t++)
	{
		SYNetRbSnapshotSlot *hist;
		s32 i;

		hist = syNetRbSnapshotSlotForTick(t);
		if ((hist == NULL) || (hist->is_valid == FALSE) || (hist->tick != t))
		{
			continue;
		}
		for (i = 0; i < hist->item_count; i++)
		{
			const SYNetRbSnapItemBlob *blob;
			f32 sparkle_scale;
			const char *replay_kind;

			blob = &hist->items[i];
			if (syNetRbSnapItemBlobWantsExplodeSparkleReplay(blob, &sparkle_scale) == FALSE)
			{
				continue;
			}
			if (syNetRbSnapSparklePosNearExisting(&blob->translate, replay_pos, replay_count) != FALSE)
			{
				continue;
			}
			replay_kind = (blob->kind == nITKindMarumine) ? "marumine_sparkle" : "link_bomb_sparkle";
			replay_pos[replay_count] = blob->translate;
			replay_count++;
			syNetRbSnapReplayCosmeticExplodeSparkle(&blob->translate, sparkle_scale);
			if (syNetRbSnapSnapshotParticleDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 NetRbSnapshot: particle_replay kind=%s load_tick=%u hist_tick=%u "
				    "multi=%u pos=(%.1f,%.1f,%.1f)\n",
				    replay_kind,
				    (unsigned int)load_tick,
				    (unsigned int)t,
				    (unsigned int)blob->multi,
				    blob->translate.x,
				    blob->translate.y,
				    blob->translate.z);
			}
		}
		for (i = 0; i < hist->weapon_count; i++)
		{
			const SYNetRbSnapWeaponBlob *wb;

			wb = &hist->weapons[i];
			if (syNetRbSnapWeaponBlobWantsEggExplodeParticleReplay(wb) == FALSE)
			{
				continue;
			}
			if (syNetRbSnapSparklePosNearExisting(&wb->translate, replay_pos, replay_count) != FALSE)
			{
				continue;
			}
			replay_pos[replay_count] = wb->translate;
			replay_count++;
			syNetRbSnapReplayCosmeticYoshiEggExplode(&wb->translate);
			if (syNetRbSnapSnapshotParticleDiagEnabled() != FALSE)
			{
				port_log(
				    "SSB64 NetRbSnapshot: particle_replay kind=yoshi_egg_explode load_tick=%u hist_tick=%u "
				    "pos=(%.1f,%.1f,%.1f)\n",
				    (unsigned int)load_tick,
				    (unsigned int)t,
				    wb->translate.x,
				    wb->translate.y,
				    wb->translate.z);
			}
		}
	}
	if ((syNetRbSnapSnapshotParticleDiagEnabled() != FALSE) && (replay_count > 0))
	{
		port_log("SSB64 NetRbSnapshot: particle_replay_summary load_tick=%u replay_count=%d\n",
		         (unsigned int)load_tick,
		         replay_count);
	}
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

static sb32 syNetRbSnapshotSynctestSlotTransientOnlyEffects(const SYNetRbSnapshotSlot *slot)
{
	s32 ei;
	sb32 has_transient;
	sb32 has_respawnable;

	if ((slot == NULL) || (slot->effect_count <= 0))
	{
		return FALSE;
	}
	has_transient = FALSE;
	has_respawnable = FALSE;
	for (ei = 0; ei < slot->effect_count; ei++)
	{
		const SYNetRbSnapEffectBlob *blob;

		blob = &slot->effects[ei];
		if (blob->is_valid == FALSE)
		{
			continue;
		}
		if (blob->respawn_kind == SYNETRB_EFFECT_RESPAWN_NONE)
		{
			has_transient = TRUE;
		}
		else
		{
			has_respawnable = TRUE;
		}
	}
	/*
	 * Class A eff drift: one-shot hit VFX (respawn_kind=NONE) cannot survive emergency→slot verify
	 * load; skip synctest probes on those ticks instead of counting load_hash_drift soft-continues.
	 */
	return ((has_transient != FALSE) && (has_respawnable == FALSE)) ? TRUE : FALSE;
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

sb32 syNetRbSnapshotSynctestShouldSkipProbeTick(u32 probe_tick, const char **reason_out)
{
	SYNetRbSnapshotSlot *slot;

	slot = syNetRbSnapshotSlotForTick(probe_tick);
	if ((slot == NULL) || (slot->is_valid == FALSE) || (slot->tick != probe_tick))
	{
		return FALSE;
	}
#ifdef PORT
	if (syNetRbSnapshotYamabukiGateSlotSynctestFragile(slot) != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "yamabuki_gate_probe";
		}
		return TRUE;
	}
	if (syNetRbSnapshotYamabukiMonsterLiveSynctestFragile() != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "yamabuki_monster_probe";
		}
		return TRUE;
	}
	if (syNetRbSnapshotYamabukiMonsterSlotSynctestFragile(slot) != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "yamabuki_monster_probe";
		}
		return TRUE;
	}
	if (syNetRbSnapshotSectorArwingDeckSlotProbeFragile(slot) != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "sector_arwing_deck_probe";
		}
		return TRUE;
	}
	if (syNetRbSnapshotSectorArwingPatrolSlotSynctestFragile(slot) != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "sector_arwing_patrol_probe";
		}
		return TRUE;
	}
	if (syNetRbSnapshotYamabukiMonsterProbeCaptureGapFragile(slot) != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "yamabuki_monster_item_gap_probe";
		}
		return TRUE;
	}
	if (syNetRbSnapshotSlotAnyFighterYoshiEggLayScope(slot) != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "yoshi_egg_lay_probe";
		}
		return TRUE;
	}
	{
		s32 pidx;

		for (pidx = 0; pidx < GMCOMMON_PLAYERS_MAX; pidx++)
		{
			if (syNetRbSnapBlobInYoshiEggLayAttackScope(&slot->fighters[pidx]) != FALSE)
			{
				if (reason_out != NULL)
				{
					*reason_out = "yoshi_egg_lay_attack_probe";
				}
				return TRUE;
			}
		}
	}
	{
		s32 pidx;

		for (pidx = 0; pidx < GMCOMMON_PLAYERS_MAX; pidx++)
		{
			if (syNetRbSnapBlobInKirbySpecialNInhaleDeferScope(&slot->fighters[pidx]) != FALSE)
			{
				if (reason_out != NULL)
				{
					*reason_out = "kirby_specialn_inhale_probe";
				}
				return TRUE;
			}
		}
	}
	{
		s32 pidx;

		/*
		 * Live grab_coupling skip uses current fighters; probe loads a historical slot that can
		 * still be Catch/Throw while live has moved on — avoid finalize/verify on those ticks.
		 */
		for (pidx = 0; pidx < GMCOMMON_PLAYERS_MAX; pidx++)
		{
			if (syNetRbSnapBlobInGrabThrowSynctestFragileScope(&slot->fighters[pidx]) != FALSE)
			{
				if (reason_out != NULL)
				{
					*reason_out = "grab_coupling_probe";
				}
				return TRUE;
			}
		}
	}
	if (syNetRbSnapshotSynctestSlotTransientOnlyEffects(slot) != FALSE)
	{
		if (reason_out != NULL)
		{
			*reason_out = "transient_effect_probe";
		}
		return TRUE;
	}
#endif
	if (slot->item_count >= 2)
	{
		if (reason_out != NULL)
		{
			*reason_out = "multi_item_probe";
		}
		return TRUE;
	}
	if (probe_tick > 0U)
	{
		SYNetRbSnapshotSlot *prev_slot;

		prev_slot = syNetRbSnapshotSlotForTick(probe_tick - 1U);
		if ((prev_slot != NULL) && (prev_slot->is_valid != FALSE) && (prev_slot->tick == (probe_tick - 1U)) &&
		    (prev_slot->item_count >= 2))
		{
			if (reason_out != NULL)
			{
				*reason_out = "post_multi_item_probe";
			}
			return TRUE;
		}
	}
	{
		s32 si;

		for (si = 0; si < slot->item_count; si++)
		{
			if ((slot->items[si].is_valid != FALSE) && (slot->items[si].kind == nITKindLinkBomb))
			{
				if (reason_out != NULL)
				{
					*reason_out = "link_bomb_probe";
				}
				return TRUE;
			}
		}
	}
	{
		s32 wi;

		for (wi = 0; wi < slot->weapon_count; wi++)
		{
			if ((slot->weapons[wi].is_valid != FALSE) && (slot->weapons[wi].kind == nWPKindEggThrow))
			{
				if (reason_out != NULL)
				{
					*reason_out = "yoshi_egg_probe";
				}
				return TRUE;
			}
		}
	}
	{
		u32 sparkle_tick;
		u32 sparkle_start;

		if (probe_tick < SYNETRB_LINK_BOMB_SPARKLE_REPLAY_WINDOW)
		{
			sparkle_start = 0U;
		}
		else
		{
			sparkle_start = probe_tick - SYNETRB_LINK_BOMB_SPARKLE_REPLAY_WINDOW;
		}
		for (sparkle_tick = sparkle_start; sparkle_tick <= probe_tick; sparkle_tick++)
		{
			if (syNetRbSnapSlotTickHasExplodeSparkleReplay(sparkle_tick) != FALSE)
			{
				if (reason_out != NULL)
				{
					*reason_out = "explode_sparkle_probe";
				}
				return TRUE;
			}
		}
	}
	if (syNetRbSnapshotSynctestProbeEffectMismatch(probe_tick) != FALSE)
	{
#ifdef PORT
		if (syNetRbSnapshotYamabukiMonsterLiveSynctestFragile() != FALSE)
		{
			if (reason_out != NULL)
			{
				*reason_out = "yamabuki_monster_effect_probe";
			}
			return TRUE;
		}
		if (syNetRbSnapshotYamabukiGateSynctestFragile() != FALSE)
		{
			if (reason_out != NULL)
			{
				*reason_out = "yamabuki_gate_effect_probe";
			}
			return TRUE;
		}
#endif
		if (reason_out != NULL)
		{
			*reason_out = "effect_probe_mismatch";
		}
		return TRUE;
	}
	return FALSE;
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
#if defined(SSB64_NETMENU)
	sSYNetRbSnapDeferNetplayCatchUpDuringApply = FALSE;
	sSYNetRbSnapDeferWeaponEjectUntilVerify = FALSE;
#endif
	memset(sSYNetRbSnapWeaponApplyMatched, 0, sizeof(sSYNetRbSnapWeaponApplyMatched));
	s_syNetRbSnapParticleResetGen = 0U;
	memset(s_syNetRbSnapYoshiEggLayHatchReplayTick, 0, sizeof(s_syNetRbSnapYoshiEggLayHatchReplayTick));
#if defined(SSB64_NETMENU)
	memset(s_syNetRbSnapYoshiEggLayHatchReplay, 0, sizeof(s_syNetRbSnapYoshiEggLayHatchReplay));
	memset(s_syNetRbSnapYoshiEggLayHatchShellParticlePending, 0,
	       sizeof(s_syNetRbSnapYoshiEggLayHatchShellParticlePending));
#endif
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

/*
 * Recover guard_effect_gobj_id from the just-captured effect blobs when the live fighter capture missed it.
 * syNetRbSnapCaptureFighterCoupledIds reads ftStatusVarsGuard(fp)->effect_gobj (with a FindLiveShieldEffect
 * fallback), but on the GuardOn entry frame the coupling can still be unbound while the bubble is already on
 * the effect list, leaving guard_effect_gobj_id == 0. A zero id makes syNetRbSnapEnsureShieldEffectsFromSlot
 * skip the slot respawn on load, so the live reconcile spawns a fresh anim_frame=0 bubble instead of restoring
 * the saved one — the residual eff-hash LOAD_HASH_DRIFT seen on sustained shield (e.g. host.log tick 869).
 * slot->effects already holds the live bubble (with shield.player), so it is authoritative for recovery here.
 * Mirrors syNetRbSnapBackfillFighterCoupledIdsFromWeapons; runs after syNetRbSnapCaptureEffects.
 */
#ifdef PORT
static void syNetRbSnapBackfillGuardShieldEffectIdsFromEffects(SYNetRbSnapshotSlot *slot)
{
	s32 pi;

	if (slot == NULL)
	{
		return;
	}
	for (pi = 0; pi < GMCOMMON_PLAYERS_MAX; pi++)
	{
		SYNetRbSnapFighterBlob *blob = &slot->fighters[pi];

		if (syNetRbSnapBlobInGuardScope(blob) == FALSE)
		{
			continue;
		}
		/* Yoshi has no egg bubble until is_shield; do not couple a GuardOn-only frame. */
		if ((blob->fkind == nFTKindYoshi) && (blob->is_shield == FALSE))
		{
			continue;
		}
		{
			const SYNetRbSnapEffectBlob *eb;

			/*
			 * Effect blobs are authoritative per-player shield identity (shield.player). Always
			 * prefer them over guard->effect_gobj coupling so dual-shield windows never snapshot
			 * the same pool id on both fighters (synctest tick 2195 class).
			 */
			eb = syNetRbSnapFindShieldEffectBlobForPlayer(slot, pi);
			if (eb != NULL)
			{
				blob->guard_effect_gobj_id = eb->gobj_id;
			}
		}
	}
}
#endif

static sb32 syNetRbSnapFillSlotFromLive(SYNetRbSnapshotSlot *slot, u32 completed_sim_tick)
{
	GObj *fighter_gobj;

#ifdef PORT
	syNetSyncReconcileBattleTimePassedForSnapshotSave(completed_sim_tick);
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
#ifdef PORT
	syNetRbSnapCaptureBarrel(slot);
	syNetRbSnapCaptureYamabukiGate(slot);
#endif
	syNetRbSnapCaptureWorld(&slot->world);
	if (syNetRbSnapCaptureItems(slot) == FALSE)
	{
		slot->is_valid = FALSE;
		return FALSE;
	}
#if defined(SSB64_NETMENU)
	if (syNetplayNessIsPKThunderGlobalDeferActive() == FALSE)
#endif
	{
		syNetRbSnapCullAllOrphanPKThunderLive();
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
#ifdef PORT
	/* Effect blobs are populated above (syNetRbSnapCaptureEffects); recover any shield coupling the live
	 * fighter capture missed so load-side slot respawn restores the saved bubble instead of churning. */
	syNetRbSnapBackfillGuardShieldEffectIdsFromEffects(slot);
#endif
	syNetRbSnapCaptureCamera(&slot->camera);

	slot->hash_fighter = syNetSyncHashBattleFightersFull();
	slot->hash_world = syNetSyncHashRollbackWorld();
	slot->hash_item = syNetSyncHashActiveItemsForRollback();
	slot->hash_weapon = syNetSyncHashActiveWeaponsForRollback();
#ifdef PORT
	/*
	 * Map hash folds live kin + ground. Re-capture yakumono and Arwing pose partitions after the final
	 * deck reconcile so ring blobs match the instant hashed below (synctest verify reads kin from live).
	 */
	syNetRbSnapshotPrepareMapStateForHash();
	syNetRbSnapCaptureGround(slot);
	syNetRbSnapCaptureMap(slot);
	syNetRbSnapCaptureArwing(slot);
	slot->hash_map = syNetRbSnapshotComputeMapHashWithGround(
	    (slot->ground_captured != FALSE) ? &slot->ground : NULL);
	syNetRbSnapshotLogMapHashSaveSelfTest(completed_sim_tick);
#else
	slot->hash_map = syNetSyncHashMapCollisionKinematicsForRollback();
#endif
	slot->hash_rng = syNetSyncHashRNGSeed();
	slot->hash_camera = syNetSyncHashGMCamera();
	slot->hash_animation = syNetSyncHashFighterAnimationStateForRollback();
#ifdef PORT
	slot->hash_effect = syNetSyncHashActiveEffectsForRollback();
	syNetRbSnapLogRingSaveDiag(slot, completed_sim_tick);
#endif

	return TRUE;
}

static void syNetRbSnapApplySlotToLive(const SYNetRbSnapshotSlot *slot)
{
	GObj *fighter_gobj;

	/*
	 * Mirror syNetRbSnapFillSlotFromLive capture order (fighters before map/world) so MPColl/floor state
	 * applied from map does not run before fighter joint/coll restore — avoids LOAD_HASH_DRIFT on figh.
	 * Rebirth pose is captured verbatim in the fighter blob (gobj_translate + control bitfields); vanilla
	 * procMap continues from restored rebirth state on the next sim tick.
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
	/* Sector Arwing kinematics repair runs once in finalize (after items/weapons) — not idempotent. */
	syNetRbSnapRepairStageAfterParticleResetInternal(slot, FALSE);
	syNetRbSnapEnsureFoxReflectorEffectsFromSlot(slot);
	syNetRbSnapReconcileGuardShieldEffectsCore(slot);
	syNetRbSnapReconcileYoshiEggLayEffectsCore(slot);
	syNetRbSnapEnsureRebirthHaloEffectsFromSlot(slot);
	syNetRbSnapEnsureNessPKWaveEffectsFromSlot(slot);
	syNetRbSnapEnsureNessPsychicMagnetEffectsFromSlot(slot);
	syNetRbSnapEnsurePikachuThunderShockEffectsFromSlot(slot);
	syNetRbSnapReconcileSnapshotEffectsBeforeItems(slot);
	syNetRbSnapPruneStaleRebirthHalos(slot);
	syNetRbSnapPruneStaleFoxReflectors(slot);
	syNetRbSnapPruneStaleShockSmallEffects(slot);
	syNetRbSnapPruneStaleNessPKWaveEffects(slot);
	syNetRbSnapPruneStaleNessPsychicMagnetEffects(slot);
	syNetRbSnapPruneStalePikachuThunderShockEffects(slot);
	syNetRbSnapPruneStaleKirbyInhaleWindEffects(slot);
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
				syNetRbSnapRebindFighterEffectGobjs(&slot->fighters[pidx], fp_re, fighter_gobj_re);
			}
		}
		syNetRbSnapSanitizeAllFightersCaptureYoshiEffectGobjs();
		syNetRbSnapFinalizeFighterEffectAttachFlags(slot);
	}
#endif
	syNetRbSnapApplyItems(slot);
	/* Item apply can eject/respawn item gobjs (incl. the Castle bumper on
	 * truncation/id-alias and the Yamabuki rooftop Pokémon). Re-resolve stage
	 * singletons now so the next ground proc tick never dereferences a dangling
	 * pointer or restores gate collision before the live monster exists. */
	syNetRbSnapEnsureCastleBumperAfterParticleReset(slot);
	syNetRbSnapEnsureYamabukiGateAfterParticleReset(slot);
	syNetRbSnapReplayExplodeSparklesFromRing(slot);
	syNetRbSnapReplayYoshiEggLayHatchCosmeticsFromSlot(slot);
	syNetRbSnapRebindAllFighterMPCollPointers();
	syNetRbSnapApplyWeapons(slot);
#ifdef PORT
	/* The flame weapon GObjs restored just above are collision-only; their visible fire was wiped by the
	 * rollback particle reset. Re-emit it so tower-monster flames render again after a rollback. */
	syNetRbSnapEnsureMonsterFlameParticlesAfterParticleReset();
	syNetRbSnapRebindFighterCoupledGObjs(slot, FALSE);
	syNetRbSnapRebindFighterGrabCoupling();
	syNetRbSnapRebindFighterItemHoldCoupling();
#if defined(SSB64_NETMENU)
	syNetplayNessSanitizeAllFightersAfterSlotApply();
	syNetplayPikachuSanitizeAllFightersAfterSlotApply();
#endif
#endif
	sSYNetRbSnapApplyCameraTick = slot->tick;
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
#if defined(SSB64_NETMENU)
	syNetRbSnapResetSectorArwingRepairDedup();
#endif
	syNetRbSnapApplySlotToLive(&sSYNetRbEmergencySlot);
	syNetRbSnapshotFinalizeLoadFromSlot(&sSYNetRbEmergencySlot, TRUE, TRUE);
	syNetRbSnapshotRebindAllFighters();
#if defined(SSB64_NETMENU)
	sSYNetRbSnapDeferNetplayCatchUpDuringApply = FALSE;
	sSYNetRbSnapDeferWeaponEjectUntilVerify = FALSE;
#endif
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

#if defined(SSB64_NETMENU)
	sSYNetRbSnapDeferNetplayCatchUpDuringApply = TRUE;
	syNetRbSnapResetSectorArwingRepairDedup();
#endif
	syNetRbSnapApplySlotToLive(slot);
#if defined(SSB64_NETMENU)
	sSYNetRbSnapDeferNetplayCatchUpDuringApply = FALSE;
	sSYNetRbSnapDeferWeaponEjectUntilVerify = TRUE;
#endif
	return TRUE;
#else
	(void)completed_sim_tick;
	return FALSE;
#endif
}

#if defined(SSB64_NETMENU)
sb32 syNetRbSnapshotPikachuQuickAttackCatchUpPendingAtTick(u32 tick)
{
	SYNetRbSnapshotSlot *slot;
	s32 pidx;

	slot = syNetRbSnapshotSlotForTick(tick);
	if ((slot == NULL) || (slot->is_valid == FALSE) || (slot->tick != tick))
	{
		return FALSE;
	}
	for (pidx = 0; pidx < GMCOMMON_PLAYERS_MAX; pidx++)
	{
		const SYNetRbSnapFighterBlob *blob;
		const union FTStatusVars *status_vars;
		s32 status_id;

		blob = &slot->fighters[pidx];
		if (blob->is_valid == FALSE)
		{
			continue;
		}
		if ((blob->fkind != nFTKindPikachu) && (blob->fkind != nFTKindNPikachu))
		{
			continue;
		}
		status_id = blob->status_id;
		status_vars = (const union FTStatusVars *)blob->status_vars;
		if (syNetplayPikachuFighterInQuickAttackStartScope(status_id) != FALSE)
		{
			if (status_vars->pikachu.specialhi.anim_frames <= 0)
			{
				return TRUE;
			}
		}
		if (syNetplayPikachuFighterInQuickAttackZipScope(status_id) != FALSE)
		{
			if (status_vars->pikachu.specialhi.anim_frames <= 0)
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

void syNetRbSnapshotCommitDeferredWeaponEject(u32 tick)
{
	SYNetRbSnapshotSlot *slot;

	if (sSYNetRbSnapDeferWeaponEjectUntilVerify == FALSE)
	{
		return;
	}
	slot = syNetRbSnapshotSlotForTick(tick);
	if ((slot == NULL) || (slot->is_valid == FALSE) || (slot->tick != tick))
	{
		sSYNetRbSnapDeferWeaponEjectUntilVerify = FALSE;
		return;
	}
	syNetRbSnapEjectUnmatchedWeaponsAfterCoupling(slot);
	sSYNetRbSnapDeferWeaponEjectUntilVerify = FALSE;
}

void syNetRbSnapshotCancelDeferredWeaponEject(void)
{
	sSYNetRbSnapDeferWeaponEjectUntilVerify = FALSE;
}
#endif

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
				fp->joints[ji]->translate.vec.f = blob->joint_translate[ji];
				fp->joints[ji]->rotate.vec.f = blob->joint_rotate[ji];
				fp->joints[ji]->scale.vec.f = blob->joint_scale[ji];
#if defined(SSB64_NETMENU)
				syNetplayQuantizeVec3f(&fp->joints[ji]->translate.vec.f);
				syNetplayQuantizeVec3f(&fp->joints[ji]->rotate.vec.f);
				syNetplayQuantizeVec3f(&fp->joints[ji]->scale.vec.f);
#endif
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
#if defined(SSB64_NETMENU)
		fighter_gobj->anim_frame = syNetplayQuantizeAnimScalar(fighter_gobj->anim_frame);
		syNetplayCanonicalizeFighterSimState(fighter_gobj);
#endif
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
static void syNetRbSnapRestoreRebirthFightersAfterFinalize(const SYNetRbSnapshotSlot *slot)
{
	s32 pidx;

	if (slot == NULL)
	{
		return;
	}
	for (pidx = 0; pidx < GMCOMMON_PLAYERS_MAX; pidx++)
	{
		const SYNetRbSnapFighterBlob *blob;
		GObj *fighter_gobj;
		FTStruct *fp;

		blob = &slot->fighters[pidx];
		if (blob->is_valid == FALSE)
		{
			continue;
		}
		if ((blob->status_id < nFTCommonStatusRebirthDown) || (blob->status_id > nFTCommonStatusRebirthWait))
		{
			continue;
		}
		fighter_gobj = syNetRbSnapResolveFighterGobjByPlayer((s8)pidx);
		if (fighter_gobj == NULL)
		{
			continue;
		}
		fp = ftGetStruct(fighter_gobj);
		if ((fp == NULL) || (syNetRbSnapFighterInRebirthScope(fp) == FALSE))
		{
			continue;
		}
		{
			const union FTStatusVars *blob_sv = (const union FTStatusVars *)blob->status_vars;

			syNetplayRestoreRebirthStatusVars(fp, blob_sv);
#if defined(SSB64_NETMENU)
			syNetRbSnapQuantizeFighterRebirthStatusVars(fp, &fp->status_vars);
			syNetplayRepairRebirthApexIfInverted(fp);
#endif
		}
		syNetRbSnapApplyFighterGobjPose(blob, fp, fighter_gobj);
		if (fp->joints[nFTPartsJointTopN] != NULL)
		{
			fp->joints[nFTPartsJointTopN]->translate.vec.f = blob->joint_translate[nFTPartsJointTopN];
#if defined(SSB64_NETMENU)
			syNetplayQuantizeVec3f(&fp->joints[nFTPartsJointTopN]->translate.vec.f);
#endif
		}
#if defined(SSB64_NETMENU)
		syNetplayCanonicalizeRebirthFighterMapPose(fighter_gobj);
#endif
	}
}

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
		if (syNetRbSnapLiveHasFoxReflectorScope() != FALSE)
		{
			GObj *fighter_gobj_fx;

			for (fighter_gobj_fx = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj_fx != NULL;
			     fighter_gobj_fx = fighter_gobj_fx->link_next)
			{
				FTStruct *fp_fx;
				s32 pidx_fx;

				fp_fx = ftGetStruct(fighter_gobj_fx);
				if (fp_fx == NULL)
				{
					continue;
				}
				pidx_fx = fp_fx->player;
				if ((pidx_fx >= 0) && (pidx_fx < GMCOMMON_PLAYERS_MAX))
				{
					syNetRbSnapRebindFighterEffectGobjs(&slot->fighters[pidx_fx], fp_fx, fighter_gobj_fx);
				}
			}
		}
		if (syNetRbSnapLiveHasGuardScope() != FALSE)
		{
			GObj *fighter_gobj_gd;

			for (fighter_gobj_gd = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj_gd != NULL;
			     fighter_gobj_gd = fighter_gobj_gd->link_next)
			{
				FTStruct *fp_gd;
				s32 pidx_gd;

				fp_gd = ftGetStruct(fighter_gobj_gd);
				if (fp_gd == NULL)
				{
					continue;
				}
				pidx_gd = fp_gd->player;
				if ((pidx_gd >= 0) && (pidx_gd < GMCOMMON_PLAYERS_MAX))
				{
					syNetRbSnapRebindFighterEffectGobjs(&slot->fighters[pidx_gd], fp_gd, fighter_gobj_gd);
				}
			}
		}
		if (syNetRbSnapLiveHasNessPKThunderScope() != FALSE)
		{
			GObj *fighter_gobj_ness;

			for (fighter_gobj_ness = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj_ness != NULL;
			     fighter_gobj_ness = fighter_gobj_ness->link_next)
			{
				FTStruct *fp_ness;
				s32 pidx_ness;

				fp_ness = ftGetStruct(fighter_gobj_ness);
				if ((fp_ness == NULL) || (syNetRbSnapFighterInNessPKThunderScope(fp_ness) == FALSE))
				{
					continue;
				}
				pidx_ness = fp_ness->player;
				if ((pidx_ness >= 0) && (pidx_ness < GMCOMMON_PLAYERS_MAX))
				{
					syNetRbSnapRebindFighterEffectGobjs(&slot->fighters[pidx_ness], fp_ness,
					                                    fighter_gobj_ness);
				}
			}
		}
		syNetRbSnapPruneStaleShockSmallEffects(slot);
		syNetRbSnapPruneStaleNessPKWaveEffects(slot);
		syNetRbSnapPruneStalePikachuThunderShockEffects(slot);
		syNetRbSnapPruneStaleKirbyInhaleWindEffects(slot);
		syNetRbSnapEnsurePikachuThunderShockEffectsFromSlot(slot);
		syNetRbSnapReapplyFighterJointAnimFromSlot(slot);
		syNetRbSnapshotRefreshGrabCouplingGeometry();
		syNetRbSnapRestoreRebirthFightersAfterFinalize(slot);
		if (syNetRbSnapshotAnyFighterRebirthScopeActive() != FALSE)
		{
			GObj *fighter_gobj_rb;

			syNetRbSnapEnsureRebirthHaloEffectsFromSlot(slot);
			syNetRbSnapPruneStaleRebirthHalos(slot);
			for (fighter_gobj_rb = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj_rb != NULL;
			     fighter_gobj_rb = fighter_gobj_rb->link_next)
			{
				FTStruct *fp_rb;
				s32 pidx_rb;

				fp_rb = ftGetStruct(fighter_gobj_rb);
				if (fp_rb == NULL)
				{
					continue;
				}
				pidx_rb = fp_rb->player;
				if ((pidx_rb >= 0) && (pidx_rb < GMCOMMON_PLAYERS_MAX) &&
				    (syNetRbSnapFighterInRebirthScope(fp_rb) != FALSE))
				{
					syNetRbSnapRebindFighterEffectGobjs(&slot->fighters[pidx_rb], fp_rb, fighter_gobj_rb);
				}
			}
		}
	}
	/*
	 * Joint anim / presentation finalize can break fighter<->held-item coupling and leave
	 * free-floating effects out of sync with the slot blobs. Reconcile before hash verify.
	 */
	syNetRbSnapRepairStageAfterParticleResetInternal(slot, TRUE);
	syNetRbSnapRebindFighterItemHoldCoupling();
	syNetRbSnapReconcileOrphanHeldItems(slot);
	syNetRbSnapReconcileSnapshotEffectsBeforeItems(slot);
	syNetRbSnapReconcileItemsToSlotBlobs(slot);
	syNetRbSnapshotFinalizeLoadCouplingFromSlot(slot, refresh_coupled_weapon_geometry);
	if (sSYNetRbSnapDeferWeaponEjectUntilVerify == FALSE)
	{
		syNetRbSnapEjectUnmatchedWeaponsAfterCoupling(slot);
	}
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

void syNetRbSnapshotPrepareLoadedSlotForVerify(u32 completed_sim_tick)
{
	SYNetRbSnapshotSlot *slot;

	syNetRbSnapshotFinalizeLoad(completed_sim_tick);
	syNetRbSnapshotRebindAllFighters();
	syNetRbSnapshotReapplyJointAnimAtTick(completed_sim_tick);
	syNetRbSnapshotFinalizeLoadCoupling(completed_sim_tick);
	syNetRbSnapshotReapplyJointAnimAtTick(completed_sim_tick);
	if (syNetRbSnapshotGetSlotItemCount(completed_sim_tick) > 0U)
	{
		syNetRbSnapshotRebindFighterItemHoldCoupling();
	}
	syNetRbSnapshotReconcileLoadedItemsForVerify(completed_sim_tick);
	slot = syNetRbSnapshotSlotForTick(completed_sim_tick);
	if ((slot != NULL) && (slot->is_valid != FALSE) && (slot->tick == completed_sim_tick))
	{
		syNetRbSnapHealFighterShieldOnApply(slot);
	}
	(void)syNetRbSnapshotTryRepairEffectHashForVerify(completed_sim_tick);
	slot = syNetRbSnapshotSlotForTick(completed_sim_tick);
	if ((slot != NULL) && (slot->is_valid != FALSE) && (slot->tick == completed_sim_tick))
	{
		syNetRbSnapFreezeSlotQuakeEffectsFromSlot(slot);
	}
	syNetRbSnapshotReapplyJointAnimAtTick(completed_sim_tick);
}

sb32 syNetRbSnapshotTryRepairEffectHashForVerify(u32 completed_sim_tick)
{
	SYNetRbSnapshotSlot *slot;
	GObj *fighter_gobj_re;
	u32 live_ef;

	slot = syNetRbSnapshotSlotForTick(completed_sim_tick);
	if ((slot == NULL) || (slot->is_valid == FALSE) || (slot->tick != completed_sim_tick))
	{
		return FALSE;
	}
	syNetRbSnapReconcileGuardShieldEffectsInternal(slot);
	syNetRbSnapReconcileYoshiEggLayEffectsInternal(slot);
	syNetRbSnapReconcileSnapshotEffectsBeforeItems(slot);
	syNetRbSnapReapplyEffectBlobsFromSlot(slot);
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
			syNetRbSnapRebindFighterEffectGobjs(&slot->fighters[pidx], fp_re, fighter_gobj_re);
		}
	}
	syNetRbSnapFinalizeFighterEffectAttachFlags(slot);
	syNetRbSnapPatchAllGuardShieldsFromSlot(slot);
	syNetRbSnapPruneDuplicateShieldEffects(slot);
	/*
	 * Final blob pose stamp after shield patch/prune: reconcile procs may advance anim/translate on
	 * respawnable effects; re-apply canonical blob anim+translate so verify eff hash matches save.
	 */
	syNetRbSnapReapplyEffectBlobsFromSlot(slot);
	syNetRbSnapFreezeSlotQuakeEffectsFromSlot(slot);
	live_ef = syNetSyncHashActiveEffectsForRollback();
	return (live_ef == syNetRbSnapshotGetSlotHashEffect(completed_sim_tick)) ? TRUE : FALSE;
}

void syNetRbSnapshotReconcileGuardShieldEffectsAtTick(u32 completed_sim_tick)
{
	SYNetRbSnapshotSlot *slot;

#ifdef PORT
	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return;
	}
#endif
	slot = syNetRbSnapshotSlotForTick(completed_sim_tick);
	if ((slot == NULL) || (slot->is_valid == FALSE) || (slot->tick != completed_sim_tick))
	{
		return;
	}
	syNetRbSnapReconcileGuardShieldEffectsInternal(slot);
}

void syNetRbSnapReconcileGuardShieldEffectsLive(void)
{
#ifdef PORT
	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return;
	}
#endif
	syNetRbSnapReconcileGuardShieldEffectsInternal(NULL);
}

void syNetRbSnapshotReconcileYoshiEggLayEffectsAtTick(u32 completed_sim_tick)
{
	SYNetRbSnapshotSlot *slot;

#ifdef PORT
	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return;
	}
#endif
	slot = syNetRbSnapshotSlotForTick(completed_sim_tick);
	if ((slot == NULL) || (slot->is_valid == FALSE) || (slot->tick != completed_sim_tick))
	{
		return;
	}
	syNetRbSnapReconcileYoshiEggLayEffectsInternal(slot);
}

void syNetRbSnapReconcileYoshiEggLayEffectsLive(void)
{
#ifdef PORT
	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return;
	}
#endif
	syNetRbSnapReconcileYoshiEggLayEffectsInternal(NULL);
}

void syNetRbSnapCullYoshiChargeEggsForFighter(GObj *fighter_gobj, GObj *keep_egg_gobj)
{
	GObj *weapon_gobj;
	FTStruct *fp;

#ifdef PORT
	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return;
	}
#endif
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

#ifdef PORT
	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return NULL;
	}
#endif
	fighter_gobj = syNetRbSnapFighterGObjFromFP(fp);
	if (fighter_gobj == NULL)
	{
		return NULL;
	}
	return syNetRbSnapFindLiveWeaponForOwner(fighter_gobj, nWPKindChargeShot, syNetRbSnapWeaponChargeShotIsCharging);
}

GObj *syNetRbSnapReacquireFireballForFighter(GObj *fighter_gobj)
{
#ifdef PORT
	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return NULL;
	}
#endif
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

#define SYNETRB_SNAP_FIREBALL_EMERGENCY_FRAME       3.0F
#define SYNETRB_SNAP_FIREBALL_HAND_RADIUS_SQ        3600.0F
/* Tighter than hand dedup — cull only true duplicates still at the live hand joint. */
#define SYNETRB_SNAP_FIREBALL_DUP_CULL_RADIUS_SQ    900.0F
#define SYNETRB_SNAP_FIREBALL_THROW_PRESERVE_FRAMES 25.0F

static sb32 syNetRbSnapFireballNearFighterHand(GObj *fighter_gobj, GObj *fireball_gobj, f32 radius_sq);

static void syNetRbSnapCullAllOwnedFireballsForFighter(GObj *fighter_gobj, GObj *keep_fireball_gobj)
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

		next_gobj = weapon_gobj->link_next;
		wp = wpGetStruct(weapon_gobj);
		if ((wp == NULL) || (wp->kind != nWPKindFireball) || (weapon_gobj == keep_fireball_gobj) ||
		    (syNetRbSnapWeaponOwnedByFighterGObj(wp, fighter_gobj) == FALSE))
		{
			weapon_gobj = next_gobj;
			continue;
		}
		wpMainDestroyWeapon(weapon_gobj);
		weapon_gobj = next_gobj;
	}
}

void syNetRbSnapCullOwnedFireballsNearPose(GObj *fighter_gobj, GObj *keep_fireball_gobj, const Vec3f *pos, f32 radius_sq)
{
	GObj *weapon_gobj;
	f32 cull_radius_sq;

	if (fighter_gobj == NULL)
	{
		return;
	}
	(void)pos;
	cull_radius_sq = (radius_sq < SYNETRB_SNAP_FIREBALL_DUP_CULL_RADIUS_SQ) ? radius_sq
	                                                                           : SYNETRB_SNAP_FIREBALL_DUP_CULL_RADIUS_SQ;
	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;)
	{
		GObj *next_gobj;
		WPStruct *wp;

		next_gobj = weapon_gobj->link_next;
		wp = wpGetStruct(weapon_gobj);
		if ((wp == NULL) || (wp->kind != nWPKindFireball) || (weapon_gobj == keep_fireball_gobj) ||
		    (syNetRbSnapWeaponOwnedByFighterGObj(wp, fighter_gobj) == FALSE))
		{
			weapon_gobj = next_gobj;
			continue;
		}
		if (DObjGetStruct(weapon_gobj) == NULL)
		{
			wpMainDestroyWeapon(weapon_gobj);
			weapon_gobj = next_gobj;
			continue;
		}
		/* Only cull duplicates still at the live hand — in-flight balls stay alive. */
		if (syNetRbSnapFireballNearFighterHand(fighter_gobj, weapon_gobj, cull_radius_sq) != FALSE)
		{
			wpMainDestroyWeapon(weapon_gobj);
		}
		weapon_gobj = next_gobj;
	}
}

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
		/* Windup before latch: only preserve a ball still at the hand (this throw), not carry-over. */
		return syNetRbSnapFireballNearFighterHand(owner_gobj, weapon_gobj, SYNETRB_SNAP_FIREBALL_HAND_RADIUS_SQ);
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

#ifdef PORT
	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return TRUE;
	}
#endif
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

#ifdef PORT
	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return FALSE;
	}
#endif
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

#ifdef PORT
	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return;
	}
#endif
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

	should_spawn = FALSE;
	spawn_path = NULL;

	if (fp->motion_vars.flags.flag1 != 0)
	{
		if (syNetRbSnapFireballOwnedByFighter(fighter_gobj) != FALSE)
		{
			syNetRbSnapFireballSpawnDiag("latched", fp, anim_frame);
			return;
		}
		/*
		 * Latched but no owned ball. flag1 is only set after a ball was actually made, so
		 * reaching here always means this throw already delivered one. In live forward sim a
		 * now-gone ball left legitimately (it launched and, at point-blank range, hit/expired
		 * within a frame) — re-spawning would emit a fresh fireball every throw-window frame,
		 * i.e. the close-range fireball stream the player sees as a "double"/multi throw. Only a
		 * rollback resim can lose a ball that *should* still exist, so the single recovery retry
		 * is restricted to resim and capped once per throw via flag2 (reset at throw entry in
		 * ft*SpecialNInitStatusVars). flag0/flag1 are the only motion flags Mario's fireball
		 * status uses; flag2 is otherwise unused here and is snapshotted (rollback-safe).
		 */
		fp->motion_vars.flags.flag1 = 0;
		syNetRbSnapFireballSpawnDiag("latch_clear", fp, anim_frame);
		if ((syNetRollbackIsResimulating() == FALSE) || (fp->motion_vars.flags.flag2 != 0) ||
		    (anim_frame >= SYNETRB_SNAP_FIREBALL_THROW_PRESERVE_FRAMES))
		{
			return;
		}
		fp->motion_vars.flags.flag2 = 1;
		should_spawn = TRUE;
		spawn_path = "retry";
	}
	else if (fp->motion_vars.flags.flag0 != 0)
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

	fireball_gobj = syNetRbSnapReacquireFireballAtHand(fighter_gobj, &pos, SYNETRB_SNAP_FIREBALL_HAND_RADIUS_SQ);
	if (fireball_gobj != NULL)
	{
		syNetRbSnapCullAllOwnedFireballsForFighter(fighter_gobj, fireball_gobj);
		syNetRbSnapCullOwnedFireballsNearPose(fighter_gobj, fireball_gobj, &pos, SYNETRB_SNAP_FIREBALL_HAND_RADIUS_SQ);
		fp->motion_vars.flags.flag1 = 1;
		if (spawn_path == NULL)
		{
			spawn_path = "reacquire";
		}
	}
	else if (syNetRbSnapFireballNeedsSpawnAtHand(fighter_gobj, &pos) == FALSE)
	{
		syNetRbSnapFireballSpawnDiag("skip_dedup", fp, anim_frame);
		if (syNetRbSnapWeaponDiagEnabled() != FALSE)
		{
			port_log("SSB64 NetRbSnapshot: fireball_spawn path=skip_dedup owner_player=%d joint=%d\n",
			         (int)fp->player, spawn_joint);
		}
		return;
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

void syNetRbSnapCullAllOrphanPKThunderLive(void)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		GObj *keep_head_gobj;

		fp = ftGetStruct(fighter_gobj);
		if ((fp == NULL) || ((fp->fkind != nFTKindNess) && (fp->fkind != nFTKindNNess)))
		{
			continue;
		}
		if (syNetRbSnapFighterIsInPKThunderSpecialHiStatus(fp) != FALSE)
		{
			keep_head_gobj = fp->status_vars.ness.specialhi.pkthunder_gobj;
			if ((keep_head_gobj == NULL) || (wpGetStruct(keep_head_gobj) == NULL))
			{
				keep_head_gobj = syNetRbSnapReacquirePKThunderHeadForFighter(fighter_gobj);
				fp->status_vars.ness.specialhi.pkthunder_gobj = keep_head_gobj;
			}
			syNetRbSnapCullOwnedPKThunderForFighter(fighter_gobj, keep_head_gobj);
		}
		else
		{
			fp->status_vars.ness.specialhi.pkthunder_gobj = NULL;
			syNetRbSnapCullOwnedPKThunderForFighter(fighter_gobj, NULL);
		}
	}
}

void syNetRbSnapPruneStaleNessPKWaveEffectsLive(void)
{
	syNetRbSnapPruneStaleNessPKWaveEffects(NULL);
}

void syNetRbSnapCullOwnedPKThunderForFighter(GObj *fighter_gobj, GObj *keep_head_gobj)
{
	GObj *weapon_gobj;
	FTStruct *fp;

#ifdef PORT
	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return;
	}
#endif
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
#ifdef PORT
	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return NULL;
	}
#endif
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

#ifdef PORT
	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return;
	}
#endif
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

#ifdef PORT
	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return;
	}
#endif
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

#ifdef PORT
	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return;
	}
#endif
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

#ifdef PORT
	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return;
	}
#endif
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

#ifdef PORT
	if (syNetplayRollbackSemanticsActive() == FALSE)
	{
		return TRUE;
	}
#endif
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
void syNetRbSnapshotRebindFighterItemHoldCoupling(void)
{
	syNetRbSnapRebindFighterItemHoldCoupling();
}

u32 syNetRbSnapshotGetSlotItemCount(u32 tick)
{
	SYNetRbSnapshotSlot *slot;

	slot = syNetRbSnapshotSlotForTick(tick);
	if ((slot == NULL) || (slot->is_valid == FALSE) || (slot->tick != tick))
	{
		return 0U;
	}
	return (u32)slot->item_count;
}

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

sb32 syNetRbSnapshotGetStoredSubsystemHashesEx(u32 tick, u32 *figh, u32 *world, u32 *item, u32 *rng, u32 *effect)
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
	if (effect != NULL)
	{
		*effect = slot->hash_effect;
	}
	return TRUE;
}

sb32 syNetRbSnapshotGetStoredSubsystemHashes(u32 tick, u32 *figh, u32 *world, u32 *item, u32 *rng)
{
	return syNetRbSnapshotGetStoredSubsystemHashesEx(tick, figh, world, item, rng, NULL);
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
