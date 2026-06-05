# Netplay — cross-ISA libm trig determinism (2026-06-04)

**Date:** 2026-06-04  
**Status:** Fix shipped (soak pending) — see **Root cause #2** below (`du` constant endianness, `__cosf(0)=+inf`)  
**Area:** `port/stubs/libc_compat.c`, `CMakeLists.txt`, `decomp/include/PR/guint.h`, `decomp/src/libultra/gu/sinf.c`, `cosf.c`

Playbook: [netplay_cross_isa_float_determinism.md](../netplay_cross_isa_float_determinism.md)

## Symptom

Cross-ISA (Android aarch64 host ↔ Linux x86_64 guest) Ness PK Thunder soak: frame commit @ validation **1285** splits **`figh` only** while **inputs, world, rng, eff** match. Last agreed FC @1245; drift window 1246–1284 (~40 ticks). PK Thunder hold → jibaku → landing → re-hold cycle. Rollback reanchor to 1245 correct but resim could not reconverge; epoch hold deadlock secondary.

Earlier @1247: `vel_air` already differed in the 4th decimal (`-97.162560` vs `-97.163429`) despite matched inputs.

## Root cause (Class A — cross-libm)

1. Decomp sim calls **`__sinf` / `__cosf`** (IDO builtins), e.g. jibaku launch in `syNetplayCanonicalizeNessPKJibakuLaunchState`: `__cosf(angle) * VEL`.
2. Port routed these to **system libm** via [port/stubs/libc_compat.c](../../port/stubs/libc_compat.c) (`cosf`/`sinf` wrappers).
3. The **N64 Cody–Waite polynomial** in `decomp/src/libultra/gu/sinf.c` / `cosf.c` was **not compiled** (only `mtxcatf`, `mtxutil`, `mtxxfmf`, `normalize` in `SSB64_LIBULTRA_PORT`).
4. **bionic ≠ glibc**: different range reduction / coefficients → different float ULP. `syNetplayQuantizeF32` (1/65536) snaps pre-snap values on opposite sides of a grid midpoint to **different cells** → divergent `vel_air` → `syNetSyncFoldFighterSlotFullContribution` → `figh` split.

`-ffp-contract=off` **cannot fix this** — transcendental executes inside precompiled `libm.so`.

## Secondary (Class B — matcher gaps)

Sync-relevant TUs not in contraction matcher: `decomp/src/gr/` (Sector Arwing `__sinf`/`syUtilsArcTan2`), `sys/utils.c` (jibaku angle polynomial), `sys/vector.c` (angle diff). Addressed in same change set.

Render-only (no hash writeback): `sys/matrix.c`, `guMtxCatF` — excluded from scope.

## Fix

| Layer | Change |
|-------|--------|
| **Compile N64 trig** | Add `decomp/src/libultra/gu/sinf.c`, `cosf.c` to build when `SSB64_NETMENU=ON` |
| **Symbol routing** | Gate out `libc_compat.c` `__sinf`/`__cosf` wrappers in netmenu (avoid duplicate symbol) |
| **No global `sinf`/`cosf` alias** | Netmenu port: omit `#pragma weak sinf/cosf = __sinf/__cosf` in `gu/sinf.c` + `cosf.c` so libc `sinf()` (audio LFO) stays system libm; gameplay still calls `__sinf`/`__cosf` |
| **`__libm_qnan_f`** | Port definition `(float)NAN` netmenu-only (`guint.h` extern, MIPS `libm_vals.s` not built) |
| **Matcher** | `-ffp-contract=off` on `sinf.c`, `cosf.c`, `sys/utils.c`, `sys/vector.c`, `/decomp/src/gr/` |
| **Docs** | Playbook `docs/netplay_cross_isa_float_determinism.md`; audit update |

## Documented deviation

- **Offline binary** (`SSB64_NETMENU=OFF`): unchanged — still system libm via `libc_compat.c`.
- **Netmenu binary** (including offline modes inside it): uses N64 polynomial for `__sinf`/`__cosf`. More ROM-faithful; eliminates cross-ISA libm split. Not promoted to JRickey offline until review.

## Verification

1. Netmenu build links without duplicate `__sinf`/`__cosf` or undefined `__libm_qnan_f`.
2. Offline build (`SSB64_NETMENU=OFF`) still provides wrappers; no libultra sin/cos objects linked.
3. `compile_commands.json`: `-ffp-contract=off` on new/changed TUs.
4. Cross-ISA Ness soak: no `figh`-only FC split @1285 with matched inputs through full hold/jibaku/land/hold cycle.
5. Sector Z: Arwing weapon orientation stable cross-ISA (`grsector.c` trig path).

## Regression (2026-06-04) — intro hang after weak `sinf` alias

**Symptom:** Netmenu AppImage freezes Opening Room @ frame 73; watchdog reports audio tid stuck in `alCents2Ratio` ← `syAudioUpdateOsc` (vibrato LFO) after first non-zero BGM sample.

**Cause:** `#pragma weak sinf = __sinf` redirected `syAudioUpdateOsc`'s libc `sinf()` to N64 polynomial; `(s32)NaN` from bad float path → `alCents2Ratio(INT_MIN)` infinite loop.

**Fix:** Gate weak `sinf`/`cosf` aliases off when `PORT && SSB64_NETMENU`; deterministic trig remains on explicit `__sinf`/`__cosf` only.

## Regression (2026-06-04) — NETMENU=ON offline render vanish after alias removal

**Symptom:** CSS fighter preview, level-select stage 3D, and other DObj draws vanish or look wrong in **offline modes inside the netmenu binary** only (`SSB64_NETMENU=OFF` clean).

**Cause:** Several render paths pair `__sinf()` with bare `cosf()`. On N64 (and NETMENU=OFF) both resolve to the same libm; after omitting `#pragma weak cosf = __cosf`, netmenu build mixes N64 `__sinf` with libc `cosf()` → non-orthonormal rotation matrices and camera frames.

**Fix (ungated decomp):** Use `__cosf()` at mixed callsites — `sys/objdisplay.c` cases 45/46, `mn/mndata/mncharacters.c` `mnCharactersMoveFighterCamera`. Justification for JRickey review: canonical IDO builtin (matches `lbcommon.c`); **no NETMENU=OFF behavior change** (`libc_compat.c` `__cosf` wraps `cosf`); avoids `#ifdef PORT && SSB64_NETMENU` noise at each site. Do **not** restore global `cosf` weak alias without also fixing `sinf()`+`cosf()` pairs in `sys/matrix.c` / `sys/vector.c`.

## Follow-up (2026-06-04) — complete N64 poly routing (Option A)

CSS / level select still broken after objdisplay/mncharacters pairing fix because those scenes use **`sys/matrix.c`** rotation (`syMatrixRotRpyR`, etc.) — already paired `__sinf`/`__cosf`, not objdisplay cases 45/46.

**Additional changes (ungated, NETMENU=OFF noop via wrappers):**

| File | Change |
|------|--------|
| `libultra/gu/sinf.c`, `cosf.c` | `memcpy` bit extract instead of `*(int*)&x` (strict-aliasing UB on Clang) |
| `sys/matrix.c` | `syMatrixPerspF`: `sinf`/`cosf` → `__sinf`/`__cosf` |
| `sys/vector.c` | `syVectorRotate3D`, `syVectorRotateAbout3D`: → `__sinf`/`__cosf` |
| `ft/ftphysics.c` | TransN: `cosf` → `__cosf` (paired with `__sinf`) |
| `wp/wppikachu/wppikachuthunderjolt.c` | `cosf` → `__cosf` |
| `lb/lbcommon.c` | `lbCommonCheckAdjustSim2D`: `cosf` → `__cosf` |

**Intentional libc `sinf()` only:** `sys/audio.c` LFO / vibrato (weak `sinf` alias must stay off).

## Render vs sim trig — split, then unified (2026-06-04)

**Original split (superseded):** while `__cosf` was still returning `+inf` (root cause #2), the Pipeline-2 MVP recalc callsites (`lbcommon` `func_ovl0_800CA*`, `objdisplay` cases 45/46, `mnCharactersMoveFighterCamera`, `syMatrixPerspF`) were routed to a `gSYSinTable` **float** workaround (`syMatrixSinF`/`syMatrixCosF`) so the broken poly couldn't produce degenerate `FTOFIX32` matrices (fighters / intro / stage-select 3D vanish). That float wrapper also carried the Q15-vs-s14 divisor bug (root cause #3).

**Unification (current):** with the Cody-Waite constants fixed and `__sinf`/`__cosf` verified numerically correct (incl. large-angle range reduction, `sin²+cos² = 1`), the workaround is removed. Render and sim now share the **same deterministic N64 polynomial** in the netmenu build:

| Layer | Mechanism |
|-------|-----------|
| **Sim** (`ft/`, `it/`, `wp/`, `vector.c`, …) | `__sinf` / `__cosf` → N64 polynomial when `SSB64_NETMENU=ON` |
| **Render MVP** | `SSB64_RENDER_SINF` / `SSB64_RENDER_COSF` (`sys/matrix.h`) → `__sinf`/`__cosf` in **all** builds (macro kept only as a render-side marker). `syMatrixPerspF` netmenu branch → `__cosf`/`__sinf`; offline branch keeps libc `cosf`/`sinf` (JRickey parity) |
| **Render rotation (most 3D)** | Unchanged: `syGetSinCosUShort` / `syMatrixRotRpyR` integer `gSYSinTable` lookup — original N64 fixed-point path, already deterministic & not an OS call |
| **Audio** | Bare libc `sinf()` only (LFO/vibrato) |

`syMatrixSinF`/`syMatrixCosF` and the `SYMATRIX_*_TO_F32` macro were deleted from `matrix.c`. The original-decomp `syMatrixFastSin`/`syMatrixFastCos` are kept (untouched). Callsites in `lbcommon.c`/`objdisplay.c`/`mncharacters.c` still use the `SSB64_RENDER_*` macros and were not edited.

## Root cause #2 (2026-06-04) — `du` trig constants byte-swapped on little-endian (`__cosf(0)=+inf`)

**Symptom:** Cross-ISA Ness soak — Ness vanishes/clips on a backward double jump (`JumpAerialB`, status 24), then desyncs. New `airveltransn_nan` witness probe (`SSB64_NETPLAY_STATUSVARS_WITNESS=1`) caught the producing op directly: in `ftPhysicsGetAirVelTransN`, with `rot_z=0x00000000`, `__sinf(0)=0x00000000` (correct) but **`__cosf(0)=0x7f800000` (+inf)**. `anim_vel·(+inf)` → `0·inf = NaN` (host `+nan 0x7fc00000` / guest `-nan 0xffc00000`) then `finite·inf = ±inf`, integrating into `vel_air`/`translate` until the fighter position is `(nan,nan)` ~60 ticks later (PK Thunder `fighter_nan` probe).

**Root cause:** `sinf.c`/`cosf.c` build their Cody–Waite constants from `du` word pairs:

```c
typedef union { struct { unsigned int hi; unsigned int lo; } word; double d; } du; /* PR/guint.h */
static const du rpi = { 0x3fd45f30, 0x6dc9c883 }; /* big-endian N64 hi/lo halves */
```

Reading `.d` is only correct on **big-endian** N64. On **little-endian** PC (x86/x64/aarch64) the two 32-bit halves land in the wrong end of the double, so `rpi`/`pihi`/`pilo` become astronomically huge → range reduction overflows → `fcos` returns `±inf`. `fsin` only dodged it via its `|x|<1.5` fast path (`return x` for tiny args, near-linear for the rest), which is why `__sinf(0)` looked fine while `__cosf(0)` blew up. The earlier `ssb64_float_bits` `memcpy` fix (root cause #1 follow-up) addressed the **input** bit-pun aliasing UB but not the **constant** byte order.

This is the deterministic origin: `cos=+inf` is identical on both peers; only the downstream `0·inf` NaN **sign bit** diverges per ISA (the actual `figh` hash split). Strong candidate for the broader "NETMENU=ON 3D render vanish / intro props missing" family too (any netmenu `__cosf` range-reduction caller).

**Fix:** `SSB64_DU_HL(hi, lo)` macro in `PR/guint.h` reorders the halves at compile time by `__BYTE_ORDER__` (big-endian keeps N64 order; little-endian / unknown swaps), so `.d` reconstructs the intended double on any host. Applied to all `du` constants in `sinf.c` and `cosf.c`. Verified standalone: `rpi.d == 1/π` exactly. NETMENU=OFF unaffected (those TUs use the `libc_compat` `cosf`/`sinf` wrappers, not compiled). Diagnostics: `airveltransn_nan` / `corrupt jumpaerial` / `jumpaerial entry` witness rows + `diff_netstatusvars_nan` in `scripts/netplay-trim-logs.py`.

## Root cause #3 (2026-06-04) — render MVP sin/cos 2x too large (`gSYSinTable` is Q15, not s14)

**Symptom:** With root causes #1/#2 fixed, the soak NaN families are gone (`airveltransn_nan` / `corrupt jumpaerial` / `fighter_nan` all 0, no cross-ISA `figh` divergence) and the intro "render vanish" is resolved. New cosmetic regression: certain **spinning articles render ~2x bigger** in `SSB64_NETMENU=ON` only — e.g. Ness PK Thunder, the Yoshi Egg Lay capture egg. The camera/scene and fighters are correctly sized; only specific articles inflate.

**Root cause:** The render-MVP wrapper introduced for the split (above) normalized the lookup table with the wrong divisor:

```c
#define SYMATRIX_S14_TO_F32(v) ((f32)(v) / 16384.0F)   /* WRONG */
```

`gSYSinTable` is **Q15**, not s14 — `gSYSinTable[1024] == 0x8000 == 32768 == 1.0`. (The integer matrix builders prove it: `syMatrixTraRotRpyRSca` does `(table*table) >> 14`, which only lands on `FTOFIX32` `×65536` format if each table entry is `value*32768`.) Dividing by 16384 made `syMatrixSinF`/`syMatrixCosF` return up to **2.0**.

Why only some objects, and only netmenu:
- `objdisplay.c` billboard cases 45/46 build the MVP as `gGCMatrixPerspF[..] * scale * cos/sin` — the inflated `cos/sin` multiplies object size directly → ~2x. Same for `lbcommon`/`mncharacters` render callsites.
- `syMatrixPerspF` uses `cot = cos/sin`; the 2x cancels in the ratio, so the global projection (camera, fighters, stage) is unaffected — exactly why only the spinning articles look big.
- Offline uses `__sinf/__cosf` (libm) for these callsites, so it was always correct.

**Fix:** divide by `32768.0F` (renamed macro `SYMATRIX_Q15_TO_F32`) in `sys/matrix.c`. `syMatrixPerspF` behavior is unchanged (ratio). Offline unaffected (wrappers not compiled).

## Follow-up (2026-06-04) — `sys/interp.c` contraction gap (Sector Z Arwing deck map-hash split)

**Status:** Fix shipped (CMake matcher only) — **soak pending**.

**Symptom:** Cross-ISA (Android aarch64 host ↔ Linux x86_64 guest) Sector Z soak after the figh-side trig fix landed. Ness jibaku play; fighters stay in perfect lockstep (`figh`/`world`/`rng`/`anim`/`eff`/`cam` byte-identical cross-peer) but the per-tick **map** hash splits: tick 1043 host `mph=0x9A840396` vs guest `mph=0xCB036590`. Masked while a fighter is fragile on the deck by the `sector_arwing_deck` synctest gate; once the player jumps **off**, the gate disengages and the still-divergent Arwing map state surfaces as `LOAD_HASH_DRIFT reason=map_mismatch` → `SYNCTEST_FAIL` (1043/1163/1399/1527) → hard `restoring live world and stopping VS session` (tick 1484). See `netplay-trimmed.log`.

**Root cause:** `sys/interp.c` was **not** in the `-ffp-contract=off` matcher. The Arwing flight path drives `map_dobjs[0]->translate` through `syInterpQuad`/`syInterpCubic`; their spline evaluators (`syInterpCatromCubicSpline`, `syInterp*Bezier*`, `syInterpQuadSpline`, `syInterpGetCubicIntegralApprox`) are dense `ctrl[i]*w[i]` multiply-add polynomials. With contraction on, clang fuses these to `fmadd` on aarch64 but emits two rounded ops on x86_64 → ~1-ULP split in the raw translate. That straddles the 1/65536 grid midpoint, so the **already-present** `grSectorArwingCanonicalizeSimState` quantize (`grsector.c`, runs every forward + resim tick) snaps the two peers to *different* cells. Quantization could not fix it because the divergence is upstream of the grid; the TU had to be made bit-deterministic. `sys/objanim.c` (AObjEvent16 joint anim, what fighters use) was already matched — the spline TU used by stage motion was the gap, which is why only the map hash split.

**Fix:** add `decomp/src/sys/interp\.c$` to the `SSB64_NETMENU` contraction matcher in `CMakeLists.txt`. No source change. NETMENU=OFF unaffected (the matcher is inside `if (SSB64_NETMENU)`). Verified: `interp.c.o_OPTIONS = -ffp-contract=off` in `ssb64_game` flags; full netmenu link clean.

**Soak:** Sector Z VS, Android↔Linux, Ness on the Arwing deck — jump on/off repeatedly ≥60s; expect no cross-peer `mph` split and no `sector_arwing_deck` `map_mismatch` abort once the gate disengages. If it persists, next suspect is the snapshot save-vs-verify ground-fold round-trip (slot≠live within a peer) per [netrollback_map_hash_parity_2026-06-03.md](netrollback_map_hash_parity_2026-06-03.md).

## Related

- [netplay_ness_pkthunder_jibaku_quantize_2026-06-01.md](netplay_ness_pkthunder_jibaku_quantize_2026-06-01.md) — prior quantize/canonicalize (insufficient without deterministic libm)
- [netplay_cross_isa_determinism_2026-05-27.md](netplay_cross_isa_determinism_2026-05-27.md) — quantize grid + initial matcher
- [netplay_thrown_item_world_pose_fma_2026-05-30.md](netplay_thrown_item_world_pose_fma_2026-05-30.md) — Class B matcher gap (gm/)
