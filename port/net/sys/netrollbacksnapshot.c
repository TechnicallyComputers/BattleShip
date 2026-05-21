#include <sys/netrollbacksnapshot.h>

#include <sys/netsync.h>
#include <sys/objdef.h>
#include <sys/objhelper.h>
#include <sys/objman.h>
#include <sys/utils.h>

#include <ft/fighter.h>
#include <ft/ftchar/ftkirby/ftkirby.h>
#include <ft/ftchar/ftkirby/ftkirbyfunctions.h>
#include <ft/ftchar/ftlink/ftlink.h>
#include <ft/ftchar/ftpikachu/ftpikachu.h>
#include <ft/ftchar/ftness/ftness.h>
#include <ft/ftchar/ftsamus/ftsamus.h>
#include <ft/ftchar/ftsamus/ftsamusfunctions.h>
#include <ft/ftchar/ftyoshi/ftyoshi.h>
#include <ft/ftchar/ftyoshi/ftyoshifunctions.h>
#include <ft/ftdef.h>
#include <ft/ftmain.h>
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <it/itmain.h>
#include <it/itcommon/itfflower.h>
#include <it/itcommon/itlgun.h>
#include <it/itcommon/itstarrod.h>
#include <it/itvars.h>

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
	f32 length_invert;
	f32 length;
	f32 value_base;
	f32 value_target;
	f32 rate_base;
	f32 rate_target;

} SYNetRbSnapAObjNodeBlob;

typedef struct SYNetRbSnapDObjAnimBlob
{
	f32 anim_wait;
	f32 anim_speed;
	f32 anim_frame;
	u8 aobj_count;
	u8 aobj_chain_total;
	u8 pad[2];
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

#define SYNETRB_WEAPON_SPAWN_DEFAULT           0U
#define SYNETRB_WEAPON_SPAWN_PK_REFLECT_HEAD   1U
#define SYNETRB_WEAPON_SPAWN_PK_REFLECT_TRAIL  2U

typedef struct SYNetRbSnapCameraBlob
{
	GMCamera camera;
	u32 camera_gobj_id;
	u32 pzoom_fighter_gobj_id;
	u32 pfollow_fighter_gobj_id;
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
	SYNetRbSnapCameraBlob camera;

} SYNetRbSnapshotSlot;

static SYNetRbSnapshotSlot sSYNetRbSnapshotRing[SYNETRB_SNAPSHOT_RING_MAX];
static u32 sSYNetRbSnapshotRingLen = SYNETRB_SNAPSHOT_RING_DEFAULT;

#ifdef PORT
static SYNetRbSnapshotSlot sSYNetRbEmergencySlot;
static sb32 sSYNetRbEmergencyValid;
static s32 sSYNetRbSnapshotGuardLogBudget = 16;

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
static void syNetRbSnapshotFinalizeLoadCouplingFromSlot(const SYNetRbSnapshotSlot *slot);
static void syNetRbSnapshotFinalizeLoadFromSlot(const SYNetRbSnapshotSlot *slot, sb32 sync_presentation);

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

static GObj *syNetRbSnapFindLiveEggForFighter(GObj *fighter_gobj)
{
	GObj *weapon_gobj;

	for (weapon_gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; weapon_gobj != NULL;
	     weapon_gobj = weapon_gobj->link_next)
	{
		WPStruct *wp = wpGetStruct(weapon_gobj);

		if ((wp == NULL) || (wp->kind != nWPKindEggThrow) || (wp->owner_gobj != fighter_gobj))
		{
			continue;
		}
		if (wp->weapon_vars.egg_throw.is_throw == FALSE)
		{
			return weapon_gobj;
		}
	}
	return NULL;
}

static void syNetRbSnapCaptureFighterCoupledIds(SYNetRbSnapFighterBlob *blob, const FTStruct *fp)
{
	blob->coupled_egg_weapon_gobj_id = 0U;
	blob->coupled_boomerang_weapon_gobj_id = 0U;
	blob->coupled_spin_attack_weapon_gobj_id = 0U;
	blob->coupled_charge_weapon_gobj_id = 0U;
	blob->coupled_pkthunder_weapon_gobj_id = 0U;
	blob->coupled_thunder_weapon_gobj_id = 0U;

	if (fp->fkind == nFTKindYoshi)
	{
		if ((fp->status_id == nFTYoshiStatusSpecialHi) || (fp->status_id == nFTYoshiStatusSpecialAirHi))
		{
			blob->coupled_egg_weapon_gobj_id = syNetRbSnapGobjId(fp->status_vars.yoshi.specialhi.egg_gobj);
		}
	}
	if (fp->fkind == nFTKindLink)
	{
		blob->coupled_boomerang_weapon_gobj_id = syNetRbSnapGobjId(fp->passive_vars.link.boomerang_gobj);
		if ((fp->status_id == nFTLinkStatusSpecialHi) || (fp->status_id == nFTLinkStatusSpecialAirHi))
		{
			blob->coupled_spin_attack_weapon_gobj_id =
			    syNetRbSnapGobjId(fp->status_vars.link.specialhi.spin_attack_gobj);
		}
	}
	if (fp->fkind == nFTKindKirby)
	{
		blob->coupled_boomerang_weapon_gobj_id = syNetRbSnapGobjId(fp->passive_vars.kirby.copylink_boomerang_gobj);
		if ((fp->status_id == nFTKirbyStatusCopySamusSpecialNStart) ||
		    (fp->status_id == nFTKirbyStatusCopySamusSpecialNLoop) ||
		    (fp->status_id == nFTKirbyStatusCopySamusSpecialAirNStart))
		{
			blob->coupled_charge_weapon_gobj_id =
			    syNetRbSnapGobjId(fp->status_vars.kirby.copysamus_specialn.charge_gobj);
		}
	}
	if (fp->fkind == nFTKindSamus)
	{
		if ((fp->status_id == nFTSamusStatusSpecialNStart) || (fp->status_id == nFTSamusStatusSpecialNLoop) ||
		    (fp->status_id == nFTSamusStatusSpecialAirNStart))
		{
			blob->coupled_charge_weapon_gobj_id = syNetRbSnapGobjId(fp->status_vars.samus.specialn.charge_gobj);
		}
	}
	if (fp->fkind == nFTKindNess)
	{
		if ((fp->status_id == nFTNessStatusSpecialHiStart) || (fp->status_id == nFTNessStatusSpecialHiHold) ||
		    (fp->status_id == nFTNessStatusSpecialAirHiStart) ||
		    (fp->status_id == nFTNessStatusSpecialAirHiHold))
		{
			blob->coupled_pkthunder_weapon_gobj_id =
			    syNetRbSnapGobjId(fp->status_vars.ness.specialhi.pkthunder_gobj);
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
			    syNetRbSnapGobjId(fp->status_vars.pikachu.speciallw.thunder_gobj);
		}
	}
}

static void syNetRbSnapScrubCoupledPointersInBlob(SYNetRbSnapFighterBlob *blob)
{
	union FTStatusVars *status_vars = (union FTStatusVars *)blob->status_vars;
	union FTPassiveVars *passive_vars = (union FTPassiveVars *)blob->passive_vars;

	if (blob->fkind == nFTKindYoshi)
	{
		if ((blob->status_id == nFTYoshiStatusSpecialHi) || (blob->status_id == nFTYoshiStatusSpecialAirHi))
		{
			status_vars->yoshi.specialhi.egg_gobj = NULL;
		}
	}
	if (blob->fkind == nFTKindLink)
	{
		passive_vars->link.boomerang_gobj = NULL;
		if ((blob->status_id == nFTLinkStatusSpecialHi) || (blob->status_id == nFTLinkStatusSpecialAirHi))
		{
			status_vars->link.specialhi.spin_attack_gobj = NULL;
		}
	}
	if (blob->fkind == nFTKindKirby)
	{
		passive_vars->kirby.copylink_boomerang_gobj = NULL;
		if ((blob->status_id == nFTKirbyStatusCopySamusSpecialNStart) ||
		    (blob->status_id == nFTKirbyStatusCopySamusSpecialNLoop) ||
		    (blob->status_id == nFTKirbyStatusCopySamusSpecialAirNStart))
		{
			status_vars->kirby.copysamus_specialn.charge_gobj = NULL;
		}
	}
	if (blob->fkind == nFTKindSamus)
	{
		if ((blob->status_id == nFTSamusStatusSpecialNStart) || (blob->status_id == nFTSamusStatusSpecialNLoop) ||
		    (blob->status_id == nFTSamusStatusSpecialAirNStart))
		{
			status_vars->samus.specialn.charge_gobj = NULL;
		}
	}
	if (blob->fkind == nFTKindNess)
	{
		if ((blob->status_id == nFTNessStatusSpecialHiStart) || (blob->status_id == nFTNessStatusSpecialHiHold) ||
		    (blob->status_id == nFTNessStatusSpecialAirHiStart) ||
		    (blob->status_id == nFTNessStatusSpecialAirHiHold))
		{
			status_vars->ness.specialhi.pkthunder_gobj = NULL;
		}
	}
	if (blob->fkind == nFTKindPikachu)
	{
		if ((blob->status_id == nFTPikachuStatusSpecialLwStart) ||
		    (blob->status_id == nFTPikachuStatusSpecialLwLoop) ||
		    (blob->status_id == nFTPikachuStatusSpecialAirLwStart) ||
		    (blob->status_id == nFTPikachuStatusSpecialAirLwLoop))
		{
			status_vars->pikachu.speciallw.thunder_gobj = NULL;
		}
	}
}

static void syNetRbSnapScrubCoupledPointersInFighter(FTStruct *fp, const SYNetRbSnapFighterBlob *blob)
{
	if (fp->fkind == nFTKindYoshi)
	{
		if ((blob->status_id == nFTYoshiStatusSpecialHi) || (blob->status_id == nFTYoshiStatusSpecialAirHi))
		{
			fp->status_vars.yoshi.specialhi.egg_gobj = NULL;
		}
	}
	if (fp->fkind == nFTKindLink)
	{
		fp->passive_vars.link.boomerang_gobj = NULL;
		if ((blob->status_id == nFTLinkStatusSpecialHi) || (blob->status_id == nFTLinkStatusSpecialAirHi))
		{
			fp->status_vars.link.specialhi.spin_attack_gobj = NULL;
		}
	}
	if (fp->fkind == nFTKindKirby)
	{
		fp->passive_vars.kirby.copylink_boomerang_gobj = NULL;
		if ((blob->status_id == nFTKirbyStatusCopySamusSpecialNStart) ||
		    (blob->status_id == nFTKirbyStatusCopySamusSpecialNLoop) ||
		    (blob->status_id == nFTKirbyStatusCopySamusSpecialAirNStart))
		{
			fp->status_vars.kirby.copysamus_specialn.charge_gobj = NULL;
		}
	}
	if (fp->fkind == nFTKindSamus)
	{
		if ((blob->status_id == nFTSamusStatusSpecialNStart) || (blob->status_id == nFTSamusStatusSpecialNLoop) ||
		    (blob->status_id == nFTSamusStatusSpecialAirNStart))
		{
			fp->status_vars.samus.specialn.charge_gobj = NULL;
		}
	}
	if (fp->fkind == nFTKindNess)
	{
		if ((blob->status_id == nFTNessStatusSpecialHiStart) || (blob->status_id == nFTNessStatusSpecialHiHold) ||
		    (blob->status_id == nFTNessStatusSpecialAirHiStart) ||
		    (blob->status_id == nFTNessStatusSpecialAirHiHold))
		{
			fp->status_vars.ness.specialhi.pkthunder_gobj = NULL;
		}
	}
	if (fp->fkind == nFTKindPikachu)
	{
		if ((blob->status_id == nFTPikachuStatusSpecialLwStart) ||
		    (blob->status_id == nFTPikachuStatusSpecialLwLoop) ||
		    (blob->status_id == nFTPikachuStatusSpecialAirLwStart) ||
		    (blob->status_id == nFTPikachuStatusSpecialAirLwLoop))
		{
			fp->status_vars.pikachu.speciallw.thunder_gobj = NULL;
		}
	}
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

static void syNetRbSnapRebindFighterCoupledGObjs(const SYNetRbSnapshotSlot *slot)
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
		if (blob->coupled_egg_weapon_gobj_id != 0U)
		{
			egg_gobj = syNetRbSnapResolveLiveGobj(blob->coupled_egg_weapon_gobj_id);
			if (egg_gobj == NULL)
			{
				egg_gobj = syNetRbSnapFindLiveEggForFighter(fighter_gobj);
				if ((egg_gobj == NULL) && (syNetRbSnapCoupledDiagEnabled() != FALSE))
				{
					port_log(
					    "SSB64 NetRbSnapshot: SNAPSHOT_COUPLED_GOBJ_MISS tick=%u player=%d kind=egg id=%u\n",
					    (unsigned int)slot->tick,
					    (int)fp->player,
					    (unsigned int)blob->coupled_egg_weapon_gobj_id);
				}
			}
		}
		if (fp->fkind == nFTKindYoshi)
		{
			if ((fp->status_id == nFTYoshiStatusSpecialHi) || (fp->status_id == nFTYoshiStatusSpecialAirHi))
			{
				fp->status_vars.yoshi.specialhi.egg_gobj = egg_gobj;
				if (egg_gobj != NULL)
				{
					WPStruct *wp = wpGetStruct(egg_gobj);

					if ((wp != NULL) && (wp->weapon_vars.egg_throw.is_throw == FALSE))
					{
						ftYoshiSpecialHiUpdateEggVectors(fp);
					}
				}
				if (syNetRbSnapCoupledDiagEnabled() != FALSE)
				{
					port_log(
					    "SSB64 NetRbSnapshot: coupled egg tick=%u player=%d blob_id=%u egg_gobj=%p\n",
					    (unsigned int)slot->tick,
					    (int)fp->player,
					    (unsigned int)blob->coupled_egg_weapon_gobj_id,
					    (void *)egg_gobj);
				}
			}
		}

		boomerang_gobj = NULL;
		if (blob->coupled_boomerang_weapon_gobj_id != 0U)
		{
			boomerang_gobj = syNetRbSnapResolveLiveGobj(blob->coupled_boomerang_weapon_gobj_id);
			if ((boomerang_gobj == NULL) && (syNetRbSnapCoupledDiagEnabled() != FALSE))
			{
				port_log(
				    "SSB64 NetRbSnapshot: SNAPSHOT_COUPLED_GOBJ_MISS tick=%u player=%d kind=boomerang id=%u\n",
				    (unsigned int)slot->tick,
				    (int)fp->player,
				    (unsigned int)blob->coupled_boomerang_weapon_gobj_id);
			}
		}
		if (fp->fkind == nFTKindLink)
		{
			fp->passive_vars.link.boomerang_gobj = boomerang_gobj;
		}
		if (fp->fkind == nFTKindKirby)
		{
			fp->passive_vars.kirby.copylink_boomerang_gobj = boomerang_gobj;
		}

		spin_gobj = NULL;
		if (blob->coupled_spin_attack_weapon_gobj_id != 0U)
		{
			spin_gobj = syNetRbSnapResolveLiveGobj(blob->coupled_spin_attack_weapon_gobj_id);
			if ((spin_gobj == NULL) && (syNetRbSnapCoupledDiagEnabled() != FALSE))
			{
				port_log(
				    "SSB64 NetRbSnapshot: SNAPSHOT_COUPLED_GOBJ_MISS tick=%u player=%d kind=spin id=%u\n",
				    (unsigned int)slot->tick,
				    (int)fp->player,
				    (unsigned int)blob->coupled_spin_attack_weapon_gobj_id);
			}
		}
		if (fp->fkind == nFTKindLink)
		{
			if ((fp->status_id == nFTLinkStatusSpecialHi) || (fp->status_id == nFTLinkStatusSpecialAirHi))
			{
				fp->status_vars.link.specialhi.spin_attack_gobj = spin_gobj;
			}
		}

		if (blob->coupled_charge_weapon_gobj_id != 0U)
		{
			GObj *charge_gobj = syNetRbSnapResolveLiveGobj(blob->coupled_charge_weapon_gobj_id);

			if ((charge_gobj == NULL) && (syNetRbSnapCoupledDiagEnabled() != FALSE))
			{
				port_log(
				    "SSB64 NetRbSnapshot: SNAPSHOT_COUPLED_GOBJ_MISS tick=%u player=%d kind=charge id=%u\n",
				    (unsigned int)slot->tick,
				    (int)fp->player,
				    (unsigned int)blob->coupled_charge_weapon_gobj_id);
			}
			if (fp->fkind == nFTKindSamus)
			{
				if ((fp->status_id == nFTSamusStatusSpecialNStart) ||
				    (fp->status_id == nFTSamusStatusSpecialNLoop) ||
				    (fp->status_id == nFTSamusStatusSpecialAirNStart))
				{
					fp->status_vars.samus.specialn.charge_gobj = charge_gobj;
					if (charge_gobj != NULL)
					{
						ftSamusSpecialNSetChargeShotPosition(fp);
					}
				}
			}
			if (fp->fkind == nFTKindKirby)
			{
				if ((fp->status_id == nFTKirbyStatusCopySamusSpecialNStart) ||
				    (fp->status_id == nFTKirbyStatusCopySamusSpecialNLoop) ||
				    (fp->status_id == nFTKirbyStatusCopySamusSpecialAirNStart))
				{
					fp->status_vars.kirby.copysamus_specialn.charge_gobj = charge_gobj;
					if (charge_gobj != NULL)
					{
						ftKirbyCopySamusSpecialNSetChargeShotPosition(fp);
					}
				}
			}
		}

		if (blob->coupled_pkthunder_weapon_gobj_id != 0U)
		{
			GObj *pkthunder_gobj = syNetRbSnapResolveLiveGobj(blob->coupled_pkthunder_weapon_gobj_id);

			if ((pkthunder_gobj == NULL) && (syNetRbSnapCoupledDiagEnabled() != FALSE))
			{
				port_log(
				    "SSB64 NetRbSnapshot: SNAPSHOT_COUPLED_GOBJ_MISS tick=%u player=%d kind=pkthunder id=%u\n",
				    (unsigned int)slot->tick,
				    (int)fp->player,
				    (unsigned int)blob->coupled_pkthunder_weapon_gobj_id);
			}
			if (fp->fkind == nFTKindNess)
			{
				if ((fp->status_id == nFTNessStatusSpecialHiStart) ||
				    (fp->status_id == nFTNessStatusSpecialHiHold) ||
				    (fp->status_id == nFTNessStatusSpecialAirHiStart) ||
				    (fp->status_id == nFTNessStatusSpecialAirHiHold))
				{
					fp->status_vars.ness.specialhi.pkthunder_gobj = pkthunder_gobj;
				}
			}
		}

		if (blob->coupled_thunder_weapon_gobj_id != 0U)
		{
			GObj *thunder_gobj = syNetRbSnapResolveLiveGobj(blob->coupled_thunder_weapon_gobj_id);

			if ((thunder_gobj == NULL) && (syNetRbSnapCoupledDiagEnabled() != FALSE))
			{
				port_log(
				    "SSB64 NetRbSnapshot: SNAPSHOT_COUPLED_GOBJ_MISS tick=%u player=%d kind=thunder id=%u\n",
				    (unsigned int)slot->tick,
				    (int)fp->player,
				    (unsigned int)blob->coupled_thunder_weapon_gobj_id);
			}
			if (fp->fkind == nFTKindPikachu)
			{
				if ((fp->status_id == nFTPikachuStatusSpecialLwStart) ||
				    (fp->status_id == nFTPikachuStatusSpecialLwLoop) ||
				    (fp->status_id == nFTPikachuStatusSpecialAirLwStart) ||
				    (fp->status_id == nFTPikachuStatusSpecialAirLwLoop))
				{
					fp->status_vars.pikachu.speciallw.thunder_gobj = thunder_gobj;
				}
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
}

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
	}
}

static void syNetRbSnapApplyDObjAnim(DObj *dobj, const SYNetRbSnapDObjAnimBlob *src)
{
	AObj *aobj;
	u8 i;

	if (dobj == NULL)
	{
		return;
	}
	dobj->anim_wait = src->anim_wait;
	dobj->anim_speed = src->anim_speed;
	dobj->anim_frame = src->anim_frame;
	aobj = dobj->aobj;
	for (i = 0U; (aobj != NULL) && (i < src->aobj_count) && (i < SYNETROLLBACK_SNAPSHOT_AOBJ_CHAIN_MAX); i++)
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

#ifdef PORT
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
		matched_count++;
#endif
		gobj = next_gobj;
	}
	for (si = 0; si < slot->item_count; si++)
	{
		const SYNetRbSnapItemBlob *blob;
		Vec3f pos;
		Vec3f vel;
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
		pos = blob->translate;
		vel.x = vel.y = vel.z = 0.0F;
		spawned = itManagerMakeItemSetupCommon(NULL, blob->kind, &pos, &vel, ITEM_FLAG_PARENT_DEFAULT);
		if (spawned == NULL)
		{
			port_log("SSB64 NetRbSnapshot: item respawn failed kind=%d tick=%u gobj_id=%u\n",
			         (int)blob->kind,
			         (unsigned int)slot->tick,
			         (unsigned int)blob->gobj_id);
			continue;
		}
		syNetRbSnapApplyItemBlobToGObj(spawned, blob);
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

		if ((other->is_valid == FALSE) || (other->gobj_id == skip_gobj_id))
		{
			continue;
		}
		if ((other->kind == kind) && (other->group_id == group_id) && (other->player == player))
		{
			return other->gobj_id;
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
		                                                  blob->gobj_id);

	case nWPKindPKThunderHead:
		if (blob->spawn_profile == SYNETRB_WEAPON_SPAWN_PK_REFLECT_HEAD)
		{
			u32 pk_head_id;

			pk_head_id = syNetRbSnapFindWeaponGobjIdByKindGroupPlayer(slot, nWPKindPKThunderHead, blob->group_id,
			                                                          blob->player, blob->gobj_id);
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
		wp->weapon_vars.charge_shot.owner_gobj = syNetRbSnapResolveLiveGobj(blob->var_parent_gobj_id);
		if (wp->weapon_vars.charge_shot.owner_gobj == NULL)
		{
			wp->weapon_vars.charge_shot.owner_gobj = wp->owner_gobj;
		}
		break;

	case nWPKindBoomerang:
		wp->weapon_vars.boomerang.parent_gobj = syNetRbSnapResolveLiveGobj(blob->var_parent_gobj_id);
		if (wp->weapon_vars.boomerang.parent_gobj == NULL)
		{
			wp->weapon_vars.boomerang.parent_gobj = wp->owner_gobj;
		}
		break;

	case nWPKindPKThunderHead:
		wp->weapon_vars.pkthunder.parent_gobj = syNetRbSnapResolveLiveGobj(blob->var_parent_gobj_id);
		if (wp->weapon_vars.pkthunder.parent_gobj == NULL)
		{
			wp->weapon_vars.pkthunder.parent_gobj = wp->owner_gobj;
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
	parent_gobj = syNetRbSnapResolveLiveGobj(blob->spawn_parent_gobj_id);
	if (parent_gobj == NULL)
	{
		parent_gobj = syNetRbSnapResolveLiveGobj(blob->owner_gobj_id);
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
		blob->kind = wp->kind;
		blob->team = wp->team;
		blob->player = wp->player;
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
	owner_gobj = syNetRbSnapResolveLiveGobj(blob->owner_gobj_id);
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

	default:
#ifdef PORT
		port_log("SSB64 NetRbSnapshot: weapon respawn unsupported kind=%d\n", (int)blob->kind);
#endif
		return NULL;
	}
}

static void syNetRbSnapApplyWeaponBlobToGObj(GObj *gobj, const SYNetRbSnapWeaponBlob *blob)
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
	wp->owner_gobj = syNetRbSnapResolveLiveGobj(blob->owner_gobj_id);
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

	for (si = 0; si < SYNETRB_SNAPSHOT_MAX_WEAPONS; si++)
	{
		matched[si] = FALSE;
	}
	ejected_count = 0;
	matched_count = 0;
	respawned_count = 0;

	for (gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; gobj != NULL;)
	{
		GObj *next_gobj;
		s32 found;

		next_gobj = gobj->link_next;
		if (wpGetStruct(gobj) == NULL)
		{
			syNetRbSnapLogSkippedGObj("apply", "weapon", gobj, slot->tick);
			gobj = next_gobj;
			continue;
		}
		found = -1;
		for (si = 0; si < slot->weapon_count; si++)
		{
			if ((slot->weapons[si].is_valid != FALSE) && (slot->weapons[si].gobj_id == gobj->id) &&
			    (matched[si] == FALSE))
			{
				found = si;
				break;
			}
		}
		if (found < 0)
		{
			gcEjectGObj(gobj);
			ejected_count++;
			gobj = next_gobj;
			continue;
		}
		matched[found] = TRUE;
		matched_count++;
		syNetRbSnapApplyWeaponBlobToGObj(gobj, &slot->weapons[found]);
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
				syNetRbSnapApplyWeaponBlobToGObj(spawned, blob);
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
		found = -1;
		for (si = 0; si < slot->weapon_count; si++)
		{
			if ((slot->weapons[si].is_valid != FALSE) && (slot->weapons[si].gobj_id == gobj->id))
			{
				found = si;
				break;
			}
		}
		if (found >= 0)
		{
			syNetRbSnapApplyWeaponBlobMeta(wp, &slot->weapons[found]);
		}
	}

#ifdef PORT
	if (syNetRbSnapWeaponDiagEnabled() != FALSE)
	{
		port_log("SSB64 NetRbSnapshot: weapon apply tick=%u ejected=%d matched=%d respawned=%d blob_count=%d\n",
		         (unsigned int)slot->tick,
		         ejected_count,
		         matched_count,
		         respawned_count,
		         (int)slot->weapon_count);
	}
#endif
}

static void syNetRbSnapCaptureCamera(SYNetRbSnapCameraBlob *cam)
{
	extern f32 gGMCameraPauseCameraEyeX;
	extern f32 gGMCameraPauseCameraEyeY;

	cam->camera = gGMCameraStruct;
	cam->camera_gobj_id = syNetRbSnapGobjId(gGMCameraGObj);
	cam->pzoom_fighter_gobj_id = syNetRbSnapGobjId(gGMCameraStruct.pzoom_fighter_gobj);
	cam->pfollow_fighter_gobj_id = syNetRbSnapGobjId(gGMCameraStruct.pfollow_fighter_gobj);
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
	gGMCameraStruct.pzoom_fighter_gobj = syNetRbSnapResolveLiveGobj(cam->pzoom_fighter_gobj_id);
	gGMCameraStruct.pfollow_fighter_gobj = syNetRbSnapResolveLiveGobj(cam->pfollow_fighter_gobj_id);
	gGMCameraPauseCameraEyeX = cam->pause_eye_x;
	gGMCameraPauseCameraEyeY = cam->pause_eye_y;
}

static SYNetRbSnapshotSlot *syNetRbSnapshotSlotForTick(u32 tick)
{
	return &sSYNetRbSnapshotRing[tick % sSYNetRbSnapshotRingLen];
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
	syNetRbSnapCaptureCamera(&slot->camera);

	slot->hash_fighter = syNetSyncHashBattleFightersFull();
	slot->hash_world = syNetSyncHashRollbackWorld();
	slot->hash_item = syNetSyncHashActiveItemsForRollback();
	slot->hash_weapon = syNetSyncHashActiveWeaponsForRollback();
	slot->hash_map = syNetSyncHashMapCollisionKinematics();
	slot->hash_rng = syNetSyncHashRNGSeed();
	slot->hash_camera = syNetSyncHashGMCamera();
	slot->hash_animation = syNetSyncHashFighterAnimationStateForRollback();

	return TRUE;
}

static void syNetRbSnapApplySlotToLive(const SYNetRbSnapshotSlot *slot)
{
	GObj *fighter_gobj;

	/*
	 * Mirror syNetRbSnapFillSlotFromLive capture order (fighters before map/world) so MPColl/floor state
	 * applied from map does not run before fighter joint/coll restore — avoids LOAD_HASH_DRIFT on figh.
	 * Fighter-coupled weapon rebind runs in syNetRbSnapshotFinalizeLoadCoupling (before load-hash verify);
	 * figatree presentation sync runs after verify passes (syNetRbSnapshotSyncFighterPresentation).
	 * ftMainRebindStatusProcs runs only after load verify (syNetRbSnapshotRebindAllFighters).
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
	syNetRbSnapApplyWorld(&slot->world, slot->tick);
	syNetRbSnapApplyItems(slot);
	syNetRbSnapApplyWeapons(slot);
#ifdef PORT
	syNetRbSnapRebindFighterGrabCoupling();
	syNetRbSnapRebindFighterItemHoldCoupling();
#endif
	syNetRbSnapApplyCamera(&slot->camera);
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

sb32 syNetRbSnapshotRestoreLiveEmergency(void)
{
	if (sSYNetRbEmergencyValid == FALSE)
	{
		return FALSE;
	}
	syNetRbSnapApplySlotToLive(&sSYNetRbEmergencySlot);
	syNetRbSnapshotFinalizeLoadCouplingFromSlot(&sSYNetRbEmergencySlot);
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
 * Invoked from syNetRbSnapshotFinalizeLoad before load-hash verify so fighter-coupled weapons
 * (Yoshi egg vectors, Samus charge shot position, etc.) can query part world positions.
 *
 * syNetRbSnapshotRebindAllFighters runs after successful verify — proc pointers are not hashed.
 *
 * SSB64_NETPLAY_SNAPSHOT_FIGHTER_CLEANUP=force|full|1 — legacy ftMainSetStatus path for bisect only.
 */
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
static void syNetRbSnapshotFinalizeLoadCouplingFromSlot(const SYNetRbSnapshotSlot *slot)
{
	if ((slot == NULL) || (slot->is_valid == FALSE))
	{
		return;
	}
	syNetRbSnapRebindFighterCoupledGObjs(slot);
	syNetRbSnapRefreshWeaponHitPositions();
}

static void syNetRbSnapshotFinalizeLoadFromSlot(const SYNetRbSnapshotSlot *slot, sb32 sync_presentation)
{
	if ((slot == NULL) || (slot->is_valid == FALSE))
	{
		return;
	}
	syNetRbSnapshotFinalizeLoadCouplingFromSlot(slot);
	if (sync_presentation != FALSE)
	{
		syNetRbSnapshotSyncFighterPresentation();
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
	syNetRbSnapshotFinalizeLoadCouplingFromSlot(slot);
}

void syNetRbSnapshotFinalizeLoad(u32 completed_sim_tick)
{
	SYNetRbSnapshotSlot *slot;

	slot = syNetRbSnapshotSlotForTick(completed_sim_tick);
	if ((slot == NULL) || (slot->is_valid == FALSE) || (slot->tick != completed_sim_tick))
	{
		return;
	}
	syNetRbSnapshotFinalizeLoadFromSlot(slot, TRUE);
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
#endif
