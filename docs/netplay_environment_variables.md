# Netplay and netmenu environment variables

Reference for debugging and for porting this netcode to other decomp projects. Values are read with `getenv` / `std::getenv` unless noted. Empty or unset usually means “default off” or “use compiled defaults”; check call sites for exact parsing (`atoi`, truthy, level numbers).

**Precedence (input delay):** When `SSB64_NETPLAY_MATCH_INPUT_DELAY` is set (0–99), it defines the **linked** wire delay contract (floor/ceiling in netpeer for committed **`D`**). It overrides **`SSB64_NETPLAY_DELAY`** for that purpose. **Strict extra slack** (`SSB64_NETPLAY_STRICT_SLACK_FRAMES` and legacy aliases) is **independent**: it always comes from those envs via `g_NetInputDelayFrames` (clamped 0–4), not from the match delay integer. **Automatch:** `syNetPeer`’s **delay floor** (and committed `D`) start at `min(server input_delay, match_delay)` when match delay is set; the **ceiling** and final linked contract still use the match value once the host arms full `D` via `INPUT_DELAY_SYNC` after skew is within ±1 tick (guest commits on local **`sim + N`** ticks per **`SSB64_NETPLAY_DELAY_SYNC_COMMIT_LEAD_TICKS`** (default **2**) and then restores the floor to the match target). When match delay unset, use per-layer delay/slack envs for lab tuning.

**Linux vs Windows:** Matchmaking (`mm_*`), automatch UDP configure, and some barrier paths are Linux-oriented; Windows builds may stub or omit related sources.

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
| **`D` (committed delay)** | **`syNetPeerGetCommittedInputDelay()`** / **`syNetPeerGetInputDelay()`** — committed **wire** delay for the session (`sSYNetPeerInputDelay`). **`wire_tick = sim_tick + D`** for bundles / ring columns. | Set by **`SSB64_NETPLAY_DELAY`**, **`SSB64_NETPLAY_MATCH_INPUT_DELAY`** (linked; overrides delay env when **set**), automatch configure, **`INPUT_DELAY_SYNC`**, adaptive / starvation bump ramps. Feeds **`wire_base`**, **`wire_cap`**, delay-sync diag. |
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
| Strict confirmations | **`SSB64_NETPLAY_STRICT_INPUT_CONTRACT=1`**. |
| Pacing | Leave skew / stall envs at defaults until the baseline is stable; see [docs/netplay_pacing.md](netplay_pacing.md). |

**Diagnostics:** **`SSB64_NETPLAY_DELAY_SYNC_DIAG`** — `0` off (default), **`1`** first ~300 sim ticks after execution begin plus every tick where committed delay changes and logs when deferred delay applies; **`2`** every sim tick while VS is active (very noisy). Compare host vs guest `delay_sync_diag` lines for `sim`, `D`, `wire`, and remote ring presence. After the symmetric delay-sync change, **`INPUT_DELAY_SYNC` commits** are scheduled at **each peer’s** `syNetInputGetTick() + N` with **`N`** from **`SSB64_NETPLAY_DELAY_SYNC_COMMIT_LEAD_TICKS`** (default **2**, clamped **1–16**; saturating add); the `effective_tick` field on the wire is **not** compared to the guest’s sim tick (host and guest ticks are not comparable across machines).

**Optional input starvation (match-linked buffer):** With **`SSB64_NETPLAY_MATCH_INPUT_DELAY`** set and strict contract on, you can enable **`SSB64_NETPLAY_DELAY_SYNC_STARVATION_HANDLER`** so sustained `hr` below the **effective** admission wire row (`wire_eff` in `delay_sync_diag`: `min(max(sim+D, hr), sim+D+slack)`) latches a pause path (admission **`V`**, same partial-publish + scene suppress as strict **`R`**) until `hr` clears the starvation exit band (hysteresis on **`required_wire`**, and when **`EXIT_HR_LEAD` > 0** also **`hr` past `sim+D`**) for several frames—giving the remote ring time to refill without mutating `D`. Default **off** for backwards compatibility. Tune enter/exit counts, hysteresis, and **`EXIT_HR_LEAD_TICKS`** with the companion envs below (names must include **`DELAY_SYNC_STARVATION_`** — e.g. not `SSB64_NETPLAY_DELAY_SYNC_EXIT_FRAMES`); use **`DELAY_SYNC_DIAG`** / **`FRAME_COMMIT_DIAG`** to correlate `hr` vs `wire_cap` / `wire_eff` and `path=V` lines.

---

## Input, delay, strict contract ([`port/net/sys/netinput.c`](port/net/sys/netinput.c))

- **`SSB64_NETPLAY_MATCH_INPUT_DELAY`** — Integer 0–99. Match-mode **linked** wire delay **`D`** (floor/ceiling in netpeer). Does **not** set strict slack — use **`STRICT_SLACK_FRAMES`** separately. Overrides **`SSB64_NETPLAY_DELAY`** for committed delay when set. Read at VS session start and when netpeer reads match delay. **Full linked `D`** is committed via `INPUT_DELAY_SYNC` after startup skew is within ±1 tick (host sends; guest applies on **local** `sim + N` with the same **`SSB64_NETPLAY_DELAY_SYNC_COMMIT_LEAD_TICKS`** default as the host’s self-pending path). **Automatch configure** sets **`delay_floor` and initial `D` to `min(server input_delay, match_delay)`**; when the deferred match `D` is committed, **both** peers raise **`delay_floor`** to the match target so adaptive ramps cannot drop below the linked contract.
- **`SSB64_NETPLAY_MATCH_INPUT_BUFFER_MIN_SLACK_TICKS`** — Integer **0–32** (default **0** = off). Only when **`SSB64_NETPLAY_MATCH_INPUT_DELAY`** is active: strict admission additionally requires **`(hr - wire_base) >= B`** when **`hr > wire_base`** (`wire_base = sim + D`); when **`hr <= wire_base`** (lockstep tie or behind on labels), **B** adds no requirement—so tight lockstep does not deadlock. With **`STRICT_REMOTE_LEAD_BUFFER_TICKS`** (`lead_b > 0`), both the lead rule and the buffer rule must pass (**AND**). Ring checks still use the blended effective wire row. Symmetric: set the **same** `B` on host and guest. Lazy-parsed once per VS session; reset when `syNetPeerRefreshCachedNetplayEnvForNewMatch` runs. See `delay_sync_diag` **`buf_min_slack`** (`B`).
- **`SSB64_NETPLAY_STRICT_SLACK_FRAMES`** — Strict extra slack frames (0–4) for `wire_cap` / `syNetPeerGetStrictRequiredWireTick`. Legacy aliases: **`SSB64_NET_DELAY_FRAMES`**, **`SSB64_NETPLAY_INPUT_EXEC_DELAY_FRAMES`**. **Independent** of **`MATCH_INPUT_DELAY`** (match value does not copy into slack).
- **`SSB64_NETPLAY_STRICT_RING_FUZZ_TICKS`** — Integer **0–2** (default **0** = off). When **`required_wire > hr`** (effective frontier pinned to **`sim+D`** ahead of the peer’s highest staged wire tick), allow strict ring admission if **`hr + f >= required_wire`** and **`syNetInputHasRemoteInputForWireTick(..., required_wire - f)`** for some **`1 <= f <= value`**. Does **not** apply when **`required_wire <= hr`** (avoids admitting on stale rows while waiting for the true frontier cell). **Symmetric:** set the **same** value on host and guest. Lazy cache reset with `syNetInputRefreshCachedNetplayEnvForNewMatch`. **Desync risk** vs strict lockstep — enable only for lab / LAN tuning.
- **`SSB64_NET_DELAY_FRAMES`** — Legacy alias for strict slack frames (0–4), after `SSB64_NETPLAY_STRICT_SLACK_FRAMES`.
- **`SSB64_NETPLAY_INPUT_EXEC_DELAY_FRAMES`** — Legacy alias for strict slack frames, after `SSB64_NET_DELAY_FRAMES`.
- **`SSB64_NETPLAY_INPUT_PREDICTION`** — `0` disables last-input prediction for strict remote misses; default on.
- **`SSB64_NETPLAY_SOFT_STRICT_MISS_RESOLVE`** — Non-zero: when **authoritative wire contract** (`INPUT_CONTRACT` tier ≥ 1) sees a **strict R remote miss**, skip the partial-publish + “no sim advance” stall for that FuncRead and instead run full **`syNetInputSynchronizeInputsForTick`** so **`syNetInputResolveFrame`** may write **predicted** remotes into the wire-keyed ring (`syNetInputStoreRemotePredictedWireFromSimTick`). Only after **`syNetPeerCheckBattleExecutionReady()`** is true (input bind / battle exec sync / barrier as applicable) — during exec hold the legacy strict-R partial stall still applies so sim ticks do not run far ahead of the symmetric startup handshake. **Requires** prediction on (`INPUT_PREDICTION` not `0`); if prediction is off, behavior matches the legacy strict-R path. **Default off** (opt-in LAN / lab). **Symmetric:** use the **same** value on host and guest for deterministic lockstep. Cache reset per match with `syNetInputRefreshCachedNetplayEnvForNewMatch`. Rate-limited log: `soft_strict_miss_resolve` about every **60** sim ticks while active.
- **`SSB64_NETPLAY_INPUT_FUTURE_WIRE_TICKS`** — See **Diagnostics / slots / UDP** (append placeholder INPUT rows ahead on the wire); default **0**.
- **`SSB64_NETPLAY_INPUT_CONTRACT`** — Integer **0** / **1** / **2** (unset: use legacy **`STRICT_INPUT_CONTRACT`** — non-zero ⇒ tier **2**, else tier **0**). **0** = legacy FuncRead admission (exec / stall-until-remote / skew). **1** = delay-sync **lite** (effective-wire ring gate + partial publish on miss; skips match buffer **B**, `STRICT_REMOTE_LEAD_BUFFER_TICKS` **hr** folding, and delay-sync starvation **V**). **2** = full **strict** (same as **`STRICT_INPUT_CONTRACT=1`**). All tiers are **PORT** (including Windows); reset per match with `syNetInputRefreshCachedNetplayEnvForNewMatch`.
- **`SSB64_NETPLAY_STRICT_INPUT_CONTRACT`** — `1` selects tier **2** when **`INPUT_CONTRACT`** is unset; ignored when **`INPUT_CONTRACT`** is set. Legacy name for full strict.
- **`SSB64_NETPLAY_STRICT_REMOTE_LEAD_BUFFER_TICKS`** — Integer **0–16** (default **0**). With strict VS, after remote ring cells exist for the **effective** required wire tick (`wire_eff` / `min(max(sim+D, hr), sim+D+slack)` after startup grace), admission also gates on **`HighestRemoteTick` (`hr`)**: require `hr >= required_wire + min(B, max(0, hr - required_wire))` (i.e. **`hr >= required_wire`**, and when the peer is ahead on the wire by slack ticks, up to **B** of that slack is folded into the threshold). **Use the same value on host and guest** (does not change committed wire delay `D`; pairs with `SSB64_NETPLAY_DELAY`). Cache reset with `syNetInputRefreshCachedNetplayEnvForNewMatch`. See `delay_sync_diag` fields `wire_base`, `wire_cap` (strict ceiling), `wire_eff`, `lead_b`, and `hr_need`.
- **`SSB64_NETPLAY_PREDICT_NEUTRAL`** — VS session: neutral prediction policy when on.
- **`SSB64_NETPLAY_LOG_LOCAL_INPUT`** — Guest-side local input logging (rate-limited).
- **`SSB64_NETPLAY_ABORT_ON_INPUT_MISMATCH`** — Bitmask: bit 1 NetSync validation, bit 2 rollback pre-resim; logs mismatch (hard `abort` only if fatal env set).
- **`SSB64_NETPLAY_ABORT_ON_INPUT_MISMATCH_FATAL`** — Non-zero: after logging mismatch, call `abort()` (for CI / bisect).
- **`SSB64_NETPLAY_STALL_UNTIL_REMOTE`** — Stall-until-remote path; cached.
- **`SSB64_NETPLAY_INPUT_PREDICT_DIAG`** — Level 1/2 predicted-remote diagnostics.
- **`SSB64_NETPLAY_FRAME_COMMIT_DIAG`** — Admission path logging level.
- **`SSB64_NETPLAY_FRAME_COMMIT_SUMMARY`** — Frame-commit summary logging.
- **`SSB64_NETPLAY_STRICT_R_STUCK_FORCE_DIAG`** — With strict slack 0, force advance after sustained strict-R miss (diagnostic).
- **`SSB64_NETPLAY_DELAY_SYNC_DIAG`** — Delay / wire alignment trace (`0` / `1` / `2`); see **Delay Sync Test Preset** above.
- **`SSB64_NETPLAY_DELAY_SYNC_COMMIT_LEAD_TICKS`** — Integer **1–16** (default **2**). How many **local** sim ticks after the decision to queue a change before **`INPUT_DELAY_SYNC`** / host delay-ramp commits take effect (`syNetInputGetTick() + N`, saturating). Startup skew **`D`** alignment and starvation adaptive bumps use the same lead. Larger **`N`** defers committed wire delay changes (less aggressive “next boundary” apply). Lazy cache reset when `syNetPeerRefreshCachedNetplayEnvForNewMatch` runs (re-read per match).
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
- **`SSB64_NETPLAY_BARRIER_CLOCK_STALL_DIAG`** — Linux UDP: **`1`** logs every **300** `BattleBarrierWaitFrames` while the host awaits a **TIME_PONG** during the clock-sync sample loop (no sample progress). Off by default.

**Running clock sync (steady match, Linux UDP, host-led)**

- **`SSB64_NETPLAY_RUNNING_CLOCK_SYNC_MS`** — Interval between host **TIME_PING** rounds using the high-bit seq flag (**`0`** = off). Default **3000** ms; clamped **0…600000**.
- **`SSB64_NETPLAY_RUNNING_CLOCK_BIAS_CLAMP_NS`** — Max nanoseconds applied per median-delta correction to `port_add_vs_decouple_barrier_latch_bias_ns`. Default **500000** (0.5 ms); clamped **0…10000000**.
- **`SSB64_NETPLAY_RUNNING_CLOCK_BIAS_MIN_GAP_MS`** — Minimum wall time between bias applications. Default **500** ms; clamped **0…60000**.

**Strict admission + running confidence (host only)**

- **`SSB64_NETPLAY_STRICT_RUNNING_CONFIDENCE_FUZZ`** — **`1`**: when the host’s running offset ring is **valid** (≥4 samples) and **spread** ≤ **`SSB64_NETPLAY_STRICT_RUNNING_CONFIDENCE_SPREAD_MAX_MS`**, add **+1** tick to the strict ring fuzz window (`syNetPeerIsRemoteInputReadyForSimTickEx`). **Guest ignores** (host-only ring). Off by default.
- **`SSB64_NETPLAY_STRICT_RUNNING_CONFIDENCE_SPREAD_MAX_MS`** — Max offset spread (ms) for the running-confidence fuzz path. Default **25**; values **< 0** are treated as **0**.

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

- **`SSB64_NETPLAY_DECOUPLE_DISPLAY_SIM`** — Display vs sim decouple. When on during VS, `PortPushFrame` steps the sim at the negotiated barrier VI Hz and may skip posting VI / taskman for a wall frame; **`syNetPeerPumpIngressTransport("port_push")`** still runs on those skips (and once per push while decoupled) so UDP ingress does not stall with the sim.
- **`SSB64_NETPLAY_PUSH_FRAME_DIAG_MS`** — Push-frame diagnostic interval.
- **`SSB64_FREEZE_PACING`** — Pacing freeze hook (affects loop timing; can interact with net sessions).

---

## Persistence (not net-only, useful for net sessions)

- **`SSB64_SAVE_PATH`** — Override path for SRAM file (`port_save.cpp`); same tree as app data / config on many installs.

---

## Porting note

Paths above are under **BattleShip** `port/net/` (not `decomp/src/sys/…`). When moving netcode, keep the `SSB64_NETPLAY_*` names stable so scripts, CI, and player notes stay valid, or document a rename map in your fork.
