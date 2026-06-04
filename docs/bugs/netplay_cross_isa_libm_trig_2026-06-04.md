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

## Render vs sim trig split (2026-06-04)

Pipeline 2 MVP recalc (`lbcommon` `func_ovl0_800CA*`, `objdisplay` cases 45/46, `mnCharactersMoveFighterCamera`) must **not** use netmenu N64 `__sinf`/`__cosf` polynomial — breaks `FTOFIX32` MVP matrices (fighters / intro / stage select 3D vanish). Sim / sync TUs keep N64 poly.

| Layer | Mechanism |
|-------|-----------|
| **Sim** (`ft/`, `it/`, `wp/`, `vector.c`, …) | `__sinf` / `__cosf` → N64 polynomial when `SSB64_NETMENU=ON` |
| **Render MVP** | `SSB64_RENDER_SINF` / `SSB64_RENDER_COSF` in `sys/matrix.h` — netmenu → `syMatrixSinF`/`syMatrixCosF` (`gSYSinTable` s14→float); else `__sinf`/`__cosf` (offline wrappers) |
| **Render rotation (most 3D)** | Unchanged: `syGetSinCosUShort` / `syMatrixRotRpyR` lookup table |
| **Audio** | Bare libc `sinf()` only |

Definitions: `syMatrixSinF`/`syMatrixCosF` gated `#if PORT && SSB64_NETMENU` in `matrix.c`. Macro + callsite comments in `matrix.h`, `lbcommon.c`, `objdisplay.c`, `mncharacters.c`.

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

## Related

- [netplay_ness_pkthunder_jibaku_quantize_2026-06-01.md](netplay_ness_pkthunder_jibaku_quantize_2026-06-01.md) — prior quantize/canonicalize (insufficient without deterministic libm)
- [netplay_cross_isa_determinism_2026-05-27.md](netplay_cross_isa_determinism_2026-05-27.md) — quantize grid + initial matcher
- [netplay_thrown_item_world_pose_fma_2026-05-30.md](netplay_thrown_item_world_pose_fma_2026-05-30.md) — Class B matcher gap (gm/)
