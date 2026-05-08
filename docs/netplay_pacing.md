# Netplay sim pacing (skew vs `HighestRemoteTick`)

## Purpose

Reduce **scheduling-induced desync** when one peer’s OS stalls, the UDP stack delivers a burst of packets, or display versus simulation cadence diverges. This layer **does not replace rollback**; it limits how far **local sim tick** (`syNetInputGetTick`) can run ahead of the highest tick label seen on inbound INPUT bundles (`sSYNetPeerHighestRemoteTick`, exposed as `syNetPeerGetHighestRemoteTick()`).

Rollback still resolves prediction mismatches; pacing reduces how often **tick** and **ingress timelines** diverge before rollback must correct.

For how **sim tick** relates to taskman / host frames and why skew holds interact with `gcRunAll`, see [`netplay_taskman_simtick.md`](netplay_taskman_simtick.md).

## Terms

| Symbol / name | Meaning |
|----------------|---------|
| **tick** | VS-local monotonic index from `syNetInputGetTick()`; advanced once per completed `scVSBattleFuncUpdate` (`syNetInputAdvanceAuthoritativeSimTick`) when execution is ready. |
| **hr** / `HighestRemoteTick` | Maximum `frame.tick` observed while staging remote INPUT (`syNetPeerStagePacketBundle`). |
| **skew** | `(s32)tick - (s32)hr` (same as `tick_minus_hr` in NetSync tick_diag lines). |
| **Positive skew** | Local tick is **ahead** of the latest remote tick label — often means local sim has advanced without remote labels keeping up (ingress delay, scheduling). |
| **Negative skew** | Local tick is **behind** `hr` — remote packets reference ticks you have not reached yet; usually you want to **catch up**, not hold local tick. |

## Skew hold (lead cap)

If **skew** (`tick - HighestRemoteTick`) exceeds the **lead cap**, `syNetInputFuncRead` **suppresses** the following **`scVSBattleFuncUpdate`** (taskman runs **`scVSBattleFuncUpdateSkewPacingNetSlice`** instead). Hardware is still sampled and frames are **republished** for the same tick (same pattern as `SSB64_NETPLAY_STALL_UNTIL_REMOTE`). **`syNetInputAdvanceAuthoritativeSimTick`** does not run, so **`syNetInputGetTick()`** stays unchanged. Ingress continues to be drained at the start of `syNetInputFuncRead` via `syNetPeerPumpIngressBeforeInputRead`, so `hr` can catch up.

| Setting | Meaning |
|---------|---------|
| **`SSB64_NETPLAY_SKEW_LEAD_MAX_TICKS`** | Max allowed **positive** skew before holding tick advance. **`0`** disables skew pacing. **Unset** uses compile-time default (**4** — `SYNETPEER_SKEW_PACING_LEAD_MAX_TICKS_DEFAULT` in [`port/net/sys/netpeer.c`](../port/net/sys/netpeer.c)). Parsed once per `syNetPeerStartVSSession` (including idempotent VS activation). Values above **10000** are clamped. |

**Optional debug logging:** `SSB64_NETPLAY_PACING_LOG=1` enables rate-limited lines when a hold occurs (logs effective **`lead_max`**).

**Lag side (`skew << 0`)**

Large negative skew is **not** handled by holding local tick (that would increase backlog). Mitigation remains **ingress pumps** (`PortPushFrame` host-frame gate pump, `syNetPeerUpdateBattleGate`) and rollback. A future revision could add **catch-up** policy (multiple sim steps per wall frame) under a separate flag; this document describes lead-only behavior.

**Experimental catch-up behind (`skew >> 0` in the sense `hr` ahead of local tick)**

| **`SSB64_NETPLAY_SKEW_BEHIND_MAX_TICKS`** | When **`> 0`**: if **`HighestRemoteTick - local_sim_tick >= N`**, netinput runs an **extra** `syNetPeerUpdateBattleGate()` before `SSB64_NETPLAY_STALL_UNTIL_REMOTE`, and **bypasses strict stall** for that `syNetInputFuncRead` pass (publish may proceed without remote ring rows for the current tick). **`0`** or unset = off. Clamped **0…10000**. Parsed on each `syNetPeerStartVSSession` (same cadence as lead cap). |
| **`SSB64_NETPLAY_SKEW_BEHIND_LOG`** | **`1`**: rate-limited `catch_up_behind` lines when pump/relax triggers. |

This is **orthogonal** to **`SSB64_NETPLAY_SKEW_LEAD_MAX_TICKS`** (lead cap when local tick is **ahead** of `hr`). Tune them independently for experiments; bake defaults in later once stable.

## Strict authoritative input contract (debug baseline)

| **`SSB64_NETPLAY_STRICT_INPUT_CONTRACT`** | **`1`** (Linux UDP VS): **Mode 1 — strict confirmed inputs.** Authoritative sim steps only after `syNetInputFuncRead` has confirmed remote ring data for every remote human slot at the **wire-aligned** row: `RemoteHistory[slot][sim_tick + committed_input_delay]` (same tick label as staged INPUT from `syNetPeerGatherHistoryBundle`; `sim_tick` is `syNetInputGetTick()`). Admission uses **only** that check (not exec sync, skew, catch-up-behind, or `STALL_UNTIL_REMOTE`). Skew pacing is disabled; `syNetInputResolveFrame` does **not** predict missing remotes (clears the frame if the gate leaks). `scVSBattleFuncUpdate`, skew net slice, and rollback snapshot/update hooks **bypass** `syNetPeerCheckBattleExecutionReady` while VS+strict. **Strict remote-miss** does **not** taskman-suppress `scene_update`; it **partial-publishes local** sim slot(s) from the HID latch into published history so **`syNetPeerGatherHistoryBundle` / INPUT send** still run, and `syNetPeerGatherHistoryBundle` falls back to **`syNetInputMakeLocalFrame`** when strict is on and published tick lags sim tick. That pass skips **`syNetInputAdvanceAuthoritativeSimTick`** + **`syNetRollbackAfterBattleUpdate`** until full publish succeeds. **`syNetPeerUpdate`** sends INPUT while strict+VS even if exec is not ready yet. **Unset / `0`**: default behavior. |

While testing this mode, disable `SSB64_NETPLAY_TICK_GRID_EXEC_GATE`, `SSB64_NETPLAY_SKEW_*`, and optional `SSB64_NETPLAY_STALL_UNTIL_REMOTE` (strict subsumes the ring-row stall for admission).

## Ordering and guards

Skew pacing is evaluated **only** when:

- `syNetPeerIsVSSessionActive()` and `syNetPeerCheckBattleExecutionReady()` (running battle path),
- `syNetRollbackIsResimulating()` is false (resimulation must not stall on pacing).

Matches the intent of other net guards (`syNetPeerUpdateBattleGate` early return during resim).

## Interaction with duplicate INPUT sends

If tick does not advance for several controller passes, `syNetPeerUpdate` may emit INPUT bundles with the **same** wire tick repeatedly. That is acceptable for UDP reliability (peer deduplicates by tick / seq as usual).

## Related environment knobs (reference)

| Variable | Role |
|----------|------|
| `SSB64_NETPLAY_SKEW_LEAD_MAX_TICKS` | Positive skew lead cap before suppressing full `scVSBattleFuncUpdate` (see Skew hold section). |
| `SSB64_NETPLAY_SKEW_BEHIND_MAX_TICKS` | When **> 0**, catch-up behind `hr` (extra gate pump + relax strict stall-until-remote for that read). See **Experimental catch-up behind** above. |
| `SSB64_NETPLAY_SKEW_BEHIND_LOG` | Log `catch_up_behind` when catch-up triggers (rate-limited). |
| `SSB64_NETPLAY_DECOUPLE_DISPLAY_SIM` | Decouple host refresh rate from sim stepping (`port/gameloop.cpp`). |
| `SSB64_NETPLAY_SIM_HZ` | Target sim Hz for taskman interval scaling (`syNetRollbackApplyPortSimPacing`). |
| `SSB64_NETPLAY_STALL_UNTIL_REMOTE` | Stall full battle update (and thus **`syNetInputAdvanceAuthoritativeSimTick`**) until remote ring has a frame at **sim_tick + committed_input_delay** (wire-aligned row; same rule as strict admission). |
| `SSB64_NETPLAY_STRICT_INPUT_CONTRACT` | **`1`**: strict ring admission + partial local publish for wire + `GatherHistoryBundle` strict fallback + exec bypass + INPUT before exec ready; remote-miss skips **full** publish and tick advance without scene suppress (see **Strict authoritative input contract**). |
| `SSB64_NETPLAY_HOSTFRAME_GATE_PUMP` | Pump `syNetPeerUpdateBattleGate` on host frames when execution not ready. |
| `SSB64_NETPLAY_SYNC_PRESENT_HOLD` | Avoid idle present during barrier/sync (`syNetPeerWantsSyncPresentHold`). |
| `SSB64_NETPLAY_BARRIER_ESCAPE_MS` / `SSB64_NETPLAY_BARRIER_REQUEUE_MS` | Wall-clock escape / automatch re-queue while barrier waiting (Linux UDP). |
| `SSB64_NETPLAY_PACING_LOG` | Rate-limited logs when skew pacing holds tick advance. |
| `SSB64_NETPLAY_DETERMINISTIC_RANDTIME` | **`1`** enables sim-tick–derived `syUtilsRandTime*` during VS battle (see [`netplay_taskman_simtick.md`](netplay_taskman_simtick.md)). Default off. |
| `SSB64_NETPLAY_ASSERT_MP_TIC` | **`1`** logs `mp_tic_diag` (`sim_tick` vs `gMPCollisionUpdateTic`) alongside NetSync validation. Default off. |
| `SSB64_NETPLAY_GC_TRAVERSAL_DIAG` | **`1`** logs `gc_traversal` (`gch`, `gobj`, `grun`, `prun`) after each periodic NetSync line. **`2`** adds `pairs=` (first 16 `L<link>:g<id>`). See [`netplay_frame_composition.md`](netplay_frame_composition.md). Default off. |

## Observability

Periodic `syNetPeerLogStats` lines include a cumulative **`skew_pace_frames`** counter (taskman iterations where skew pacing suppressed a full battle update). NetSync `tick_minus_hr` in tick_diag complements tuning **`SSB64_NETPLAY_SKEW_LEAD_MAX_TICKS`** / the default in [`port/net/sys/netpeer.c`](../port/net/sys/netpeer.c).

### Desync investigation (opt-in)

These environment variables add **high-signal, pair-diff-friendly** logs without changing simulation:

| Variable | Effect |
|----------|--------|
| `SSB64_NETPLAY_DESYNC_TRACE=1` | Each NetSync interval (~every 120 ticks): **`desync_needle`** line — per-slot CRC for **published history vs remote ring** at **`needle_tick = validation_tick - 1`** (single completed sim tick). **`desync_trace figh_transition`** when `figh` changes vs the prior sample. |
| `SSB64_NETPLAY_DESYNC_TRACE=2` | Same as `1`, plus **`desync_needle_detail`** per slot: raw tick/buttons/stick/source/pred for published vs ring at `needle_tick`. |
| `SSB64_NETPLAY_UDP_FRAME_TRACE=1` | Linux UDP: every staged INPUT packet logs **`udp_frame_trace`** with `seq`, `ack`, `cur_tick`, sender slot, and wire **`ticks=[...]`** (primary bundle + secondary when dual). |
| `SSB64_NETPLAY_FRAME_COMMIT_DIAG` | **`1`** = rate-limited admission logs (`frame_commit_diag`) on active VS FuncRead; **`2`** = every eligible read. |
| `SSB64_NETPLAY_FRAME_COMMIT_SUMMARY` | Positive **N**: every **N** sim ticks (after a publish), log cumulative **`admission_summary`** (`P`/`E`/`S`/`K` counts + percentages) without resetting. End of UDP VS session logs `tag=vs_stop` and resets counters. |
| `SSB64_NETPLAY_GC_TRAVERSAL_DIAG` | **`1`** / **`2`**: `gc_traversal` line after NetSync — `gch` fingerprint + GObj/process counts; level **2** adds `pairs=` prefix. See [`netplay_frame_composition.md`](netplay_frame_composition.md). |
| `SSB64_NETPLAY_FIGHTER_PHASE_TRACE` | Per-slot fighter Phase A/B/C hashes (`ft_phase` lines). **`≥2`** logs immediate `ctrl_hist_mismatch`. Requires **`SSB64_NETMENU`**. See [`netplay_frame_composition.md`](netplay_frame_composition.md). |
| `SSB64_NETPLAY_FIGHTER_PHASE_ASSERT` | **`≥1`**: log `ft_phase_assert` on controller vs history hash mismatch at interrupt entry. |

Rollback logs **`INPUT_MISMATCH_DETAIL`** (buttons/sticks/source/pred/valid) when an input mismatch triggers resim; the **`input mismatch`** line includes the **sim slot** index.

Session stop/start resets desync trace state so env changes apply after a new VS session.

## Manual verification notes

1. Two Linux peers, typical VS match.
2. Optional: add latency on one host (`tc netem delay`) or CPU stress; enable `SSB64_NETPLAY_TICK_DIAG=1`; compare `skew_pace_frames` and `tick_minus_hr` vs stock builds.
3. Confirm `tick_minus_hr` stays within a tighter band vs disabled pacing; confirm no holds during rollback resim (no unexpected pacing spam in logs).

## Taskman / sim drift execution plan (organized testing)

Use this sequence when chasing **scheduling skew** between peers (e.g. tick vs `HighestRemoteTick` drift, long-run input strangeness). **`pub_crc` needles can still match** while sim timelines diverge — prioritize **skew**, **taskman counters**, and **`push`** over RNG-only traces until skew is bounded.

### Phase 1 — Baseline measurement

**Goal:** See **which peer runs ahead** (`tick >> hr`) and whether **`skew_pace_frames`** explodes on one OS before symptoms.

1. Use **identical** net/sim env on **both** peers (same `SSB64_NETPLAY_DECOUPLE_DISPLAY_SIM`, `SSB64_NETPLAY_SIM_HZ`, delay, stage/characters).
2. Enable at minimum:
   - `SSB64_NETPLAY_TICK_DIAG=1` — `tick_diag` lines (`tick_minus_hr`, `push`, `tm_up`, `tm_fr`, scene).
   - `SSB64_NETPLAY_PACING_LOG=1` — rate-limited skew hold lines.
3. From periodic **`SSB64 NetPeer:`** lines, compare **`skew_pace_frames`** and **`tick_minus_hr`** over wall time.
4. Archive logs with **machine name + env** in the filename.

Optional template: [`scripts/netplay-pacing-baseline.env.example`](../scripts/netplay-pacing-baseline.env.example).

**Exit:** You can state which side leads and whether pacing holds correlate with the symptom window.

### Phase 2 — Tune skew lead cap

**Goal:** Tighten how far local **`tick`** may lead **`hr`**.

- Adjust **`SSB64_NETPLAY_SKEW_LEAD_MAX_TICKS`** (e.g. **4 → 2**) — **no rebuild required**; takes effect on next VS session start (or idempotent `syNetPeerStartVSSession`).
- **`0`** disables skew pacing entirely.

Rebuild only if you change **`SYNETPEER_SKEW_PACING_LEAD_MAX_TICKS_DEFAULT`** in source.

### Phase 3 — Align display / sim and host-frame plumbing

**Goal:** Remove extra refresh/present paths that advance wall time without aligned net/sim cadence.

Matrix tests with **the same values on both peers**:

- `SSB64_NETPLAY_DECOUPLE_DISPLAY_SIM` / `SSB64_NETPLAY_SIM_HZ`
- `SSB64_NETPLAY_HOSTFRAME_GATE_PUMP` — barrier/gate pump on host frames when execution not ready
- `SSB64_NETPLAY_SYNC_PRESENT_HOLD` — avoid idle present during barrier/sync

Re-run Phase 1 metrics; **`skew_pace_frames`** should shrink if decouple was amplifying skew.

### Phase 4 — Barrier freeze experiment

**Goal:** Test whether **pre-ready taskman spin** behaves differently per OS.

- Set **`SSB64_NETPLAY_TASKMAN_BARRIER_FREEZE=0`** on **both** peers (`syTaskmanShouldFreezeForNetBarrier` in [`port/net/sys/taskman.c`](../port/net/sys/taskman.c)).
- Repeat Phase 1.

If drift **improves**, barrier vs execution-ready timing matters; if **worse**, keep default freeze and tune elsewhere.

### Phase 5 — Strict readiness stress

**Goal:** See whether hard-stalling **full battle updates** until the remote ring has the current tick removes drift (may feel bad under loss).

- Enable **`SSB64_NETPLAY_STALL_UNTIL_REMOTE`** (see [`port/net/sys/netinput.c`](../port/net/sys/netinput.c)).

Use **short** matches first; watch for stall storms.

### Phase 6 — Structural changes (implemented)

When skew pacing **suppresses `scene_update()`**, taskman still runs **`scVSBattleFuncUpdateSkewPacingNetSlice`** ([`port/net/sc/sccommon/scvsbattle.c`](../port/net/sc/sccommon/scvsbattle.c)): **`syNetPeerUpdateBattleGate`** + **`syNetPeerUpdate`** (ingress was already pumped in **`syNetInputFuncRead`**). This omits **`ifCommonBattleUpdateInterfaceAll`** (**`gcRunAll`**), **`syNetReplayUpdate`**, and **`syNetRollbackAfterBattleUpdate`** — no sim step completed for that task iteration, so no snapshot.

**Barrier phase alignment:** When **`syNetPeerReleaseBattleBarrier`** runs on the VS battle scene, **`port_reset_vs_decouple_pacing_for_net_barrier`** ([`port/gameloop.cpp`](../port/gameloop.cpp)) runs alongside **`syTaskmanResyncCountersAfterNetBarrier`** / **`port_reset_push_frame_count_for_net_barrier`** so **`PortPushFrame`** decouple sim stepping (**`SSB64_NETPLAY_DECOUPLE_DISPLAY_SIM`**) re-latches its deadline from wall-clock “now” on the first post-go frames.

### Phase 7 — Negative skew / catch-up (future)

Large **negative** skew (`tick << hr`) is **not** fixed by lead caps; needs ingress/catch-up policy — track separately once lead-side tuning is stable.

## Code references

- Tick advance: [`port/net/sys/netinput.c`](../port/net/sys/netinput.c) (`syNetInputAdvanceAuthoritativeSimTick`, called from [`scVSBattleFuncUpdate`](../port/net/sc/sccommon/scvsbattle.c))
- Admission counters + `admission_summary`: [`port/net/sys/netinput.c`](../port/net/sys/netinput.c) (`syNetInputAdmissionBump`, `syNetInputLogAdmissionStatsSummary`); VS stop [`syNetPeerStopVSSession`](../port/net/sys/netpeer.c)
- Skew helper: [`port/net/sys/netpeer.c`](../port/net/sys/netpeer.c) (`syNetPeerShouldHoldSimTickForSkewPacing`)
- `hr` update: [`port/net/sys/netpeer.c`](../port/net/sys/netpeer.c) (`syNetPeerStagePacketBundle`)
- Display/sim decouple: [`port/gameloop.cpp`](../port/gameloop.cpp) (`PortPushFrame`)
- Taskman VS loop / barrier freeze / suppress + skew net slice: [`port/net/sys/taskman.c`](../port/net/sys/taskman.c) (`syTaskmanRunTask`, `syTaskmanCommonTaskUpdate`)
- Barrier decouple reset: [`port/net/sys/netpeer.c`](../port/net/sys/netpeer.c) (`syNetPeerReleaseBattleBarrier`), [`port/gameloop.cpp`](../port/gameloop.cpp) (`port_reset_vs_decouple_pacing_for_net_barrier`)
