# Modding BattleShip

BattleShip ships a native **C mod** system: at boot the engine compiles each
mod's `.c` source at runtime (via libtcc), links it against the engine's own
export table, and calls the mod's `ModInit`. Mods can subscribe to engine
events, detour any exported engine function, and play custom audio — without
recompiling the port.

This page is the **map**. It tells you what the system can do, the three ways a
mod hooks into the engine, and a complete reference of the events you can
subscribe to. For the step-by-step authoring workflow (project layout, build,
packaging to `.o2r`, hot reload), the canonical how-to is
[`workspace/template/README.md`](../workspace/template/README.md).

> **Scope:** the mod loader is **desktop-only** (Windows / Linux / macOS). It is
> compiled out on Android and any build configured with `-DDISABLE_SCRIPTING`.
> Mods are loaded from `<install>/mods/`, either as unpacked folders (handy for
> development) or `.o2r` archives (for distribution).

---

## Start here

1. Read [`workspace/template/README.md`](../workspace/template/README.md) — the
   authoring guide: folder layout, `manifest.json`, building, packaging, hot
   reload.
2. Copy `workspace/template/` to start a new mod. It builds a heartbeat mod that
   logs once per second — the smallest complete example.
3. See `workspace/hooktest/` for a minimal **function-detour** mod (it hooks
   `osSpTaskStartGo` and passes through).
4. See `workspace/playertint/` for a minimal **fighter-event** mod (it tints
   each player a distinct color via `FighterEnvColorQueryEvent`).
5. Use this page as your reference for **which events exist** and **how to read
   their payloads**.

---

## Three ways to extend the engine

Pick the mechanism that matches what you're trying to do.

### 1. Event listeners — react to engine moments

The engine fires named events at well-defined points (per-frame, per-display,
ImGui render). Subscribe from `ModInit`, unsubscribe from `ModExit`:

```c
#include "mod.h"
#include "port/hooks/Events.h"

ListenerID gListener;

void OnFrame(IEvent* event) {
    /* runs every frame, after the game tick */
}

MOD_INIT() {
    gListener = REGISTER_LISTENER(GamePostUpdateEvent, EVENT_PRIORITY_NORMAL, OnFrame);
}

MOD_EXIT() {
    UNREGISTER_LISTENER(GamePostUpdateEvent, gListener);
}
```

This is the right tool when an engine event already exists for the moment you
care about. See the [Event reference](#event-reference) below.

### 2. Function detours — replace engine behavior

Any function in the engine's export table can be detoured. Calls to it land in
your replacement; the original comes back through an out-pointer so you can call
it (chain) or skip it:

```c
typedef sb32 (*orig_fn)(GObj*);
static orig_fn s_orig = NULL;

static sb32 my_replacement(GObj* g) {
    /* before */
    sb32 rv = s_orig(g);   /* call the original (optional) */
    /* after */
    return rv;
}

MOD_INIT() {
    mod_install_hook("itTomatoFallProcUpdate", my_replacement, (void**)&s_orig);
}
```

Hooks are **owned by the installing mod** — on unload/hot-reload the engine
removes each of the mod's hooks before re-running `ModInit`, so reloading a
hook-installing mod is safe. Use this when there's a discrete engine symbol for
the behavior you want to change.

### 3. Fighter query/override events — patch mid-function decisions

Some fighter decisions happen deep inside thousand-line procs or `fkind`-keyed
macros where there's no discrete symbol to detour. For those, the engine fires a
**Fighter event** carrying a mutable payload: it fills in the vanilla default,
your listener can overwrite the out-field, and the engine reads the payload back.
With zero listeners these are no-ops and the game behaves byte-identically.

```c
void OnEnvColorQuery(IEvent* event) {
    FighterEnvColorQueryEvent* e = (FighterEnvColorQueryEvent*)event;
    if (e->player == 0) {
        e->rgba = 0xFF0000FF;   /* force player 1's env color to red */
    }
}

MOD_INIT() {
    REGISTER_LISTENER(FighterEnvColorQueryEvent, EVENT_PRIORITY_NORMAL, OnEnvColorQuery);
}
```

See the [Fighter events](#fighter-events) table for each override point's
payload and semantics.

---

## Event reference

Every event is declared with `DEFINE_EVENT`, which generates a struct of the
same name whose first member is `IEvent Event;` followed by the payload fields.
In a listener, cast the `IEvent*` you receive to the event's type and
read/write its fields:

```c
void OnX(IEvent* event) {
    SomeEvent* e = (SomeEvent*)event;
    /* read e->field, write e->out_field, or cancel with event->Cancelled = true */
}
```

Listener priority is one of `EVENT_PRIORITY_LOW`, `EVENT_PRIORITY_NORMAL`,
`EVENT_PRIORITY_HIGH` (higher runs first).

### Engine & UI events

Declared in [`port/hooks/list/EngineEvent.h`](../port/hooks/list/EngineEvent.h).
These carry **no payload** — they're pure notifications.

| Event | Fires |
|-------|-------|
| `GamePreUpdateEvent` | In `port.cpp`, **before** the per-frame game tick (`gmMain` dispatch). |
| `GamePostUpdateEvent` | In `port.cpp`, **after** the per-frame game tick. The most common hook for reading/writing game state each frame. |
| `DisplayPreUpdateEvent` | Before the GFX dispatch (RCP frame submit). |
| `DisplayPostUpdateEvent` | After the GFX dispatch. |
| `EngineRenderMenubarEvent` | During ImGui menubar render — add your own menu items here. |
| `EngineRenderModsEvent` | During ImGui main render — draw your own ImGui windows here. |

### Fighter events

Declared in [`port/hooks/list/FighterEvent.h`](../port/hooks/list/FighterEvent.h).
These are **mutable** — fill in the out-field to override, leave it to take the
vanilla path. Pointer fields are passed as `void*` to keep the header free of
decomp types; cast them to the documented decomp type inside your listener.

| Event | Fires at | Payload | Semantics |
|-------|----------|---------|-----------|
| `FighterEnvColorQueryEvent` | `ftdisplaymain.c` `ftDisplayMainProcDisplay` | `int32_t player; uint32_t rgba;` | **Query.** `rgba` in/out: `0` = no override; any other value is forced as the env color (RGBA word). |
| `FighterDamageDirectionApplyEvent` | `ftmain.c` `ftMainProcessHitCollisionStatsMain`, after default `damage_lr` is computed | `void* this_fp; void* attacker_fp; void* attack_coll;` | **Apply.** Derive the hitbox id from the attacker's `attack_colls`; overwrite `this_fp->damage_lr` (cast `this_fp` to `FTStruct*`, `attack_coll` to `FTAttackColl*`). |
| `FighterHitboxSlotResetEvent` | `ftmain.c` `ftMainParseMotionEvent` make-attack path | `int32_t player; int32_t slot;` | **Notify.** Zero your per-player/per-hitbox override slot so stale state from a prior attack with the same id doesn't bleed in. |
| `FighterParentKindResolveEvent` | `ftcommonattack1/100.c` `fkind` whitelists | `int32_t fkind; int32_t resolved_fkind;` | **Query.** `resolved_fkind` in/out: starts at `fkind`; rewrite it so a synthetic fighter passes its parent's jab / rapid-jab gates. |
| `FighterRapidJabStatusQueryEvent` | `ftcommonattack100.c` Start/Loop/End `SetStatus` helpers | `int32_t fkind; int32_t phase; int32_t status_id;` | **Query.** `status_id` in/out: `-1` = use the vanilla `fkind` switch; else the rapid-jab status id for `phase` (`0`=begin, `1`=loop, `2`=end). |

> **Whole-function override points** deliberately have **no** event — detour the
> engine symbol with `mod_install_hook` instead: `ftMainSetStatus`,
> `ftParamClearAttackCollAll`, `ftSamusSpecialNGetChargeShotPosition`.

---

## A complete example: tint a fighter red

A full mod that forces player 1's environment color to red. Drop this in
`src/` of a copied template, build, and load. A complete, packaged version
that tints **all four** players a distinct color ships in
[`workspace/playertint/`](../workspace/playertint/) — copy that folder to start
from a working fighter-event mod instead of the heartbeat template.

```c
/* tint.c — force player 1's fighter env color to red. */
#include "mod.h"
#include "port/hooks/Events.h"

static ListenerID s_listener;

static void OnEnvColorQuery(IEvent* event) {
    FighterEnvColorQueryEvent* e = (FighterEnvColorQueryEvent*)event;
    if (e->player == 0) {       /* player index is 0-based */
        e->rgba = 0xFF0000FF;   /* R=FF G=00 B=00 A=FF */
    }
}

MOD_INIT() {
    s_listener = REGISTER_LISTENER(FighterEnvColorQueryEvent,
                                   EVENT_PRIORITY_NORMAL, OnEnvColorQuery);
    mod_log("[Tint] init OK\n");
}

MOD_EXIT() {
    UNREGISTER_LISTENER(FighterEnvColorQueryEvent, s_listener);
}
```

---

## Gotchas

- **`PORT` struct layout.** The engine is built with `-DPORT`, which changes the
  layout of many decomp structs (pointer fields collapse to `u32` reloc tokens).
  `mod.h` defines `PORT 1` before anything else for you — keep that include
  first, or cross-boundary struct reads return garbage.
- **Logging.** `mod_log(fmt, ...)` writes to `%APPDATA%/BattleShip/ssb64.log`
  (unbuffered) and works in both Debug and Release. `printf`/`stderr` only
  surface in the Debug-build console on Windows; in Release they go nowhere.
  Rely on `mod_log`.
- **Hot reload.** The Mods menu's Reload action runs every mod's `ModExit`,
  removes its owned hooks, frees its compiled image, recompiles from disk, and
  re-runs `ModInit`. Unregister listeners and free allocations in `ModExit` so
  reload stays clean.
- **Build errors surface at load, not at author-build time.** `merge_mod.py`
  only preprocesses/amalgamates your sources; the real compile happens in libtcc
  when the engine loads the mod. Watch `ssb64.log` for compile errors.
- **Pillow is a packaging dependency.** `merge_mod.py` imports Pillow (PIL). If
  `cmake --build` fails with `No module named 'PIL'`, install it with
  `pip install Pillow` — into the interpreter **CMake selected**, which can
  differ from the `python` on your `PATH` (CMake's `find_package(Python3)` may
  pick another install). The build output prints the interpreter path it found.

---

## Where the API is defined

| Piece | File |
|-------|------|
| Author API (`MOD_INIT`, `mod_install_hook`, `mod_log`, `CALL_FUNC`) | `workspace/template/include/mod.h` |
| Event macros (`DEFINE_EVENT`, `REGISTER_LISTENER`, `IEvent`, priorities) | libultraship `ship/events/EventTypes.h`, `bridge/eventsbridge.h` |
| Event catalog | `port/hooks/list/EngineEvent.h`, `port/hooks/list/FighterEvent.h` |
| Runtime loader / mod mount | `port/port.cpp` (`MountModsDir`), `port/mods/` |
| Packaging helper | `cmake/ModO2R.cmake`, `tools/merge_mod.py` |
