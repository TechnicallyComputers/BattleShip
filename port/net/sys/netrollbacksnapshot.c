#include <sys/netrollbacksnapshot.h>

#include <sys/netsync.h>
#include <sys/objdef.h>
#include <sys/objhelper.h>
#include <sys/objman.h>
#include <sys/utils.h>

#include <ft/fighter.h>
#include <ft/ftdef.h>
#include <ft/ftmain.h>
#include <ft/fttypes.h>
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
#include <wp/wpmanager.h>
#include <wp/wptypes.h>

#ifdef PORT
#include <stdlib.h>
#include <string.h>

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
	u8 pad[3];
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

	FTModelPartStatus modelpart_status[FTPARTS_JOINT_NUM_MAX - nFTPartsJointCommonStart];
	FTTexturePartStatus texturepart_status[2];

	u8 status_vars[sizeof(union FTStatusVars)];
	u8 passive_vars[sizeof(union FTPassiveVars)];

	f32 gobj_anim_frame;

} SYNetRbSnapFighterBlob;

typedef struct SYNetRbSnapYakuBlob
{
	Vec3f translate;
	Vec3f speed;
	s32 user_data_s;

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
	u8 item_vars[sizeof(union ITStatusVars)];

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
	u8 weapon_vars[sizeof(union wpStatusVars)];

} SYNetRbSnapWeaponBlob;

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
	count = 0U;
	for (aobj = dobj->aobj; (aobj != NULL) && (count < SYNETROLLBACK_SNAPSHOT_AOBJ_CHAIN_MAX); aobj = aobj->next)
	{
		syNetRbSnapCaptureAObjNode(&dst->aobj[count], aobj);
		count++;
	}
	dst->aobj_count = count;
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
	memcpy(blob->modelpart_status, fp->modelpart_status, sizeof(blob->modelpart_status));
	memcpy(blob->texturepart_status, fp->texturepart_status, sizeof(blob->texturepart_status));

	for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
	{
		if (fp->joints[ji] != NULL)
		{
			blob->joint_translate[ji] = fp->joints[ji]->translate.vec.f;
			syNetRbSnapCaptureDObjAnim(&blob->joint_anim[ji], fp->joints[ji]);
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
	memcpy(fp->modelpart_status, blob->modelpart_status, sizeof(fp->modelpart_status));
	memcpy(fp->texturepart_status, blob->texturepart_status, sizeof(fp->texturepart_status));

	for (ji = 0; ji < FTPARTS_JOINT_NUM_MAX; ji++)
	{
		if (fp->joints[ji] != NULL)
		{
			fp->joints[ji]->translate.vec.f = blob->joint_translate[ji];
			syNetRbSnapApplyDObjAnim(fp->joints[ji], &blob->joint_anim[ji]);
		}
	}
	if (fighter_gobj != NULL)
	{
		fighter_gobj->anim_frame = blob->gobj_anim_frame;
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
			slot->mp_yaku[i].translate.x = slot->mp_yaku[i].translate.y = slot->mp_yaku[i].translate.z = 0.0F;
			slot->mp_yaku[i].speed.x = slot->mp_yaku[i].speed.y = slot->mp_yaku[i].speed.z = 0.0F;
			slot->mp_yaku[i].user_data_s = 0;
			continue;
		}
		slot->mp_yaku[i].translate = dobj->translate.vec.f;
		slot->mp_yaku[i].speed = gMPCollisionSpeeds[i];
		slot->mp_yaku[i].user_data_s = dobj->user_data.s;
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
		dobj->translate.vec.f = slot->mp_yaku[i].translate;
		dobj->user_data.s = slot->mp_yaku[i].user_data_s;
		gMPCollisionSpeeds[i] = slot->mp_yaku[i].speed;
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

static sb32 syNetRbSnapCaptureItems(SYNetRbSnapshotSlot *slot)
{
	GObj *gobj;
	s32 count;
	sb32 truncated;

	count = 0;
	truncated = FALSE;
	for (gobj = gGCCommonLinks[nGCCommonLinkIDItem]; gobj != NULL; gobj = gobj->link_next)
	{
		ITStruct *ip;
		SYNetRbSnapItemBlob *blob;
		DObj *dobj;
		Vec3f *topn;

		if (count >= SYNETRB_SNAPSHOT_MAX_ITEMS)
		{
			truncated = TRUE;
			break;
		}
		ip = itGetStruct(gobj);
		if (ip == NULL)
		{
			syNetRbSnapLogSkippedGObj("save", "item", gobj, slot->tick);
			continue;
		}
		blob = &slot->items[count];
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
		memcpy(blob->item_vars, &ip->item_vars, sizeof(blob->item_vars));
		(void)topn;
		count++;
	}
	slot->item_count = count;
	if (truncated != FALSE)
	{
		port_log("SSB64 NetRbSnapshot: item cap overflow (max=%d) tick=%u — save failed\n",
		         SYNETRB_SNAPSHOT_MAX_ITEMS,
		         (unsigned int)slot->tick);
		return FALSE;
	}
	return TRUE;
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
	ip->damage_coll = blob->damage_coll;
	ip->lifetime = blob->lifetime;
	ip->owner_gobj = syNetRbSnapResolveLiveGobj(blob->owner_gobj_id);
	ip->reflect_gobj = syNetRbSnapResolveLiveGobj(blob->reflect_gobj_id);
	ip->damage_gobj = syNetRbSnapResolveLiveGobj(blob->damage_gobj_id);
	ip->arrow_gobj = syNetRbSnapResolveArrowGobjForItem(blob->arrow_gobj_id, gobj, ip);
	ip->multi = blob->multi;
	ip->event_id = blob->event_id;
	ip->spin_step = blob->spin_step;
	if (dobj != NULL)
	{
		dobj->translate.vec.f = blob->translate;
	}
	memcpy(&ip->item_vars, blob->item_vars, sizeof(ip->item_vars));
}

static void syNetRbSnapApplyItems(const SYNetRbSnapshotSlot *slot)
{
	GObj *gobj;
	s32 si;
	sb32 matched[SYNETRB_SNAPSHOT_MAX_ITEMS];

	memset(matched, 0, sizeof(matched));
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
		found = -1;
		for (si = 0; si < slot->item_count; si++)
		{
			if ((slot->items[si].is_valid != FALSE) && (slot->items[si].gobj_id == gobj->id))
			{
				found = si;
				break;
			}
		}
		if (found < 0)
		{
			gcEjectGObj(gobj);
			gobj = next_gobj;
			continue;
		}
		matched[found] = TRUE;
		syNetRbSnapApplyItemBlobToGObj(gobj, &slot->items[found]);
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
	}
}

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
		dobj = DObjGetStruct(gobj);
		if (dobj != NULL)
		{
			blob->translate = dobj->translate.vec.f;
		}
		memcpy(blob->weapon_vars, &wp->weapon_vars, sizeof(blob->weapon_vars));
		count++;
	}
	slot->weapon_count = count;
	if (truncated != FALSE)
	{
		port_log("SSB64 NetRbSnapshot: weapon cap overflow (max=%d) tick=%u — save failed\n",
		         SYNETRB_SNAPSHOT_MAX_WEAPONS,
		         (unsigned int)slot->tick);
		return FALSE;
	}
	return TRUE;
}

static void syNetRbSnapApplyWeapons(const SYNetRbSnapshotSlot *slot)
{
	GObj *gobj;

	for (gobj = gGCCommonLinks[nGCCommonLinkIDWeapon]; gobj != NULL;)
	{
		GObj *next_gobj;
		WPStruct *wp;
		s32 si;
		s32 found;

		next_gobj = gobj->link_next;
		wp = wpGetStruct(gobj);
		if (wp == NULL)
		{
			syNetRbSnapLogSkippedGObj("apply", "weapon", gobj, slot->tick);
			gobj = next_gobj;
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
		if (found < 0)
		{
			gcEjectGObj(gobj);
			gobj = next_gobj;
			continue;
		}
		{
			const SYNetRbSnapWeaponBlob *blob = &slot->weapons[found];
			DObj *dobj;
			Vec3f *topn;

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
			}
			memcpy(&wp->weapon_vars, blob->weapon_vars, sizeof(wp->weapon_vars));
		}
		gobj = next_gobj;
	}
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
		sSYNetRbSnapshotRing[i].tick = ~(u32)0;
	}
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

	syNetRbSnapApplyMap(slot);
	syNetRbSnapApplyWorld(&slot->world, slot->tick);
	syNetRbSnapApplyCamera(&slot->camera);

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

	syNetRbSnapApplyItems(slot);
	syNetRbSnapApplyWeapons(slot);
	syNetRbSnapshotAfterApplyCleanup();
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
	sSYNetRbEmergencyValid = FALSE;
	return TRUE;
}

sb32 syNetRbSnapshotSave(u32 completed_sim_tick)
{
#ifdef PORT
	SYNetRbSnapshotSlot *slot;

	slot = syNetRbSnapshotSlotForTick(completed_sim_tick);
	return syNetRbSnapFillSlotFromLive(slot, completed_sim_tick);
#else
	(void)completed_sim_tick;
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

void syNetRbSnapshotAfterApplyCleanup(void)
{
#ifdef PORT
	/*
	 * Keep active AObj/MObj chains intact. They are part of the live animation state restored above; stripping them
	 * here leaves fighters with a valid motion/status but no figatree playback until the next status transition.
	 */
#else
	(void)0;
#endif
}

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
#endif
