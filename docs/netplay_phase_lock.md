# Netplay phase-locked execution

This note defines the replacement netplay execution model for Linux UDP VS. The old hybrid model let each peer ask "can I advance locally?" using a mix of exec gates, VI clock gates, skew pacing, and local ring admission. The phase-locked model makes tick ownership a shared contract instead:

```text
wire_tick = sim_tick + committed_input_delay
```

That mapping is the only authority for which **remote** input row belongs to a simulation tick. Local feel is separate: under NETMENU, committed `D` is **send/predict runway only** (not closed-loop local lag).

## Local ownership (NETMENU feel-0)

```text
HID@sample_t → GameplayRing[t]
HID@sample_t → SendLeadRing[t]  (+ hold-last provisional rows t+1…t+D)
gameplay@T    ← GameplayRing[T]          (= HID@T)     // feel delay 0
wire for T    = T + D                    // unchanged
```

- **Feel delay** = 0 (same resolve shape as the training lab at `D=0`).
- **Send lead** = negotiated / committed `D` (`MATCH_INPUT_DELAY` / RTT tiers). Staging hold-last-fills send-lead through `sample+D` so `hr` can lead intro Wait’s `DelaySim(hr)` frontier; real samples overwrite provisional ahead slots. Provisional ahead rows are **wire-only** (not transmitted/published authority). Do not relax the intro Wait gate.
- **Local authority / FC** = gameplay ring first under NETMENU (not Transmitted). Egress must not `NoteTransmitted` a send-lead row until gameplay for that tick exists; a prior provisional lock is realigned on sample. See `docs/bugs/netplay_feel0_fc_transmitted_skew_2026-07-11.md`.
- Remote path, shared commit admission, phase_lock prediction, and FC onset fixes are unchanged: the peer confirms or predicts remote rows behind the shared frontier.

See `docs/bugs/netplay_zero_delay_local_feel_2026-07-11.md`.

## Invariants

- `syNetInputGetTick()` is the authoritative simulation index. It advances only after a completed battle update.
- The wire row for sim tick `T` is `T + D`, where `D` is the committed session input delay (send lead).
- Local battle resolve for the local slot reads the **gameplay** ring at `T` (sample-aligned HID), not `HID@(T-D)`.
- Receive-side admission does not bias or reinterpret `T` based on local VI phase, local `hr`, or skew pacing.
- A peer may execute tick `T` when the exact remote wire row for `T` is present, or when `T` is inside the bounded prediction window behind the shared confirmed frontier.
- Missing input outside that prediction window is a stall, not a local catch-up or placement shift.
- Rollback may correct predicted input values, but it must not change which wire row owns a sim tick.
- On rollback sessions, runway-deficit / skew-lead lockstep holds must not preempt the prediction window (that punched HID sampling holes; see `docs/bugs/netplay_stick_r_stall_sampling_holes_2026-07-11.md`).
- After shared commit admits a tick (confirmed **or** prediction), FuncRead / battle republish must not re-impose skew R-holds or require strict-confirmed remote history for that tick — otherwise prediction is a no-op and the session soft-locksteps at `D` (see `docs/bugs/netplay_predict_gate_veto_lockstep_2026-07-11.md`). Speculative remote rows are hold-last tagged `RemotePredicted` until wire arrives; mismatch arms rollback.
- When frame-commit state diverges with **matching** input digests, reanchor from a **shared** published non-neutral onset (or last agreed), not each peer’s private predicted-usage flag — local flags fork recovery on analog onset (`docs/bugs/netplay_predict_fc_asymmetric_onset_2026-07-11.md`).
- Bootstrap with wire `D=0` must treat the first inbound remote frame (including wire tick `0`) as ingress-seen; `hr > 0` alone cannot clear the exec gate when the first labeled row is `0`.

## Runtime Frontier

The shared confirmed frontier is derived from peer connect-status state carried in INPUT packets plus the local inbound wire frontier:

- each peer exports the last confirmed sim tick per slot,
- the receiver combines peer-reported confirmation of its local slot with its own inbound remote frontier (`DelaySimTickFromWire(hr)`),
- commit admission uses the minimum available confirmation plus `SSB64_NETPLAY_PHASE_LOCK_PREDICTION_TICKS`.

This makes prediction a bounded speculative window on top of the same wire-to-sim contract, not permission for independent local cadence.

## Reclassified Local Systems

- VI / wall-clock gates may pace presentation or polling, but they do not decide tick ownership.
- Skew pacing no longer suppresses authoritative battle updates. If a peer cannot keep up, it stalls on the shared frontier or rolls back from prediction misses.
- Display/sim decoupling may pump ingress during skipped host frames, but skipped frames cannot publish or advance authoritative sim state independently.

## Diagnostics

Pair-diff logs should compare:

- `sim` / `tick`,
- `hr`,
- required wire row,
- `commit_gen`,
- prediction window,
- publish/hold path.

`commit_gen` increments when an accepted battle step advances the authoritative sim tick, giving host/client logs a direct logical progression marker.
