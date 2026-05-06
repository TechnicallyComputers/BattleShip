/*
 * port/net/menus/mnvsoffline.c — offline tier: full ROM-faithful VS rules menu (decomp-derived).
 * Prefixes: sMNVSOffClassic / mnVSOffClassic / mnVSModeOfflineClassicStartScene.
 * Tier nav: Back (B) returns to net VS hub (nSCKindVSMode / mnvsmodenet).
 */
#include <mn/menu.h>
#include <gm/gmsound.h>
#include <sc/scene.h>
#include <sys/video.h>
#include <sys/rdp.h>
#include <sys/controller.h>
#include <reloc_data.h>
#include <ft/ftparam.h>
#include <sys/audio.h>

extern void* func_800269C0_275C0(u16);

sb32 mnVSOffClassicIsTime(void);

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
u32 dMNVSOffClassicFileIDs[/* */] = { llMNCommonFileID, llMNVSModeFileID };

// 0x801347B8
Lights1 dMNVSOffClassicLights1 = gdSPDefLights1(0x20, 0x20, 0x20, 0xFF, 0xFF, 0xFF, 0x3C, 0x3C, 0x3C);

// 0x801347D0
Gfx dMNVSOffClassicDisplayList[/* */] =
{
    gsSPSetGeometryMode(G_LIGHTING),
    gsSPSetLights1(dMNVSOffClassicLights1),
    gsSPEndDisplayList()
};

// // // // // // // // // // // //
//                               //
//   GLOBAL / STATIC VARIABLES   //
//                               //
// // // // // // // // // // // //

// 0x80134930
GObj* sMNVSOffClassicButtonGObjVSStart;

// 0x80134934
GObj* sMNVSOffClassicButtonGObjRule;

// 0x80134938
GObj* sMNVSOffClassicButtonGObjTimeStock;

// 0x8013493C
GObj* sMNVSOffClassicButtonGObjVSOptions;

// 0x80134940 - Padding?
s32 sMNVSOffClassicPad0x80134940[2];

// 0x80134948
s32 sMNVSOffClassicCursorIndex;

// 0x8013494C
s32 sMNVSOffClassicRule;

// 0x80134950
s32 sMNVSOffClassicTime;

// 0x80134954
s32 sMNVSOffClassicStock;

// 0x80134958
GObj* sMNVSOffClassicRuleValueGObj;

// 0x8013495C
GObj* sMNVSOffClassicTimeStockValueGObj;

// 0x80134960
GObj* sMNVSOffClassicUnusedGObj;

// 0x80134964
GObj* sMNVSOffClassicRuleArrowsGObj;

// 0x80134968
GObj* sMNVSOffClassicTimeStockArrowsGObj;

// 0x8013496C
s32 sMNVSOffClassicTimeStockArrowBlinkTimer;

// 0x80134970
s32 sMNVSOffClassicRuleArrowBlinkTimer;

// 0x80134974
s32 sMNVSOffClassicExitInterrupt;

// 0x80134978
s32 sMNVSOffClassicInputDirection;

// 0x8013497C
s32 sMNVSOffClassicChangeWait;

// 0x80134980
s32 sMNVSOffClassicTotalTimeTics;

// 0x80134984
s32 sMNVSOffClassicReturnTic;

// 0x80134988
LBFileNode sMNVSOffClassicStatusBuffer[24];

// 0x80134A48
void *sMNVSOffClassicFiles[ARRAY_COUNT(dMNVSOffClassicFileIDs)];

// // // // // // // // // // // //
//                               //
//           FUNCTIONS           //
//                               //
// // // // // // // // // // // //

// 0x80131B00
void mnVSOffClassicFuncLights(Gfx **display_list)
{
    gSPDisplayList(display_list[0]++, dMNVSOffClassicDisplayList);
}

// 0x80131B24
s32 mnVSOffClassicPow(s32 num, s32 pow)
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
void mnVSOffClassicSetTextureColors(SObj* sobj, u32 *colors)
{
    sobj->sprite.attr &= ~SP_FASTCOPY;
    sobj->sprite.attr |= SP_TRANSPARENT;
    sobj->sprite.red = colors[0];
    sobj->sprite.green = colors[1];
    sobj->sprite.blue = colors[2];
}

// 0x80131BF4
s32 mnVSOffClassicGetNumberOfDigits(s32 num, s32 maxDigits)
{
    s32 numDigits;

    for (numDigits = maxDigits; numDigits > 0; numDigits--)
    {
        if (mnVSOffClassicPow(10, numDigits - 1) != 0 ? num / mnVSOffClassicPow(10, numDigits - 1) : 0 != 0) return numDigits;
    }

    return 0;
}

// 0x80131CA0
void mnVSOffClassicMakeNumber(GObj* number_gobj, s32 num, f32 x, f32 y, u32 *colors, s32 maxDigits, sb32 pad)
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

    number_sobj = lbCommonMakeSObjForGObj(number_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[0], number_offsets[num % 10]));
    mnVSOffClassicSetTextureColors(number_sobj, colors);
    left_x -= 11.0F;
    number_sobj->pos.x = left_x;
    number_sobj->pos.y = y;

    for
    (
        place = 1, numDigits = (pad != FALSE) ? maxDigits : mnVSOffClassicGetNumberOfDigits(num, maxDigits);
        place < numDigits;
        place++, numDigits = (pad != FALSE) ? maxDigits : mnVSOffClassicGetNumberOfDigits(num, maxDigits)
    )
    {
        digit = (mnVSOffClassicPow(10, place) != 0) ? num / mnVSOffClassicPow(10, place) : 0;

        number_sobj = lbCommonMakeSObjForGObj(number_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[0], number_offsets[digit % 10]));
        mnVSOffClassicSetTextureColors(number_sobj, colors);
        left_x -= 11.0F;
        number_sobj->pos.x = left_x;
        number_sobj->pos.y = y;
    }
}

// 0x80131F4C
void mnVSOffClassicUpdateButton(GObj* button_gobj, s32 button_status)
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
void mnVSOffClassicMakeButton(GObj* button_gobj, f32 x, f32 y, s32 arg3)
{
    SObj* button_sobj;

    button_sobj = lbCommonMakeSObjForGObj(button_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[0], llMNCommonOptionTabLeftSprite));
    button_sobj->sprite.attr &= ~SP_FASTCOPY;
    button_sobj->sprite.attr |= SP_TRANSPARENT;
    button_sobj->pos.x = x;
    button_sobj->pos.y = y;

    button_sobj = lbCommonMakeSObjForGObj(button_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[0], llMNCommonOptionTabMiddleSprite));
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

    button_sobj = lbCommonMakeSObjForGObj(button_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[0], llMNCommonOptionTabRightSprite));
    button_sobj->sprite.attr &= ~SP_FASTCOPY;
    button_sobj->sprite.attr |= SP_TRANSPARENT;
    button_sobj->pos.x = x + 16.0F + (arg3 * 8);
    button_sobj->pos.y = y;
}

// 0x80132154
void mnVSOffClassicMakeVSStartButton()
{
    GObj* button_gobj;
    SObj* button_sobj;

    sMNVSOffClassicButtonGObjVSStart = button_gobj = gcMakeGObjSPAfter(0, NULL, 4, GOBJ_PRIORITY_DEFAULT);
    gcAddGObjDisplay(button_gobj, lbCommonDrawSObjAttr, 2, GOBJ_PRIORITY_DEFAULT, ~0);
    mnVSOffClassicMakeButton(button_gobj, 120.0F, 31.0F, 17);

    mnVSOffClassicUpdateButton(button_gobj, (sMNVSOffClassicCursorIndex == 0) ? nMNOptionTabStatusHighlight : nMNOptionTabStatusNot);

    button_sobj = lbCommonMakeSObjForGObj(button_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeVSStartTextSprite));
    button_sobj->sprite.attr &= ~SP_FASTCOPY;
    button_sobj->sprite.attr |= SP_TRANSPARENT;
    button_sobj->sprite.red = 0x00;
    button_sobj->sprite.green = 0x00;
    button_sobj->sprite.blue = 0x00;
    button_sobj->pos.x = 153.0F;
    button_sobj->pos.y = 36.0F;
}

// 0x80132238
void mnVSOffClassicMakeRuleValue()
{
    GObj* rule_value_gobj;
#ifdef PORT
    SObj* rule_value_sobj = NULL;
#else
    SObj* rule_value_sobj;
#endif

    // 0x80134860
    SYColorRGB color = { 0xFF, 0xFF, 0xFF };

    sMNVSOffClassicRuleValueGObj = rule_value_gobj = gcMakeGObjSPAfter(0, NULL, 5, GOBJ_PRIORITY_DEFAULT);

    gcAddGObjDisplay(rule_value_gobj, lbCommonDrawSObjAttr, 3, GOBJ_PRIORITY_DEFAULT, ~0);

    switch (sMNVSOffClassicRule)
    {
        case nMNVSModeRuleStock:
            rule_value_sobj = lbCommonMakeSObjForGObj(rule_value_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeStockTextSprite));
            rule_value_sobj->sprite.attr &= ~SP_FASTCOPY;
            rule_value_sobj->sprite.attr |= SP_TRANSPARENT;
            rule_value_sobj->pos.x = 183.0F;
            rule_value_sobj->pos.y = 78.0F;
            rule_value_sobj->sprite.red = color.r;
            rule_value_sobj->sprite.green = color.g;
            rule_value_sobj->sprite.blue = color.b;
            return;
        case nMNVSModeRuleTime:
            rule_value_sobj = lbCommonMakeSObjForGObj(rule_value_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeTimeTextSprite));
            rule_value_sobj->sprite.attr &= ~SP_FASTCOPY;
            rule_value_sobj->sprite.attr |= SP_TRANSPARENT;
            rule_value_sobj->pos.x = 187.0F;
            rule_value_sobj->pos.y = 78.0F;
            rule_value_sobj->sprite.red = color.r;
            rule_value_sobj->sprite.green = color.g;
            rule_value_sobj->sprite.blue = color.b;
            return;
        case nMNVSModeRuleStockTeam:
            rule_value_sobj = lbCommonMakeSObjForGObj(rule_value_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeStockTextSprite));
            rule_value_sobj->sprite.attr &= ~SP_FASTCOPY;
            rule_value_sobj->sprite.attr |= SP_TRANSPARENT;
            rule_value_sobj->pos.x = 165.0F;
            rule_value_sobj->pos.y = 78.0F;
            rule_value_sobj->sprite.red = color.r;
            rule_value_sobj->sprite.green = color.g;
            rule_value_sobj->sprite.blue = color.b;

            rule_value_sobj = lbCommonMakeSObjForGObj(rule_value_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeTeamTextSprite));
            rule_value_sobj->sprite.attr &= ~SP_FASTCOPY;
            rule_value_sobj->sprite.attr |= SP_TRANSPARENT;
            rule_value_sobj->pos.x = 212.0F;
            rule_value_sobj->pos.y = 78.0F;
            rule_value_sobj->sprite.red = color.r;
            rule_value_sobj->sprite.green = color.g;
            rule_value_sobj->sprite.blue = color.b;
            return;
        case nMNVSModeRuleTimeTeam:
            rule_value_sobj = lbCommonMakeSObjForGObj(rule_value_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeTimeTextSprite));
            rule_value_sobj->sprite.attr &= ~SP_FASTCOPY;
            rule_value_sobj->sprite.attr |= SP_TRANSPARENT;
            rule_value_sobj->pos.x = 168.0F;
            rule_value_sobj->pos.y = 78.0F;
            rule_value_sobj->sprite.red = color.r;
            rule_value_sobj->sprite.green = color.g;
            rule_value_sobj->sprite.blue = color.b;

            rule_value_sobj = lbCommonMakeSObjForGObj(rule_value_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeTeamTextSprite));
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
SObj* mnVSOffClassicGetArrowSObj(GObj* arrow_gobj, s32 direction)
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
void mnVSOffClassicMakeLeftArrow(GObj* arrow_gobj, f32 x, f32 y)
{
    SObj* arrow_sobj;

    arrow_sobj = lbCommonMakeSObjForGObj(arrow_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[0], llMNCommonArrowLSprite));
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
void mnVSOffClassicMakeRightArrow(GObj* arrow_gobj, f32 x, f32 y)
{
    SObj* arrow_sobj;

    arrow_sobj = lbCommonMakeSObjForGObj(arrow_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[0], llMNCommonArrowRSprite));
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
void mnVSOffClassicAnimateRuleArrows(GObj* rule_arrows_gobj)
{
    SObj* arrow_sobj;

    while (TRUE)
    {
        if (sMNVSOffClassicCursorIndex == 1)
        {
            sMNVSOffClassicRuleArrowBlinkTimer--;

            if (sMNVSOffClassicRuleArrowBlinkTimer == 0)
            {
                if (rule_arrows_gobj->flags == GOBJ_FLAG_HIDDEN)
                {
                    rule_arrows_gobj->flags = GOBJ_FLAG_NONE;
                }
                else rule_arrows_gobj->flags = GOBJ_FLAG_HIDDEN;

                sMNVSOffClassicRuleArrowBlinkTimer = 30;
            }

            if (sMNVSOffClassicRule == nMNVSModeRuleTime)
            {
                arrow_sobj = mnVSOffClassicGetArrowSObj(rule_arrows_gobj, 0);

                if (arrow_sobj != NULL)
                {
                    gcEjectSObj(arrow_sobj);
                }
            }
            else if (mnVSOffClassicGetArrowSObj(rule_arrows_gobj, 0) == NULL)
            {
                mnVSOffClassicMakeLeftArrow(rule_arrows_gobj, 165.0F, 70.0F);
            }

            if (sMNVSOffClassicRule == nMNVSModeRuleStockTeam)
            {
                arrow_sobj = mnVSOffClassicGetArrowSObj(rule_arrows_gobj, 1);

                if (arrow_sobj != NULL)
                {
                    gcEjectSObj(arrow_sobj);
                }
            }
            else if (mnVSOffClassicGetArrowSObj(rule_arrows_gobj, 1) == NULL)
            {
                mnVSOffClassicMakeRightArrow(rule_arrows_gobj, 250.0F, 70.0F);
            }
        }
        else rule_arrows_gobj->flags = GOBJ_FLAG_HIDDEN;

        gcSleepCurrentGObjThread(1);
    }
}

// 0X80132818
void mnVSOffClassicMakeRuleArrows()
{
    GObj* rule_arrows_gobj;

    if (sMNVSOffClassicRuleArrowsGObj != NULL)
    {
        gcEjectGObj(sMNVSOffClassicRuleArrowsGObj);

        sMNVSOffClassicRuleArrowsGObj = NULL;
    }
    sMNVSOffClassicRuleArrowsGObj = rule_arrows_gobj = gcMakeGObjSPAfter(0, NULL, 4, GOBJ_PRIORITY_DEFAULT);

    gcAddGObjDisplay(rule_arrows_gobj, lbCommonDrawSObjAttr, 2, GOBJ_PRIORITY_DEFAULT, ~0);
    gcAddGObjProcess(rule_arrows_gobj, mnVSOffClassicAnimateRuleArrows, 0, 1);
}

// 0x801328A8
void mnVSOffClassicAnimateTimeStockArrows(GObj* time_stock_arrows_gobj)
{
    while (TRUE)
    {
        if (sMNVSOffClassicCursorIndex == 2)
        {
            sMNVSOffClassicTimeStockArrowBlinkTimer--;

            if (sMNVSOffClassicTimeStockArrowBlinkTimer == 0)
            {
                if (time_stock_arrows_gobj->flags == GOBJ_FLAG_HIDDEN)
                {
                    time_stock_arrows_gobj->flags = GOBJ_FLAG_NONE;
                }
                else time_stock_arrows_gobj->flags = GOBJ_FLAG_HIDDEN;

                sMNVSOffClassicTimeStockArrowBlinkTimer = 30;
            }
        }
        else time_stock_arrows_gobj->flags = GOBJ_FLAG_HIDDEN;

        gcSleepCurrentGObjThread(1);
    }
}

// 0x80132964
void mnVSOffClassicMakeTimeStockArrows()
{
    GObj* time_stock_arrows_gobj;

    if (sMNVSOffClassicTimeStockArrowsGObj != NULL)
    {
        gcEjectGObj(sMNVSOffClassicTimeStockArrowsGObj);

        sMNVSOffClassicTimeStockArrowsGObj = NULL;
    }

    sMNVSOffClassicTimeStockArrowsGObj = time_stock_arrows_gobj = gcMakeGObjSPAfter(0, NULL, 4, GOBJ_PRIORITY_DEFAULT);
    gcAddGObjDisplay(time_stock_arrows_gobj, lbCommonDrawSObjAttr, 2, GOBJ_PRIORITY_DEFAULT, ~0);
    gcAddGObjProcess(time_stock_arrows_gobj, mnVSOffClassicAnimateTimeStockArrows, nGCProcessKindThread, 1);

    if (mnVSOffClassicIsTime() != FALSE)
    {
        mnVSOffClassicMakeLeftArrow(time_stock_arrows_gobj, 155.0F, 109.0F);
        mnVSOffClassicMakeRightArrow(time_stock_arrows_gobj, 230.0F, 109.0F);
    }
    else
    {
        mnVSOffClassicMakeLeftArrow(time_stock_arrows_gobj, 165.0F, 109.0F);
        mnVSOffClassicMakeRightArrow(time_stock_arrows_gobj, 230.0F, 109.0F);
    }
}

// 0x80132A4C
void mnVSOffClassicMakeRuleButton()
{
    GObj* rule_button_gobj;
    SObj* button_sobj;

    sMNVSOffClassicButtonGObjRule = rule_button_gobj = gcMakeGObjSPAfter(0, NULL, 4, GOBJ_PRIORITY_DEFAULT);
    gcAddGObjDisplay(rule_button_gobj, lbCommonDrawSObjAttr, 2, GOBJ_PRIORITY_DEFAULT, ~0);
    mnVSOffClassicMakeButton(rule_button_gobj, 97.0F, 70.0F, 17);

    mnVSOffClassicUpdateButton(rule_button_gobj, (sMNVSOffClassicCursorIndex == nMNVSModeOptionRule) ? nMNOptionTabStatusHighlight : nMNOptionTabStatusNot);

    button_sobj = lbCommonMakeSObjForGObj(rule_button_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeRulePeriodTextSprite));
    button_sobj->sprite.attr &= ~SP_FASTCOPY;
    button_sobj->sprite.attr |= SP_TRANSPARENT;
    button_sobj->sprite.red = 0x00;
    button_sobj->sprite.green = 0x00;
    button_sobj->sprite.blue = 0x00;
    button_sobj->pos.x = 108.0F;
    button_sobj->pos.y = 75.0F;

    mnVSOffClassicMakeRuleArrows();
}

// 0x80132B38
sb32 mnVSOffClassicIsTime()
{
    if ((sMNVSOffClassicRule == nMNVSModeRuleTime) || (sMNVSOffClassicRule == nMNVSModeRuleTimeTeam))
    {
        return TRUE;
    }
    else return FALSE;
}

// 0x80132B68
s32 mnVSOffClassicGetTimeStockValue()
{
    if (mnVSOffClassicIsTime() != FALSE)
    {
        return sMNVSOffClassicTime;
    }
    else return sMNVSOffClassicStock + 1;
}

// 0x80132BA0
void mnVSOffClassicMakeTimeStockValue()
{
    GObj* time_stock_value_gobj;
    SObj* time_stock_value_sobj;
    s32 value;
    s32 x;
    s32 unused;

    // 0x80134864
    u32 colors[/* */] = { 0x000000FF, 0x000000FF, 0x000000FF };

    sMNVSOffClassicTimeStockValueGObj = time_stock_value_gobj = gcMakeGObjSPAfter(0, NULL, 5, GOBJ_PRIORITY_DEFAULT);
    gcAddGObjDisplay(time_stock_value_gobj, lbCommonDrawSObjAttr, 3, GOBJ_PRIORITY_DEFAULT, ~0);

    value = mnVSOffClassicGetTimeStockValue();

    if (value == SCBATTLE_TIMELIMIT_INFINITE)
    {
        time_stock_value_sobj = lbCommonMakeSObjForGObj(time_stock_value_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[0], llMNCommonInfinitySprite));
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
        if (mnVSOffClassicIsTime() != FALSE)
        {
            x = (value < 10) ? 185 : 190;
        }
        else
        {
            x = (value < 10) ? 210 : 215;
        }
        mnVSOffClassicMakeNumber(time_stock_value_gobj, value, (f32)x, 116.0F, colors, 2, 0);
    }
}

// 0x80132D04
void mnVSOffClassicMakeTimeStockButton()
{
    GObj* time_stock_button_gobj;
    SObj* time_stock_button_sobj;

    sMNVSOffClassicButtonGObjTimeStock = time_stock_button_gobj = gcMakeGObjSPAfter(0, NULL, 4, GOBJ_PRIORITY_DEFAULT);
    gcAddGObjDisplay(time_stock_button_gobj, lbCommonDrawSObjAttr, 2, GOBJ_PRIORITY_DEFAULT, ~0);
    mnVSOffClassicMakeButton(time_stock_button_gobj, 74.0F, 109.0F, 17);

    mnVSOffClassicUpdateButton(time_stock_button_gobj, (sMNVSOffClassicCursorIndex == nMNVSModeOptionTimeStock) ? nMNOptionTabStatusHighlight : nMNOptionTabStatusNot);

    if ((sMNVSOffClassicRule == nMNVSModeRuleTime) || (sMNVSOffClassicRule == nMNVSModeRuleTimeTeam))
    {
        time_stock_button_sobj = lbCommonMakeSObjForGObj(time_stock_button_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeTimePeriodTextSprite));
        time_stock_button_sobj->sprite.attr &= ~SP_FASTCOPY;
        time_stock_button_sobj->sprite.attr |= SP_TRANSPARENT;
        time_stock_button_sobj->sprite.red = 0x00;
        time_stock_button_sobj->sprite.green = 0x00;
        time_stock_button_sobj->sprite.blue = 0x00;
        time_stock_button_sobj->pos.x = 97.0F;
        time_stock_button_sobj->pos.y = 113.0F;

        time_stock_button_sobj = lbCommonMakeSObjForGObj(time_stock_button_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeMinTextSprite));
        time_stock_button_sobj->sprite.attr &= ~SP_FASTCOPY;
        time_stock_button_sobj->sprite.attr |= SP_TRANSPARENT;
        time_stock_button_sobj->sprite.red = 0x00;
        time_stock_button_sobj->sprite.green = 0x00;
        time_stock_button_sobj->sprite.blue = 0x00;
        time_stock_button_sobj->pos.x = 197.0F;
        time_stock_button_sobj->pos.y = 120.0F;
    }
    else
    {
        time_stock_button_sobj = lbCommonMakeSObjForGObj(time_stock_button_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeStockPeriodTextSprite));
        time_stock_button_sobj->sprite.attr &= ~SP_FASTCOPY;
        time_stock_button_sobj->sprite.attr |= SP_TRANSPARENT;
        time_stock_button_sobj->sprite.red = 0x00;
        time_stock_button_sobj->sprite.green = 0x00;
        time_stock_button_sobj->sprite.blue = 0x00;
        time_stock_button_sobj->pos.x = 106.0F;
        time_stock_button_sobj->pos.y = 114.0F;
    }

    mnVSOffClassicMakeTimeStockArrows();
}

// 0x80132EBC
void mnVSOffClassicMakeVSOptionsButton(void)
{
    GObj* button_gobj;
    SObj* button_sobj;

    sMNVSOffClassicButtonGObjVSOptions = button_gobj = gcMakeGObjSPAfter(0, NULL, 4, GOBJ_PRIORITY_DEFAULT);
    gcAddGObjDisplay(button_gobj, lbCommonDrawSObjAttr, 2, GOBJ_PRIORITY_DEFAULT, ~0);
    mnVSOffClassicMakeButton(button_gobj, 51.0F, 148.0F, 17);
    mnVSOffClassicUpdateButton(button_gobj, (sMNVSOffClassicCursorIndex == nMNVSModeOptionOptions) ? nMNOptionTabStatusHighlight : nMNOptionTabStatusNot);

    button_sobj = lbCommonMakeSObjForGObj(button_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeVSOptionsTextSprite));
    button_sobj->sprite.attr &= ~SP_FASTCOPY;
    button_sobj->sprite.attr |= SP_TRANSPARENT;
    button_sobj->sprite.red = 0x00;
    button_sobj->sprite.green = 0x00;
    button_sobj->sprite.blue = 0x00;
#if defined(REGION_US)
    button_sobj->pos.x = 71.0F;
#else
    button_sobj->pos.x = 98.0F;
#endif
    button_sobj->pos.y = 151.0F;
}

// 0x80132FA4 - Unused?
void mnVSOffClassicSetSubtitleSpriteColors(SObj* sobj)
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
void mnVSOffClassicMakeSubtitle(void)
{
    GObj *gobj;
    SObj *sobj;

    sMNVSOffClassicUnusedGObj = gobj = gcMakeGObjSPAfter(0, NULL, 5, GOBJ_PRIORITY_DEFAULT);

#if defined(REGION_JP)
    gcAddGObjDisplay(gobj, lbCommonDrawSObjAttr, 3, GOBJ_PRIORITY_DEFAULT, ~0);

    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[0], llMNCommonFrameSprite));

    sobj->pos.x = 93.0F;
    sobj->pos.y = 189.0F;
    
    mnVSOffClassicSetSubtitleSpriteColors(sobj);
    
    switch (sMNVSOffClassicCursorIndex)
    {
        case nMNVSModeOptionStart:
            sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeVSStartTextJapSprite));
            
            sobj->pos.x = 121.0F;
            sobj->pos.y = 195.0F;
            
            mnVSOffClassicSetSubtitleSpriteColors(sobj);
            return;

        case nMNVSModeOptionRule:
            switch (sMNVSOffClassicRule)
            {
                case nMNVSModeRuleTime:
                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeTimeBasedTextJapSprite));
                    
                    sobj->pos.x = 120.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOffClassicSetSubtitleSpriteColors(sobj);

                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeBattleTextJapSprite));
                    
                    sobj->pos.x = 166.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOffClassicSetSubtitleSpriteColors(sobj);
                    break;
                case nMNVSModeRuleStock:
                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeStockTextJapSprite));
                    
                    sobj->pos.x = 115.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOffClassicSetSubtitleSpriteColors(sobj);

                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeBattleTextJapSprite));
                    
                    sobj->pos.x = 172.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOffClassicSetSubtitleSpriteColors(sobj);
                    break;
                case nMNVSModeRuleTimeTeam:
                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeTimeBasedTextJapSprite));
                    
                    sobj->pos.x = 102.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOffClassicSetSubtitleSpriteColors(sobj);

                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeTeamTextJapSprite));
                    
                    sobj->pos.x = 148.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOffClassicSetSubtitleSpriteColors(sobj);

                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeBattleTextJapSprite));
                    
                    sobj->pos.x = 182.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOffClassicSetSubtitleSpriteColors(sobj);
                    break;
                case nMNVSModeRuleStockTeam:
                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeStockTextJapSprite));
                    
                    sobj->pos.x = 98.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOffClassicSetSubtitleSpriteColors(sobj);

                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeTeamTextJapSprite));
                    
                    sobj->pos.x = 154.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOffClassicSetSubtitleSpriteColors(sobj);

                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeBattleTextJapSprite));
                    
                    sobj->pos.x = 188.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOffClassicSetSubtitleSpriteColors(sobj);
                    break;
            }

            return;

        case nMNVSModeOptionTimeStock:
            switch (sMNVSOffClassicRule)
            {
                case nMNVSModeRuleStock:
                case nMNVSModeRuleStockTeam:
                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeRemaningPlayersTextJapSprite));
                    
                    sobj->pos.x = 126.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOffClassicSetSubtitleSpriteColors(sobj);
                    break;
                case nMNVSModeRuleTime:
                case nMNVSModeRuleTimeTeam:
                    sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeTimeLimitTextJapSprite));
                    
                    sobj->pos.x = 132.0F;
                    sobj->pos.y = 195.0F;
                    
                    mnVSOffClassicSetSubtitleSpriteColors(sobj);
                    break;
            }
            return;

        case nMNVSModeOptionOptions:
            sobj = lbCommonMakeSObjForGObj(gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeMatchOptionsTextJapSprite));
            
            sobj->pos.x = 111.0F;
            sobj->pos.y = 195.0F;
            
            mnVSOffClassicSetSubtitleSpriteColors(sobj);
            return;
    }
#endif
}

// 0x80133008
void mnVSOffClassicRenderMenuName(GObj* menu_name_gobj)
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
void mnVSOffClassicMakeMenuName()
{
    GObj* menu_name_gobj;
    SObj* menu_name_sobj;

    menu_name_gobj = gcMakeGObjSPAfter(0, NULL, 3, GOBJ_PRIORITY_DEFAULT);
    gcAddGObjDisplay(menu_name_gobj, mnVSOffClassicRenderMenuName, 1, GOBJ_PRIORITY_DEFAULT, ~0);

    menu_name_sobj = lbCommonMakeSObjForGObj(menu_name_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[0], llMNCommonSmashLogoSprite));
    menu_name_sobj->sprite.attr &= ~SP_FASTCOPY;
    menu_name_sobj->sprite.attr |= SP_TRANSPARENT;
    menu_name_sobj->sprite.red = 0x00;
    menu_name_sobj->sprite.green = 0x00;
    menu_name_sobj->sprite.blue = 0x00;
    menu_name_sobj->pos.x = 235.0F;
    menu_name_sobj->pos.y = 158.0F;

    menu_name_sobj = lbCommonMakeSObjForGObj(menu_name_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeVSTextSprite));
    menu_name_sobj->sprite.attr &= ~SP_FASTCOPY;
    menu_name_sobj->sprite.attr |= SP_TRANSPARENT;
    menu_name_sobj->sprite.red = 0x00;
    menu_name_sobj->sprite.green = 0x00;
    menu_name_sobj->sprite.blue = 0x00;
    menu_name_sobj->pos.x = 158.0F;
    menu_name_sobj->pos.y = 192.0F;

    menu_name_sobj = lbCommonMakeSObjForGObj(menu_name_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[0], llMNCommonGameModeTextSprite));
    menu_name_sobj->sprite.attr &= ~SP_FASTCOPY;
    menu_name_sobj->sprite.attr |= SP_TRANSPARENT;
    menu_name_sobj->sprite.red = 0x00;
    menu_name_sobj->sprite.green = 0x00;
    menu_name_sobj->sprite.blue = 0x00;
    menu_name_sobj->pos.x = 189.0F;
    menu_name_sobj->pos.y = 87.0F;
}

// 0x80133298
void mnVSOffClassicMakeBackground(void)
{
    GObj* bg_gobj;
    SObj* bg_sobj;

    bg_gobj = gcMakeGObjSPAfter(0, NULL, 2, GOBJ_PRIORITY_DEFAULT);
    gcAddGObjDisplay(bg_gobj, lbCommonDrawSObjAttr, 0, GOBJ_PRIORITY_DEFAULT, ~0);

    bg_sobj = lbCommonMakeSObjForGObj(bg_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[0], llMNCommonSmashBrosCollageSprite));
    bg_sobj->pos.x = 10.0F;
    bg_sobj->pos.y = 10.0F;

    bg_sobj = lbCommonMakeSObjForGObj(bg_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[0], llMNCommonDecalPaperSprite));
    bg_sobj->sprite.attr &= ~SP_FASTCOPY;
    bg_sobj->sprite.attr |= SP_TRANSPARENT;
    bg_sobj->sprite.red = 0xA0;
    bg_sobj->sprite.green = 0x78;
    bg_sobj->sprite.blue = 0x14;
    bg_sobj->pos.x = 140.0F;
    bg_sobj->pos.y = 143.0F;

    bg_sobj = lbCommonMakeSObjForGObj(bg_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[0], llMNCommonDecalPaperSprite));
    bg_sobj->sprite.attr &= ~SP_FASTCOPY;
    bg_sobj->sprite.attr |= SP_TRANSPARENT;
    bg_sobj->sprite.red = 0xA0;
    bg_sobj->sprite.green = 0x78;
    bg_sobj->sprite.blue = 0x14;
    bg_sobj->pos.x = 225.0F;
    bg_sobj->pos.y = 56.0F;

    bg_sobj = lbCommonMakeSObjForGObj(bg_gobj, lbRelocGetFileData(Sprite*, sMNVSOffClassicFiles[1], llMNVSModeConsoleIconDarkSprite));
    bg_sobj->sprite.attr &= ~SP_FASTCOPY;
    bg_sobj->sprite.attr |= SP_TRANSPARENT;
    bg_sobj->sprite.red = 0x99;
    bg_sobj->sprite.green = 0x99;
    bg_sobj->sprite.blue = 0x99;
    bg_sobj->pos.x = 10.0F;
    bg_sobj->pos.y = 10.0F;
}

// 0x8013342C
void mnVSOffClassicMakeButtonValuegSYRdpViewport(void)
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
void mnVSOffClassicMakeButtonViewport(void)
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
void mnVSOffClassicMakeMenuNameViewport(void)
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
void mnVSOffClassicMakeBackgroundViewport()
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
void mnVSOffClassicFuncStartVars()
{
    if (gSCManagerSceneData.scene_prev == nSCKindVSOptions)
    {
        sMNVSOffClassicCursorIndex = nMNVSModeOptionOptions;
    }
    else sMNVSOffClassicCursorIndex = nMNVSModeOptionStart;

    sMNVSOffClassicChangeWait = 0;

    switch (gSCManagerTransferBattleState.is_team_battle)
    {
        case FALSE:
            if (gSCManagerTransferBattleState.game_rules == SCBATTLE_GAMERULE_TIME)
            {
                sMNVSOffClassicRule = nMNVSModeRuleTime;
            }
            else sMNVSOffClassicRule = nMNVSModeRuleStock;

            break;
        case TRUE:
            if (gSCManagerTransferBattleState.game_rules == SCBATTLE_GAMERULE_TIME)
            {
                sMNVSOffClassicRule = nMNVSModeRuleTimeTeam;
            }
            else sMNVSOffClassicRule = nMNVSModeRuleStockTeam;

            break;
    }

    sMNVSOffClassicTime = gSCManagerTransferBattleState.time_limit;
    sMNVSOffClassicStock = gSCManagerTransferBattleState.stocks;
    sMNVSOffClassicTimeStockArrowsGObj = 0;
    sMNVSOffClassicRuleArrowsGObj = 0;
    sMNVSOffClassicInputDirection = nMNVSModeInputDirectionNone;
    sMNVSOffClassicTotalTimeTics = 0;
    sMNVSOffClassicExitInterrupt = 0;
    sMNVSOffClassicReturnTic = sMNVSOffClassicTotalTimeTics + I_MIN_TO_TICS(5);
    sMNVSOffClassicTimeStockArrowBlinkTimer = 0;
    sMNVSOffClassicRuleArrowBlinkTimer = 0;
}

// 0x801337B8
void mnVSOffClassicSaveSettings()
{
    gSCManagerTransferBattleState.time_limit = sMNVSOffClassicTime;
    gSCManagerTransferBattleState.stocks = sMNVSOffClassicStock;

    switch (sMNVSOffClassicRule)
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
s32 mnVSOffClassicGetShade(s32 player)
{
    sb32 is_same_costume[GMCOMMON_PLAYERS_MAX];
    s32 i;

    if ((sMNVSOffClassicRule == nMNVSModeRuleTime) || (sMNVSOffClassicRule == nMNVSModeRuleStock))
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
s32 mnVSOffClassicGetCostume(s32 fkind, s32 arg1)
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
void mnVSOffClassicSetCostumesAndShades(void)
{
    s32 i;

    switch (sMNVSOffClassicRule)
    {
        case nMNVSModeRuleTime:
        case nMNVSModeRuleStock:
            for (i = 0; i < ARRAY_COUNT(gSCManagerTransferBattleState.players); i++)
            {
                if (gSCManagerTransferBattleState.players[i].fkind != nFTKindNull)
                {
                    gSCManagerTransferBattleState.players[i].costume = ftParamGetCostumeCommonID(gSCManagerTransferBattleState.players[i].fkind, mnVSOffClassicGetCostume(gSCManagerTransferBattleState.players[i].fkind, i));
                    gSCManagerTransferBattleState.players[i].shade = mnVSOffClassicGetShade(i);
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
                    gSCManagerTransferBattleState.players[i].shade = mnVSOffClassicGetShade(i);
                }
            }
            break;
    }
}

// 0x80133B8C
void mnVSOffClassicMain(GObj *gobj)
{
    s32 unused;

    // 0x80134870
    GObj** buttons[/* */] = { &sMNVSOffClassicButtonGObjVSStart, &sMNVSOffClassicButtonGObjRule, &sMNVSOffClassicButtonGObjTimeStock, &sMNVSOffClassicButtonGObjVSOptions };
    
    s32 stick_range;
    s32 is_button;

    sMNVSOffClassicTotalTimeTics++;

    if (sMNVSOffClassicTotalTimeTics >= 10)
    {
        if (sMNVSOffClassicTotalTimeTics == sMNVSOffClassicReturnTic)
        {
            gSCManagerSceneData.scene_prev = gSCManagerSceneData.scene_curr;
            gSCManagerSceneData.scene_curr = nSCKindTitle;

            mnVSOffClassicSaveSettings();
            syTaskmanSetLoadScene();

            return;
        }
        if (scSubsysControllerCheckNoInputAll() == FALSE)
        {
            sMNVSOffClassicReturnTic = sMNVSOffClassicTotalTimeTics + I_MIN_TO_TICS(5);
        }
        if (sMNVSOffClassicExitInterrupt != 0)
        {
            syTaskmanSetLoadScene();
        }
        if (sMNVSOffClassicChangeWait != 0)
        {
            sMNVSOffClassicChangeWait--;
        }
        if
        (
            (scSubsysControllerGetPlayerStickInRangeLR(-20, 20) != FALSE) &&
            (scSubsysControllerGetPlayerStickInRangeUD(-20, 20) != FALSE) &&
            (scSubsysControllerGetPlayerHoldButtons(U_JPAD | R_JPAD | R_TRIG | U_CBUTTONS | R_CBUTTONS) == FALSE) &&
            (scSubsysControllerGetPlayerHoldButtons(D_JPAD | L_JPAD | L_TRIG | D_CBUTTONS | L_CBUTTONS) == FALSE)
        )
        {
            sMNVSOffClassicChangeWait = 0;
            sMNVSOffClassicInputDirection = nMNVSModeInputDirectionNone;
        }
        if (scSubsysControllerGetPlayerTapButtons(A_BUTTON | START_BUTTON) != FALSE)
        {
            switch (sMNVSOffClassicCursorIndex)
            {
                case nMNVSModeOptionStart:
                    func_800269C0_275C0(nSYAudioFGMMenuSelect);
                    mnVSOffClassicUpdateButton(sMNVSOffClassicButtonGObjVSStart, nMNOptionTabStatusSelected);
                    mnVSOffClassicSaveSettings();

                    sMNVSOffClassicExitInterrupt = TRUE;

                    gSCManagerSceneData.scene_prev = gSCManagerSceneData.scene_curr;
                    gSCManagerSceneData.scene_curr = nSCKindPlayersVS;

                    return;
                case nMNVSModeOptionOptions:
                    func_800269C0_275C0(nSYAudioFGMMenuSelect);
                    mnVSOffClassicUpdateButton(sMNVSOffClassicButtonGObjVSOptions, nMNOptionTabStatusSelected);
                    mnVSOffClassicSaveSettings();

                    sMNVSOffClassicExitInterrupt = TRUE;

                    gSCManagerSceneData.scene_prev = gSCManagerSceneData.scene_curr;
                    gSCManagerSceneData.scene_curr = nSCKindVSOptions;

                    return;
            }
        }

        if (scSubsysControllerGetPlayerTapButtons(B_BUTTON) != FALSE)
        {
            mnVSOffClassicSaveSettings();

            gSCManagerSceneData.scene_prev = gSCManagerSceneData.scene_curr;
            gSCManagerSceneData.scene_curr = nSCKindVSMode;

            syTaskmanSetLoadScene();
        }
        if
        (
            mnCommonCheckGetOptionButtonInput(sMNVSOffClassicChangeWait, is_button, U_JPAD | U_CBUTTONS) ||
            mnCommonCheckGetOptionStickInputUD(sMNVSOffClassicChangeWait, stick_range, 20, 1)
        )
        {
            func_800269C0_275C0(nSYAudioFGMMenuScroll2);

            mnCommonSetOptionChangeWaitP(sMNVSOffClassicChangeWait, is_button, stick_range, 7);

            if (sMNVSOffClassicCursorIndex == nMNVSModeOptionRule)
            {
                sMNVSOffClassicChangeWait += 8;
            }
            mnVSOffClassicUpdateButton(*buttons[sMNVSOffClassicCursorIndex], nMNOptionTabStatusNot);

            if (sMNVSOffClassicCursorIndex == nMNVSModeOptionStart)
            {
                sMNVSOffClassicCursorIndex = nMNVSModeOptionOptions;
            }
            else sMNVSOffClassicCursorIndex--;

            mnVSOffClassicUpdateButton(*buttons[sMNVSOffClassicCursorIndex], nMNOptionTabStatusHighlight);
            gcEjectGObj(sMNVSOffClassicUnusedGObj);
            mnVSOffClassicMakeSubtitle();

            sMNVSOffClassicInputDirection = nMNVSModeInputDirectionUp;

            if (sMNVSOffClassicCursorIndex == nMNVSModeOptionRule)
            {
                sMNVSOffClassicRuleArrowsGObj->flags = GOBJ_FLAG_NONE;
                sMNVSOffClassicRuleArrowBlinkTimer = 30;
            }

            if (sMNVSOffClassicCursorIndex == nMNVSModeOptionTimeStock)
            {
                sMNVSOffClassicTimeStockArrowsGObj->flags = GOBJ_FLAG_NONE;
                sMNVSOffClassicTimeStockArrowBlinkTimer = 30;
            }
        }

        if
        (
            mnCommonCheckGetOptionButtonInput(sMNVSOffClassicChangeWait, is_button, D_JPAD | D_CBUTTONS) ||
            mnCommonCheckGetOptionStickInputUD(sMNVSOffClassicChangeWait, stick_range, -20, 0)
        )
        {
            func_800269C0_275C0(nSYAudioFGMMenuScroll2);

            mnCommonSetOptionChangeWaitN(sMNVSOffClassicChangeWait, is_button, stick_range, 7);

            if (sMNVSOffClassicCursorIndex == nMNVSModeOptionTimeStock)
            {
                sMNVSOffClassicChangeWait += 8;
            }

            mnVSOffClassicUpdateButton(*buttons[sMNVSOffClassicCursorIndex], nMNOptionTabStatusNot);

            if (sMNVSOffClassicCursorIndex == nMNVSModeOptionOptions)
            {
                sMNVSOffClassicCursorIndex = nMNVSModeOptionStart;
            }
            else sMNVSOffClassicCursorIndex++;

            mnVSOffClassicUpdateButton(*buttons[sMNVSOffClassicCursorIndex], nMNOptionTabStatusHighlight);
            gcEjectGObj(sMNVSOffClassicUnusedGObj);
            mnVSOffClassicMakeSubtitle();

            sMNVSOffClassicInputDirection = nMNVSModeInputDirectionDown;

            if (sMNVSOffClassicCursorIndex == 1)
            {
                sMNVSOffClassicRuleArrowsGObj->flags = GOBJ_FLAG_NONE;
                sMNVSOffClassicRuleArrowBlinkTimer = 30;
            }

            if (sMNVSOffClassicCursorIndex == 2)
            {
                sMNVSOffClassicTimeStockArrowsGObj->flags = GOBJ_FLAG_NONE;
                sMNVSOffClassicTimeStockArrowBlinkTimer = 30;
            }
        }

        switch (sMNVSOffClassicCursorIndex)
        {
            case nMNVSModeOptionRule:
                if
                (
                    mnCommonCheckGetOptionButtonInput(sMNVSOffClassicChangeWait, is_button, L_JPAD | L_TRIG | L_CBUTTONS) ||
                    mnCommonCheckGetOptionStickInputLR(sMNVSOffClassicChangeWait, stick_range, -20, 0)
                )
                {
                    if (sMNVSOffClassicRule > nMNVSModeRuleTime)
                    {
                        func_800269C0_275C0(nSYAudioFGMMenuScroll1);

                        sMNVSOffClassicRule--;

                        if (sMNVSOffClassicRule == nMNVSModeRuleStock)
                        {
                            mnVSOffClassicSetCostumesAndShades();
                        }

                        gcEjectGObj(sMNVSOffClassicButtonGObjTimeStock);
                        mnVSOffClassicMakeTimeStockButton();
                        gcEjectGObj(sMNVSOffClassicRuleValueGObj);
                        mnVSOffClassicMakeRuleValue();
                        gcEjectGObj(sMNVSOffClassicTimeStockValueGObj);
                        mnVSOffClassicMakeTimeStockValue();

                        mnCommonSetOptionChangeWaitN(sMNVSOffClassicChangeWait, is_button, stick_range, 7);

                        gcEjectGObj(sMNVSOffClassicUnusedGObj);
                        mnVSOffClassicMakeSubtitle();

                        sMNVSOffClassicInputDirection = nMNVSModeInputDirectionLeft;
                    }
                }

                if (
                    mnCommonCheckGetOptionButtonInput(sMNVSOffClassicChangeWait, is_button, R_JPAD | R_TRIG | R_CBUTTONS) ||
                    mnCommonCheckGetOptionStickInputLR(sMNVSOffClassicChangeWait, stick_range, 20, 1)
                )
                {
                    if (sMNVSOffClassicRule < nMNVSModeRuleStockTeam)
                    {
                        func_800269C0_275C0(nSYAudioFGMMenuScroll1);

                        sMNVSOffClassicRule++;

                        if (sMNVSOffClassicRule == nMNVSModeRuleTimeTeam)
                        {
                            mnVSOffClassicSetCostumesAndShades();
                        }

                        gcEjectGObj(sMNVSOffClassicButtonGObjTimeStock);
                        mnVSOffClassicMakeTimeStockButton();
                        gcEjectGObj(sMNVSOffClassicRuleValueGObj);
                        mnVSOffClassicMakeRuleValue();
                        gcEjectGObj(sMNVSOffClassicTimeStockValueGObj);
                        mnVSOffClassicMakeTimeStockValue();

                        mnCommonSetOptionChangeWaitP(sMNVSOffClassicChangeWait, is_button, stick_range, 7);

                        gcEjectGObj(sMNVSOffClassicUnusedGObj);
                        mnVSOffClassicMakeSubtitle();

                        sMNVSOffClassicInputDirection = nMNVSModeInputDirectionRight;

                        return;
                    }
                }
                break;

            case nMNVSModeOptionTimeStock:
                if (
                    mnCommonCheckGetOptionButtonInput(sMNVSOffClassicChangeWait, is_button, L_JPAD | L_TRIG | L_CBUTTONS) ||
                    mnCommonCheckGetOptionStickInputLR(sMNVSOffClassicChangeWait, stick_range, -20, 0)
                )
                {
                    func_800269C0_275C0(nSYAudioFGMMenuScroll1);

                    mnCommonSetOptionChangeWaitN(sMNVSOffClassicChangeWait, is_button, stick_range, 14);

                    if (sMNVSOffClassicInputDirection != nMNVSModeInputDirectionLeft)
                    {
                        sMNVSOffClassicChangeWait *= 2;
                    }

                    if (mnVSOffClassicIsTime() != FALSE)
                    {
                        if (sMNVSOffClassicTime == 1)
                        {
                            sMNVSOffClassicTime = SCBATTLE_TIMELIMIT_INFINITE;
                        }
                        else sMNVSOffClassicTime--;

                        if (sMNVSOffClassicTime == 1)
                        {
                            sMNVSOffClassicChangeWait += 8;
                        }
                    }
                    else
                    {
                        if (sMNVSOffClassicStock == 0)
                        {
                            sMNVSOffClassicStock = 98;
                        }
                        else sMNVSOffClassicStock--;

                        if (sMNVSOffClassicStock == 0)
                        {
                            sMNVSOffClassicChangeWait += 8;
                        }
                    }
                    gcEjectGObj(sMNVSOffClassicTimeStockValueGObj);
                    mnVSOffClassicMakeTimeStockValue();

                    sMNVSOffClassicInputDirection = nMNVSModeInputDirectionLeft;
                }
                if
                (
                    mnCommonCheckGetOptionButtonInput(sMNVSOffClassicChangeWait, is_button, R_JPAD | R_TRIG | R_CBUTTONS) ||
                    mnCommonCheckGetOptionStickInputLR(sMNVSOffClassicChangeWait, stick_range, 20, 1)
                )
                {
                    func_800269C0_275C0(nSYAudioFGMMenuScroll1);

                    mnCommonSetOptionChangeWaitP(sMNVSOffClassicChangeWait, is_button, stick_range, 14);

                    if (sMNVSOffClassicInputDirection != nMNVSModeInputDirectionRight)
                    {
                        sMNVSOffClassicChangeWait *= 2;
                    }

                    if (mnVSOffClassicIsTime() != FALSE)
                    {
                        if (sMNVSOffClassicTime == 100)
                        {
                            sMNVSOffClassicTime = 1;
                        }
                        else sMNVSOffClassicTime += 1;

                        if (sMNVSOffClassicTime == SCBATTLE_TIMELIMIT_INFINITE)
                        {
                            sMNVSOffClassicChangeWait += 8;
                        }
                    }
                    else
                    {
                        if (sMNVSOffClassicStock == 98)
                        {
                            sMNVSOffClassicStock = 0;
                        }
                        else sMNVSOffClassicStock++;

                        if (sMNVSOffClassicStock == 98)
                        {
                            sMNVSOffClassicChangeWait += 8;
                        }
                    }
                    gcEjectGObj(sMNVSOffClassicTimeStockValueGObj);
                    mnVSOffClassicMakeTimeStockValue();

                    sMNVSOffClassicInputDirection = nMNVSModeInputDirectionRight;
                }
                break;
        }
    }
}

// 0x801345C4
void mnVSOffClassicFuncStart(void)
{
    LBRelocSetup rl_setup;

    rl_setup.table_addr = (uintptr_t)&lLBRelocTableAddr;
    rl_setup.table_files_num = (u32)llRelocFileCount;
    rl_setup.file_heap = NULL;
    rl_setup.file_heap_size = 0;
    rl_setup.status_buffer = sMNVSOffClassicStatusBuffer;
    rl_setup.status_buffer_size = ARRAY_COUNT(sMNVSOffClassicStatusBuffer);
    rl_setup.force_status_buffer = NULL;
    rl_setup.force_status_buffer_size = 0;

    lbRelocInitSetup(&rl_setup);

    if (!(gSCManagerBackupData.error_flags & LBBACKUP_ERROR_RANDOMKNOCKBACK) && (gSCManagerBackupData.boot > 21) && (gSYMainImemOK == FALSE))
    {
        gSCManagerBackupData.error_flags |= LBBACKUP_ERROR_RANDOMKNOCKBACK;
    }
    lbRelocLoadFilesListed(dMNVSOffClassicFileIDs, sMNVSOffClassicFiles);
    gcMakeGObjSPAfter(0, mnVSOffClassicMain, 0, GOBJ_PRIORITY_DEFAULT);
    gcMakeDefaultCameraGObj(0, GOBJ_PRIORITY_DEFAULT, 100, 0, GPACK_RGBA8888(0x00, 0x00, 0x00, 0x00));

    mnVSOffClassicFuncStartVars();
    mnVSOffClassicMakeBackgroundViewport();
    mnVSOffClassicMakeMenuNameViewport();
    mnVSOffClassicMakeButtonViewport();
    mnVSOffClassicMakeButtonValuegSYRdpViewport();
    mnVSOffClassicMakeBackground();
    mnVSOffClassicMakeMenuName();
    mnVSOffClassicMakeVSStartButton();
    mnVSOffClassicMakeRuleButton();
    mnVSOffClassicMakeRuleValue();
    mnVSOffClassicMakeTimeStockButton();
    mnVSOffClassicMakeTimeStockValue();
    mnVSOffClassicMakeVSOptionsButton();
    mnVSOffClassicMakeSubtitle();

    if (gSCManagerSceneData.scene_prev == nSCKindPlayersVS)
    {
        syAudioPlayBGM(0, nSYAudioBGMModeSelect);
    }
}

// 0x80134880
SYVideoSetup dMNVSOffClassicVideoSetup = SYVIDEO_SETUP_DEFAULT();

// 0x8013489C
SYTaskmanSetup dMNVSOffClassicTaskmanSetup =
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
        mnVSOffClassicFuncLights,    	        // Pre-render function
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
    
    mnVSOffClassicFuncStart          	        // Task start function
};

// 0x80134758
void mnVSModeOfflineClassicStartScene(void)
{
    dMNVSOffClassicVideoSetup.zbuffer = SYVIDEO_ZBUFFER_START(320, 240, 0, 10, u16);
    syVideoInit(&dMNVSOffClassicVideoSetup);

    dMNVSOffClassicTaskmanSetup.scene_setup.arena_size = (size_t) ((uintptr_t)&ovl1_VRAM - (uintptr_t)&ovl19_BSS_END);
    syTaskmanStartTask(&dMNVSOffClassicTaskmanSetup);
}
