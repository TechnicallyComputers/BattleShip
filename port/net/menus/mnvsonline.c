#include <mn/menu.h>
#include <gm/gmsound.h>
#include <sc/scene.h>
#include <sys/video.h>
#include <sys/rdp.h>
#include <sys/controller.h>
#include <reloc_data.h>
#include <ft/ftparam.h>
#include <sys/audio.h>

#include "mn_vs_submenu_png.h"

/*
 * port/net/menus/mnvsonline.c — online tier: netplay feature submenu (stub rows).
 * Mirrors hub layout (cascade tabs + PNG labels); A on rows is no-op for now; B returns to hub (nSCKindVSMode).
 * Entry: mnVSModeOnlineStartScene. SSB64_NETMENU only.
 */

extern void* func_800269C0_275C0(u16);

sb32 mnVSOnlineIsTime(void);

// // // // // // // // // // // //
//                               //
//       EXTERNAL VARIABLES      //
//                               //
// // // // // // // // // // // //

extern ub8 gSYMainImemOK;

// // // // // // // // // // // //
//                               //
//       INITIALIZED DATA        //
//                               //
// // // // // // // // // // // //

// 0x801347B0
u32 dMNVSOnlineFileIDs[/* */] = { llMNCommonFileID, llMNVSModeFileID };

// 0x801347B8
Lights1 dMNVSOnlineLights1 = gdSPDefLights1(0x20, 0x20, 0x20, 0xFF, 0xFF, 0xFF, 0x3C, 0x3C, 0x3C);

// 0x801347D0
Gfx dMNVSOnlineDisplayList[/* */] =
{
    gsSPSetGeometryMode(G_LIGHTING),
    gsSPSetLights1(dMNVSOnlineLights1),
    gsSPEndDisplayList()
};

// // // // // // // // // // // //
//                               //
//   GLOBAL / STATIC VARIABLES   //
//                               //
// // // // // // // // // // // //

// 0x80134930
GObj* sMNVSOnlineButtonGObjVSStart;

// 0x80134934
GObj* sMNVSOnlineButtonGObjRule;

// 0x80134938
GObj* sMNVSOnlineButtonGObjTimeStock;

// 0x8013493C
GObj* sMNVSOnlineButtonGObjVSOptions;

// 0x80134940 - Padding?
s32 sMNVSOnlinePad0x80134940[2];

// 0x80134948
s32 sMNVSOnlineCursorIndex;

// 0x8013494C
s32 sMNVSOnlineRule;

// 0x80134950
s32 sMNVSOnlineTime;

// 0x80134954
s32 sMNVSOnlineStock;

// 0x80134958
GObj* sMNVSOnlineRuleValueGObj;

// 0x8013495C
GObj* sMNVSOnlineTimeStockValueGObj;

// 0x80134960
GObj* sMNVSOnlineUnusedGObj;

// 0x80134964
GObj* sMNVSOnlineRuleArrowsGObj;

// 0x80134968
GObj* sMNVSOnlineTimeStockArrowsGObj;

// 0x8013496C
s32 sMNVSOnlineTimeStockArrowBlinkTimer;

// 0x80134970
s32 sMNVSOnlineRuleArrowBlinkTimer;

// 0x80134974
s32 sMNVSOnlineExitInterrupt;

// 0x80134978
s32 sMNVSOnlineInputDirection;

// 0x8013497C
s32 sMNVSOnlineChangeWait;

// 0x80134980
s32 sMNVSOnlineTotalTimeTics;

// 0x80134984
s32 sMNVSOnlineReturnTic;

// 0x80134988
LBFileNode sMNVSOnlineStatusBuffer[24];

// 0x80134A48
void *sMNVSOnlineFiles[ARRAY_COUNT(dMNVSOnlineFileIDs)];

// // // // // // // // // // // //
//                               //
//           FUNCTIONS           //
//                               //
// // // // // // // // // // // //

// 0x80131B00
void mnVSOnlineFuncLights(Gfx **display_list)
{
    gSPDisplayList(display_list[0]++, dMNVSOnlineDisplayList);
}

// 0x80131B24
s32 mnVSOnlinePow(s32 num, s32 pow)
{
    if (pow == 0) 
    {
        return 1;
    }
    else
    {
        s32 result = num, i = pow;

        if (pow >= 2)
        {
            do
            {
                result *= num;
            }
            while (--i != 1);
        }
        return result;
    }
}

// 0x80131BC4
void mnVSOnlineSetTextureColors(SObj* sobj, u32 *colors)
{
    sobj->sprite.attr &= ~SP_FASTCOPY;
    sobj->sprite.attr |= SP_TRANSPARENT;
    sobj->sprite.red = colors[0];
    sobj->sprite.green = colors[1];
    sobj->sprite.blue = colors[2];
}

// 0x80131BF4
s32 mnVSOnlineGetNumberOfDigits(s32 num, s32 maxDigits)
{
    s32 numDigits;

    for (numDigits = maxDigits; numDigits > 0; numDigits--)
    {
        if (mnVSOnlinePow(10, numDigits - 1) != 0 ? num / mnVSOnlinePow(10, numDigits - 1) : 0 != 0) return numDigits;
    }

    return 0;
}

// 0x80131CA0
void mnVSOnlineMakeNumber(GObj* number_gobj, s32 num, f32 x, f32 y, u32 *colors, s32 maxDigits, sb32 pad)
{
    // 0x801347F8
    intptr_t number_offsets[/* */] =
    {
        llMNCommonDigit0Sprite,
        llMNCommonDigit1Sprite,
        llMNCommonDigit2Sprite,
        llMNCommonDigit3Sprite,
        llMNCommonDigit4Sprite,
        llMNCommonDigit5Sprite,
        llMNCommonDigit6Sprite,
        llMNCommonDigit7Sprite,
        llMNCommonDigit8Sprite,
        llMNCommonDigit9Sprite
    };

    // 0x80134820
    Vec2f floats[/* */] =
    {
        { 10.0F,  6.0F }, 
        {  9.0F,  9.0F },
        { 10.0F,  9.0F }, 
        {  9.0F, 10.0F },
        {  9.0F, 10.0F }
    };

    SObj* number_sobj;
    f32 left_x = x;
    s32 place;
    s32 numDigits;
    s32 digit;

    if (num < 0) num = 0;

    number_sobj = lbCommonMakeSObjForGObj(number_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[0], number_offsets[num % 10]));
    mnVSOnlineSetTextureColors(number_sobj, colors);
    left_x -= 11.0F;
    number_sobj->pos.x = left_x;
    number_sobj->pos.y = y;

    for
    (
        place = 1, numDigits = (pad != FALSE) ? maxDigits : mnVSOnlineGetNumberOfDigits(num, maxDigits);
        place < numDigits;
        place++, numDigits = (pad != FALSE) ? maxDigits : mnVSOnlineGetNumberOfDigits(num, maxDigits)
    )
    {
        digit = (mnVSOnlinePow(10, place) != 0) ? num / mnVSOnlinePow(10, place) : 0;

        number_sobj = lbCommonMakeSObjForGObj(number_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[0], number_offsets[digit % 10]));
        mnVSOnlineSetTextureColors(number_sobj, colors);
        left_x -= 11.0F;
        number_sobj->pos.x = left_x;
        number_sobj->pos.y = y;
    }
}

// 0x80131F4C
void mnVSOnlineUpdateButton(GObj* button_gobj, s32 button_status)
{
    // 0x80134848
    SYColorRGBPair selcolors = { { 0x00, 0x00, 0x00 }, { 0xFF, 0xFF, 0xFF } };

    // 0x80134850
    SYColorRGBPair hicolors = { { 0x82, 0x00, 0x28 }, { 0xFF, 0x00, 0x28 } };

    // 0x80134858
    SYColorRGBPair notcolors = { { 0x00, 0x00, 0x00 }, { 0x82, 0x82, 0xAA } };

    SYColorRGBPair *colors;
    s32 i;
#ifdef PORT
    SObj* button_sobj = NULL;
#else
    SObj* button_sobj;
#endif

    switch (button_status)
    {
    case nMNOptionTabStatusHighlight:
        colors = &hicolors;
        break;
    
    case nMNOptionTabStatusNot:
        colors = &notcolors;
        break;

    case nMNOptionTabStatusSelected:
        colors = &selcolors;
        break;

    default:
        break;
    }

    button_sobj = SObjGetStruct(button_gobj);

    for (i = 0; i < 3; i++)
    {
        button_sobj->envcolor.r = colors->prim.r;
        button_sobj->envcolor.g = colors->prim.g;
        button_sobj->envcolor.b = colors->prim.b;
        
        button_sobj->sprite.red = colors->env.r;
        button_sobj->sprite.green = colors->env.g;
        button_sobj->sprite.blue = colors->env.b;

        button_sobj = button_sobj->next;
    }
}

// 0x80132024
void mnVSOnlineMakeButton(GObj* button_gobj, f32 x, f32 y, s32 arg3)
{
    SObj* button_sobj;

    button_sobj = lbCommonMakeSObjForGObj(button_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[0], llMNCommonOptionTabLeftSprite));
    button_sobj->sprite.attr &= ~SP_FASTCOPY;
    button_sobj->sprite.attr |= SP_TRANSPARENT;
    button_sobj->pos.x = x;
    button_sobj->pos.y = y;

    button_sobj = lbCommonMakeSObjForGObj(button_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[0], llMNCommonOptionTabMiddleSprite));
    button_sobj->sprite.attr &= ~SP_FASTCOPY;
    button_sobj->sprite.attr |= SP_TRANSPARENT;
    button_sobj->pos.x = x + 16.0F;
    button_sobj->pos.y = y;
    button_sobj->cms = 0;
    button_sobj->cmt = 0;
    button_sobj->masks = 4;
    button_sobj->maskt = 0;
    button_sobj->lrs = arg3 * 8;
    button_sobj->lrt = 0x1D;

    button_sobj = lbCommonMakeSObjForGObj(button_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[0], llMNCommonOptionTabRightSprite));
    button_sobj->sprite.attr &= ~SP_FASTCOPY;
    button_sobj->sprite.attr |= SP_TRANSPARENT;
    button_sobj->pos.x = x + 16.0F + (arg3 * 8);
    button_sobj->pos.y = y;
}

// 0x80132154
void mnVSOnlineMakeVSStartButton()
{
    GObj* button_gobj;
    SObj* button_sobj;

    sMNVSOnlineButtonGObjVSStart = button_gobj = gcMakeGObjSPAfter(0, NULL, 4, GOBJ_PRIORITY_DEFAULT);
    gcAddGObjDisplay(button_gobj, lbCommonDrawSObjAttr, 2, GOBJ_PRIORITY_DEFAULT, ~0);
    mnVSOnlineMakeButton(button_gobj, 120.0F, 31.0F, 17);

    mnVSOnlineUpdateButton(button_gobj, (sMNVSOnlineCursorIndex == nMNVSModeOptionStart) ? nMNOptionTabStatusHighlight : nMNOptionTabStatusNot);

    button_sobj = lbCommonMakeSObjForGObj(button_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeVSStartTextSprite));
    button_sobj->sprite.attr &= ~SP_FASTCOPY;
    button_sobj->sprite.attr |= SP_TRANSPARENT;
    if (mnPortTryApplyVsSubmenuLabelPng(button_sobj, "Automatch.png", 120.0F, 31.0F) == FALSE)
    {
        button_sobj->sprite.red = 0x00;
        button_sobj->sprite.green = 0x00;
        button_sobj->sprite.blue = 0x00;
        button_sobj->pos.x = 153.0F;
        button_sobj->pos.y = 36.0F;
    }
}

// 0x80132238
void mnVSOnlineMakeRuleValue()
{
    GObj* rule_value_gobj;
#ifdef PORT
    SObj* rule_value_sobj = NULL;
#else
    SObj* rule_value_sobj;
#endif

    // 0x80134860
    SYColorRGB color = { 0xFF, 0xFF, 0xFF };

    sMNVSOnlineRuleValueGObj = rule_value_gobj = gcMakeGObjSPAfter(0, NULL, 5, GOBJ_PRIORITY_DEFAULT);

    gcAddGObjDisplay(rule_value_gobj, lbCommonDrawSObjAttr, 3, GOBJ_PRIORITY_DEFAULT, ~0);

    switch (sMNVSOnlineRule)
    {
        case nMNVSModeRuleStock:
            rule_value_sobj = lbCommonMakeSObjForGObj(rule_value_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeStockTextSprite));
            rule_value_sobj->sprite.attr &= ~SP_FASTCOPY;
            rule_value_sobj->sprite.attr |= SP_TRANSPARENT;
            rule_value_sobj->pos.x = 183.0F;
            rule_value_sobj->pos.y = 78.0F;
            rule_value_sobj->sprite.red = color.r;
            rule_value_sobj->sprite.green = color.g;
            rule_value_sobj->sprite.blue = color.b;
            return;
        case nMNVSModeRuleTime:
            rule_value_sobj = lbCommonMakeSObjForGObj(rule_value_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeTimeTextSprite));
            rule_value_sobj->sprite.attr &= ~SP_FASTCOPY;
            rule_value_sobj->sprite.attr |= SP_TRANSPARENT;
            rule_value_sobj->pos.x = 187.0F;
            rule_value_sobj->pos.y = 78.0F;
            rule_value_sobj->sprite.red = color.r;
            rule_value_sobj->sprite.green = color.g;
            rule_value_sobj->sprite.blue = color.b;
            return;
        case nMNVSModeRuleStockTeam:
            rule_value_sobj = lbCommonMakeSObjForGObj(rule_value_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeStockTextSprite));
            rule_value_sobj->sprite.attr &= ~SP_FASTCOPY;
            rule_value_sobj->sprite.attr |= SP_TRANSPARENT;
            rule_value_sobj->pos.x = 165.0F;
            rule_value_sobj->pos.y = 78.0F;
            rule_value_sobj->sprite.red = color.r;
            rule_value_sobj->sprite.green = color.g;
            rule_value_sobj->sprite.blue = color.b;

            rule_value_sobj = lbCommonMakeSObjForGObj(rule_value_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeTeamTextSprite));
            rule_value_sobj->sprite.attr &= ~SP_FASTCOPY;
            rule_value_sobj->sprite.attr |= SP_TRANSPARENT;
            rule_value_sobj->pos.x = 212.0F;
            rule_value_sobj->pos.y = 78.0F;
            rule_value_sobj->sprite.red = color.r;
            rule_value_sobj->sprite.green = color.g;
            rule_value_sobj->sprite.blue = color.b;
            return;
        case nMNVSModeRuleTimeTeam:
            rule_value_sobj = lbCommonMakeSObjForGObj(rule_value_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeTimeTextSprite));
            rule_value_sobj->sprite.attr &= ~SP_FASTCOPY;
            rule_value_sobj->sprite.attr |= SP_TRANSPARENT;
            rule_value_sobj->pos.x = 168.0F;
            rule_value_sobj->pos.y = 78.0F;
            rule_value_sobj->sprite.red = color.r;
            rule_value_sobj->sprite.green = color.g;
            rule_value_sobj->sprite.blue = color.b;

            rule_value_sobj = lbCommonMakeSObjForGObj(rule_value_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeTeamTextSprite));
            rule_value_sobj->sprite.attr &= ~SP_FASTCOPY;
            rule_value_sobj->sprite.attr |= SP_TRANSPARENT;
            rule_value_sobj->pos.x = 212.0F;
            rule_value_sobj->pos.y = 78.0F;
            rule_value_sobj->sprite.red = color.r;
            rule_value_sobj->sprite.green = color.g;
            rule_value_sobj->sprite.blue = color.b;
            return;
    }
}

// 0x80132524
SObj* mnVSOnlineGetArrowSObj(GObj* arrow_gobj, s32 direction)
{
    SObj* next_arrow_sobj;
    SObj* first_arrow_sobj;

    first_arrow_sobj = SObjGetStruct(arrow_gobj);

    if (first_arrow_sobj != NULL)
    {
        if (direction == first_arrow_sobj->user_data.s)
        {
            return first_arrow_sobj;
        }
        next_arrow_sobj = first_arrow_sobj->next;

        if ((next_arrow_sobj != NULL) && (direction == next_arrow_sobj->user_data.s))
        {
            return next_arrow_sobj;
        }
    }
    return NULL;
}

// 0x80132570
void mnVSOnlineMakeLeftArrow(GObj* arrow_gobj, f32 x, f32 y)
{
    SObj* arrow_sobj;

    arrow_sobj = lbCommonMakeSObjForGObj(arrow_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[0], llMNCommonArrowLSprite));
    arrow_sobj->user_data.s = 0;
    arrow_sobj->sprite.attr &= ~SP_FASTCOPY;
    arrow_sobj->sprite.attr |= SP_TRANSPARENT;
    arrow_sobj->pos.x = x;
    arrow_sobj->pos.y = y;
    arrow_sobj->sprite.red = 0xFF;
    arrow_sobj->sprite.green = 0xAE;
    arrow_sobj->sprite.blue = 0x00;
}

// 0x801325E4
void mnVSOnlineMakeRightArrow(GObj* arrow_gobj, f32 x, f32 y)
{
    SObj* arrow_sobj;

    arrow_sobj = lbCommonMakeSObjForGObj(arrow_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[0], llMNCommonArrowRSprite));
    arrow_sobj->user_data.s = 1;
    arrow_sobj->sprite.attr &= ~SP_FASTCOPY;
    arrow_sobj->sprite.attr |= SP_TRANSPARENT;
    arrow_sobj->pos.x = x;
    arrow_sobj->pos.y = y;
    arrow_sobj->sprite.red = 0xFF;
    arrow_sobj->sprite.green = 0xAE;
    arrow_sobj->sprite.blue = 0x00;
}

// 0x8013265C
void mnVSOnlineAnimateRuleArrows(GObj* rule_arrows_gobj)
{
    SObj* arrow_sobj;

    while (TRUE)
    {
        if (sMNVSOnlineCursorIndex == 1)
        {
            sMNVSOnlineRuleArrowBlinkTimer--;

            if (sMNVSOnlineRuleArrowBlinkTimer == 0)
            {
                if (rule_arrows_gobj->flags == GOBJ_FLAG_HIDDEN)
                {
                    rule_arrows_gobj->flags = GOBJ_FLAG_NONE;
                }
                else rule_arrows_gobj->flags = GOBJ_FLAG_HIDDEN;

                sMNVSOnlineRuleArrowBlinkTimer = 30;
            }

            if (sMNVSOnlineRule == nMNVSModeRuleTime)
            {
                arrow_sobj = mnVSOnlineGetArrowSObj(rule_arrows_gobj, 0);

                if (arrow_sobj != NULL)
                {
                    gcEjectSObj(arrow_sobj);
                }
            }
            else if (mnVSOnlineGetArrowSObj(rule_arrows_gobj, 0) == NULL)
            {
                mnVSOnlineMakeLeftArrow(rule_arrows_gobj, 165.0F, 70.0F);
            }

            if (sMNVSOnlineRule == nMNVSModeRuleStockTeam)
            {
                arrow_sobj = mnVSOnlineGetArrowSObj(rule_arrows_gobj, 1);

                if (arrow_sobj != NULL)
                {
                    gcEjectSObj(arrow_sobj);
                }
            }
            else if (mnVSOnlineGetArrowSObj(rule_arrows_gobj, 1) == NULL)
            {
                mnVSOnlineMakeRightArrow(rule_arrows_gobj, 250.0F, 70.0F);
            }
        }
        else rule_arrows_gobj->flags = GOBJ_FLAG_HIDDEN;

        gcSleepCurrentGObjThread(1);
    }
}

// 0X80132818
void mnVSOnlineMakeRuleArrows()
{
    GObj* rule_arrows_gobj;

    if (sMNVSOnlineRuleArrowsGObj != NULL)
    {
        gcEjectGObj(sMNVSOnlineRuleArrowsGObj);

        sMNVSOnlineRuleArrowsGObj = NULL;
    }
    sMNVSOnlineRuleArrowsGObj = rule_arrows_gobj = gcMakeGObjSPAfter(0, NULL, 4, GOBJ_PRIORITY_DEFAULT);

    gcAddGObjDisplay(rule_arrows_gobj, lbCommonDrawSObjAttr, 2, GOBJ_PRIORITY_DEFAULT, ~0);
    gcAddGObjProcess(rule_arrows_gobj, mnVSOnlineAnimateRuleArrows, 0, 1);
}

// 0x801328A8
void mnVSOnlineAnimateTimeStockArrows(GObj* time_stock_arrows_gobj)
{
    while (TRUE)
    {
        if (sMNVSOnlineCursorIndex == 2)
        {
            sMNVSOnlineTimeStockArrowBlinkTimer--;

            if (sMNVSOnlineTimeStockArrowBlinkTimer == 0)
            {
                if (time_stock_arrows_gobj->flags == GOBJ_FLAG_HIDDEN)
                {
                    time_stock_arrows_gobj->flags = GOBJ_FLAG_NONE;
                }
                else time_stock_arrows_gobj->flags = GOBJ_FLAG_HIDDEN;

                sMNVSOnlineTimeStockArrowBlinkTimer = 30;
            }
        }
        else time_stock_arrows_gobj->flags = GOBJ_FLAG_HIDDEN;

        gcSleepCurrentGObjThread(1);
    }
}

// 0x80132964
void mnVSOnlineMakeTimeStockArrows()
{
    GObj* time_stock_arrows_gobj;

    if (sMNVSOnlineTimeStockArrowsGObj != NULL)
    {
        gcEjectGObj(sMNVSOnlineTimeStockArrowsGObj);

        sMNVSOnlineTimeStockArrowsGObj = NULL;
    }

    sMNVSOnlineTimeStockArrowsGObj = time_stock_arrows_gobj = gcMakeGObjSPAfter(0, NULL, 4, GOBJ_PRIORITY_DEFAULT);
    gcAddGObjDisplay(time_stock_arrows_gobj, lbCommonDrawSObjAttr, 2, GOBJ_PRIORITY_DEFAULT, ~0);
    gcAddGObjProcess(time_stock_arrows_gobj, mnVSOnlineAnimateTimeStockArrows, nGCProcessKindThread, 1);

    if (mnVSOnlineIsTime() != FALSE)
    {
        mnVSOnlineMakeLeftArrow(time_stock_arrows_gobj, 155.0F, 109.0F);
        mnVSOnlineMakeRightArrow(time_stock_arrows_gobj, 230.0F, 109.0F);
    }
    else
    {
        mnVSOnlineMakeLeftArrow(time_stock_arrows_gobj, 165.0F, 109.0F);
        mnVSOnlineMakeRightArrow(time_stock_arrows_gobj, 230.0F, 109.0F);
    }
}

// 0x80132A4C
void mnVSOnlineMakeRuleButton()
{
    GObj* rule_button_gobj;
    SObj* button_sobj;

    sMNVSOnlineButtonGObjRule = rule_button_gobj = gcMakeGObjSPAfter(0, NULL, 4, GOBJ_PRIORITY_DEFAULT);
    gcAddGObjDisplay(rule_button_gobj, lbCommonDrawSObjAttr, 2, GOBJ_PRIORITY_DEFAULT, ~0);
    mnVSOnlineMakeButton(rule_button_gobj, 97.0F, 70.0F, 17);

    mnVSOnlineUpdateButton(rule_button_gobj, (sMNVSOnlineCursorIndex == nMNVSModeOptionRule) ? nMNOptionTabStatusHighlight : nMNOptionTabStatusNot);

    button_sobj = lbCommonMakeSObjForGObj(rule_button_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeVSStartTextSprite));
    button_sobj->sprite.attr &= ~SP_FASTCOPY;
    button_sobj->sprite.attr |= SP_TRANSPARENT;
    if (mnPortTryApplyVsSubmenuLabelPng(button_sobj, "LevelPrefs.png", 97.0F, 70.0F) == FALSE)
    {
        button_sobj->sprite.red = 0x00;
        button_sobj->sprite.green = 0x00;
        button_sobj->sprite.blue = 0x00;
        button_sobj->pos.x = 108.0F;
        button_sobj->pos.y = 75.0F;
    }
}

// 0x80132B38
sb32 mnVSOnlineIsTime()
{
    if ((sMNVSOnlineRule == nMNVSModeRuleTime) || (sMNVSOnlineRule == nMNVSModeRuleTimeTeam))
    {
        return TRUE;
    }
    else return FALSE;
}

// 0x80132B68
s32 mnVSOnlineGetTimeStockValue()
{
    if (mnVSOnlineIsTime() != FALSE)
    {
        return sMNVSOnlineTime;
    }
    else return sMNVSOnlineStock + 1;
}

// 0x80132BA0
void mnVSOnlineMakeTimeStockValue()
{
    GObj* time_stock_value_gobj;
    SObj* time_stock_value_sobj;
    s32 value;
    s32 x;
    s32 unused;

    // 0x80134864
    u32 colors[/* */] = { 0x000000FF, 0x000000FF, 0x000000FF };

    sMNVSOnlineTimeStockValueGObj = time_stock_value_gobj = gcMakeGObjSPAfter(0, NULL, 5, GOBJ_PRIORITY_DEFAULT);
    gcAddGObjDisplay(time_stock_value_gobj, lbCommonDrawSObjAttr, 3, GOBJ_PRIORITY_DEFAULT, ~0);

    value = mnVSOnlineGetTimeStockValue();

    if (value == SCBATTLE_TIMELIMIT_INFINITE)
    {
        time_stock_value_sobj = lbCommonMakeSObjForGObj(time_stock_value_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[0], llMNCommonInfinitySprite));
        time_stock_value_sobj->sprite.attr &= ~SP_FASTCOPY;
        time_stock_value_sobj->sprite.attr |= SP_TRANSPARENT;
        time_stock_value_sobj->pos.x = 162.0F;
        time_stock_value_sobj->pos.y = 118.0F;
        time_stock_value_sobj->sprite.red = colors[0];
        time_stock_value_sobj->sprite.green = colors[1];
        time_stock_value_sobj->sprite.blue = colors[2];
    }
    else
    {
        if (mnVSOnlineIsTime() != FALSE)
        {
            x = (value < 10) ? 185 : 190;
        }
        else
        {
            x = (value < 10) ? 210 : 215;
        }
        mnVSOnlineMakeNumber(time_stock_value_gobj, value, (f32)x, 116.0F, colors, 2, 0);
    }
}

// 0x80132D04
void mnVSOnlineMakeTimeStockButton()
{
    GObj* time_stock_button_gobj;
    SObj* time_stock_button_sobj;

    sMNVSOnlineButtonGObjTimeStock = time_stock_button_gobj = gcMakeGObjSPAfter(0, NULL, 4, GOBJ_PRIORITY_DEFAULT);
    gcAddGObjDisplay(time_stock_button_gobj, lbCommonDrawSObjAttr, 2, GOBJ_PRIORITY_DEFAULT, ~0);
    mnVSOnlineMakeButton(time_stock_button_gobj, 74.0F, 109.0F, 17);

    mnVSOnlineUpdateButton(time_stock_button_gobj, (sMNVSOnlineCursorIndex == nMNVSModeOptionTimeStock) ? nMNOptionTabStatusHighlight : nMNOptionTabStatusNot);

    time_stock_button_sobj = lbCommonMakeSObjForGObj(time_stock_button_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeVSStartTextSprite));
    time_stock_button_sobj->sprite.attr &= ~SP_FASTCOPY;
    time_stock_button_sobj->sprite.attr |= SP_TRANSPARENT;
    if (mnPortTryApplyVsSubmenuLabelPng(time_stock_button_sobj, "Customs.png", 74.0F, 109.0F) == FALSE)
    {
        time_stock_button_sobj->sprite.red = 0x00;
        time_stock_button_sobj->sprite.green = 0x00;
        time_stock_button_sobj->sprite.blue = 0x00;
        time_stock_button_sobj->pos.x = 106.0F;
        time_stock_button_sobj->pos.y = 114.0F;
    }
}

// 0x80132EBC
void mnVSOnlineMakeVSOptionsButton(void)
{
    GObj* button_gobj;
    SObj* button_sobj;

    sMNVSOnlineButtonGObjVSOptions = button_gobj = gcMakeGObjSPAfter(0, NULL, 4, GOBJ_PRIORITY_DEFAULT);
    gcAddGObjDisplay(button_gobj, lbCommonDrawSObjAttr, 2, GOBJ_PRIORITY_DEFAULT, ~0);
    mnVSOnlineMakeButton(button_gobj, 120.0F, 148.0F, 17);
    mnVSOnlineUpdateButton(button_gobj, (sMNVSOnlineCursorIndex == nMNVSModeOptionOptions) ? nMNOptionTabStatusHighlight : nMNOptionTabStatusNot);

    button_sobj = lbCommonMakeSObjForGObj(button_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeVSStartTextSprite));
    button_sobj->sprite.attr &= ~SP_FASTCOPY;
    button_sobj->sprite.attr |= SP_TRANSPARENT;
    button_sobj->sprite.red = 0x00;
    button_sobj->sprite.green = 0x00;
    button_sobj->sprite.blue = 0x00;
    button_sobj->pos.x = 153.0F;
    button_sobj->pos.y = 153.0F;
}

// 0x80132FA4 - Unused?
void mnVSOnlineSetSubtitleSpriteColors(SObj* sobj)
{
    sobj->sprite.attr &= ~SP_FASTCOPY;
    sobj->sprite.attr |= SP_TRANSPARENT;
    sobj->envcolor.r = 0;
    sobj->envcolor.g = 0;
    sobj->envcolor.b = 0;
    sobj->sprite.red = 0xFF;
    sobj->sprite.green = 0xFF;
    sobj->sprite.blue = 0xFF;
}

// 0x80132FD8
void mnVSOnlineMakeSubtitle(void)
{
    GObj *gobj;
    SObj *sobj;

    sMNVSOnlineUnusedGObj = gobj = gcMakeGObjSPAfter(0, NULL, 5, GOBJ_PRIORITY_DEFAULT);

#if defined(REGION_JP)
    gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 3, GOBJ_PRIORITY_DEFAULT, ~0);

    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[0], llMNCommonFrameSprite));

    sobj->pos.x = 93.0F;
    sobj->pos.y = 189.0F;
    
    mnVSOnlineSetSubtitleSpriteColors(sobj);
    
    switch (sMNVSOnlineCursorIndex)
    {
        case nMNVSModeOptionStart:
            sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeVSStartTextJapSprite));
            
            sobj->pos.x = 121.0F;
            sobj->pos.y = 195.0F;
            
            mnVSOnlineSetSubtitleSpriteColors(sobj);
            return;

        case nMNVSModeOptionRule:
            switch (sMNVSOnlineRule)
            {
                case nMNVSModeRuleTime:
                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeTimeBasedTextJapSprite));
                    
                    sobj->pos.x = 120.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOnlineSetSubtitleSpriteColors(sobj);

                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeBattleTextJapSprite));
                    
                    sobj->pos.x = 166.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOnlineSetSubtitleSpriteColors(sobj);
                    break;
                case nMNVSModeRuleStock:
                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeStockTextJapSprite));
                    
                    sobj->pos.x = 115.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOnlineSetSubtitleSpriteColors(sobj);

                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeBattleTextJapSprite));
                    
                    sobj->pos.x = 172.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOnlineSetSubtitleSpriteColors(sobj);
                    break;
                case nMNVSModeRuleTimeTeam:
                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeTimeBasedTextJapSprite));
                    
                    sobj->pos.x = 102.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOnlineSetSubtitleSpriteColors(sobj);

                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeTeamTextJapSprite));
                    
                    sobj->pos.x = 148.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOnlineSetSubtitleSpriteColors(sobj);

                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeBattleTextJapSprite));
                    
                    sobj->pos.x = 182.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOnlineSetSubtitleSpriteColors(sobj);
                    break;
                case nMNVSModeRuleStockTeam:
                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeStockTextJapSprite));
                    
                    sobj->pos.x = 98.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOnlineSetSubtitleSpriteColors(sobj);

                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeTeamTextJapSprite));
                    
                    sobj->pos.x = 154.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOnlineSetSubtitleSpriteColors(sobj);

                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeBattleTextJapSprite));
                    
                    sobj->pos.x = 188.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOnlineSetSubtitleSpriteColors(sobj);
                    break;
            }

            return;

        case nMNVSModeOptionTimeStock:
            switch (sMNVSOnlineRule)
            {
                case nMNVSModeRuleStock:
                case nMNVSModeRuleStockTeam:
                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeRemaningPlayersTextJapSprite));
                    
                    sobj->pos.x = 126.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOnlineSetSubtitleSpriteColors(sobj);
                    break;
                case nMNVSModeRuleTime:
                case nMNVSModeRuleTimeTeam:
                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeTimeLimitTextJapSprite));
                    
                    sobj->pos.x = 132.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOnlineSetSubtitleSpriteColors(sobj);
                    break;
            }
            return;

        case nMNVSModeOptionOptions:
            sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeMatchOptionsTextJapSprite));
            
            sobj->pos.x = 111.0F;
            sobj->pos.y = 195.0F;
            
            mnVSOnlineSetSubtitleSpriteColors(sobj);
            return;
    }
#endif
}

// 0x80133008
void mnVSOnlineRenderMenuName(GObj* menu_name_gobj)
{
    gDPPipeSync(gSYTaskmanDLHeads[0]++);
    gDPSetCycleType(gSYTaskmanDLHeads[0]++, G_CYC_1CYCLE);
    gDPSetPrimColor(gSYTaskmanDLHeads[0]++, 0, 0, 0xA0, 0x78, 0x14, 0xE6);
    gDPSetCombineLERP(gSYTaskmanDLHeads[0]++, 0, 0, 0, PRIMITIVE,  0, 0, 0, PRIMITIVE,  0, 0, 0, PRIMITIVE,  0, 0, 0, PRIMITIVE);
    gDPSetRenderMode(gSYTaskmanDLHeads[0]++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    gDPFillRectangle(gSYTaskmanDLHeads[0]++, 225, 143, 310, 230);
    gDPPipeSync(gSYTaskmanDLHeads[0]++);
    gDPSetRenderMode(gSYTaskmanDLHeads[0]++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    gDPSetCycleType(gSYTaskmanDLHeads[0]++, G_CYC_1CYCLE);

    lbCommonClearExternSpriteParams();
    lbCommonDrawSObjAttr(menu_name_gobj);
}

// 0x8013314C
void mnVSOnlineMakeMenuName()
{
    GObj* menu_name_gobj;
    SObj* menu_name_sobj;

    menu_name_gobj = gcMakeGObjSPAfter(0, NULL, 3, GOBJ_PRIORITY_DEFAULT);
    gcAddGObjDisplay(menu_name_gobj, mnVSOnlineRenderMenuName, 1, GOBJ_PRIORITY_DEFAULT, ~0);

    menu_name_sobj = lbCommonMakeSObjForGObj(menu_name_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[0], llMNCommonSmashLogoSprite));
    menu_name_sobj->sprite.attr &= ~SP_FASTCOPY;
    menu_name_sobj->sprite.attr |= SP_TRANSPARENT;
    menu_name_sobj->sprite.red = 0x00;
    menu_name_sobj->sprite.green = 0x00;
    menu_name_sobj->sprite.blue = 0x00;
    menu_name_sobj->pos.x = 235.0F;
    menu_name_sobj->pos.y = 158.0F;

    menu_name_sobj = lbCommonMakeSObjForGObj(menu_name_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeVSTextSprite));
    menu_name_sobj->sprite.attr &= ~SP_FASTCOPY;
    menu_name_sobj->sprite.attr |= SP_TRANSPARENT;
    menu_name_sobj->sprite.red = 0x00;
    menu_name_sobj->sprite.green = 0x00;
    menu_name_sobj->sprite.blue = 0x00;
    menu_name_sobj->pos.x = 158.0F;
    menu_name_sobj->pos.y = 192.0F;

    menu_name_sobj = lbCommonMakeSObjForGObj(menu_name_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[0], llMNCommonGameModeTextSprite));
    menu_name_sobj->sprite.attr &= ~SP_FASTCOPY;
    menu_name_sobj->sprite.attr |= SP_TRANSPARENT;
    menu_name_sobj->sprite.red = 0x00;
    menu_name_sobj->sprite.green = 0x00;
    menu_name_sobj->sprite.blue = 0x00;
    menu_name_sobj->pos.x = 189.0F;
    menu_name_sobj->pos.y = 87.0F;
}

// 0x80133298
void mnVSOnlineMakeBackground(void)
{
    GObj* bg_gobj;
    SObj* bg_sobj;

    bg_gobj = gcMakeGObjSPAfter(0, NULL, 2, GOBJ_PRIORITY_DEFAULT);
    gcAddGObjDisplay(bg_gobj, lbCommonDrawSObjAttr, 0, GOBJ_PRIORITY_DEFAULT, ~0);

    bg_sobj = lbCommonMakeSObjForGObj(bg_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[0], llMNCommonSmashBrosCollageSprite));
    bg_sobj->pos.x = 10.0F;
    bg_sobj->pos.y = 10.0F;

    bg_sobj = lbCommonMakeSObjForGObj(bg_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[0], llMNCommonDecalPaperSprite));
    bg_sobj->sprite.attr &= ~SP_FASTCOPY;
    bg_sobj->sprite.attr |= SP_TRANSPARENT;
    bg_sobj->sprite.red = 0xA0;
    bg_sobj->sprite.green = 0x78;
    bg_sobj->sprite.blue = 0x14;
    bg_sobj->pos.x = 140.0F;
    bg_sobj->pos.y = 143.0F;

    bg_sobj = lbCommonMakeSObjForGObj(bg_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[0], llMNCommonDecalPaperSprite));
    bg_sobj->sprite.attr &= ~SP_FASTCOPY;
    bg_sobj->sprite.attr |= SP_TRANSPARENT;
    bg_sobj->sprite.red = 0xA0;
    bg_sobj->sprite.green = 0x78;
    bg_sobj->sprite.blue = 0x14;
    bg_sobj->pos.x = 225.0F;
    bg_sobj->pos.y = 56.0F;

    bg_sobj = lbCommonMakeSObjForGObj(bg_gobj, lbRelocGetFileData(Sprite*, sMNVSOnlineFiles[1], llMNVSModeConsoleIconDarkSprite));
    bg_sobj->sprite.attr &= ~SP_FASTCOPY;
    bg_sobj->sprite.attr |= SP_TRANSPARENT;
    bg_sobj->sprite.red = 0x99;
    bg_sobj->sprite.green = 0x99;
    bg_sobj->sprite.blue = 0x99;
    bg_sobj->pos.x = 10.0F;
    bg_sobj->pos.y = 10.0F;
}

// 0x8013342C
void mnVSOnlineMakeButtonValuegSYRdpViewport(void)
{
    GObj *camera_gobj = gcMakeCameraGObj
    (
        1,
        NULL,
        1,
        GOBJ_PRIORITY_DEFAULT,
        lbCommonDrawSprite,
        0x14,
        COBJ_MASK_DLLINK(3),
        -1,
        FALSE,
        nGCProcessKindFunc,
        NULL,
        1,
        FALSE
    );
    CObj *cobj = CObjGetStruct(camera_gobj);
    syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);
}

// 0x801334CC
void mnVSOnlineMakeButtonViewport(void)
{
    GObj *camera_gobj = gcMakeCameraGObj
    (
        1,
        NULL,
        1,
        GOBJ_PRIORITY_DEFAULT,
        lbCommonDrawSprite,
        40,
        COBJ_MASK_DLLINK(2),
        -1,
        0,
        1,
        0,
        1,
        0
    );
    CObj *cobj = CObjGetStruct(camera_gobj);
    syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);
}

// 0x8013356C
void mnVSOnlineMakeMenuNameViewport(void)
{
    GObj *camera_gobj = gcMakeCameraGObj
    (
        1,
        NULL,
        1,
        GOBJ_PRIORITY_DEFAULT,
        lbCommonDrawSprite,
        60,
        COBJ_MASK_DLLINK(1),
        -1,
        FALSE,
        nGCProcessKindFunc,
        NULL,
        1,
        FALSE
    );
    CObj *cobj = CObjGetStruct(camera_gobj);
    syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);
}

// 0x8013360C
void mnVSOnlineMakeBackgroundViewport()
{
    GObj *camera_gobj = gcMakeCameraGObj(
        1,
        NULL,
        1,
        GOBJ_PRIORITY_DEFAULT,
        lbCommonDrawSprite,
        80,
        COBJ_MASK_DLLINK(0),
        -1,
        0,
        1,
        0,
        1,
        0
    );
    CObj *cobj = CObjGetStruct(camera_gobj);
    syRdpSetViewport(&cobj->viewport, 10.0F, 10.0F, 310.0F, 230.0F);
}

// 0x801336AC
void mnVSOnlineFuncStartVars()
{
    sMNVSOnlineCursorIndex = nMNVSModeOptionStart;

    sMNVSOnlineChangeWait = 0;

    switch (gSCManagerTransferBattleState.is_team_battle)
    {
        case FALSE:
            if (gSCManagerTransferBattleState.game_rules == SCBATTLE_GAMERULE_TIME)
            {
                sMNVSOnlineRule = nMNVSModeRuleTime;
            }
            else sMNVSOnlineRule = nMNVSModeRuleStock;

            break;
        case TRUE:
            if (gSCManagerTransferBattleState.game_rules == SCBATTLE_GAMERULE_TIME)
            {
                sMNVSOnlineRule = nMNVSModeRuleTimeTeam;
            }
            else sMNVSOnlineRule = nMNVSModeRuleStockTeam;

            break;
    }

    sMNVSOnlineTime = gSCManagerTransferBattleState.time_limit;
    sMNVSOnlineStock = gSCManagerTransferBattleState.stocks;
    sMNVSOnlineTimeStockArrowsGObj = 0;
    sMNVSOnlineRuleArrowsGObj = 0;
    sMNVSOnlineInputDirection = nMNVSModeInputDirectionNone;
    sMNVSOnlineTotalTimeTics = 0;
    sMNVSOnlineExitInterrupt = 0;
    sMNVSOnlineReturnTic = sMNVSOnlineTotalTimeTics + I_MIN_TO_TICS(5);
    sMNVSOnlineTimeStockArrowBlinkTimer = 0;
    sMNVSOnlineRuleArrowBlinkTimer = 0;
}

// 0x801337B8
void mnVSOnlineSaveSettings()
{
    gSCManagerTransferBattleState.time_limit = sMNVSOnlineTime;
    gSCManagerTransferBattleState.stocks = sMNVSOnlineStock;

    switch (sMNVSOnlineRule)
    {
        case nMNVSModeRuleStock:
            gSCManagerTransferBattleState.is_team_battle = FALSE;
            gSCManagerTransferBattleState.game_rules = SCBATTLE_GAMERULE_STOCK;
            break;
        case nMNVSModeRuleTime:
            gSCManagerTransferBattleState.is_team_battle = FALSE;
            gSCManagerTransferBattleState.game_rules = SCBATTLE_GAMERULE_TIME;
            break;
        case nMNVSModeRuleStockTeam:
            gSCManagerTransferBattleState.is_team_battle = TRUE;
            gSCManagerTransferBattleState.game_rules = SCBATTLE_GAMERULE_STOCK;
            break;
        case nMNVSModeRuleTimeTeam:
            gSCManagerTransferBattleState.is_team_battle = TRUE;
            gSCManagerTransferBattleState.game_rules = SCBATTLE_GAMERULE_TIME;
            break;
    }
}

// 0x80133850
s32 mnVSOnlineGetShade(s32 player)
{
    sb32 is_same_costume[GMCOMMON_PLAYERS_MAX];
    s32 i;

    if ((sMNVSOnlineRule == nMNVSModeRuleTime) || (sMNVSOnlineRule == nMNVSModeRuleStock))
    {
        return 0;
    }
    for (i = 0; i < ARRAY_COUNT(is_same_costume); i++)
    {
        is_same_costume[i] = FALSE;
    }
    for (i = 0; i < ARRAY_COUNT(is_same_costume); i++)
    {
        if
        (
            (player != i) &&
            (gSCManagerTransferBattleState.players[player].fkind == gSCManagerTransferBattleState.players[i].fkind) &&
            (gSCManagerTransferBattleState.players[player].team == gSCManagerTransferBattleState.players[i].team)
        )
        {
            is_same_costume[gSCManagerTransferBattleState.players[i].shade] = TRUE;
        }
    }
    for (i = 0; i < ARRAY_COUNT(is_same_costume); i++)
    {
        if (is_same_costume[i] == FALSE)
        {
            return i;
        }
    }
    return 0;
}

// 0x8013394C
s32 mnVSOnlineGetCostume(s32 fkind, s32 arg1)
{
    s32 i;
    s32 j;
    s32 unused[2];
    sb32 is_same_costume[GMCOMMON_PLAYERS_MAX];

    for (i = 0; i < ARRAY_COUNT(is_same_costume); i++)
    {
        is_same_costume[i] = FALSE;
    }
    for (i = 0; i < ARRAY_COUNT(is_same_costume); i++)
    {
        if (i != arg1)
        {
            if (fkind == gSCManagerTransferBattleState.players[i].fkind)
            {
                for (j = 0; j < ARRAY_COUNT(is_same_costume); j++)
                {
                    if (ftParamGetCostumeCommonID(fkind, j) == gSCManagerTransferBattleState.players[i].costume)
                    {
                        is_same_costume[j] = TRUE;
                    }
                }
            }
        }
    }
    for (i = 0; i < ARRAY_COUNT(is_same_costume); i++)
    {
        if (is_same_costume[i] == FALSE)
        {
            return i;
        }
    }
    return 0;
}

// 0x80133A8C
void mnVSOnlineSetCostumesAndShades(void)
{
    s32 i;

    switch (sMNVSOnlineRule)
    {
        case nMNVSModeRuleTime:
        case nMNVSModeRuleStock:
            for (i = 0; i < ARRAY_COUNT(gSCManagerTransferBattleState.players); i++)
            {
                if (gSCManagerTransferBattleState.players[i].fkind != nFTKindNull)
                {
                    gSCManagerTransferBattleState.players[i].costume = ftParamGetCostumeCommonID(gSCManagerTransferBattleState.players[i].fkind, mnVSOnlineGetCostume(gSCManagerTransferBattleState.players[i].fkind, i));
                    gSCManagerTransferBattleState.players[i].shade = mnVSOnlineGetShade(i);
                }
            }
            break;
        case nMNVSModeRuleTimeTeam:
        case nMNVSModeRuleStockTeam:
            for (i = 0; i < ARRAY_COUNT(gSCManagerTransferBattleState.players); i++)
            {
                if (gSCManagerTransferBattleState.players[i].fkind != nFTKindNull)
                {
                    gSCManagerTransferBattleState.players[i].costume = ftParamGetCostumeTeamID(gSCManagerTransferBattleState.players[i].fkind, gSCManagerTransferBattleState.players[i].team);
                    gSCManagerTransferBattleState.players[i].shade = mnVSOnlineGetShade(i);
                }
            }
            break;
    }
}

// 0x80133B8C
void mnVSOnlineMain(GObj *gobj)
{
    GObj** buttons[/* */] = { &sMNVSOnlineButtonGObjVSStart, &sMNVSOnlineButtonGObjRule, &sMNVSOnlineButtonGObjTimeStock };

    s32 stick_range;
    s32 is_button;

    sMNVSOnlineTotalTimeTics++;

    if (sMNVSOnlineTotalTimeTics >= 10)
    {
        if (sMNVSOnlineTotalTimeTics == sMNVSOnlineReturnTic)
        {
            gSCManagerSceneData.scene_prev = gSCManagerSceneData.scene_curr;
            gSCManagerSceneData.scene_curr = nSCKindTitle;

            mnVSOnlineSaveSettings();
            syTaskmanSetLoadScene();

            return;
        }
        if (scSubsysControllerCheckNoInputAll() == FALSE)
        {
            sMNVSOnlineReturnTic = sMNVSOnlineTotalTimeTics + I_MIN_TO_TICS(5);
        }
        if (sMNVSOnlineExitInterrupt != 0)
        {
            syTaskmanSetLoadScene();
        }
        if (sMNVSOnlineChangeWait != 0)
        {
            sMNVSOnlineChangeWait--;
        }
        if
        (
            (scSubsysControllerGetPlayerStickInRangeLR(-20, 20) != FALSE) &&
            (scSubsysControllerGetPlayerStickInRangeUD(-20, 20) != FALSE) &&
            (scSubsysControllerGetPlayerHoldButtons(U_JPAD | R_JPAD | R_TRIG | U_CBUTTONS | R_CBUTTONS) == FALSE) &&
            (scSubsysControllerGetPlayerHoldButtons(D_JPAD | L_JPAD | L_TRIG | D_CBUTTONS | L_CBUTTONS) == FALSE)
        )
        {
            sMNVSOnlineChangeWait = 0;
            sMNVSOnlineInputDirection = nMNVSModeInputDirectionNone;
        }
        if (scSubsysControllerGetPlayerTapButtons(A_BUTTON | START_BUTTON) != FALSE)
        {
            switch (sMNVSOnlineCursorIndex)
            {
                case nMNVSModeOptionStart:
                    func_800269C0_275C0(nSYAudioFGMMenuSelect);
                    mnVSOnlineUpdateButton(sMNVSOnlineButtonGObjVSStart, nMNOptionTabStatusSelected);
                    mnVSOnlineSaveSettings();

                    sMNVSOnlineExitInterrupt = TRUE;

                    gSCManagerSceneData.scene_prev = gSCManagerSceneData.scene_curr;
                    gSCManagerSceneData.scene_curr = nSCKindVSNetAutomatch;

                    return;
                case nMNVSModeOptionRule:
                    func_800269C0_275C0(nSYAudioFGMMenuSelect);
                    mnVSOnlineUpdateButton(sMNVSOnlineButtonGObjRule, nMNOptionTabStatusSelected);
                    mnVSOnlineSaveSettings();

                    sMNVSOnlineExitInterrupt = TRUE;

                    gSCManagerSceneData.scene_prev = gSCManagerSceneData.scene_curr;
                    gSCManagerSceneData.scene_curr = nSCKindVSNetLevelPrefs;

                    return;
                case nMNVSModeOptionTimeStock:
                    break;
            }
        }

        if (scSubsysControllerGetPlayerTapButtons(B_BUTTON) != FALSE)
        {
            mnVSOnlineSaveSettings();

            gSCManagerSceneData.scene_prev = gSCManagerSceneData.scene_curr;
            gSCManagerSceneData.scene_curr = nSCKindVSMode;

            syTaskmanSetLoadScene();
        }
        if
        (
            mnCommonCheckGetOptionButtonInput(sMNVSOnlineChangeWait, is_button, U_JPAD | U_CBUTTONS) ||
            mnCommonCheckGetOptionStickInputUD(sMNVSOnlineChangeWait, stick_range, 20, 1)
        )
        {
            func_800269C0_275C0(nSYAudioFGMMenuScroll2);

            mnCommonSetOptionChangeWaitP(sMNVSOnlineChangeWait, is_button, stick_range, 7);

            if (sMNVSOnlineCursorIndex == nMNVSModeOptionRule)
            {
                sMNVSOnlineChangeWait += 8;
            }

            mnVSOnlineUpdateButton(*buttons[sMNVSOnlineCursorIndex], nMNOptionTabStatusNot);

            if (sMNVSOnlineCursorIndex == nMNVSModeOptionStart)
            {
                sMNVSOnlineCursorIndex = nMNVSModeOptionTimeStock;
            }
            else sMNVSOnlineCursorIndex--;

            mnVSOnlineUpdateButton(*buttons[sMNVSOnlineCursorIndex], nMNOptionTabStatusHighlight);

            sMNVSOnlineInputDirection = nMNVSModeInputDirectionUp;
        }

        if
        (
            mnCommonCheckGetOptionButtonInput(sMNVSOnlineChangeWait, is_button, D_JPAD | D_CBUTTONS) ||
            mnCommonCheckGetOptionStickInputUD(sMNVSOnlineChangeWait, stick_range, -20, 0)
        )
        {
            func_800269C0_275C0(nSYAudioFGMMenuScroll2);

            mnCommonSetOptionChangeWaitN(sMNVSOnlineChangeWait, is_button, stick_range, 7);

            if (sMNVSOnlineCursorIndex == nMNVSModeOptionTimeStock)
            {
                sMNVSOnlineChangeWait += 8;
            }

            mnVSOnlineUpdateButton(*buttons[sMNVSOnlineCursorIndex], nMNOptionTabStatusNot);

            if (sMNVSOnlineCursorIndex == nMNVSModeOptionTimeStock)
            {
                sMNVSOnlineCursorIndex = nMNVSModeOptionStart;
            }
            else sMNVSOnlineCursorIndex++;

            mnVSOnlineUpdateButton(*buttons[sMNVSOnlineCursorIndex], nMNOptionTabStatusHighlight);

            sMNVSOnlineInputDirection = nMNVSModeInputDirectionDown;
        }
    }
}

// 0x801345C4
void mnVSOnlineFuncStart(void)
{
    LBRelocSetup rl_setup;

    rl_setup.table_addr = (uintptr_t)&lLBRelocTableAddr;
    rl_setup.table_files_num = (u32)llRelocFileCount;
    rl_setup.file_heap = NULL;
    rl_setup.file_heap_size = 0;
    rl_setup.status_buffer = sMNVSOnlineStatusBuffer;
    rl_setup.status_buffer_size = ARRAY_COUNT(sMNVSOnlineStatusBuffer);
    rl_setup.force_status_buffer = NULL;
    rl_setup.force_status_buffer_size = 0;

    lbRelocInitSetup(&rl_setup);

    if (!(gSCManagerBackupData.error_flags & LBBACKUP_ERROR_RANDOMKNOCKBACK) && (gSCManagerBackupData.boot > 21) && (gSYMainImemOK == FALSE))
    {
        gSCManagerBackupData.error_flags |= LBBACKUP_ERROR_RANDOMKNOCKBACK;
    }
    lbRelocLoadFilesListed(dMNVSOnlineFileIDs, sMNVSOnlineFiles);
    gcMakeGObjSPAfter(0, mnVSOnlineMain, 0, GOBJ_PRIORITY_DEFAULT);
    gcMakeDefaultCameraGObj(0, GOBJ_PRIORITY_DEFAULT, 100, 0, GPACK_RGBA8888(0x00, 0x00, 0x00, 0x00));

    mnVSOnlineFuncStartVars();
    mnVSOnlineMakeBackgroundViewport();
    mnVSOnlineMakeMenuNameViewport();
    mnVSOnlineMakeButtonViewport();
    mnVSOnlineMakeButtonValuegSYRdpViewport();
    mnVSOnlineMakeBackground();
    mnVSOnlineMakeMenuName();
    mnVSOnlineMakeVSStartButton();
    mnVSOnlineMakeRuleButton();
    mnVSOnlineMakeTimeStockButton();

    if (gSCManagerSceneData.scene_prev == nSCKindPlayersVS ||
        gSCManagerSceneData.scene_prev == nSCKindVSOfflineClassic ||
        gSCManagerSceneData.scene_prev == nSCKindVSOnline ||
        gSCManagerSceneData.scene_prev == nSCKindVSNetAutomatch ||
        gSCManagerSceneData.scene_prev == nSCKindVSNetLevelPrefs)
    {
        syAudioPlayBGM(0, nSYAudioBGMModeSelect);
    }
}

// 0x80134880
SYVideoSetup dMNVSOnlineVideoSetup = SYVIDEO_SETUP_DEFAULT();

// 0x8013489C
SYTaskmanSetup dMNVSOnlineTaskmanSetup =
{
    // Task Manager Buffer Setup
    {
        0,                              // ???
        gcRunAll,                 	 	// Update function
        gcDrawAll,                      // Frame draw function
        &ovl19_BSS_END,                 // Allocatable memory pool start
        0,                              // Allocatable memory pool size
        1,                              // ???
        2,                              // Number of contexts?
        sizeof(Gfx) * 7500,             // Display List Buffer 0 Size
        0,                              // Display List Buffer 1 Size
        0,                              // Display List Buffer 2 Size
        0,                              // Display List Buffer 3 Size
        0x8000,                         // Graphics Heap Size
        2,                              // ???
        0xC000,                         // RDP Output Buffer Size
        mnVSOnlineFuncLights,    	        // Pre-render function
        syControllerFuncRead,           // Controller I/O function
    },

    0,                                  // Number of GObjThreads
    sizeof(u64) * 192,                  // Thread stack size
    0,                                  // Number of thread stacks
    0,                                  // ???
    0,                                  // Number of GObjProcesses
    0,                                  // Number of GObjs
    sizeof(GObj),                       // GObj size
    0,                                  // Number of XObjs
    NULL,                               // Matrix function list
    NULL,                               // DObjVec eject function
    0,                                  // Number of AObjs
    0,                                  // Number of MObjs
    0,                                  // Number of DObjs
    sizeof(DObj),                       // DObj size
    0,                                  // Number of SObjs
    sizeof(SObj),                       // SObj size
    0,                                  // Number of CObjs
    sizeof(CObj),                       // Camera size
    
    mnVSOnlineFuncStart          	        // Task start function
};

// 0x80134758
void mnVSModeOnlineStartScene(void)
{
    dMNVSOnlineVideoSetup.zbuffer = SYVIDEO_ZBUFFER_START(320, 240, 0, 10, u16);
    syVideoInit(&dMNVSOnlineVideoSetup);

    dMNVSOnlineTaskmanSetup.scene_setup.arena_size = (size_t) ((uintptr_t)&ovl1_VRAM - (uintptr_t)&ovl19_BSS_END);
    syTaskmanStartTask(&dMNVSOnlineTaskmanSetup);
}
