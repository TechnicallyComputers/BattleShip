#ifndef MN_VS_SUBMENU_PNG_H
#define MN_VS_SUBMENU_PNG_H

#include <ssb_types.h>

struct SObj;

/*
 * Load <bundle>/port/net/assets/<asset_filename> (e.g. "offline_vs.png") as an
 * RGBA texture on the VS submenu label row. asset_filename must be basename only.
 *
 * tab_anchor_x / tab_row_y match mnVSModeMakeButton(button, x, y, ...) for that row
 * (decomp: 120/31 VS Start, 97/70 Rule, 74/109 Time–Stock cascade).
 *
 * ROM layout on failure — caller paints black modulation and default label coords.
 */
sb32 mnPortTryApplyVsSubmenuLabelPng(struct SObj *sobj, const char *asset_filename, f32 tab_anchor_x, f32 tab_row_y);

/*
 * Load PNG like the label helper, but anchor the sprite's **top-left** at (anchor_x, anchor_y).
 * Sizes are uniformly scaled down (preserving aspect) so drawn width ≤ max_disp_w / height ≤ max_disp_h.
 */
sb32 mnPortTryApplyPngDecorationTl(struct SObj *sobj, const char *asset_filename, f32 anchor_x, f32 anchor_y, f32 max_disp_w,
	f32 max_disp_h);

#endif /* MN_VS_SUBMENU_PNG_H */
