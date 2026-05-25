# Netplay and netmenu environment variables

Reference for debugging and for porting this netcode to other decomp projects. Values are read with `getenv` / `std::getenv` unless noted. Empty or unset usually means “default off” or “use compiled defaults”; check call sites for exact parsing (`atoi`, truthy, level numbers).

**Precedence (input delay):** When `SSB64_NETPLAY_MATCH_INPUT_DELAY` is set (0–99), it defines the **linked** wire delay contract (floor/ceiling in netpeer for committed **`D`**). It overrides **`SSB64_NETPLAY_DELAY`** for that purpose. When match delay is unset, **`SSB64_NETPLAY_DELAY`** overrides the automatch caller delay. **Online default:** unless **`SSB64_NETPLAY_MATCH_INPUT_DELAY=0`** (explicit lockstep-at-zero contract) or **`SSB64_NETPLAY_ALLOW_INPUT_DELAY_ZERO=1`** (lab override), committed **`D`** and **`delay_floor`** are raised to **at least 1** after automatch configure, debug `SSB64_NETPLAY` init, and delay-sync commits. **Strict extra slack** (`SSB64_NETPLAY_STRICT_SLACK_FRAMES` and legacy aliases) is **independent**: it always comes from those envs via `g_NetInputDelayFrames` (clamped 0–4), not from the match delay integer. **Automatch:** `syNetPeer` resolves delay as `MATCH_INPUT_DELAY` → `SSB64_NETPLAY_DELAY` → caller input delay, commits that value immediately for VS startup, and logs `committed_input_delay` with the source.

**Build variants (`SSB64_NETMENU`):** Default **OFF** (“offline”) — stock VS flow, `port/stubs/net_port_glue_offline.c` satisfies netpeer symbols, **no libcurl**. **ON** (“netmenu” / netplay) — full `port/net/**` + `decomp/src/netplay/**`, libcurl for automatch HTTPS. Desktop: **`find_package(CURL)`** (Homebrew / distro / vcpkg / MinGW). **Android:** static curl+mbedTLS via [`cmake/Ssb64CurlAndroid.cmake`](../cmake/Ssb64CurlAndroid.cmake); enable with Gradle **`-Pssb64Netmenu=true`** (see [`docs/android_port_status_2026-05-01.md`](android_port_status_2026-05-01.md)). Packaging: `-DSSB64_NETMENU=ON` or `--netplay` (`package-linux.sh`, `package-macos.sh`, `package-mingw-windows.sh`, `package-android.sh --netplay`); offline is the default when `--netplay` / `-Pssb64Netmenu` is omitted.

**Linux vs Windows (netmenu):** With **`SSB64_NETMENU`**, Windows (MSVC and Linux→MinGW cross) links the same manual UDP P2P + bootstrap path as Linux (`SSB64_NETPLAY_*` bind/peer, delay-sync, phase lock). Matchmaking (`port/net/matchmaking/mm_*`), automatch HTTPS/CURL, and `port/net/bootstrap/mm_server_barrier.c` are compiled on **all** netmenu platforms (CMake no longer omits `mm_*` on `WIN32`). Platform-specific code inside those TUs (e.g. Win32 LAN enumeration in `mm_lan_detect.c`) is gated in source, not by excluding files at configure time.

**Maintenance:** Refresh this list when adding knobs:

```bash
rg 'getenv\("SSB64_' port/net port/gameloop.cpp
```

See also [docs/netplay_architecture.md](netplay_architecture.md), [docs/netcode_agent_rules.md](netcode_agent_rules.md), and [docs/netplay_pacing.md](netplay_pacing.md) (skew / starvation pacing narrative).

---

## Runtime concepts (tuning & under the hood)

These names appear in **`delay_sync_diag`**, **`STRICT:`** logs, NetSync **`tick_diag`**, and comments in **`netpeer.c`** / **`netinput.c`**. They are **not** environment variables; they explain what the **`SSB64_NETPLAY_*`** knobs are steering.

| Name | What it is | Big interactions |
|------|------------|-------------------|
| **sim tick** | Authoritative VS battle index from **`syNetInputGetTick()`**; advances only after a full **`scVSBattleFuncUpdate`** when admission allows. | Keys **published** local history; pairs with **wire** labels via **`D`**. FuncRead admission, rollback hooks, and **`INPUT_DELAY_SYNC`** “`sim + N`” scheduling (**`SSB64_NETPLAY_DELAY_SYNC_COMMIT_LEAD_TICKS`**, default **N=2**) are all expressed in this tick. |
| **`D` (committed delay)** | **`syNetPeerGetCommittedInputDelay()`** / **`syNetPeerGetInputDelay()`** — committed **wire** delay for the session (`sSYNetPeerInputDelay`). **`wire_tick = sim_tick + D`** for bundles / ring columns. | Set by **`SSB64_NETPLAY_DELAY`**, **`SSB64_NETPLAY_MATCH_INPUT_DELAY`** (linked; overrides delay env when **set**), automatch configure, **`INPUT_DELAY_SYNC`**, adaptive / starvation bump ramps, and (Linux UDP) **host auto runway** +1 bumps when sim persistently leads **`DelaySimTickFromWire(hr)`** (still subject to contract floor/ceiling). Feeds **`wire_base`**, **`wire_cap`**, delay-sync diag. |
| **`wire_base`** | **`sim + D`** (saturating) — minimum strict wire row for this sim tick before slack. | **`delay_sync_diag` `wire_base=`**; **`MATCH_INPUT_BUFFER_MIN_SLACK_TICKS`** compares **`hr`** excess vs this when **`hr > wire_base`**; starvation **`EXIT_HR_LEAD`** exit leg anchored here so it does not track **`hr`** 1:1. |
| **strict slack** | Extra frames **`sim + D + slack`** ceiling from **`g_NetInputDelayFrames`** (**`STRICT_SLACK_FRAMES`** etc., clamped **≤ 4**), **not** tied to the match delay integer. | **`wire_cap`** in diag; **`syNetPeerGetStrictRequiredWireTick`** caps how far ahead on the wire strict admission looks. |
| **`wire_cap`** | **`sim + D + strict_slack`** (saturating). | Upper bound blended with **`hr`** for the effective row. |
| **`hr` (`HighestRemoteTick`)** | Max **`frame.tick`** on inbound INPUT frames staged for remote slots (**`syNetPeerGetHighestRemoteTick`**). | Raises/lowers **`required_wire`** via **`min(max(wire_base, hr), wire_cap)`**; drives **skew pacing** (local tick vs delayed **`hr`**); **starvation** underrun is **`hr < required_wire`**; **lead buffer** / buffer-min-slack rules; echoed in INPUT packet header + checksum. |
| **`required_wire` / `wire_eff`** | Effective wire index strict admission probes on the remote ring this tick (same as **`syNetPeerGetEffectiveWireFrontierForAdmission`**). **`wire_eff`** is the diag name. | **`syNetPeerIsRemoteInputReadyForSimTick`**, **starvation** enter/exit, **`delay_sync_diag`**. Ring checks use **`syNetInputHasRemoteInputForWireTick(..., required_wire)`**; optional **`STRICT_RING_FUZZ_TICKS`** relaxes the ring row when **`required_wire > hr`** (see env doc). |
| **`wire_slack` (diag)** | **`max(0, hr - wire_eff)`** — how many wire ticks the peer’s labels sit **past** the effective row. | Read with **`hr`** and **`lead_b`** to see if strict is waiting on ring vs headroom. |
| **`R` (strict remote miss)** | Admission path: missing confirmed remote input for the effective wire row; **partial** local publish for the same sim tick, **no** tick advance that pass. | Still allows **`syNetPeerUpdate`** / INPUT send so the wire does not go silent; interacts with **prediction** and **`INPUT_FUTURE_WIRE_TICKS`** (inflates **`hr`** without advancing sim). |
| **`V` (delay-sync starvation hold)** | Sustained **`hr < required_wire`** (match-linked delay + handler) latches a hold similar in spirit to **`R`** for full publish. | **`DELAY_SYNC_STARVATION_*`** envs; exit uses **`EXIT_FRAMES`**, **`HYSTERESIS`**, **`EXIT_HR_LEAD`**; optional **adaptive bump** changes **`D`** on the host. |
| **Rollback / resim** | Separate pipeline: resim ticks bypass strict stall predicates where coded. | **`SSB64_NETPLAY_ROLLBACK`** and rollback envs; do not confuse with strict **`R`**. |
| **`B` (buffer min slack)** | **`SSB64_NETPLAY_MATCH_INPUT_BUFFER_MIN_SLACK_TICKS`** when match delay active: if **`hr > wire_base`**, require **`(hr - wire_base) ≥ B`**; at **`hr ≤ wire_base`** adds no bar (lockstep-safe). | AND with **`STRICT_REMOTE_LEAD_BUFFER_TICKS`** when **`lead_b > 0`**. |
| **`lead_b`** | **`SSB64_NETPLAY_STRICT_REMOTE_LEAD_BUFFER_TICKS`** — uses excess **`hr`** past **`required_wire`** (up to **`lead_b`** ticks) in the strict **`hr`** admission threshold (see `syNetPeerIsRemoteInputReadyForSimTick` / `netinput.h`). | Distinct from buffer-min-slack **`B`** (`MATCH_INPUT_BUFFER_MIN_SLACK_TICKS`). See **`hr_need`** in **`delay_sync_diag`** when **`lead_b > 0`**. |
| **Future wire placeholders** | **`SSB64_NETPLAY_INPUT_FUTURE_WIRE_TICKS`** appends extra INPUT rows at **`max(bundle)+1…`** repeating the last bundled sample. | Inflates peer **`hr`** / ring occupancy ahead of authored sim; later real sends overwrite the same wire tick. |

### Delay Sync Test Preset (fixed delay, community / lab)

Use this **preset** to validate wire delay semantics (`wire_tick = sim_tick + committed_delay`) before enabling adaptive delay, execution delay, or rollback. Set **the same values on both peers**.

| Goal | Setting |
|------|--------|
| Linked match delay off | **Unset** `SSB64_NETPLAY_MATCH_INPUT_DELAY` (stops match-linked **`D`**; strict slack still comes only from **`STRICT_SLACK_FRAMES`** / aliases). |
| Fixed wire delay | **`SSB64_NETPLAY_DELAY`** = same integer on host and guest (or the same automatch `input_delay` on both sides). |
| Adaptive off | **Unset** or **`SSB64_NETPLAY_ADAPTIVE_DELAY=0`**. |
| Strict slack zero | **`SSB64_NETPLAY_STRICT_SLACK_FRAMES=0`** (legacy aliases: `SSB64_NET_DELAY_FRAMES=0` and `SSB64_NETPLAY_INPUT_EXEC_DELAY_FRAMES=0`). |
| Strict confirmations | Phase-locked strict wire admission is now the default live VS path. |
| Pacing | Skew pacing no longer owns authoritative tick placement; see [docs/netplay_phase_lock.md](netplay_phase_lock.md). |

**Diagnostics:** **`SSB64_NETPLAY_DELAY_SYNC_DIAG`** — `0` off (default), **`1`** first ~300 sim ticks after execution begin plus every tick where committed delay changes and logs when deferred delay applies; **`2`** every sim tick while VS is active (very noisy). Compare host vs guest `delay_sync_diag` lines for `sim`, `D`, `wire`, and remote ring presence. **`SSB64_NETPLAY_RUNWAY_FRONTIER_LOG_TICKS`** (UDP netmenu, lazy cache reset with adaptive tracking): default **60** sim ticks between `runway_frontier` sample lines (`sim`, `hr`, `frontier_sim`, `D`, `deficit`, `gap_hr_minus_sim`, `host`); **0** disables; clamped **0–600**. **`SSB64_NETPLAY_BOOTSTRAP_INGRESS_SYMMETRY`** is default-on in phase-locked VS: execution readiness stays false until each peer has sent outbound INPUT and received **`hr > 0`**; warmup INPUT may be sent during exec-hold when the normal history bundle is empty.

**Optional input starvation (match-linked buffer):** With **`SSB64_NETPLAY_MATCH_INPUT_DELAY`** set and strict contract on, you can enable **`SSB64_NETPLAY_DELAY_SYNC_STARVATION_HANDLER`** so sustained `hr` below the **effective** admission wire row (`wire_eff` in `delay_sync_diag`: `min(max(sim+D, hr), sim+D+slack)`) latches a pause path (admission **`V`**, same partial-publish + scene suppress as strict **`R`**) until `hr` clears the starvation exit band (hysteresis on **`required_wire`**, and when **`EXIT_HR_LEAD` > 0** also **`hr` past `sim+D`**) for several frames—giving the remote ring time to refill without mutating `D`. Default **off** for backwards compatibility. Tune enter/exit counts, hysteresis, and **`EXIT_HR_LEAD_TICKS`** with the companion envs below (names must include **`DELAY_SYNC_STARVATION_`** — e.g. not `SSB64_NETPLAY_DELAY_SYNC_EXIT_FRAMES`); use **`DELAY_SYNC_DIAG`** / **`FRAME_COMMIT_DIAG`** to correlate `hr` vs `wire_cap` / `wire_eff` and `path=V` lines.

---

## Auto session negotiation ([`port/net/sys/netsession_params.c`](port/net/sys/netsession_params.c))

- **`SSB64_NETPLAY_AUTO_SESSION_PARAMS`** — **`1`** or unset (default): host measures RTT (`TIME_PING` during VS sync, or barrier clock sync when enabled), computes and negotiates rollback-first **`D`** (small commit delay, typically **2–4**), **`PHASE_LOCK_PREDICTION_TICKS`** (prediction runway ≈ one-way RTT in ticks + margin, up to **16**), transport knobs, snapshot/resim budgets, and symmetric rollback enablement, then sends **`SESSION_PARAMS`** (wire type **22**), guest **`SESSION_PARAMS_ACK`** (type **23**). Both peers apply the same contract before battle execution unlocks. **`0`**: legacy manual env only. **`SSB64_NETPLAY_DELAY`** / **`SSB64_NETPLAY_MATCH_INPUT_DELAY`** still override committed **`D`** when set. **`SSB64_NETPLAY_ROLLBACK=0`** still disables rollback locally even if the host proposal includes it.
- Example / bisect: [`scripts/netplay-auto-session.env.example`](../scripts/netplay-auto-session.env.example).

---

## Input, delay, strict contract ([`port/net/sys/netinput.c`](port/net/sys/netinput.c))

- **`SSB64_NETPLAY_BOOTSTRAP_INGRESS_SYMMETRY`** — UDP netmenu (Linux and Windows): bootstrap-phase ingress symmetry (`syNetPeerBootstrapIngressSymmetrySatisfied` + warmup sends in `syNetPeerUpdate`). Default **on** (unset); set **`0`** only for targeted transport debugging. Execution readiness remains false until outbound INPUT warmup and inbound `hr > 0` are satisfied.
- **`SSB64_NETPLAY_MATCH_INPUT_DELAY`** — Integer 0–99. Match-mode **linked** wire delay **`D`** (floor/ceiling in netpeer). Does **not** set strict slack — use **`STRICT_SLACK_FRAMES`** separately. Overrides **`SSB64_NETPLAY_DELAY`** for committed delay when set. Automatch commits the linked `D` immediately before VS execution and raises **`delay_floor`** to the match target so adaptive ramps cannot drop below the linked contract.
- **`SSB64_NETPLAY_MATCH_INPUT_BUFFER_MIN_SLACK_TICKS`** — Retired from the phase-locked commit predicate. The live gate uses exact `sim + D` row ownership plus `SSB64_NETPLAY_PHASE_LOCK_PREDICTION_TICKS`.
- **`SSB64_NETPLAY_STRICT_SLACK_FRAMES`** — Strict extra slack frames (0–4) for `wire_cap` / `syNetPeerGetStrictRequiredWireTick`. Legacy aliases: **`SSB64_NET_DELAY_FRAMES`**, **`SSB64_NETPLAY_INPUT_EXEC_DELAY_FRAMES`**. **Independent** of **`MATCH_INPUT_DELAY`** (match value does not copy into slack).
- **`SSB64_NETPLAY_STRICT_RING_FUZZ_TICKS`** — Integer **0–2** (default **0** = off). When **`required_wire > hr`** (effective frontier pinned to **`sim+D`** ahead of the peer’s highest staged wire tick), allow strict ring admission if **`hr + f >= required_wire`** and **`syNetInputHasRemoteInputForWireTick(..., required_wire - f)`** for some **`1 <= f <= value`**. Does **not** apply when **`required_wire <= hr`** (avoids admitting on stale rows while waiting for the true frontier cell). **Symmetric:** set the **same** value on host and guest. Lazy cache reset with `syNetInputRefreshCachedNetplayEnvForNewMatch`. **Desync risk** vs strict lockstep — enable only for lab / LAN tuning.
- **`SSB64_NET_DELAY_FRAMES`** — Legacy alias for strict slack frames (0–4), after `SSB64_NETPLAY_STRICT_SLACK_FRAMES`.
- **`SSB64_NETPLAY_INPUT_EXEC_DELAY_FRAMES`** — Legacy alias for strict slack frames, after `SSB64_NET_DELAY_FRAMES`.
- **`SSB64_NETPLAY_INPUT_PREDICTION`** — `0` disables last-input prediction for strict remote misses; default on.
- **`SSB64_NETPLAY_PHASE_LOCK_PREDICTION_TICKS`** — Integer **0–16** (default **2** when auto negotiation is off). With auto negotiation on, the negotiated **`phase_lock_ticks`** override this. Phase-locked VS may execute a missing remote wire row while sim is within this many ticks of the observed remote frontier (`hr` mapped to sim when rollback is on). Prediction still uses `wire_tick = sim_tick + D`; late real input triggers rollback if gameplay fields differ.
- **`SSB64_NETPLAY_INPUT_FUTURE_WIRE_TICKS`** — See **Diagnostics / slots / UDP** (append placeholder INPUT rows ahead on the wire); default **0**.
- **`SSB64_NETPLAY_INPUT_CONTRACT`** / **`SSB64_NETPLAY_STRICT_INPUT_CONTRACT`** — Retired for live VS selection. The phase-locked strict contract is always active for netplay VS.
- **`SSB64_NETPLAY_STRICT_REMOTE_LEAD_BUFFER_TICKS`** — Retired from the phase-locked commit predicate. `hr` no longer changes which wire row owns a sim tick.
- **`SSB64_NETPLAY_PREDICT_NEUTRAL`** — VS session: neutral prediction policy when on.
- **`SSB64_NETPLAY_LOG_LOCAL_INPUT`** — Guest-side local input logging (rate-limited).
- **`SSB64_NETPLAY_ABORT_ON_INPUT_MISMATCH`** — Bitmask: bit 1 NetSync validation, bit 2 rollback pre-resim; logs mismatch (hard `abort` only if fatal env set).
- **`SSB64_NETPLAY_ABORT_ON_INPUT_MISMATCH_FATAL`** — Non-zero: after logging mismatch, call `abort()` (for CI / bisect).
- **`SSB64_NETPLAY_STALL_UNTIL_REMOTE`** — Stall-until-remote path; cached.
- **`SSB64_NETPLAY_INPUT_PREDICT_DIAG`** — Level 1/2 predicted-remote diagnostics.
- **`SSB64_NETPLAY_FRAME_COMMIT_DIAG`** — Admission path logging level. Also enables extended NetSync input diag (`remote_ring_hist_win`, `pub_vs_remote_summary` per player) on validation ticks.
- **`SSB64_NETPLAY_FRAME_COMMIT_SUMMARY`** — Frame-commit summary logging.
- **`SSB64_NETPLAY_PATCH_PUBLISH_LOG`** — **`1`**: log silent published-history rewrites and defer-without-patch decisions on remote wire confirm. Tags: **`patch_publish`** (`reason=insignificant|no_ggpo|digital_tap|post_queue|unknown`) and **`defer_analog_correction`** (`reason=analog_onset|ggpo_queued`). Grep separately; pair with **`SSB64_NETPLAY_ANALOG_ONSET_LOG=1`** for onset prediction lines.
- **`SSB64_NETPLAY_RESIM_RECONCILE_LOG`** — **`1`**: log published-history reconcile spans: `resim_reconcile_span` (resim begin / post-resim complete) and `commit_window_reconcile` (frame-commit validation window before digest build). Also logs `resim_reconcile_post_complete` on post-resim complete.
- **`SSB64_NETPLAY_FRAME_COMMIT_RECV_LOG_MAX`** — Rate-limited `FRAME_COMMIT_RECV_DROP` lines when **`FRAME_COMMIT_TOKEN`** or **`FRAME_COMMIT_DIAG`** is on (default **16**). Session summary adds `recv_drop_size` / `recv_drop_header` / `recv_drop_checksum` counters.
- **`SSB64_NETPLAY_UDP_RCVBUF_BYTES`** — `SO_RCVBUF` on the VS datagram socket (default **1048576**, clamp **256 KiB–16 MiB**). Sized for wire **V4 INPUT** bundles (**200 B** each); NetPeer `stg=` in stats is cumulative remote **input frames** staged, not unread datagram count. Tune only after soak if `fc_recv` stays 0.
- **`SSB64_NETPLAY_STRICT_R_STUCK_FORCE_DIAG`** — With strict slack 0, force advance after sustained strict-R miss (diagnostic).
- **`SSB64_NETPLAY_DELAY_SYNC_DIAG`** — Delay / wire alignment trace (`0` / `1` / `2`); see **Delay Sync Test Preset** above.
- **`SSB64_NETPLAY_DELAY_SYNC_COMMIT_LEAD_TICKS`** — Integer **1–16** (default **2**). How many **local** sim ticks after the decision to queue a change before **`INPUT_DELAY_SYNC`** / host delay-ramp commits take effect (`syNetInputGetTick() + N`, saturating). Starvation adaptive bumps use the same lead. Larger **`N`** defers committed wire delay changes (less aggressive “next boundary” apply). Lazy cache reset when `syNetPeerRefreshCachedNetplayEnvForNewMatch` runs (re-read per match).
- **`SSB64_NETPLAY_DELAY_SYNC_STARVATION_HANDLER`** — Linux strict VS: non-zero enables sustained **`hr` below `required_wire`** detection (only when **`SSB64_NETPLAY_MATCH_INPUT_DELAY`** is set). While latched, `syNetInputFuncRead` takes the starvation path (admission **`V`**, partial publish + scene suppress) so the match-style wire buffer can rebuild; default **off**.
- **`SSB64_NETPLAY_DELAY_SYNC_STARVATION_ENTER_FRAMES`** — Consecutive underrun frames before latching (default **4**, clamped 1–60). Parsed once per VS session when the handler first runs.
- **`SSB64_NETPLAY_DELAY_SYNC_STARVATION_EXIT_FRAMES`** — Consecutive frames where the starvation **`exit_ok`** predicate holds before clearing the latch (default **2**, clamped 1–60). **`exit_ok`** is **`hr >= required_wire + HYSTERESIS`** when **`EXIT_HR_LEAD=0`**; when **`EXIT_HR_LEAD > 0`**, also requires **`hr >= wire_base + HYSTERESIS + EXIT_HR_LEAD`** *and* **`hr >= required_wire + HYSTERESIS`** (see glossary).
- **`SSB64_NETPLAY_DELAY_SYNC_STARVATION_HYSTERESIS_TICKS`** — Extra wire ticks added to `required_wire` for the exit test (default **0**, clamped 0–16). Parsed once per VS session with the handler bundle.
- **`SSB64_NETPLAY_DELAY_SYNC_STARVATION_EXIT_HR_LEAD_TICKS`** — Additional **`hr`** headroom (default **0**, clamped **0–32**). When **> 0**, a frame counts toward **exit** only if **`hr >= wire_base + HYSTERESIS + EXIT_HR_LEAD`** *and* **`hr >= required_wire + HYSTERESIS`** (`wire_base = sim + D`; `required_wire` is the effective admission row). When **0**, exit uses only **`hr >= required_wire + HYSTERESIS`** (unchanged). The dual test avoids the old impossible **`hr >= required_wire + lead`** case when **`required_wire`** tracks **`hr`**. Parsed with the starvation env bundle.
- **`SSB64_NETPLAY_STARVATION_ADAPTIVE_DELAY_BUMP`** — Non-zero: when **host** first latches delay-sync starvation (match-linked delay + handler on), queue **`INPUT_DELAY_SYNC`** via the same host ramp path as `SSB64_NETPLAY_ADAPTIVE_DELAY` — committed wire delay increases by **`STARVATION_ADAPTIVE_DELAY_STEP`** (clamped to **`DELAY_MAX`** / ceiling), applied at local `sim + N` ticks (same commit lead as **`SSB64_NETPLAY_DELAY_SYNC_COMMIT_LEAD_TICKS`**). Guest applies the pending commit from the next host packet. Default **off**; parsed with the starvation env bundle.
- **`SSB64_NETPLAY_STARVATION_ADAPTIVE_DELAY_STEP`** — Integer **1–4** (default **1**). Added to committed delay per latch event (then clamped to contract ceiling).
- **`SSB64_NETPLAY_STARVATION_ADAPTIVE_DELAY_COOLDOWN_TICKS`** — Minimum sim ticks between starvation bump queues (default **120**, clamped **0–600**; **0** disables cooldown so only latch-edge + ramp-pending guards limit repeats).

---

## NetPeer: debug P2P, barrier, clock, pacing ([`port/net/sys/netpeer.c`](port/net/sys/netpeer.c))

**Bootstrap / session**

- **`SSB64_NETPLAY`** — Non-zero enables debug netplay env init (`syNetPeerInitDebugEnv`).
- **`SSB64_NETPLAY_LOCAL_PLAYER`**, **`SSB64_NETPLAY_REMOTE_PLAYER`** — Sim slot indices.
- **`SSB64_NETPLAY_DELAY`** — Initial **wire** committed input delay (unless `SSB64_NETPLAY_MATCH_INPUT_DELAY` set). When match delay is **unset**, values below **1** are clamped up to **1** for online sessions unless **`SSB64_NETPLAY_ALLOW_INPUT_DELAY_ZERO=1`**.
- **`SSB64_NETPLAY_ALLOW_INPUT_DELAY_ZERO`** — Non-zero: disable the online default **minimum `D` of 1** (allows **`SSB64_NETPLAY_DELAY=0`** and automatch caller delay **0** when **`MATCH_INPUT_DELAY` is unset**). **`MATCH_INPUT_DELAY=0`** still pins **`D=0`** without this env.
- **`SSB64_NETPLAY_SESSION`** — Session id.
- **`SSB64_NETPLAY_BIND`**, **`SSB64_NETPLAY_PEER`** — IPv4 `host:port` for UDP debug.
- **`SSB64_NETPLAY_BOOTSTRAP`**, **`SSB64_NETPLAY_HOST`**, **`SSB64_NETPLAY_SEED`** — Bootstrap / host flag / seed.
- **`SSB64_NETPLAY_BOOTSTRAP_RETRY_COUNT`** — Control-plane retries per bootstrap phase (READY / MATCH_CONFIG / START / offer exchange). Default **360** (~**6 s** at default sleep). Clamped **60–900**.
- **`SSB64_NETPLAY_BOOTSTRAP_RETRY_SLEEP_US`** — Microseconds between bootstrap pump iterations (default **16666**). Clamped **4000–50000**.
- **`SSB64_NETPLAY_BOOTSTRAP_START_BURST`** — Host **`START`** packets after guest READY (default **60**). Clamped **10–240**.
- **`SSB64_NETPLAY_BOOTSTRAP_PAUSE_BETWEEN_MS`** — Wall pause between automatch LAN/reflexive bootstrap attempts (default **500** ms). Clamped **0–5000**.
- **`SSB64_NETPLAY_STAGE_SCENE_GO_HOLD_MS`** — Post-bootstrap staging rendezvous hold before VS load (default **2500** ms). Clamped **100–8000**. Host repeats **`STAGE_SCENE_GO`** proportionally.
- **`SSB64_NETPLAY_SYNC_START_MS`** — Sync start timing (debug init block).
- **`SSB64_NETPLAY_LOCAL_HARDWARE`** — Maps local sim slot to hardware device index (multiple sites).

**Adaptive delay**

- **`SSB64_NETPLAY_ADAPTIVE_DELAY`** — Enable adaptive delay ramp.
- **`SSB64_NETPLAY_DELAY_MAX`** — Ceiling for adaptive / floor clamping (with automatch configure).

**Automatch / Linux**

- **`SSB64_NETPLAY_AUTOMATCH_NO_ITEMS`** — Item policy for automatch offer path.
- **`SSB64_NETPLAY_UDP_LINK_SYNC`** — **`1`** or unset (default): run **5** UDP echo rounds (`UDP_SYNC_REQ`/`REP`) at bootstrap to prove the path before `MATCH_CONFIG`. **`0`**: skip the probe entirely. On timeout, bootstrap **continues automatically** (fallback) unless **`SSB64_NETPLAY_UDP_LINK_SYNC_REQUIRED=1`**.
- **`SSB64_NETPLAY_UDP_LINK_SYNC_REQUIRED`** — Non-zero: **`UDP_LINK_SYNC` timeout aborts bootstrap** (legacy strict). Default off (fallback continue).
- **`SSB64_NETPLAY_UDP_LINK_SYNC_RETRANSMIT_MS`** — While awaiting each echo `REP`, re-send the **same** challenge token at this interval (default **300** ms). Must exceed one-way+return RTT; increase for heavy `tc netem` (e.g. **400** for 120 ms each way).
- **`SSB64_NETPLAY_REQUIRE_INPUT_BIND`** — Require input bind.
- **`SSB64_NETPLAY_BATTLE_EXEC_SYNC`** — Battle execution sync flag.

**Clock / barrier**

- **`SSB64_NETPLAY_CLOCK_SYNC_SAMPLES`**, **`SSB64_NETPLAY_CLOCK_EXTRA_SAMPLES`**, **`SSB64_NETPLAY_CLOCK_SETTLE_ROUNDS`**
- **`SSB64_NETPLAY_BARRIER_VI_ALIGN`**, **`SSB64_NETPLAY_BARRIER_VI_HZ`**, **`SSB64_NETPLAY_BARRIER_CONSERVATIVE`**
- **`SSB64_NETPLAY_BARRIER_MAX_CONTRACT_SKEW_MS`**, **`SSB64_NETPLAY_BARRIER_EXTRA_LEAD_MS`**
- **`SSB64_NETPLAY_BARRIER_ESCAPE_MS`**, **`SSB64_NETPLAY_BARRIER_REQUEUE_MS`**
- **`SSB64_NETPLAY_BARRIER_CLOCK_STALL_DIAG`** — Linux UDP: **`1`** logs every **300** `BattleBarrierWaitFrames` while the host awaits a **TIME_PONG** during the clock-sync sample loop (no sample progress). Off by default.

**Retired (ignored): post-barrier “running clock” NTP**

The old host-led periodic **TIME_PING** / **TIME_PONG** path (high-bit seq, ~3s default) and related **`SSB64_NETPLAY_RUNNING_CLOCK_*`** / **`SSB64_NETPLAY_STRICT_RUNNING_CONFIDENCE_*`** knobs are **removed**. Wall-clock alignment is **once** at the barrier (`TIME_PING`/`TIME_PONG` during the pre-start sample loop + **`BATTLE_START_TIME`**); steady play relies on the phase-locked shared frontier and fixed `sim + D` wire ownership, not continuous wall-clock resync.

**Diagnostics / slots / UDP**

- **`SSB64_NETPLAY_TICK_DIAG`** — Tick / NetSync diagnostic level.
- **`SSB64_NETPLAY_TICK_GRID_EXEC_GATE`** — Tick-grid exec gate.
- **`SSB64_NETPLAY_REMOTE_SLOTS`**, **`SSB64_NETPLAY_PEER_SENDER_SLOTS`**, **`SSB64_NETPLAY_EXTRA_LOCAL_PLAYER`** — Slot wiring overrides.
- **`SSB64_NETPLAY_INPUT_BUNDLE_REDUNDANCY`** — Positive integer (clamped **1–8**): after **`GatherHistoryBundle`**, duplicates the **last N** primary (and dual-local secondary) frames already in the packet (same **wire ticks** + samples) for UDP loss tolerance. Does **not** invent new future ticks (contrast **`INPUT_FUTURE_WIRE_TICKS`**). Parsed each **`syNetPeerBuildPacket`**.
- **`SSB64_NETPLAY_INPUT_FUTURE_WIRE_TICKS`** — Integer **0–8** (default **0** = off). After `GatherHistoryBundle` + redundancy, appends up to **N** extra primary (and dual-local secondary) INPUT rows at wire ticks **`max(bundle tick)+1 … +N`** (saturating add per offset), each copying **buttons/stick** from the **last row already in that bundle** (placeholder until real sim history for that wire supersedes on a later send). Raises peer **`hr`** / ring occupancy ahead of strictly authored ticks—**symmetric** value on host and guest; start low (**1**) and validate; wrong placeholders can be overwritten when real frames arrive but can interact badly with prediction / strict if mis-tuned. Read every `syNetPeerBuildPacket` (Linux `PORT`).
- **`SSB64_NETPLAY_UDP_FRAME_TRACE`**
- **`SSB64_NETPLAY_GC_TRAVERSAL_DIAG`** — GcRunAll traversal logging with NetSync.
- **`SSB64_NETPLAY_DESYNC_TRACE`**, **`SSB64_NETPLAY_NETSYNC_INPUT_DIAG`**, **`SSB64_NETPLAY_REMOTE_RING_CHECKSUM`**
- **`SSB64_NETPLAY_FRAME_COMMIT_TOKEN`** — Frame-commit token checks (shared with classifier).
- **`SSB64_NETPLAY_ASSERT_MP_TIC`** — Log `mp_collision` tic vs sim tick.
- **`SSB64_NETPLAY_HOSTFRAME_GATE_PUMP`**, **`SSB64_NETPLAY_SYNC_PRESENT_HOLD`**
- **`SSB64_NETPLAY_INGRESS_DIAG`** — Linux UDP: `0` off (default); **`1`** rate-limited `ingress_diag` lines (~120 `PortPushFrame` intervals) with **`sim`** (`syNetInputGetTick()`), **`hr`** before/after the pump, datagrams drained, **`push`** counter, and cumulative pump/datagram totals; tag identifies the caller (`port_push`, `funcread`, `inactive_pre_read`, `stall_extra`, or `pump`). **`2`** logs every qualifying pump (very noisy). Resets with `syNetPeerRefreshCachedNetplayEnvForNewMatch`. Complements **`HOSTFRAME_GATE_PUMP`**, which only runs `UpdateBattleGate` while execution is **not** ready; recv-only **`syNetPeerPumpIngressTransport`** also runs from **`PortPushFrame`** when **`SSB64_NETPLAY_DECOUPLE_DISPLAY_SIM`** is on (including sim-skip iterations) and optionally from strict stall paths below.
- **`SSB64_NETPLAY_INGRESS_EXTRA_PUMPS_ON_STALL`** — Linux strict **`R`** / delay-sync starvation **`V`** early-return paths in `syNetInputFuncRead`: integer **0–4** (default **0** = off). After logging frame-commit diag, invokes **`syNetPeerPumpIngressTransport("stall_extra")`** that many extra times to drain UDP bursts without changing admission semantics (no extra delay-sync apply). Lazy-parsed once per value; clamped **0–4**. Cache reset with `syNetInputRefreshCachedNetplayEnvForNewMatch`.

**Skew / sim trace**

- **`SSB64_NETPLAY_SKEW_LEAD_MAX_TICKS`**, **`SSB64_NETPLAY_SKEW_BEHIND_MAX_TICKS`**, **`SSB64_NETPLAY_SKEW_BEHIND_LOG`**, **`SSB64_NETPLAY_SKEW_GAP_EWMA_PACING`**, **`SSB64_NETPLAY_SKEW_GAP_EWMA_CAP_TICKS`**, **`SSB64_NETPLAY_SKEW_GAP_EWMA_MIN_LEAD_TICKS`**
- **`SSB64_NETPLAY_PACING_LOG`**
- **`SSB64_NETPLAY_SIM_TRACE_NEEDLE_MIN`**, **`SSB64_NETPLAY_SIM_TRACE_NEEDLE_MAX`**, **`SSB64_NETPLAY_SIM_TRACE_NEEDLE_LEVEL`**
- **`SSB64_NETPLAY_SIM_STATE_TICK_INTERVAL`**
- **`SSB64_NETPLAY_JOINT_TRANSLATE_TRACE`** — When **`1`**, each **live forward-sim** tick (`rb_applied=0`, not resimming) logs one `joint_translate` line per non-null `fp->joints[ji]` with translate hashes (same encoding as Full `figh`), per-joint `anim_frame`/`anim_wait`, and Full-only fighter scalars (`motion_attack_id`, `hitstatus`, `invincible_tics`, `is_shield`, `is_hitstun`) on the same line. Optional **`SSB64_NETPLAY_JOINT_TRANSLATE_TRACE_SLOT`** / **`_FKIND`** restrict which fighters are logged. On the **first** forward-sim `figh` (`syNetSyncHashBattleFightersFull`) change per battle, emits one `joint_translate_trigger` bookmark (`prev_tick` + old/new hash); session resets on battle clock reset. No tick window — intentionally noisy. Diff host vs client logs at `prev_tick` and `tick` after the trigger to find the first diverging `ji`.
- **`SSB64_NETPLAY_FHASH_LIGHT_MISMATCH_TRIGGER`** — When **`1`**, on live forward sim only: on consecutive tick steps where any player's **`fhash_light`** and aggregate **`figh`** both change, logs `fhash_light_trigger` + full **`fighter_detail`** at **`prev_tick`** (`fhash_light_pre`) and **`tick`** (`fhash_light_step`) — `floor`, `floor_flags`, `vel_ground`, `vel_air`, `top`, `pos_prev`, `coll_tic`, jostle. **Two fires per battle:** `phase=first` (earliest in window, movement onset) and `phase=second` (default first tick ≥ **`SSB64_NETPLAY_FHASH_LIGHT_MISMATCH_TRIGGER_SECOND_MIN`** = **473**, targets cross-peer slot split at 474). Session resets on battle clock reset. Optional **`SSB64_NETPLAY_FHASH_LIGHT_MISMATCH_TICK_MIN`** / **`_TICK_MAX`**. Compare host/client `fhash_light_pre` @473: if floor/contact fields match but `fhash_light_step` @474 differs, divergence is that sim step.

---

## Rollback ([`port/net/sys/netrollback.c`](port/net/sys/netrollback.c))

- **`SSB64_NETPLAY_ROLLBACK`** — Master rollback enable for env-gated init.
- **`SSB64_NETPLAY_ROLLBACK_INJECT_TICK`**, **`SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH`**, **`SSB64_NETPLAY_ROLLBACK_MISMATCH_DEBUG`**
- **`SSB64_NETPLAY_ROLLBACK_VERIFY_STRICT`**, **`SSB64_NETPLAY_ROLLBACK_LOAD_HASH_VERIFY`**
- **`SSB64_NETPLAY_ROLLBACK_MISMATCH_REMOTE_WITHOUT_PUBLISHED`**, **`SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH_PLAYER`**
- **`SSB64_NETPLAY_ROLLBACK_SNAPSHOT_FRAMES`** — Rollback snapshot ring depth (default **32**, clamp **1–64**). Independent of input history length.
- **`SSB64_NETPLAY_ROLLBACK_SYNCTEST=1`** — After each save, load `tick-1` and verify subsystem hashes (no rebind); `SYNCTEST_OK` / `SYNCTEST_FAIL` every 120 ticks.
- **`SSB64_NETPLAY_ROLLBACK_EPISODE_AUTHORITY=1`** (default on) — Symmetric follower executes initiator `ROLLBACK_SYNC` `(epoch, load, mismatch, target)` verbatim; `=0` restores legacy follower re-derivation (`follower_local_auth`, frontier clamp, `LOAD_TICK_ADJUST` mismatch shift).
- **`SSB64_NETPLAY_ROLLBACK_EPISODE_FSM`** — **Default on** (unset). Unified episode FSM (`SealInputs` → `AwaitingBaseline` → `Replay` → `Verify` → `Commit|Abort`); sealed replay + replay-log POST. Enables bidirectional **`SYNETPEER_PACKET_EPISODE_SEAL_ROWS`** (26): each peer sends locally-authoritative sealed input rows; `Replay` starts only after baseline match and all required peer seal rows received (retransmit with baseline). Set **`=0`** for legacy resim input path (bisect only).
- **`SSB64_NETPLAY_REMOTE_ANALOG_ONSET_PRED=1`** — When episode FSM is on (default), restores legacy optimistic `analog_onset_predict` for remote humans (bisect only). Default: off under FSM (authoritative wire/hold-last for sim + published history).
- **`SSB64_NETPLAY_LOCAL_PUBLISH_LOG=1`** — Log when local authority promotion writes non-neutral sticks into published history: `LOCAL_PUBLISH player=… source=transmitted|delay|latch`. Soak with `ROLLBACK_EPISODE_FSM=1` at stick onset (~930).
- **`SSB64_NETPLAY_AUTHORITY_PUBLISH_LOG=1`** — Alias: enables both `LOCAL_PUBLISH` and `REMOTE_PUBLISH` (remote: `source=wire_confirmed|hold_last`, plus `REMOTE_PUBLISH_SKIP` / `REMOTE_PUBLISH_LATE`). Use for @1700+ analog-onset soaks with `FRAME_COMMIT_DIAG=2`.
- **`SSB64_NETPLAY_SNAPSHOT_FIGHTER_DIAG=1`** — On `LOAD_HASH_DRIFT`, log `fighter_load_verify` (live vs slot `figh`/`anim`) plus per-player `fighter_slot` (`status`, `motion`, `fhash_light`). [`netrollbacksnapshot.c`](port/net/sys/netrollbacksnapshot.c)
- **`SSB64_NETPLAY_SNAPSHOT_FIGHTER_FIELD_DIFF=1`** — On `LOAD_HASH_DRIFT` when `figh` drifts, log named field deltas (`status`, `motion`, `mot_atk`, `vel_air_y`, top joint Y, `pos_prev_y`). [`netrollbacksnapshot.c`](port/net/sys/netrollbacksnapshot.c)
- **`SSB64_NETPLAY_SNAPSHOT_FIGHTER_CLEANUP`** — Post-verify fighter presentation when a rollback load commits (`syNetRollbackLoadPostTick`: apply → verify → presentation → rebind). Unset (default): **`syNetRbSnapshotSyncFighterPresentation`** — figatree resolve + rebind at `gobj->anim_frame` only (`ftMainRefreshFigatreeVisual`; no `ftMainSetStatus`, no motion event replay). Not run on synctest emergency load/restore. **`force`** / **`full`** / **`1`**: legacy bisect — run old `ftMainSetStatus` with preserve flags (reproduces pre-fix intro flicker / anim drift class). [`netrollbacksnapshot.c`](port/net/sys/netrollbacksnapshot.c)
- **`SSB64_NETPLAY_FIGHTER_SLOT_HASH_LOG=1`** — Per-player `fighter_slot_hash` on each `sim_state_tick` (`fhash_light`, **`fhash_full`**, **`anim_hash`**, **`camera_mode`**). Use with `NETSYNC_LOG_INTERVAL=1` or `SIM_STATE_TICK_INTERVAL=1`. Optional **`SSB64_NETPLAY_FIGHTER_SLOT_HASH_TICK_MIN`** / **`_MAX`** window (default: all ticks). [`netsync.c`](port/net/sys/netsync.c)
- **`SSB64_NETPLAY_RESIM_ANCHOR_PROBE=1`** — After rollback load, run one sealed sim step and log `RESIM_ANCHOR_PROBE` (ring vs live hashes at `load_tick+1`). Separates load poison vs replay poison. [`netrollback.c`](port/net/sys/netrollback.c)
- **`SSB64_NETPLAY_FRAME_COMMIT_DIAG=2`** — Adds `FRAME_COMMIT_STATE_DIVERGE_DETAIL` (commit-token `figh`/`world` vs live hashes at validation). [`netpeer.c`](port/net/sys/netpeer.c)
- **`SSB64_NETPLAY_SNAPSHOT_ITEM_DIAG=1`** — Save `item_count`/`truncated`; apply `ejected`/`matched`/`respawned`. [`netrollbacksnapshot.c`](port/net/sys/netrollbacksnapshot.c)
- **`SSB64_NETPLAY_ROLLBACK_RESIM_TICKS_PER_FRAME`** — Max authoritative sim ticks to replay per NetPeer update during catch-up (default **4**, max **32**).
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

- **`SSB64_MATCHMAKING_BASE_URL`** — [`mm_matchmaking.c`](port/net/matchmaking/mm_matchmaking.c). Default: `https://netplay.technicallycomputers.ca` (HTTPS proxy to the matchmaking API). Override for local/dev servers.
- **`SSB64_MATCHMAKING_CA_BUNDLE`** — PEM CA bundle path for libcurl TLS verification when the host store is unavailable (AppImage sets `CURL_CA_BUNDLE` / `SSL_CERT_FILE` automatically; this env wins if set).
- **`SSB64_MATCHMAKING_PUBLIC_ENDPOINT`**, **`SSB64_MATCHMAKING_BIND`**, **`SSB64_MATCHMAKING_LAN_ENDPOINT`** — [`scautomatch.c`](decomp/src/netplay/sc/sccommon/scautomatch.c). After match, bootstrap always tries reflexive **`peer`** (WAN/STUN path) first. **`peer_lan`** is tried only if that fails **and** local STUN **`udp_endpoint`** and opponent **`peer`** share the same IPv4 (`mmHostportWanIpv4Equal`).
- **`SSB64_MATCHMAKING_FORCE_PEER_LAN`** — Non-zero: dev override — try **`peer_lan`** before reflexive (ignores WAN comparison).
- **`SSB64_MATCHMAKING_LAN_INTERFACE`** — [`mm_lan_detect.c`](port/net/matchmaking/mm_lan_detect.c)
- **`SSB64_NETPLAY_AUTOMATCH_CONNECT_TIMEOUT_MS`** — Wall-clock budget for post-match P2P bootstrap on the automatch staging scene (default **60000**, clamp **5000–300000**). Expired or **B** during connect returns to character select (`nSCKindVSNetAutomatch`). Press **B** while searching/connecting on staging to cancel the queue ticket and return to CSS.
- **`SSB64_NETPLAY_SERVER_BOOTSTRAP`** — [`mm_server_barrier.c`](port/net/bootstrap/mm_server_barrier.c) (Linux)

---

## Scene / automatch dev ([`port/net/sc/scmanager.c`](port/net/sc/scmanager.c), [`scautomatch.c`](port/net/sc/sccommon/scautomatch.c))

- **`SSB64_START_SCENE`**, **`SSB64_SPGAME_STAGE`**, **`SSB64_SPGAME_FKIND`** — Dev scene / stage / fighter kind overrides (also in scautomatch for stage).

---

## Game loop (net-related) ([`port/gameloop.cpp`](port/gameloop.cpp))

- **`SSB64_NETPLAY_DECOUPLE_DISPLAY_SIM`** — Display vs sim decouple. When on during VS, `PortPushFrame` steps the sim at the negotiated barrier VI Hz and may skip posting VI / taskman for a wall frame; **`syNetPeerPumpIngressTransport("port_push")`** still runs on those skips (and once per push while decoupled) so UDP ingress does not stall with the sim.
- **`SSB64_NETPLAY_PUSH_FRAME_DIAG_MS`** — Push-frame diagnostic interval.
- **`SSB64_FREEZE_PACING`** — Pacing freeze hook (affects loop timing; can interact with net sessions).

---

## Persistence (not net-only, useful for net sessions)

- **`SSB64_SAVE_PATH`** — Override path for SRAM file (`port_save.cpp`); same tree as app data / config on many installs.

---

## Porting note

Paths above are under **BattleShip** `port/net/` (not `decomp/src/sys/…`). When moving netcode, keep the `SSB64_NETPLAY_*` names stable so scripts, CI, and player notes stay valid, or document a rename map in your fork.
