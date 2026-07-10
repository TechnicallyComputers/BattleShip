# Netplay phase-locked execution

This note defines the replacement netplay execution model for Linux UDP VS. The old hybrid model let each peer ask "can I advance locally?" using a mix of exec gates, VI clock gates, skew pacing, and local ring admission. The phase-locked model makes tick ownership a shared contract instead:

```text
wire_tick = sim_tick + committed_input_delay
```

That mapping is the only authority for which input row belongs to a simulation tick.

## Invariants

- `syNetInputGetTick()` is the authoritative simulation index. It advances only after a completed battle update.
- The wire row for sim tick `T` is `T + D`, where `D` is the committed session input delay.
- Receive-side admission does not bias or reinterpret `T` based on local VI phase, local `hr`, or skew pacing.
- A peer may execute tick `T` when the exact remote wire row for `T` is present, or when `T` is inside the bounded prediction window behind the shared confirmed frontier.
- Missing input outside that prediction window is a stall, not a local catch-up or placement shift.
- Rollback may correct predicted input values, but it must not change which wire row owns a sim tick.

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
