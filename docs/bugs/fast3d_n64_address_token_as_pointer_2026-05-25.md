# Fast3D: N64 address tokens mistaken for host pointers (and C strings)

**Date:** 2026-05-25  
**Status:** FIX SHIPPED (defensive guards in BattleShip `libultraship` fork, branch `ssb64`)  
**Audience:** Shareable summary for port/rendering contributors — not a decomp gameplay bug.

---

## Executive summary

On the N64, many GBI command operands are **not host pointers**. They are **segmented addresses** (segment ID in the high byte + 24-bit offset), **reloc file offsets**, or **ROM/RDRAM tokens** that only become meaningful after `gsSPSegment`, DMA, or the port’s reloc pipeline runs.

The PC port’s Fast3D interpreter (`libultraship` `Interpreter::SegAddr`) resolves what it can, but when resolution fails it **returns `w1` unchanged** — casting that integer to `void*`, `F3DVtx*`, or **`const char*`** and passing it into OpenGL/D3D/Metal paths.

Values like **`0x00024720`** (a small offset or unresolved token) are **never** valid C strings or vertex buffers on Linux/macOS/Windows, yet filepath handlers called `strlen` / resource loaders, and SETTIMG/VTX paths dereferenced them. That is **port / Fast3D integration**, not “the N64 game wrote a bad pointer.”

BattleShip added **guards** so unresolved tokens are skipped or rejected instead of crashing. Guards do **not** fix why a stale or unrelocated token reached the display list in the first place.

---

## How addressing works on hardware vs the port

| On N64 | On BattleShip PC port |
|--------|------------------------|
| RSP segment table maps `0x04xxxxxx` → RDRAM | `mSegmentPointers[]` + per-frame `gsSPSegment` |
| Display lists in ROM use segment tags | Reloc load may rewrite some `0x0E` refs to **absolute file addresses** at bake time |
| Strings for OTR-style assets are real RDRAM pointers after load | OTR commands expect **`const char*`** in host memory |

**`Interpreter::SegAddr(uintptr_t w1)`** resolution order:

1. `portRelocTryResolvePointer((uint32_t)w1)` — port reloc token table  
2. `mSegmentPointers[seg] + offset` if that segment is bound this frame  
3. **Fallback:** `return (void*)w1;` — the raw command word is treated as a host pointer

Step 3 is correct for some PC-side absolute addresses, but catastrophic when `w1` is still an **N64-shaped token** because:

- the segment was **not set** yet (or was cleared at frame start),  
- reloc **did not apply** to that command, or  
- the word is only a **byte offset** into a reloc blob (e.g. `0x24720`), not a full tagged address.

---

## Bug class (one family, several symptoms)

**Class:** *Unresolved N64 addressing token interpreted as a host pointer (sometimes as a C string).*

### Symptom A — SIGSEGV in vertex load (`G_VTX`)

`gfx_vtx_handler_*` → `SegAddr(w1)` → `GfxSpVertex` → read through `vertices[i]`.

Example crash: `fault_addr=0x248d2a` — the “pointer” is just the low bits of `w1`, not mapped memory.

**Doc:** [g_vtx_unresolved_addr_guard_2026-05-03.md](g_vtx_unresolved_addr_guard_2026-05-03.md)

### Symptom B — Invalid texture bind (`G_SETTIMG`)

`gfx_set_timg_handler_rdp` → `SegAddr` → `gfx_check_image_signature` / texture upload.

If `w1` is unresolved, `imgData` points at address `<= 0x0FFFFFFF` (classic N64 segment space on a 32-bit token). Dereferencing causes corruption or crash.

**Guard:** skip SETTIMG when resolved address `<= 0x0FFFFFFF` (do **not** use a 4 GB cap on 64-bit — valid host allocations can live below 4 GB on Linux).

### Symptom C — OTR filepath commands treated as strings (`G_*_OTR_FILEPATH`, etc.)

Handlers such as `gfx_set_timg_otr_filepath_handler_custom` do:

```cpp
const char* fileName = (const char*)gfx->SegAddr(cmd->words.w1);
LoadResourceProcess(fileName);  // strlen / path logic inside
```

If `SegAddr` returns `0x24720`, the resource layer walks memory as a path → **undefined behavior**, null loads, or crashes in GL/D3D/Metal init code.

**Guard (in `gfx_step`):** for OTR filepath opcodes, resolve via `SegAddr`, then reject if:

- address `< 0x10000`, or  
- on 64-bit: above canonical user-space bound, or  
- `gfxPointerHasReadableBytes(ptr, 8)` fails  

→ skip command without calling `strlen`.

### Symptom D — `__OTR__` signature check on non-strings

`gfx_check_image_signature` used to assume any even pointer might be an OTR string. Small integers like `0x24720` are not strings.

**Guard:** same low-address and readability filters before `OtrSignatureCheck`.

---

## Why you see `0x24720` on the host

`0x24720` is typical of:

- a **24-bit offset** into a reloc file (e.g. `reloc_movies/MVCommon` with `fileSize=0x24720` in runtime texture fixup logs), or  
- a **partial / untagged** word left in a heap-built display list when segment `0x0E` was not bound.

It is **not** a plausible host `char*` or `F3DVtx*` on 64-bit Linux. Treating it as one is the integration bug.

Port-side classification for diagnostics (no deref):

```text
n64_seg[0]+0x24720   /* addr <= 0x0FFFFFFF */
```

See `port_classify_dl_ptr` in `port/bridge/lbreloc_bridge.cpp`.

---

## Fixes shipped (BattleShip / `libultraship` fork)

| Layer | File | Behavior |
|-------|------|----------|
| SETTIMG | `interpreter.cpp` `gfx_set_timg_handler_rdp` | Skip if resolved `w1 <= 0x0FFFFFFF`; optional `SSB64_RENDER_DIAG=1` log |
| G_VTX (f3dex2/f3dex/f3d) | `interpreter.cpp` | Skip vertex load + `SPDLOG_ERROR` with `n_vertices` / raw `w1` |
| OTR filepath opcodes | `interpreter.cpp` `gfx_step` | Pre-check `SegAddr(w1)` before OTR handlers run |
| OTR signature | `interpreter.cpp` `gfx_check_image_signature` | Readable-memory + range filter before `__OTR__` check |
| Reloc / runtime tex | `port/bridge/lbreloc_byteswap.cpp` | `runtimeTexFix` maps file-relative offsets when segment path fails |
| DL push (related) | `gfx_dl_handler` | Intra-file fallback for unresolved `0x0E` G_DL; conservative walked-past reject |

Upstream Kenix3 `libultraship` had partial coverage (e.g. LUS PR #1042 for SETTIMG only). BattleShip’s **`ssb64`** branch extends the same **“unresolved if `<= 0x0FFFFFFF`”** rule across VTX, OTR filepath dispatch, and signature checks.

**Index of Linux menu / Fast3D fork deltas:** [linux_menu_rendering_fixes_2026-05-22.md](../linux_menu_rendering_fixes_2026-05-22.md) (Fix layer 3).

---

## What these fixes do **not** do

- They do **not** prove the N64 ROM or decomp “sent a bad pointer” — the game sent a **token** valid in its memory model.  
- They do **not** stop **upstream** stale display lists (scene heap reuse, fixup cache, CSS material sub-DLs, segment not bound for a frame). Visual glitches can still occur; the guard only prevents the **last** dereference from killing the process.  
- They do **not** replace proper **reloc + `gsSPSegment`** setup for runtime-built DLs — that remains the structural fix when bisecting a scene.

---

## Diagnostics

| Environment variable | Effect |
|---------------------|--------|
| `SSB64_RENDER_DIAG=1` | Log rejected `G_SETTIMG` addresses (once per distinct value) |
| `SSB64_TEX_FIXUP_LOG=1` | Reloc/runtime texture fixup (`runtimeTexFix` lines with `off=0x…`) |
| Crash `fault_addr < 0x10000000` in `Fast::Interpreter::GfxSp*` | Same family — grep logs for `G_VTX(...) skipped: unresolved addr=` |

---

## Takeaway for sharing

> **Root issue:** Fast3D assumed GBI `w1` operands were host pointers (including C strings) after a best-effort `SegAddr`. Unrelocated N64 segment/offset tokens (e.g. `0x24720`) are integers, not pointers.  
> **Fix direction:** Classify resolved addresses before deref (`<= 0x0FFFFFFF` ⇒ unresolved N64 token on this port); never call string or vertex APIs on those values. Fix segment/reloc upstream separately.

---

## Related documentation

- [g_vtx_unresolved_addr_guard_2026-05-03.md](g_vtx_unresolved_addr_guard_2026-05-03.md) — VTX crash bisect  
- [linux_stale_scene_data_family_2026-05-11.md](linux_stale_scene_data_family_2026-05-11.md) — DL walker variant 4/5 (stale segment tokens)  
- [linux_menu_rendering_fixes_2026-05-22.md](../linux_menu_rendering_fixes_2026-05-22.md) — umbrella for menu/CSS rendering + LUS fork  
- [fighter_render_investigation.md](../fighter_render_investigation.md) — reloc load pipeline (pass1/pass2/runtime fixups)
