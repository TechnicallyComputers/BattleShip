# Overscan black border framing the game image

**Date:** 2026-06-16
**Status:** RESOLVED (libultraship `Gui::DrawGame` + port menu)
**Symptom:** A black border frames the game image on **all four sides**, in every
window size and in fullscreen. It is *not* the 4:3 pillarbox: in regular (4:3)
mode the expected left/right pillarbox bars are present **plus** an extra thin
border all the way around; in widescreen/fill mode (where the image should fill
the window) there is still a thin border on every side. The viewport is
consistently a little smaller than the window. Predates the libultraship
integration entirely — the same border existed in the early raw-Fast3D build.
Ship of Harkinian (OoT, same engine family) fills the window with no border.

## Root cause

SSB64 bakes a **10-pixel "title-safe" overscan margin** into its own GBI on
every side. Two scissor sites, both insetting by 10 (in 320×240 space, scaled
proportionally to the active internal resolution):

- `decomp/src/sys/rdp.c` — `syRdpResetSettings` sets the default scissor to
  `(10, 10) … (310, 230)`:
  ```c
  gDPSetScissor(dl++, G_SC_NON_INTERLACE,
      10 * (gSYVideoResWidth  / GS_SCREEN_WIDTH_DEFAULT),
      10 * (gSYVideoResHeight / GS_SCREEN_HEIGHT_DEFAULT),
      gSYVideoResWidth  - 10 * (gSYVideoResWidth  / GS_SCREEN_WIDTH_DEFAULT),
      gSYVideoResHeight - 10 * (gSYVideoResHeight / GS_SCREEN_HEIGHT_DEFAULT));
  ```
- `decomp/src/sys/objdisplay.c` — the per-camera scissor clamps viewport-derived
  bounds to the same margin via `dGCCameraScissor{Left,Top,Right,Bottom} = 10`
  (declared at `objdisplay.c:383-392`). The **per-frame framebuffer clear/fill**
  (`gDPFillRectangle` at `objdisplay.c:3306/3316/3371/3382`) uses those same
  clamped bounds.

So all drawing — 3D world (viewport is full 0..320 but clipped by the scissor),
2D HUD/backgrounds, and even the background clear — is confined to the
(10,10)–(310,230) safe area. The 10px margin is **never drawn into and never
cleared**.

On original hardware this was invisible: a CRT TV's overscan crops roughly the
outer ~5–8% of the picture, so the black margin (and a little of the content
edge) fell behind the TV bezel and the safe area filled the visible screen. A PC
port renders the full 320×240 framebuffer 1:1 into a desktop window, so the
never-drawn margin shows up as a black frame on all four sides.

This is why OoT/SoH doesn't have it: OoT's scissor is the full framebuffer, so
its content fills its own framebuffer. SSB64 is the unusual one — it deliberately
insets.

## Why "just widen the scissor" does **not** work

The obvious fix — widen the in-game scissors to the full framebuffer so the game
fills it like OoT — fails here. Because the framebuffer clear and the 2D draws
are *also* clamped to the safe area, the margin is uncleared memory. Widening the
scissor would reveal **uncleared/stale framebuffer garbage** in the margin, not
content (3D would extend, but the 2D background and clear would not). It would
also require touching multiple decomp scissor sites and the clear path.

## Fix

A **present-level overscan crop**: when blitting the rendered framebuffer texture
to the window, sample it minus its proportional margin and let the safe area
stretch to fill the viewport — exactly what a CRT's overscan did. One place, no
decomp changes, works in every aspect/resolution mode.

`libultraship/src/ship/window/gui/Gui.cpp`, `Gui::DrawGame` — the single
`ImGui::Image(fb, size)` call now passes cropped UVs:

```cpp
const float fx = overscanHorizontal / 320.0f;   // 10/320 by default
const float fy = overscanVertical   / 240.0f;   // 10/240 by default
ImGui::Image(fb, size, ImVec2(fx, fy), ImVec2(1.0f - fx, 1.0f - fy));
```

Properties:
- **Resolution-independent.** The margin is a fixed fraction of the frame
  regardless of internal render resolution (Fast3D scales the scissor by
  `mCurDimensions/native`), so the UV fractions are constant.
- **Orientation-agnostic.** The crop is symmetric (equal top/bottom, equal
  left/right), so it is immune to any framebuffer Y-flip differences between
  backends.
- **Skipped in pixel-perfect mode** (`gAdvancedResolution.PixelPerfectMode`),
  where the user has opted into an exact 1:1 framebuffer.

### CVars (read every frame at present time — no reload needed)

| CVar | Default | Meaning |
|------|---------|---------|
| `gAdvancedResolution.Overscan.Enabled` | `1` | Master toggle for the crop |
| `gAdvancedResolution.Overscan.Horizontal` | `10.0` | N64 px cropped from each of left/right (of 320) |
| `gAdvancedResolution.Overscan.Vertical` | `10.0` | N64 px cropped from each of top/bottom (of 240) |

Exposed in the menu under **Settings → Enhancements**: "Crop Overscan Border"
(checkbox) plus horizontal/vertical sliders. Defaults reproduce the on-TV image;
disabling shows the raw N64 framebuffer including its overscan border.

## Interaction with the existing aspect handling

- **Fill / widescreen mode** (`gAdvancedResolution.Enabled=0`): image already
  fills the window; the crop removes the thin overscan frame → full fill.
- **Regular / 4:3 pillarbox mode** (`gAdvancedResolution.Enabled=1`): the crop
  removes the extra all-around border *within* the 4:3 sub-rect; the intended
  left/right pillarbox bars (true 4:3-vs-window difference) remain.
- **N64 / 240p / 480p low-res modes:** crop applies to the displayed region.
- **Pixel-perfect integer scale:** crop intentionally skipped.

## Audit hook

The black frame is a *content* property of SSB64's GBI (the 10px safe-area
scissor), not a window/aspect bug. Any future "image is inset from the window"
report should first distinguish: (a) the 4:3 pillarbox (correct, aspect-driven,
left/right only), (b) this overscan frame (all four sides, ~3% x / ~4% y, fixed
by the present-level crop), or (c) an actual ImGui dockspace/viewport sizing
issue (would also affect the menu bar / floating windows).
