# Netplay and netmenu environment variables

Reference for debugging and for porting this netcode to other decomp projects. Values are read with `getenv` / `std::getenv` unless noted. Empty or unset usually means “default off” or “use compiled defaults”; check call sites for exact parsing (`atoi`, truthy, level numbers).

**Precedence (input delay):** When `SSB64_NETPLAY_MATCH_INPUT_DELAY` is set (0–99), it sets **both** committed **wire** input delay (`syNetPeer` / packet `sim_tick + delay`, remote ring keys) **and** **execution** delay for strict-contract probing in `netinput`, with execution capped at **4** frames. It overrides `SSB64_NETPLAY_DELAY`, `SSB64_NET_DELAY_FRAMES`, and `SSB64_NETPLAY_INPUT_EXEC_DELAY_FRAMES` for those purposes. When unset, use those per-layer variables for lab tuning.

**Linux vs Windows:** Matchmaking (`mm_*`), automatch UDP configure, and some barrier paths are Linux-oriented; Windows builds may stub or omit related sources.

**Maintenance:** Refresh this list when adding knobs:

```bash
rg 'getenv\("SSB64_' port/net port/gameloop.cpp
```

See also [docs/netplay_architecture.md](netplay_architecture.md) and [docs/netcode_agent_rules.md](netcode_agent_rules.md).

---

## Input, delay, strict contract ([`port/net/sys/netinput.c`](port/net/sys/netinput.c))

- **`SSB64_NETPLAY_MATCH_INPUT_DELAY`** — Integer 0–99. Match-mode **linked** delay: wire delay + exec delay (exec = `min(value, 4)`). Overrides separate delay envs. Read at VS session start and when netpeer reads match delay.
- **`SSB64_NET_DELAY_FRAMES`** — Execution delay frames (0–4). Ignored if match delay set.
- **`SSB64_NETPLAY_INPUT_EXEC_DELAY_FRAMES`** — Same as above, second choice after `SSB64_NET_DELAY_FRAMES`.
- **`SSB64_NETPLAY_INPUT_PREDICTION`** — `0` disables last-input prediction for strict remote misses; default on.
- **`SSB64_NETPLAY_STRICT_INPUT_CONTRACT`** — `1` enables strict authoritative input path (Linux UDP VS); cached at first query.
- **`SSB64_NETPLAY_PREDICT_NEUTRAL`** — VS session: neutral prediction policy when on.
- **`SSB64_NETPLAY_LOG_LOCAL_INPUT`** — Guest-side local input logging (rate-limited).
- **`SSB64_NETPLAY_ABORT_ON_INPUT_MISMATCH`** — Bitmask: bit 1 NetSync validation, bit 2 rollback pre-resim; logs mismatch (hard `abort` only if fatal env set).
- **`SSB64_NETPLAY_ABORT_ON_INPUT_MISMATCH_FATAL`** — Non-zero: after logging mismatch, call `abort()` (for CI / bisect).
- **`SSB64_NETPLAY_STALL_UNTIL_REMOTE`** — Stall-until-remote path; cached.
- **`SSB64_NETPLAY_INPUT_PREDICT_DIAG`** — Level 1/2 predicted-remote diagnostics.
- **`SSB64_NETPLAY_FRAME_COMMIT_DIAG`** — Admission path logging level.
- **`SSB64_NETPLAY_FRAME_COMMIT_SUMMARY`** — Frame-commit summary logging.
- **`SSB64_NETPLAY_STRICT_R_STUCK_FORCE_DIAG`** — With exec delay 0, force advance after sustained strict-R miss (diagnostic).

---

## NetPeer: debug P2P, barrier, clock, pacing ([`port/net/sys/netpeer.c`](port/net/sys/netpeer.c))

**Bootstrap / session**

- **`SSB64_NETPLAY`** — Non-zero enables debug netplay env init (`syNetPeerInitDebugEnv`).
- **`SSB64_NETPLAY_LOCAL_PLAYER`**, **`SSB64_NETPLAY_REMOTE_PLAYER`** — Sim slot indices.
- **`SSB64_NETPLAY_DELAY`** — Initial **wire** committed input delay (unless `SSB64_NETPLAY_MATCH_INPUT_DELAY` set).
- **`SSB64_NETPLAY_SESSION`** — Session id.
- **`SSB64_NETPLAY_BIND`**, **`SSB64_NETPLAY_PEER`** — IPv4 `host:port` for UDP debug.
- **`SSB64_NETPLAY_BOOTSTRAP`**, **`SSB64_NETPLAY_HOST`**, **`SSB64_NETPLAY_SEED`** — Bootstrap / host flag / seed.
- **`SSB64_NETPLAY_SYNC_START_MS`** — Sync start timing (debug init block).
- **`SSB64_NETPLAY_LOCAL_HARDWARE`** — Maps local sim slot to hardware device index (multiple sites).

**Adaptive delay**

- **`SSB64_NETPLAY_ADAPTIVE_DELAY`** — Enable adaptive delay ramp.
- **`SSB64_NETPLAY_DELAY_MAX`** — Ceiling for adaptive / floor clamping (with automatch configure).

**Automatch / Linux**

- **`SSB64_NETPLAY_AUTOMATCH_NO_ITEMS`** — Item policy for automatch offer path.
- **`SSB64_NETPLAY_UDP_LINK_SYNC`** — UDP link sync behavior.
- **`SSB64_NETPLAY_REQUIRE_INPUT_BIND`** — Require input bind.
- **`SSB64_NETPLAY_BATTLE_EXEC_SYNC`** — Battle execution sync flag.

**Clock / barrier**

- **`SSB64_NETPLAY_CLOCK_SYNC_SAMPLES`**, **`SSB64_NETPLAY_CLOCK_EXTRA_SAMPLES`**, **`SSB64_NETPLAY_CLOCK_SETTLE_ROUNDS`**
- **`SSB64_NETPLAY_BARRIER_VI_ALIGN`**, **`SSB64_NETPLAY_BARRIER_VI_HZ`**, **`SSB64_NETPLAY_BARRIER_CONSERVATIVE`**
- **`SSB64_NETPLAY_BARRIER_MAX_CONTRACT_SKEW_MS`**, **`SSB64_NETPLAY_BARRIER_EXTRA_LEAD_MS`**
- **`SSB64_NETPLAY_BARRIER_ESCAPE_MS`**, **`SSB64_NETPLAY_BARRIER_REQUEUE_MS`**

**Diagnostics / slots / UDP**

- **`SSB64_NETPLAY_TICK_DIAG`** — Tick / NetSync diagnostic level.
- **`SSB64_NETPLAY_TICK_GRID_EXEC_GATE`** — Tick-grid exec gate.
- **`SSB64_NETPLAY_REMOTE_SLOTS`**, **`SSB64_NETPLAY_PEER_SENDER_SLOTS`**, **`SSB64_NETPLAY_EXTRA_LOCAL_PLAYER`** — Slot wiring overrides.
- **`SSB64_NETPLAY_INPUT_BUNDLE_REDUNDANCY`**
- **`SSB64_NETPLAY_UDP_FRAME_TRACE`**
- **`SSB64_NETPLAY_GC_TRAVERSAL_DIAG`** — GcRunAll traversal logging with NetSync.
- **`SSB64_NETPLAY_DESYNC_TRACE`**, **`SSB64_NETPLAY_NETSYNC_INPUT_DIAG`**, **`SSB64_NETPLAY_REMOTE_RING_CHECKSUM`**
- **`SSB64_NETPLAY_FRAME_COMMIT_TOKEN`** — Frame-commit token checks (shared with classifier).
- **`SSB64_NETPLAY_ASSERT_MP_TIC`** — Log `mp_collision` tic vs sim tick.
- **`SSB64_NETPLAY_HOSTFRAME_GATE_PUMP`**, **`SSB64_NETPLAY_SYNC_PRESENT_HOLD`**

**Skew / sim trace**

- **`SSB64_NETPLAY_SKEW_LEAD_MAX_TICKS`**, **`SSB64_NETPLAY_SKEW_BEHIND_MAX_TICKS`**, **`SSB64_NETPLAY_SKEW_BEHIND_LOG`**
- **`SSB64_NETPLAY_PACING_LOG`**
- **`SSB64_NETPLAY_SIM_TRACE_NEEDLE_MIN`**, **`SSB64_NETPLAY_SIM_TRACE_NEEDLE_MAX`**, **`SSB64_NETPLAY_SIM_TRACE_NEEDLE_LEVEL`**
- **`SSB64_NETPLAY_SIM_STATE_TICK_INTERVAL`**

---

## Rollback ([`port/net/sys/netrollback.c`](port/net/sys/netrollback.c))

- **`SSB64_NETPLAY_ROLLBACK`** — Master rollback enable for env-gated init.
- **`SSB64_NETPLAY_ROLLBACK_INJECT_TICK`**, **`SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH`**, **`SSB64_NETPLAY_ROLLBACK_MISMATCH_DEBUG`**
- **`SSB64_NETPLAY_ROLLBACK_VERIFY_STRICT`**, **`SSB64_NETPLAY_ROLLBACK_LOAD_HASH_VERIFY`**
- **`SSB64_NETPLAY_ROLLBACK_MISMATCH_REMOTE_WITHOUT_PUBLISHED`**, **`SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH_PLAYER`**
- **`SSB64_NETPLAY_RESIM_TICK_TRACE`**, **`SSB64_NETPLAY_ROLLBACK_SCAN_DIAG`**
- **`SSB64_NETPLAY_SIM_HZ`** — Sim Hz hint for rollback-related timing.

---

## Replay ([`port/net/sys/netreplay.c`](port/net/sys/netreplay.c))

- **`SSB64_REPLAY_RECORD`**, **`SSB64_REPLAY_PLAY`** — Paths for record / playback.
- **`SSB64_REPLAY_RECORD_FRAMES`** — Cap recorded frames.

---

## Tick grid, phase, taskman, classifier, controller freeze

- **`SSB64_NETPLAY_TICK_GRID_LOCK_DIAG`** — [`nettickgridlock.c`](port/net/sys/nettickgridlock.c)
- **`SSB64_NETPLAY_TICK_GRID_CALIBRATE_MS`** — [`netphase.c`](port/net/sys/netphase.c)
- **`SSB64_NETPLAY_TASKMAN_DEBUG`** — [`taskman.c`](port/net/sys/taskman.c)
- **`SSB64_NETPLAY_FIGHTER_PHASE_TRACE`**, **`SSB64_NETPLAY_FIGHTER_PHASE_ASSERT`** — [`netfighterphase.c`](port/net/sys/netfighterphase.c)
- **`SSB64_NETPLAY_DESYNC_CLASSIFIER`**, **`SSB64_NETPLAY_FRAME_COMMIT_TOKEN`**, **`SSB64_NETPLAY_FRAME_COMMIT_STARVATION`** — [`netdesyncclassifier.c`](port/net/sys/netdesyncclassifier.c)
- **`SSB64_NETPLAY_CONTROLLER_FREEZE_SNAPSHOT`** — [`netcontrollerfreeze.c`](port/net/sys/netcontrollerfreeze.c)

---

## Matchmaking and bootstrap

- **`SSB64_MATCHMAKING_BASE_URL`** — [`mm_matchmaking.c`](port/net/matchmaking/mm_matchmaking.c)
- **`SSB64_MATCHMAKING_PUBLIC_ENDPOINT`**, **`SSB64_MATCHMAKING_BIND`**, **`SSB64_MATCHMAKING_LAN_ENDPOINT`** — [`scautomatch.c`](port/net/sc/sccommon/scautomatch.c)
- **`SSB64_MATCHMAKING_LAN_INTERFACE`** — [`mm_lan_detect.c`](port/net/matchmaking/mm_lan_detect.c)
- **`SSB64_NETPLAY_SERVER_BOOTSTRAP`** — [`mm_server_barrier.c`](port/net/bootstrap/mm_server_barrier.c) (Linux)

---

## Scene / automatch dev ([`port/net/sc/scmanager.c`](port/net/sc/scmanager.c), [`scautomatch.c`](port/net/sc/sccommon/scautomatch.c))

- **`SSB64_START_SCENE`**, **`SSB64_SPGAME_STAGE`**, **`SSB64_SPGAME_FKIND`** — Dev scene / stage / fighter kind overrides (also in scautomatch for stage).

---

## Game loop (net-related) ([`port/gameloop.cpp`](port/gameloop.cpp))

- **`SSB64_NETPLAY_DECOUPLE_DISPLAY_SIM`** — Display vs sim decouple.
- **`SSB64_NETPLAY_PUSH_FRAME_DIAG_MS`** — Push-frame diagnostic interval.
- **`SSB64_FREEZE_PACING`** — Pacing freeze hook (affects loop timing; can interact with net sessions).

---

## Persistence (not net-only, useful for net sessions)

- **`SSB64_SAVE_PATH`** — Override path for SRAM file (`port_save.cpp`); same tree as app data / config on many installs.

---

## Porting note

Paths above are under **BattleShip** `port/net/` (not `decomp/src/sys/…`). When moving netcode, keep the `SSB64_NETPLAY_*` names stable so scripts, CI, and player notes stay valid, or document a rename map in your fork.
