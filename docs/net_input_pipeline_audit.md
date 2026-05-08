# Net input pipeline audit (SSB64 PC port)

This document implements the review described in the net-input audit plan: entry points, SDL vs native Raphnet convergence, VS battle wiring, integer tick delay semantics, and hardening recommendations.

---

## 1. Scene / entry-point matrix (`port/net` overlay)

| Scene module | Controller `FuncRead` | Net ingress + latch + resolve/publish | Classification |
|--------------|-------------------------|----------------------------------------|------------------|
| [port/net/sc/sccommon/scvsbattle.c](port/net/sc/sccommon/scvsbattle.c) | **`syNetInputFuncRead`** | Yes — pump → sample → latch → neutralize → resolve → publish | **Authoritative online VS battle path** |
| [port/net/sys/netrollback.c](port/net/sys/netrollback.c) | **`syNetInputFuncRead`** (resim loop) | Same pipeline during rollback resimulation | **Must match battle path** |
| [port/net/sc/sccommon/scautomatch.c](port/net/sc/sccommon/scautomatch.c) | `syControllerFuncRead` | No — raw `syControllerFuncRead` only | **Staging / MM_POLL — OK** while not in VS battle task (UDP may already be up) |
| [port/net/sc/sccommon/scnetmatchstaging.c](port/net/sc/sccommon/scnetmatchstaging.c) | `syControllerFuncRead` | No | **Staging — OK** |
| [port/net/menus/mnvsonline.c](port/net/menus/mnvsonline.c) | `syControllerFuncRead` | No | **Online menus — OK** (not VS sim ticks) |
| [port/net/menus/mnvsonline_maps.c](port/net/menus/mnvsonline_maps.c) | `syControllerFuncRead` | No | **Maps UI — OK** |
| [port/net/menus/mnvsmodenet.c](port/net/menus/mnvsmodenet.c) | `syControllerFuncRead` | No | **Mod net UI — OK** |
| [port/net/menus/mnvsoffline.c](port/net/menus/mnvsoffline.c) | `syControllerFuncRead` | No | **Offline menus — OK** |
| [port/net/menus/mnvsresults.c](port/net/menus/mnvsresults.c) | `syControllerFuncRead` | No | **Results — OK** (post-battle; reads `gSYControllerDevices` directly in places — see risks) |

**Finding:** Only **`scVSBattle`** (and rollback resim) use **`syNetInputFuncRead`**. All net-menu / automatch / staging scenes intentionally use **`syControllerFuncRead`** so players can navigate without sim tick/barrier semantics.

**Risk note:** If a bug ever leaves an **active UDP VS session** (`syNetPeerStartVSSession`) while the **scene still uses** `syControllerFuncRead` for gameplay-adjacent logic that affects match state, inputs would bypass netinput. No such scene was found for core VS simulation — battle always uses `syNetInputFuncRead`.

---

## 2. SDL vs Raphnet vs `osCont` — convergence before the latch

### 2.1 LUS `Controller::ReadToOSContPad`

File: [libultraship/src/libultraship/controller/controldevice/controller/Controller.cpp](libultraship/src/libultraship/controller/controldevice/controller/Controller.cpp)

- **Native Raphnet:** When `mRaphnetBindingActive` and `Poll()` succeeds, the function **returns immediately** after filling `OSContPad`. It **does not** run the SDL/keyboard mapping loop or the **simulated input lag** CVAR path (comments state lag is bypassed to avoid double-counting with USB latency).
- **SDL / keyboard / mappings:** Full `UpdatePad` pipeline over virtual buttons/sticks, including any configured lag behavior.

**Asymmetry risk:** Two peers using **different backends** (one native Raphnet, one SDL joystick) can see **different preprocessing** before packets are built, even at the same wall-clock moment. Mitigation options are listed in §6.

### 2.2 `osContGetReadData` (PORT bridge)

File: [libultraship/src/libultraship/libultra/os.cpp](libultraship/src/libultraship/libultra/os.cpp)

- All controllers ultimately feed **`Ship::Context::GetInstance()->GetControlDeck()->WriteToPad(lus_pads)`**, then copy **game-sized** fields into the decomp `OSContPad` array (guards against layout/size mismatch bugs).

There is **no second hidden writer** to game pads here; both backends converge on **ControlDeck → WriteToPad**.

### 2.3 `syControllerUpdateGlobalData`

File: [decomp/src/sys/controller.c](decomp/src/sys/controller.c) (`syControllerUpdateGlobalData`)

- **`port_enhancement_analog_remap`** runs for every port index whenever descriptors are valid (menus + battle).
- **`port_enhancement_c_stick_smash`** and **`port_enhancement_dpad_jump`** run only when **`gSCManagerBattleState->players[i].fighter_gobj != NULL`** (and index bounds). So **battle** can apply **extra** hold/tap/stick shaping vs **menus/CSS**.

**Implication:** For netplay, **published** inputs after `syNetInputPublishFrame` should be the source of truth; any code reading **`gSYControllerDevices`** during menus vs battle may see different enhancement layers **before** netinput in scenes that do not use `syNetInputFuncRead`. VS battle uses netinput publish path after latch, so enhancements applied in **`syControllerUpdateGlobalData`** still run **before** the latch copy in **`syNetInputFuncRead`** — both peers run the same C; determinism issues would come from **different battle state**, not from skipping netinput in battle.

---

## 3. VS battle path — hardware decouple, slots, `gSYControllerMain`

### 3.1 `syNetInputFuncRead` order

File: [port/net/sys/netinput.c](port/net/sys/netinput.c)

1. **`syNetPeerPumpIngressBeforeInputRead()`** (Linux UDP, when VS session active — see below)
2. **`syControllerFuncRead()`** → eventually **`syControllerUpdateGlobalData`** → **`gSYControllerDevices`**
3. **`memcpy(sSYNetInputHardwareLatch, gSYControllerDevices, ...)`**
4. **`syNetInputNeutralizeAllControllerDevices()`** — clears sim-facing globals until publish
5. Per-player **`syNetInputResolveFrame` → `syNetInputPublishFrame`**
6. **`syNetInputPublishMainController()`** — sets **`gSYControllerMain`** from **`gSYControllerDevices[syNetPeerGetLocalSimSlot()]`** when decouple is active

### 3.2 Ingress before resolve

File: [port/net/sys/netpeer.c](port/net/sys/netpeer.c) — `syNetPeerPumpIngressBeforeInputRead`

When **`syNetPeerIsVSSessionActive()`** (same flag as **`sSYNetPeerIsActive`** for this check) and not rollback-resimulating:

1. **`syNetPeerReceiveRemoteInput()`**
2. **`syNetPeerApplyPendingInputDelaySync()`** — applies host **`INPUT_DELAY_SYNC`** after clamp

This matches the contract in [port/net/sys/netinput.h](port/net/sys/netinput.h): remote ring + delay sync are applied **before** local resolve/publish.

### 3.3 Slot sources

File: [port/net/sys/netpeer.c](port/net/sys/netpeer.c) — `syNetPeerApplySimSlotInputSources`

- Local sim slot: **`nSYNetInputSourceLocal`**
- Remote human slots: **`nSYNetInputSourceRemotePredicted`** (confirmed overwrites via remote ring)

### 3.4 Local hardware index for “guest on P2”

Files: [port/net/sys/netpeer.c](port/net/sys/netpeer.c), [port/net/sys/netinput.c](port/net/sys/netinput.c)

- **`syNetPeerResolveLocalHardwareDevice(sim_player)`**: If not VS active or **sim_player ≠ local player**, returns **`sim_player`**. Otherwise returns **`syNetPeerGetPrimaryLocalHardwareDeviceIndex()`**.
- **`syNetPeerGetPrimaryLocalHardwareDeviceIndex()`**: **`SSB64_NETPLAY_LOCAL_HARDWARE`** env (0–3), else **0**.

**Finding:** Physical device index defaults to **port 0**. Guests whose **local** game port is **P2** but hardware is **SDL port 1** must set **`SSB64_NETPLAY_LOCAL_HARDWARE=1`** (or appropriate index), or **`syNetInputMakeLocalFrame`** samples the **wrong latch slot** → wrong buttons on wire → desync. This is a **P0 operational / UX** requirement for asymmetric wiring.

### 3.5 Decouple flag

**`syNetPeerIsOnlineP2PHardwareDecoupleActive()`** is true when UDP netplay is **enabled, configured, and active** ([port/net/sys/netpeer.c](port/net/sys/netpeer.c)). Non-local sim slots get **neutral local frames**; remote data must arrive via **`syNetInputSetRemoteInput`**.

---

## 4. N-tick delay strategy (existing code + recommendations)

### 4.1 Wire semantics

- Outbound INPUT packets attach **`history_frame.tick + sSYNetPeerInputDelay`** ([port/net/sys/netpeer.c](port/net/sys/netpeer.c) ~3970).
- Delay is an **unsigned integer tick count**, not milliseconds.

### 4.2 Bootstrap (automatch)

**`syNetPeerConfigureUdpForAutomatch(..., input_delay)`** ([port/net/sys/netpeer.c](port/net/sys/netpeer.c) ~3261+):

- **`sSYNetPeerInputDelayFloor`** = passed **`input_delay`**
- Initial **`sSYNetPeerInputDelay`** = same (clamped to default if absurd)
- **`sSYNetPeerInputDelayCeil`** = **12** unless **`SSB64_NETPLAY_DELAY_MAX`** overrides

### 4.3 Adaptive delay (host only)

- Enabled when **`SSB64_NETPLAY_ADAPTIVE_DELAY`** is non-zero (atoi).
- Host adjusts **`sSYNetPeerInputDelay`** from **late frames** and **rollback load-fail** deltas (`syNetPeerMaybeAdaptInputDelay`).
- Host broadcasts **`INPUT_DELAY_SYNC`** from **`syNetPeerRunAdaptiveInputDelaySimStep`** (guest applies via **`syNetPeerApplyPendingInputDelaySync`** before input read).

### 4.4 Recommendations

1. **Matchmaking / preflight:** Pass **`input_delay` floor** from measured **UDP RTT or echo** (e.g. bootstrap STUN/MM stats) so LAN matches start at **floor = 2** (or 1) instead of a WAN-safe default when unnecessary.
2. **Operations:** Document **`SSB64_NETPLAY_LOCAL_HARDWARE`**, **`SSB64_NETPLAY_ADAPTIVE_DELAY`**, **`SSB64_NETPLAY_DELAY_MAX`** for tournaments vs casual play.
3. Keep **ceil** bounded (current default 12 ticks) to cap worst-case input latency.

---

## 5. Hardening recommendations

### 5.1 Debug / CI

1. **Optional dev assert:** When **`syNetPeerIsOnlineP2PHardwareDecoupleActive()`** and VS battle scene, detect unexpected writes to **`gSYControllerDevices`** outside **`syNetInputPublishFrame`** / **`syNetInputFuncRead`** controlled sections (whitelist **`syControllerUpdateGlobalData`** once per pump). Implement behind **`PORT`** + env gate to avoid retail overhead.

2. **NetSync `pub_vs_remote mismatch`:** When **`SSB64_NETPLAY_TICK_DIAG`** / extended NetSync input diag is on, treat recurring **presence** mismatches during **`syNetPeerCheckBattleExecutionReady`** as **high severity** in CI log scraping (already logged in [port/net/sys/netpeer.c](port/net/sys/netpeer.c) ~4675).

3. **Mixed backend policy:** For competitive parity, optionally **require matching input backend** (native Raphnet vs SDL) or apply **equivalent buffering** on Raphnet when net delay is active — only if profiling shows measurable skew (§2.1).

### 5.2 Regression harness (future work)

- Deterministic replay: feed **`syNetInputSetRemoteInput`** / hardware latch with a fixed tape; compare **`syNetInputGetHistoryChecksum`** windows across two builds or machines.

---

## 6. Prioritized fix / follow-up list

| Priority | Item |
|----------|------|
| **P0** | Document + enforce **`SSB64_NETPLAY_LOCAL_HARDWARE`** for guest/off-default port wiring; consider UI surfacing when **`syNetPeerGetLocalSimSlot() != 0`** and hardware isn’t on index 0. |
| **P0** | Audit any **results / transition** code paths that read **`gSYControllerDevices`** while UDP session flags might still be ambiguous ([port/net/menus/mnvsresults.c](port/net/menus/mnvsresults.c) loops). |
| **P1** | Quantify Raphnet vs SDL **latency/shape** difference (§2.1); decide policy (warn, queue parity, or ignore). |
| **P1** | Matchmaking-supplied **`input_delay` floor** from RTT bucket (§4.4). |
| **P2** | Debug assert for stray **`gSYControllerDevices`** mutation during net VS (§5.1). |
| **P2** | Automated replay checksum regression (§5.2). |

---

## 7. References

- Sim pacing vs `HighestRemoteTick`: [docs/netplay_pacing.md](netplay_pacing.md)
- Pipeline overview: [port/net/sys/netinput.h](port/net/sys/netinput.h)
- Pump + delay sync: [port/net/sys/netpeer.c](port/net/sys/netpeer.c) (`syNetPeerPumpIngressBeforeInputRead`, `syNetPeerConfigureUdpForAutomatch`, adaptive delay)
- Battle scene hook: [port/net/sc/sccommon/scvsbattle.c](port/net/sc/sccommon/scvsbattle.c)
