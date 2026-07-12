# Netplay Architecture

## Purpose

`src/sys/netinput.c` and `src/sys/netinput.h` prepare the VS Mode controller path for rollback netcode. The module owns deterministic per-player input samples for each VS simulation tick, records those samples in bounded history buffers, and republishes the resolved input through the existing `SYController` globals that battle code already consumes.

Companion handoff notes for future agent sessions live in `docs/netcode_agent_rules.md`.

Phase-locked execution, shared frontier admission, and the fixed `sim + D` wire mapping are documented in [`docs/netplay_phase_lock.md`](netplay_phase_lock.md). Historical sim pacing vs remote tick labels (`HighestRemoteTick`) is documented in [`docs/netplay_pacing.md`](netplay_pacing.md).

Taskman vs **sim tick** (`syNetInputGetTick`) vs host push, and binding authoritative battle state to sim ticks, are documented in [`docs/netplay_taskman_simtick.md`](netplay_taskman_simtick.md) — including **simulation authority** (only the rollback frame index; not Taskman or VI as clocks), an **execution trace**, and **sim-tick phase skew** risks for cross-peer input timing.

**Frame composition** (deterministic `gcRunAll` traversal / entity lists vs peer) is documented in [`docs/netplay_frame_composition.md`](netplay_frame_composition.md), including `SSB64_NETPLAY_GC_TRAVERSAL_DIAG`.

**Rollback state contract** (GGPO-style: snapshot completeness, deterministic resim, not “input sync fixes all”) is documented in [`docs/netplay_rollback_state_contract.md`](netplay_rollback_state_contract.md).

**Canonical State Image (CSI)** — cross-peer / cross-build comparison via explicit serialization (not raw struct `memcmp`) — is documented in [`docs/netplay_canonical_state_image.md`](netplay_canonical_state_image.md).

## Debugging environment (commit / edges / pacing)

A **complete index** of `SSB64_*` netplay/netmenu environment variables lives in [`docs/netplay_environment_variables.md`](netplay_environment_variables.md).

Useful flags for the **frame-commit + desync-classification + input-edge** phase (all read in `port/net/sys/` unless noted):

| Priority | Variable | Notes |
|----------|----------|--------|
| Must | `SSB64_NETPLAY_FRAME_COMMIT_TOKEN` | Cross-peer commit tokens on the wire. **Default is on** when unset (`=1`); set `=0` to disable. |
| Must | `SSB64_NETPLAY_DESYNC_CLASSIFIER` | `0` off, `1` evidence + report on VS stop, `2` also logs when the **leading** category would change (noisy). |
| High | `SSB64_NETPLAY_INPUT_EDGE_DIAG` | `≥1`: first A-button 0→1 per sim slot + `rollback_prepare` lines (`port/net/sys/netinput.c`). |
| Testing | `SSB64_NETPLAY_FRAME_COMMIT_STARVATION` | Integer threshold (default **4**): validations without a peer commit token before INPUT starvation latch. Set **2** or **3** to surface hidden “no peer token” gaps earlier. |

**Execution / commit visibility (names differ from generic “_DEBUG” placeholders):**

- **Tick-grid execution gate (guest):** `SSB64_NETPLAY_TICK_GRID_EXEC_GATE=1` — battle sim waits on tick-grid lock when phase is RUNNING (`netpeer.c`).
- **Phase-lock commit:** `SSB64_NETPLAY_FRAME_COMMIT_DIAG` logs `wire`, `commit_gen`, and prediction window for the current admission path (`netinput.c`).
- **Admission / tick visibility:** `SSB64_NETPLAY_TICK_DIAG` (level; VS session has a floor so tick_diag is not silent). Taskman: `SSB64_NETPLAY_TASKMAN_DEBUG` (`taskman.c`).

There is **no** `SSB64_NETPLAY_EXECUTION_GATE_DEBUG` or `SSB64_NETPLAY_SKEW_DEBUG` string in this tree; use the rows above.

This keeps the first netplay boundary at the controller layer:

- local hardware input still comes from `src/sys/controller.c`
- battle simulation still reads `gSYControllerDevices`
- netplay-only input policy lives in `src/sys/netinput.c`
- debug replay file I/O lives in `src/sys/netreplay.c`
- debug UDP P2P transport and match bootstrap live in `src/sys/netpeer.c`
- narrow gameplay-state hashing for diagnostics lives in `src/sys/netsync.c`
- **`gcRunAll` traversal fingerprint** (PORT): `gcPortHashGcRunAllTraversalFingerprint` in `decomp/src/sys/objman.c`, wrapped by `syNetSyncHashGcRunAllTraversalFingerprint` in `port/net/sys/netsync.c` — see [`docs/netplay_frame_composition.md`](netplay_frame_composition.md)

## Current Integration

VS battle uses `syNetInputFuncRead` as its controller callback. 1P Game, Training Mode, Bonus 1 Practice, Bonus 2 Practice, menus, and other non-VS scenes remain on `syControllerFuncRead`.

The VS update order is:

```text
VS scene start
  -> syNetInputStartVSSession()
  -> syNetReplayStartVSSession()
  -> syNetPeerStartVSSession()
  -> bootstrap P2P sessions enable the execution gate
  -> netinput tick starts at 0

taskman game tick during VS
  -> scene controller callback (`syNetInputFuncRead`)
  -> **Active Linux UDP:** `syNetPeerUpdateBattleGate` (recv + delays + barrier + bind/exec) then execution-ready gate **before** HID latch / resolve / publish; **inactive:** `PumpIngressBeforeInputRead` only
  -> publish synchronized frames into `gSYControllerDevices` only when the phase-locked shared commit gate passes; advance netinput tick only after each full `scVSBattleFuncUpdate` (`syNetInputAdvanceAuthoritativeSimTick`)
  -> `scene_update()` (`scVSBattleFuncUpdate` or skew net slice)
  -> fighter input derivation
```

`syNetInputGetTick()` returns a VS-local tick counter reset by `syNetInputStartVSSession()`; it advances once per completed `scVSBattleFuncUpdate` (`syNetInputAdvanceAuthoritativeSimTick`), not from `syNetInputFuncRead` alone. `dSYTaskmanUpdateCount` remains part of the engine, but netplay input history is keyed by match-local time so rematches and scene transitions do not reuse stale global tick assumptions.

**GGPO-style replay:** during rollback resim (nested `syNetInputFuncRead` inside `syNetRollbackRunResim`), local slots must not re-sample HID — `syNetInputMakeLocalFrame` takes authoritative inputs from published history for that tick, analogous to `ggpo_synchronize_inputs` overwriting raw adds during rewind.

The P2P start barrier and VS execution gate are debug-only and only active when both `SSB64_NETPLAY=1` and `SSB64_NETPLAY_BOOTSTRAP=1` are set. Local VS, replay playback/recording, and manual P2P input injection without bootstrap continue advancing netinput and VS updates immediately.

**GGPO battle frame:** `syNetGgpoBattleFrameGet()` mirrors `syNetInputGetTick()` (same counter, advanced only in `syNetInputAdvanceAuthoritativeSimTick` after each full `scVSBattleFuncUpdate`). Phase-locked stalls may pump ingress without advancing; they do not reinterpret input ownership. Deterministic `syUtilsRandTime*` (when `SSB64_NETPLAY_DETERMINISTIC_RANDTIME=1`) mixes that sim tick into RandTime, not wall time. Rollback resim rewinds via `syNetInputSetTick` each replayed tick (which keeps the frame counter aligned).

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
| ------ | ------- |
| `nSYNetInputSourceLocal` | Read the current local `gSYControllerDevices[player]` state after `syControllerFuncRead()` updates it. |
| `nSYNetInputSourceRemoteConfirmed` | Use a remote input sample already delivered for this tick. |
| `nSYNetInputSourceRemotePredicted` | Use confirmed remote input if present; otherwise predict from the last confirmed remote input, or neutral if none exists. |
| `nSYNetInputSourceSaved` | Use a saved input sample for replay or deterministic validation. |

Slots default to local input. This means routing VS battle through `syNetInputFuncRead()` should preserve local controller behavior until a caller explicitly changes a VS slot source.

## History Buffers

`netinput.c` keeps three bounded ring buffers per player:

- `sSYNetInputHistory` — resolved and published inputs used by simulation
- `sSYNetInputRemoteHistory` — confirmed remote inputs staged by tick
- `sSYNetInputSavedHistory` — saved/replay inputs staged by tick

All buffers use `SYNETINPUT_HISTORY_LENGTH` and index by `tick % SYNETINPUT_HISTORY_LENGTH`. Reads validate both `is_valid` and exact `tick` to reject stale ring entries.

For full-match debug replay files, `netinput.c` also keeps a separate replay frame stream capped by `SYNETINPUT_REPLAY_MAX_FRAMES`. This avoids treating the 720-frame rollback ring as permanent replay storage.

## Publishing To SYController

`syNetInputPublishFrame()` converts a canonical frame back into the existing `SYController` shape:

- `button_hold` comes from `SYNetInputFrame.buttons`
- `stick_range.x/y` come from `stick_x/stick_y`
- `button_tap` is computed from the previous published canonical buttons
- `button_release` is computed from the previous published canonical buttons
- `button_update` currently mirrors newly pressed buttons

`syNetInputPublishMainController()` mirrors player 0 into `gSYControllerMain` for systems that read the main controller global.

## Public API

`src/sys/netinput.h` exposes the current VS netplay input surface:

| Function | Role |
| -------- | ---- |
| `syNetInputReset()` | Reset slot sources and all input histories. |
| `syNetInputStartVSSession()` | Reset netinput state for a new VS match and default slots to local input. |
| `syNetInputGetTick()` | Return the current VS-local simulation tick used for input history. |
| `syNetInputSetTick()` | Set the VS-local tick, reserved for future rollback/replay control. |
| `syNetInputSetSlotSource()` | Select local, remote, predicted, or saved input for a player slot. |
| `syNetInputGetSlotSource()` | Inspect a player slot's current source. |
| `syNetInputSetRemoteInput()` | Stage a confirmed remote input sample for a tick. |
| `syNetInputSetSavedInput()` | Stage a saved/replay input sample for a tick. |
| `syNetInputGetHistoryFrame()` | Read the resolved input actually published for a player/tick. |
| `syNetInputGetPublishedFrame()` | Read the latest published input for a player. |
| `syNetInputGetHistoryChecksum()` | Produce a lightweight checksum over resolved input history for validation. |
| `syNetInputGetHistoryInputChecksum()` | Produce a source-independent checksum over published buttons/sticks for replay validation. |
| `syNetInputGetHistoryInputValueChecksumForPlayer()` | Source-independent checksum for one player across a contiguous tick span in `sSYNetInputHistory`. |
| `syNetInputGetHistoryInputValueChecksumWindow()` | Per-player checksums plus a folded combined checksum for a tick window. |
| `syNetInputSetRecordingEnabled()` | Enable or disable recording of resolved VS input frames. |
| `syNetInputGetRecordingEnabled()` | Inspect whether resolved VS input recording is enabled. |
| `syNetInputGetRecordedFrameCount()` | Read the number of VS ticks recorded since recording was enabled. |
| `syNetInputClearReplayFrames()` | Clear the full-match replay frame stream. |
| `syNetInputSetReplayFrame()` | Store one replay frame for a player and tick. |
| `syNetInputGetReplayFrame()` | Read one replay frame for a player and tick. |
| `syNetInputGetReplayInputChecksum()` | Produce a source-independent checksum over the full-match replay stream. |
| `syNetInputSetReplayMetadata()` | Stage metadata needed by future saved VS replay files. |
| `syNetInputGetReplayMetadata()` | Read staged VS replay metadata if available. |
| `syNetInputFuncRead()` | VS controller callback that resolves and publishes current-tick inputs. |

`src/sys/netreplay.h` exposes the debug runner surface:

| Function | Role |
| -------- | ---- |
| `syNetReplayInitDebugEnv()` | Read debug replay environment variables at port startup. |
| `syNetReplayStartVSSession()` | Configure record or playback state after a clean VS netinput session starts. |
| `syNetReplayUpdate()` | Write a record file at the frame limit or verify playback once enough frames have run. |
| `syNetReplayWriteDebugFile()` | Write explicit replay metadata and input frames to disk. |
| `syNetReplayLoadDebugFile()` | Load a debug replay file for playback. |

`src/sys/netpeer.h` exposes the debug UDP P2P surface:

| Function | Role |
| -------- | ---- |
| `syNetPeerInitDebugEnv()` | Read debug netplay environment variables and run optional match metadata bootstrap. |
| `syNetPeerStartVSSession()` | Open/reuse the UDP socket for VS, configure local/remote slot ownership, and enable the optional in-battle start barrier. |
| `syNetPeerCheckBattleExecutionReady()` | Return whether VS battle simulation/presentation may advance. Non-netplay and non-bootstrap sessions return true. |
| `syNetPeerCheckStartBarrierReleased()` | Same as `syNetPeerCheckBattleExecutionReady()` — when false, `syNetInputFuncRead` returns before resolve/publish for live ticks. |
| `syNetPeerUpdateBattleGate()` | Receive control packets and drive `BATTLE_READY` / `BATTLE_START` while VS execution is held. |
| `syNetPeerUpdate()` | Receive packets, drive the start barrier, send local input frames, and log runtime stats. |
| `syNetPeerStopVSSession()` | Close the debug UDP socket and log the session summary. |

## VS Replay Direction

Saved VS replays are deterministic engine re-runs, not video captures. The current debug replay file includes:

- magic and version
- metadata size, frame size, frame count, player count, and input checksum
- VS scene/mode identifier
- initial battle settings and player slot setup
- stage, stocks/time/rules, item settings, characters, costumes, teams, handicaps, CPU levels, and match-start RNG seed
- per-player `SYNetInputFrame` stream
- source-independent input checksum for record/playback comparison

Playback resets the VS netinput session, loads metadata, stages full-match replay inputs, sets replayed slots to `nSYNetInputSourceSaved`, restores the match-start RNG seed, and runs the normal game engine.

**Diagnostic playback** (`SSB64_REPLAY_DIAGNOSTIC=1`, netmenu): same saved-input path, but also starts a local rollback session (no UDP). Snapshot ring + synctest + quantize/hardening run as in live netplay. Optional `SSB64_REPLAY_DIAGNOSTIC_RESIM_TICK` forces one solo load→resim. Users still share ordinary `.ssb64r` files; you enable diagnostics at playback time.

The debug runner is controlled with environment variables:

```sh
cd build
SSB64_REPLAY_RECORD=/tmp/test.ssb64r SSB64_REPLAY_RECORD_FRAMES=43200 ./BattleShip
SSB64_REPLAY_PLAY=/tmp/test.ssb64r ./BattleShip
```

`SYNETINPUT_REPLAY_MAX_FRAMES` / default record ceiling is **43200** (~12 minutes @ 60 Hz). Automatch auto-save finalizes via `syNetReplayFinishVSSession` on **match end**, **VS session stop** (`syNetPeerStopVSSession`), or clean **`PortShutdown`**. Mid-match **checkpoints** (default every 300 frames / ~5 s; `SSB64_REPLAY_CHECKPOINT_FRAMES`, `0` to disable) rewrite the same path atomically (`.part` then rename) so a hard crash can still leave a playable partial file. `SSB64_REPLAY_RECORD_FRAMES` is optional and only forces an early finalize when set below the max (debug short captures). Record mode still uses the normal VS menus; playback mode loads the file and jumps directly into VS battle using the saved metadata.

## Debug P2P Netplay

`src/sys/netpeer.c` adds a UDP-only debug transport for manual P2P testing. It is not a final lobby or NAT traversal layer; it exists to prove that two local game instances can share match setup and exchange per-tick input frames.

Current debug environment variables:

- `SSB64_NETPLAY=1` enables the UDP P2P module.
- `SSB64_NETPLAY_LOCAL_PLAYER` selects the local slot index.
- `SSB64_NETPLAY_REMOTE_PLAYER` selects the remote slot index.
- `SSB64_NETPLAY_BIND` is the local IPv4 bind address in `host:port` form.
- `SSB64_NETPLAY_PEER` is the remote IPv4 address in `host:port` form.
- `SSB64_NETPLAY_DELAY` offsets sent input ticks by a fixed frame delay. It defaults to 2.
- `SSB64_NETPLAY_SESSION` optionally overrides the packet session id. It defaults to 1.
- `SSB64_NETPLAY_BOOTSTRAP=1` enables automatic VS match bootstrap.
- `SSB64_NETPLAY_HOST=1` marks the peer that creates and sends match metadata.
- `SSB64_NETPLAY_SEED` optionally overrides the bootstrap match RNG seed.

Packet phases:

- `INPUT` packets carry recent local `SYNetInputFrame` samples, tagged with delayed VS-local ticks and staged into the remote confirmed input history.
- `MATCH_CONFIG` packets carry explicit `SYNetInputReplayMetadata` so both peers enter the same VS battle with the same stage, rules, players, characters, and RNG seed.
- Bootstrap `READY` / `START` packets complete the pre-VS metadata handshake.
- In-battle `BATTLE_READY` / `BATTLE_START` packets align the VS-local netinput tick before runtime input packets begin.

Match metadata sync, input tick start sync, and VS execution sync are separate layers:

- Metadata bootstrap makes both peers enter the same battle state.
- The input tick barrier keeps `syNetInputGetTick()` at 0 until both peers have reached VS and exchanged readiness.
- The VS execution gate keeps `scVSBattleFuncUpdate()` from advancing battle/interface presentation while the bootstrap barrier is still waiting.

The execution gate is intentionally shaped as a reusable readiness query. Future runtime pacing, peer advertised ticks, and rollback readiness checks should build on this boundary instead of adding more one-off checks to the VS scene.

Bootstrap P2P input packets (`INPUT`, wire version `SYNETPEER_VERSION` 2):

- Carry a strictly increasing UDP send `packet_seq`, included in the packet checksum and surfaced as cumulative `gap` / `dup` / `ooo` counters when sequence jumps repeat, advance with holes, or arrive behind the observed high watermark.
- Bundle the last 16 simulated local frames (`SYNETPEER_MAX_PACKET_FRAMES`), each with delayed tick + buttons/stick, serialized explicitly (not raw struct casts).
- Still embed `ack_tick`: the sender’s tracked `sSYNetPeerHighestRemoteTick` advertised to the peer (see `peer_ack=` / `puck=` lines in logs).

Operational desync instrumentation (logged only when UDP netplay is active and bootstrap execution is released):

| Log prefix | Approx. cadence | Use |
| ---------- | ----------------- | --- |
| `SSB64 NetPeer:` | Every 120 sim ticks (`SYNETPEER_LOG_INTERVAL`) | Transport counters, sequence diagnostics, staged frames, cumulative remote-input fingerprint (`inpchk`). |
| `SSB64 NetSync:` | Same ticks as NetPeer summaries | **`hist_win=[begin,end)`** — half-open `[begin,end)` VS tick range hashed from **resolved published history only** (`sSYNetInputHistory`) using the same logical fields as replay validation (player id + tick + buttons + sticks). **`all`** / **`p0..p3`** show combined and per-slot checksums. **`figh`** is `syNetSyncHashBattleFighters()` over active fighter `FTStruct` scalars plus selected velocities and `coll_data.pos_prev` using IEEE754 bit reinterpretation — order-independent across controller ports. **`pko`/`pkn`** are oldest/newest frame ticks bundled in the most recent validated remote `INPUT` packet (or sentinel `4294967295` when **`pkt_valid=0`**). **`gap`/`dup`/`ooo`** track inferred sequence anomalies. |

Debug workflow:

1. After metadata bootstrap and execution gate, confirm **`SSB64 NetSync`** lines appear on matching ticks across host/client logs.
2. If **`hist_win`** checksum columns diverge first, prioritize packet redundancy, deserialization, staging order, tick assignment, or prediction quirks before rewriting rollback.
3. If input windows match while **`figh`** diverges, widen or narrow deterministic gameplay hashes before blaming UDP pacing.
4. If both stay aligned but observers still perceive drift, escalate to pacing / telemetry using the same instrumentation hooks.

`cmake` discovers `src/sys/*.c` through `file(GLOB_RECURSE …)`; adding a netplay sys source requires re-running CMake configuration (for example `cmake -B build`) so the glob refreshes before the next build.

## Validation Path

Before adding sockets or rollback state restoration, use the saved-input path to validate deterministic VS input replay:

1. Run a local battle with all slots set to local input.
2. Capture `sSYNetInputHistory` through `syNetInputGetHistoryFrame()`.
3. Re-stage those samples with `syNetInputSetSavedInput()`.
4. Set the relevant slots to `nSYNetInputSourceSaved`.
5. Compare `syNetInputGetHistoryInputChecksum()` against the replay file checksum.

This verifies the input layer can reproduce the same per-tick controller stream before introducing rollback state rewind.

## Non-Goals

This module does not yet implement:

- matchmaking, lobby, ELO, region, or ping selection
- STUN/TURN, NAT traversal, or relay fallback
- game-state snapshot/restore
- framebuffer rollback
- exhaustive determinism hashing of fighter/world state (only narrow diagnostic hashes ship today via `netsync.c`)
- wall-clock input scheduling, rollback prediction windows, or resimulation
- netplay support for 1P Game, Training Mode, Bonus 1 Practice, or Bonus 2 Practice

Those systems should build on top of this input boundary rather than bypassing it.
