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
