#include <mn/menu.h>
#include <ft/fighter.h>
#include <gr/ground.h>
// #include <gm/gmsound.h> // temporary, until this finds a proper place (included from fighter.h)
#include <sc/scene.h>
#include <sys/video.h>
#include <sys/controller.h>
#include <sys/rdp.h>
#include <reloc_data.h>
#ifdef PORT
extern void *func_800269C0_275C0(u16 id);
#endif
/*
 * sc/scene.h may pull <lb/library.h> (decomp) which includes decomp lbcommon.h first.
 * Undef the shared guard so the port/net shadow can load.
 */
#ifdef _LBCOMMON_H_
#undef _LBCOMMON_H_
#endif
#ifdef LBCOMMON_H_PORT_NET
#undef LBCOMMON_H_PORT_NET
#endif
#include <lbcommon.h>
#ifndef LBCOMMON_H_PORT_NET
#error "BattleShip netmenu menus expect port/net lb/lbcommon.h shadow."
#endif

/*
 * Netmenu level prefs: mnmaps fork for nSCKindVSNetLevelPrefs only (SSB64_NETMENU).
 * Init treats entry from VS online like VS battle; Back (non-training) returns to VS online.
 */

void mnVSNetLevelPrefsMapsSetPreviewCameraPosition(CObj *cobj, s32 gkind);
sb32 mnVSNetLevelPrefsMapsCheckLocked(s32 gkind);
s32 mnVSNetLevelPrefsMapsGetGroundKind(s32 slot);
s32 mnVSNetLevelPrefsMapsGetSlot(s32 gkind);
void mnVSNetLevelPrefsMapsMakePreview(s32 gkind);

// // // // // // // // // // // //
//                               //
//       INITIALIZED DATA        //
//                               //
// // // // // // // // // // // //

// 0x801344D0
u32 dMNVSNetLevelPrefsMapsFileIDs[/* */] =
{
#ifdef PORT
	llFTEmblemSpritesFileID,
	llMNSelectCommonFileID,
	llMNMapsFileID,
	llMNCommonFontsFileID,
	llGRWallpaperTrainingBlackFileID
#else
	&llFTEmblemSpritesFileID,
	&llMNSelectCommonFileID,
	&llMNMapsFileID,
	&llMNCommonFontsFileID,
	&llGRWallpaperTrainingBlackFileID
#endif
};

// 0x801344E4
GRFileInfo dMNVSNetLevelPrefsMapsFileInfos[/* */] =
{
#ifdef PORT
	{ llGRCastleMapFileID,   llGRCastleMapMapHeader },
	{ llGRSectorMapFileID,   llGRSectorMapMapHeader },
	{ llGRJungleMapFileID,   llGRJungleMapMapHeader },
	{ llGRZebesMapFileID,    llGRZebesMapMapHeader },
	{ llGRHyruleMapFileID,   llGRHyruleMapMapHeader },
	{ llGRYosterMapFileID,   llGRYosterMapMapHeader },
	{ llGRPupupuMapFileID,   llGRPupupuMapMapHeader },
	{ llGRYamabukiMapFileID, llGRYamabukiMapMapHeader },
	{ llGRInishieMapFileID,  llGRInishieMapMapHeader }
#else
	{ &llGRCastleMapFileID,   &llGRCastleMapMapHeader },
	{ &llGRSectorMapFileID,   &llGRSectorMapMapHeader },
	{ &llGRJungleMapFileID,   &llGRJungleMapMapHeader },
	{ &llGRZebesMapFileID,    &llGRZebesMapMapHeader },
	{ &llGRHyruleMapFileID,   &llGRHyruleMapMapHeader },
	{ &llGRYosterMapFileID,   &llGRYosterMapMapHeader },
	{ &llGRPupupuMapFileID,   &llGRPupupuMapMapHeader },
	{ &llGRYamabukiMapFileID, &llGRYamabukiMapMapHeader },
	{ &llGRInishieMapFileID,  &llGRInishieMapMapHeader }
#endif
};

// 0x8013452C
intptr_t dMNVSNetLevelPrefsMapsWallpaperOffsets[/* */] =
{
	0x00026C88, 0x00026C88, 0x00026C88,
	0x00026C88, 0x00026C88, 0x00026C88,
	0x00026C88, 0x00026C88, 0x00026C88
};

// 0x80134550
GRFileInfo dMNVSNetLevelPrefsMapsTrainingModeFileInfos[/* */] =
{
#ifdef PORT
	{ llGRWallpaperTrainingBlackFileID, 0x00000000 },
	{ llGRWallpaperTrainingYellowFileID,0xEE9E0600 },
	{ llGRWallpaperTrainingBlueFileID, 	0xAFF5FF00 }
#else
	{ &llGRWallpaperTrainingBlackFileID, 0x00000000 },
	{ &llGRWallpaperTrainingYellowFileID,0xEE9E0600 },
	{ &llGRWallpaperTrainingBlueFileID, 	0xAFF5FF00 }
#endif
};

// 0x80134568
s32 dMNVSNetLevelPrefsMapsTrainingModeWallpaperIDs[/* */] = { 2, 0, 0, 0, 2, 1, 2, 2, 2, 0 };

// 0x80134590
Lights1 dMNVSNetLevelPrefsMapsLights1 = gdSPDefLights1(0x20, 0x20, 0x20, 0xFF, 0xFF, 0xFF, 0x14, 0x14, 0x14);

#ifdef SSB64_NETMENU
Lights1 dMNVSNetLevelPrefsMapsLightsGrey = gdSPDefLights1(0x22, 0x22, 0x22, 0xA0, 0xA0, 0xA0, 0x18, 0x18, 0x18);
#endif

// 0x801345A8
Gfx dMNVSNetLevelPrefsMapsDisplayList[/* */] =
{
	gsSPSetGeometryMode(G_LIGHTING),
	gsSPSetLights1(dMNVSNetLevelPrefsMapsLights1),
	gsSPEndDisplayList()
};

// // // // // // // // // // // //
//                               //
//   GLOBAL / STATIC VARIABLES   //
//                               //
// // // // // // // // // // // //

// 0x80134BD0
s32 sMNVSNetLevelPrefsMapsPad0x80134BD0[2];

// 0x80134BD8
s32 sMNVSNetLevelPrefsMapsCursorSlot;

// 0x80134BDC
GObj *sMNVSNetLevelPrefsMapsCursorGObj;

// 0x80134BE0
GObj *sMNVSNetLevelPrefsMapsNameLogoGObj;

// 0x80134BE4
GObj *sMNVSNetLevelPrefsMapsHeap0WallpaperGObj;

// 0x80134BE8
GObj *sMNVSNetLevelPrefsMapsHeap1WallpaperGObj;

// 0x80134BF0
GObj *sMNVSNetLevelPrefsMapsHeap0LayerGObjs[4];

// 0x80134C00
GObj *sMNVSNetLevelPrefsMapsHeap1LayerGObjs[4];

// 0x80134C10
MPGroundData *sMNVSNetLevelPrefsMapsGroundInfo;

// 0x80134C14;
CObj *sMNVSNetLevelPrefsMapsPreviewCObj;

// 0x80134C18
sb32 sMNVSNetLevelPrefsMapsIsTrainingMode;

// 0x80134C1C - // flag indicating which bonus features are available
u8 sMNVSNetLevelPrefsMapsUnlockedMask;

// 0x80134C20
s32 sMNVSNetLevelPrefsMapsHeapID;

// 0x80134C24 - Frames elapsed on SSS
s32 sMNVSNetLevelPrefsMapsTotalTimeTics;

// 0x80134C28 - Frames until cursor can be moved again
s32 sMNVSNetLevelPrefsMapsScrollWait;

// 0x80134C2C - Frames to wait until exiting Stage Select
s32 sMNVSNetLevelPrefsMapsReturnTic;

// 0x80134C30
LBFileNode sMNVSNetLevelPrefsMapsStatusBuffer[30];

// 0x80134D20
LBFileNode sMNVSNetLevelPrefsMapsForceStatusBuffer[30];

// 0x80134E10
void *sMNVSNetLevelPrefsMapsFiles[ARRAY_COUNT(dMNVSNetLevelPrefsMapsFileIDs)];

// 0x80134E24
void *sMNVSNetLevelPrefsMapsModelHeap0;

// 0x80134E28
void *sMNVSNetLevelPrefsMapsModelHeap1;

#ifdef SSB64_NETMENU
static SObj *sMNVSNetLevelPrefsMapsIconSlotSObjs[10];
static s32 sMNVSNetLevelPrefsMapsActivePreviewGkind;
#endif

// // // // // // // // // // // //
//                               //
//           FUNCTIONS           //
//                               //
// // // // // // // // // // // //

#ifdef SSB64_NETMENU
static u16 mnVSNetLevelPrefsMapsForcedBanBits(void)
{
	return mnVSNetLevelPrefsMapsCheckLocked(nGRKindInishie) != FALSE ? (u16)(1u << 4) : (u16)0;
}

static u16 mnVSNetLevelPrefsMapsEffectiveBanMask(void)
{
	return (u16)(gSCManagerSceneData.vs_net_stage_ban_mask | mnVSNetLevelPrefsMapsForcedBanBits());
}

static u32 mnVSNetLevelPrefsMapsPopcount16(u16 v)
{
	u32 n;

	for (n = 0; v != 0; v >>= 1)
		n += (u32)(v & 1u);
	return n;
}

static u16 mnVSNetLevelPrefsMapsSanitizeUserBanMask(u16 mask)
{
	mask &= (u16)((1u << 9) - 1u);

	if (mnVSNetLevelPrefsMapsCheckLocked(nGRKindInishie) != FALSE)
		mask &= (u16)~(u16)(1u << 4);

	while (mnVSNetLevelPrefsMapsPopcount16(mask) > 2u)
		mask &= (u16)(mask - 1u);

	return mask;
}

static sb32 mnVSNetLevelPrefsMapsIsEffectiveBanForActivePreview(void)
{
	s32 slot;

	if (sMNVSNetLevelPrefsMapsActivePreviewGkind == 0xDE)
		return FALSE;

	slot = mnVSNetLevelPrefsMapsGetSlot(sMNVSNetLevelPrefsMapsActivePreviewGkind);

	if (slot < 0 || slot > 8)
		return FALSE;

	return (mnVSNetLevelPrefsMapsEffectiveBanMask() & (u16)(1u << slot)) != 0;
}

static void mnVSNetLevelPrefsMapsTintSObjGrey(SObj *sobj, sb32 grey)
{
	if (grey != FALSE)
	{
		sobj->sprite.red = 0x58;
		sobj->sprite.green = 0x58;
		sobj->sprite.blue = 0x58;
	}
	else
	{
		sobj->sprite.red = 0xFF;
		sobj->sprite.green = 0xFF;
		sobj->sprite.blue = 0xFF;
	}
}

static void mnVSNetLevelPrefsMapsTintWallpaperChain(GObj *gobj, sb32 grey)
{
	SObj *sobj;

	if (gobj == NULL)
		return;

	for (sobj = SObjGetStruct(gobj); sobj != NULL; sobj = sobj->next)
		mnVSNetLevelPrefsMapsTintSObjGrey(sobj, grey);
}

static void mnVSNetLevelPrefsMapsRefreshIconBanTints(void)
{
	u16 eff;
	s32 i;

	eff = mnVSNetLevelPrefsMapsEffectiveBanMask();

	for (i = 0; i <= 8; i++)
	{
		if (sMNVSNetLevelPrefsMapsIconSlotSObjs[i] != NULL)
			mnVSNetLevelPrefsMapsTintSObjGrey(sMNVSNetLevelPrefsMapsIconSlotSObjs[i], (eff & (u16)(1u << i)) ? TRUE : FALSE);
	}
}

static void mnVSNetLevelPrefsMapsRefreshAllWallpaperGreys(void)
{
	sb32 g = mnVSNetLevelPrefsMapsIsEffectiveBanForActivePreview();

	if (sMNVSNetLevelPrefsMapsHeap0WallpaperGObj != NULL)
		mnVSNetLevelPrefsMapsTintWallpaperChain(sMNVSNetLevelPrefsMapsHeap0WallpaperGObj, g);
	if (sMNVSNetLevelPrefsMapsHeap1WallpaperGObj != NULL)
		mnVSNetLevelPrefsMapsTintWallpaperChain(sMNVSNetLevelPrefsMapsHeap1WallpaperGObj, g);
}

static void mnVSNetLevelPrefsMapsTryToggleBanAtCursor(void)
{
	s32 slot;
	u16 bit;
	u16 *m;

	if (sMNVSNetLevelPrefsMapsIsTrainingMode != FALSE)
		return;

	slot = sMNVSNetLevelPrefsMapsCursorSlot;
	m = &gSCManagerSceneData.vs_net_stage_ban_mask;

	if (slot == 9)
	{
		*m = 0;
		*m = mnVSNetLevelPrefsMapsSanitizeUserBanMask(*m);
		func_800269C0_275C0(nSYAudioFGMMenuScroll2);
	}
	else
	{
		bit = (u16)(1u << slot);

		if (mnVSNetLevelPrefsMapsCheckLocked(mnVSNetLevelPrefsMapsGetGroundKind(slot)) != FALSE)
			return;

		if ((*m & bit) != 0)
		{
			*m &= (u16)~bit;
			func_800269C0_275C0(nSYAudioFGMMenuScroll2);
		}
		else
		{
			if (mnVSNetLevelPrefsMapsPopcount16(*m) >= 2u)
				return;

			*m |= bit;
			func_800269C0_275C0(nSYAudioFGMMenuScroll2);
		}
		*m = mnVSNetLevelPrefsMapsSanitizeUserBanMask(*m);
	}
	mnVSNetLevelPrefsMapsRefreshIconBanTints();
	mnVSNetLevelPrefsMapsRefreshAllWallpaperGreys();
	if (sMNVSNetLevelPrefsMapsIsTrainingMode == FALSE)
	{
		mnVSNetLevelPrefsMapsMakePreview(mnVSNetLevelPrefsMapsGetGroundKind(sMNVSNetLevelPrefsMapsCursorSlot));
	}
}

static void mnVSNetLevelPrefsMapsIconsProcDisplay(GObj *gobj)
{
	lbCommonDrawSObjChainDecalAsPrimMultiply(gobj);
}
#endif /* SSB64_NETMENU */

// 0x80131B00
void mnVSNetLevelPrefsMapsAllocModelHeaps(void)
{
	size_t size, max = 0;
	s32 i;

	for (i = 0; i < ARRAY_COUNT(dMNVSNetLevelPrefsMapsFileInfos); i++)
	{
		size = lbRelocGetFileSize(dMNVSNetLevelPrefsMapsFileInfos[i].file_id);

		if (max < size)
		{
			max = size;
		}
	}
	sMNVSNetLevelPrefsMapsModelHeap0 = syTaskmanMalloc(max, 0x10);
	sMNVSNetLevelPrefsMapsModelHeap1 = syTaskmanMalloc(max, 0x10);
}

// 0x80131B88
void mnVSNetLevelPrefsMapsFuncLights(Gfx **dls)
{
	gSPDisplayList(dls[0]++, dMNVSNetLevelPrefsMapsDisplayList);
}

// 0x80131BAC
sb32 mnVSNetLevelPrefsMapsCheckLocked(s32 gkind)
{
	if (gkind == nGRKindInishie)
	{
		if (sMNVSNetLevelPrefsMapsUnlockedMask & LBBACKUP_UNLOCK_MASK_INISHIE)
		{
			return FALSE;
		}
		else return TRUE;
	}
	else return FALSE;
}

// 0x80131BE4
s32 mnVSNetLevelPrefsMapsGetCharacterID(const char c)
{
	switch (c)
	{
	case '\'':
		return 0x1A;

	case '%':
		return 0x1B;

	case '.':
		return 0x1C;

	case ' ':
		return 0x1D;

	default:
		if ((c < 'A') || (c > 'Z'))
		{
			return 0x1D;
		}
		else return c - 'A';
	}
}

// 0x80131C5C
f32 mnVSNetLevelPrefsMapsGetCharacterSpacing(const char *str, s32 c)
{
	switch (str[c])
	{
	case 'A':
		switch (str[c + 1])
		{
		case 'F':
		case 'P':
		case 'T':
		case 'V':
		case 'Y':
			return 0.0F;

		default:
			return 1.0F;
		}
		break;

	case 'F':
	case 'P':
	case 'V':
	case 'Y':
		switch(str[c + 1])
		{
		case 'A':
		case 'T':
			return 0.0F;

		default:
			return 1.0F;
		}
		break;

	case 'Q':
	case 'T':
		switch(str[c + 1])
		{
		case '\'':
		case '.':
			return 1.0F;

		default:
			return 0.0F;
		}
		break;

	case '\'':
		return 1.0F;

	case '.':
		return 1.0F;

	default:
		switch(str[c + 1])
		{
		case 'T':
			return 0.0F;

		default:
			return 1.0F;
		}
		break;
	}
}

// 0x80131D80 - Unused?
void mnVSNetLevelPrefsMapsMakeString(GObj *gobj, const char *str, f32 x, f32 y, u32 *color)
{
	intptr_t chars[/* */] =
	{
#ifdef PORT
		llMNCommonFontsLetterASprite, llMNCommonFontsLetterBSprite,
		llMNCommonFontsLetterCSprite, llMNCommonFontsLetterDSprite,
		llMNCommonFontsLetterESprite, llMNCommonFontsLetterFSprite,
		llMNCommonFontsLetterGSprite, llMNCommonFontsLetterHSprite,
		llMNCommonFontsLetterISprite, llMNCommonFontsLetterJSprite,
		llMNCommonFontsLetterKSprite, llMNCommonFontsLetterLSprite,
		llMNCommonFontsLetterMSprite, llMNCommonFontsLetterNSprite,
		llMNCommonFontsLetterOSprite, llMNCommonFontsLetterPSprite,
		llMNCommonFontsLetterQSprite, llMNCommonFontsLetterRSprite,
		llMNCommonFontsLetterSSprite, llMNCommonFontsLetterTSprite,
		llMNCommonFontsLetterUSprite, llMNCommonFontsLetterVSprite,
		llMNCommonFontsLetterWSprite, llMNCommonFontsLetterXSprite,
		llMNCommonFontsLetterYSprite, llMNCommonFontsLetterZSprite,
#else
		&llMNCommonFontsLetterASprite, &llMNCommonFontsLetterBSprite,
		&llMNCommonFontsLetterCSprite, &llMNCommonFontsLetterDSprite,
		&llMNCommonFontsLetterESprite, &llMNCommonFontsLetterFSprite,
		&llMNCommonFontsLetterGSprite, &llMNCommonFontsLetterHSprite,
		&llMNCommonFontsLetterISprite, &llMNCommonFontsLetterJSprite,
		&llMNCommonFontsLetterKSprite, &llMNCommonFontsLetterLSprite,
		&llMNCommonFontsLetterMSprite, &llMNCommonFontsLetterNSprite,
		&llMNCommonFontsLetterOSprite, &llMNCommonFontsLetterPSprite,
		&llMNCommonFontsLetterQSprite, &llMNCommonFontsLetterRSprite,
		&llMNCommonFontsLetterSSprite, &llMNCommonFontsLetterTSprite,
		&llMNCommonFontsLetterUSprite, &llMNCommonFontsLetterVSprite,
		&llMNCommonFontsLetterWSprite, &llMNCommonFontsLetterXSprite,
		&llMNCommonFontsLetterYSprite, &llMNCommonFontsLetterZSprite,
#endif

#ifdef PORT
		llMNCommonFontsSymbolApostropheSprite,
		llMNCommonFontsSymbolPercentSprite,
		llMNCommonFontsSymbolPeriodSprite
#else
		&llMNCommonFontsSymbolApostropheSprite,
		&llMNCommonFontsSymbolPercentSprite,
		&llMNCommonFontsSymbolPeriodSprite
#endif
	};
	SObj *sobj;
	f32 start_x = x;
	s32 i;

	for (i = 0; str[i] != 0; i++)
	{
		if (((((str[i] >= '0') && (str[i] <= '9')) ? TRUE : FALSE)) || (str[i] == ' '))
		{
			if (str[i] == ' ')
			{
				start_x += 4.0F;
			}
			else start_x += str[i] - '0';
		}
		else
		{
			sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[3], chars[mnVSNetLevelPrefsMapsGetCharacterID(str[i])]));
			sobj->pos.x = start_x;

			start_x += sobj->sprite.width + mnVSNetLevelPrefsMapsGetCharacterSpacing(str, i);

			switch (str[i])
			{
			case '\'':
				sobj->pos.y = y - 1.0F;
				break;
			
			case '.':
				sobj->pos.y = y + 4.0F;
				break;
				
			default:
				sobj->pos.y = y;
				break;
			}
			sobj->sprite.attr &= ~SP_FASTCOPY;
			sobj->sprite.attr |= SP_TRANSPARENT;

			sobj->sprite.red = color[0];
			sobj->sprite.green = color[1];
			sobj->sprite.blue = color[2];
		}
	}
}

// 0x80131FA4
void mnVSNetLevelPrefsMapsMakeWallpaper(void)
{
	GObj *gobj;
	SObj *sobj;

	gobj = gcMakeGObjSPAfter(0, NULL, 2, GOBJ_PRIORITY_DEFAULT);
	gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 0, GOBJ_PRIORITY_DEFAULT, ~0);
	
#ifdef PORT
	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[1], llMNSelectCommonStoneBackgroundSprite));
#else
	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[1], &llMNSelectCommonStoneBackgroundSprite));
#endif

	sobj->cms = G_TX_WRAP;
	sobj->cmt = G_TX_WRAP;

	sobj->masks = 6;
	sobj->maskt = 5;

	sobj->lrs = 300;
	sobj->lrt = 220;

	sobj->pos.x = 10.0F;
	sobj->pos.y = 10.0F;
}

// 0x80132048
void mnVSNetLevelPrefsMapsMakePlaque(void)
{
	GObj *gobj;
	SObj *sobj;

	gobj = gcMakeGObjSPAfter(0, NULL, 8, GOBJ_PRIORITY_DEFAULT);
	gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 6, GOBJ_PRIORITY_DEFAULT, ~0);

#ifdef PORT
	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], llMNMapsWoodenCircleSprite));
#else
	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], &llMNMapsWoodenCircleSprite));
#endif
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;

	sobj->pos.x = 189.0F;
	sobj->pos.y = 124.0F;
}

// 0x801320E0
void mnVSNetLevelPrefsMapsLabelsProcDisplay(GObj *gobj)
{
	gDPPipeSync(gSYTaskmanDLHeads[0]++);
	gDPSetCycleType(gSYTaskmanDLHeads[0]++, G_CYC_1CYCLE);
	gDPSetPrimColor(gSYTaskmanDLHeads[0]++, 0, 0, 0x57, 0x60, 0x88, 0xFF);
	gDPSetCombineMode(gSYTaskmanDLHeads[0]++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
	gDPSetRenderMode(gSYTaskmanDLHeads[0]++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
	gDPSetPrimColor(gSYTaskmanDLHeads[0]++, 0, 0, 0x57, 0x60, 0x88, 0xFF);
	gDPFillRectangle(gSYTaskmanDLHeads[0]++, 160, 128, 320, 134);
	gDPSetPrimColor(gSYTaskmanDLHeads[0]++, 0, 0, 0x00, 0x00, 0x00, 0x33);
	gDPFillRectangle(gSYTaskmanDLHeads[0]++, 194, 189, 268, 193);
	gDPPipeSync(gSYTaskmanDLHeads[0]++);
	gDPSetRenderMode(gSYTaskmanDLHeads[0]++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
	gDPSetCycleType(gSYTaskmanDLHeads[0]++, G_CYC_1CYCLE);

	lbCommonClearExternSpriteParams();
	lbCommonDrawSObjAttr(gobj);
}

// 0x80132288
void mnVSNetLevelPrefsMapsMakeLabels(void)
{
	GObj *gobj;
	SObj *sobj;
	s32 x;

	gobj = gcMakeGObjSPAfter(0, NULL, 6, GOBJ_PRIORITY_DEFAULT);
	gcAddGObjDisplay(gobj, mnVSNetLevelPrefsMapsLabelsProcDisplay, 4, GOBJ_PRIORITY_DEFAULT, ~0);

#ifdef PORT
	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], llMNMapsStageSelectTextSprite));
#else
	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], &llMNMapsStageSelectTextSprite));
#endif
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;

	sobj->envcolor.r = 0x00;
	sobj->envcolor.g = 0x00;
	sobj->envcolor.b = 0x00;

	sobj->sprite.red = 0xAF;
	sobj->sprite.green = 0xB1;
	sobj->sprite.blue = 0xCC;

	sobj->pos.x = 172.0F;
	sobj->pos.y = 122.0F;

#ifdef PORT
	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], llMNMapsPlateLeftSprite));
#else
	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], &llMNMapsPlateLeftSprite));
#endif
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;

	sobj->pos.x = 174.0F;
	sobj->pos.y = 191.0F;

	for (x = 186; x < 262; x += 4)
	{
#ifdef PORT
		sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], llMNMapsPlateMiddleSprite));
#else
		sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], &llMNMapsPlateMiddleSprite));
#endif
		sobj->pos.x = x;
		sobj->pos.y = 191.0F;
	}
#ifdef PORT
	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], llMNMapsPlateRightSprite));
#else
	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], &llMNMapsPlateRightSprite));
#endif
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;

	sobj->pos.x = 262.0F;
	sobj->pos.y = 191.0F;
}

// 0x80132430
s32 mnVSNetLevelPrefsMapsGetGroundKind(s32 slot)
{
	s32 gkinds[/* */] =
	{
		nGRKindCastle, nGRKindJungle, nGRKindHyrule, nGRKindZebes, nGRKindInishie,
		nGRKindYoster, nGRKindPupupu, nGRKindSector, nGRKindYamabuki, 0xDE
	};

	if (slot == 9)
	{
		return 0xDE;
	}
	return gkinds[slot];
}

// 0x80132498
s32 mnVSNetLevelPrefsMapsGetSlot(s32 gkind)
{
	switch (gkind)
	{
	case nGRKindCastle:
		return 0;
		
	case nGRKindJungle:
		return 1;
		
	case nGRKindHyrule:
		return 2;
		
	case nGRKindZebes:
		return 3;
		
	case nGRKindInishie:
		return 4;
		
	case nGRKindYoster:
		return 5;
		
	case nGRKindPupupu:
		return 6;
		
	case nGRKindSector:
		return 7;
		
	case nGRKindYamabuki:
		return 8;
		
	case 0xDE:
		return 9;
	}
#ifdef PORT
	return 0;
#endif
}

// 0x80132528
void mnVSNetLevelPrefsMapsMakeIcons(void)
{
	GObj *gobj;
	SObj *sobj;

	intptr_t offsets[/* */] =
	{
#ifdef PORT
		llMNMapsPeachsCastleSprite, 	llMNMapsSectorZSprite,
		llMNMapsCongoJungleSprite, 	llMNMapsPlanetZebesSprite,
		llMNMapsHyruleCastleSprite, 	llMNMapsYoshisIslandSprite,
		llMNMapsDreamLandSprite,	llMNMapsSaffronCitySprite,
		llMNMapsMushroomKingdomSprite,	llMNMapsRandomSmallSprite
#else
		&llMNMapsPeachsCastleSprite, 	&llMNMapsSectorZSprite,
		&llMNMapsCongoJungleSprite, 	&llMNMapsPlanetZebesSprite,
		&llMNMapsHyruleCastleSprite, 	&llMNMapsYoshisIslandSprite,
		&llMNMapsDreamLandSprite,	&llMNMapsSaffronCitySprite,
		&llMNMapsMushroomKingdomSprite,	&llMNMapsRandomSmallSprite
#endif
	};
	s32 x;
	s32 i;

#ifdef SSB64_NETMENU
	for (i = 0; i < 10; i++)
		sMNVSNetLevelPrefsMapsIconSlotSObjs[i] = NULL;
#endif
	gobj = gcMakeGObjSPAfter(0, NULL, 3, GOBJ_PRIORITY_DEFAULT);
#ifdef SSB64_NETMENU
	gcAddGObjDisplay(gobj, mnVSNetLevelPrefsMapsIconsProcDisplay, 1, GOBJ_PRIORITY_DEFAULT, ~0);
#else
	gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 1, GOBJ_PRIORITY_DEFAULT, ~0);
#endif

	for (i = 0; i < ARRAY_COUNT(offsets); i++)
	{
		if (mnVSNetLevelPrefsMapsCheckLocked(mnVSNetLevelPrefsMapsGetGroundKind(i)) == FALSE)
		{
			x = i * 50;

			if (i == 9)
			{
#ifdef PORT
				sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], llMNMapsRandomSmallSprite));
#else
				sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], &llMNMapsRandomSmallSprite));
#endif
			}
			else sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], offsets[mnVSNetLevelPrefsMapsGetGroundKind(i)]));

#ifdef SSB64_NETMENU
			/* SP_FASTCOPY uses G_CYC_COPY; prim × texel modulation for ban tints
			 * is ignored — match preview/name sprites (1-cycle + transparent). */
			sobj->sprite.attr &= ~SP_FASTCOPY;
			sobj->sprite.attr |= SP_TRANSPARENT;
			sMNVSNetLevelPrefsMapsIconSlotSObjs[i] = sobj;
#endif
			if (i < 5)
			{
				sobj->pos.y = 30.0F;
				sobj->pos.x = x + 30;
			}
			else
			{
				sobj->pos.y = 68.0F;
				sobj->pos.x = x - 220;
			}
		}
	}
#ifdef SSB64_NETMENU
	mnVSNetLevelPrefsMapsRefreshIconBanTints();
#endif
}
// 0x801326DC
void mnVSNetLevelPrefsMapsSetNamePosition(SObj *sobj, s32 gkind)
{
	Vec2f positions[/* */] =
	{
		{ 195.0F, 196.0F },
		{ 202.0F, 196.0F },
		{ 190.0F, 196.0F },
		{ 195.0F, 196.0F },
		{ 198.0F, 196.0F },
		{ 190.0F, 196.0F },
		{ 195.0F, 196.0F },
		{ 190.0F, 196.0F },
		{ 190.0F, 196.0F }
	};
#if defined(REGION_US)
	sobj->pos.x = 183.0F;
	sobj->pos.y = 196.0F;
#else
	sobj->pos.x = positions[gkind].x;
	sobj->pos.y = positions[gkind].y;
#endif
}

// 0x80132738
void mnVSNetLevelPrefsMapsMakeName(GObj *gobj, s32 gkind)
{
	SObj* sobj;
	intptr_t offsets[/* */] =
	{
#ifdef PORT
		llMNMapsPeachsCastleTextSprite,
		llMNMapsSectorZTextSprite,
		llMNMapsCongoJungleTextSprite,
		llMNMapsPlanetZebesTextSprite,
		llMNMapsHyruleCastleTextSprite,
		llMNMapsYoshisIslandTextSprite,
		llMNMapsDreamLandTextSprite,
		llMNMapsSaffronCityTextSprite,
		llMNMapsMushroomKingdomTextSprite
#else
		&llMNMapsPeachsCastleTextSprite,
		&llMNMapsSectorZTextSprite,
		&llMNMapsCongoJungleTextSprite,
		&llMNMapsPlanetZebesTextSprite,
		&llMNMapsHyruleCastleTextSprite,
		&llMNMapsYoshisIslandTextSprite,
		&llMNMapsDreamLandTextSprite,
		&llMNMapsSaffronCityTextSprite,
		&llMNMapsMushroomKingdomTextSprite
#endif
	};

	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], offsets[gkind]));
	mnVSNetLevelPrefsMapsSetNamePosition(sobj, gkind);

	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;

	sobj->sprite.red = 0x00;
	sobj->sprite.green = 0x00;
	sobj->sprite.blue = 0x00;
}

#if defined(REGION_US)
// 0x80134700
char *dMNVSNetLevelPrefsMapsSubtitles[/* */] =
{
	"IN THE SKY OF",
	"SECTOR Z",
	"CONGO JUNGLE",
	"PLANET ZEBES",
	"CASTLE OF HYRULE",
	"YOSHI'S ISLAND",
	"PUPUPU LAND",
	"YAMABUKI CITY",
	"CLASSIC MUSHROOM"
};
char *dMNVSNetLevelPrefsMapsSubtitles2[/* */] =
{
	"CASTLE PEACH",
	"ABOARD A GREAT FOX",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"KINGDOM"
};

// 0x80134748
Vec2f dMNVSNetLevelPrefsMapsSubtitlePositions[/* */] =
{
	{ 192.0F, 167.0F },
	{ 214.0F, 167.0F },
	{ 202.0F, 169.0F },
	{ 202.0F, 169.0F },
	{ 193.0F, 169.0F },
	{ 198.0F, 169.0F },
	{ 205.0F, 169.0F },
	{ 199.0F, 169.0F },
	{ 191.0F, 167.0F }
};
Vec2f dMNVSNetLevelPrefsMapsSubtitlePositions2[/* */] =
{
	{ 209.0F, 174.0F },
	{ 188.0F, 174.0F },
	{   0.0F,   0.0F },
	{   0.0F,   0.0F },
	{   0.0F,   0.0F },
	{   0.0F,   0.0F },
	{   0.0F,   0.0F },
	{ 203.0F, 174.0F }, 
	{ 213.0F, 174.0 }
};

// 0x801347D8
u32 dMNVSNetLevelPrefsMapsSubtitleColors[/* */] = { 255, 255, 255 };

// 0x801327E0 - Unused?
void mnVSNetLevelPrefsMapsSubtitleHasExtraLine(void)
{
	return;
}

// 0x801327E8 - Unused?
void mnVSNetLevelPrefsMapsMakeSubtitle(void)
{
	return;
}
#else
// 0x801327E0 - Unused?
sb32 mnVSNetLevelPrefsMapsSubtitleHasExtraLine(s32 gkind)
{
	switch (gkind)
	{
		case nGRKindCastle:
		case nGRKindSector:
		case nGRKindInishie:
			return TRUE;
		default:
			return FALSE;
	}
}

// 0x801327E8 - Unused?
void mnVSNetLevelPrefsMapsMakeSubtitle(GObj *gobj, s32 gkind) {
    char *dMNVSNetLevelPrefsMapsSubtitles[/* */] =
    {
    	"IN THE SKY OF",
    	"SECTOR Z",
    	"CONGO JUNGLE",
    	"PLANET ZEBES",
    	"CASTLE OF HYRULE",
    	"YOSHI'S ISLAND",
    	"PUPUPU LAND",
    	"YAMABUKI CITY",
    	"CLASSIC MUSHROOM"
    };
    char *dMNVSNetLevelPrefsMapsSubtitles2[/* */] =
    {
    	"CASTLE PEACH",
    	"ABOARD A GREAT FOX",
    	NULL,
    	NULL,
    	NULL,
    	NULL,
    	NULL,
    	NULL,
    	"KINGDOM"
    };

    Vec2f dMNVSNetLevelPrefsMapsSubtitlePositions[/* */] =
    {
        { 192.0F, 167.0F },
        { 214.0F, 167.0F },
        { 202.0F, 169.0F },
        { 202.0F, 169.0F },
        { 193.0F, 169.0F },
        { 198.0F, 169.0F },
        { 205.0F, 169.0F },
        { 199.0F, 169.0F },
        { 191.0F, 167.0F }
    };
    Vec2f dMNVSNetLevelPrefsMapsSubtitlePositions2[/* */] =
    {
        { 209.0F, 174.0F },
        { 188.0F, 174.0F },
        {   0.0F,   0.0F },
        {   0.0F,   0.0F },
        {   0.0F,   0.0F },
        {   0.0F,   0.0F },
        {   0.0F,   0.0F },
        { 203.0F, 174.0F }, 
        { 213.0F, 174.0 }
    };

    u32 dMNVSNetLevelPrefsMapsSubtitleColors[/* */] = { 255, 255, 255 };

    mnVSNetLevelPrefsMapsMakeString(gobj, dMNVSNetLevelPrefsMapsSubtitles[gkind], dMNVSNetLevelPrefsMapsSubtitlePositions[gkind].x, dMNVSNetLevelPrefsMapsSubtitlePositions[gkind].y, dMNVSNetLevelPrefsMapsSubtitleColors);
    
    if (mnVSNetLevelPrefsMapsSubtitleHasExtraLine(gkind) != FALSE) 
    {
        mnVSNetLevelPrefsMapsMakeString(gobj, dMNVSNetLevelPrefsMapsSubtitles2[gkind], dMNVSNetLevelPrefsMapsSubtitlePositions2[gkind].x, dMNVSNetLevelPrefsMapsSubtitlePositions2[gkind].y, dMNVSNetLevelPrefsMapsSubtitleColors);
    }
}
#endif

// 0x801327F0
void mnVSNetLevelPrefsMapsSetLogoPosition(GObj *gobj, s32 gkind)
{
	Vec2f positions[/* */] =
	{
		{ 3.0F, 19.0F },
		{ 3.0F, 19.0F },
		{ 3.0F, 20.0F },
		{ 2.0F, 20.0F },
		{ 3.0F, 17.0F },
		{-1.0F, 19.0F },
		{ 1.0F, 20.0F },
		{ 1.0F, 20.0F },
		{ 3.0F, 19.0F },
		{34.0F, 20.0F }
	};

	if (gkind == 0xDE)
	{
		SObjGetStruct(gobj)->pos.x = 223.0F;
		SObjGetStruct(gobj)->pos.y = 144.0F;
	}
	else
	{
		SObjGetStruct(gobj)->pos.x = positions[gkind].x + 189.0F;
		SObjGetStruct(gobj)->pos.y = positions[gkind].y + 124.0F;
	}
}

// 0x801328A8
void mnVSNetLevelPrefsMapsMakeEmblem(GObj *gobj, s32 gkind)
{
	SObj *sobj;

	intptr_t offsets[/* */] =
	{
#ifdef PORT
		llFTEmblemSpritesMarioSprite,	llFTEmblemSpritesFoxSprite,
		llFTEmblemSpritesDonkeySprite, 	llFTEmblemSpritesMetroidSprite,
		llFTEmblemSpritesZeldaSprite, 	llFTEmblemSpritesYoshiSprite,
		llFTEmblemSpritesKirbySprite, 	llFTEmblemSpritesPMonstersSprite,
		llFTEmblemSpritesMarioSprite
#else
		&llFTEmblemSpritesMarioSprite,	&llFTEmblemSpritesFoxSprite,
		&llFTEmblemSpritesDonkeySprite, 	&llFTEmblemSpritesMetroidSprite,
		&llFTEmblemSpritesZeldaSprite, 	&llFTEmblemSpritesYoshiSprite,
		&llFTEmblemSpritesKirbySprite, 	&llFTEmblemSpritesPMonstersSprite,
		&llFTEmblemSpritesMarioSprite
#endif
	};

	if (gkind == 0xDE)
	{
#ifdef PORT
		sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], llMNMapsQuestionMarkSprite));
#else
		sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], &llMNMapsQuestionMarkSprite));
#endif

		sobj->sprite.attr &= ~SP_FASTCOPY;
		sobj->sprite.attr |= SP_TRANSPARENT;

		sobj->sprite.red = 0x5C;
		sobj->sprite.green = 0x22;
		sobj->sprite.blue = 0x00;
	}
	else
	{
		sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[0], offsets[gkind]));
		sobj->sprite.attr &= ~SP_FASTCOPY;
		sobj->sprite.attr |= SP_TRANSPARENT;

		sobj->sprite.red = 0x5C;
		sobj->sprite.green = 0x22;
		sobj->sprite.blue = 0x00;
	}
	mnVSNetLevelPrefsMapsSetLogoPosition(gobj, gkind);
}

// 0x801329AC
void mnVSNetLevelPrefsMapsMakeNameAndEmblem(s32 slot)
{
	GObj *gobj;

	if (sMNVSNetLevelPrefsMapsNameLogoGObj != NULL)
	{
		gcEjectGObj(sMNVSNetLevelPrefsMapsNameLogoGObj);
	}
	gobj = gcMakeGObjSPAfter(0, NULL, 4, GOBJ_PRIORITY_DEFAULT);
	sMNVSNetLevelPrefsMapsNameLogoGObj = gobj;
	gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 2, GOBJ_PRIORITY_DEFAULT, ~0);
	mnVSNetLevelPrefsMapsMakeEmblem(sMNVSNetLevelPrefsMapsNameLogoGObj, mnVSNetLevelPrefsMapsGetGroundKind(slot));

	if (slot != 9)
	{
		mnVSNetLevelPrefsMapsMakeName(sMNVSNetLevelPrefsMapsNameLogoGObj, mnVSNetLevelPrefsMapsGetGroundKind(slot));
#if defined(REGION_JP)
		mnVSNetLevelPrefsMapsMakeSubtitle(sMNVSNetLevelPrefsMapsNameLogoGObj, mnVSNetLevelPrefsMapsGetGroundKind(slot));
#endif
	}
}

// 0x80132A58
void mnVSNetLevelPrefsMapsSetCursorPosition(GObj *gobj, s32 slot)
{
	if (slot < 5)
	{
		SObjGetStruct(gobj)->pos.x = (slot * 50) + 23;
		SObjGetStruct(gobj)->pos.y = 23.0F;
	}
	else
	{
		SObjGetStruct(gobj)->pos.x = (slot * 50) - 250 + 23;
		SObjGetStruct(gobj)->pos.y = 61.0F;
	}
}

// 0x80132ADC
void mnVSNetLevelPrefsMapsMakeCursor(void)
{
	GObj *gobj;
	SObj *sobj;

	sMNVSNetLevelPrefsMapsCursorGObj = gobj = gcMakeGObjSPAfter(0, NULL, 7, GOBJ_PRIORITY_DEFAULT);
	gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 5, GOBJ_PRIORITY_DEFAULT, ~0);

#ifdef PORT
	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], llMNMapsCursorSprite));
#else
	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], &llMNMapsCursorSprite));
#endif
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;

	sobj->sprite.red = 0xFF;
	sobj->sprite.green = 0x00;
	sobj->sprite.blue = 0x00;

	mnVSNetLevelPrefsMapsSetCursorPosition(gobj, sMNVSNetLevelPrefsMapsCursorSlot);
}

// 0x80132B84
void mnVSNetLevelPrefsMapsLoadMapFile(s32 gkind, void *heap)
{
	sMNVSNetLevelPrefsMapsGroundInfo = lbRelocGetFileData
	(
		MPGroundData*,
		lbRelocGetForceExternHeapFile
		(
			dMNVSNetLevelPrefsMapsFileInfos[gkind].file_id,
			heap
		),
		dMNVSNetLevelPrefsMapsFileInfos[gkind].offset
	);
#ifdef PORT
	mpCollisionFixGroundDataLayout(sMNVSNetLevelPrefsMapsGroundInfo);
#endif
}

// 0x80132BC8
void mnVSNetLevelPrefsMapsPreviewWallpaperProcDisplay(GObj *gobj)
{
	gDPPipeSync(gSYTaskmanDLHeads[0]++);
	gDPSetCycleType(gSYTaskmanDLHeads[0]++, G_CYC_1CYCLE);
	gDPSetPrimColor(gSYTaskmanDLHeads[0]++, 0, 0, 0x57, 0x60, 0x88, 0xFF);
	gDPSetCombineMode(gSYTaskmanDLHeads[0]++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
	gDPSetRenderMode(gSYTaskmanDLHeads[0]++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
	gDPSetPrimColor(gSYTaskmanDLHeads[0]++, 0, 0, 0x00, 0x00, 0x00, 0x73);
	gDPFillRectangle(gSYTaskmanDLHeads[0]++, 43, 130, 152, 211);
	gDPPipeSync(gSYTaskmanDLHeads[0]++);
	gDPSetRenderMode(gSYTaskmanDLHeads[0]++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
	gDPSetCycleType(gSYTaskmanDLHeads[0]++, G_CYC_1CYCLE);

	lbCommonClearExternSpriteParams();
	lbCommonDrawSObjChainDecalAsPrimMultiply(gobj);
}

// 0x80132D2C
GObj* mnVSNetLevelPrefsMapsMakePreviewWallpaper(s32 gkind)
{
	GObj *gobj;
	SObj *sobj;
	s32 x;

	gobj = gcMakeGObjSPAfter(0, NULL, 9, GOBJ_PRIORITY_DEFAULT);
	gcAddGObjDisplay(gobj, mnVSNetLevelPrefsMapsPreviewWallpaperProcDisplay, 7, GOBJ_PRIORITY_DEFAULT, ~0);

	// draw patterned bg
	for (x = 43; x < 155; x += 16)
	{
#ifdef PORT
		sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], llMNMapsTilesSprite));
#else
		sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], &llMNMapsTilesSprite));
#endif
		sobj->pos.x = x;
		sobj->pos.y = 130.0F;

		continue;
	}
	// Check if Random
	if (gkind == 0xDE)
	{
		// If Random, use Random image
#ifdef PORT
		sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], llMNMapsRandomBigSprite));
#else
		sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetLevelPrefsMapsFiles[2], &llMNMapsRandomBigSprite));
#endif
		sobj->pos.x = 40.0F;
		sobj->pos.y = 127.0F;
	}
	else
	{
		// If not Random, check if Training Mode
		if (sMNVSNetLevelPrefsMapsIsTrainingMode == TRUE)
		{
			/* PORT: hoist the #ifdef-PORT conditional values out of the
			 * lbRelocGetFileData macro argument list. MSVC's preprocessor
			 * (both legacy and /Zc:preprocessor) rejects `#` directives
			 * inside function-like macro arg lists with C2059 (the C
			 * standard says it's undefined behavior; clang/gcc accept
			 * it leniently). */
#ifdef PORT
			void *training_wallpaper_arg =
				(void*) ((uintptr_t)PORT_RESOLVE(sMNVSNetLevelPrefsMapsGroundInfo->wallpaper) - dMNVSNetLevelPrefsMapsWallpaperOffsets[gkind]);
			intptr_t training_blue_offset = llGRWallpaperTrainingBlueSprite;
#else
			void *training_wallpaper_arg =
				(void*) ((uintptr_t)sMNVSNetLevelPrefsMapsGroundInfo->wallpaper - dMNVSNetLevelPrefsMapsWallpaperOffsets[gkind]);
			intptr_t training_blue_offset = (intptr_t)&llGRWallpaperTrainingBlueSprite;
#endif
			// If Training Mode, use Smash logo bg
			sobj = lbCommonMakeSObjForGObj
			(
				gobj,
				lbRelocGetFileData
				(
					Sprite*,
					lbRelocGetForceExternHeapFile
					(
						dMNVSNetLevelPrefsMapsTrainingModeFileInfos[dMNVSNetLevelPrefsMapsTrainingModeWallpaperIDs[gkind]].file_id,
						training_wallpaper_arg
					),
					training_blue_offset
				)
			);
		}
#ifdef PORT
		else sobj = lbCommonMakeSObjForGObj(gobj, (Sprite*)PORT_RESOLVE(sMNVSNetLevelPrefsMapsGroundInfo->wallpaper)); // Use stage bg
#else
		else sobj = lbCommonMakeSObjForGObj(gobj, sMNVSNetLevelPrefsMapsGroundInfo->wallpaper); // Use stage bg
#endif
		
		sobj->sprite.attr &= ~SP_FASTCOPY;

		sobj->sprite.scalex = 0.37F;
		sobj->sprite.scaley = 0.37F;

		sobj->pos.x = 40.0F;
		sobj->pos.y = 127.0F;
	}
	return gobj;
}

// 0x80132EF0
void mnVSNetLevelPrefsMapsModelPriProcDisplay(GObj *gobj)
{
	gDPPipeSync(gSYTaskmanDLHeads[0]++);
	gSPSetGeometryMode(gSYTaskmanDLHeads[0]++, G_ZBUFFER);
	gDPSetRenderMode(gSYTaskmanDLHeads[0]++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);

#ifdef SSB64_NETMENU
	if (mnVSNetLevelPrefsMapsIsEffectiveBanForActivePreview() != FALSE)
	{
		gSPSetLights1(gSYTaskmanDLHeads[0]++, dMNVSNetLevelPrefsMapsLightsGrey);
	}
	else
	{
		gSPSetLights1(gSYTaskmanDLHeads[0]++, dMNVSNetLevelPrefsMapsLights1);
	}
#else
	gSPSetLights1(gSYTaskmanDLHeads[0]++, dMNVSNetLevelPrefsMapsLights1);
#endif

	gcDrawDObjTreeForGObj(gobj);
}

// 0x80132F70
void mnVSNetLevelPrefsMapsModelSecProcDisplay(GObj *gobj)
{
	gDPPipeSync(gSYTaskmanDLHeads[0]++);
	gSPSetGeometryMode(gSYTaskmanDLHeads[0]++, G_ZBUFFER);
	gDPSetRenderMode(gSYTaskmanDLHeads[0]++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
#ifdef SSB64_NETMENU
	if (mnVSNetLevelPrefsMapsIsEffectiveBanForActivePreview() != FALSE)
	{
		gSPSetLights1(gSYTaskmanDLHeads[0]++, dMNVSNetLevelPrefsMapsLightsGrey);
		gSPSetLights1(gSYTaskmanDLHeads[1]++, dMNVSNetLevelPrefsMapsLightsGrey);
	}
	else
	{
		gSPSetLights1(gSYTaskmanDLHeads[0]++, dMNVSNetLevelPrefsMapsLights1);
		gSPSetLights1(gSYTaskmanDLHeads[1]++, dMNVSNetLevelPrefsMapsLights1);
	}
#else
	gSPSetLights1(gSYTaskmanDLHeads[0]++, dMNVSNetLevelPrefsMapsLights1);
	gSPSetLights1(gSYTaskmanDLHeads[1]++, dMNVSNetLevelPrefsMapsLights1);
#endif

	gDPPipeSync(gSYTaskmanDLHeads[1]++);
	gSPSetGeometryMode(gSYTaskmanDLHeads[1]++, G_ZBUFFER);
	gDPSetRenderMode(gSYTaskmanDLHeads[1]++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);


	gcDrawDObjTreeDLLinksForGObj(gobj);
}

// 0x8013303C
GObj* mnVSNetLevelPrefsMapsMakeLayer(s32 gkind, MPGroundData *ground_data, MPGroundDesc *ground_desc, s32 id)
{
	GObj *gobj;
	f32 scales[/* */] =
	{
		0.5F, 0.2F, 0.6F,
		0.5F, 0.3F, 0.6F,
		0.5F, 0.4F, 0.2F
	};

	if (ground_desc->dobjdesc == NULL)
	{
		return NULL;
	}
	gobj = gcMakeGObjSPAfter(0, NULL, 5, GOBJ_PRIORITY_DEFAULT);
	gcAddGObjDisplay(gobj, (ground_data->layer_mask & (1 << id)) ? mnVSNetLevelPrefsMapsModelSecProcDisplay : mnVSNetLevelPrefsMapsModelPriProcDisplay, 3, GOBJ_PRIORITY_DEFAULT, ~0);
#ifdef PORT
	gcSetupCustomDObjs(gobj, (DObjDesc*)PORT_RESOLVE(ground_desc->dobjdesc), NULL, nGCMatrixKindTraRotRpyRSca, nGCMatrixKindNull, nGCMatrixKindNull);
#else
	gcSetupCustomDObjs(gobj, ground_desc->dobjdesc, NULL, nGCMatrixKindTraRotRpyRSca, nGCMatrixKindNull, nGCMatrixKindNull);
#endif

	if (ground_desc->p_mobjsubs != NULL)
	{
#ifdef PORT
		gcAddMObjAll(gobj, (MObjSub***)PORT_RESOLVE(ground_desc->p_mobjsubs));
#else
		gcAddMObjAll(gobj, ground_desc->p_mobjsubs);
#endif
	}
	if ((ground_desc->anim_joints != NULL) || (ground_desc->p_matanim_joints != NULL))
	{
#ifdef PORT
		gcAddAnimAll(gobj, (AObjEvent32**)PORT_RESOLVE(ground_desc->anim_joints), (AObjEvent32***)PORT_RESOLVE(ground_desc->p_matanim_joints), 0.0F);
#else
		gcAddAnimAll(gobj, ground_desc->anim_joints, ground_desc->p_matanim_joints, 0.0F);
#endif
		gcPlayAnimAll(gobj);
	}
	DObjGetStruct(gobj)->scale.vec.f.x = scales[gkind];
	DObjGetStruct(gobj)->scale.vec.f.y = scales[gkind];
	DObjGetStruct(gobj)->scale.vec.f.z = scales[gkind];

	return gobj;
}

// 0x801331AC
void mnVSNetLevelPrefsMapsMakeModel(s32 gkind, MPGroundData *ground_data, s32 heap_id)
{
	DObj *root_dobj, *next_dobj;
	GObj **gobjs = (heap_id == 0) ? sMNVSNetLevelPrefsMapsHeap1LayerGObjs : sMNVSNetLevelPrefsMapsHeap0LayerGObjs;
	s32 i;

	gobjs[0] = mnVSNetLevelPrefsMapsMakeLayer(gkind, ground_data, &ground_data->gr_desc[0], 0);
	gobjs[1] = mnVSNetLevelPrefsMapsMakeLayer(gkind, ground_data, &ground_data->gr_desc[1], 1);
	gobjs[2] = mnVSNetLevelPrefsMapsMakeLayer(gkind, ground_data, &ground_data->gr_desc[2], 2);
	gobjs[3] = mnVSNetLevelPrefsMapsMakeLayer(gkind, ground_data, &ground_data->gr_desc[3], 3);

	if (gkind == nGRKindYamabuki)
	{
		DObjGetChild(DObjGetChild(DObjGetStruct(gobjs[3])))->flags = DOBJ_FLAG_HIDDEN;
	}
	if (gkind == nGRKindYoster)
	{
		for
		(
			next_dobj = root_dobj = DObjGetStruct(gobjs[0]), i = 1;
			next_dobj != NULL;
			next_dobj = lbCommonGetTreeDObjNextFromRoot(next_dobj, root_dobj), i++
		)
		{
			if ((i == 0xF) || (i == 0x11))
			{
				next_dobj->flags = DOBJ_FLAG_HIDDEN;
			}
		}
	}
}

// 0x801332DC
void mnVSNetLevelPrefsMapsDestroyPreview(s32 heap_id)
{
	s32 i;

	if (heap_id == 0)
	{
		if (sMNVSNetLevelPrefsMapsHeap0WallpaperGObj != NULL)
		{
			gcEjectGObj(sMNVSNetLevelPrefsMapsHeap0WallpaperGObj);
			sMNVSNetLevelPrefsMapsHeap0WallpaperGObj = NULL;
		}
		for (i = 0; i < ARRAY_COUNT(sMNVSNetLevelPrefsMapsHeap0LayerGObjs); i++)
		{
			if (sMNVSNetLevelPrefsMapsHeap0LayerGObjs[i] != NULL)
			{
				gcEjectGObj(sMNVSNetLevelPrefsMapsHeap0LayerGObjs[i]);
				sMNVSNetLevelPrefsMapsHeap0LayerGObjs[i] = NULL;
			}
		}
	}
	else
	{
		if (sMNVSNetLevelPrefsMapsHeap1WallpaperGObj != NULL)
		{
			gcEjectGObj(sMNVSNetLevelPrefsMapsHeap1WallpaperGObj);
			sMNVSNetLevelPrefsMapsHeap1WallpaperGObj = NULL;
		}
		for (i = 0; i < ARRAY_COUNT(sMNVSNetLevelPrefsMapsHeap1LayerGObjs); i++)
		{
			if (sMNVSNetLevelPrefsMapsHeap1LayerGObjs[i] != NULL)
			{
				gcEjectGObj(sMNVSNetLevelPrefsMapsHeap1LayerGObjs[i]);
				sMNVSNetLevelPrefsMapsHeap1LayerGObjs[i] = NULL;
			}
		}
	}
}

// 0x801333B4
void mnVSNetLevelPrefsMapsMakePreview(s32 gkind)
{
#ifdef SSB64_NETMENU
	sMNVSNetLevelPrefsMapsActivePreviewGkind = gkind;
#endif
	if (gkind != 0xDE)
	{
		if (sMNVSNetLevelPrefsMapsHeapID == 0)
		{
			mnVSNetLevelPrefsMapsLoadMapFile(gkind, sMNVSNetLevelPrefsMapsModelHeap1);
		}
		else mnVSNetLevelPrefsMapsLoadMapFile(gkind, sMNVSNetLevelPrefsMapsModelHeap0);
	}
	if (sMNVSNetLevelPrefsMapsHeapID == 0)
	{
		sMNVSNetLevelPrefsMapsHeap1WallpaperGObj = mnVSNetLevelPrefsMapsMakePreviewWallpaper(gkind);
#ifdef SSB64_NETMENU
		mnVSNetLevelPrefsMapsTintWallpaperChain(
			sMNVSNetLevelPrefsMapsHeap1WallpaperGObj,
			mnVSNetLevelPrefsMapsIsEffectiveBanForActivePreview());
#endif
	}
	else
	{
		sMNVSNetLevelPrefsMapsHeap0WallpaperGObj = mnVSNetLevelPrefsMapsMakePreviewWallpaper(gkind);
#ifdef SSB64_NETMENU
		mnVSNetLevelPrefsMapsTintWallpaperChain(
			sMNVSNetLevelPrefsMapsHeap0WallpaperGObj,
			mnVSNetLevelPrefsMapsIsEffectiveBanForActivePreview());
#endif
	}

	if (gkind != 0xDE)
	{
#ifdef SSB64_NETMENU
		if (mnVSNetLevelPrefsMapsIsEffectiveBanForActivePreview() != FALSE)
		{
			s32 hi;
			GObj **incoming_layers =
				(sMNVSNetLevelPrefsMapsHeapID == 0) ?
					sMNVSNetLevelPrefsMapsHeap1LayerGObjs :
					sMNVSNetLevelPrefsMapsHeap0LayerGObjs;

			for (hi = 0; hi < 4; hi++)
			{
				if (incoming_layers[hi] != NULL)
				{
					gcEjectGObj(incoming_layers[hi]);
					incoming_layers[hi] = NULL;
				}
			}
			mnVSNetLevelPrefsMapsSetPreviewCameraPosition(sMNVSNetLevelPrefsMapsPreviewCObj, gkind);
		}
		else
#endif
		{
			mnVSNetLevelPrefsMapsMakeModel(gkind, sMNVSNetLevelPrefsMapsGroundInfo, sMNVSNetLevelPrefsMapsHeapID);
			mnVSNetLevelPrefsMapsSetPreviewCameraPosition(sMNVSNetLevelPrefsMapsPreviewCObj, gkind);
		}
	}
	mnVSNetLevelPrefsMapsDestroyPreview(sMNVSNetLevelPrefsMapsHeapID);

	sMNVSNetLevelPrefsMapsHeapID = (sMNVSNetLevelPrefsMapsHeapID == 0) ? 1 : 0;
}

// 0x801334AC
void mnVSNetLevelPrefsMapsMakeWallpaperCamera(void)
{
	GObj *gobj = gcMakeCameraGObj
	(
		1,
		NULL,
		1,
		GOBJ_PRIORITY_DEFAULT,
		lbCommonDrawSprite,
		80,
		COBJ_MASK_DLLINK(0),
		~0,
		FALSE,
		nGCProcessKindFunc,
		NULL,
		1,
		FALSE
	);
	CObj *cobj = CObjGetStruct(gobj);
	syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);
}

// 0x8013354C
void mnVSNetLevelPrefsMapsMakePlaqueCamera(void)
{
	GObj *gobj = gcMakeCameraGObj
	(
		1,
		NULL,
		1,
		GOBJ_PRIORITY_DEFAULT,
		lbCommonDrawSprite,
		40,
		COBJ_MASK_DLLINK(6),
		~0,
		FALSE,
		nGCProcessKindFunc,
		NULL,
		1,
		FALSE
	);
	CObj *cobj = CObjGetStruct(gobj);
	syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);
}

// 0x801335EC
void mnVSNetLevelPrefsMapsMakePreviewWallpaperCamera(void)
{
	GObj *gobj = gcMakeCameraGObj
	(
		1,
		NULL,
		1,
		GOBJ_PRIORITY_DEFAULT,
		lbCommonDrawSprite,
		70,
		COBJ_MASK_DLLINK(7),
		~0,
		FALSE,
		nGCProcessKindFunc,
		NULL,
		1,
		FALSE
	);
	CObj *cobj = CObjGetStruct(gobj);
	syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);
}

// 0x8013368C
void mnVSNetLevelPrefsMapsMakeLabelsViewport(void)
{
	GObj *gobj = gcMakeCameraGObj
	(
		1,
		NULL,
		1,
		GOBJ_PRIORITY_DEFAULT,
		lbCommonDrawSprite,
		30,
		COBJ_MASK_DLLINK(4),
		~0,
		FALSE,
		nGCProcessKindFunc,
		NULL,
		1,
		FALSE
	);
	CObj *cobj = CObjGetStruct(gobj);
	syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);
}

// 0x8013372C
void mnVSNetLevelPrefsMapsMakeIconsCamera(void)
{
	GObj *gobj = gcMakeCameraGObj
	(
		1,
		NULL,
		1,
		GOBJ_PRIORITY_DEFAULT,
		lbCommonDrawSprite,
		60,
		COBJ_MASK_DLLINK(1),
		~0,
		FALSE,
		nGCProcessKindFunc,
		NULL,
		1,
		FALSE
	);
	CObj *cobj = CObjGetStruct(gobj);
	syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);
}

// 0x801337CC
void mnVSNetLevelPrefsMapsMakeNameAndEmblemCamera(void)
{
	GObj *gobj = gcMakeCameraGObj
	(
		1,
		NULL,
		1,
		GOBJ_PRIORITY_DEFAULT,
		lbCommonDrawSprite,
		20,
		COBJ_MASK_DLLINK(2),
		~0,
		FALSE,
		nGCProcessKindFunc,
		NULL,
		1,
		FALSE
	);
	CObj *cobj = CObjGetStruct(gobj);
	syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);
}

// 0x8013386C
void mnVSNetLevelPrefsMapsMakeCursorCamera(void)
{
	GObj *gobj = gcMakeCameraGObj
	(
		1,
		NULL,
		1,
		GOBJ_PRIORITY_DEFAULT,
		lbCommonDrawSprite,
		50,
		COBJ_MASK_DLLINK(5),
		~0,
		FALSE,
		nGCProcessKindFunc,
		NULL,
		1,
		FALSE
	);
	CObj *cobj = CObjGetStruct(gobj);
	syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);
}

// 0x8013390C
void mnVSNetLevelPrefsMapsSetPreviewCameraPosition(CObj *cobj, s32 gkind)
{
	Vec3f positions[/* */] =
	{
		{ 1700.0F, 1800.0F, 0.0F },
		{ 1600.0F, 1600.0F, 0.0F },
		{ 1600.0F, 1600.0F, 0.0F },
		{ 1600.0F, 1600.0F, 0.0F },
		{ 1600.0F, 1500.0F, 0.0F },
		{ 1600.0F, 1600.0F, 0.0F },
		{ 1600.0F, 1500.0F, 0.0F },
		{ 1600.0F, 1600.0F, 0.0F },
		{ 1200.0F, 1600.0F, 0.0F },
	};

	if (gkind == 0xDE)
	{
		gkind = nGRKindCastle;
	}
	cobj->vec.eye.x = -3000.0F;
	cobj->vec.eye.y = 3000.0F;
	cobj->vec.eye.z = 9000.0F;
	cobj->vec.up.x = 0.0F;
	cobj->vec.up.y = 1.0F;
	cobj->vec.up.z = 0.0F;
	cobj->vec.at.x = positions[gkind].x;
	cobj->vec.at.y = positions[gkind].y;
	cobj->vec.at.z = positions[gkind].z;
}

// 0x801339C4
void mnVSNetLevelPrefsMapsPreviewCameraThreadUpdate(GObj *gobj)
{
	CObj* cobj = CObjGetStruct(gobj);
	f32 y = cobj->vec.at.y;
	f32 deg = 0.0F;

	while (TRUE)
	{
		cobj->vec.at.y = __sinf(F_CLC_DTOR32(deg)) * 40.0F + y;

		deg = (deg + 2.0F > 360.0F) ? deg + 2.0F - 360.0F : deg + 2.0F;

		gcSleepCurrentGObjThread(1);
	}
}

// 0x80133A88
void mnVSNetLevelPrefsMapsMakePreviewCamera(void)
{
	s32 unused;
	GObj *gobj = gcMakeCameraGObj
	(
		1,
		NULL,
		1,
		GOBJ_PRIORITY_DEFAULT,
		func_80017DBC,
		65,
		COBJ_MASK_DLLINK(3),
		~0,
		TRUE,
		nGCProcessKindThread,
		NULL,
		1,
		FALSE
	);
	CObj *cobj = CObjGetStruct(gobj);

	sMNVSNetLevelPrefsMapsPreviewCObj = cobj;

	syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);

	cobj->projection.persp.far = 16384.0F;

	mnVSNetLevelPrefsMapsSetPreviewCameraPosition(cobj, mnVSNetLevelPrefsMapsGetGroundKind(sMNVSNetLevelPrefsMapsCursorSlot));

	gcAddGObjProcess(gobj, mnVSNetLevelPrefsMapsPreviewCameraThreadUpdate, nGCProcessKindThread, 1);
}

// 0x80133B78
void mnVSNetLevelPrefsMapsSaveSceneData(void)
{
	s32 unused[/* */] =
	{
		nGRKindPupupu, 	nGRKindZebes,	nGRKindCastle,
		nGRKindInishie,	nGRKindJungle, 	nGRKindSector,
		nGRKindYoster, 	nGRKindYamabuki,nGRKindHyrule
	};
	s32 gkind;

	if (sMNVSNetLevelPrefsMapsCursorSlot == 9)
	{
		do
		{
			gkind = syUtilsRandTimeUCharRange(9);
		}
		while ((mnVSNetLevelPrefsMapsCheckLocked(gkind) != FALSE) || (gkind == gSCManagerSceneData.gkind));

		gSCManagerSceneData.gkind = gkind;
	}
	else gSCManagerSceneData.gkind = mnVSNetLevelPrefsMapsGetGroundKind(sMNVSNetLevelPrefsMapsCursorSlot);

	if (sMNVSNetLevelPrefsMapsIsTrainingMode == FALSE)
	{
		gSCManagerSceneData.maps_vsmode_gkind = mnVSNetLevelPrefsMapsGetGroundKind(sMNVSNetLevelPrefsMapsCursorSlot);
	}
	if (sMNVSNetLevelPrefsMapsIsTrainingMode == TRUE)
	{
		gSCManagerSceneData.maps_training_gkind = mnVSNetLevelPrefsMapsGetGroundKind(sMNVSNetLevelPrefsMapsCursorSlot);
	}
}

// 0x80133C6C
void mnVSNetLevelPrefsMapsInitVars(void)
{
	s32 i;

	sMNVSNetLevelPrefsMapsNameLogoGObj = NULL;
	sMNVSNetLevelPrefsMapsHeap0WallpaperGObj = NULL;
	sMNVSNetLevelPrefsMapsHeap1WallpaperGObj = NULL;

	for (i = 0; i < ARRAY_COUNT(sMNVSNetLevelPrefsMapsHeap0LayerGObjs); i++)
	{
		sMNVSNetLevelPrefsMapsHeap0LayerGObjs[i] = NULL;
		sMNVSNetLevelPrefsMapsHeap1LayerGObjs[i] = NULL;
	}
	switch (gSCManagerSceneData.scene_prev)
	{
	case nSCKindPlayers1PTraining:
		sMNVSNetLevelPrefsMapsIsTrainingMode = TRUE;
		sMNVSNetLevelPrefsMapsCursorSlot = mnVSNetLevelPrefsMapsGetSlot(gSCManagerSceneData.maps_training_gkind);
		break;
		
	case nSCKindPlayersVS:
	case nSCKindVSOnline:
		sMNVSNetLevelPrefsMapsIsTrainingMode = FALSE;
		sMNVSNetLevelPrefsMapsCursorSlot = mnVSNetLevelPrefsMapsGetSlot(gSCManagerSceneData.maps_vsmode_gkind);
		break;
	}
	sMNVSNetLevelPrefsMapsUnlockedMask = gSCManagerBackupData.unlock_mask;
#ifdef SSB64_NETMENU
	gSCManagerSceneData.vs_net_stage_ban_mask =
		mnVSNetLevelPrefsMapsSanitizeUserBanMask(gSCManagerSceneData.vs_net_stage_ban_mask);
#endif
	sMNVSNetLevelPrefsMapsHeapID = 1;
	sMNVSNetLevelPrefsMapsTotalTimeTics = 0;
	sMNVSNetLevelPrefsMapsReturnTic = sMNVSNetLevelPrefsMapsTotalTimeTics + I_MIN_TO_TICS(5);
}

// 0x80133D60
void mnVSNetLevelPrefsMapsSaveSceneData2(void)
{
	mnVSNetLevelPrefsMapsSaveSceneData();
}

// 0x80133D80
void mnVSNetLevelPrefsMapsFuncRun(GObj *gobj)
{
	s32 unused;
	s32 stick_input;
	s32 button_input;

	sMNVSNetLevelPrefsMapsTotalTimeTics++;

	if (sMNVSNetLevelPrefsMapsTotalTimeTics >= 10)
	{
		if (sMNVSNetLevelPrefsMapsTotalTimeTics == sMNVSNetLevelPrefsMapsReturnTic)
		{
			gSCManagerSceneData.scene_prev = gSCManagerSceneData.scene_curr;
			gSCManagerSceneData.scene_curr = nSCKindTitle;

			mnVSNetLevelPrefsMapsSaveSceneData2();
			syTaskmanSetLoadScene();
			return;
		}
		if (scSubsysControllerCheckNoInputAll() == FALSE)
		{
			sMNVSNetLevelPrefsMapsReturnTic = sMNVSNetLevelPrefsMapsTotalTimeTics + I_MIN_TO_TICS(5);
		}
		if (sMNVSNetLevelPrefsMapsScrollWait != 0)
		{
			sMNVSNetLevelPrefsMapsScrollWait--;
		}
		if
		(
			(scSubsysControllerGetPlayerStickInRangeLR(-20, 20)) &&
			(scSubsysControllerGetPlayerStickInRangeUD(-20, 20)) &&
			(scSubsysControllerGetPlayerHoldButtons(U_JPAD | R_JPAD | R_TRIG | U_CBUTTONS | R_CBUTTONS) == FALSE) && 
			(scSubsysControllerGetPlayerHoldButtons(D_JPAD | L_JPAD | L_TRIG | D_CBUTTONS | L_CBUTTONS) == FALSE)
		)
		{
			sMNVSNetLevelPrefsMapsScrollWait = 0;
		}

		if (scSubsysControllerGetPlayerTapButtons(A_BUTTON))
		{
			mnVSNetLevelPrefsMapsSaveSceneData2();

			if (sMNVSNetLevelPrefsMapsIsTrainingMode != FALSE)
			{
				func_800269C0_275C0(nSYAudioFGMStageSelect);

				gSCManagerSceneData.scene_prev = gSCManagerSceneData.scene_curr;
				gSCManagerSceneData.scene_curr = nSCKind1PTrainingMode;
				syTaskmanSetLoadScene();
			}
			else
			{
#ifdef SSB64_NETMENU
				mnVSNetLevelPrefsMapsTryToggleBanAtCursor();
#else
				func_800269C0_275C0(nSYAudioFGMStageSelect);

				gSCManagerSceneData.scene_prev = gSCManagerSceneData.scene_curr;
				gSCManagerSceneData.scene_curr = nSCKindVSBattle;
				syTaskmanSetLoadScene();
#endif
			}
		}
		if (scSubsysControllerGetPlayerTapButtons(B_BUTTON))
		{
			mnVSNetLevelPrefsMapsSaveSceneData2();

			if (sMNVSNetLevelPrefsMapsIsTrainingMode == TRUE)
			{
				gSCManagerSceneData.scene_prev = gSCManagerSceneData.scene_curr;
				gSCManagerSceneData.scene_curr = nSCKindPlayers1PTraining;
			}
			else
			{
				gSCManagerSceneData.scene_prev = gSCManagerSceneData.scene_curr;
				gSCManagerSceneData.scene_curr = nSCKindVSOnline;
			}
			syTaskmanSetLoadScene();
		}
		if (sMNVSNetLevelPrefsMapsScrollWait == 0)
		{
			button_input = scSubsysControllerGetPlayerHoldButtons(U_JPAD | U_CBUTTONS);

			if ((button_input != 0) || (stick_input = scSubsysControllerGetPlayerStickUD(20, 1), (stick_input != 0)))
			{
				if ((sMNVSNetLevelPrefsMapsCursorSlot >= 5) && (mnVSNetLevelPrefsMapsCheckLocked(mnVSNetLevelPrefsMapsGetGroundKind(sMNVSNetLevelPrefsMapsCursorSlot - 5)) == FALSE))
				{
					func_800269C0_275C0(nSYAudioFGMMenuScroll2);

					sMNVSNetLevelPrefsMapsCursorSlot -= 5;

					mnVSNetLevelPrefsMapsMakeNameAndEmblem(sMNVSNetLevelPrefsMapsCursorSlot);
					mnVSNetLevelPrefsMapsSetCursorPosition(sMNVSNetLevelPrefsMapsCursorGObj, sMNVSNetLevelPrefsMapsCursorSlot);
					mnVSNetLevelPrefsMapsMakePreview(mnVSNetLevelPrefsMapsGetGroundKind(sMNVSNetLevelPrefsMapsCursorSlot));
				}
				if (button_input != 0)
				{
					sMNVSNetLevelPrefsMapsScrollWait = 12;
				}
				else sMNVSNetLevelPrefsMapsScrollWait = (160 - stick_input) / 7;

				return;
			}
			button_input = scSubsysControllerGetPlayerHoldButtons(D_JPAD | D_CBUTTONS);

			if ((button_input != 0) || (stick_input = scSubsysControllerGetPlayerStickUD(-20, 0), (stick_input != 0)))
			{
				if ((sMNVSNetLevelPrefsMapsCursorSlot < 5) && (mnVSNetLevelPrefsMapsCheckLocked(mnVSNetLevelPrefsMapsGetGroundKind(sMNVSNetLevelPrefsMapsCursorSlot + 5)) == FALSE))
				{
					func_800269C0_275C0(nSYAudioFGMMenuScroll2);

					sMNVSNetLevelPrefsMapsCursorSlot += 5;

					mnVSNetLevelPrefsMapsMakeNameAndEmblem(sMNVSNetLevelPrefsMapsCursorSlot);
					mnVSNetLevelPrefsMapsSetCursorPosition(sMNVSNetLevelPrefsMapsCursorGObj, sMNVSNetLevelPrefsMapsCursorSlot);
					mnVSNetLevelPrefsMapsMakePreview(mnVSNetLevelPrefsMapsGetGroundKind(sMNVSNetLevelPrefsMapsCursorSlot));
				}
				if (button_input != 0)
				{
					sMNVSNetLevelPrefsMapsScrollWait = 12;
				}
				else sMNVSNetLevelPrefsMapsScrollWait = (stick_input + 160) / 7;

				return;
			}
			button_input = scSubsysControllerGetPlayerHoldButtons(L_JPAD | L_TRIG | L_CBUTTONS);

			if ((button_input != 0) || (stick_input = scSubsysControllerGetPlayerStickLR(-20, 0), (stick_input)))
			{
				switch (sMNVSNetLevelPrefsMapsCursorSlot)
				{
				case 0:
					sMNVSNetLevelPrefsMapsCursorSlot = (mnVSNetLevelPrefsMapsCheckLocked(mnVSNetLevelPrefsMapsGetGroundKind(4))) ? 3 : 4;
					break;

				case 5:
					sMNVSNetLevelPrefsMapsCursorSlot = 9;
					break;
					
				default:
					sMNVSNetLevelPrefsMapsCursorSlot--;
				}
				func_800269C0_275C0(nSYAudioFGMMenuScroll2);
				mnVSNetLevelPrefsMapsMakeNameAndEmblem(sMNVSNetLevelPrefsMapsCursorSlot);
				mnVSNetLevelPrefsMapsSetCursorPosition(sMNVSNetLevelPrefsMapsCursorGObj, sMNVSNetLevelPrefsMapsCursorSlot);
				mnVSNetLevelPrefsMapsMakePreview(mnVSNetLevelPrefsMapsGetGroundKind(sMNVSNetLevelPrefsMapsCursorSlot));

				if (button_input != 0)
				{
					sMNVSNetLevelPrefsMapsScrollWait = 12;
				}
				else sMNVSNetLevelPrefsMapsScrollWait = (stick_input + 160) / 7;

				return;
			}
			button_input = scSubsysControllerGetPlayerHoldButtons(R_JPAD | R_TRIG | R_CBUTTONS);

			if ((button_input != 0) || (stick_input = scSubsysControllerGetPlayerStickLR(20, 1), (stick_input)))
			{
				switch (sMNVSNetLevelPrefsMapsCursorSlot)
				{
				case 3:
					sMNVSNetLevelPrefsMapsCursorSlot = (mnVSNetLevelPrefsMapsCheckLocked(mnVSNetLevelPrefsMapsGetGroundKind(4))) ? 0 : 4;
					break;
					
				case 4:
					sMNVSNetLevelPrefsMapsCursorSlot = 0;
					break;
					
				case 9:
					sMNVSNetLevelPrefsMapsCursorSlot = 5;
					break;
					
				default:
					sMNVSNetLevelPrefsMapsCursorSlot++;
				}
				func_800269C0_275C0(nSYAudioFGMMenuScroll2);
				mnVSNetLevelPrefsMapsMakeNameAndEmblem(sMNVSNetLevelPrefsMapsCursorSlot);
				mnVSNetLevelPrefsMapsSetCursorPosition(sMNVSNetLevelPrefsMapsCursorGObj, sMNVSNetLevelPrefsMapsCursorSlot);
				mnVSNetLevelPrefsMapsMakePreview(mnVSNetLevelPrefsMapsGetGroundKind(sMNVSNetLevelPrefsMapsCursorSlot));

				if (button_input != 0)
				{
					sMNVSNetLevelPrefsMapsScrollWait = 12;
				}
				else sMNVSNetLevelPrefsMapsScrollWait = (160 - stick_input) / 7;
			}
		}
	}
}

// 0x80134304
void mnVSNetLevelPrefsMapsFuncStart(void)
{
	s32 unused1[2];
	LBRelocSetup rl_setup;
	s32 unused2;

	rl_setup.table_addr = (uintptr_t)&lLBRelocTableAddr;
#ifdef PORT
	rl_setup.table_files_num = (u32)llRelocFileCount;
#else
	rl_setup.table_files_num = (u32)&llRelocFileCount;
#endif
	rl_setup.file_heap = NULL;
	rl_setup.file_heap_size = 0;
	rl_setup.status_buffer = sMNVSNetLevelPrefsMapsStatusBuffer;
	rl_setup.status_buffer_size = ARRAY_COUNT(sMNVSNetLevelPrefsMapsStatusBuffer);
	rl_setup.force_status_buffer = sMNVSNetLevelPrefsMapsForceStatusBuffer;
	rl_setup.force_status_buffer_size = ARRAY_COUNT(sMNVSNetLevelPrefsMapsForceStatusBuffer);

	lbRelocInitSetup(&rl_setup);
	lbRelocLoadFilesListed(dMNVSNetLevelPrefsMapsFileIDs, sMNVSNetLevelPrefsMapsFiles);
	mnVSNetLevelPrefsMapsAllocModelHeaps();

	gcMakeGObjSPAfter(0, mnVSNetLevelPrefsMapsFuncRun, 0, GOBJ_PRIORITY_DEFAULT);
	gcMakeDefaultCameraGObj(1, GOBJ_PRIORITY_DEFAULT, 100, COBJ_FLAG_ZBUFFER, GPACK_RGBA8888(0x00, 0x00, 0x00, 0x00));
	mnVSNetLevelPrefsMapsInitVars();
	mnVSNetLevelPrefsMapsMakeWallpaperCamera();
	mnVSNetLevelPrefsMapsMakeLabelsViewport();
	mnVSNetLevelPrefsMapsMakeIconsCamera();
	mnVSNetLevelPrefsMapsMakeNameAndEmblemCamera();
	mnVSNetLevelPrefsMapsMakeCursorCamera();
	mnVSNetLevelPrefsMapsMakePreviewCamera();
	mnVSNetLevelPrefsMapsMakePlaqueCamera();
	mnVSNetLevelPrefsMapsMakePreviewWallpaperCamera();
	mnVSNetLevelPrefsMapsMakeWallpaper();
	mnVSNetLevelPrefsMapsMakePlaque();
	mnVSNetLevelPrefsMapsMakeLabels();
	mnVSNetLevelPrefsMapsMakeIcons();
	mnVSNetLevelPrefsMapsMakeNameAndEmblem(sMNVSNetLevelPrefsMapsCursorSlot);
	mnVSNetLevelPrefsMapsMakeCursor();
	mnVSNetLevelPrefsMapsMakePreview(mnVSNetLevelPrefsMapsGetGroundKind(sMNVSNetLevelPrefsMapsCursorSlot));
}

// 0x8013490C
SYVideoSetup dMNVSNetLevelPrefsMapsVideoSetup = SYVIDEO_SETUP_DEFAULT();

// 0x80134928
SYTaskmanSetup dMNVSNetLevelPrefsMapsTaskmanSetup =
{
    // Task Manager Buffer Setup
    {
        0,                          // ???
        gcRunAll,              		// Update function
        scManagerFuncDraw,        	// Frame draw function
        &ovl30_BSS_END,             // Allocatable memory pool start
        0,                          // Allocatable memory pool size
        1,                          // ???
        2,                          // Number of contexts?
        sizeof(Gfx) * 2125,         // Display List Buffer 0 Size
        sizeof(Gfx) * 512,          // Display List Buffer 1 Size
        0,                          // Display List Buffer 2 Size
        0,                          // Display List Buffer 3 Size
        0x8000,                     // Graphics Heap Size
        2,                          // ???
        0x8000,                     // RDP Output Buffer Size
        mnVSNetLevelPrefsMapsFuncLights,   		// Pre-render function
        syControllerFuncRead,       // Controller I/O function
    },

    0,                              // Number of GObjThreads
    sizeof(u64) * 64,              	// Thread stack size
    0,                              // Number of thread stacks
    0,                              // ???
    0,                              // Number of GObjProcesses
    0,                              // Number of GObjs
    sizeof(GObj),                   // GObj size
    0,                              // Number of XObjs
    NULL,        					// Matrix function list
    NULL,                           // DObjVec eject function
    0,                              // Number of AObjs
    0,                              // Number of MObjs
    0,                              // Number of DObjs
    sizeof(DObj),                   // DObj size
    0,                              // Number of SObjs
    sizeof(SObj),                   // SObj size
    0,                              // Number of CObjs
    sizeof(CObj),                 	// CObj size
    
    mnVSNetLevelPrefsMapsFuncStart         		// Task start function
};

// 0x8013446C
void mnVSNetLevelPrefsStartScene(void)
{
	dMNVSNetLevelPrefsMapsVideoSetup.zbuffer = SYVIDEO_ZBUFFER_START(320, 240, 0, 10, u16);
	syVideoInit(&dMNVSNetLevelPrefsMapsVideoSetup);

	dMNVSNetLevelPrefsMapsTaskmanSetup.scene_setup.arena_size = (size_t) ((uintptr_t)&ovl1_VRAM - (uintptr_t)&ovl30_BSS_END);
	scManagerFuncUpdate(&dMNVSNetLevelPrefsMapsTaskmanSetup);
}
