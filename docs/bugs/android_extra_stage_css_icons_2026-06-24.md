# Android Extra-Stage CSS Icons

**Status:** Fixed.

## Symptom

On Android, the three port-added stage slots (Final Destination, Metal
Cavern, and Battlefield) could appear as blank/question-mark icons on the
stage-select extra page. Desktop builds showed the icons correctly.

## Root Cause

Desktop builds run `tools/derive_stage_assets.py` against the user's local
ROM and place PNGs under:

```text
assets/css_icons/<stage>_background.png
assets/css_icons/<stage>_small.png
```

`portCSSGetStageIconSprite` loads those files from the app directory at
runtime. Android, however, extracts `BattleShip.o2r` from the user's ROM on
device through `libtorch_runner.so` and never ran the CSS PNG derivation step.
The APK also must not bundle pre-derived ROM assets, so Gradle staging would
not be a release-safe fix.

With no `<stage>_small.png` in `externalFilesDir/assets/css_icons`, the
runtime icon getter returned `NULL`, and `mnMapsMakeIcons` fell back to the
question-mark sprite for port-introduced stages.

## Fix

After Android's Torch O2R export succeeds, `port/android_torch_bridge.cpp`
now derives the same ROM-backed CSS background and small-icon PNGs from the
staged user ROM into:

```text
<externalFilesDir>/assets/css_icons/
```

That is the same directory `Ship::Context::GetPathRelativeToAppDirectory`
resolves for `portCSSGetStageIconSprite` and
`portCSSGetStageBackgroundSprite`.

The helper is Android-only, uses the staged user ROM before Java deletes it,
normalizes `.z64` / `.n64` / `.v64` byte order in memory, and keeps the APK
free of Nintendo-derived data.

## Caveat

The Android helper generates the background and small icon PNGs. Synthetic
nameplate PNGs are still absent on Android, but `mnmaps.c` already falls back
to runtime subtitle-font text for those three stages. The port-added emblem
path is intentionally blank for these stages.

## Audit Hook

Any future port-only CSS asset should answer two questions separately:

1. How desktop derives or stages the file.
2. How Android derives it from the user's picked ROM without bundling the
   derived bytes in the APK.

## Follow-up: on-device verification (2026-06-29)

The wiring above was correct, but the feature had **never been run on a
device** — and it did not actually work. End-to-end testing on an `arm64-v8a`
emulator (clean install → `dev_rom` extraction) showed:

```
CSS stage assets: VPK0 decode failed for reloc file 96
CSS stage assets: VPK0 decode failed for reloc file 98
CSS stage assets: VPK0 decode failed for reloc file 97
CSS stage assets: derived 0/3 stage asset sets
```

### Root cause

`port/android_torch_bridge.cpp` hardcoded the reloc **data region** start as
`kRelocDataStart = 0x1AEAA0`. The correct value for US v1.0 (NALE) is
`0x1B2C6C`: the data region begins immediately after the reloc table, which
contains `(fileCount + 1)` entries including a trailing sentinel:

```
dataStart = tableRomAddr + (fileCount + 1) * entrySize
          = 0x1AC870 + (2132 + 1) * 12 = 0x1B2C6C
```

This matches torch's `SSB64::GetRelocLayout` and the desktop
`tools/derive_stage_assets.py` (`RELOC_DATA_START = 0x1B2C6C`). With the wrong
base, `file_data` pointed `0x41CC` bytes short of the real VPK0 stream, so
every compressed stage file failed to decode and `0/3` icons were produced —
i.e. the icons would still have been question marks on Android.

### Fix

Derive `kRelocDataStart` from the file count instead of hardcoding it (so it
can't silently drift from torch's layout again). After the fix, the same
emulator run reports `derived 3/3 stage asset sets`, and all six PNGs
(`{final_destination,metal_cavern,battlefield}_{background,small}.png`) are
**pixel-identical** to the desktop `derive_stage_assets.py` output (verified
with `ImageChops.difference`; byte sizes differ only because lodepng and PIL
encode the same pixels differently).

### Audit hook

Any reloc-table offsets duplicated outside torch (`tableRomAddr`, `dataStart`,
`entrySize`, `fileCount`) must match `SSB64::GetRelocLayout`. Prefer deriving
`dataStart` from `tableRomAddr + (fileCount + 1) * entrySize` over a hardcoded
constant. A wrong `dataStart` reads as "VPK0 decode failed" for every
compressed file, not as a crash.
