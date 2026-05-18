# Building BattleShip

BattleShip uses CMake plus the `libultraship` and `torch` submodules.

The current asset pipeline is Torch-based:
- game assets are extracted into `BattleShip.o2r`
- port-only assets are packed into `ssb64.o2r`
- generated build inputs are handled by `ExtractAssetHeaders`
- checked-in reloc YAMLs under `yamls/us/reloc_*.yml` are treated as source inputs, not normal build outputs

## Prerequisites

Common requirements:
- Git
- CMake
- Python 3
- clone the repo with submodules, or run:

```bash
git submodule update --init --recursive
```

Asset extraction requirements:
- a legal Super Smash Bros. 64 ROM, placed at the repo root, matching the
  build's ROM version (see below):
  - **US** (default): `baserom.us.{z64,n64,v64}` — NTSC-U v1.0
  - **JP**: `baserom.jp.{z64,n64,v64}` — Nintendo All-Star! Dairantou
    Smash Brothers (NALJ)

## ROM Version (US / JP)

The decomp game code is region-conditionally compiled, so US and JP are
**separate builds** — select the version at configure time with the
`SSB64_VERSION` cache variable and **use a separate build directory per
version**:

```bash
# US (default — identical to omitting the flag)
cmake -S . -B build-us -GNinja -DSSB64_VERSION=us

# JP
cmake -S . -B build-jp -GNinja -DSSB64_VERSION=jp
```

`SSB64_VERSION` accepts `us` (default) or `jp`. It drives the region
defines, the `yamls/<ver>/` extraction configs, `baserom.<ver>.*`
selection, the per-version generated reloc artifacts, credits text, and
the JP-only source exclusions. Asset extraction writes the o2r into the
**build directory** (not the source root), so US and JP builds never
clobber each other — run the binary from its own build dir (the runtime
loads `BattleShip.o2r` relative to the working directory):

```bash
cmake --build build-jp --target ssb64 -j4
cmake --build build-jp --target ExtractAssets -j4
cd build-jp && ./BattleShip
```

You must supply the ROM matching the selected version
(`baserom.us.*` for a US build, `baserom.jp.*` for a JP build).

## Windows

Requires:
- Visual Studio 2022 or newer with Desktop development with C++
- a Windows SDK

Configure:

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' -S . -B "build/x64" -A x64
```

CMake auto-detects the newest installed Visual Studio and its default toolset. If you need to pin a specific generator/toolset (e.g. on a CI runner with multiple installs), pass `-G "Visual Studio 17 2022" -T v143` explicitly.

Build:

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' --build .\build\x64
```

Build Release:

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' --build .\build\x64 --config Release
```

Run:
- Debug: `.\build\x64\Debug\BattleShip.exe`
- Release: `.\build\x64\Release\BattleShip.exe`

## Linux / macOS

BattleShip currently expects a Ninja-style single-config build on these platforms.

Configure:

```bash
cmake -S . -B build-cmake -GNinja
```

Build:

```bash
cmake --build build-cmake
```

Run:

```bash
./build-cmake/BattleShip
```

Build Release:

Ninja is single-config, so the build type is fixed at configure time. Use a separate build directory:

```bash
cmake -S . -B build-release -GNinja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

Run: `./build-release/BattleShip`

## Asset Targets

Generate generated headers / codegen inputs:

Windows:

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' --build .\build\x64 --target ExtractAssetHeaders
```

Linux / macOS:

```bash
cmake --build build-cmake --target ExtractAssetHeaders
```

Regenerate the checked-in reloc YAMLs explicitly:

Windows:

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' --build .\build\x64 --target RegenerateRelocYamls
```

Linux / macOS:

```bash
cmake --build build-cmake --target RegenerateRelocYamls
```

Generate `BattleShip.o2r` from the ROM:

Windows:

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' --build .\build\x64 --target GenerateBattleShipO2R
```

Linux / macOS:

```bash
cmake --build build-cmake --target GenerateBattleShipO2R
```

Run the full extraction target directly:

Windows:

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' --build .\build\x64 --target ExtractAssets
```

Linux / macOS:

```bash
cmake --build build-cmake --target ExtractAssets
```

Pack port-only assets into `ssb64.o2r`:

Windows:

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' --build .\build\x64 --target GeneratePortO2R
```

Linux / macOS:

```bash
cmake --build build-cmake --target GeneratePortO2R
```

## Notes

- A normal `cmake --build` builds `BattleShip` and the standalone `torch` sidecar.
- Post-build, CMake copies these next to the executable:
  - `torch`
  - `config.yml`
  - `yamls/us`
  - `f3d.o2r`
  - `gamecontrollerdb.txt`
  - menu font files under `assets/custom/fonts`
- Runtime first-run extraction also uses the sidecar `torch` executable.
- `ExtractAssetHeaders` no longer rewrites `yamls/us/reloc_*.yml` on every build.
- `port/resource/RelocFileTable.cpp` is still generated from the checked-in reloc YAMLs.

## Unsupported In This Repo

The following Shipwright-specific flows do not apply to BattleShip:
- `GenerateSohOtr`
- `soh`, `soh_nro`, `soh.wuhb`, or other SoH target names
- `OTRExporter` / `ZAPDTR` instructions
- `supportedHashes.json`

BattleShip currently documents and supports the Torch-based workflow above.
