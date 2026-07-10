# Netplay timebase: what is authoritative vs advisory

This note maps **wall / VI / exec-sync** machinery to **sim tick identity** so debugging does not confuse **ingress wire labels** with **real-time clocks**.

## Authoritative: simulation tick

After battle execution begins, the deterministic game step index is **`syNetInputGetTick()`** (`sSYNetInputTick`). It advances **only** via **`syNetInputAdvanceAuthoritativeSimTick()`** after a full battle update path that the stack accepts for that step (see `port/net/sys/netinput.c`: `syNetInputGetTick` / `syNetInputAdvanceAuthoritativeSimTick`).

**Rule:** nothing in the net layer should **rewrite** `sSYNetInputTick` from wall clock, VI bucket, or `hr` during normal forward play. **`syNetInputSetTick`** is for explicit rewinds (rollback resim) or session reset—not ongoing “alignment” to a peer clock.

## Not a wall clock: `hr` (`HighestRemoteTick`)

**`hr`** is **`syNetPeerGetHighestRemoteTick()`**: the maximum **`frame.tick`** seen on **inbound INPUT** staging (`syNetPeerStagePacketBundle`). That is a **logical wire / bundle label frontier**, not a high-resolution timer.

So a **stable gap** between **`sim_tick`** and **`hr`** (or between **`sim_tick`** and **`syNetPeerDelaySimTickFromWire(hr)`**) is usually **cadence / decouple / scheduling** (one peer runs more accepted sim steps per wall interval than the peer’s labels grow), not “timer drift between `D` and `hr`” in the literal sense.

See also: [`netplay_pacing.md`](netplay_pacing.md) (skew vs `HighestRemoteTick`).

## Committed delay `D`

**`D`** is **`syNetPeerGetCommittedInputDelay()`**: the match’s committed **wire delay**. It maps **sim tick → wire row** for bundles and strict ring checks (`wire_tick ≈ sim_tick + D` with saturating arithmetic). It is **not** a wall-clock offset; it is part of the **transport contract**.

## Wall / VI / exec-sync: how they are used

| Mechanism | Role w.r.t. `sim_tick` | Notes |
|-----------|-------------------------|--------|
| **`syNetPeerIsClockReadyForSimTick(sim_tick)`** | **Presentation / diagnostics only for phase-locked VS** | The phase-locked commit path no longer lets local VI phase decide authoritative tick ownership. Wall / VI sync can pace polling or presentation, but tick commit is owned by the shared wire frontier. `port/net/sys/netpeer.c` |
| **Battle exec sync** (`agreed_tick`, VI phase on wire) | **One-shot / ongoing phase latch** for **when** steps may proceed | Used by the clock gate above and by **admission bias clock leg** (below). Does not replace `syNetInputGetTick` as the step counter. |
| **Barrier / BATTLE_START_TIME** | **Session bootstrap** (release to running, deadlines) | Wall/VI for **sync pipeline**, not per-tick sim identity. |
| **Bootstrap ingress symmetry** (`SSB64_NETPLAY_BOOTSTRAP_INGRESS_SYMMETRY`, Linux UDP) | **Execution-ready bootstrap gate** | Enabled by default. After INPUT_BIND + exec sync, execution readiness remains false until this peer has sent an INPUT-shaped warmup and has received an inbound INPUT frontier (`hr > 0`). Warmup sends use the same wire layout as normal INPUT (including synthetic one-row gather when history is empty). |
| **`syNetPeerUpdateAdmissionWireBias`** | **Retired for phase-locked commit** | Receive-side lookup is now the same pure `sim + D` mapping used by senders. Local VI phase and observed `hr` do not reinterpret which wire row owns a sim tick. |
| **Shared commit gate (`syNetPeerEvaluateSharedCommitStep`)** | **Authoritative advance predicate** | Tick `T` may advance when exact remote wire row `T + D` is present, or when `T` is within `SSB64_NETPLAY_PHASE_LOCK_PREDICTION_TICKS` of the shared confirmed frontier. Missing input outside that window is admission **`R`** and stalls the battle step. |
| **Skew pacing (`K`)** | **Retired from authoritative placement** | Skew may still be logged historically, but it no longer suppresses authoritative battle updates or changes tick ownership. |
| **`SSB64_NETPLAY_DECOUPLE_DISPLAY_SIM`** (`PortPushFrame`) | **Cadence**: may skip running the game sim for a **display** frame | Skips sim work for that push; **does not decrement** `sim_tick`. Can create **persistent `sim_tick - hr`** asymmetry if one side pumps more **accepted** sim steps than the other’s **wire frontier** grows. `port/gameloop.cpp` |

## Debugging checklist (when “`D` vs `hr`” is reported)

1. **Confirm symbols:** is **`hr`** actually **`HighestRemoteTick`** in logs/diag, not a wall-clock field?
2. **Skew vs mapping:** is the gap **`tick - DelaySimTickFromWire(hr)`** (pacing) vs wrong **sim→wire** mapping (`DelayWireLookupTickFromSim`, admission bias)?
3. **Clock gate:** are **`W`** holds (`syNetPeerIsClockReadyForSimTick`) correlating with divergence, or only **`K`** / decouple?
4. **Decouple:** compare **`push`**, **`run_game_sim_tick`**, and **`tick_minus_hr`** across host/client logs for the same wall period.

## Related docs

- [`netplay_pacing.md`](netplay_pacing.md) — skew, EWMA gap pacing, decouple interaction.
- [`netplay_phase_lock.md`](netplay_phase_lock.md) — shared frontier and phase-locked placement invariants.
- [`netplay_environment_variables.md`](netplay_environment_variables.md) — `D`, strict slack, exec sync, decouple env.
- [`netplay_taskman_simtick.md`](netplay_taskman_simtick.md) — taskman vs sim tick.
