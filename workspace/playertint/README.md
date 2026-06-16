# Player Tint — Fighter event example mod

A small, complete BattleShip mod that gives each player a distinct fighter
color: **P1 red, P2 blue, P3 green, P4 yellow**. Useful for telling players
apart, and a worked example of the **Fighter query/override event** system
documented in [`docs/modding.md`](../../docs/modding.md).

Where the template (`workspace/template`) shows a per-frame engine listener
and `workspace/hooktest` shows a function detour, this mod shows the third
extension mechanism: a **Fighter event**. It subscribes to
`FighterEnvColorQueryEvent`, which the engine fires once per fighter per frame
with `rgba` pre-filled to `0` ("take the vanilla color"). The listener writes a
non-zero RGBA word to force a color instead:

```c
static void OnEnvColorQuery(IEvent* event) {
    FighterEnvColorQueryEvent* e = (FighterEnvColorQueryEvent*)event;
    if (e->player >= 0 && e->player < 4) {
        e->rgba = kPlayerColors[e->player];   /* 0xRRGGBBAA */
    }
}
```

`FighterEnvColorQueryEvent`'s payload is all primitives (`int32 player`,
`uint32 rgba`), so unlike the other Fighter events it needs **no** decomp-struct
casts — which makes it the cleanest one to start from. With the mod unloaded the
event has zero listeners and the engine takes the vanilla path, so fighters look
exactly as they always did.

## Build

```bash
cmake -B build
cmake --build build
```

`output/` now holds the packaged mod. Drop the folder into
`<BattleShip-install>/mods/` for development, or pack it as `.o2r` for
distribution:

```bash
cmake --build build --target mod_playertint_o2r   # inside the engine tree
# or, standalone:
torch pack output playertint.o2r o2r
```

## Files

| File | Purpose |
|------|---------|
| `src/playertint.c` | The mod: registers the `FighterEnvColorQueryEvent` listener. |
| `include/mod.h` | Author API (`MOD_INIT`/`MOD_EXIT`, `mod_log`, hooks). Same as the template. |
| `manifest.json` | Mod metadata the loader reads. |
| `CMakeLists.txt` | Packaging-only build (amalgamation + copy). |

See [`docs/modding.md`](../../docs/modding.md) for the full event catalog and
the other two extension mechanisms.
