# Netplay Architecture

## Purpose
`src/sys/netinput.c` and `src/sys/netinput.h` prepare the controller path for rollback netcode without adding networking or save-state rewind yet. The module owns deterministic per-player input samples for each simulation tick, records those samples in bounded history buffers, and republishes the resolved input through the existing `SYController` globals that battle code already consumes.

Companion handoff notes for future agent sessions live in `docs/netcode_agent_rules.md`.

This keeps the first netplay boundary at the controller layer:

- local hardware input still comes from `src/sys/controller.c`
- battle simulation still reads `gSYControllerDevices`
- netplay-only input policy lives in `src/sys/netinput.c`

## Current Integration
Battle scenes use `syNetInputFuncRead` as their controller callback. Non-battle scenes remain on `syControllerFuncRead`.

The update order is:

```text
taskman game tick
  -> scene controller callback
  -> syNetInputFuncRead()
  -> syControllerFuncRead()
  -> resolve one SYNetInputFrame per player for the current tick
  -> publish resolved frames into gSYControllerDevices
  -> scene_update()
  -> fighter input derivation
```

`syNetInputGetTick()` currently returns `dSYTaskmanUpdateCount`. This is a staging point for rollback; a later rollback manager can replace or wrap this with a stricter battle-local frame counter if needed.

## Canonical Input Frame
`SYNetInputFrame` is the deterministic input record used by netplay:

```c
typedef struct SYNetInputFrame
{
	u32 tick;
	u16 buttons;
	s8 stick_x;
	s8 stick_y;
	u8 source;
	ub8 is_predicted;
	ub8 is_valid;

} SYNetInputFrame;
```

The canonical frame stores held buttons and analog stick position only. `SYController.button_tap`, `button_release`, and `button_update` are derived when publishing back into the legacy controller globals.

## Input Sources
Each controller slot has a `SYNetInputSource`:

| Source | Meaning |
|--------|---------|
| `nSYNetInputSourceLocal` | Read the current local `gSYControllerDevices[player]` state after `syControllerFuncRead()` updates it. |
| `nSYNetInputSourceRemoteConfirmed` | Use a remote input sample already delivered for this tick. |
| `nSYNetInputSourceRemotePredicted` | Use confirmed remote input if present; otherwise predict from the last confirmed remote input, or neutral if none exists. |
| `nSYNetInputSourceSaved` | Use a saved input sample for replay or deterministic validation. |

Slots default to local input. This means routing a battle scene through `syNetInputFuncRead()` should preserve local controller behavior until a caller explicitly changes a slot source.

## History Buffers
`netinput.c` keeps three bounded ring buffers per player:

- `sSYNetInputHistory` — resolved and published inputs used by simulation
- `sSYNetInputRemoteHistory` — confirmed remote inputs staged by tick
- `sSYNetInputSavedHistory` — saved/replay inputs staged by tick

All buffers use `SYNETINPUT_HISTORY_LENGTH` and index by `tick % SYNETINPUT_HISTORY_LENGTH`. Reads validate both `is_valid` and exact `tick` to reject stale ring entries.

## Publishing To SYController
`syNetInputPublishFrame()` converts a canonical frame back into the existing `SYController` shape:

- `button_hold` comes from `SYNetInputFrame.buttons`
- `stick_range.x/y` come from `stick_x/stick_y`
- `button_tap` is computed from the previous published canonical buttons
- `button_release` is computed from the previous published canonical buttons
- `button_update` currently mirrors newly pressed buttons

`syNetInputPublishMainController()` mirrors player 0 into `gSYControllerMain` for systems that read the main controller global.

## Public API
`src/sys/netinput.h` exposes the current netplay input surface:

| Function | Role |
|----------|------|
| `syNetInputReset()` | Reset slot sources and all input histories. |
| `syNetInputGetTick()` | Return the current simulation tick used for input history. |
| `syNetInputSetSlotSource()` | Select local, remote, predicted, or saved input for a player slot. |
| `syNetInputGetSlotSource()` | Inspect a player slot's current source. |
| `syNetInputSetRemoteInput()` | Stage a confirmed remote input sample for a tick. |
| `syNetInputSetSavedInput()` | Stage a saved/replay input sample for a tick. |
| `syNetInputGetHistoryFrame()` | Read the resolved input actually published for a player/tick. |
| `syNetInputGetPublishedFrame()` | Read the latest published input for a player. |
| `syNetInputGetHistoryChecksum()` | Produce a lightweight checksum over resolved input history for validation. |
| `syNetInputFuncRead()` | Battle-scene controller callback that resolves and publishes current-tick inputs. |

## Validation Path
Before adding sockets or rollback state restoration, use the saved-input path to validate deterministic input replay:

1. Run a local battle with all slots set to local input.
2. Capture `sSYNetInputHistory` through `syNetInputGetHistoryFrame()`.
3. Re-stage those samples with `syNetInputSetSavedInput()`.
4. Set the relevant slots to `nSYNetInputSourceSaved`.
5. Compare `syNetInputGetHistoryChecksum()` or stronger future battle-state hashes across runs.

This verifies the input layer can reproduce the same per-tick controller stream before introducing network transport or state rewind.

## Non-Goals
This module does not yet implement:

- network sockets or packet formats
- matchmaking, lobby, or peer/session ownership
- game-state snapshot/restore
- framebuffer rollback
- full determinism hashing of fighter/world state

Those systems should build on top of this input boundary rather than bypassing it.
