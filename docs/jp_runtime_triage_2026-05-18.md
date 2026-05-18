# JP runtime triage — automated test session 2026-05-18

Build: `cmake -DSSB64_VERSION=jp` (decomp `caae085e1`, libultraship
`d6587f8`), `baserom.jp.z64`. Ran `/tmp/cfg-jp/BattleShip` under `xvfb-run`
(env has `DISPLAY=:0`; xvfb used for determinism). o2r = complete JP
archive (2134 entries: 2107 reloc + 8 audio + 18 particles + version).

## Works ✅

- Boots. All audio assets load ("all audio assets loaded successfully").
- **SFX works** (user-confirmed interactive).
- Reaches the render loop / attract; runs ~70 s before the texture
  breakage starts.

## Issue A — particle/texture breakage → bad DL → stale-DL crash (task #7)

Symptom: sustained `GfxDpLoadBlock: missing texture image for tile 7` +
`ImportTexture: null texture address for tile 0 (both TMEM slots empty)`,
then (headless run, ~frame 4555) the watchdog's `GFX STALE-DL DIAG`
fires — diag classifies the bad command as `scene_arena+0x…` with a long
run of `seg[0xe] … (MovewordF3dex2/G_MW_SEGMENT)` writes. Interactive run
crashed (SIGSEGV) at the same point. This is the **known stale-DL crash
family** (stability-fixes PR #182), per user.

**Not a JP data/extraction bug.** All 18 JP particle bank sizes match US
byte-for-byte (`particles_unk*`, `efcommon`, `itmanager`, `grpupupu`,
`grhyrule`, `gryoster`, `mntitle` scb/txb); offsets come from the
authoritative `decomp origin/main:smashbrothers.jp.yaml` splat segment
list. Particle data is region-identical and correctly extracted.

**Decomp/LUS fixes ARE present.** `caae085e1` contains the stale-DL
structural fixes `b7d9291d2` / `f6d2e1965`; US is stable with them. So
JP triggers a variant/trigger the US-developed guards don't cover, on a
region-conditional path that feeds garbage into segment `0x0E` / the
scene-arena DL. This is genuine port-side debugging on the JP scene /
particle DL setup — likely a `#if REGION_*` path or a JP scene-arena
layout difference, not the YAMLs.

Next: capture which scene/effect is active when the first
`missing texture image` fires (the headless first occurrence is ~70 s
in, well past title); bisect against the stability-fixes variant list;
check JP-conditional code feeding `gGCCommonDLLinks[]` / segment 0x0E /
particle bank pointers (cf. `docs/bugs/segment_0e_gdl_*`,
`dl_link_stale_pointer_guard_*`, `linux_stale_scene_data_family_*`,
`npikachu_particlebankid_lp64_overrun_*`).

## Issue B — JP music silent (task #8)

SFX works, music does not. audio_bridge log:
`parsed sounds1_ctl → sSYAudioSequenceBank2 (music): 43 instruments`,
`parsed sounds2_ctl → sSYAudioSequenceBank1 (SFX): 1 instruments` (the
"1 instruments" is implausible). JP `B1_sounds2_ctl/tbl` legitimately
differ in size from US (0xFC40 vs 0xFBA0; 0x2B05E0 vs 0x2DC1E0) — these
are correct per the JP splat boundaries (verified). So the extraction is
right; the **audio bridge's CTL parse / ctl→bank mapping is US-tuned and
mishandles the JP sound-bank-2 layout**. Investigate
`port/audio/*` (the `audio_bridge: parsed … ctl → sSYAudioSequenceBank*`
path) for region assumptions about bank ordering / instrument counts.

## Repro

```
cmake -S . -B <dir> -DSSB64_VERSION=jp -GNinja
cmake --build <dir> --target ssb64 -j4
rm -f <dir>/extracted/BattleShip.o2r   # force re-extract if yamls changed
cmake --build <dir> --target ExtractAssets -j4
cd <dir> && xvfb-run -a ./BattleShip    # or run on a real display
```

(Stale repo-root `BattleShip.o2r` moved to
`BattleShip.o2r.stale-noaudio.bak` so CWD-relative loads from repo root
don't pick up the old no-audio archive. Run from the build dir.)
