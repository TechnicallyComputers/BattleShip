#include "mn_vs_submenu_png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <PR/gbi.h>
#include <lb/lbcommon.h>
#ifndef LBCOMMON_H_PORT_NET
#error "BattleShip netmenu expects port/net lb/lbcommon.h shadow, not stock decomp header."
#endif
#include <sys/objtypes.h>

#include <resource/RelocPointerTable.h>
#include <ssb64_paths_capi.h>

#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

/*
 * Replacing bmfmt/bitmap on an SObj that was cloned from ROM leaves istep,
 * frac_s/t, rsp_dl tokens, SP_TEXSHIFT, etc. Those fields skew Fast3D's
 * RGBA32 UploadTexture stride and UV prep for port PNG substitutions.
 * Match lbCommonMakeSObjForGObj defaults for wrap + mask.
 */
static void mnPortResetSObjForHostRgbaPng(SObj *sobj)
{
	memset(&sobj->sprite, 0, sizeof(sobj->sprite));
	sobj->cmt = 2;
	sobj->cms = 2;
	sobj->maskt = 0;
	sobj->masks = 0;
	sobj->lrs = 0;
	sobj->lrt = 0;
}

sb32 mnPortTryApplyVsSubmenuLabelPng(SObj *sobj, const char *asset_filename, f32 tab_anchor_x, f32 tab_row_y)
{
	char base[768];
	char path[896];
	stbi_uc *img;
	Sprite *sp;
	int iw;
	int ih;
	size_t nbytes;
	u8 *pixel_buf;
	Bitmap *bm;
	u32 buf_tok;
	u32 bm_tok;
	f32 tab_mid_px;
	f32 tab_band_h;
	f32 sc;
	f32 cx;
	f32 cy;
	f32 draw_w;
	f32 draw_h;

	if (sobj == NULL || asset_filename == NULL || strstr(asset_filename, "/") != NULL)
	{
		return FALSE;
	}
	if (ssb64_RealAppBundlePathUtf8(base, sizeof(base)) == 0)
	{
		return FALSE;
	}
	if (snprintf(path, sizeof(path), "%s/port/net/assets/%s", base, asset_filename) >= (int)sizeof(path))
	{
		return FALSE;
	}

	img = stbi_load(path, &iw, &ih, NULL, STBI_rgb_alpha);
	if (img == NULL || iw <= 0 || ih <= 0)
	{
		if (img != NULL)
		{
			stbi_image_free(img);
		}
		return FALSE;
	}

	nbytes = (size_t)iw * (size_t)ih * 4u;
	pixel_buf = malloc(nbytes);
	bm = malloc(sizeof(Bitmap));
	if (pixel_buf == NULL || bm == NULL)
	{
		free(bm);
		free(pixel_buf);
		stbi_image_free(img);
		return FALSE;
	}

	memcpy(pixel_buf, img, nbytes);
	stbi_image_free(img);

	memset(bm, 0, sizeof(*bm));

	buf_tok = portRelocRegisterPointer(pixel_buf);
	bm_tok = portRelocRegisterPointer(bm);
	if (buf_tok == 0 || bm_tok == 0)
	{
		free(bm);
		free(pixel_buf);
		return FALSE;
	}

	bm->width = (s16)iw;
	bm->width_img = (s16)iw;
	bm->s = 0;
	bm->t = 0;
	bm->buf = buf_tok;
	bm->actualHeight = (s16)ih;
	bm->LUToffset = 0;

	mnPortResetSObjForHostRgbaPng(sobj);
	sp = &sobj->sprite;

	sp->bmfmt = G_IM_FMT_RGBA;
	sp->bmsiz = G_IM_SIZ_32b;
	sp->LUT = 0;
	sp->startTLUT = 0;
	sp->nTLUT = 0;
	sp->bmheight = (s16)ih;
	sp->bmHreal = (s16)ih;
	sp->width = (s16)iw;
	sp->height = (s16)ih;
	sp->nbitmaps = 1;
	sp->bitmap = bm_tok;

	tab_mid_px = 17.0F * 8.0F;
	tab_band_h = 29.0F;
	sc = tab_mid_px / (f32)iw;
	if (((f32)ih * sc) > tab_band_h)
	{
		sc = tab_band_h / (f32)ih;
	}
	sp->scalex = sc;
	sp->scaley = sc;

	cx = tab_anchor_x + 16.0F + tab_mid_px * 0.5F;
	cy = tab_row_y + tab_band_h * 0.5F;
	draw_w = (f32)iw * sc;
	draw_h = (f32)ih * sc;
	sobj->pos.x = cx - draw_w * 0.5F;
	sobj->pos.y = cy - draw_h * 0.5F;

	sp->attr = SP_TRANSPARENT;
	sp->red = 0xFF;
	sp->green = 0xFF;
	sp->blue = 0xFF;
	sp->alpha = 0xFF;

	lbCommonInvalidatePrevBitmapBuf();

	return TRUE;
}

sb32 mnPortTryApplyPngDecorationTl(SObj *sobj, const char *asset_filename, f32 anchor_x, f32 anchor_y, f32 max_disp_w,
	f32 max_disp_h)
{
	char base[768];
	char path[896];
	stbi_uc *img;
	Sprite *sp;
	int iw;
	int ih;
	size_t nbytes;
	u8 *pixel_buf;
	Bitmap *bm;
	u32 buf_tok;
	u32 bm_tok;
	f32 sc;

	if (sobj == NULL || asset_filename == NULL || strstr(asset_filename, "/") != NULL || max_disp_w <= 0.0F || max_disp_h <= 0.0F)
	{
		return FALSE;
	}
	if (ssb64_RealAppBundlePathUtf8(base, sizeof(base)) == 0)
	{
		return FALSE;
	}
	if (snprintf(path, sizeof(path), "%s/port/net/assets/%s", base, asset_filename) >= (int)sizeof(path))
	{
		return FALSE;
	}

	img = stbi_load(path, &iw, &ih, NULL, STBI_rgb_alpha);
	if (img == NULL || iw <= 0 || ih <= 0)
	{
		if (img != NULL)
		{
			stbi_image_free(img);
		}
		return FALSE;
	}

	nbytes = (size_t)iw * (size_t)ih * 4u;
	pixel_buf = malloc(nbytes);
	bm = malloc(sizeof(Bitmap));
	if (pixel_buf == NULL || bm == NULL)
	{
		free(bm);
		free(pixel_buf);
		stbi_image_free(img);
		return FALSE;
	}

	memcpy(pixel_buf, img, nbytes);
	stbi_image_free(img);

	memset(bm, 0, sizeof(*bm));

	buf_tok = portRelocRegisterPointer(pixel_buf);
	bm_tok = portRelocRegisterPointer(bm);
	if (buf_tok == 0 || bm_tok == 0)
	{
		free(bm);
		free(pixel_buf);
		return FALSE;
	}

	bm->width = (s16)iw;
	bm->width_img = (s16)iw;
	bm->s = 0;
	bm->t = 0;
	bm->buf = buf_tok;
	bm->actualHeight = (s16)ih;
	bm->LUToffset = 0;

	mnPortResetSObjForHostRgbaPng(sobj);
	sp = &sobj->sprite;

	sp->bmfmt = G_IM_FMT_RGBA;
	sp->bmsiz = G_IM_SIZ_32b;
	sp->LUT = 0;
	sp->startTLUT = 0;
	sp->nTLUT = 0;
	sp->bmheight = (s16)ih;
	sp->bmHreal = (s16)ih;
	sp->width = (s16)iw;
	sp->height = (s16)ih;
	sp->nbitmaps = 1;
	sp->bitmap = bm_tok;

	sc = max_disp_w / (f32)iw;
	if (((f32)ih * sc) > max_disp_h)
	{
		sc = max_disp_h / (f32)ih;
	}
	sp->scalex = sc;
	sp->scaley = sc;

	sobj->pos.x = anchor_x;
	sobj->pos.y = anchor_y;

	sp->attr = SP_TRANSPARENT;

	sp->red = 0xFF;
	sp->green = 0xFF;
	sp->blue = 0xFF;
	sp->alpha = 0xFF;

	lbCommonInvalidatePrevBitmapBuf();

	return TRUE;
}
