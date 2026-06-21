# Decomp patches (armeabi-v7a / ILP32)

`android-v7a-ilp32-decomp.patch` carries the 32-bit ARM (ILP32) fixes that live
inside the **decomp submodule**, not this outer repo. The dev environment that
produced them could only push to `JRickey/BattleShip`, not to the
`ssb-decomp-re` fork, so the patch is staged here and applied to the
checked-out decomp tree by `.github/workflows/android-v7a.yml` at build time.

Same diff is also committed on the decomp branch `agent/armeabi-v7a-ilp32`
(if that branch was pushed to the fork).

## What it changes

| File | Fix |
|------|-----|
| `src/sys/taskman.c` | Relax the PORT `_Static_assert(sizeof(uintptr_t)==8)` to accept 4- or 8-byte `uintptr_t`. |
| `src/libultra/n_audio/n_env.c` | Gate the `ALWhatever8009EE0C`/`...siz34` audio-struct layout asserts on `__SIZEOF_POINTER__` (LP64 offsets vs N64-native ILP32 offsets). |
| `src/lb/lbcommon.c` | `lbCommonMakePositionFGM` byte-poke offset: `0x43` on LP64, N64-native `0x2F` on ILP32. |

All changes are inside `#ifdef PORT` and gated by pointer width, so LP64/LLP64
builds and the matching ROM build are unaffected.

## Apply it (manual / local build)

```sh
git -C decomp apply tools/patches/android-v7a-ilp32-decomp.patch
```

## Land it upstream (preferred — then delete this patch)

For whoever has push access to the `ssb-decomp-re` fork:

```sh
cd decomp
git checkout -b armeabi-v7a-ilp32 <pinned-submodule-SHA>
git apply ../tools/patches/android-v7a-ilp32-decomp.patch
git commit -am "port(armeabi-v7a): ILP32 support (taskman guard + n_env/lbcommon audio layout)"
git push origin armeabi-v7a-ilp32         # then merge into port-patches

# back in the superproject, bump the submodule pointer:
cd ..
git add decomp
git commit -m "Bump decomp: armeabi-v7a ILP32 fixes"
```

Once the submodule pointer includes these changes, remove both this patch and
the "Apply decomp ILP32 patches" step in `.github/workflows/android-v7a.yml`.

See `docs/android_armeabi_v7a_2026-06-20.md` for the full write-up.
