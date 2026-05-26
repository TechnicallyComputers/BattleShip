/**
 * port_css_stage_assets.cpp — Generic CSS stage asset loader (port-only).
 *
 * Loads full-resolution CSS wallpaper PNGs and 48x36 icon PNGs that were
 * extracted from baserom.us.z64 at CMake build time by
 * tools/derive_stage_assets.py, converts them to RGBA16 BE (the format the
 * Fast3D renderer expects for N64 sprite textures), and wraps each in a
 * synthetic Sprite + Bitmap array.
 *
 * The PNGs live at runtime under:
 *   <app-data>/assets/css_icons/<name>_background.png
 *   <app-data>/assets/css_icons/<name>_small.png
 *   <app-data>/assets/css_icons/<name>_name.png   (optional — see below)
 *
 * If a file is absent the getter returns NULL and the caller falls back to
 * the ROM-resident sprite (mnmaps.c handles this).
 *
 * Sprite construction mirrors the old port_css_fd_background.cpp /
 * port_css_fd_icon.cpp pattern:
 *   - Background: 300x220, 44 bitmaps x 5 rows each (bmheight=5, bmHreal=6),
 *                 ndisplist=552, RGBA16.
 *   - Icon: 48x36, 1 bitmap, ndisplist=36, RGBA16.
 *   - Name: 96x10, 1 bitmap, ndisplist=10, RGBA16.
 *     Dimensions match ROM IA4 nameplate sprites (MNMaps/file 30).
 *     Only port-introduced stages with a synthesized nameplate PNG return
 *     non-NULL; ROM-resident stages return NULL so caller uses ROM sprite.
 * These constants are derived from dStageLastBackground_0x26c88 (ROM-verified
 * in the old port_css_fd_background.h header comment).
 *
 * Adding a new stage: add one entry to STAGE_TABLE below.
 */

#ifdef PORT

#include "port_css_stage_assets.h"

#include "../bridge/lbreloc_byteswap.h"
#include "../resource/RelocPointerTable.h"
#include "../port_log.h"

// stb_image's implementation lives in libultraship's stb_impl.c.
// Only include the header to pick up declarations.
#include <stb_image.h>

#include <libultraship/libultraship.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

/* PR/sp.h uses _Static_assert (C11) and cannot be included from C++.
 * Opaque type tags. */
struct sprite;
struct bitmap;
typedef struct sprite Sprite;
typedef struct bitmap Bitmap;

namespace {

// ---------------------------------------------------------------------------
// Stage table.
//
// To add a new port-introduced stage:
//   1. Append a StageEntry here.
//   2. Add a matching entry to STAGES in tools/derive_stage_assets.py.
//   3. Add the CMake OUTPUT + DEPENDS + POST_BUILD copy for the new PNGs.
// ---------------------------------------------------------------------------

struct StageEntry {
    int         gkind;       // GRKind enum value
    const char *name;        // filename stem (e.g. "final_destination")

    // Background sprite geometry (ROM-verified from dStageLastBackground_0x26c88):
    int         bg_w;        // full image width  (pixels)
    int         bg_h;        // full image height (pixels)
    int         bg_nbitmaps; // number of TMEM-slice bitmaps
    int         bg_bm_h;    // rendered rows per bitmap (bmheight)
    int         bg_bm_hreal;// N64 bmHreal (rows in the TMEM load including fringe)
    int         bg_ndisplist;// ndisplist from ROM sprite

    // Nameplate sprite: set to true when a synthesized <name>_name.png exists.
    // False for stages that ship a ROM-resident IA4 nameplate (the 9 starters).
    bool        has_name_png; // if false, portCSSGetStageNameSprite returns NULL
};

static constexpr StageEntry STAGE_TABLE[] = {
    {
        /* nGRKindLast = 16 */
        16, "final_destination",
        300, 220,
        44,  /* nbitmaps: 44 * 5 rendered rows = 220 total */
        5,   /* bmheight: rows rendered per bitmap */
        6,   /* bmHreal:  rows in the TMEM load     */
        552, /* ndisplist from dStageLastBackground_0x26c88 */
        true,/* has_name_png: synthesized 96x10 nameplate */
    },
    // { next_gkind, "next_name", bg_w, bg_h, nbitmaps, bm_h, bm_hreal, ndisplist, has_name_png },
};

static constexpr int kStageCount = (int)(sizeof(STAGE_TABLE) / sizeof(STAGE_TABLE[0]));

// Icon dimensions — same for all stages (matches N64 CSS format).
constexpr int      kIconW  = 48;
constexpr int      kIconH  = 36;
constexpr uint8_t  kFmtRGBA16 = 0; /* G_IM_FMT_RGBA */
constexpr uint8_t  kSiz16b    = 2; /* G_IM_SIZ_16b  */

// ---------------------------------------------------------------------------
// Cache entries.
// ---------------------------------------------------------------------------

enum AssetKind { kBackground = 0, kIcon = 1, kName = 2, kAssetKindCount = 3 };

struct CacheEntry {
    Sprite   *sprite     = nullptr;
    Bitmap   *bitmaps    = nullptr;  // pointer to first Bitmap in the array
    uint8_t  *rgba16_buf = nullptr;  // heap-allocated RGBA16 BE pixel data
    int       nbitmaps   = 0;
};

// Flat 2D array: cache[gkind_index][asset_kind]
static CacheEntry  sCache[kStageCount][kAssetKindCount];
static std::mutex  sCacheMutex;

// ---------------------------------------------------------------------------
// Sprite builder helpers.
// ---------------------------------------------------------------------------

static inline void put_s16(uint8_t *b, unsigned off, int16_t v) {
    uint16_t u; std::memcpy(&u, &v, 2);
    b[off+0] = (uint8_t)(u & 0xFF); b[off+1] = (uint8_t)(u >> 8);
}
static inline void put_u16(uint8_t *b, unsigned off, uint16_t v) {
    b[off+0] = (uint8_t)(v & 0xFF); b[off+1] = (uint8_t)(v >> 8);
}
static inline void put_u32(uint8_t *b, unsigned off, uint32_t v) {
    b[off+0] = (uint8_t)(v & 0xFF);
    b[off+1] = (uint8_t)((v >> 8) & 0xFF);
    b[off+2] = (uint8_t)((v >> 16) & 0xFF);
    b[off+3] = (uint8_t)(v >> 24);
}
static inline void put_f32(uint8_t *b, unsigned off, float v) {
    uint32_t bits; std::memcpy(&bits, &v, 4); put_u32(b, off, bits);
}

// ---------------------------------------------------------------------------
// RGBA8888 -> RGBA16 BE converter.
//
// stb_image returns 4-byte RGBA8888. The Fast3D renderer expects RGBA16 BE
// (R5G5B5A1) with no TMEM odd-row swizzle (synthetic sprites bypass the
// portFixupSpriteBitmapData swizzle via portMarkSyntheticSprite).
// ---------------------------------------------------------------------------

static uint8_t *convertToRGBA16BE(const uint8_t *rgba8, int w, int h) {
    const int npixels = w * h;
    uint8_t *out = static_cast<uint8_t *>(std::malloc((size_t)npixels * 2));
    if (!out) return nullptr;

    for (int i = 0; i < npixels; ++i) {
        uint8_t r = rgba8[i * 4 + 0];
        uint8_t g = rgba8[i * 4 + 1];
        uint8_t b = rgba8[i * 4 + 2];
        uint8_t a = rgba8[i * 4 + 3];
        uint16_t r5 = r >> 3;
        uint16_t g5 = g >> 3;
        uint16_t b5 = b >> 3;
        uint16_t a1 = a >= 128 ? 1 : 0;
        uint16_t word = (r5 << 11) | (g5 << 6) | (b5 << 1) | a1;
        // Store big-endian.
        out[i * 2 + 0] = (uint8_t)(word >> 8);
        out[i * 2 + 1] = (uint8_t)(word & 0xFF);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Build a multi-bitmap background Sprite from heap-allocated RGBA16 data.
//
// Layout matches dStageLastBackground_0x26c88: w x h image split into
// nbitmaps strips of bm_h rendered rows each.
// ---------------------------------------------------------------------------

static Sprite *buildBackgroundSprite(const StageEntry &se,
                                     uint8_t *rgba16_buf,
                                     CacheEntry *entry)
{
    const int w           = se.bg_w;
    const int h           = se.bg_h;
    const int nbitmaps    = se.bg_nbitmaps;
    const int bm_h        = se.bg_bm_h;
    const int bm_hreal    = se.bg_bm_hreal;
    const int ndisplist   = se.bg_ndisplist;
    const int bm_bytes    = w * bm_h * 2; // bytes per bitmap strip (rendered rows only)

    uint8_t *sp_raw = static_cast<uint8_t *>(std::malloc(68));
    uint8_t *bm_raw = static_cast<uint8_t *>(std::malloc((size_t)nbitmaps * 16));
    void   **buf_ptrs = static_cast<void **>(std::malloc((size_t)nbitmaps * sizeof(void *)));
    if (!sp_raw || !bm_raw || !buf_ptrs) {
        std::free(sp_raw); std::free(bm_raw); std::free(buf_ptrs);
        return nullptr;
    }
    std::memset(sp_raw, 0, 68);
    std::memset(bm_raw, 0, (size_t)nbitmaps * 16);

    for (int i = 0; i < nbitmaps; ++i) {
        uint8_t *bm       = bm_raw + i * 16;
        uint8_t *strip    = rgba16_buf + (size_t)i * bm_bytes;
        put_s16(bm, 0x00, (int16_t)w);
        put_s16(bm, 0x02, (int16_t)w);
        put_s16(bm, 0x04, 0);
        put_s16(bm, 0x06, 0);
        put_u32(bm, 0x08, portRelocRegisterPointer(strip));
        put_s16(bm, 0x0C, (int16_t)bm_h);
        put_s16(bm, 0x0E, 0);
        buf_ptrs[i] = strip;
    }

    Bitmap *bm0 = reinterpret_cast<Bitmap *>(bm_raw);

    put_s16(sp_raw, 0x00, 0);
    put_s16(sp_raw, 0x02, 0);
    put_s16(sp_raw, 0x04, (int16_t)w);
    put_s16(sp_raw, 0x06, (int16_t)h);
    put_f32(sp_raw, 0x08, 1.0f);
    put_f32(sp_raw, 0x0C, 1.0f);
    put_s16(sp_raw, 0x10, 0); put_s16(sp_raw, 0x12, 0);
    put_u16(sp_raw, 0x14, 0); put_s16(sp_raw, 0x16, 0);
    sp_raw[0x18] = 0xFF; sp_raw[0x19] = 0xFF;
    sp_raw[0x1A] = 0xFF; sp_raw[0x1B] = 0xFF;
    put_s16(sp_raw, 0x1C, 0); put_s16(sp_raw, 0x1E, 0);
    put_u32(sp_raw, 0x20, portRelocRegisterPointer(nullptr));
    put_s16(sp_raw, 0x24, 0); put_s16(sp_raw, 0x26, 1);
    put_s16(sp_raw, 0x28, (int16_t)nbitmaps);
    put_s16(sp_raw, 0x2A, (int16_t)ndisplist);
    put_s16(sp_raw, 0x2C, (int16_t)bm_h);
    put_s16(sp_raw, 0x2E, (int16_t)bm_hreal);
    sp_raw[0x30] = kFmtRGBA16; sp_raw[0x31] = kSiz16b;
    sp_raw[0x32] = 0;          sp_raw[0x33] = 0;
    put_u32(sp_raw, 0x34, portRelocRegisterPointer(bm0));
    put_u32(sp_raw, 0x38, portRelocRegisterPointer(nullptr));
    put_u32(sp_raw, 0x3C, portRelocRegisterPointer(nullptr));
    put_s16(sp_raw, 0x40, 0); put_s16(sp_raw, 0x42, 0);

    Sprite *sp = reinterpret_cast<Sprite *>(sp_raw);
    portMarkSyntheticSprite(sp, bm0, (unsigned int)nbitmaps, buf_ptrs);
    std::free(buf_ptrs);

    entry->sprite     = sp;
    entry->bitmaps    = bm0;
    entry->rgba16_buf = rgba16_buf;
    entry->nbitmaps   = nbitmaps;
    return sp;
}

// ---------------------------------------------------------------------------
// Build a single-bitmap icon Sprite (48x36).
// ---------------------------------------------------------------------------

static Sprite *buildIconSprite(uint8_t *rgba16_buf, CacheEntry *entry)
{
    const int w         = kIconW;
    const int h         = kIconH;
    const int nbitmaps  = 1;
    const int ndisplist = h;

    uint8_t *sp_raw = static_cast<uint8_t *>(std::malloc(68));
    uint8_t *bm_raw = static_cast<uint8_t *>(std::malloc(16));
    if (!sp_raw || !bm_raw) { std::free(sp_raw); std::free(bm_raw); return nullptr; }
    std::memset(sp_raw, 0, 68);
    std::memset(bm_raw, 0, 16);

    put_s16(bm_raw, 0x00, (int16_t)w);
    put_s16(bm_raw, 0x02, (int16_t)w);
    put_s16(bm_raw, 0x04, 0);
    put_s16(bm_raw, 0x06, 0);
    put_u32(bm_raw, 0x08, portRelocRegisterPointer(rgba16_buf));
    put_s16(bm_raw, 0x0C, (int16_t)h);
    put_s16(bm_raw, 0x0E, 0);

    Bitmap *bm = reinterpret_cast<Bitmap *>(bm_raw);

    put_s16(sp_raw, 0x00, 0);
    put_s16(sp_raw, 0x02, 0);
    put_s16(sp_raw, 0x04, (int16_t)w);
    put_s16(sp_raw, 0x06, (int16_t)h);
    put_f32(sp_raw, 0x08, 1.0f);
    put_f32(sp_raw, 0x0C, 1.0f);
    put_s16(sp_raw, 0x10, 0); put_s16(sp_raw, 0x12, 0);
    put_u16(sp_raw, 0x14, 0); put_s16(sp_raw, 0x16, 0);
    sp_raw[0x18] = 0xFF; sp_raw[0x19] = 0xFF;
    sp_raw[0x1A] = 0xFF; sp_raw[0x1B] = 0xFF;
    put_s16(sp_raw, 0x1C, 0); put_s16(sp_raw, 0x1E, 0);
    put_u32(sp_raw, 0x20, portRelocRegisterPointer(nullptr));
    put_s16(sp_raw, 0x24, 0); put_s16(sp_raw, 0x26, 1);
    put_s16(sp_raw, 0x28, (int16_t)nbitmaps);
    put_s16(sp_raw, 0x2A, (int16_t)ndisplist);
    put_s16(sp_raw, 0x2C, (int16_t)h);
    put_s16(sp_raw, 0x2E, (int16_t)h);
    sp_raw[0x30] = kFmtRGBA16; sp_raw[0x31] = kSiz16b;
    sp_raw[0x32] = 0;          sp_raw[0x33] = 0;
    put_u32(sp_raw, 0x34, portRelocRegisterPointer(bm));
    put_u32(sp_raw, 0x38, portRelocRegisterPointer(nullptr));
    put_u32(sp_raw, 0x3C, portRelocRegisterPointer(nullptr));
    put_s16(sp_raw, 0x40, 0); put_s16(sp_raw, 0x42, 0);

    Sprite *sp = reinterpret_cast<Sprite *>(sp_raw);

    void *buf_ptrs[1] = { rgba16_buf };
    portMarkSyntheticSprite(sp, bm, (unsigned int)nbitmaps, buf_ptrs);

    entry->sprite     = sp;
    entry->bitmaps    = bm;
    entry->rgba16_buf = rgba16_buf;
    entry->nbitmaps   = nbitmaps;
    return sp;
}

// ---------------------------------------------------------------------------
// Build a single-bitmap nameplate Sprite (96x10).
//
// Matches the ROM IA4 nameplate sprite dimensions from reloc file 30 / MNMaps:
//   width=96, height=10, 1 bitmap, bmheight=10, bmHreal=10, ndisplist=36.
// Format is stored as RGBA16 (same as background/icon) because convertToRGBA16BE
// is applied at load time and portMarkSyntheticSprite bypasses the TMEM swizzle.
// The caller (mnmaps.c) must force red=green=blue=0x00 on the SObj so the
// sprite renders as black text on transparent — identical to ROM nameplate style.
// ---------------------------------------------------------------------------

static constexpr int kNameW  = 96;
static constexpr int kNameH  = 10;

static Sprite *buildNameSprite(uint8_t *rgba16_buf, CacheEntry *entry)
{
    const int w         = kNameW;
    const int h         = kNameH;
    const int nbitmaps  = 1;
    // ndisplist=36 matches ROM nameplate sprites (PeachsCastleText etc.)
    const int ndisplist = 36;

    uint8_t *sp_raw = static_cast<uint8_t *>(std::malloc(68));
    uint8_t *bm_raw = static_cast<uint8_t *>(std::malloc(16));
    if (!sp_raw || !bm_raw) { std::free(sp_raw); std::free(bm_raw); return nullptr; }
    std::memset(sp_raw, 0, 68);
    std::memset(bm_raw, 0, 16);

    put_s16(bm_raw, 0x00, (int16_t)w);
    put_s16(bm_raw, 0x02, (int16_t)w);
    put_s16(bm_raw, 0x04, 0);
    put_s16(bm_raw, 0x06, 0);
    put_u32(bm_raw, 0x08, portRelocRegisterPointer(rgba16_buf));
    put_s16(bm_raw, 0x0C, (int16_t)h);
    put_s16(bm_raw, 0x0E, 0);

    Bitmap *bm = reinterpret_cast<Bitmap *>(bm_raw);

    put_s16(sp_raw, 0x00, 0);
    put_s16(sp_raw, 0x02, 0);
    put_s16(sp_raw, 0x04, (int16_t)w);
    put_s16(sp_raw, 0x06, (int16_t)h);
    put_f32(sp_raw, 0x08, 1.0f);
    put_f32(sp_raw, 0x0C, 1.0f);
    put_s16(sp_raw, 0x10, 0); put_s16(sp_raw, 0x12, 0);
    put_u16(sp_raw, 0x14, 0); put_s16(sp_raw, 0x16, 0);
    sp_raw[0x18] = 0xFF; sp_raw[0x19] = 0xFF;
    sp_raw[0x1A] = 0xFF; sp_raw[0x1B] = 0xFF;
    put_s16(sp_raw, 0x1C, 0); put_s16(sp_raw, 0x1E, 0);
    put_u32(sp_raw, 0x20, portRelocRegisterPointer(nullptr));
    put_s16(sp_raw, 0x24, 0); put_s16(sp_raw, 0x26, 1);
    put_s16(sp_raw, 0x28, (int16_t)nbitmaps);
    put_s16(sp_raw, 0x2A, (int16_t)ndisplist);
    put_s16(sp_raw, 0x2C, (int16_t)h);
    put_s16(sp_raw, 0x2E, (int16_t)h);
    sp_raw[0x30] = kFmtRGBA16; sp_raw[0x31] = kSiz16b;
    sp_raw[0x32] = 0;          sp_raw[0x33] = 0;
    put_u32(sp_raw, 0x34, portRelocRegisterPointer(bm));
    put_u32(sp_raw, 0x38, portRelocRegisterPointer(nullptr));
    put_u32(sp_raw, 0x3C, portRelocRegisterPointer(nullptr));
    put_s16(sp_raw, 0x40, 0); put_s16(sp_raw, 0x42, 0);

    Sprite *sp = reinterpret_cast<Sprite *>(sp_raw);

    void *buf_ptrs[1] = { rgba16_buf };
    portMarkSyntheticSprite(sp, bm, (unsigned int)nbitmaps, buf_ptrs);

    entry->sprite     = sp;
    entry->bitmaps    = bm;
    entry->rgba16_buf = rgba16_buf;
    entry->nbitmaps   = nbitmaps;
    return sp;
}

// ---------------------------------------------------------------------------
// Re-register fixup tracking on every call (scene-boundary safety).
// portResetStructFixups() clears sStructU16Fixups / sDeswizzle4cFixups at
// scene boundaries, so portMarkSyntheticSprite must be called again each time
// the CSS is entered to keep portFixup* as no-ops for these synthetic sprites.
// ---------------------------------------------------------------------------

static void reregisterEntry(const CacheEntry &e) {
    if (!e.sprite || !e.bitmaps || e.nbitmaps == 0) return;
    void **buf_ptrs = static_cast<void **>(std::malloc((size_t)e.nbitmaps * sizeof(void *)));
    if (!buf_ptrs) return;
    // Walk the Bitmap array, reading each bitmap's buf reloc token (LE u32 at
    // offset +0x08 within each 16-byte Bitmap struct) and resolving it back to
    // the actual pointer, so portMarkSyntheticSprite can re-register all entries.
    const uint8_t *bm_base = reinterpret_cast<const uint8_t *>(e.bitmaps);
    for (int i = 0; i < e.nbitmaps; ++i) {
        uint32_t token;
        std::memcpy(&token, bm_base + (size_t)i * 16 + 0x08, 4);
        buf_ptrs[i] = portRelocResolvePointer(token);
    }
    portMarkSyntheticSprite(e.sprite, e.bitmaps, (unsigned int)e.nbitmaps, buf_ptrs);
    std::free(buf_ptrs);
}

// ---------------------------------------------------------------------------
// PNG loader — loads a PNG and converts to RGBA16 BE.
// Expected dimensions are validated; returns nullptr on mismatch/error.
// ---------------------------------------------------------------------------

static uint8_t *loadPNGAsRGBA16BE(const std::string &path, int expected_w, int expected_h) {
    int w = 0, h = 0, channels = 0;
    uint8_t *rgba8 = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!rgba8) {
        port_log("CSS stage assets: stbi_load failed: %s (%s)\n",
                 path.c_str(),
                 stbi_failure_reason() ? stbi_failure_reason() : "?");
        return nullptr;
    }

    if (w != expected_w || h != expected_h) {
        port_log("CSS stage assets: %s — expected %dx%d, got %dx%d; skipping.\n",
                 path.c_str(), expected_w, expected_h, w, h);
        stbi_image_free(rgba8);
        return nullptr;
    }

    uint8_t *rgba16 = convertToRGBA16BE(rgba8, w, h);
    stbi_image_free(rgba8);

    if (!rgba16) {
        port_log("CSS stage assets: convertToRGBA16BE OOM for %s\n", path.c_str());
    }
    return rgba16;
}

// ---------------------------------------------------------------------------
// Core getter implementation.
// ---------------------------------------------------------------------------

static Sprite *getSprite(int gkind, AssetKind asset_kind) {
    // Find the stage table entry.
    int stage_idx = -1;
    for (int i = 0; i < kStageCount; ++i) {
        if (STAGE_TABLE[i].gkind == gkind) {
            stage_idx = i;
            break;
        }
    }
    if (stage_idx < 0) return nullptr;  // gkind not in table

    std::lock_guard<std::mutex> guard(sCacheMutex);

    CacheEntry &entry = sCache[stage_idx][asset_kind];

    if (entry.sprite) {
        // Cache hit: re-register for scene-boundary safety.
        reregisterEntry(entry);
        return entry.sprite;
    }

    // Cache miss: load the PNG.
    const StageEntry &se = STAGE_TABLE[stage_idx];

    // For nameplate sprites: return NULL immediately if this stage has no PNG.
    if (asset_kind == kName && !se.has_name_png) {
        return nullptr;
    }

    const char *suffix;
    int expected_w, expected_h;
    if (asset_kind == kBackground) {
        suffix     = "_background.png";
        expected_w = se.bg_w;
        expected_h = se.bg_h;
    } else if (asset_kind == kName) {
        suffix     = "_name.png";
        expected_w = kNameW;
        expected_h = kNameH;
    } else {
        suffix     = "_small.png";
        expected_w = kIconW;
        expected_h = kIconH;
    }

    std::string rel_path = std::string("assets/css_icons/") + se.name + suffix;
    std::string full_path = Ship::Context::GetPathRelativeToAppDirectory(rel_path.c_str());

    uint8_t *rgba16 = loadPNGAsRGBA16BE(full_path, expected_w, expected_h);
    if (!rgba16) {
        // File missing or wrong size — return NULL so caller falls back to ROM sprite.
        return nullptr;
    }

    Sprite *sp = nullptr;
    if (asset_kind == kBackground) {
        sp = buildBackgroundSprite(se, rgba16, &entry);
    } else if (asset_kind == kName) {
        sp = buildNameSprite(rgba16, &entry);
    } else {
        sp = buildIconSprite(rgba16, &entry);
    }

    if (!sp) {
        std::free(rgba16);
        port_log("CSS stage assets: sprite build failed for %s\n", full_path.c_str());
        return nullptr;
    }

    port_log("CSS stage assets: loaded %s (%dx%d, %d bitmaps)\n",
             full_path.c_str(), expected_w, expected_h, entry.nbitmaps);
    return sp;
}

} // namespace

// ---------------------------------------------------------------------------
// Public C entry points.
// ---------------------------------------------------------------------------

extern "C" Sprite *portCSSGetStageBackgroundSprite(int gkind) {
    return getSprite(gkind, kBackground);
}

extern "C" Sprite *portCSSGetStageIconSprite(int gkind) {
    return getSprite(gkind, kIcon);
}

extern "C" Sprite *portCSSGetStageNameSprite(int gkind) {
    return getSprite(gkind, kName);
}

#endif /* PORT */
