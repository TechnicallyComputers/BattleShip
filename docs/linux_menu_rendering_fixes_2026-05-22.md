# Linux Menu Rendering — Issue Family & BattleShip Fixes

**Audience:** Handoff for partner review (human or agent).  
**Status:** BattleShip offline + netmenu builds are clean on Linux/OpenGL for the
symptoms described below. Upstream `Kenix3/libultraship` and a vanilla port
without BattleShip's `port/` bridge still exhibit parts of this family.

---

## Executive summary

During early Linux bring-up, testers reported **inconsistent rendering across
menus** — Character Select gate panels, pause/training option rows, HUD digits,
title sprites, and portrait/description screens could show:

- Missing or blank sprites
- Sheared / zigzag text
- Wrong or state-dependent colors (especially CSS gate backdrops after re-entry)
- Squished 1-pixel-wide digits (VS Record)
- Flickering broken fighter previews on CSS hover, sometimes followed by crash

This was **not one bug**. It was a stack of independent failures that all
concentrated in menu scenes because menus are almost entirely **2D sprites +
CI palettes + frequent scene transitions**, with small reloc files loaded into
a recycled 16 MiB scene heap.

BattleShip fixes span three layers:

| Layer | Where | What it fixes |
|-------|-------|---------------|
| **Linux scene / pointer stability** | `decomp/src/sys/taskman.c`, `port/bridge/lbreloc_bridge.cpp`, `decomp/src/sys/objdisplay.c` | Intermittent corruption/crashes on menu re-entry (Linux glibc) |
| **Port reloc / sprite pipeline** | `port/bridge/lbreloc_byteswap.cpp`, `port/bridge/lbreloc_bridge.cpp`, `decomp/src/lb/lbcommon.c`, various `sc*` / `mn*` init paths | Garbled/sheared sprites, palette overlap, init-order hazards |
| **Forked libultraship Fast3D** | `libultraship/src/fast/interpreter.cpp`, `libultraship/src/fast/backends/gfx_opengl.cpp` | SETTIMG/G_VTX guards, PrimDepth, upload width clamp, OGL FB sampling |

**Offline vs netmenu:** Both build variants share the same rendering stack.
`SSB64_NETMENU=ON` adds `port/net/**` for rollback/netplay but does **not**
fork the sprite/reloc bridge or libultraship submodule. If menus render correctly
in offline Linux builds, netmenu builds inherit the same fixes.

**Upstream delta:** BattleShip pins `libultraship` to `JRickey/libultraship`
branch `ssb64`, not upstream Kenix3. Several Fast3D fixes live only in that fork.

---

## Symptom → likely cause → fix reference

Use this table to triage a repro against known fix families.

| What you see | Likely cause class | Primary fix doc |
|--------------|-------------------|-----------------|
| CSS gate panel gray/striped; worse after HMN↔CPU toggles or re-entering VS | Overlapping TLUT runtime fixups double-BSWAP palette bytes | [css_gate_palette_overlap_fixup_2026-04-29.md](bugs/css_gate_palette_overlap_fixup_2026-04-29.md) |
| Pause/training menu: only first option per row renders; digits wrong tint | Sprite struct writes before `portFixupSprite` | [training_mode_sprite_init_pre_fixup_2026-04-29.md](bugs/training_mode_sprite_init_pre_fixup_2026-04-29.md) |
| All sprite text/HUD sheared or zigzag (logo, labels, digits) | Pass2 misses runtime-built sprite texels + missing TMEM line deswizzle | [sprite_texel_tmem_swizzle_2026-04-10.md](bugs/sprite_texel_tmem_swizzle_2026-04-10.md), [sprite_32bpp_tmem_swizzle_2026-04-20.md](bugs/sprite_32bpp_tmem_swizzle_2026-04-20.md) |
| VS Record digits ~1 px wide vertical strokes | IA4 upload uses natural TMEM width (16) not SetTileSize clamp (4) | [ia4_upload_unclamped_to_tile_2026-04-29.md](bugs/ia4_upload_unclamped_to_tile_2026-04-29.md) |
| 2D portrait card in front of 3D fighter in description scenes | `gDPSetPrimDepth` was a no-op in Fast3D | [primdepth_unimplemented_2026-04-25.md](bugs/primdepth_unimplemented_2026-04-25.md) |
| Textures randomly blank in some scenes (Linux only); depends on heap layout | libultraship SETTIMG guard rejected valid Linux brk-arena pointers | [dl_link_stale_pointer_guard_2026-05-09.md](bugs/dl_link_stale_pointer_guard_2026-05-09.md) (Part 3) |
| CSS hover: flickering green broken fighter, lag, then SIGSEGV | Unresolved N64 segment address in G_VTX + stale scene heap | [g_vtx_unresolved_addr_guard_2026-05-03.md](bugs/g_vtx_unresolved_addr_guard_2026-05-03.md), [dl_link_stale_pointer_guard_2026-05-09.md](bugs/dl_link_stale_pointer_guard_2026-05-09.md) |
| Intro-skip or attract → CSS crash in `gcDrawDObjTreeDLLinks` (Linux) | Stale `DObjDLLink*` into freed/recycled scene heap | [dl_link_stale_pointer_guard_2026-05-09.md](bugs/dl_link_stale_pointer_guard_2026-05-09.md) |
| Intermittent Linux-only SIGSEGV at scene boundaries (Kirby effect, mnCharacters init) | Stale pointer family; partial fixes shipped | [linux_stale_scene_data_family_2026-05-11.md](bugs/linux_stale_scene_data_family_2026-05-11.md) |
| 1P stage-clear / VS photo wipe upside-down (Linux/NVIDIA OGL) | Wrong `FbNeedsSampleVFlip` assumption | [fb_passthrough_v_flip_linux_nvidia_2026-05-11.md](bugs/fb_passthrough_v_flip_linux_nvidia_2026-05-11.md) |
| BTT arrow pink outline + per-character inner color | Chain vs runtime TLUT fixup byte-count mismatch | [btt_arrow_loadtlut_dma_alignment_2026-04-29.md](bugs/btt_arrow_loadtlut_dma_alignment_2026-04-29.md) |

---

## Why Linux looked worse than Windows/macOS

Three platform-specific mechanisms amplified menu bugs on Linux/OpenGL:

### 1. glibc frees pages aggressively

On scene transition the port allocates a **16 MiB scene arena** for reloc files,
sprites, and transient game objects. An earlier approach `free()`'d this heap
each scene. On Linux/glibc, freed mmap blocks are unmapped and brk pages get
`madvise(MADV_DONTNEED)` — **dangling reads SIGSEGV**. macOS Magazine and
Windows LFH often leave freed pages readable, returning stale bytes instead of
faulting. The same latent BSS stale-pointer bugs therefore appeared as
**intermittent visual corruption or crashes only on Linux** while other OSes
"silently corrupted."

### 2. Non-PIE Linux heap addresses vs Fast3D guards

Reloc files frequently land in the brk arena around `0x10000000`–`0x80000000`.
Upstream libultraship commit `beb5b9a1` tightened `gfx_set_timg_handler_rdp` to
reject host pointers below 4 GB, intending to catch unresolved N64 segment
tokens. That guard **dropped legitimate SETTIMG commands** on Linux, blanking
textures. Menus load many small files → symptoms looked random.

### 3. OpenGL framebuffer sampling differs on Linux/NVIDIA

Issue #157 assumed OpenGL always needs a V-flip when sampling an `invertY`
game FBO as a texture. On Linux/NVIDIA proprietary drivers (observed 595.x),
that assumption was inverted — applying the flip broke FB-passthrough wallpapers
and transition photos. See the FB V-flip doc (mostly transitions, not all menus).

---

## Fix layer 1 — Scene heap recycle & cache eviction

**Primary doc:** [dl_link_stale_pointer_guard_2026-05-09.md](bugs/dl_link_stale_pointer_guard_2026-05-09.md)

### Scene heap recycle (`decomp/src/sys/taskman.c`)

Under `#ifdef PORT`, `syTaskmanStartTask` no longer `free()` + `malloc()` the
scene arena each scene. Instead:

1. **First scene:** `malloc(16 MiB)` once, register range with DL debugger.
2. **Later scenes:** `port_taskman_evict_arena_caches()` then `memset(0)` the
   entire arena.
3. Same virtual address stays mapped → stale pointer reads return zero (like
   macOS/Windows) instead of SIGSEGV on Linux.

Relevant lines: ~1325–1357 in `decomp/src/sys/taskman.c`.

### Arena cache eviction (`port/bridge/lbreloc_bridge.cpp`)

`port_taskman_evict_arena_caches(base, size)` clears every port-side structure
that could hold a pointer into the old arena:

- Packed display-list cache
- Texture upload cache
- Struct fixup idempotency sets (`portFixupSprite`, etc.)
- Reloc file range registry
- **`portRelocInvalidateRange`** — bumps generation on reloc tokens whose
  targets fall in the recycled arena (structural fix for stale-token family;
  see [linux_stale_scene_data_family_2026-05-11.md](bugs/linux_stale_scene_data_family_2026-05-11.md))

Also evicts stale entries from `sLBRelocInternBuffer.status_buffer[]` when their
addresses intersect the arena (Training → VS cross-mode transitions).

Relevant lines: ~197–234 in `port/bridge/lbreloc_bridge.cpp`.

### Defensive dl_link guards (`decomp/src/sys/objdisplay.c`)

PORT-gated bound checks + iteration cap in `gcDrawDObjTreeDLLinks` /
`gcDrawDObjDLLinks` catch out-of-range `list_id` when stale `DObjDLLink*`
survive scene transitions. Safety net if a latent BSS holder is not cleared.

---

## Fix layer 2 — Port reloc / sprite pipeline

Menus depend on the **reloc byte-swap bridge** (`port/bridge/lbreloc_byteswap.cpp`
+ `lbreloc_bridge.cpp`). N64 reloc files are big-endian blobs; pass1 blanket
`BSWAP32` scrambles sub-u32 fields and texture bytes. Pass2 fixes in-file DL
references; runtime + chain-walk fixups catch sprites built at draw time.

### Core sprite fixup chain

Called from `lbCommonMakeSObjForGObj` in `decomp/src/lb/lbcommon.c`:

1. **`portFixupSprite`** — rotate16/bswap32 on Sprite struct fields post-pass1
2. **`portFixupBitmapArray`** — fix bitmap metadata
3. **`portFixupSpriteBitmapData`** — BSWAP32 texel bytes + **TMEM line deswizzle**

**Docs:**
- [sprite_texel_tmem_swizzle_2026-04-10.md](bugs/sprite_texel_tmem_swizzle_2026-04-10.md) — 4/8/16bpp
- [sprite_32bpp_tmem_swizzle_2026-04-20.md](bugs/sprite_32bpp_tmem_swizzle_2026-04-20.md) — RGBA32 title logo / portraits

**Why pass2 misses menus:** Sprite LOAD blocks are built at **runtime** in
`lbCommonDrawSObjBitmap` from `bitmap.buf`, not stored in reloc DLs. Pass2 only
walks stored display lists → sprite texels need runtime/struct fixup.

### Per-word TLUT idempotency (`sTexFixupWords`)

**Doc:** [css_gate_palette_overlap_fixup_2026-04-29.md](bugs/css_gate_palette_overlap_fixup_2026-04-29.md)

CSS gate panels share one red-card bitmap; each player slot swaps only
`sprite->LUT` to a small adjacent TLUT block in `MNPlayersCommon`. Each panel
LOAD requests ~512 bytes of palette fixup. Old idempotency keyed only on **start
address** → overlapping loads from panel 2/3/4 **double-BSWAP** bytes panel 1
already fixed → state-dependent garbled colors.

Fix: track fixed texture bytes per **u32 word** in `sTexFixupWords` (mirrors
per-vertex idempotency in `portRelocFixupVertexAtRuntime`). Also records fixups
in `sTexFixupExtent` so chain-walk and runtime LOADTLUT paths stay consistent
(see [btt_arrow_loadtlut_dma_alignment_2026-04-29.md](bugs/btt_arrow_loadtlut_dma_alignment_2026-04-29.md)).

### Init-order hazards (pause / training menus)

**Doc:** [training_mode_sprite_init_pre_fixup_2026-04-29.md](bugs/training_mode_sprite_init_pre_fixup_2026-04-29.md)

Training mode init writes `red`/`green`/`blue`/`attr` into sprite arrays **before**
`lbCommonMakeSObjForGObj` runs `portFixupSprite`. Writes land at wrong byte
offsets in the post-pass1 scrambled layout → only sprites that happen to go
through fixup first render correctly ("first option per row OK").

Fix: `sc1PTrainingModeFixupSprite` applies full fixup at init time.

Related menu fixes in the same family:
- [css_reentry_status_selected_stale_2026-04-29.md](bugs/css_reentry_status_selected_stale_2026-04-29.md) — CSS re-entry pose
- [training_mode_sprite_array_stride_2026-04-24.md](bugs/training_mode_sprite_array_stride_2026-04-24.md) — LP64 pointer array stride
- [zako_stock_sprite_unfixed_2026-05-01.md](bugs/zako_stock_sprite_unfixed_2026-05-01.md) — struct copy without fixup

### Broader pipeline reference

- [fighter_render_investigation.md](fighter_render_investigation.md) — full reloc
  load pipeline (pass1/pass2/chain/runtime/struct fixups)
- [sprite_broken_textures_handoff_2026-04-19.md](sprite_broken_textures_handoff_2026-04-19.md) — sprite triage handoff
- [css_panel_garble_investigation_2026-04-29.md](css_panel_garble_investigation_2026-04-29.md) — pre-fix investigation notes
  (superseded by css_gate_palette_overlap fix)

---

## Fix layer 3 — Forked libultraship Fast3D deltas

Submodule: **`JRickey/libultraship` branch `ssb64`** (not upstream Kenix3).

### SETTIMG unresolved-address guard (`gfx_set_timg_handler_rdp`)

**File:** `libultraship/src/fast/interpreter.cpp` (~5474–5524)

If `SegAddr(w1)` resolves to `<= 0x0FFFFFFF`, treat as unresolved N64 segment
token → skip SETTIMG (don't dereference). **Do not** widen to a 4 GB host cap;
that rejects valid Linux brk pointers.

Diagnostic: `SSB64_RENDER_DIAG=1` logs rejected addresses once each.

Upstream PR #1042 added this pattern for SETTIMG only; BattleShip's fork keeps
the Linux-safe threshold.

### G_VTX unresolved-address guard

**Doc:** [g_vtx_unresolved_addr_guard_2026-05-03.md](bugs/g_vtx_unresolved_addr_guard_2026-05-03.md)

Same `<= 0x0FFFFFFF` filter propagated to `gfx_vtx_handler_f3dex2`, `f3dex`,
and `f3d`. Prevents CSS-hover SIGSEGV; logs `n_vertices`/`dest`/raw `w1`.
Does **not** fix upstream stale-pointer seeding — defensive only.

### gDPSetPrimDepth (`G_ZS_PRIM` sprite layering)

**Doc:** [primdepth_unimplemented_2026-04-25.md](bugs/primdepth_unimplemented_2026-04-25.md)

Implemented store of `prim_depth_z`/`prim_depth_dz` and override per-vertex Z in
`GfxSpTri1` when depth source is `G_ZS_PRIM`. Upstream Kenix3 still has the
TODO stub. Affects description/portrait scenes and HUD layering.

### ClampUploadWidthToTile (IA4 / menu digits)

**Doc:** [ia4_upload_unclamped_to_tile_2026-04-29.md](bugs/ia4_upload_unclamped_to_tile_2026-04-29.md)

`ImportTextureIA4` (and related paths) now clamp GPU upload width to
SetTileSize, not natural TMEM line width. Fixes VS Record squished digits.
Helper: `ClampUploadWidthToTile()` in `interpreter.cpp`.

### OpenGL FB sample V-flip

**Doc:** [fb_passthrough_v_flip_linux_nvidia_2026-05-11.md](bugs/fb_passthrough_v_flip_linux_nvidia_2026-05-11.md)

`GfxRenderingAPIOGL::FbNeedsSampleVFlip()` returns `false` unconditionally.
Fixes upside-down FB-passthrough on Linux/NVIDIA; D3D11/Metal unchanged.

---

## Diagnostics & reproduction tooling

| Env var / tool | Purpose |
|----------------|---------|
| `SSB64_RENDER_DIAG=1` | Log rejected G_SETTIMG addresses (`interpreter.cpp`) |
| `SSB64_TEX_FIXUP_LOG=1` | Per-fixup log from `portRelocFixupTextureAtRuntime` / chain path |
| `SSB64_STAGE_AUDIT=1` | Per-file pass2/chain/runtime fixup counters (stage investigation infra) |
| GBI trace + `debug_tools/gbi_diff/gbi_diff.py` | Compare port vs Mupen64Plus command streams — see [debug_gbi_trace.md](debug_gbi_trace.md) |

**CSS gate repro (issue #3):** Re-enter VS mode several times; toggle HMN↔CPU
between entries. Compare `tex_fixup.log` LUT ranges across visits.

**Linux stale-pointer repro:** Intro-skip during opening, or extended attract
→ CSS with many scene transitions. Bisect note in dl_link doc: `mallopt(M_MMAP_MAX,0)`
+ restore `free(sPrevHeap)` makes repro ~7 transitions instead of ~22+.

---

## What upstream still lacks (checklist for partner diff)

When comparing against vanilla `Kenix3/libultraship` or an unpatched port tree:

| Fix | BattleShip location | In upstream Kenix3? |
|-----|---------------------|---------------------|
| Linux-safe SETTIMG guard (256MB cap, not 4GB) | `libultraship/.../interpreter.cpp` | No — over-strict guard |
| G_VTX guard (PR #1042 parity) | same | Partial — SETTIMG only |
| `gDPSetPrimDepth` implementation | same | No — TODO stub |
| `ClampUploadWidthToTile` for IA4/I8/IA16 | same | No |
| `FbNeedsSampleVFlip` Linux/NVIDIA fix | `gfx_opengl.cpp` | No |
| Scene heap recycle + token invalidation | `decomp/`, `port/` | Port-specific |
| Sprite/TMEM/TLUT fixup pipeline | `port/bridge/lbreloc_*` | Port-specific |
| Training/CSS init-order fixes | `decomp/src/sc/`, `decomp/src/mn/` | Port-specific |

To see BattleShip's libultraship delta: diff submodule `JRickey/libultraship`
`ssb64` against `Kenix3/libultraship` `main`, focusing on
`src/fast/interpreter.cpp` and `src/fast/backends/gfx_opengl.cpp`.

---

## Related bug index entries

All individual write-ups are linked from [bugs/README.md](bugs/README.md).
Key slugs for this family:

| Date | Slug |
|------|------|
| 2026-04-10 | [sprite_texel_tmem_swizzle](bugs/sprite_texel_tmem_swizzle_2026-04-10.md) |
| 2026-04-20 | [sprite_32bpp_tmem_swizzle](bugs/sprite_32bpp_tmem_swizzle_2026-04-20.md) |
| 2026-04-25 | [primdepth_unimplemented](bugs/primdepth_unimplemented_2026-04-25.md) |
| 2026-04-29 | [css_gate_palette_overlap_fixup](bugs/css_gate_palette_overlap_fixup_2026-04-29.md) |
| 2026-04-29 | [training_mode_sprite_init_pre_fixup](bugs/training_mode_sprite_init_pre_fixup_2026-04-29.md) |
| 2026-04-29 | [ia4_upload_unclamped_to_tile](bugs/ia4_upload_unclamped_to_tile_2026-04-29.md) |
| 2026-04-29 | [btt_arrow_loadtlut_dma_alignment](bugs/btt_arrow_loadtlut_dma_alignment_2026-04-29.md) |
| 2026-05-03 | [g_vtx_unresolved_addr_guard](bugs/g_vtx_unresolved_addr_guard_2026-05-03.md) |
| 2026-05-09 | [dl_link_stale_pointer_guard](bugs/dl_link_stale_pointer_guard_2026-05-09.md) |
| 2026-05-11 | [linux_stale_scene_data_family](bugs/linux_stale_scene_data_family_2026-05-11.md) |
| 2026-05-11 | [fb_passthrough_v_flip_linux_nvidia](bugs/fb_passthrough_v_flip_linux_nvidia_2026-05-11.md) |

---

## Open issues / not fully resolved

- **Latent BSS stale-pointer owner** — scene heap recycle + guards catch symptoms;
  the specific long-lived holder of stale `DObjDLLink*` is not identified
  ([dl_link_stale_pointer_guard](bugs/dl_link_stale_pointer_guard_2026-05-09.md)).
- **linux_stale_scene_data_family** — three of four observed Linux-only variants
  still open; one defensive guard shipped for Kirby Cutter Draw
  ([linux_stale_scene_data_family_2026-05-11.md](bugs/linux_stale_scene_data_family_2026-05-11.md)).
- **g_vtx guard** — proximate crash prevention only; root cause of bad GBI
  addresses at scene transition still unknown
  ([g_vtx_unresolved_addr_guard_2026-05-03.md](bugs/g_vtx_unresolved_addr_guard_2026-05-03.md)).

---

## Historical note — missing handoff files

Some bug entries reference `docs/linux_intro_skip_crash_handoff_2026-05-09.md`
and `...-session2.md` from the May 2026 bisect that identified PR #144 +
libultraship `beb5b9a1`. Those handoff files are **not present in the repo**
anymore; the shipped fix state is captured in
[dl_link_stale_pointer_guard_2026-05-09.md](bugs/dl_link_stale_pointer_guard_2026-05-09.md)
and this document.

---

## Suggested review workflow for partner agent

1. Read this doc top-to-bottom for the fix map.
2. If reproducing on upstream: check **Fix layer 3 checklist** first (libultraship diff).
3. For sprite/shear/garble: trace `lbCommonMakeSObjForGObj` → `portFixupSprite*`.
4. For state-dependent CSS colors: inspect `sTexFixupWords` path in
   `portRelocFixupTextureAtRuntime`.
5. For Linux-only intermittent crash on menu re-entry: inspect `syTaskmanStartTask`
   PORT heap recycle + `port_taskman_evict_arena_caches`.
6. Enable `SSB64_RENDER_DIAG=1` and `SSB64_TEX_FIXUP_LOG=1` during repro.
7. Cross-reference individual bug docs for verification steps and issue numbers.
