#include <ft/fighter.h>
#include <if/interface.h>
#include <mn/menu.h>
#include <sc/scene.h>
#include <sys/video.h>
#include <sys/rdp.h>
#include <reloc_data.h>
#include <sys/audio.h>
#ifdef PORT
#include <errno.h>
#include <stdlib.h>
#include <string.h>
extern char *getenv(const char *name);
extern int atoi(const char *str);
#endif
#if defined(PORT) && defined(SSB64_NETMENU) && !defined(_WIN32)
#include <sc/scmanager.h>
#include <stdio.h>
#include <sys/netpeer.h>
#include <mm_matchmaking.h>
#include <mm_lan_detect.h>
#include <mm_stun.h>

extern void port_log(const char *fmt, ...);

void mnVSNetAutomatchAMReset(void);
void mnVSNetAutomatchAMStartSearch(void);
void mnVSNetAutomatchMatchmakingTick(void);
void mnVSNetAutomatchAMFinalizeVsLoad(void);
sb32 mnVSNetAutomatchAMConsumeStagingHandshake(void);
sb32 mnVSNetAutomatchAMIsError(void);
void mnVSNetAutomatchAMStagingReturnToAutomatch(void);
#endif
extern void *func_800269C0_275C0(u16 id);
extern void func_80026738_27338(void *arg0);
extern void func_800266A0_272A0(void);

/*
 * Netmenu automatch: fork of decomp mn/mnplayers/mnplayers1pgame.c with unique symbol prefix.
 * Entry nSCKindVSNetAutomatch. Back returns to nSCKindVSOnline. SSB64_NETMENU only.
 */

/* Internal forward declarations (-Wimplicit-function-declaration under PORT). */
void mnVSNetAutomatchFuncLights(Gfx **dls);
s32 mnVSNetAutomatchGetPowerOf(s32 base, s32 exp);
void mnVSNetAutomatchSetDigitColors(SObj *sobj, u32 *colors);
s32 mnVSNetAutomatchGetNumberDigitCount(s32 number, s32 digit_count_max);
void mnVSNetAutomatchMakeNumber(GObj *gobj, s32 number, f32 x, f32 y, u32 *colors, s32 digit_count_max, sb32 is_fixed_digit_count);
s32 mnVSNetAutomatchGetCharacterID(const char c);
f32 mnVSNetAutomatchGetCharacterSpacing(const char *str, s32 c);
void mnVSNetAutomatchMakeString(GObj *gobj, const char *str, f32 x, f32 y, u32 *colors);
void mnVSNetAutomatchSelectFighterPuck(s32 player, s32 select_button);
f32 mnVSNetAutomatchGetNextPortraitX(s32 portrait, f32 current_pos_x);
sb32 mnVSNetAutomatchCheckFighterCrossed(s32 fkind);
void mnVSNetAutomatchPortraitProcUpdate(GObj *gobj);
void mnVSNetAutomatchSetPortraitWallpaperPosition(SObj *sobj, s32 portrait);
void mnVSNetAutomatchPortraitAddCross(GObj *gobj, s32 portrait);
sb32 mnVSNetAutomatchCheckFighterLocked(s32 fkind);
s32 mnVSNetAutomatchGetFighterKind(s32 portrait);
s32 mnVSNetAutomatchGetPortrait(s32 fkind);
void mnVSNetAutomatchPortraitProcDisplay(GObj *gobj);
void mnVSNetAutomatchMakePortraitShadow(s32 portrait);
void mnVSNetAutomatchMakePortrait(s32 portrait);
void mnVSNetAutomatchMakePortraitAll(void);
void mnVSNetAutomatchMakeNameAndEmblem(GObj *gobj, s32 player, s32 fkind);
void mnVSNetAutomatchMakePortraitCamera(void);
void mnVSNetAutomatchMakePortraitWallpaperCamera(void);
void mnVSNetAutomatchMakePortraitFlashCamera(void);
void mnVSNetAutomatchMakePlayerKindCamera(void);
void mnVSNetAutomatchSetGateLUT(GObj *gobj, s32 player);
void mnVSNetAutomatchMakeGate(s32 player);
void mnVSNetAutomatchMakeTimeNumber(GObj *gobj, s32 number, f32 x, f32 y, u32 *colors, s32 digit_count_max, sb32 is_fixed_digit_count);
void mnVSNetAutomatchMakeTimeSetting(s32 number);
void mnVSNetAutomatchMakeTimeSelect(s32 number);
void mnVSNetAutomatchMakeWallpaper(void);
void mnVSNetAutomatchLoadWallpaperRelocFiles(void);
void mnVSNetAutomatchMakeWallpaperCamera(void);
void mnVSNetAutomatchLabelsProcDisplay(GObj *gobj);
void mnVSNetAutomatchLevelThreadUpdate(GObj *gobj);
void mnVSNetAutomatchMakeLevel(s32 level);
void mnVSNetAutomatchMakeLevelOption(void);
void mnVSNetAutomatchStockThreadUpdate(GObj *gobj);
void mnVSNetAutomatchMakeStock(s32 stock, s32 fkind);
void mnVSNetAutomatchMakeStockOption(void);
void mnVSNetAutomatchMakeLabels(void);
void mnVSNetAutomatchMakeLabelsCamera(void);
void mnVSNetAutomatchLabelsMinimalProcDisplay(GObj *gobj);
void mnVSNetAutomatchMakeLabelsMinimal(void);
u32 mnVSNetAutomatchGetHiScore(s32 fkind);
void mnVSNetAutomatchMakeHiScore(void);
u32 mnVSNetAutomatchGetBonusCount(s32 fkind);
void mnVSNetAutomatchMakeBonusCount(void);
void mnVSNetAutomatchMakeFighterRecord(void);
u32 mnVSNetAutomatchGetTotalHiScore(void);
void mnVSNetAutomatchMakeTotalHiScore(void);
u32 mnVSNetAutomatchGetTotalBonusCount(void);
void mnVSNetAutomatchMakeTotalBonusCount(void);
void mnVSNetAutomatchMakeTotalRecord(void);
s32 mnVSNetAutomatchGetFreeCostume(s32 fkind, s32 select_button);
s32 mnVSNetAutomatchGetStatusSelected(s32 fkind);
void mnVSNetAutomatchFighterProcUpdate(GObj *fighter_gobj);
void mnVSNetAutomatchMakeFighter(GObj *fighter_gobj, s32 player, s32 fkind, s32 costume);
void mnVSNetAutomatchMakeFighterCamera(void);
void mnVSNetAutomatchUpdateCursor(GObj *gobj, s32 player, s32 cursor_status);
sb32 mnVSNetAutomatchCheckTimeArrowRInRange(GObj *gobj);
sb32 mnVSNetAutomatchCheckTimeArrowLInRange(GObj *gobj);
sb32 mnVSNetAutomatchCheckBackInRange(GObj *gobj);
sb32 mnVSNetAutomatchCheckPuckInRange(GObj *gobj, s32 cursor_player, s32 player);
void mnVSNetAutomatchUpdateFighter(s32 player);
void mnVSNetAutomatchUpdateNameAndEmblem(s32 player);
void mnVSNetAutomatchDestroyPortraitFlash(s32 player);
void mnVSNetAutomatchPortraitFlashThreadUpdate(GObj *gobj);
void mnVSNetAutomatchMakePortraitFlash(s32 player);
void mnVSNetAutomatchAnnounceFighter(s32 player, s32 slot);
sb32 mnVSNetAutomatchCheckSelectFighter(GObj *gobj, s32 player, s32 unused, s32 select_button);
void mnVSNetAutomatchUpdateCursorGrabPriorities(s32 player, s32 puck);
void mnVSNetAutomatchUpdateCursorPlacementPriorities(s32 player);
void mnVSNetAutomatchSetCursorPuckOffset(s32 player);
void mnVSNetAutomatchSetCursorGrab(s32 player);
sb32 mnVSNetAutomatchCheckCursorPuckGrab(GObj *gobj, s32 player);
s32 mnVSNetAutomatchGetForcePuckFighterKind(void);
s32 mnVSNetAutomatchGetPuckFighterKind(s32 player);
void mnVSNetAutomatchAdjustCursor(GObj *gobj, s32 player);
void mnVSNetAutomatchUpdateCursorNoRecall(GObj *gobj, s32 player);
sb32 mnVSNetAutomatchCheckLevelArrowRInRange(GObj *gobj);
sb32 mnVSNetAutomatchCheckLevelArrowLInRange(GObj *gobj);
sb32 mnVSNetAutomatchCheckLevelArrowPress(GObj *gobj);
sb32 mnVSNetAutomatchCheckStockArrowRInRange(GObj *gobj);
sb32 mnVSNetAutomatchCheckStockArrowLInRange(GObj *gobj);
sb32 mnVSNetAutomatchCheckStockArrowPress(GObj *gobj);
void mnVSNetAutomatchUpdateCostume(s32 player, s32 select_button);
sb32 mnVSNetAutomatchCheckManFighterSelected(s32 player);
void mnVSNetAutomatchRecallPuck(s32 player);
void mnVSNetAutomatchBackTo1PMode(void);
void mnVSNetAutomatchDetectBack(s32 player);
void mnVSNetAutomatchCursorProcUpdate(GObj *gobj);
void mnVSNetAutomatchCenterPuckInPortrait(GObj *gobj, s32 fkind);
void mnVSNetAutomatchMovePuck(s32 player);
void mnVSNetAutomatchPuckProcUpdate(GObj *gobj);
void mnVSNetAutomatchMakeCursorCamera(void);
void mnVSNetAutomatchMakePuckCamera(void);
void mnVSNetAutomatchMakeReadyCamera(void);
void mnVSNetAutomatchMakeCursor(s32 player);
void mnVSNetAutomatchMakePuck(s32 player);
void mnVSNetAutomatchPuckAdjustPortraitEdge(s32 player);
void mnVSNetAutomatchPuckAdjustPlaced(s32 player);
void mnVSNetAutomatchPuckAdjustRecall(s32 player);
void mnVSNetAutomatchPuckAdjustProcUpdate(GObj *gobj);
void mnVSNetAutomatchMakePuckAdjust(void);
void mnVSNetAutomatchSpotlightProcUpdate(GObj *gobj);
void mnVSNetAutomatchMakeSpotlight(void);
void mnVSNetAutomatchReadyProcUpdate(GObj *gobj);
void mnVSNetAutomatchMakeReady(void);
sb32 mnVSNetAutomatchCheckReady(void);
void mnVSNetAutomatchSetSceneData(void);
void mnVSNetAutomatchPauseSlotProcesses(void);
void mnVSNetAutomatchFuncRun(GObj *gobj);
s32 mnVSNetAutomatchGetNextTimeValue(s32 value);
s32 mnVSNetAutomatchGetPrevTimeValue(s32 value);
void mnVSNetAutomatchInitPlayer(s32 player);
void mnVSNetAutomatchInitVars(void);
void mnVSNetAutomatchInitSlot(s32 player);
void mnVSNetAutomatchFuncStart(void);
void mnVSNetAutomatchStartScene(void);

// // // // // // // // // // // //
//                               //
//       INITIALIZED DATA        //
//                               //
// // // // // // // // // // // //

// 0x801385B0
s32 dMNVSNetAutomatchUnknown0x801385B0[/* */] =
{
	0x000080C7, 0x914B4001, 0x30017085, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x000031A1, 0x39E52119, 0x2113211D, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00008BC1, 0x9C0341C1, 0x31417B41, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00001AC7, 0x230901C1, 0x09431245, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000
};

// 0x80138630
u32 dMNVSNetAutomatchFileIDs[/* */] =
{
	llMNPlayersCommonFileID,
	llFTEmblemSpritesFileID,
	llMNSelectCommonFileID,
	llMNPlayersGameModesFileID, 
	llMNPlayersPortraitsFileID,
	llMNPlayers1PModeFileID,
	llMNPlayersDifficultyFileID,
	llFTStocksZakoFileID,
	llMNCommonFontsFileID,
	llIFCommonDigitsFileID,
	llMNPlayersSpotlightFileID
};

// 0x80138660
Lights1 dMNVSNetAutomatchLights11 = gdSPDefLights1(0x20, 0x20, 0x20, 0xFF, 0xFF, 0xFF, 0x14, 0x14, 0x14);

// 0x80138678
Lights1 dMNVSNetAutomatchLights12 = gdSPDefLights1(0x20, 0x20, 0x20, 0xFF, 0xFF, 0xFF, 0x00, 0xEC, 0x00);

// // // // // // // // // // // //
//                               //
//   GLOBAL / STATIC VARIABLES   //
//                               //
// // // // // // // // // // // //

// 0x80138EE0
s32 sMNVSNetAutomatchPad0x80138EE0[2];

// 0x80138EE8
MNPlayersSlot1PGame sMNVSNetAutomatchSlot;

// 0x80138F70
GObj *sMNVSNetAutomatchTimeGObj;

// 0x80138F74
s32 sMNVSNetAutomatchTotalTimeTics;

// 0x80138F78
s32 sMNVSNetAutomatchReturnTic;

// 0x80138F7C
s32 sMNVSNetAutomatchReadyBlinkWait;

// 0x80138F80
s32 sMNVSNetAutomatchTimeSetting;

// 0x80138F80
s32 sMNVSNetAutomatchPad0x80138F80[4];

// 0x80138F98
s32 sMNVSNetAutomatchStartProceedWait;

// 0x80138F9C
sb32 sMNVSNetAutomatchIsStart;

// 0x80138FA0
sb32 sMNVSNetAutomatchIsTeamBattle;

// 0x80138FA4
s32 sMNVSNetAutomatchRule;

// 0x80138FA8
s32 sMNVSNetAutomatchManPlayer;

// 0x80138FAC
GObj *sMNVSNetAutomatchHiScoreGObj;

// 0x80138FB0
GObj *sMNVSNetAutomatchBonusesGObj;

// 0x80138FB4 level
s32 sMNVSNetAutomatchLevelValue;

// 0x80138FB8 stocks
s32 sMNVSNetAutomatchStockValue;

// 0x80138FBC
GObj *sMNVSNetAutomatchLevelGObj;

// 0x80138FC0
GObj *sMNVSNetAutomatchStockGObj;

// 0x80138FC4
void *sMNVSNetAutomatchFigatreeHeap;

// 0x80138FC8
u16 sMNVSNetAutomatchFighterMask;

// 0x80138FCC
s32 sMNVSNetAutomatchFighterKind;

// 0x80138FD0
s32 sMNVSNetAutomatchCostume;

// 0x80138FD8
s32 sMNVSNetAutomatchPad0x80138FD8[180];

// 0x801392A8
LBFileNode sMNVSNetAutomatchForceStatusBuffer[7];

// 0x801392E0
LBFileNode sMNVSNetAutomatchStatusBuffer[120];

// 0x801396A0
void *sMNVSNetAutomatchFiles[ARRAY_COUNT(dMNVSNetAutomatchFileIDs)];

// // // // // // // // // // // //
//                               //
//           FUNCTIONS           //
//                               //
// // // // // // // // // // // //

// 0x80131B00
void mnVSNetAutomatchFuncLights(Gfx **dls)
{
	gSPSetGeometryMode(dls[0]++, G_LIGHTING);
	ftDisplayLightsDrawReflect(dls, scSubsysFighterGetLightAngleX(), scSubsysFighterGetLightAngleY());
}

// 0x80131B58
s32 mnVSNetAutomatchGetPowerOf(s32 base, s32 exp)
{
	s32 raised = base;
	s32 i;

	if (exp == 0)
	{
		return 1;
	}
	i = exp;

	while (i > 1)
	{
		i--;
		raised *= base;
	}
	return raised;
}

// 0x80131BF8
void mnVSNetAutomatchSetDigitColors(SObj *sobj, u32 *colors)
{
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;
	sobj->envcolor.r = colors[0];
	sobj->envcolor.g = colors[1];
	sobj->envcolor.b = colors[2];
	sobj->sprite.red = colors[3];
	sobj->sprite.green = colors[4];
	sobj->sprite.blue = colors[5];
}

// 0x80131C40
s32 mnVSNetAutomatchGetNumberDigitCount(s32 number, s32 digit_count_max)
{
    s32 digit_count_curr = digit_count_max;

    while (digit_count_curr > 0)
    {
        s32 digit = (mnVSNetAutomatchGetPowerOf(10, digit_count_curr - 1) != 0) ? number / mnVSNetAutomatchGetPowerOf(10, digit_count_curr - 1) : 0;

        if (digit != 0)
        {
            return digit_count_curr;
        }
        else digit_count_curr--;
    }
    return 0;
}

// 0x80131CEC
void mnVSNetAutomatchMakeNumber(GObj *gobj, s32 number, f32 x, f32 y, u32 *colors, s32 digit_count_max, sb32 is_fixed_digit_count)
{
	intptr_t offsets[/* */] =
	{
		llIFCommonDigits0Sprite, llIFCommonDigits1Sprite,
		llIFCommonDigits2Sprite, llIFCommonDigits3Sprite,
		llIFCommonDigits4Sprite, llIFCommonDigits5Sprite,
		llIFCommonDigits6Sprite, llIFCommonDigits7Sprite,
		llIFCommonDigits8Sprite, llIFCommonDigits9Sprite
	};
	SObj *sobj;
	f32 left_x = x;
	s32 i;
	s32 unused;
	s32 digit;

	if (number < 0)
	{
		number = 0;
	}
	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[9], offsets[number % 10]));
	mnVSNetAutomatchSetDigitColors(sobj, colors);
	left_x -= 8;
	sobj->pos.x = left_x;
	sobj->pos.y = y;

	for (i = 1; i < ((is_fixed_digit_count != FALSE) ? digit_count_max : mnVSNetAutomatchGetNumberDigitCount(number, digit_count_max)); i++)
	{
		digit = (mnVSNetAutomatchGetPowerOf(10, i) != 0) ? number / mnVSNetAutomatchGetPowerOf(10, i) : 0;

		sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[9], offsets[digit % 10]));
		mnVSNetAutomatchSetDigitColors(sobj, colors);
		left_x -= 8;
		sobj->pos.x = left_x;
		sobj->pos.y = y;
	}
}

// 0x80131F5C
s32 mnVSNetAutomatchGetCharacterID(const char c)
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

// 0x80131FD4
f32 mnVSNetAutomatchGetCharacterSpacing(const char *str, s32 c)
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

// 0x801320F8
void mnVSNetAutomatchMakeString(GObj *gobj, const char *str, f32 x, f32 y, u32 *colors)
{
	intptr_t offsets[/* */] =
	{
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

		llMNCommonFontsSymbolApostropheSprite,
		llMNCommonFontsSymbolPercentSprite,
		llMNCommonFontsSymbolPeriodSprite
	};
	f32 widths[/* */] =
	{
		5.0F, 4.0F, 4.0F, 4.0F, 4.0F, 4.0F, 4.0F, 4.0F, 3.0F, 4.0F, 4.0F, 4.0F, 5.0F, 5.0F, 4.0F,
		4.0F, 5.0F, 4.0F, 4.0F, 5.0F, 4.0F, 5.0F, 5.0F, 5.0F, 5.0F, 4.0F, 2.0F, 7.0F, 3.0F
	};
	SObj *sobj;
	f32 current_x = x;
	s32 i;
	sb32 is_number;

	for (i = 0; str[i] != 0; i++)
	{
		is_number = ((str[i] >= '0') && (str[i] <= '9')) ? TRUE : FALSE;

		if ((is_number != FALSE) || (str[i] == ' '))
		{
			if (str[i] == ' ')
			{
				current_x += 3.0F;
			}
			else current_x += str[i] - '0';
		}
		else
		{
			sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[8], offsets[mnVSNetAutomatchGetCharacterID(str[i])]));
			sobj->pos.x = current_x;

			current_x += widths[mnVSNetAutomatchGetCharacterID(str[i])] + mnVSNetAutomatchGetCharacterSpacing(str, i);

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
			sobj->sprite.red = colors[0];
			sobj->sprite.green = colors[1];
			sobj->sprite.blue = colors[2];
		}
	}
}

// 0x80132384
void mnVSNetAutomatchSelectFighterPuck(s32 player, s32 select_button)
{
	s32 held_player = sMNVSNetAutomatchSlot.held_player;
	s32 costume = ftParamGetCostumeCommonID(sMNVSNetAutomatchSlot.fkind, select_button);

	ftParamInitAllParts(sMNVSNetAutomatchSlot.player, costume, 0);

	sMNVSNetAutomatchSlot.costume = costume;
	sMNVSNetAutomatchSlot.is_selected = TRUE;
	sMNVSNetAutomatchSlot.holder_player = GMCOMMON_PLAYERS_MAX;
	sMNVSNetAutomatchSlot.cursor_status = nMNPlayersCursorStatusHover;

	mnVSNetAutomatchUpdateCursor(sMNVSNetAutomatchSlot.cursor, player, sMNVSNetAutomatchSlot.cursor_status);

	sMNVSNetAutomatchSlot.held_player = -1;
	sMNVSNetAutomatchSlot.is_fighter_selected = TRUE;

	mnVSNetAutomatchUpdateCursorPlacementPriorities(held_player);
	mnVSNetAutomatchAnnounceFighter(player, held_player);
	mnVSNetAutomatchMakePortraitFlash(held_player);
}

// 0x8013243C
f32 mnVSNetAutomatchGetNextPortraitX(s32 portrait, f32 current_pos_x)
{
	f32 portrait_pos_x[/* */] =
	{
		25.0F, 70.0F, 115.0F, 160.0F, 205.0F, 250.0F,
		25.0F, 70.0F, 115.0F, 160.0F, 205.0F, 250.0F
	};

	f32	portrait_vel[/* */] =
	{
		1.9F, 3.9F, 7.8F, -7.8F, -3.8F, -1.8F,
		1.8F, 3.8F, 7.8F, -7.8F, -3.8F, -1.8F
	};

	if (current_pos_x == portrait_pos_x[portrait])
	{
		return -1.0F;
	}
	else if (portrait_pos_x[portrait] < current_pos_x)
	{
		return ((current_pos_x + portrait_vel[portrait]) <= portrait_pos_x[portrait]) ? 
		portrait_pos_x[portrait] :
		current_pos_x + portrait_vel[portrait];
	}
	else return (current_pos_x + portrait_vel[portrait]) >= portrait_pos_x[portrait] ?
	portrait_pos_x[portrait] :
	current_pos_x + portrait_vel[portrait];
}

// 0x80132550
sb32 mnVSNetAutomatchCheckFighterCrossed(s32 fkind)
{
	return FALSE;
}

// 0x8013255C
void mnVSNetAutomatchPortraitProcUpdate(GObj *gobj)
{
	f32 new_pos_x = mnVSNetAutomatchGetNextPortraitX(gobj->user_data.s, SObjGetStruct(gobj)->pos.x);

	if (new_pos_x != -1.0F)
	{
		SObjGetStruct(gobj)->pos.x = new_pos_x;

		if (SObjGetStruct(gobj)->next != NULL)
		{
			SObjGetStruct(gobj)->next->pos.x = SObjGetStruct(gobj)->pos.x + 4.0F;
		}
	}
}

// 0x801325D8
void mnVSNetAutomatchSetPortraitWallpaperPosition(SObj *sobj, s32 portrait)
{
	Vec2f pos[/* */] =
	{
		{ -35.0F, 36.0F }, { -35.0F, 36.0F },
		{ -35.0F, 36.0F }, { 310.0F, 36.0F },
		{ 310.0F, 36.0F }, { 310.0F, 36.0F },
		{ -35.0F, 79.0F }, { -35.0F, 79.0F },
		{ -35.0F, 79.0F }, { 310.0F, 79.0F },
		{ 310.0F, 79.0F }, { 310.0F, 79.0F }
	};

	sobj->pos.x = pos[portrait].x;
	sobj->pos.y = pos[portrait].y;
}

// 0x80132634
void mnVSNetAutomatchPortraitAddCross(GObj *gobj, s32 portrait)
{
	SObj *sobj = SObjGetStruct(gobj);
	f32 x = sobj->pos.x;
	f32 y = sobj->pos.y;

	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[4], llMNPlayersPortraitsCrossSprite));

	sobj->pos.x = x + 4.0F;
	sobj->pos.y = y + 12.0F;
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;
	sobj->sprite.red = 0xFF;
	sobj->sprite.green = 0x00;
	sobj->sprite.blue = 0x00;
}

// 0x801326C8
sb32 mnVSNetAutomatchCheckFighterLocked(s32 fkind)
{
	switch (fkind)
	{
	case nFTKindNess:
		return (sMNVSNetAutomatchFighterMask & (1 << nFTKindNess)) ? FALSE : TRUE;

	case nFTKindPurin:
		return (sMNVSNetAutomatchFighterMask & (1 << nFTKindPurin)) ? FALSE : TRUE;

	case nFTKindCaptain:
		return (sMNVSNetAutomatchFighterMask & (1 << nFTKindCaptain)) ? FALSE : TRUE;

	case nFTKindLuigi:
		return (sMNVSNetAutomatchFighterMask & (1 << nFTKindLuigi)) ? FALSE : TRUE;

	default:
		return FALSE;
	}
}

// 0x80138860
s32 dMNVSNetAutomatchUnknown0x80138860[/* */] =
{
	0xC55252C5,
	0xA6524294,
	0x595252C5,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000
};

// 0x80132794 - Unused?
void func_vsNetAutomatch27_80132794(void)
{
	return;
}

// 0x8013279C
s32 mnVSNetAutomatchGetFighterKind(s32 portrait)
{
	s32 fkinds[/* */] =
	{
		nFTKindLuigi, nFTKindMario, nFTKindDonkey, nFTKindLink, nFTKindSamus,   nFTKindCaptain,
		nFTKindNess,  nFTKindYoshi, nFTKindKirby,  nFTKindFox,  nFTKindPikachu, nFTKindPurin
	};

	return fkinds[portrait];
}

// 0x801327EC
s32 mnVSNetAutomatchGetPortrait(s32 fkind)
{
	s32 portraits[/* */] =
	{
		1, 9, 2, 4, 0, 3,
		7, 5, 8, 10, 11, 6
	};

	return portraits[fkind];
}

// 0x8013283C
void mnVSNetAutomatchPortraitProcDisplay(GObj *gobj)
{
	gDPPipeSync(gSYTaskmanDLHeads[0]++);
	gDPSetCycleType(gSYTaskmanDLHeads[0]++, G_CYC_1CYCLE);
	gDPSetPrimColor(gSYTaskmanDLHeads[0]++, 0, 0, 0x30, 0x30, 0x30, 0xFF);
	gDPSetCombineLERP(gSYTaskmanDLHeads[0]++, NOISE, TEXEL0, PRIMITIVE, TEXEL0, 0, 0, 0, TEXEL0, NOISE, TEXEL0, PRIMITIVE, TEXEL0,  0, 0, 0, TEXEL0);
	gDPSetRenderMode(gSYTaskmanDLHeads[0]++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);

	lbCommonDrawSObjNoAttr(gobj);
}

// 0x801328FC
void mnVSNetAutomatchMakePortraitShadow(s32 portrait)
{
	GObj *gobj;
	SObj *sobj;
	intptr_t offsets[/* */] =
	{
		0x0, 									0x0,
		0x0, 									0x0,
		llMNPlayersPortraitsLuigiShadowSprite, 	0x0,
		0x0, 									llMNPlayersPortraitsCaptainShadowSprite,
		0x0, 									0x0,
		llMNPlayersPortraitsPurinShadowSprite, 	llMNPlayersPortraitsNessShadowSprite
	};

	gobj = gcMakeGObjSPAfter(0, NULL, 18, GOBJ_PRIORITY_DEFAULT);
	gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 27, GOBJ_PRIORITY_DEFAULT, ~0);
	gcAddGObjProcess(gobj, mnVSNetAutomatchPortraitProcUpdate, nGCProcessKindFunc, 1);

	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[4], llMNPlayersPortraitsPortraitFireBgSprite));
	sobj->pos.x = (((portrait >= 6) ? portrait - 6 : portrait) * 45) + 25;
	sobj->pos.y = (((portrait >= 6) ? 1 : 0) * 43) + 36;

	mnVSNetAutomatchSetPortraitWallpaperPosition(sobj, portrait);
	gobj->user_data.s = portrait;

	gobj = gcMakeGObjSPAfter(0, NULL, 18, GOBJ_PRIORITY_DEFAULT);
	gcAddGObjDisplay(gobj, mnVSNetAutomatchPortraitProcDisplay, 27, GOBJ_PRIORITY_DEFAULT, ~0);
	gcAddGObjProcess(gobj, mnVSNetAutomatchPortraitProcUpdate, nGCProcessKindFunc, 1);

	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[4], offsets[mnVSNetAutomatchGetFighterKind(portrait)]));
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;

	gobj->user_data.s = portrait;
	mnVSNetAutomatchSetPortraitWallpaperPosition(sobj, portrait);

	gobj = gcMakeGObjSPAfter(0, NULL, 18, GOBJ_PRIORITY_DEFAULT);
	gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 27, GOBJ_PRIORITY_DEFAULT, ~0);
	gcAddGObjProcess(gobj, mnVSNetAutomatchPortraitProcUpdate, nGCProcessKindFunc, 1);

	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[4], llMNPlayersPortraitsPortraitQuestionMarkSprite));
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;
	sobj->envcolor.r = 0x5B;
	sobj->envcolor.g = 0x41;
	sobj->envcolor.b = 0x33;
	sobj->sprite.red = 0xC4;
	sobj->sprite.green = 0xB9;
	sobj->sprite.blue = 0xA9;

	gobj->user_data.s = portrait;
	mnVSNetAutomatchSetPortraitWallpaperPosition(sobj, portrait);
}

// 0x80132BA4
void mnVSNetAutomatchMakePortrait(s32 portrait)
{
	GObj *portrait_gobj, *wallpaper_gobj;
	SObj *sobj;
	intptr_t offsets[/* */] =
	{
		llMNPlayersPortraitsMarioSprite,	llMNPlayersPortraitsFoxSprite,
		llMNPlayersPortraitsDonkeySprite,	llMNPlayersPortraitsSamusSprite,
		llMNPlayersPortraitsLuigiSprite,	llMNPlayersPortraitsLinkSprite,
		llMNPlayersPortraitsYoshiSprite,	llMNPlayersPortraitsCaptainSprite,
		llMNPlayersPortraitsKirbySprite,	llMNPlayersPortraitsPikachuSprite,
		llMNPlayersPortraitsPurinSprite,	llMNPlayersPortraitsNessSprite
	};

	if (mnVSNetAutomatchCheckFighterLocked(mnVSNetAutomatchGetFighterKind(portrait)) != FALSE)
	{
		mnVSNetAutomatchMakePortraitShadow(portrait);
	}
	else
	{
		wallpaper_gobj = gcMakeGObjSPAfter(0, NULL, 25, GOBJ_PRIORITY_DEFAULT);
		gcAddGObjDisplay(wallpaper_gobj, lbCommonDrawSObjAttr, 32, GOBJ_PRIORITY_DEFAULT, ~0);
		wallpaper_gobj->user_data.s = portrait;
		gcAddGObjProcess(wallpaper_gobj, mnVSNetAutomatchPortraitProcUpdate, nGCProcessKindFunc, 1);

		sobj = lbCommonMakeSObjForGObj(wallpaper_gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[4], llMNPlayersPortraitsPortraitFireBgSprite));

		mnVSNetAutomatchSetPortraitWallpaperPosition(sobj, portrait);

		portrait_gobj = gcMakeGObjSPAfter(0, NULL, 18, GOBJ_PRIORITY_DEFAULT);
		gcAddGObjDisplay(portrait_gobj, lbCommonDrawSObjAttr, 27, GOBJ_PRIORITY_DEFAULT, ~0);
		gcAddGObjProcess(portrait_gobj, mnVSNetAutomatchPortraitProcUpdate, nGCProcessKindFunc, 1);

		sobj = lbCommonMakeSObjForGObj(portrait_gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[4], offsets[mnVSNetAutomatchGetFighterKind(portrait)]));
		sobj->sprite.attr &= ~SP_FASTCOPY;
		sobj->sprite.attr |= SP_TRANSPARENT;
		portrait_gobj->user_data.s = portrait;

		if (mnVSNetAutomatchCheckFighterCrossed(mnVSNetAutomatchGetFighterKind(portrait)) != FALSE)
		{
			mnVSNetAutomatchPortraitAddCross(portrait_gobj, portrait);
		}
		mnVSNetAutomatchSetPortraitWallpaperPosition(sobj, portrait);
	}
}

// 0x80132D60
void mnVSNetAutomatchMakePortraitAll(void)
{
	s32 i;

	for (i = nFTKindPlayableStart; i <= nFTKindPlayableEnd; i++)
	{
		mnVSNetAutomatchMakePortrait(i);
	}
}

// 0x80132DA0
void mnVSNetAutomatchMakeNameAndEmblem(GObj *gobj, s32 player, s32 fkind)
{
	SObj *sobj;
	Vec2f pos[/* */] =
	{
		{ 13.0F, 28.0F }, {  6.0F, 25.0F },
		{  5.0F, 25.0F }, { 13.0F, 25.0F },
		{ 13.0F, 28.0F }, { 13.0F, 28.0F },
		{ 16.0F, 25.0F }, {  4.0F, 25.0F },
		{ 13.0F, 25.0F }, { 13.0F, 25.0F },
		{ 13.0F, 25.0F }, { 13.0F, 25.0F }
	};
	intptr_t emblem_offsets[/* */] =
	{
		llFTEmblemSpritesMarioSprite,     llFTEmblemSpritesFoxSprite,
		llFTEmblemSpritesDonkeySprite,    llFTEmblemSpritesMetroidSprite,
		llFTEmblemSpritesMarioSprite,     llFTEmblemSpritesZeldaSprite,
		llFTEmblemSpritesYoshiSprite,     llFTEmblemSpritesFZeroSprite,
		llFTEmblemSpritesKirbySprite,     llFTEmblemSpritesPMonstersSprite,
		llFTEmblemSpritesPMonstersSprite, llFTEmblemSpritesMotherSprite
	};
	intptr_t name_offsets[/* */] =
	{
		llMNPlayersCommonMarioTextSprite,      llMNPlayersCommonFoxTextSprite,
		llMNPlayersCommonDKTextSprite,         llMNPlayersCommonSamusTextSprite,
		llMNPlayersCommonLuigiTextSprite,      llMNPlayersCommonLinkTextSprite,
		llMNPlayersCommonYoshiTextSprite,      llMNPlayersCommonCaptainFalconTextSprite,
		llMNPlayersCommonKirbyTextSprite,      llMNPlayersCommonPikachuTextSprite,
		llMNPlayersCommonJigglypuffTextSprite, llMNPlayersCommonNessTextSprite
	};

	if (fkind != nFTKindNull)
	{
		gcRemoveSObjAll(gobj);

		sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[1], emblem_offsets[fkind]));
		sobj->sprite.attr &= ~SP_FASTCOPY;
		sobj->sprite.attr |= SP_TRANSPARENT;
		sobj->sprite.red = 0x00;
		sobj->sprite.green = 0x00;
		sobj->sprite.blue = 0x00;
		sobj->pos.x = 35.0F;
		sobj->pos.y = 144.0F;

		sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[0], name_offsets[fkind]));
		sobj->sprite.attr &= ~SP_FASTCOPY;
		sobj->sprite.attr |= SP_TRANSPARENT;
		sobj->pos.x = 33.0F;
		sobj->pos.y = 202.0F;
	}
}

// 0x80132F1C
void mnVSNetAutomatchMakePortraitCamera(void)
{
	CObj *cobj = CObjGetStruct
	(
		gcMakeCameraGObj
		(
			nGCCommonKindSceneCamera,
			NULL,
			16,
			GOBJ_PRIORITY_DEFAULT,
			lbCommonDrawSprite,
			40,
			COBJ_MASK_DLLINK(27),
			~0,
			FALSE,
			nGCProcessKindFunc,
			NULL,
			1,
			FALSE
		)
	);
	syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);
}

// 0x80132FBC
void mnVSNetAutomatchMakePortraitWallpaperCamera(void)
{
	CObj *cobj = CObjGetStruct
	(
		gcMakeCameraGObj
		(
			nGCCommonKindSceneCamera,
			NULL,
			16,
			GOBJ_PRIORITY_DEFAULT,
			lbCommonDrawSprite,
			60,
			COBJ_MASK_DLLINK(32),
			~0,
			FALSE,
			nGCProcessKindFunc,
			NULL,
			1,
			FALSE
		)
	);
	syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);
}

// 0x8013305C
void mnVSNetAutomatchMakePortraitFlashCamera(void)
{
	CObj *cobj = CObjGetStruct
	(
		gcMakeCameraGObj
		(
			nGCCommonKindSceneCamera,
			NULL,
			16,
			GOBJ_PRIORITY_DEFAULT,
			lbCommonDrawSprite,
			50,
			COBJ_MASK_DLLINK(33),
			~0,
			FALSE,
			nGCProcessKindFunc,
			NULL,
			1,
			FALSE
		)
	);
	syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);
}

// 0x801330FC
void mnVSNetAutomatchMakePlayerKindCamera(void)
{
	CObj *cobj = CObjGetStruct
	(
		gcMakeCameraGObj
		(
			nGCCommonKindSceneCamera,
			NULL,
			16,
			GOBJ_PRIORITY_DEFAULT,
			lbCommonDrawSprite,
			30,
			COBJ_MASK_DLLINK(28),
			~0,
			FALSE,
			nGCProcessKindFunc,
			NULL,
			1,
			FALSE
		)
	);
	syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);
}

// 0x8013319C
void mnVSNetAutomatchSetGateLUT(GObj *gobj, s32 player)
{
	SObj *sobj;

	intptr_t offsets[/* */] =
	{
		llMNPlayersCommonGateMan1PLUT, llMNPlayersCommonGateMan2PLUT,
		llMNPlayersCommonGateMan3PLUT, llMNPlayersCommonGateMan4PLUT
	};

	sobj = SObjGetStruct(gobj);
	sobj->sprite.LUT = PORT_REGISTER(lbRelocGetFileData(void*, sMNVSNetAutomatchFiles[0], offsets[player]));
}

// 0x801331F4
void mnVSNetAutomatchMakeGate(s32 player)
{
	GObj *gobj;
	SObj *sobj;

	intptr_t offsets[/* */] =
	{
		llMNPlayersCommon1PTextSprite, llMNPlayersCommon2PTextSprite,
		llMNPlayersCommon3PTextSprite, llMNPlayersCommon4PTextSprite
	};
	f32 pos_x[/* */] = { 8.0F, 5.0F, 5.0F, 5.0F };

	gobj = lbCommonMakeSpriteGObj
	(
		0,
		NULL,
		22,
		GOBJ_PRIORITY_DEFAULT,
		lbCommonDrawSObjAttr,
		28,
		GOBJ_PRIORITY_DEFAULT,
		~0,
		lbRelocGetFileData
		(
			Sprite*,
			sMNVSNetAutomatchFiles[5],
			llMNPlayers1PModeRedCardSprite
		),
		nGCProcessKindFunc,
		NULL,
		1
	);
	SObjGetStruct(gobj)->pos.x = 25.0F;
	SObjGetStruct(gobj)->pos.y = 127.0F;
	SObjGetStruct(gobj)->sprite.attr &= ~SP_FASTCOPY;
	SObjGetStruct(gobj)->sprite.attr |= SP_TRANSPARENT;

	sMNVSNetAutomatchSlot.panel = gobj;

	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[0], offsets[player]));
	sobj->pos.x = pos_x[player] + 25.0F;
	sobj->pos.y = 132.0F;
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;
	sobj->sprite.red = 0x00;
	sobj->sprite.green = 0x00;
	sobj->sprite.blue = 0x00;

	mnVSNetAutomatchSetGateLUT(gobj, player);

	gobj = gcMakeGObjSPAfter(0, NULL, 22, GOBJ_PRIORITY_DEFAULT);
	sMNVSNetAutomatchSlot.name_emblem_gobj = gobj;
#ifdef PORT
	sMNVSNetAutomatchSlot.name_emblem_fkind = nFTKindNull;
	sMNVSNetAutomatchSlot.record_update_fkind = nFTKindNull;
#endif
	gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 28, GOBJ_PRIORITY_DEFAULT, ~0);

	mnVSNetAutomatchUpdateNameAndEmblem(player);
}

// 0x801333D4
void mnVSNetAutomatchMakeTimeNumber(GObj *gobj, s32 number, f32 x, f32 y, u32 *colors, s32 digit_count_max, sb32 is_fixed_digit_count)
{
	intptr_t offsets[/* */] =
	{
		llMNPlayersCommon0DarkSprite,
		llMNPlayersCommon1DarkSprite,
		llMNPlayersCommon2DarkSprite,
		llMNPlayersCommon3DarkSprite,
		llMNPlayersCommon4DarkSprite,
		llMNPlayersCommon5DarkSprite,
		llMNPlayersCommon6DarkSprite,
		llMNPlayersCommon7DarkSprite,
		llMNPlayersCommon8DarkSprite,
		llMNPlayersCommon9DarkSprite
	};

	f32 widths[/* */] =
	{
		8.0F, 6.0F, 9.0F, 8.0F, 8.0F,
		9.0F, 8.0F, 8.0F, 8.0F, 9.0F
	};

	SObj *sobj;
	f32 left_x = x;
	s32 i;
	s32 digit;

	if (number < 0)
	{
		number = 0;
	}
	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[0], offsets[number % 10]));
	mnVSNetAutomatchSetDigitColors(sobj, colors);
	left_x -= widths[number % 10];
	sobj->pos.x = left_x;
	sobj->pos.y = y;

	for (i = 1; i < ((is_fixed_digit_count != FALSE) ? digit_count_max : mnVSNetAutomatchGetNumberDigitCount(number, digit_count_max)); i++)
	{
		digit = (mnVSNetAutomatchGetPowerOf(10, i) != 0) ? number / mnVSNetAutomatchGetPowerOf(10, i) : 0;

		sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[0], offsets[digit % 10]));
		mnVSNetAutomatchSetDigitColors(sobj, colors);
		left_x -= widths[digit % 10];
		sobj->pos.x = left_x;
		sobj->pos.y = y;
	}
}

// 0x80133680
void mnVSNetAutomatchMakeTimeSetting(s32 number)
{
	u32 colors[/* */] = { 0x32, 0x1C, 0x0E, 0xFF, 0xFF, 0xFF };
	SObj *sobj;

	while (SObjGetStruct(sMNVSNetAutomatchTimeGObj)->next != NULL)
	{
		gcEjectSObj(SObjGetStruct(sMNVSNetAutomatchTimeGObj)->next);
	}
	if (number == 100)
	{
		sobj = lbCommonMakeSObjForGObj(sMNVSNetAutomatchTimeGObj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[0], llMNPlayersCommonInfinityDarkSprite));
		sobj->pos.x = 194.0F;
		sobj->pos.y = 24.0F;
		sobj->envcolor.r = colors[0];
		sobj->envcolor.g = colors[1];
		sobj->envcolor.b = colors[2];
		sobj->sprite.red = colors[3];
		sobj->sprite.green = colors[4];
		sobj->sprite.blue = colors[5];
		sobj->sprite.attr &= ~SP_FASTCOPY;
		sobj->sprite.attr |= SP_TRANSPARENT;
	}
	else if (number < 10)
	{
		mnVSNetAutomatchMakeTimeNumber(sMNVSNetAutomatchTimeGObj, number, 205.0F, 23.0F, colors, 2, FALSE);
	}
	else mnVSNetAutomatchMakeTimeNumber(sMNVSNetAutomatchTimeGObj, number, 209.0F, 23.0F, colors, 2, FALSE);
}

// 0x80133804
void mnVSNetAutomatchMakeTimeSelect(s32 number)
{
	GObj *gobj;

	if (sMNVSNetAutomatchTimeGObj != NULL)
	{
		gcEjectGObj(sMNVSNetAutomatchTimeGObj);
	}
	gobj = lbCommonMakeSpriteGObj
	(
		0,
		NULL,
		23,
		GOBJ_PRIORITY_DEFAULT,
		lbCommonDrawSObjAttr,
		26,
		GOBJ_PRIORITY_DEFAULT,
		~0,
		lbRelocGetFileData
		(
			Sprite*,
			sMNVSNetAutomatchFiles[0],
			llMNPlayersCommonTimeSelectorSprite
		),
		nGCProcessKindFunc,
		NULL,
		1
	);
	sMNVSNetAutomatchTimeGObj = gobj;

	SObjGetStruct(gobj)->pos.x = 140.0F;
	SObjGetStruct(gobj)->pos.y = 22.0F;
	SObjGetStruct(gobj)->sprite.attr &= ~SP_FASTCOPY;
	SObjGetStruct(gobj)->sprite.attr |= SP_TRANSPARENT;

	mnVSNetAutomatchMakeTimeSetting(sMNVSNetAutomatchTimeSetting);
}

// 0x801338EC
void mnVSNetAutomatchMakeWallpaper(void)
{
	GObj *gobj;
	SObj *sobj;

	gobj = gcMakeGObjSPAfter(0, NULL, 17, GOBJ_PRIORITY_DEFAULT);
	gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 26, GOBJ_PRIORITY_DEFAULT, ~0);
	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[2], llMNSelectCommonStoneBackgroundSprite));
	sobj->cms = G_TX_WRAP;
	sobj->cmt = G_TX_WRAP;
	sobj->masks = 6;
	sobj->maskt = 5;
	sobj->lrs = 300;
	sobj->lrt = 220;
	sobj->pos.x = 10.0F;
	sobj->pos.y = 10.0F;
}

// 0x80133990
void mnVSNetAutomatchMakeWallpaperCamera(void)
{
	CObj *cobj = CObjGetStruct
	(
		gcMakeCameraGObj
		(
			nGCCommonKindSceneCamera,
			NULL,
			16,
			GOBJ_PRIORITY_DEFAULT,
			lbCommonDrawSprite,
			80,
			COBJ_MASK_DLLINK(26),
			~0,
			FALSE,
			nGCProcessKindFunc,
			NULL,
			1,
			FALSE
		)
	);
	syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);
}

// 0x80133A30
void mnVSNetAutomatchLabelsProcDisplay(GObj *gobj)
{
	gDPPipeSync(gSYTaskmanDLHeads[0]++);
	gDPSetCycleType(gSYTaskmanDLHeads[0]++, G_CYC_1CYCLE);
	gDPSetCombineLERP(gSYTaskmanDLHeads[0]++, 0, 0, 0, PRIMITIVE,  0, 0, 0, PRIMITIVE,  0, 0, 0, PRIMITIVE,  0, 0, 0, PRIMITIVE);
	gDPSetRenderMode(gSYTaskmanDLHeads[0]++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
	gDPSetPrimColor(gSYTaskmanDLHeads[0]++, 0, 0, 0x57, 0x60, 0x88, 0xFF);
	gDPFillRectangle(gSYTaskmanDLHeads[0]++, 157, 136, 320, 141);
	gDPPipeSync(gSYTaskmanDLHeads[0]++);
	gDPSetRenderMode(gSYTaskmanDLHeads[0]++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
	gDPSetCycleType(gSYTaskmanDLHeads[0]++, G_CYC_1CYCLE);

	lbCommonClearExternSpriteParams();
	lbCommonDrawSObjAttr(gobj);
}

// 0x80133B74
SObj* mnVSNetAutomatchGetArrowSObj(GObj *gobj, s32 direction)
{
	if (SObjGetStruct(gobj) != NULL)
	{
		if (direction == SObjGetStruct(gobj)->user_data.s)
		{
			return SObjGetStruct(gobj);
		}
		if ((SObjGetStruct(gobj)->next != NULL) && (direction == SObjGetStruct(gobj)->next->user_data.s))
		{
			return SObjGetStruct(gobj)->next;
		}
	}
	return NULL;
}

// 0x80133BC0
void mnVSNetAutomatchLevelThreadUpdate(GObj *gobj)
{
	SObj *sobj;
	s32 player = gobj->user_data.s;
	s32 blink_wait = 10;

	while (TRUE)
	{
		blink_wait--;

		if (blink_wait == 0)
		{
			blink_wait = 10;
			gobj->flags = (gobj->flags == GOBJ_FLAG_HIDDEN) ? GOBJ_FLAG_NONE : GOBJ_FLAG_HIDDEN;
		}
		if (sMNVSNetAutomatchLevelValue == nSC1PGameDifficultyVeryEasy)
		{
			sobj = mnVSNetAutomatchGetArrowSObj(gobj, 0);

			if (sobj != NULL)
			{
				gcEjectSObj(sobj);
			}
		}
		else if (mnVSNetAutomatchGetArrowSObj(gobj, 0) == NULL)
		{
			sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[0], llMNPlayersCommonArrowLSprite));
			sobj->pos.x = 194.0F;
			sobj->pos.y = 158.0F;
			sobj->sprite.attr &= ~SP_FASTCOPY;
			sobj->sprite.attr |= SP_TRANSPARENT;
			sobj->user_data.s = 0;
		}
		if (sMNVSNetAutomatchLevelValue == nSC1PGameDifficultyVeryHard)
		{
			sobj = mnVSNetAutomatchGetArrowSObj(gobj, 1);

			if (sobj != NULL)
			{
				gcEjectSObj(sobj);
			}
		}
		else if (mnVSNetAutomatchGetArrowSObj(gobj, 1) == NULL)
		{
			sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[0], llMNPlayersCommonArrowRSprite));
			sobj->pos.x = 269.0F;
			sobj->pos.y = 158.0F;
			sobj->sprite.attr &= ~SP_FASTCOPY;
			sobj->sprite.attr |= SP_TRANSPARENT;
			sobj->user_data.s = 1;
		}
		gcSleepCurrentGObjThread(1);
	}
}

// 0x80133D9C
void mnVSNetAutomatchMakeLevel(s32 level)
{
	GObj *gobj;
	SObj *sobj;

	intptr_t offsets[/* */] =
	{
		llMNPlayersDifficultyVeryEasyTextSprite,
		llMNPlayersDifficultyEasyTextSprite,
		llMNPlayersDifficultyNormalTextSprite,
		llMNPlayersDifficultyHardTextSprite,
		llMNPlayersDifficultyVeryHardTextSprite
	};
	Vec2f pos[/* */] =
	{
		{ 204.0F, 159.0F },
		{ 219.0F, 159.0F },
		{ 209.0F, 159.0F },
		{ 219.0F, 159.0F },
		{ 205.0F, 159.0F }
	};
	SYColorRGB colors[/* */] =
	{
		{ 0x41, 0x6F, 0xE4 },
		{ 0x8D, 0xBB, 0x5A },
		{ 0xE4, 0xBE, 0x41 },
		{ 0xE4, 0x78, 0x41 },
		{ 0xE4, 0x41, 0x41 }
	};

	if (sMNVSNetAutomatchLevelGObj != NULL)
	{
		gcEjectGObj(sMNVSNetAutomatchLevelGObj);
	}
	sMNVSNetAutomatchLevelGObj = gobj = gcMakeGObjSPAfter(0, NULL, 23, GOBJ_PRIORITY_DEFAULT);
	gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 34, GOBJ_PRIORITY_DEFAULT, ~0);

	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[6], offsets[level]));
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;
	sobj->pos.x = pos[level].x;
	sobj->pos.y = pos[level].y;
	sobj->sprite.red = colors[level].r;
	sobj->sprite.green = colors[level].g;
	sobj->sprite.blue = colors[level].b;
}

// 0x80133F30
void mnVSNetAutomatchMakeLevelOption(void)
{
	GObj *gobj = gcMakeGObjSPAfter(0, NULL, 23, GOBJ_PRIORITY_DEFAULT);

	gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 34, GOBJ_PRIORITY_DEFAULT, ~0);
	gcAddGObjProcess(gobj, mnVSNetAutomatchLevelThreadUpdate, nGCProcessKindThread, 1);
	mnVSNetAutomatchMakeLevel(sMNVSNetAutomatchLevelValue);
}

// 0x80133FA4
void mnVSNetAutomatchStockThreadUpdate(GObj *gobj)
{
	SObj *sobj;
	s32 player = gobj->user_data.s;
	s32 blink_wait = 10;

	while (TRUE)
	{
		blink_wait--;

		if (blink_wait == 0)
		{
			blink_wait = 10;
			gobj->flags = (gobj->flags == GOBJ_FLAG_HIDDEN) ? GOBJ_FLAG_NONE : GOBJ_FLAG_HIDDEN;
		}
		if (sMNVSNetAutomatchStockValue == 0)
		{
			sobj = mnVSNetAutomatchGetArrowSObj(gobj, 0);

			if (sobj != NULL)
			{
				gcEjectSObj(sobj);
			}
		}
		else if (mnVSNetAutomatchGetArrowSObj(gobj, 0) == NULL)
		{
			sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[0], llMNPlayersCommonArrowLSprite));
			sobj->pos.x = 194.0F;
			sobj->pos.y = 178.0F;
			sobj->sprite.attr &= ~SP_FASTCOPY;
			sobj->sprite.attr |= SP_TRANSPARENT;
			sobj->user_data.s = 0;
		}
		if (sMNVSNetAutomatchStockValue == 4)
		{
			sobj = mnVSNetAutomatchGetArrowSObj(gobj, 1);

			if (sobj != NULL)
			{
				gcEjectSObj(sobj);
			}
		}
		else if (mnVSNetAutomatchGetArrowSObj(gobj, 1) == NULL)
		{
			sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[0], llMNPlayersCommonArrowRSprite));
			sobj->pos.x = 269.0F;
			sobj->pos.y = 178.0F;
			sobj->sprite.attr &= ~SP_FASTCOPY;
			sobj->sprite.attr |= SP_TRANSPARENT;
			sobj->user_data.s = 1;
		}
		gcSleepCurrentGObjThread(1);
	}
}

// 0x8013419C
void mnVSNetAutomatchMakeStock(s32 stock, s32 fkind)
{
	GObj *gobj;

	if (sMNVSNetAutomatchStockGObj != NULL)
	{
		gcEjectGObj(sMNVSNetAutomatchStockGObj);
	}
	sMNVSNetAutomatchStockGObj = gobj = gcMakeGObjSPAfter(0, NULL, 23, GOBJ_PRIORITY_DEFAULT);
	gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 34, GOBJ_PRIORITY_DEFAULT, ~0);

	for (stock += 1; stock > 0; stock--)
	{
		SObj *sobj;

		if (fkind == nFTKindNull)
		{
			sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[7], llFTStocksZakoSprite));
			sobj->pos.y = 179.0F;
		}
		else
		{
			FTStruct *fp = ftGetStruct(sMNVSNetAutomatchSlot.player);
#ifdef PORT
			{
				FTSprites *_spr = (FTSprites*)PORT_RESOLVE(fp->attr->sprites);
				sobj = lbCommonMakeSObjForGObj(gobj, (Sprite*)PORT_RESOLVE(_spr->stock_sprite));
				u32 *_luts = (u32*)PORT_RESOLVE(_spr->stock_luts);
				sobj->sprite.LUT = _luts[fp->costume];
			}
#else
			sobj = lbCommonMakeSObjForGObj(gobj, fp->attr->sprites->stock_sprite);
			sobj->sprite.LUT = fp->attr->sprites->stock_luts[fp->costume];
#endif
			sobj->pos.y = 178.0F;
		}
		sobj->pos.x = (stock - 1) * 12 + 207.0F;
		sobj->sprite.attr &= ~SP_FASTCOPY;
		sobj->sprite.attr |= SP_TRANSPARENT;
	}
}

// 0x8013434C
void mnVSNetAutomatchMakeStockOption(void)
{
	GObj *gobj = gcMakeGObjSPAfter(0, NULL, 23, GOBJ_PRIORITY_DEFAULT);

	gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 34, GOBJ_PRIORITY_DEFAULT, ~0);
	gcAddGObjProcess(gobj, mnVSNetAutomatchStockThreadUpdate, nGCProcessKindThread, 1);
	mnVSNetAutomatchMakeStock(sMNVSNetAutomatchStockValue, sMNVSNetAutomatchSlot.fkind);
}

void mnVSNetAutomatchLabelsMinimalProcDisplay(GObj *gobj)
{
	lbCommonClearExternSpriteParams();
	lbCommonDrawSObjAttr(gobj);
}

void mnVSNetAutomatchMakeLabelsMinimal(void)
{
	GObj *gobj;
	SObj *sobj;

	gobj = lbCommonMakeSpriteGObj(
		0,
		NULL,
		23,
		GOBJ_PRIORITY_DEFAULT,
		mnVSNetAutomatchLabelsMinimalProcDisplay,
		34,
		GOBJ_PRIORITY_DEFAULT,
		~0,
		lbRelocGetFileData(Sprite *, sMNVSNetAutomatchFiles[0], llMNPlayersCommonBackButtonSprite),
		nGCProcessKindFunc,
		NULL,
		1);

	sobj = SObjGetStruct(gobj);
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;
	sobj->pos.x = 244.0F;
	sobj->pos.y = 23.0F;
}

// 0x801343C8
void mnVSNetAutomatchMakeLabels(void)
{
	GObj *gobj;
	SObj *sobj;

	gobj = lbCommonMakeSpriteGObj
	(
		0,
		NULL,
		23,
		GOBJ_PRIORITY_DEFAULT,
		mnVSNetAutomatchLabelsProcDisplay,
		34,
		GOBJ_PRIORITY_DEFAULT,
		~0,
		lbRelocGetFileData
		(
			Sprite*,
			sMNVSNetAutomatchFiles[5],
			llMNPlayers1PMode1PlayerGameTextSprite
		),
		nGCProcessKindFunc,
		NULL,
		1
	);
	SObjGetStruct(gobj)->pos.x = 27.0F;
	SObjGetStruct(gobj)->pos.y = 24.0F;
	SObjGetStruct(gobj)->sprite.attr &= ~SP_FASTCOPY;
	SObjGetStruct(gobj)->sprite.attr |= SP_TRANSPARENT;
	SObjGetStruct(gobj)->sprite.red = 0xE3;
	SObjGetStruct(gobj)->sprite.green = 0xAC;
	SObjGetStruct(gobj)->sprite.blue = 0x04;

	mnVSNetAutomatchMakeTimeSelect(sMNVSNetAutomatchTimeSetting);

	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[0], llMNPlayersCommonBackButtonSprite));
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;
	sobj->pos.x = 244.0F;
	sobj->pos.y = 23.0F;

	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[5], llMNPlayers1PModeOptionTextSprite));
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;
	sobj->envcolor.r = 0x00;
	sobj->envcolor.g = 0x00;
	sobj->envcolor.b = 0x00;
	sobj->sprite.red = 0xAF;
	sobj->sprite.green = 0xB1;
	sobj->sprite.blue = 0xCC;
	sobj->pos.x = 180.0F;
	sobj->pos.y = 129.0F;

	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[5], llMNPlayers1PModeOptionOutlineSprite));
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;
	sobj->sprite.red = 0x57;
	sobj->sprite.green = 0x60;
	sobj->sprite.blue = 0x88;
	sobj->cms = G_TX_WRAP;
	sobj->cmt = G_TX_MIRROR;
	sobj->masks = 0;
	sobj->maskt = 5;
	sobj->lrs = 184;
	sobj->lrt = 64;
	sobj->pos.x = 128.0F;
	sobj->pos.y = 141.0F;

	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[5], llMNPlayers1PModeLevelColonTextSprite));
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;
	sobj->sprite.red = 0xC5;
	sobj->sprite.green = 0xB6;
	sobj->sprite.blue = 0xA7;
	sobj->pos.x = 145.0F;
	sobj->pos.y = 159.0F;

	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[5], llMNPlayers1PModeStockColonTextSprite));
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;
	sobj->sprite.red = 0xC5;
	sobj->sprite.green = 0xB6;
	sobj->sprite.blue = 0xA7;
	sobj->pos.x = 144.0F;
	sobj->pos.y = 179.0F;
}

// 0x801346B8
void mnVSNetAutomatchMakeLabelsCamera(void)
{
	CObj *cobj = CObjGetStruct
	(
		gcMakeCameraGObj
		(
			nGCCommonKindSceneCamera,
			NULL,
			16,
			GOBJ_PRIORITY_DEFAULT,
			lbCommonDrawSprite,
			70,
			COBJ_MASK_DLLINK(34),
			~0,
			FALSE,
			nGCProcessKindFunc,
			NULL,
			1,
			FALSE
		)
	);
	syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);
}

// 0x80134758
u32 mnVSNetAutomatchGetHiScore(s32 fkind)
{
	return gSCManagerBackupData.spgame_records[fkind].spgame_hiscore;
}

// 0x8013476C
void mnVSNetAutomatchMakeHiScore(void)
{
	GObj *gobj;
	s32 unused[2];
	SObj *sobj;

	u32 text_colors[/* */] = { 0x7E, 0x7C, 0x77 };
	u32 number_colors[/* */] = { 0x00, 0x00, 0x00, 0x7E, 0x7C, 0x77 };
	SYColorRGB difficulty_colors[/* */] =
	{
		{ 0x41, 0x6F, 0xE4 },
		{ 0x8D, 0xBB, 0x5A },
		{ 0xE4, 0xBE, 0x41 },
		{ 0xE4, 0x78, 0x41 },
		{ 0xE4, 0x41, 0x41 }
	};

	s32 best_difficulty;
	s32 fkind = mnVSNetAutomatchGetForcePuckFighterKind();

	if (sMNVSNetAutomatchHiScoreGObj != NULL)
	{
		gcEjectGObj(sMNVSNetAutomatchHiScoreGObj);
		sMNVSNetAutomatchHiScoreGObj = NULL;
	}
	if (fkind != nFTKindNull)
	{
		sMNVSNetAutomatchHiScoreGObj = gobj = gcMakeGObjSPAfter(0, NULL, 23, GOBJ_PRIORITY_DEFAULT);
		gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 26, GOBJ_PRIORITY_DEFAULT, ~0);

		mnVSNetAutomatchMakeString(gobj, "HIGH SCORE", 142.0F, 201.0F, text_colors);
		mnVSNetAutomatchMakeNumber(gobj, mnVSNetAutomatchGetHiScore(fkind), 256.0F, 198.0F, number_colors, 8, TRUE);

		best_difficulty = gSCManagerBackupData.spgame_records[fkind].spgame_best_difficulty;

		if (best_difficulty != 0)
		{
			sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[5], llMNPlayers1PModeSmashLogoSprite));
			sobj->sprite.attr &= ~SP_FASTCOPY;
			sobj->sprite.attr |= SP_TRANSPARENT;
			sobj->pos.x = 126.0F;
			sobj->pos.y = 198.0F;
			sobj->sprite.red = difficulty_colors[best_difficulty - 1].r;
			sobj->sprite.green = difficulty_colors[best_difficulty - 1].g;
			sobj->sprite.blue = difficulty_colors[best_difficulty - 1].b;
		}
	}
}

// 0x80134968
u32 mnVSNetAutomatchGetBonusCount(s32 fkind)
{
	return gSCManagerBackupData.spgame_records[fkind].spgame_total_bonuses;
}

// 0x8013497C
void mnVSNetAutomatchMakeBonusCount(void)
{
	GObj *gobj;
	s32 unused[2];
	SObj *sobj;
	u32 colors[/* */] = { 0x00, 0x00, 0x00, 0x40, 0x6F, 0xCD };
	s32 fkind = mnVSNetAutomatchGetForcePuckFighterKind();

	if (sMNVSNetAutomatchBonusesGObj != NULL)
	{
		gcEjectGObj(sMNVSNetAutomatchBonusesGObj);
		sMNVSNetAutomatchBonusesGObj = NULL;
	}
	if (fkind != nFTKindNull)
	{
		sMNVSNetAutomatchBonusesGObj = gobj = gcMakeGObjSPAfter(0, NULL, 23, GOBJ_PRIORITY_DEFAULT);
		gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 26, GOBJ_PRIORITY_DEFAULT, ~0);

		sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[5], llMNPlayers1PModeOpeningParenthesisSprite));
		sobj->sprite.attr &= ~SP_FASTCOPY;
		sobj->sprite.attr |= SP_TRANSPARENT;
		sobj->pos.x = 258.0F;
		sobj->pos.y = 199.0F;
		sobj->sprite.red = 0x40;
		sobj->sprite.green = 0x6F;
		sobj->sprite.blue = 0xCD;

		sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[5], llMNPlayers1PModeClosingParenthesisSprite));
		sobj->sprite.attr &= ~SP_FASTCOPY;
		sobj->sprite.attr |= SP_TRANSPARENT;
		sobj->pos.x = 286.0F;
		sobj->pos.y = 199.0F;
		sobj->sprite.red = 0x40;
		sobj->sprite.green = 0x6F;
		sobj->sprite.blue = 0xCD;

		mnVSNetAutomatchMakeNumber(gobj, mnVSNetAutomatchGetBonusCount(fkind), 285.0F, 198.0F, colors, 3, TRUE);
	}
}

// 0x80134B3C
void mnVSNetAutomatchMakeFighterRecord(void)
{
	mnVSNetAutomatchMakeHiScore();
	mnVSNetAutomatchMakeBonusCount();
}

// 0x80134B64
u32 mnVSNetAutomatchGetTotalHiScore(void)
{
	s32 i;
	u32 sum = 0;

	for (i = nFTKindPlayableStart; i <= nFTKindPlayableEnd; i++)
	{
		sum += mnVSNetAutomatchGetHiScore(i);
	}
	return sum;
}

// 0x80134BB4
void mnVSNetAutomatchMakeTotalHiScore(void)
{
	GObj *gobj;
	s32 unused[3];
	u32 text_colors[/* */] = { 0x7E, 0x7C, 0x77 };
	u32 number_colors[/* */] = { 0x00, 0x00, 0x00, 0x7E, 0x7C, 0x77 };

	gobj = gcMakeGObjSPAfter(0, NULL, 23, GOBJ_PRIORITY_DEFAULT);
	gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 26, GOBJ_PRIORITY_DEFAULT, ~0);
	mnVSNetAutomatchMakeString(gobj, "TOTAL HIGH SCORE", 109.0F, 211.0F, text_colors);
	mnVSNetAutomatchMakeNumber(gobj, mnVSNetAutomatchGetTotalHiScore(), 256.0F, 208.0F, number_colors, 9, TRUE);
}

// 0x80134CB8
u32 mnVSNetAutomatchGetTotalBonusCount(void)
{
	s32 i;
	u32 sum = 0;

	for (i = nFTKindPlayableStart; i <= nFTKindPlayableEnd; i++)
	{
		sum += mnVSNetAutomatchGetBonusCount(i);
	}
	return sum;
}

// 0x80134D08
void mnVSNetAutomatchMakeTotalBonusCount(void)
{
	GObj *gobj;
	s32 unused[2];
	SObj *sobj;
	u32 colors[/* */] = { 0x00, 0x00, 0x00, 0x40, 0x6F, 0xCD };

	gobj = gcMakeGObjSPAfter(0, NULL, 23, GOBJ_PRIORITY_DEFAULT);
	gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 26, GOBJ_PRIORITY_DEFAULT, ~0);

	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[5], llMNPlayers1PModeOpeningParenthesisSprite));
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;
	sobj->pos.x = 258.0F;
	sobj->pos.y = 209.0F;
	sobj->sprite.red = 0x40;
	sobj->sprite.green = 0x6F;
	sobj->sprite.blue = 0xCD;

	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[5], llMNPlayers1PModeClosingParenthesisSprite));
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;
	sobj->pos.x = 286.0F;
	sobj->pos.y = 209.0F;
	sobj->sprite.red = 0x40;
	sobj->sprite.green = 0x6F;
	sobj->sprite.blue = 0xCD;

	mnVSNetAutomatchMakeNumber(gobj, mnVSNetAutomatchGetTotalBonusCount(), 285.0F, 208.0F, colors, 3, TRUE);
}

// 0x80134E88
void mnVSNetAutomatchMakeTotalRecord(void)
{
	mnVSNetAutomatchMakeTotalHiScore();
	mnVSNetAutomatchMakeTotalBonusCount();
}

// 0x80134EB0 - Unused?
void func_vsNetAutomatch27_80134EB0(void)
{
	return;
}

// 0x80134EB8 - Unused?
void func_vsNetAutomatch27_80134EB8(void)
{
	return;
}

// 0x80134EC0
s32 mnVSNetAutomatchGetFreeCostume(s32 fkind, s32 select_button)
{
	return ftParamGetCostumeCommonID(fkind, select_button);
}

// 0x80134EE0
s32 mnVSNetAutomatchGetStatusSelected(s32 fkind)
{
	switch (fkind)
	{
	case nFTKindFox:
	case nFTKindSamus:	
		return nFTDemoStatusWin4;

	case nFTKindDonkey:
	case nFTKindLuigi:
	case nFTKindLink:
	case nFTKindCaptain:
		return nFTDemoStatusWin1;

	case nFTKindYoshi:
	case nFTKindPurin:
	case nFTKindNess:
		return nFTDemoStatusWin2;
		
	case nFTKindMario:
	case nFTKindKirby:
		return nFTDemoStatusWin3;
		
	default:
		return nFTDemoStatusWin1;
	}
}

// 0x80134F40
void mnVSNetAutomatchFighterProcUpdate(GObj *fighter_gobj)
{
	FTStruct *fp = ftGetStruct(fighter_gobj);
	s32 player = fp->player;

	if (sMNVSNetAutomatchSlot.is_fighter_selected == TRUE)
	{
		if (DObjGetStruct(fighter_gobj)->rotate.vec.f.y < F_CLC_DTOR32(0.1F))
		{
			if (sMNVSNetAutomatchSlot.is_status_selected == FALSE)
			{
				scSubsysFighterSetStatus(sMNVSNetAutomatchSlot.player, mnVSNetAutomatchGetStatusSelected(sMNVSNetAutomatchSlot.fkind));

				sMNVSNetAutomatchSlot.is_status_selected = TRUE;
			}
		}
		else
		{
			DObjGetStruct(fighter_gobj)->rotate.vec.f.y += F_CST_DTOR32(20.0F);

			if (DObjGetStruct(fighter_gobj)->rotate.vec.f.y > F_CLC_DTOR32(360.0F))
			{
				DObjGetStruct(fighter_gobj)->rotate.vec.f.y = 0.0F;

				scSubsysFighterSetStatus(sMNVSNetAutomatchSlot.player, mnVSNetAutomatchGetStatusSelected(sMNVSNetAutomatchSlot.fkind));

				sMNVSNetAutomatchSlot.is_status_selected = TRUE;
			}
		}
	}
	else
	{
		DObjGetStruct(fighter_gobj)->rotate.vec.f.y += F_CST_DTOR32(2.0F);

		if (DObjGetStruct(fighter_gobj)->rotate.vec.f.y > F_CST_DTOR32(360.0F))
		{
			DObjGetStruct(fighter_gobj)->rotate.vec.f.y -= F_CST_DTOR32(360.0F);
		}
	}
}

// 0x80135060
void mnVSNetAutomatchMakeFighter(GObj *fighter_gobj, s32 player, s32 fkind, s32 costume)
{
	f32 rot_y;
	FTDesc desc = dFTManagerDefaultFighterDesc;

	if (fkind != nFTKindNull)
	{
		if (fighter_gobj != NULL)
		{
			rot_y = DObjGetStruct(fighter_gobj)->rotate.vec.f.y;
			ftManagerDestroyFighter(fighter_gobj);
		}
		else rot_y = F_CST_DTOR32(0.0F);

		desc.fkind = fkind;
		sMNVSNetAutomatchSlot.costume = desc.costume = costume;
		desc.shade = 0;
		desc.figatree_heap = sMNVSNetAutomatchFigatreeHeap;
		desc.player = player;
#ifdef PORT
		desc.is_skip_shadow_setup = TRUE;
#endif
		sMNVSNetAutomatchSlot.player = fighter_gobj = ftManagerMakeFighter(&desc);

		gcAddGObjProcess(fighter_gobj, mnVSNetAutomatchFighterProcUpdate, nGCProcessKindFunc, 1);

		DObjGetStruct(fighter_gobj)->translate.vec.f.x = -1100.0F;
		DObjGetStruct(fighter_gobj)->translate.vec.f.y = -850.0F;

		DObjGetStruct(fighter_gobj)->rotate.vec.f.y = rot_y;

		DObjGetStruct(fighter_gobj)->scale.vec.f.x = dSCSubsysFighterScales[fkind];
		DObjGetStruct(fighter_gobj)->scale.vec.f.y = dSCSubsysFighterScales[fkind];
		DObjGetStruct(fighter_gobj)->scale.vec.f.z = dSCSubsysFighterScales[fkind];
	}
}

// 0x801351CC
void mnVSNetAutomatchMakeFighterCamera(void)
{
	CObj *cobj = CObjGetStruct
	(
		gcMakeCameraGObj
		(
			nGCCommonKindSceneCamera,
			NULL,
			16,
			GOBJ_PRIORITY_DEFAULT,
			func_80017EC0,
			20,
			COBJ_MASK_DLLINK(18) | COBJ_MASK_DLLINK(15) |
			COBJ_MASK_DLLINK(10) | COBJ_MASK_DLLINK(9),
			~0,
			TRUE,
			nGCProcessKindFunc,
			NULL,
			1,
			FALSE
		)
	);
	syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);

	cobj->vec.eye.x = 0.0F;
	cobj->vec.eye.y = 0.0F;
	cobj->vec.eye.z = 5000.0F;

	cobj->flags = COBJ_FLAG_DLBUFFERS;

	cobj->vec.at.x = 0.0F;
	cobj->vec.at.y = 0.0F;
	cobj->vec.at.z = 0.0F;

	cobj->vec.up.x = 0.0F;
	cobj->vec.up.z = 0.0F;
	cobj->vec.up.y = 1.0F;
}

// 0x801352BC
void mnVSNetAutomatchUpdateCursor(GObj *gobj, s32 player, s32 cursor_status)
{
	SObj *sobj;
	f32 start_pos_x, start_pos_y;

	SYColorRGBPair colors[/* */] =
	{
		{ { 0xE0, 0x15, 0x15 }, { 0x5B, 0x00, 0x00 } },
		{ { 0x00, 0x00, 0xFB }, { 0x00, 0x00, 0x52 } },
		{ { 0xCA, 0x94, 0x08 }, { 0x62, 0x3C, 0x00 } },
		{ { 0x00, 0x91, 0x00 }, { 0x00, 0x4F, 0x00 } }
	};
	intptr_t num_offsets[/* */] =
	{
		llMNPlayersCommon1PTextGradientSprite,
		llMNPlayersCommon2PTextGradientSprite,
		llMNPlayersCommon3PTextGradientSprite,
		llMNPlayersCommon4PTextGradientSprite
	};
	intptr_t cursor_offsets[/* */] =
	{
		llMNPlayersCommonCursorHandPointSprite,
		llMNPlayersCommonCursorHandGrabSprite,
		llMNPlayersCommonCursorHandHoverSprite
	};
	Vec2i pos[/* */] =
	{
		{ 7, 15 },
		{ 9, 10 },
		{ 9, 15 }
	};

	start_pos_x = SObjGetStruct(gobj)->pos.x;
	start_pos_y = SObjGetStruct(gobj)->pos.y;

	gcRemoveSObjAll(gobj);

	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[0], cursor_offsets[cursor_status]));
	sobj->pos.x = start_pos_x;
	sobj->pos.y = start_pos_y;
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;

	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[0], num_offsets[player]));
	sobj->pos.x = sobj->prev->pos.x + pos[cursor_status].x;
	sobj->pos.y = sobj->prev->pos.y + pos[cursor_status].y;

	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;

	sobj->sprite.red = colors[player].prim.r;
	sobj->sprite.green = colors[player].prim.g;
	sobj->sprite.blue = colors[player].prim.b;

	sobj->envcolor.r = colors[player].env.r;
	sobj->envcolor.g = colors[player].env.g;
	sobj->envcolor.b = colors[player].env.b;
}

// 0x801354CC
sb32 mnVSNetAutomatchCheckTimeArrowRInRange(GObj *gobj)
{
	f32 pos_x, pos_y;
	sb32 is_in_range;
	SObj *sobj;

	sobj = SObjGetStruct(gobj);

	pos_y = sobj->pos.y + 3.0F;

	is_in_range = ((pos_y < 12.0F) || (pos_y > 35.0F)) ? TRUE : FALSE;

	if (is_in_range != FALSE)
	{
		return FALSE;
	}
	pos_x = sobj->pos.x + 20.0F;

	is_in_range = ((pos_x >= 210.0F) && (pos_x <= 230.0F)) ? TRUE : FALSE;

	if (is_in_range != FALSE)
	{
		return TRUE;
	}
	else return FALSE;
}

// 0x80135594
sb32 mnVSNetAutomatchCheckTimeArrowLInRange(GObj *gobj)
{
	f32 pos_x, pos_y;
	sb32 is_in_range;
	SObj *sobj;

	sobj = SObjGetStruct(gobj);

	pos_y = sobj->pos.y + 3.0F;

	is_in_range = ((pos_y < 12.0F) || (pos_y > 35.0F)) ? TRUE : FALSE;

	if (is_in_range != FALSE)
	{
		return FALSE;
	}
	pos_x = sobj->pos.x + 20.0F;

	is_in_range = ((pos_x >= 140.0F) && (pos_x <= 160.0F)) ? TRUE : FALSE;

	if (is_in_range != FALSE)
	{
		return TRUE;
	}
	else return FALSE;
}

// 0x8013565C
sb32 mnVSNetAutomatchCheckBackInRange(GObj *gobj)
{
	f32 pos_x, pos_y;
	sb32 is_in_range;
	SObj *sobj;

	sobj = SObjGetStruct(gobj);

	pos_y = sobj->pos.y + 3.0F;

	is_in_range = ((pos_y < 13.0F) || (pos_y > 34.0F)) ? TRUE : FALSE;

	if (is_in_range != FALSE)
	{
		return FALSE;
	}
	pos_x = sobj->pos.x + 20.0F;

	is_in_range = ((pos_x >= 244.0F) && (pos_x <= 292.0F)) ? TRUE : FALSE;

	if (is_in_range != FALSE)
	{
		return TRUE;
	}
	else return FALSE;
}

// 0x80135724
sb32 mnVSNetAutomatchCheckPuckInRange(GObj *gobj, s32 cursor_player, s32 player)
{
	f32 pos_x, pos_y;
	sb32 is_in_range;
	SObj *sobj = SObjGetStruct(gobj);
	SObj *puck_sobj = SObjGetStruct(sMNVSNetAutomatchSlot.puck);

	pos_x = sobj->pos.x + 25.0F;
	is_in_range = ((pos_x >= puck_sobj->pos.x) && (pos_x <= puck_sobj->pos.x + 26.0F)) ? TRUE : FALSE;

	if (is_in_range != FALSE)
	{
		pos_y = sobj->pos.y + 3.0F;
		is_in_range = ((pos_y >= puck_sobj->pos.y) && (pos_y <= puck_sobj->pos.y + 24.0F)) ? TRUE : FALSE;

		if (is_in_range != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

// 0x801357FC - Unused?
void func_vsNetAutomatch27_801357FC(void)
{
	return;
}

// 0x80135804
void mnVSNetAutomatchUpdateFighter(s32 player)
{
	s32 is_skip_fighter = FALSE;

#ifdef PORT
	s32 costume;
	sb32 fkind_changed = FALSE;
#endif
	if ((sMNVSNetAutomatchSlot.fkind == nFTKindNull) && (sMNVSNetAutomatchSlot.is_selected == FALSE))
	{
		sMNVSNetAutomatchSlot.player->flags = GOBJ_FLAG_HIDDEN;
#ifdef PORT
		sMNVSNetAutomatchSlot.fighter_update_fkind = nFTKindNull;
		sMNVSNetAutomatchSlot.record_update_fkind = nFTKindNull;
#endif
		is_skip_fighter = TRUE;
	}
	if (is_skip_fighter == FALSE)
	{
#ifdef PORT
		costume = mnVSNetAutomatchGetFreeCostume(sMNVSNetAutomatchSlot.fkind, 0);

		if (
			(sMNVSNetAutomatchSlot.player != NULL) &&
			(ftGetStruct(sMNVSNetAutomatchSlot.player)->fkind == sMNVSNetAutomatchSlot.fkind))
		{
			if (sMNVSNetAutomatchSlot.costume != costume)
			{
				sMNVSNetAutomatchSlot.costume = costume;
				ftParamInitAllParts(sMNVSNetAutomatchSlot.player, costume, 0);
			}
		}
		else
		{
			mnVSNetAutomatchMakeFighter(sMNVSNetAutomatchSlot.player, player, sMNVSNetAutomatchSlot.fkind, costume);
			fkind_changed = TRUE;
		}

		if ((fkind_changed != FALSE) || (sMNVSNetAutomatchSlot.fighter_update_fkind != sMNVSNetAutomatchSlot.fkind))
		{
			sMNVSNetAutomatchSlot.fighter_update_fkind = sMNVSNetAutomatchSlot.fkind;
			sMNVSNetAutomatchSlot.record_update_fkind = sMNVSNetAutomatchSlot.fkind;
#endif			
		mnVSNetAutomatchMakeFighter(sMNVSNetAutomatchSlot.player, player, sMNVSNetAutomatchSlot.fkind, mnVSNetAutomatchGetFreeCostume(sMNVSNetAutomatchSlot.fkind, 0));
#ifdef PORT
		}
#endif
		sMNVSNetAutomatchSlot.player->flags = GOBJ_FLAG_NONE;
		sMNVSNetAutomatchSlot.is_status_selected = FALSE;
	}
}

// 0x801358BC - Unused?
void func_vsNetAutomatch27_801358BC(void)
{
	return;
}

// 0x801358C4
void mnVSNetAutomatchUpdateNameAndEmblem(s32 player)
{
	if ((sMNVSNetAutomatchSlot.fkind == nFTKindNull) && (sMNVSNetAutomatchSlot.is_selected == FALSE))
	{
		sMNVSNetAutomatchSlot.name_emblem_gobj->flags = GOBJ_FLAG_HIDDEN;
	}
	else
	{
		sMNVSNetAutomatchSlot.name_emblem_gobj->flags = GOBJ_FLAG_NONE;
#ifdef PORT
		if (sMNVSNetAutomatchSlot.name_emblem_fkind != sMNVSNetAutomatchSlot.fkind)
		{
			sMNVSNetAutomatchSlot.name_emblem_fkind = sMNVSNetAutomatchSlot.fkind;
		mnVSNetAutomatchMakeNameAndEmblem(sMNVSNetAutomatchSlot.name_emblem_gobj, player, sMNVSNetAutomatchSlot.fkind);
		}
#else
		mnVSNetAutomatchMakeNameAndEmblem(sMNVSNetAutomatchSlot.name_emblem_gobj, player, sMNVSNetAutomatchSlot.fkind);
#endif
	}
}

// 0x80135924
void mnVSNetAutomatchDestroyPortraitFlash(s32 player)
{
	GObj *gobj = sMNVSNetAutomatchSlot.flash;

	if (gobj != NULL)
	{
		sMNVSNetAutomatchSlot.flash = NULL;
		gcEjectGObj(gobj);
	}
}

// 0x8013595C
void mnVSNetAutomatchPortraitFlashThreadUpdate(GObj *gobj)
{
	s32 length = 16;
	s32 wait_tics = 1;

	while (TRUE)
	{
		length--, wait_tics--;

		if (length == 0)
		{
			mnVSNetAutomatchDestroyPortraitFlash(gobj->user_data.s);
		}
		if (wait_tics == 0)
		{
			wait_tics = 1;
			gobj->flags = (gobj->flags == GOBJ_FLAG_HIDDEN) ? GOBJ_FLAG_NONE : GOBJ_FLAG_HIDDEN;
		}
		gcSleepCurrentGObjThread(1);
	}
}

// 0x801359FC
void mnVSNetAutomatchMakePortraitFlash(s32 player)
{
	GObj *gobj;
	SObj *sobj;
	s32 portrait = mnVSNetAutomatchGetPortrait(sMNVSNetAutomatchSlot.fkind);

	mnVSNetAutomatchDestroyPortraitFlash(player);

	sMNVSNetAutomatchSlot.flash = gobj = gcMakeGObjSPAfter(0, NULL, 26, GOBJ_PRIORITY_DEFAULT);
	gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 33, GOBJ_PRIORITY_DEFAULT, ~0);
	gobj->user_data.s = player;
	gcAddGObjProcess(gobj, mnVSNetAutomatchPortraitFlashThreadUpdate, nGCProcessKindThread, 1);

	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[4], llMNPlayersPortraitsWhiteSquareSprite));
	sobj->pos.x = (((portrait >= 6) ? portrait - 6 : portrait) * 45) + 26;
	sobj->pos.y = (((portrait >= 6) ? 1 : 0) * 43) + 37;
}

// 0x80135B30
void mnVSNetAutomatchAnnounceFighter(s32 player, s32 slot)
{
	u16 announce_names[/* */] =
	{
		nSYAudioVoiceAnnounceMario,
		nSYAudioVoiceAnnounceFox,
		nSYAudioVoiceAnnounceDonkey,
		nSYAudioVoiceAnnounceSamus,
		nSYAudioVoiceAnnounceLuigi,
		nSYAudioVoiceAnnounceLink,
		nSYAudioVoiceAnnounceYoshi,
		nSYAudioVoiceAnnounceCaptain,
		nSYAudioVoiceAnnounceKirby,
		nSYAudioVoiceAnnouncePikachu,
		nSYAudioVoiceAnnouncePurin,
		nSYAudioVoiceAnnounceNess
	};

	if (sMNVSNetAutomatchSlot.p_sfx != NULL)
	{
		if ((sMNVSNetAutomatchSlot.p_sfx->sfx_id != 0) && (sMNVSNetAutomatchSlot.p_sfx->sfx_id == sMNVSNetAutomatchSlot.sfx_id))
		{
			func_80026738_27338(sMNVSNetAutomatchSlot.p_sfx);
		}
	}
	func_800269C0_275C0(nSYAudioFGMMarioDash);

	sMNVSNetAutomatchSlot.p_sfx = func_800269C0_275C0(announce_names[sMNVSNetAutomatchSlot.fkind]);

	if (sMNVSNetAutomatchSlot.p_sfx != NULL)
	{
		sMNVSNetAutomatchSlot.sfx_id = sMNVSNetAutomatchSlot.p_sfx->sfx_id;
	}
}

// 0x80135BFC - Unused?
void func_vsNetAutomatch27_80135BFC(void)
{
	return;
}

// 0x80135C04
sb32 mnVSNetAutomatchCheckSelectFighter(GObj *gobj, s32 player, s32 unused, s32 select_button)
{
	if (sMNVSNetAutomatchSlot.cursor_status != nMNPlayersCursorStatusGrab)
	{
		return FALSE;
	}
	else if (sMNVSNetAutomatchSlot.fkind != nFTKindNull)
	{
		mnVSNetAutomatchSelectFighterPuck(player, select_button);
		sMNVSNetAutomatchSlot.recall_end_tic = sMNVSNetAutomatchTotalTimeTics + 30;

		return TRUE;
	}
	else func_800269C0_275C0(nSYAudioFGMMenuDenied);

	return FALSE;
}

// 0x80135C88
void mnVSNetAutomatchUpdateCursorGrabPriorities(s32 player, s32 puck)
{
	u32 priorities[/* */] = { 6, 4, 2, 0 };

	gcMoveGObjDL(sMNVSNetAutomatchSlot.puck, 30, priorities[player] + 1);
}

// 0x80135CF4
void mnVSNetAutomatchUpdateCursorPlacementPriorities(s32 player)
{
	u32 priorities[/* */] = { 3, 2, 1, 0 };

	gcMoveGObjDL(sMNVSNetAutomatchSlot.puck, 31, priorities[player]);
}

// 0x80135D58
void mnVSNetAutomatchSetCursorPuckOffset(s32 player)
{
	sMNVSNetAutomatchSlot.cursor_pickup_x = SObjGetStruct(sMNVSNetAutomatchSlot.puck)->pos.x - 11.0F;
	sMNVSNetAutomatchSlot.cursor_pickup_y = SObjGetStruct(sMNVSNetAutomatchSlot.puck)->pos.y - -14.0F;
}

// 0x80135D9C
void mnVSNetAutomatchSetCursorGrab(s32 player)
{
	sMNVSNetAutomatchSlot.holder_player = player;
	sMNVSNetAutomatchSlot.is_selected = FALSE;
	sMNVSNetAutomatchSlot.cursor_status = nMNPlayersCursorStatusGrab;
	sMNVSNetAutomatchSlot.held_player = player;
	sMNVSNetAutomatchSlot.is_fighter_selected = FALSE;

	mnVSNetAutomatchUpdateFighter(player);
	mnVSNetAutomatchUpdateCursorGrabPriorities(player, player);
	mnVSNetAutomatchSetCursorPuckOffset(player);
	mnVSNetAutomatchUpdateCursor(sMNVSNetAutomatchSlot.cursor, player, sMNVSNetAutomatchSlot.cursor_status);

	sMNVSNetAutomatchSlot.is_cursor_adjusting = TRUE;

	func_800269C0_275C0(nSYAudioFGMSamusDash);

	mnVSNetAutomatchDestroyPortraitFlash(player);
	mnVSNetAutomatchUpdateNameAndEmblem(player);
}

// 0x80135E2C
sb32 mnVSNetAutomatchCheckCursorPuckGrab(GObj *gobj, s32 player)
{
	MNPlayersSlot1PGame *pslot = &sMNVSNetAutomatchSlot;

	if ((sMNVSNetAutomatchTotalTimeTics < sMNVSNetAutomatchSlot.recall_end_tic) || (sMNVSNetAutomatchSlot.is_recalling != FALSE))
	{
		return FALSE;
	}
	else if (sMNVSNetAutomatchSlot.cursor_status != nMNPlayersCursorStatusHover)
	{
		return FALSE;
	}
	else if ((sMNVSNetAutomatchSlot.holder_player == GMCOMMON_PLAYERS_MAX) && (mnVSNetAutomatchCheckPuckInRange(gobj, player, player) != FALSE))
	{
		sMNVSNetAutomatchSlot.holder_player = player;
		sMNVSNetAutomatchSlot.is_selected = FALSE;
		sMNVSNetAutomatchSlot.cursor_status = nMNPlayersCursorStatusGrab;
		pslot->held_player = player;
		sMNVSNetAutomatchSlot.is_fighter_selected = FALSE;

		mnVSNetAutomatchUpdateFighter(player);
		mnVSNetAutomatchUpdateCursorGrabPriorities(player, player);
		mnVSNetAutomatchSetCursorPuckOffset(player);
		mnVSNetAutomatchUpdateCursor(gobj, player, sMNVSNetAutomatchSlot.cursor_status);

		sMNVSNetAutomatchSlot.is_cursor_adjusting = TRUE;

		func_800269C0_275C0(nSYAudioFGMSamusDash);

		mnVSNetAutomatchDestroyPortraitFlash(player);
		mnVSNetAutomatchUpdateNameAndEmblem(player);

		return TRUE;
	}
	else return FALSE;
}

// 0x80135F34
s32 mnVSNetAutomatchGetForcePuckFighterKind(void)
{
	SObj *sobj = SObjGetStruct(sMNVSNetAutomatchSlot.puck);
	s32 pos_x = (s32) sobj->pos.x + 13;
	s32 pos_y = (s32) sobj->pos.y + 12;
	s32 fkind;
	sb32 is_in_range = ((pos_y > 35) && (pos_y < 79)) ? TRUE : FALSE;

	if (is_in_range != FALSE)
	{
		is_in_range = ((pos_x > 24) && (pos_x < 295)) ? TRUE : FALSE;

		if (is_in_range != FALSE)
		{
			return mnVSNetAutomatchGetFighterKind((pos_x - 25) / 45);
		}
	}
	is_in_range = ((pos_y > 78) && (pos_y < 122)) ? TRUE : FALSE;

	if (is_in_range != FALSE)
	{
		is_in_range = ((pos_x > 24) && (pos_x < 295)) ? TRUE : FALSE;

		if (is_in_range != FALSE)
		{
			return mnVSNetAutomatchGetFighterKind(((pos_x - 25) / 45) + 6);
		}
	}
	return nFTKindNull;
}

// 0x80136050
s32 mnVSNetAutomatchGetPuckFighterKind(s32 player)
{
	SObj *sobj = SObjGetStruct(sMNVSNetAutomatchSlot.puck);
	s32 pos_x = (s32) sobj->pos.x + 13;
	s32 pos_y = (s32) sobj->pos.y + 12;
	s32 fkind;
	sb32 is_in_range = ((pos_y > 35) && (pos_y < 79)) ? TRUE : FALSE;

	if (is_in_range != FALSE)
	{
		is_in_range = ((pos_x > 24) && (pos_x < 295)) ? TRUE : FALSE;

		if (is_in_range != FALSE)
		{
			fkind = mnVSNetAutomatchGetFighterKind((pos_x - 25) / 45);

			if ((mnVSNetAutomatchCheckFighterCrossed(fkind) != FALSE) || (mnVSNetAutomatchCheckFighterLocked(fkind) != FALSE))
			{
				return nFTKindNull;
			}
			else return fkind;
		}
	}
	is_in_range = ((pos_y > 78) && (pos_y < 122)) ? TRUE : FALSE;

	if (is_in_range != FALSE)
	{
		is_in_range = ((pos_x > 24) && (pos_x < 295)) ? TRUE : FALSE;

		if (is_in_range != FALSE)
		{
			fkind = mnVSNetAutomatchGetFighterKind(((pos_x - 25) / 45) + 6);

			if ((mnVSNetAutomatchCheckFighterCrossed(fkind) != FALSE) || (mnVSNetAutomatchCheckFighterLocked(fkind) != FALSE))
			{
				return nFTKindNull;
			}
			else return fkind;
		}
	}
	return nFTKindNull;
}

// 0x801361C8
void mnVSNetAutomatchAdjustCursor(GObj *gobj, s32 player)
{
	s32 unused;
	Vec2i pos[/* */] =
	{
		{ 7, 15 },
		{ 9, 10 },
		{ 9, 15 }
	};
	f32 delta;
	sb32 is_in_range;

	if (sMNVSNetAutomatchSlot.is_cursor_adjusting != FALSE)
	{
		delta = (sMNVSNetAutomatchSlot.cursor_pickup_x - SObjGetStruct(sMNVSNetAutomatchSlot.cursor)->pos.x) / 5.0F;
		is_in_range = ((delta >= -1.0F) && (delta <= 1.0F)) ? TRUE : FALSE;

		if (is_in_range != FALSE)
		{
			SObjGetStruct(sMNVSNetAutomatchSlot.cursor)->pos.x = sMNVSNetAutomatchSlot.cursor_pickup_x;
		}
		else SObjGetStruct(sMNVSNetAutomatchSlot.cursor)->pos.x += delta;

		delta = (sMNVSNetAutomatchSlot.cursor_pickup_y - SObjGetStruct(sMNVSNetAutomatchSlot.cursor)->pos.y) / 5.0F;
		is_in_range = ((delta >= -1.0F) && (delta <= 1.0F)) ? TRUE : FALSE;

		if (is_in_range != FALSE)
		{
			SObjGetStruct(sMNVSNetAutomatchSlot.cursor)->pos.y = sMNVSNetAutomatchSlot.cursor_pickup_y;
		}
		else SObjGetStruct(sMNVSNetAutomatchSlot.cursor)->pos.y += delta;

		if
		(
			(SObjGetStruct(sMNVSNetAutomatchSlot.cursor)->pos.x == sMNVSNetAutomatchSlot.cursor_pickup_x) &&
			(SObjGetStruct(sMNVSNetAutomatchSlot.cursor)->pos.y == sMNVSNetAutomatchSlot.cursor_pickup_y)
		)
		{
			sMNVSNetAutomatchSlot.is_cursor_adjusting = FALSE;
		}
		SObjGetStruct(gobj)->next->pos.x = SObjGetStruct(gobj)->pos.x + pos[sMNVSNetAutomatchSlot.cursor_status].x;
		SObjGetStruct(gobj)->next->pos.y = SObjGetStruct(gobj)->pos.y + pos[sMNVSNetAutomatchSlot.cursor_status].y;
	}
	else if (sMNVSNetAutomatchSlot.is_recalling == FALSE)
	{
		is_in_range = ((gSYControllerDevices[player].stick_range.x < -8) || (gSYControllerDevices[player].stick_range.x > 8)) ? TRUE : FALSE;

		if (is_in_range != FALSE)
		{
			delta = (gSYControllerDevices[player].stick_range.x / 20.0F) + SObjGetStruct(gobj)->pos.x;
			is_in_range = ((delta >= 0.0F) && (delta <= 280.0F)) ? TRUE : FALSE;

			if (is_in_range != FALSE)
			{
				SObjGetStruct(gobj)->pos.x = delta;
				SObjGetStruct(gobj)->next->pos.x = SObjGetStruct(gobj)->pos.x + pos[sMNVSNetAutomatchSlot.cursor_status].x;
			}
		}
		is_in_range = ((gSYControllerDevices[player].stick_range.y < -8) || (gSYControllerDevices[player].stick_range.y > 8)) ? TRUE : FALSE;

		if (is_in_range != FALSE)
		{
			delta = (gSYControllerDevices[player].stick_range.y / -20.0F) + SObjGetStruct(gobj)->pos.y;
			is_in_range = ((delta >= 10.0F) && (delta <= 205.0F)) ? TRUE : FALSE;

			if (is_in_range != FALSE)
			{
				SObjGetStruct(gobj)->pos.y = delta;
				SObjGetStruct(gobj)->next->pos.y = SObjGetStruct(gobj)->pos.y + pos[sMNVSNetAutomatchSlot.cursor_status].y;
			}
		}
	}
}

// 0x80136540
void mnVSNetAutomatchUpdateCursorNoRecall(GObj *gobj, s32 player)
{
	s32 i;

	if ((SObjGetStruct(gobj)->pos.y > 124.0F) || (SObjGetStruct(gobj)->pos.y < 38.0F))
	{
		if (sMNVSNetAutomatchSlot.cursor_status != nMNPlayersCursorStatusPointer)
		{
			mnVSNetAutomatchUpdateCursor(gobj, player, nMNPlayersCursorStatusPointer);
			sMNVSNetAutomatchSlot.cursor_status = nMNPlayersCursorStatusPointer;
		}
	}
	else if (sMNVSNetAutomatchSlot.held_player == -1)
	{
		if (sMNVSNetAutomatchSlot.cursor_status != nMNPlayersCursorStatusHover)
		{
			mnVSNetAutomatchUpdateCursor(gobj, player, nMNPlayersCursorStatusHover);
			sMNVSNetAutomatchSlot.cursor_status = nMNPlayersCursorStatusHover;
		}
	}
	else if (sMNVSNetAutomatchSlot.cursor_status != nMNPlayersCursorStatusGrab)
	{
		mnVSNetAutomatchUpdateCursor(gobj, player, nMNPlayersCursorStatusGrab);
		sMNVSNetAutomatchSlot.cursor_status = nMNPlayersCursorStatusGrab;
	}
	if ((sMNVSNetAutomatchSlot.cursor_status == nMNPlayersCursorStatusPointer) && (sMNVSNetAutomatchSlot.is_selected != FALSE))
	{
		for (i = 0; i < GMCOMMON_PLAYERS_MAX; i++)
		{
			if ((sMNVSNetAutomatchSlot.is_selected == TRUE) && (mnVSNetAutomatchCheckPuckInRange(gobj, player, i) != FALSE))
			{
				mnVSNetAutomatchUpdateCursor(gobj, player, nMNPlayersCursorStatusHover);
				sMNVSNetAutomatchSlot.cursor_status = nMNPlayersCursorStatusHover;

				break;
			}
		}
	}
}

// 0x801366C4
sb32 mnVSNetAutomatchCheckLevelArrowRInRange(GObj *gobj)
{
	f32 pos_x, pos_y;
	s32 is_in_range;
	SObj *sobj;

	sobj = SObjGetStruct(gobj);

	pos_x = sobj->pos.x + 20.0F;

	is_in_range = (pos_x >= 258.0F) && (pos_x <= 280.0F) ? TRUE : FALSE;

	if (is_in_range != FALSE)
	{
		pos_y = sobj->pos.y + 3.0F;

		is_in_range = (pos_y >= 155.0F) && (pos_y <= 174.0F) ? TRUE : FALSE;

		if (is_in_range != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

// 0x80136788
sb32 mnVSNetAutomatchCheckLevelArrowLInRange(GObj *gobj)
{
	f32 pos_x, pos_y;
	s32 is_in_range;
	SObj *sobj;

	sobj = SObjGetStruct(gobj);

	pos_x = sobj->pos.x + 20.0F;

	is_in_range = (pos_x >= 190.0F) && (pos_x <= 212.0F) ? TRUE : FALSE;

	if (is_in_range != FALSE)
	{
		pos_y = sobj->pos.y + 3.0F;

		is_in_range = (pos_y >= 155.0F) && (pos_y <= 174.0F) ? TRUE : FALSE;

		if (is_in_range != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

// 0x8013684C
sb32 mnVSNetAutomatchCheckLevelArrowPress(GObj *gobj)
{
	if (mnVSNetAutomatchCheckLevelArrowRInRange(gobj) != FALSE)
	{
		if (sMNVSNetAutomatchLevelValue < nSC1PGameDifficultyVeryHard)
		{
			func_800269C0_275C0(nSYAudioFGMMenuScroll2);

			mnVSNetAutomatchMakeLevel(++sMNVSNetAutomatchLevelValue);
		}
		return TRUE;
	}
	else if (mnVSNetAutomatchCheckLevelArrowLInRange(gobj) != FALSE)
	{
		if (sMNVSNetAutomatchLevelValue > nSC1PGameDifficultyVeryEasy)
		{
			func_800269C0_275C0(nSYAudioFGMMenuScroll2);

			mnVSNetAutomatchMakeLevel(--sMNVSNetAutomatchLevelValue);
		}
		return TRUE;
	}
	else return FALSE;
}

// 0x801368FC
sb32 mnVSNetAutomatchCheckStockArrowRInRange(GObj *gobj)
{
	f32 pos_x, pos_y;
	s32 is_in_range;
	SObj *sobj;

	sobj = SObjGetStruct(gobj);

	pos_x = sobj->pos.x + 20.0F;

	is_in_range = (pos_x >= 258.0F) && (pos_x <= 280.0F) ? TRUE : FALSE;

	if (is_in_range != FALSE)
	{
		pos_y = sobj->pos.y + 3.0F;

		is_in_range = (pos_y >= 175.0F) && (pos_y <= 194.0F) ? TRUE : FALSE;

		if (is_in_range != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

// 0x801369C0
sb32 mnVSNetAutomatchCheckStockArrowLInRange(GObj *gobj)
{
	f32 pos_x, pos_y;
	s32 is_in_range;
	SObj *sobj;

	sobj = SObjGetStruct(gobj);

	pos_x = sobj->pos.x + 20.0F;

	is_in_range = (pos_x >= 190.0F) && (pos_x <= 212.0F) ? TRUE : FALSE;

	if (is_in_range != FALSE)
	{
		pos_y = sobj->pos.y + 3.0F;

		is_in_range = (pos_y >= 175.0F) && (pos_y <= 194.0F) ? TRUE : FALSE;

		if (is_in_range != FALSE)
		{
			return TRUE;
		}
	}
	return FALSE;
}

// 0x80136A84
sb32 mnVSNetAutomatchCheckStockArrowPress(GObj *gobj)
{
	if (mnVSNetAutomatchCheckStockArrowRInRange(gobj))
	{
		if (sMNVSNetAutomatchStockValue < 4)
		{
			func_800269C0_275C0(nSYAudioFGMMenuScroll2);

			mnVSNetAutomatchMakeStock(++sMNVSNetAutomatchStockValue, sMNVSNetAutomatchSlot.fkind);
		}
		return TRUE;
	}
	else if (mnVSNetAutomatchCheckStockArrowLInRange(gobj))
	{
		if (sMNVSNetAutomatchStockValue > 0)
		{
			func_800269C0_275C0(nSYAudioFGMMenuScroll2);

			mnVSNetAutomatchMakeStock(--sMNVSNetAutomatchStockValue, sMNVSNetAutomatchSlot.fkind);
		}
		return TRUE;
	}
	else return FALSE;
}

// 0x80136B44
void mnVSNetAutomatchUpdateCostume(s32 player, s32 select_button)
{
	s32 costume = ftParamGetCostumeCommonID(sMNVSNetAutomatchSlot.fkind, select_button);

	ftParamInitAllParts(sMNVSNetAutomatchSlot.player, costume, 0);

	sMNVSNetAutomatchSlot.costume = costume;

	mnVSNetAutomatchMakeStock(sMNVSNetAutomatchStockValue, sMNVSNetAutomatchSlot.fkind);

	func_800269C0_275C0(nSYAudioFGMMenuScroll2);
}

// 0x80136BAC
sb32 mnVSNetAutomatchCheckManFighterSelected(s32 player)
{
	if (sMNVSNetAutomatchSlot.is_selected)
	{
		return TRUE;
	}
	else return FALSE;
}

// 0x80136BD4
void mnVSNetAutomatchRecallPuck(s32 player)
{
	sMNVSNetAutomatchSlot.is_fighter_selected = FALSE;
	sMNVSNetAutomatchSlot.is_selected = FALSE;
	sMNVSNetAutomatchSlot.is_recalling = TRUE;
	sMNVSNetAutomatchSlot.recall_tics = 0;
	sMNVSNetAutomatchSlot.recall_start_x = SObjGetStruct(sMNVSNetAutomatchSlot.puck)->pos.x;
	sMNVSNetAutomatchSlot.recall_start_y = SObjGetStruct(sMNVSNetAutomatchSlot.puck)->pos.y;

	sMNVSNetAutomatchSlot.recall_end_x = SObjGetStruct(sMNVSNetAutomatchSlot.cursor)->pos.x + 20.0F;

	if (sMNVSNetAutomatchSlot.recall_end_x > 280.0F)
	{
		sMNVSNetAutomatchSlot.recall_end_x = 280.0F;
	}
	sMNVSNetAutomatchSlot.recall_end_y = SObjGetStruct(sMNVSNetAutomatchSlot.cursor)->pos.y + -15.0F;

	if (sMNVSNetAutomatchSlot.recall_end_y < 10.0F)
	{
		sMNVSNetAutomatchSlot.recall_end_y = 10.0F;
	}
	if (sMNVSNetAutomatchSlot.recall_end_y < sMNVSNetAutomatchSlot.recall_start_y)
	{
		sMNVSNetAutomatchSlot.recall_mid_y = sMNVSNetAutomatchSlot.recall_end_y - 20.0F;
	}
	else sMNVSNetAutomatchSlot.recall_mid_y = sMNVSNetAutomatchSlot.recall_start_y - 20.0F;
}

// 0x80136CB8
void mnVSNetAutomatchBackTo1PMode(void)
{
	gSCManagerSceneData.scene_prev = gSCManagerSceneData.scene_curr;
	gSCManagerSceneData.scene_curr = nSCKindVSOnline;

	mnVSNetAutomatchSetSceneData();
	syAudioStopBGMAll();
	func_800266A0_272A0();
	syTaskmanSetLoadScene();
}

// 0x80136D04
void mnVSNetAutomatchDetectBack(s32 player)
{
	if ((sMNVSNetAutomatchTotalTimeTics >= 10) && (gSYControllerDevices[player].button_tap & B_BUTTON))
	{
		mnVSNetAutomatchBackTo1PMode();
	}
}

// 0x80136D58
void mnVSNetAutomatchCursorProcUpdate(GObj *gobj)
{
	s32 unused[5];
	s32 player = gobj->user_data.s;

	mnVSNetAutomatchAdjustCursor(gobj, player);

	if
	(
		(gSYControllerDevices[player].button_tap & A_BUTTON) &&
		(mnVSNetAutomatchCheckSelectFighter(gobj, player, sMNVSNetAutomatchSlot.held_player, 0) == FALSE) &&
		(mnVSNetAutomatchCheckCursorPuckGrab(gobj, player) == FALSE)
	)
	{
		if (mnVSNetAutomatchCheckBackInRange(gobj) != FALSE)
		{
			mnVSNetAutomatchBackTo1PMode();
			func_800269C0_275C0(nSYAudioFGMMenuScroll2);
		}
	}
	if
	(
		(gSYControllerDevices[player].button_tap & U_CBUTTONS) &&
		(mnVSNetAutomatchCheckSelectFighter(gobj, player, sMNVSNetAutomatchSlot.held_player, 0) == FALSE) &&
		(sMNVSNetAutomatchSlot.is_fighter_selected != FALSE)
	)
	{
		mnVSNetAutomatchUpdateCostume(player, 0);
	}
	if
	(
		(gSYControllerDevices[player].button_tap & R_CBUTTONS) &&
		(mnVSNetAutomatchCheckSelectFighter(gobj, player, sMNVSNetAutomatchSlot.held_player, 1) == FALSE) &&
		(sMNVSNetAutomatchSlot.is_fighter_selected != FALSE)
	)
	{
		mnVSNetAutomatchUpdateCostume(player, 1);
	}
	if
	(
		(gSYControllerDevices[player].button_tap & D_CBUTTONS) &&
		(mnVSNetAutomatchCheckSelectFighter(gobj, player, sMNVSNetAutomatchSlot.held_player, 2) == FALSE) &&
		(sMNVSNetAutomatchSlot.is_fighter_selected != FALSE)
	)
	{
		mnVSNetAutomatchUpdateCostume(player, 2);
	}
	if
	(
		(gSYControllerDevices[player].button_tap & L_CBUTTONS) &&
		(mnVSNetAutomatchCheckSelectFighter(gobj, player, sMNVSNetAutomatchSlot.held_player, 3) == FALSE) &&
		(sMNVSNetAutomatchSlot.is_fighter_selected != FALSE)
	)
	{
		mnVSNetAutomatchUpdateCostume(player, 3);
	}
	if ((gSYControllerDevices[player].button_tap & B_BUTTON) && (mnVSNetAutomatchCheckManFighterSelected(player) != FALSE))
	{
		mnVSNetAutomatchRecallPuck(player);
	}
	if (sMNVSNetAutomatchSlot.is_recalling == FALSE)
	{
		mnVSNetAutomatchDetectBack(player);
	}
	if (sMNVSNetAutomatchSlot.is_recalling == FALSE)
	{
		mnVSNetAutomatchUpdateCursorNoRecall(gobj, player);
	}
}

// 0x80138C0C
intptr_t dMNVSNetAutomatchPuckSpriteOffsets[/* */] =
{
	llMNPlayersCommon1PPuckSprite,
	llMNPlayersCommon2PPuckSprite,
	llMNPlayersCommon3PPuckSprite,
	llMNPlayersCommon4PPuckSprite,
	llMNPlayersCommonCPPuckSprite
};

// 0x8013702C - Unused?
void func_vsNetAutomatch27_8013702C(void)
{
	return;
}

// 0x80137034
void mnVSNetAutomatchCenterPuckInPortrait(GObj *gobj, s32 fkind)
{
	s32 portrait = mnVSNetAutomatchGetPortrait(fkind);

	if (portrait >= 6)
	{
		SObjGetStruct(gobj)->pos.x = (portrait * 45) - (6 * 45) + 36;
		SObjGetStruct(gobj)->pos.y = 89.0F;
	}
	else
	{
		SObjGetStruct(gobj)->pos.x = (portrait * 45) + 36;
		SObjGetStruct(gobj)->pos.y = 46.0F;
	}
}

// 0x801370E4 - Unused?
void func_vsNetAutomatch27_801370E4(void)
{
	return;
}

// 0x801370EC
void mnVSNetAutomatchMovePuck(s32 player)
{
	SObjGetStruct(sMNVSNetAutomatchSlot.puck)->pos.x += sMNVSNetAutomatchSlot.puck_vel_x;
	SObjGetStruct(sMNVSNetAutomatchSlot.puck)->pos.y += sMNVSNetAutomatchSlot.puck_vel_y;
}

// 0x8013712C
void mnVSNetAutomatchPuckProcUpdate(GObj *gobj)
{
	s32 fkind;
	s32 player = gobj->user_data.s;

	if (sMNVSNetAutomatchTotalTimeTics < 30)
	{
		gobj->flags = GOBJ_FLAG_HIDDEN;
	}
	else if
	(
		(sMNVSNetAutomatchSlot.cursor_status != nMNPlayersCursorStatusPointer) ||
		(sMNVSNetAutomatchSlot.is_selected == TRUE) ||
		(sMNVSNetAutomatchSlot.is_recalling == TRUE)
	)
	{
		gobj->flags = GOBJ_FLAG_NONE;
	}
	else gobj->flags = GOBJ_FLAG_HIDDEN;

	if ((sMNVSNetAutomatchSlot.is_selected == FALSE) && (sMNVSNetAutomatchSlot.holder_player != GMCOMMON_PLAYERS_MAX))
	{
		if (sMNVSNetAutomatchSlot.is_cursor_adjusting == FALSE)
		{
			SObjGetStruct(gobj)->pos.x = SObjGetStruct(sMNVSNetAutomatchSlot.cursor)->pos.x + 11.0F;
			SObjGetStruct(gobj)->pos.y = SObjGetStruct(sMNVSNetAutomatchSlot.cursor)->pos.y + -14.0F;
		}
	}
	else mnVSNetAutomatchMovePuck(player);

	fkind = mnVSNetAutomatchGetPuckFighterKind(player);

	if ((sMNVSNetAutomatchSlot.is_selected == FALSE) && (fkind != sMNVSNetAutomatchSlot.fkind))
	{
		sMNVSNetAutomatchSlot.fkind = fkind;

		mnVSNetAutomatchUpdateFighter(player);
		mnVSNetAutomatchUpdateNameAndEmblem(player);
	}

}

// 0x80137268
void mnVSNetAutomatchMakeCursorCamera(void)
{
	CObj *cobj = CObjGetStruct
	(
		gcMakeCameraGObj
		(
			nGCCommonKindSceneCamera,
			NULL,
			16,
			GOBJ_PRIORITY_DEFAULT,
			lbCommonDrawSprite,
			13,
			COBJ_MASK_DLLINK(30),
			~0,
			FALSE,
			nGCProcessKindFunc,
			NULL,
			1,
			FALSE
		)
	);
	syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);
}

// 0x80137308
void mnVSNetAutomatchMakePuckCamera(void)
{
	CObj *cobj = CObjGetStruct
	(
		gcMakeCameraGObj
		(
			nGCCommonKindSceneCamera,
			NULL,
			16,
			GOBJ_PRIORITY_DEFAULT,
			lbCommonDrawSprite,
			15,
			COBJ_MASK_DLLINK(31),
			~0,
			FALSE,
			nGCProcessKindFunc,
			NULL,
			1,
			FALSE
		)
	);
	syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);
}

// 0x801373A8
void mnVSNetAutomatchMakeReadyCamera(void)
{
	CObj *cobj = CObjGetStruct
	(
		gcMakeCameraGObj
		(
			nGCCommonKindSceneCamera,
			NULL,
			16,
			GOBJ_PRIORITY_DEFAULT,
			lbCommonDrawSprite,
			10,
			COBJ_MASK_DLLINK(35),
			~0,
			FALSE,
			nGCProcessKindFunc,
			NULL,
			1,
			FALSE
		)
	);
	syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);
}

// 0x80137448
void mnVSNetAutomatchMakeCursor(s32 player)
{
	GObj *gobj;
	s32 unused;

	// ???
	intptr_t unused_offsets[/* */] =
	{
		llMNPlayersCommon1PTextGradientSprite,
		llMNPlayersCommon2PTextGradientSprite,
		llMNPlayersCommon3PTextGradientSprite,
		llMNPlayersCommon4PTextGradientSprite
	};
	u32 priorities[/* */] = { 6, 4, 2, 0 };

	gobj = lbCommonMakeSpriteGObj
	(
		0,
		NULL,
		19,
		GOBJ_PRIORITY_DEFAULT,
		lbCommonDrawSObjAttr,
		30,
		priorities[player],
		~0,
		lbRelocGetFileData
		(
			Sprite*,
			sMNVSNetAutomatchFiles[0],
			llMNPlayersCommonCursorHandGrabSprite
		),
		nGCProcessKindFunc,
		mnVSNetAutomatchCursorProcUpdate,
		2
	);
	gobj->user_data.s = player;
	sMNVSNetAutomatchSlot.cursor = gobj;
	
	SObjGetStruct(gobj)->pos.x = 60.0F;
	SObjGetStruct(gobj)->pos.y = 170.0F;
	SObjGetStruct(gobj)->sprite.attr &= ~SP_FASTCOPY;
	SObjGetStruct(gobj)->sprite.attr |= SP_TRANSPARENT;

	mnVSNetAutomatchUpdateCursor(gobj, player, nMNPlayersCursorStatusPointer);
}

// 0x80137590
void mnVSNetAutomatchMakePuck(s32 player)
{
	GObj *gobj;
	MNPlayersSlot1PGame *pslot;

	intptr_t offsets[/* */] =
	{
		llMNPlayersCommon1PPuckSprite,
		llMNPlayersCommon2PPuckSprite,
		llMNPlayersCommon3PPuckSprite,
		llMNPlayersCommon4PPuckSprite
	};

	u32 priorities[/* */] = { 3, 2, 1, 0 };
	s32 unused;

	gobj = lbCommonMakeSpriteGObj
	(
		0,
		NULL,
		20,
		GOBJ_PRIORITY_DEFAULT,
		lbCommonDrawSObjAttr,
		31,
		priorities[player],
		~0,
		lbRelocGetFileData
		(
			Sprite*,
			sMNVSNetAutomatchFiles[0],
			offsets[player]
		),
		nGCProcessKindFunc,
		mnVSNetAutomatchPuckProcUpdate,
		1
	);
	gobj->user_data.s = player;
	sMNVSNetAutomatchSlot.puck = gobj;

	if (sMNVSNetAutomatchSlot.fkind == nFTKindNull)
	{
		SObjGetStruct(gobj)->pos.x = 51.0F;
		SObjGetStruct(gobj)->pos.y = 161.0F;
	}
	else mnVSNetAutomatchCenterPuckInPortrait(gobj, sMNVSNetAutomatchSlot.fkind);

	SObjGetStruct(gobj)->sprite.attr &= ~SP_FASTCOPY;
	SObjGetStruct(gobj)->sprite.attr |= SP_TRANSPARENT;
}

// 0x801376F0 - Unused?
void func_vsNetAutomatch27_801376F0(void)
{
	return;
}

// 0x801376F8
void mnVSNetAutomatchPuckAdjustPortraitEdge(s32 player)
{
	s32 portrait = mnVSNetAutomatchGetPortrait(sMNVSNetAutomatchSlot.fkind);
	f32 portrait_edge_x = ((portrait >= 6) ? portrait - 6 : portrait) * 45 + 25;
	f32 portrait_edge_y = ((portrait >= 6) ? 1 : 0) * 43 + 36;
	f32 new_pos_x = SObjGetStruct(sMNVSNetAutomatchSlot.puck)->pos.x + sMNVSNetAutomatchSlot.puck_vel_x + 13.0F;
	f32 new_pos_y = SObjGetStruct(sMNVSNetAutomatchSlot.puck)->pos.y + sMNVSNetAutomatchSlot.puck_vel_y + 12.0F;

	if (new_pos_x < (portrait_edge_x + 5.0F))
	{
		sMNVSNetAutomatchSlot.puck_vel_x = ((portrait_edge_x + 5.0F) - new_pos_x) / 10.0F;
	}
	if (((portrait_edge_x + 45.0F) - 5.0F) < new_pos_x)
	{
		sMNVSNetAutomatchSlot.puck_vel_x = ((new_pos_x - ((portrait_edge_x + 45.0F) - 5.0F)) * -1.0F) / 10.0F;
	}
	if (new_pos_y < (portrait_edge_y + 5.0F))
	{
		sMNVSNetAutomatchSlot.puck_vel_y = ((portrait_edge_y + 5.0F) - new_pos_y) / 10.0F;
	}
	if (((portrait_edge_y + 43.0F) - 5.0F) < new_pos_y)
	{
		sMNVSNetAutomatchSlot.puck_vel_y = ((new_pos_y - ((portrait_edge_y + 43.0F) - 5.0F)) * -1.0F) / 10.0F;
	}
}

// 0x801378A8
void mnVSNetAutomatchPuckAdjustPlaced(s32 player)
{
	mnVSNetAutomatchPuckAdjustPortraitEdge(player);
}

// 0x801378C8
void mnVSNetAutomatchPuckAdjustRecall(s32 player)
{
	f32 vel_y, vel_x;

	sMNVSNetAutomatchSlot.recall_tics++;

	if (sMNVSNetAutomatchSlot.recall_tics < 11)
	{
		vel_x = (sMNVSNetAutomatchSlot.recall_end_x - sMNVSNetAutomatchSlot.recall_start_x) / 10.0F;

		if (sMNVSNetAutomatchSlot.recall_tics < 6)
		{
			vel_y = (sMNVSNetAutomatchSlot.recall_mid_y - sMNVSNetAutomatchSlot.recall_start_y) / 5.0F;
		}
		else vel_y = (sMNVSNetAutomatchSlot.recall_end_y - sMNVSNetAutomatchSlot.recall_mid_y) / 5.0F;
		
		sMNVSNetAutomatchSlot.puck_vel_x = vel_x;
		sMNVSNetAutomatchSlot.puck_vel_y = vel_y;
	}
	else if (sMNVSNetAutomatchSlot.recall_tics == 11)
	{
		mnVSNetAutomatchSetCursorGrab(player);

		sMNVSNetAutomatchSlot.puck_vel_x = 0.0F;
		sMNVSNetAutomatchSlot.puck_vel_y = 0.0F;
	}
	if (sMNVSNetAutomatchSlot.recall_tics == 30)
	{
		sMNVSNetAutomatchSlot.is_recalling = FALSE;
	}
}

// 0x8013799C
void mnVSNetAutomatchPuckAdjustProcUpdate(GObj *gobj)
{
	if (sMNVSNetAutomatchSlot.is_recalling != FALSE)
	{
		mnVSNetAutomatchPuckAdjustRecall(sMNVSNetAutomatchManPlayer);
	}
	if (sMNVSNetAutomatchSlot.is_selected != FALSE)
	{
		mnVSNetAutomatchPuckAdjustPlaced(0);
	}
}

// 0x801379E8
void mnVSNetAutomatchMakePuckAdjust(void)
{
	gcAddGObjProcess(gcMakeGObjSPAfter(0, NULL, 24, GOBJ_PRIORITY_DEFAULT), mnVSNetAutomatchPuckAdjustProcUpdate, nGCProcessKindFunc, 1);
}

// 0x80137A2C
void mnVSNetAutomatchSpotlightProcUpdate(GObj *gobj)
{
	f32 sizes[/* */] =
	{
		1.5F, 1.5F, 2.0F, 1.5F, 1.5F, 1.5F,
		1.5F, 1.5F, 1.5F, 1.5F, 1.5F, 1.5F
	};

	if ((sMNVSNetAutomatchSlot.is_fighter_selected == FALSE) && (sMNVSNetAutomatchSlot.fkind != nFTKindNull))
	{
		gobj->flags = (gobj->flags == GOBJ_FLAG_HIDDEN) ? GOBJ_FLAG_NONE : GOBJ_FLAG_HIDDEN;

		DObjGetStruct(gobj)->scale.vec.f.x = sizes[sMNVSNetAutomatchSlot.fkind];
		DObjGetStruct(gobj)->scale.vec.f.y = sizes[sMNVSNetAutomatchSlot.fkind];
		DObjGetStruct(gobj)->scale.vec.f.y = sizes[sMNVSNetAutomatchSlot.fkind];
	}
	else gobj->flags = GOBJ_FLAG_HIDDEN;
}

// 0x80137B04
void mnVSNetAutomatchMakeSpotlight(void)
{
	GObj *gobj = gcMakeGObjSPAfter(0, NULL, 21, GOBJ_PRIORITY_DEFAULT);

	gcSetupCommonDObjs(gobj, lbRelocGetFileData(DObjDesc*, sMNVSNetAutomatchFiles[10], llMNPlayersSpotlightDObjDesc), NULL);
	gcAddGObjDisplay(gobj, gcDrawDObjTreeDLLinksForGObj, 9, GOBJ_PRIORITY_DEFAULT, ~0);
	gcAddMObjAll(gobj, lbRelocGetFileData(MObjSub***, sMNVSNetAutomatchFiles[10], llMNPlayersSpotlightMObjSub));
	gcAddGObjProcess(gobj, mnVSNetAutomatchSpotlightProcUpdate, nGCProcessKindFunc, 1);
	gcPlayAnimAll(gobj);

	DObjGetStruct(gobj)->translate.vec.f.x = -1100.0F;
	DObjGetStruct(gobj)->translate.vec.f.y = -850.0F;
	DObjGetStruct(gobj)->translate.vec.f.z = 0.0F;
}

// 0x80137BE4
void mnVSNetAutomatchReadyProcUpdate(GObj *gobj)
{
	if (mnVSNetAutomatchCheckReady() != FALSE)
	{
		sMNVSNetAutomatchReadyBlinkWait++;

		if (sMNVSNetAutomatchReadyBlinkWait == 40)
		{
			sMNVSNetAutomatchReadyBlinkWait = 0;
		}
		gobj->flags = (sMNVSNetAutomatchReadyBlinkWait < 30) ? GOBJ_FLAG_NONE : GOBJ_FLAG_HIDDEN;
	}
	else
	{
		gobj->flags = GOBJ_FLAG_HIDDEN;
		sMNVSNetAutomatchReadyBlinkWait = 0;
	}
}

// 0x80137C64
void mnVSNetAutomatchMakeReady(void)
{
	GObj *gobj;
	SObj *sobj;

	gobj = gcMakeGObjSPAfter(0, NULL, 28, GOBJ_PRIORITY_DEFAULT);
	gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 35, GOBJ_PRIORITY_DEFAULT, ~0);
	gcAddGObjProcess(gobj, mnVSNetAutomatchReadyProcUpdate, nGCProcessKindFunc, 1);

	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[0], llMNPlayersCommonReadyBannerSprite));
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;
	sobj->envcolor.r = 0x00;
	sobj->envcolor.g = 0x00;
	sobj->envcolor.b = 0x00;
	sobj->sprite.red = 0xF4;
	sobj->sprite.green = 0x56;
	sobj->sprite.blue = 0x7F;
	sobj->cms = 0;
	sobj->cmt = 0;
	sobj->masks = 3;
	sobj->maskt = 0;
	sobj->lrs = 320;
	sobj->lrt = 17;
	sobj->pos.x = 0.0F;
	sobj->pos.y = 71.0F;

	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[0], llMNPlayersCommonReadyToFightTextSprite));
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;
	sobj->envcolor.r = 0xFF;
	sobj->envcolor.g = 0xCA;
	sobj->envcolor.b = 0x13;
	sobj->sprite.red = 0xFF;
	sobj->sprite.green = 0xFF;
	sobj->sprite.blue = 0x9D;
	sobj->pos.x = 50.0F;
	sobj->pos.y = 76.0F;

	gobj = gcMakeGObjSPAfter(0, NULL, 22, GOBJ_PRIORITY_DEFAULT);
	gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 28, GOBJ_PRIORITY_DEFAULT, ~0);
	gcAddGObjProcess(gobj, mnVSNetAutomatchReadyProcUpdate, nGCProcessKindFunc, 1);

#if defined(REGION_US)
	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[0], llMNPlayersCommonPressTextSprite));
#else
	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[0], llMNPlayersCommonPushTextSprite));
#endif
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;
	sobj->sprite.red = 0xD6;
	sobj->sprite.green = 0xDD;
	sobj->sprite.blue = 0xC6;
#if defined(REGION_US)
	sobj->pos.x = 133.0F;
#else
	sobj->pos.x = 120.0F;
#endif
	sobj->pos.y = 219.0F;

	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[0], llMNPlayersCommonStartTextSprite));
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;
	sobj->sprite.red = 0xFF;
	sobj->sprite.green = 0x56;
	sobj->sprite.blue = 0x92;
#if defined(REGION_US)
	sobj->pos.x = 162.0F;
#else
	sobj->pos.x = 143.0F;
#endif
	sobj->pos.y = 219.0F;

#if defined(REGION_JP)
	sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSNetAutomatchFiles[0], llMNPlayersCommonButtonTextSprite));
	sobj->sprite.attr &= ~SP_FASTCOPY;
	sobj->sprite.attr |= SP_TRANSPARENT;
	sobj->sprite.red = 0xD6;
	sobj->sprite.green = 0xDD;
	sobj->sprite.blue = 0xC6;
	sobj->pos.x = 171.0F;
	sobj->pos.y = 219.0F;
#endif
}

// 0x80137EE0 - Unused?
void func_vsNetAutomatch27_80137EE0(void)
{
	return;
}

// 0x80137EE8 - Unused?
void func_vsNetAutomatch27_80137EE8(void)
{
	return;
}

// 0x80137EF0
sb32 mnVSNetAutomatchCheckReady(void)
{
	sb32 is_ready = TRUE;

	if (!sMNVSNetAutomatchSlot.is_fighter_selected)
	{
		is_ready = FALSE;
	}
	return is_ready;
}

// 0x80137F10
void mnVSNetAutomatchSetSceneData(void)
{
	gSCManagerSceneData.spgame_time_limit = sMNVSNetAutomatchTimeSetting;
	gSCManagerSceneData.player = sMNVSNetAutomatchManPlayer;
	gSCManagerBackupData.spgame_difficulty = sMNVSNetAutomatchLevelValue;
	gSCManagerSceneData.spgame_stage = 0;
#ifdef PORT
	{
		const char *stage_env = getenv("SSB64_SPGAME_STAGE");
		if (stage_env != NULL)
		{
			gSCManagerSceneData.spgame_stage = (u8)atoi(stage_env);
		}
	}
#endif
	gSCManagerBackupData.spgame_stock_count = sMNVSNetAutomatchStockValue;

	if (sMNVSNetAutomatchSlot.is_fighter_selected != FALSE)
	{
		gSCManagerSceneData.fkind = sMNVSNetAutomatchSlot.fkind;
	}
	else gSCManagerSceneData.fkind = nFTKindNull;

	gSCManagerSceneData.costume = sMNVSNetAutomatchSlot.costume;

	lbBackupWrite();
}

// 0x80137F9C
void mnVSNetAutomatchPauseSlotProcesses(void)
{
	if (sMNVSNetAutomatchSlot.cursor != NULL)
	{
		gcPauseGObjProcess(sMNVSNetAutomatchSlot.cursor->gobjproc_head);
	}
}

// 0x80137FCC
void mnVSNetAutomatchFuncRun(GObj *gobj)
{
	sMNVSNetAutomatchTotalTimeTics++;

	if (sMNVSNetAutomatchTotalTimeTics == sMNVSNetAutomatchReturnTic)
	{
		gSCManagerSceneData.scene_prev = gSCManagerSceneData.scene_curr;
		gSCManagerSceneData.scene_curr = nSCKindTitle;

		mnVSNetAutomatchSetSceneData();
		syTaskmanSetLoadScene();
	}
	else
	{
		if (scSubsysControllerCheckNoInputAll() == FALSE)
		{
			sMNVSNetAutomatchReturnTic = sMNVSNetAutomatchTotalTimeTics + I_MIN_TO_TICS(5);
		}
		if (sMNVSNetAutomatchIsStart != FALSE)
		{
			sMNVSNetAutomatchStartProceedWait--;

			if (sMNVSNetAutomatchStartProceedWait == 0)
			{
#if defined(PORT) && defined(SSB64_NETMENU) && !defined(_WIN32)
				mnVSNetAutomatchSetSceneData();
				gSCManagerSceneData.scene_prev = gSCManagerSceneData.scene_curr;
				gSCManagerSceneData.scene_curr = nSCKindVSNetMatchStaging;
				syTaskmanSetLoadScene();
				sMNVSNetAutomatchIsStart = FALSE;
#else
				gSCManagerSceneData.scene_prev = gSCManagerSceneData.scene_curr;
				gSCManagerSceneData.scene_curr = nSCKind1PGame;

				mnVSNetAutomatchSetSceneData();
				syTaskmanSetLoadScene();
#endif /* automatch HTTPS + P2P */
			}
		}
		else if ((scSubsysControllerGetPlayerTapButtons(START_BUTTON)) && (sMNVSNetAutomatchTotalTimeTics > 60))
		{
			if (mnVSNetAutomatchCheckReady() != FALSE)
			{
				func_800269C0_275C0(nSYAudioVoicePublicCheer);

				sMNVSNetAutomatchStartProceedWait = 30;
				sMNVSNetAutomatchIsStart = TRUE;

				mnVSNetAutomatchPauseSlotProcesses();
			}
			else func_800269C0_275C0(nSYAudioFGMMenuDenied);
		}
	}
}

// 0x80138118
s32 mnVSNetAutomatchGetNextTimeValue(s32 value)
{
	return (value == 5) ? SCBATTLE_TIMELIMIT_INFINITE : 5;
}

// 0x80138134
s32 mnVSNetAutomatchGetPrevTimeValue(s32 value)
{
	return (value == 5) ? SCBATTLE_TIMELIMIT_INFINITE : 5;
}

// 0x80138150
void mnVSNetAutomatchInitPlayer(s32 player)
{
	sMNVSNetAutomatchSlot.flash = NULL;
	sMNVSNetAutomatchSlot.p_sfx = NULL;
	sMNVSNetAutomatchSlot.sfx_id = 0;
	sMNVSNetAutomatchSlot.player = NULL;
	sMNVSNetAutomatchSlot.fkind = gSCManagerSceneData.fkind;
	sMNVSNetAutomatchSlot.costume = gSCManagerSceneData.costume;

	if (sMNVSNetAutomatchSlot.fkind == nFTKindNull)
	{
		sMNVSNetAutomatchSlot.holder_player = player;
		sMNVSNetAutomatchSlot.held_player = player;
		sMNVSNetAutomatchSlot.is_fighter_selected = FALSE;
		sMNVSNetAutomatchSlot.is_selected = FALSE;
		sMNVSNetAutomatchSlot.is_recalling = FALSE;
		sMNVSNetAutomatchSlot.is_cursor_adjusting = FALSE;
	}
	else
	{
		sMNVSNetAutomatchSlot.holder_player = GMCOMMON_PLAYERS_MAX;
		sMNVSNetAutomatchSlot.held_player = -1;
		sMNVSNetAutomatchSlot.is_fighter_selected = TRUE;
		sMNVSNetAutomatchSlot.is_selected = TRUE;
		sMNVSNetAutomatchSlot.is_recalling = FALSE;
		sMNVSNetAutomatchSlot.is_cursor_adjusting = FALSE;
	}
#ifdef PORT
	// Match the equivalent reset in mnPlayersVSInitPlayer. The decomp's 1P-game
	// InitPlayer leaves `is_status_selected` carrying its prior-CSS value, so on
	// re-entry the FighterProcUpdate sees `is_fighter_selected=TRUE,
	// is_status_selected=TRUE, rotation<0.1` and skips the
	// `scSubsysFighterSetStatus(...Win)` call — the freshly-spawned fighter never
	// gets its selected/win pose applied and renders in the default forward-facing
	// idle (issue #7). Reset the flag here so the status is re-applied on the
	// first frame post-MakeFighter.
	sMNVSNetAutomatchSlot.is_status_selected = FALSE;
#endif
}

// 0x801381D0 - Unused?
void func_vsNetAutomatch27_801381D0(void)
{
	return;
}

// 0x801381D8
void mnVSNetAutomatchInitVars(void)
{
	sMNVSNetAutomatchTotalTimeTics = 0;
	sMNVSNetAutomatchReturnTic = sMNVSNetAutomatchTotalTimeTics + I_MIN_TO_TICS(5);
	sMNVSNetAutomatchIsStart = FALSE;
	sMNVSNetAutomatchTimeSetting = gSCManagerSceneData.spgame_time_limit;
	sMNVSNetAutomatchManPlayer = gSCManagerSceneData.player;
	sMNVSNetAutomatchLevelValue = gSCManagerBackupData.spgame_difficulty;
	sMNVSNetAutomatchStockValue = gSCManagerBackupData.spgame_stock_count;
	sMNVSNetAutomatchFighterKind = gSCManagerSceneData.fkind;
	sMNVSNetAutomatchCostume = gSCManagerSceneData.costume;
	sMNVSNetAutomatchHiScoreGObj = NULL;
	sMNVSNetAutomatchBonusesGObj = NULL;
	sMNVSNetAutomatchLevelGObj = NULL;
	sMNVSNetAutomatchStockGObj = NULL;
	sMNVSNetAutomatchTimeGObj = NULL;
	sMNVSNetAutomatchIsTeamBattle = gSCManager1PGameBattleState.is_team_battle;
	sMNVSNetAutomatchRule = gSCManager1PGameBattleState.game_rules;

	mnVSNetAutomatchInitPlayer(sMNVSNetAutomatchManPlayer);

	sMNVSNetAutomatchSlot.recall_end_tic = 0;
	sMNVSNetAutomatchFighterMask = gSCManagerBackupData.fighter_mask;

#if defined(PORT) && defined(SSB64_NETMENU) && !defined(_WIN32)
	gSCManagerSceneData.is_vs_automatch_battle = (ub8)FALSE;
	gSCManagerSceneData.vs_net_automatch_post_battle_scene = (u8)(0);
	mnVSNetAutomatchAMReset();
#endif
}

// 0x801382C8
void mnVSNetAutomatchInitSlot(s32 player)
{
	mnVSNetAutomatchMakeCursor(player);
	mnVSNetAutomatchMakePuck(player);
	mnVSNetAutomatchMakeGate(player);

	if ((sMNVSNetAutomatchSlot.is_selected != FALSE) && (sMNVSNetAutomatchSlot.fkind != nFTKindNull))
	{
		mnVSNetAutomatchMakeFighter(sMNVSNetAutomatchSlot.player, player, sMNVSNetAutomatchSlot.fkind, sMNVSNetAutomatchSlot.costume);
	}
}

// 0x80138334
void mnVSNetAutomatchLoadWallpaperRelocFiles(void)
{
	LBRelocSetup rl_setup;

	rl_setup.table_addr = (uintptr_t)&lLBRelocTableAddr;
	rl_setup.table_files_num = (u32)llRelocFileCount;
	rl_setup.file_heap = NULL;
	rl_setup.file_heap_size = 0;
	rl_setup.status_buffer = sMNVSNetAutomatchStatusBuffer;
	rl_setup.status_buffer_size = ARRAY_COUNT(sMNVSNetAutomatchStatusBuffer);
	rl_setup.force_status_buffer = sMNVSNetAutomatchForceStatusBuffer;
	rl_setup.force_status_buffer_size = ARRAY_COUNT(sMNVSNetAutomatchForceStatusBuffer);

	lbRelocInitSetup(&rl_setup);
	lbRelocLoadFilesListed(dMNVSNetAutomatchFileIDs, sMNVSNetAutomatchFiles);
}

// 0x80138334
void mnVSNetAutomatchFuncStart(void)
{
	s32 unused1[2];
	s32 unused2;
	s32 i, j;

	mnVSNetAutomatchLoadWallpaperRelocFiles();

	gcMakeGObjSPAfter(nGCCommonKindPlayerSelect, mnVSNetAutomatchFuncRun, 15, GOBJ_PRIORITY_DEFAULT);
	gcMakeDefaultCameraGObj(16, GOBJ_PRIORITY_DEFAULT, 100, COBJ_FLAG_ZBUFFER, GPACK_RGBA8888(0x00, 0x00, 0x00, 0x00));
	efParticleInitAll();
	efManagerInitEffects();
	ftManagerAllocFighter(FTDATA_FLAG_SUBMOTION, 1);

	for (i = nFTKindPlayableStart; i <= nFTKindPlayableEnd; i++)
	{
		ftManagerSetupFilesAllKind(i);
	}
	sMNVSNetAutomatchFigatreeHeap = syTaskmanMalloc(gFTManagerFigatreeHeapSize, 0x10);

	mnVSNetAutomatchInitVars();
	mnVSNetAutomatchMakePortraitCamera();
	mnVSNetAutomatchMakeCursorCamera();
	mnVSNetAutomatchMakePuckCamera();
	mnVSNetAutomatchMakePlayerKindCamera();
	mnVSNetAutomatchMakeFighterCamera();
	mnVSNetAutomatchMakePortraitWallpaperCamera();
	mnVSNetAutomatchMakePortraitFlashCamera();
	mnVSNetAutomatchMakeWallpaperCamera();
	mnVSNetAutomatchMakeReadyCamera();
	mnVSNetAutomatchMakeWallpaper();
	mnVSNetAutomatchMakePortraitAll();
	mnVSNetAutomatchInitSlot(sMNVSNetAutomatchManPlayer);
	mnVSNetAutomatchMakePuckAdjust();
	mnVSNetAutomatchMakeSpotlight();
	mnVSNetAutomatchMakeLabelsMinimal();
	mnVSNetAutomatchMakeLabelsCamera();
	mnVSNetAutomatchMakeReady();
	scSubsysFighterSetLightParams(45.0F, 45.0F, 0xFF, 0xFF, 0xFF, 0xFF);

	if (gSCManagerSceneData.scene_prev != nSCKindMaps)
	{
		syAudioPlayBGM(0, nSYAudioBGMBattleSelect);
	}
	func_800269C0_275C0(nSYAudioVoiceAnnounceSelectPlayer);
}

// 0x80138C90
SYVideoSetup dMNVSNetAutomatchVideoSetup = SYVIDEO_SETUP_DEFAULT();

// 0x80138CAC
SYTaskmanSetup dMNVSNetAutomatchTaskmanSetup =
{
    // Task Manager Buffer Setup
    {
        0,                          // ???
        gcRunAll,              		// Update function
        gcDrawAll,          		// Frame draw function
        &ovl27_BSS_END,             // Allocatable memory pool start
        0,                          // Allocatable memory pool size
        1,                          // ???
        2,                          // Number of contexts?
        sizeof(Gfx) * 2375,         // Display List Buffer 0 Size
        sizeof(Gfx) * 64,          	// Display List Buffer 1 Size
        0,                          // Display List Buffer 2 Size
        0,                          // Display List Buffer 3 Size
        0x8000,                     // Graphics Heap Size
        2,                          // ???
        0x8000,                     // RDP Output Buffer Size
        mnVSNetAutomatchFuncLights,  // Pre-render function
        syControllerFuncRead,       // Controller I/O function
    },

    0,                              // Number of GObjThreads
    sizeof(u64) * 32,              	// Thread stack size
    0,                              // Number of thread stacks
    0,                              // ???
    0,                              // Number of GObjProcesses
    0,                              // Number of GObjs
    sizeof(GObj),                   // GObj size
    0,                              // Number of XObjs
    dLBCommonFuncMatrixList,        // Matrix function list
    NULL,                           // DObjVec eject function
    0,                              // Number of AObjs
    0,                              // Number of MObjs
    0,                              // Number of DObjs
    sizeof(DObj),                   // DObj size
    0,                              // Number of SObjs
    sizeof(SObj),                   // SObj size
    0,                              // Number of CObjs
    sizeof(CObj),                 	// CObj size
    
    mnVSNetAutomatchFuncStart        // Task start function
};


#if defined(PORT) && defined(SSB64_NETMENU) && !defined(_WIN32)

#define MN_AM_BIND_DEFAULT "0.0.0.0:7778"
#define MN_AM_STUB_PEER "127.0.0.1:9"

/* MN_AM_POLL: match poll often on staging (symmetric, low skew vs 120-tick CSS era); heartbeat stays slow. */
#define MN_AM_POLL_MATCH_INTERVAL 4U
#define MN_AM_POLL_HEARTBEAT_PERIOD 300U
#define MN_AM_POLL_HEARTBEAT_PHASE 20U

typedef enum MnVSNetAutomatchAMState
{
	MN_AM_IDLE = 0,
	MN_AM_ENSURE = 1,
	MN_AM_BIND = 2,
	MN_AM_JOIN = 3,
	MN_AM_POLL = 4,
	MN_AM_ENTER = 5,
	MN_AM_ERR = 99
} MnVSNetAutomatchAMState;

static MnVSNetAutomatchAMState sMnAMState = MN_AM_IDLE;
static char sMnAMTicket[72];
static char sMnAMPublicEndpoint[144];
static char sMnAMBindSpec[96];
static sb32 sMnAMStagingP2PReady = FALSE;
/* Advances once per MatchmakingTick while MN_AM_POLL (staging drives MatchmakingTick; CSS tics do not). */
static u32 sMnAMPollPeriodTics;

void mnVSNetAutomatchAMReset(void)
{
	sMnAMState = MN_AM_IDLE;
	sMnAMTicket[0] = '\0';
	sMnAMPublicEndpoint[0] = '\0';
	sMnAMBindSpec[0] = '\0';
	sMnAMStagingP2PReady = FALSE;
	sMnAMPollPeriodTics = 0;
}

static void mnVSNetAutomatchAMErr(void)
{
	func_800269C0_275C0(nSYAudioFGMMenuDenied);
	sMnAMState = MN_AM_ERR;
}

static sb32 mnVSNetAutomatchAMTryBootstrap(const MmMatchResult *mr, const char *bind, const char *peer_hp)
{
	gSYNetPeerSuppressBootstrapSceneAdvance = TRUE;
	(void)syNetPeerSetAutomatchNegotiation(TRUE);
	syNetPeerSetAutomatchLocalOffer((u16)gSCManagerSceneData.vs_net_stage_ban_mask,
	                                (u8)sMNVSNetAutomatchSlot.fkind, (u8)sMNVSNetAutomatchSlot.costume, 0U);
	if (syNetPeerConfigureUdpForAutomatch(bind, peer_hp, mr->session_id, mr->you_are_host, 2U) == FALSE)
	{
		gSYNetPeerSuppressBootstrapSceneAdvance = FALSE;
		(void)syNetPeerSetAutomatchNegotiation(FALSE);
		return FALSE;
	}
	if (syNetPeerRunBootstrap() == FALSE)
	{
		gSYNetPeerSuppressBootstrapSceneAdvance = FALSE;
		(void)syNetPeerSetAutomatchNegotiation(FALSE);
		return FALSE;
	}
	gSYNetPeerSuppressBootstrapSceneAdvance = FALSE;
	(void)syNetPeerSetAutomatchNegotiation(FALSE);
	return TRUE;
}

static void mnVSNetAutomatchAMEnterVs(const MmMatchResult *mr)
{
	const char *bind;
	const char *pub_env;

	bind = (sMnAMBindSpec[0] != '\0') ? sMnAMBindSpec : MN_AM_BIND_DEFAULT;
	pub_env = getenv("SSB64_MATCHMAKING_PUBLIC_ENDPOINT");

	port_log(
	    "SSB64 NetPeer automatch: match enter session=%u host=%d peer=%s peer_lan=%s public_endpoint_env=%s\n",
	    mr->session_id, mr->you_are_host, mr->peer_hostport,
	    (mr->peer_lan_hostport[0] != '\0') ? mr->peer_lan_hostport : "(none)",
	    (pub_env != NULL && pub_env[0] != '\0') ? "set" : "unset");

	if ((mr->peer_lan_hostport[0] != '\0') && (mnVSNetAutomatchAMTryBootstrap(mr, bind, mr->peer_lan_hostport) != FALSE))
	{
		port_log("SSB64 NetPeer automatch: reachability candidate=lan ok peer=%s\n", mr->peer_lan_hostport);
		sMnAMStagingP2PReady = TRUE;
		return;
	}
	if ((mr->peer_lan_hostport[0] != '\0') &&
	    (mnVSNetAutomatchAMTryBootstrap(mr, bind, mr->peer_hostport) != FALSE))
	{
		port_log("SSB64 NetPeer automatch: reachability candidate=reflexive ok peer=%s\n", mr->peer_hostport);
		sMnAMStagingP2PReady = TRUE;
		return;
	}
	if (mr->peer_lan_hostport[0] == '\0')
	{
		if (mnVSNetAutomatchAMTryBootstrap(mr, bind, mr->peer_hostport) != FALSE)
		{
			port_log("SSB64 NetPeer automatch: reachability candidate=reflexive ok peer=%s\n", mr->peer_hostport);
			sMnAMStagingP2PReady = TRUE;
			return;
		}
	}

	mnVSNetAutomatchAMErr();
	return;
}

void mnVSNetAutomatchAMFinalizeVsLoad(void)
{
	gSCManagerSceneData.vs_net_automatch_post_battle_scene = (u8)nSCKindVSNetAutomatch;
	gSCManagerSceneData.is_vs_automatch_battle = (ub8)(1);
	gSCManagerSceneData.scene_prev = gSCManagerSceneData.scene_curr;
	gSCManagerSceneData.scene_curr = nSCKindVSBattle;
	mnVSNetAutomatchSetSceneData();
	syTaskmanSetLoadScene();
	mnVSNetAutomatchAMReset();
}

sb32 mnVSNetAutomatchAMConsumeStagingHandshake(void)
{
	if (sMnAMStagingP2PReady == FALSE)
	{
		return FALSE;
	}
	sMnAMStagingP2PReady = FALSE;
	return TRUE;
}

sb32 mnVSNetAutomatchAMIsError(void)
{
	return (sMnAMState == MN_AM_ERR) ? TRUE : FALSE;
}

void mnVSNetAutomatchAMStagingReturnToAutomatch(void)
{
	syNetPeerStopVSSession();
	gSCManagerSceneData.scene_prev = gSCManagerSceneData.scene_curr;
	gSCManagerSceneData.scene_curr = nSCKindVSNetAutomatch;
	mnVSNetAutomatchSetSceneData();
	syTaskmanSetLoadScene();
	mnVSNetAutomatchAMReset();
}

void mnVSNetAutomatchAMStartSearch(void)
{
	mnVSNetAutomatchAMReset();
	mmMatchmakingStartup();
	(void)mmMatchmakingLoadCredentials(FALSE);
	mmMatchmakingEnqueueEnsurePlayer(FALSE);
	sMnAMState = MN_AM_ENSURE;
}

void mnVSNetAutomatchMatchmakingTick(void)
{
	MmMatchResult ev;

	while (mmMatchmakingDrainCompleted(&ev) != FALSE)
	{
		if (ev.kind == MM_POLL_PLAYER_READY)
		{
			const char *bm;
			const char *pub_env;
			s32 fd;

			if (sMnAMState != MN_AM_ENSURE)
			{
				continue;
			}
			bm = getenv("SSB64_MATCHMAKING_BIND");
			if ((bm == NULL) || (bm[0] == '\0'))
			{
				bm = MN_AM_BIND_DEFAULT;
			}
			snprintf(sMnAMBindSpec, sizeof(sMnAMBindSpec), "%s", bm);
			if (syNetPeerConfigureUdpForAutomatch(sMnAMBindSpec, MN_AM_STUB_PEER, 1U, FALSE, 2U) == FALSE)
			{
				mnVSNetAutomatchAMErr();
				continue;
			}
			if (syNetPeerOpenSocket() == FALSE)
			{
				mnVSNetAutomatchAMErr();
				continue;
			}
			fd = syNetPeerGetUdpSocketFd();
			pub_env = getenv("SSB64_MATCHMAKING_PUBLIC_ENDPOINT");
			if ((pub_env != NULL) && (pub_env[0] != '\0'))
			{
				snprintf(sMnAMPublicEndpoint, sizeof(sMnAMPublicEndpoint), "%s", pub_env);
			}
			else if ((fd >= 0) && (mmStunGetReflexiveIpv4Endpoint(fd, sMnAMPublicEndpoint,
			                                                     (u32)sizeof(sMnAMPublicEndpoint)) == FALSE))
			{
				mnVSNetAutomatchAMErr();
				continue;
			}
			{
				char lan_buf[144];
				const char *lan_env = getenv("SSB64_MATCHMAKING_LAN_ENDPOINT");
				const char *lan_for_queue;

				if ((lan_env != NULL) && (lan_env[0] != '\0'))
				{
					lan_for_queue = lan_env;
				}
				else if (mmLanDetectEndpoint(lan_buf, (u32)sizeof(lan_buf), fd, sMnAMBindSpec) != FALSE)
				{
					lan_for_queue = lan_buf;
				}
				else
				{
					lan_for_queue = NULL;
				}
				mmMatchmakingEnqueueJoinQueue(FALSE, sMnAMPublicEndpoint, (u8)sMNVSNetAutomatchSlot.fkind,
				                              (sMNVSNetAutomatchSlot.is_fighter_selected != FALSE) ? TRUE : FALSE,
				                              lan_for_queue);
			}
			sMnAMState = MN_AM_JOIN;
			continue;
		}
		if (ev.kind == MM_POLL_QUEUED)
		{
			snprintf(sMnAMTicket, sizeof(sMnAMTicket), "%s", ev.ticket_id);
			mmMatchmakingEnqueuePollMatch(FALSE, sMnAMTicket);
			sMnAMPollPeriodTics = 0;
			sMnAMState = MN_AM_POLL;
			continue;
		}
		if (ev.kind == MM_POLL_MATCHED)
		{
			port_log(
			    "SSB64 Automatch: MM_POLL_MATCHED poll_phase_tics=%u session=%u host=%d ticket=%.36s\n",
			    (unsigned int)sMnAMPollPeriodTics, (unsigned int)ev.session_id, (int)ev.you_are_host, sMnAMTicket);
			sMnAMState = MN_AM_ENTER;
			mnVSNetAutomatchAMEnterVs(&ev);
			continue;
		}
		if (ev.kind == MM_POLL_HEARTBEAT_OK)
		{
			continue;
		}
		if (ev.kind == MM_POLL_ERROR)
		{
			mnVSNetAutomatchAMErr();
		}
	}

	if (sMnAMState == MN_AM_POLL)
	{
		sMnAMPollPeriodTics++;
		if ((sMnAMPollPeriodTics % MN_AM_POLL_MATCH_INTERVAL) == 0U)
		{
			mmMatchmakingEnqueuePollMatch(FALSE, sMnAMTicket);
		}
		if ((sMnAMPollPeriodTics % MN_AM_POLL_HEARTBEAT_PERIOD) == MN_AM_POLL_HEARTBEAT_PHASE)
		{
			mmMatchmakingEnqueueHeartbeat(FALSE, sMnAMTicket);
		}
	}
}
#endif /* PORT && SSB64_NETMENU && !_WIN32 */

// 0x80138558
void mnVSNetAutomatchStartScene(void)
{
	dMNVSNetAutomatchVideoSetup.zbuffer = SYVIDEO_ZBUFFER_START(320, 240, 0, 10, u16);
	syVideoInit(&dMNVSNetAutomatchVideoSetup);

	dMNVSNetAutomatchTaskmanSetup.scene_setup.arena_size = (size_t) ((uintptr_t)&ovl1_VRAM - (uintptr_t)&ovl27_BSS_END);
	syTaskmanStartTask(&dMNVSNetAutomatchTaskmanSetup);
}
