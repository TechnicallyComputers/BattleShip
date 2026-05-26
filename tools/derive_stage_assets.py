#!/usr/bin/env python3
"""
derive_stage_assets.py — Extract CSS stage assets from baserom.us.z64.

For each stage in the STAGES manifest this script:
  1. Extracts the reloc file from baserom.us.z64 (using the VPK0/reloc logic
     shared with debug_tools/reloc_extract/reloc_extract.py).
  2. Parses the Sprite struct at the specified byte offset (big-endian ROM layout).
  3. Resolves each Bitmap's texel-buffer pointer using the SSB64 RELOC linked-list
     encoding (see "RELOC internal-pointer resolver" section below).
  4. Un-swizzles the N64 TMEM odd-row XOR4 pattern for 16bpp textures.
  5. Stitches all bitmap strips into one full RGBA image.
  6. Saves <output_dir>/<name>_background.png  (full resolution, e.g. 300x220).
  7. Saves <output_dir>/<name>_small.png       (48x36 LANCZOS downscale).
  8. If the stage entry has a "name_text" key: renders a black-on-transparent
     nameplate PNG at 96x10 (matching the ROM IA4 nameplate sprite dimensions)
     and saves it as <output_dir>/<name>_name.png.

Usage:
  python3 tools/derive_stage_assets.py <baserom.z64> <output_dir>

The output PNGs are loaded at runtime by port/css_icons/port_css_stage_assets.cpp
via stb_image. They are NOT committed to the repo — they are derived from the
user's own baserom at build time by the CMake pipeline.

Adding a new stage:
  Append an entry to the STAGES list below and re-run (CMake will re-run
  automatically because derive_stage_assets.py is listed as a DEPENDS).
"""

import struct
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

# ---------------------------------------------------------------------------
# Import the VPK0 decoder and RELOC extractor from the existing debug tool.
# We add the debug_tools/reloc_extract directory to the path and import the
# relevant functions directly so we don't duplicate the logic.
# ---------------------------------------------------------------------------
_SCRIPT_DIR = Path(__file__).resolve().parent
_REPO_ROOT   = _SCRIPT_DIR.parent
sys.path.insert(0, str(_REPO_ROOT / "debug_tools" / "reloc_extract"))
from reloc_extract import extract_file  # type: ignore  # noqa: E402

# ---------------------------------------------------------------------------
# Stage manifest.
#
# To add a new port-introduced stage:
#   1. Append a dict with these keys:
#        name        — filename stem (e.g. "brinstar")
#        gkind       — GRKind enum value (for runtime lookup table)
#        reloc_file  — file index inside the ROM's RELOC table
#        sprite_off  — byte offset of the Sprite struct inside that file
#        name_text   — (optional) string to render as a synthetic nameplate PNG.
#                      Only needed when no ROM-shipped nameplate exists for this
#                      stage (e.g. nGRKindLast/Final Destination). Stages that
#                      ship a ROM nameplate should omit this key.
#   2. Re-run this script (or trigger a CMake rebuild — it lists this file
#      and baserom.us.z64 as DEPENDS so it re-runs automatically).
#   3. The new <name>_background.png and <name>_small.png appear in the
#      output directory and are picked up by portCSSGetStageBackgroundSprite()
#      and portCSSGetStageIconSprite() at runtime via the same gkind lookup.
#   4. If name_text is set, <name>_name.png also appears and is picked up
#      by portCSSGetStageNameSprite() at runtime.
# ---------------------------------------------------------------------------
STAGES = [
    {
        "name":       "final_destination",   # filename stem
        "gkind":      16,                    # nGRKindLast
        "reloc_file": 96,                    # file 96 = StageLastFile1
        "sprite_off": 0x26c88,              # dStageLastBackground_0x26c88
        "name_text":  "FINAL DESTINATION",  # synthetic nameplate (no ROM sprite)
    },
]

# ---------------------------------------------------------------------------
# Nameplate PNG dimensions — derived from ROM IA4 nameplate sprites in
# reloc file 30 (MNMaps), e.g. PeachsCastleText at offset 0x1f8:
#   width=96, height=10, bmfmt=4 (G_IM_FMT_IA), bmsiz=0 (G_IM_SIZ_4b=IA4)
# The SObj caller forces red=green=blue=0x00 (black), so only the alpha channel
# matters at runtime.  The PNG is stored as RGBA with black opaque pixels on a
# transparent background.
# ---------------------------------------------------------------------------
NAME_W = 96
NAME_H = 10

# CSS thumbnail dimensions — same for all stage icons (matches N64 CSS format).
ICON_W = 48
ICON_H = 36

# ---------------------------------------------------------------------------
# Sprite struct / Bitmap struct sizes (bytes).
# ---------------------------------------------------------------------------
SPRITE_SIZE = 68
BITMAP_SIZE = 16

# ---------------------------------------------------------------------------
# Big-endian field readers.
# ---------------------------------------------------------------------------


def _be_s16(data: bytes, off: int) -> int:
    return struct.unpack_from(">h", data, off)[0]


def _be_u32(data: bytes, off: int) -> int:
    return struct.unpack_from(">I", data, off)[0]


def _be_u8(data: bytes, off: int) -> int:
    return data[off]


# ---------------------------------------------------------------------------
# Sprite struct parser (big-endian ROM layout, total 68 bytes).
#
# Field offsets (from port_css_fd_icon.cpp header comment):
#   0x00  s16 x, y
#   0x04  s16 width, height
#   0x08  f32 scalex
#   0x0C  f32 scaley
#   0x10  s16 expx, expy
#   0x14  u16 attr, s16 zdepth
#   0x18  u8  red, green, blue, alpha
#   0x1C  s16 startTLUT, nTLUT
#   0x20  u32 LUT  (reloc token)
#   0x24  s16 istart, istep
#   0x28  s16 nbitmaps, ndisplist
#   0x2C  s16 bmheight, bmHreal
#   0x30  u8  bmfmt, u8 bmsiz, pad, pad
#   0x34  u32 bitmap pointer (reloc token)
#   0x38  u32 rsp_dl
#   0x3C  u32 rsp_dl_next
#   0x40  s16 frac_s, frac_t
#
# Bitmap struct (16 bytes, big-endian ROM layout):
#   0x00  s16 width, width_img
#   0x04  s16 s, t
#   0x08  u32 buf  (reloc token)
#   0x0C  s16 actualHeight, LUToffset
# ---------------------------------------------------------------------------


def parse_sprite(file_data: bytes, sprite_off: int) -> dict:
    """
    Parse the Sprite struct at sprite_off inside file_data (big-endian).
    Returns a dict with the fields we care about.
    """
    d = file_data
    o = sprite_off
    sp = {
        "x":         _be_s16(d, o + 0x00),
        "y":         _be_s16(d, o + 0x02),
        "width":     _be_s16(d, o + 0x04),
        "height":    _be_s16(d, o + 0x06),
        "nbitmaps":  _be_s16(d, o + 0x28),
        "ndisplist": _be_s16(d, o + 0x2A),
        "bmheight":  _be_s16(d, o + 0x2C),
        "bmHreal":   _be_s16(d, o + 0x2E),
        "bmfmt":     _be_u8(d, o + 0x30),
        "bmsiz":     _be_u8(d, o + 0x31),
        "sprite_off": sprite_off,
    }
    return sp


def parse_bitmap(file_data: bytes, bm_off: int) -> dict:
    """Parse one Bitmap struct at bm_off (big-endian)."""
    d = file_data
    o = bm_off
    bm = {
        "width":        _be_s16(d, o + 0x00),
        "width_img":    _be_s16(d, o + 0x02),
        "s":            _be_s16(d, o + 0x04),
        "t":            _be_s16(d, o + 0x06),
        "buf_raw":      _be_u32(d, o + 0x08),
        "actualHeight": _be_s16(d, o + 0x0C),
        "LUToffset":    _be_s16(d, o + 0x0E),
        "off": o,
    }
    return bm


# ---------------------------------------------------------------------------
# RELOC internal-pointer resolver.
#
# After VPK0 decompression the file bytes contain pre-relocation data.
# The N64 OS would patch each internal-reloc slot by adding the KSEG0 load
# address; we see the raw pre-patch values from reloc_extract.py.
#
# Encoding of each u32 internal-reloc slot (big-endian):
#   high 16 bits — word index of next reloc entry in the linked list
#                  (0xFFFF = end of list)
#   low  16 bits — (file_relative_byte_offset / 4) for the pointed-to data
#
# To recover the file-relative byte offset of the pointed-to data:
#   file_off = (raw_u32 & 0xFFFF) * 4
#
# Empirically verified against file 96 (FD background, 44-bitmap RGBA16 sprite):
#   - Sprite.bitmap ptr at sprite+0x34: raw=0xFFFF9A72 -> 0x9A72*4=0x269C8
#     (which is the Bitmap array start in that file)
#   - Bitmap[0].buf at bm+0x08: raw=0x9A780002 -> 0x0002*4=0x8
#     (start of first texel strip, 8 bytes after the file start for alignment)
# ---------------------------------------------------------------------------


def resolve_reloc_ptr(file_data: bytes, field_off: int) -> int:
    """
    Read the 4-byte big-endian reloc pointer at field_off and return the
    file-relative byte offset it encodes: (raw_u32 & 0xFFFF) * 4.
    """
    raw = _be_u32(file_data, field_off)
    return (raw & 0xFFFF) * 4


def resolve_bitmap_offsets(file_data: bytes, sp: dict) -> list:
    """
    Locate the Bitmap array (via the sprite's bitmap ptr reloc field) and
    resolve each bitmap's buf field to a file-relative byte offset.
    Returns a list of dicts (one per bitmap) with an added 'buf_off' key.
    """
    sprite_off = sp["sprite_off"]
    nbitmaps   = sp["nbitmaps"]

    # Resolve Bitmap array location from sprite+0x34 (bitmap ptr reloc field).
    bm_array_start = resolve_reloc_ptr(file_data, sprite_off + 0x34)

    bitmaps = []
    for i in range(nbitmaps):
        bm_off = bm_array_start + i * BITMAP_SIZE
        bm = parse_bitmap(file_data, bm_off)
        # Resolve each bitmap's texel buf pointer.
        bm["buf_off"] = resolve_reloc_ptr(file_data, bm_off + 0x08)
        bitmaps.append(bm)

    return bitmaps


# ---------------------------------------------------------------------------
# TMEM odd-row un-swizzle for 16bpp (RGBA16 / IA16) textures.
#
# The N64 RDP pre-swizzles RGBA16/IA16 texture data stored in DRAM so that
# LOAD_BLOCK (dxt=0) can stream it into TMEM without stalls. For 16bpp, odd
# rows (strip-local row index within each bitmap) have the two 4-byte halves
# of each 8-byte qword swapped (XOR4 addressing). Fast3D reads texels linearly,
# so we must un-swap before writing pixels.
#
# Reference: docs/bugs/sprite_texel_tmem_swizzle_2026-04-10.md
# ---------------------------------------------------------------------------


def unswizzle_rgba16_strip(raw: bytes, width: int, height: int) -> bytes:
    """
    Un-swizzle one bitmap strip of RGBA16 data (width x height pixels,
    2 bytes per pixel, strip-local row indexing).

    For each odd row, within every 8-byte qword swap the two 4-byte halves.
    Returns the corrected bytes.
    """
    row_bytes = width * 2
    out = bytearray(raw)
    for row in range(height):
        if row % 2 == 0:
            continue  # even rows are fine
        row_start = row * row_bytes
        qword = 0
        while qword * 8 < row_bytes:
            q_off = row_start + qword * 8
            # Swap the two 4-byte halves within this 8-byte qword.
            a = out[q_off:q_off + 4]
            b = out[q_off + 4:q_off + 8]
            out[q_off:q_off + 4] = b
            out[q_off + 4:q_off + 8] = a
            qword += 1
    return bytes(out)


# ---------------------------------------------------------------------------
# RGBA16 -> RGBA8888 pixel decoder.
#
# N64 RGBA16 layout: R5 G5 B5 A1 (big-endian u16, MSB = R bit 4).
#   bits 15..11 = R5, bits 10..6 = G5, bits 5..1 = B5, bit 0 = A1
# ---------------------------------------------------------------------------


def rgba16_to_rgba8888(raw: bytes) -> bytes:
    """
    Convert a big-endian RGBA16 buffer to 8-bit RGBA.
    Returns bytes of length (len(raw) / 2) * 4.
    """
    npixels = len(raw) // 2
    out = bytearray(npixels * 4)
    for i in range(npixels):
        word = (raw[i * 2] << 8) | raw[i * 2 + 1]
        r5 = (word >> 11) & 0x1F
        g5 = (word >>  6) & 0x1F
        b5 = (word >>  1) & 0x1F
        a1 = word & 0x01
        out[i * 4 + 0] = (r5 << 3) | (r5 >> 2)  # expand 5 -> 8 bits
        out[i * 4 + 1] = (g5 << 3) | (g5 >> 2)
        out[i * 4 + 2] = (b5 << 3) | (b5 >> 2)
        out[i * 4 + 3] = 255 if a1 else 0
    return bytes(out)


# ---------------------------------------------------------------------------
# Nameplate PNG renderer.
#
# Renders <text> as a black-on-transparent 96x10 PNG.  Uses PIL's built-in
# bitmap font (ImageFont.load_default()), which produces a pixel-art style
# glyph at a natural size of ~6 pixels tall (fitting comfortably in 10 rows).
#
# For "FINAL DESTINATION" the natural text width is 97px; centering clips the
# rightmost pixel column, which is transparent padding.  The result visually
# matches the ROM IA4 nameplates (same canvas dimensions, same 1-bit alpha
# style — the SObj caller forces all color channels to black at draw time).
# ---------------------------------------------------------------------------


def render_name_png(text: str, output_path: Path) -> None:
    """
    Render text as a black-on-transparent nameplate PNG at NAME_W x NAME_H.

    Font: PIL's built-in bitmap font (no external TTF dependency).  The
    natural size (8px tall glyphs) fits cleanly in a 10-row canvas with one
    row of padding at top and bottom.  Anti-aliasing is off by construction
    (bitmap font), so the output is pure 1-bit alpha — pixel-art style
    matching the ROM's IA4 nameplates.
    """
    font = ImageFont.load_default()

    # Measure on a scratch image (avoid drawing to destination before centering).
    probe = Image.new("RGBA", (NAME_W * 4, NAME_H * 2), (0, 0, 0, 0))
    probe_draw = ImageDraw.Draw(probe)
    bbox = probe_draw.textbbox((0, 0), text, font=font)
    text_w = bbox[2] - bbox[0]
    text_h = bbox[3] - bbox[1]

    # Center horizontally; place baseline so text is vertically centered.
    x = (NAME_W - text_w) // 2 - bbox[0]
    y = (NAME_H - text_h) // 2 - bbox[1]

    # Draw on the real canvas.
    img = Image.new("RGBA", (NAME_W, NAME_H), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    draw.text((x, y), text, font=font, fill=(0, 0, 0, 255))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    img.save(output_path)

    filesize = output_path.stat().st_size
    print(
        f"[{text}] Saved nameplate: {output_path} ({NAME_W}x{NAME_H}, {filesize} bytes)",
        file=sys.stderr,
    )


# ---------------------------------------------------------------------------
# Main extraction logic for a single stage.
# ---------------------------------------------------------------------------


def extract_stage(stage: dict, rom: bytes, output_dir: Path) -> None:
    name       = stage["name"]
    reloc_file = stage["reloc_file"]
    sprite_off = stage["sprite_off"]

    print(f"[{name}] Extracting reloc file {reloc_file}...", file=sys.stderr)
    file_data = extract_file(rom, reloc_file)
    print(f"[{name}] Decompressed to {len(file_data)} bytes.", file=sys.stderr)

    # Parse the Sprite struct.
    sp = parse_sprite(file_data, sprite_off)
    w         = sp["width"]
    h         = sp["height"]
    nbitmaps  = sp["nbitmaps"]
    bmheight  = sp["bmheight"]
    bmsiz     = sp["bmsiz"]   # 2 -> G_IM_SIZ_16b (RGBA16)

    print(
        f"[{name}] Sprite: {w}x{h}, {nbitmaps} bitmaps, bmheight={bmheight}, "
        f"bmHreal={sp['bmHreal']}, bmfmt={sp['bmfmt']}, bmsiz={bmsiz}",
        file=sys.stderr,
    )

    if bmsiz != 2:
        print(
            f"[{name}] WARNING: bmsiz={bmsiz} (expected 2 for RGBA16). "
            f"Only RGBA16 is supported; output may be wrong.",
            file=sys.stderr,
        )

    # Resolve bitmap buffer offsets.
    bitmaps = resolve_bitmap_offsets(file_data, sp)

    # Decode each bitmap strip into RGBA rows.
    rows_rgba = []
    for i, bm in enumerate(bitmaps):
        strip_w     = bm["width"]
        strip_h     = bm["actualHeight"]
        buf_off     = bm["buf_off"]
        strip_bytes = strip_w * strip_h * 2  # 2 bytes per RGBA16 pixel

        if buf_off < 0 or buf_off + strip_bytes > len(file_data):
            raise ValueError(
                f"[{name}] bitmap[{i}].buf_off=0x{buf_off:X} out of range "
                f"(file_data length=0x{len(file_data):X}, strip_bytes={strip_bytes})"
            )

        raw_strip = file_data[buf_off:buf_off + strip_bytes]

        # Un-swizzle TMEM odd-row XOR4 pattern (16bpp, strip-local indexing).
        unswizzled = unswizzle_rgba16_strip(raw_strip, strip_w, strip_h)

        # Decode RGBA16 BE -> RGBA8888.
        rgba = rgba16_to_rgba8888(unswizzled)

        # Split into individual rows and accumulate (only bmheight rendered rows).
        row_bytes = strip_w * 4  # 4 bytes per pixel in RGBA8888
        rendered_rows = min(bmheight, strip_h)
        for row in range(rendered_rows):
            rows_rgba.append(rgba[row * row_bytes:(row + 1) * row_bytes])

    # Sanity check.
    if len(rows_rgba) != h:
        print(
            f"[{name}] WARNING: assembled {len(rows_rgba)} rows, expected {h}. "
            f"Output may be cropped or padded.",
            file=sys.stderr,
        )

    # Build a PIL Image from the decoded rows.
    img_data = b"".join(rows_rgba[:h])
    img = Image.frombytes("RGBA", (w, h), img_data)

    # Save full-resolution background PNG.
    bg_path = output_dir / f"{name}_background.png"
    output_dir.mkdir(parents=True, exist_ok=True)
    img.save(bg_path)
    bg_size = bg_path.stat().st_size
    print(
        f"[{name}] Saved background: {bg_path} ({w}x{h}, {bg_size} bytes)",
        file=sys.stderr,
    )

    # Downscale to 48x36 for the CSS icon thumbnail.
    # Use a center-crop if the aspect ratio doesn't match 4:3, then LANCZOS resize.
    target_w, target_h = ICON_W, ICON_H
    src_aspect = w / h
    tgt_aspect = target_w / target_h

    if abs(src_aspect - tgt_aspect) > 0.01:
        # Center-crop to match target aspect ratio.
        if src_aspect > tgt_aspect:
            # Image is wider -> crop width.
            new_w = int(h * tgt_aspect)
            left  = (w - new_w) // 2
            img_crop = img.crop((left, 0, left + new_w, h))
        else:
            # Image is taller -> crop height.
            new_h = int(w / tgt_aspect)
            top   = (h - new_h) // 2
            img_crop = img.crop((0, top, w, top + new_h))
    else:
        img_crop = img

    img_small = img_crop.resize((target_w, target_h), Image.LANCZOS)
    small_path = output_dir / f"{name}_small.png"
    img_small.save(small_path)
    small_size = small_path.stat().st_size
    print(
        f"[{name}] Saved icon:       {small_path} ({target_w}x{target_h}, {small_size} bytes)",
        file=sys.stderr,
    )

    # Render synthetic nameplate PNG if the stage has a name_text key.
    name_text = stage.get("name_text")
    if name_text:
        name_path = output_dir / f"{name}_name.png"
        render_name_png(name_text, name_path)


# ---------------------------------------------------------------------------
# CLI entry point.
# ---------------------------------------------------------------------------


def main(argv: list) -> int:
    if len(argv) != 2:
        print(
            f"Usage: {Path(__file__).name} <baserom.z64> <output_dir>",
            file=sys.stderr,
        )
        return 2

    rom_path = Path(argv[0])
    out_dir  = Path(argv[1])

    if not rom_path.exists():
        print(f"ERROR: ROM not found: {rom_path}", file=sys.stderr)
        return 1

    rom = rom_path.read_bytes()
    if rom[:4] != bytes([0x80, 0x37, 0x12, 0x40]):
        print(
            f"WARNING: ROM header is not .z64 ({rom[:4].hex()}). "
            "Continuing anyway.",
            file=sys.stderr,
        )

    errors = 0
    for stage in STAGES:
        try:
            extract_stage(stage, rom, out_dir)
        except Exception as exc:
            print(f"ERROR [{stage['name']}]: {exc}", file=sys.stderr)
            errors += 1

    if errors:
        print(f"\n{errors} stage(s) failed.", file=sys.stderr)
        return 1

    print(f"\nAll {len(STAGES)} stage(s) extracted successfully.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
