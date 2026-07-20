# Episode proof layer certificates (observe-only)

**Status:** INSTRUMENTATION (`PORT && SSB64_NETMENU`) — no new fail-closed gates  
**Date:** 2026-07-19  
**Exemplar session:** soak `579327289` @4696 `PEER_SNAPSHOT_DIVERGE` (figh-only, inputs agree through load)

## Symptom / triage need

Long soak kills such as FallSpecial over Dream Land CLIFF soft-lip look like “desync” while protocol (confirmation, tuple, seal) was already certified. Without a layer label, triage chases wire/input bugs for **replay determinism** failures (and the reverse).

## Class model

| Bucket | Meaning |
| ------ | ------- |
| PROTOCOL | Input confirmation / tuple / seal / input-poisoned baseline |
| SNAPSHOT_FIDELITY | Load/restore ring ≠ live before replay advances (`LOAD_HASH_*`, synctest load fail) |
| REPLAY_DETERMINISM | Inputs agree through load; post-replay partitions diverge (FC MATCH, PEER after agree-through-load, RESIM_POST inp match) |

## What landed

- Always-on `EPISODE_PROOF event=begin|diverge` in [`port/net/sys/netrollback.c`](../../port/net/sys/netrollback.c) with `conf` / `earliest` / `snap` / `seal` / `agree_through_load` / `class=`.
- Observe-only `class=` tags on `LOAD_HASH_DRIFT`, `BASELINE_UNIVERSE_MISMATCH`, `RESIM_POST_DIVERGE`, `PEER_SNAPSHOT_DIVERGE`.
- `scripts/netplay-trim-logs.py --sync-report` prints `bucket=` on UNSTABLE (parses `class=` + heuristics for older logs).
- Contracts: [`docs/netplay_rollback_refactor_contracts.md`](../netplay_rollback_refactor_contracts.md) § Layer certificates.

## Exemplar (REPLAY_DETERMINISM)

Soak `579327289` @4696: Ness **FallSpecial (58)** over Dream Land CLIFF soft-lip TopN.x — `PEER_SNAPSHOT_DIVERGE` figh-only with inputs agree through load. Protocol certified → investigate sim/replay, not confirmation.

## Non-goals

No C32 / parallel overlay storage; no FallSpecial/CLIFF physics harden from this work; no new session-stop rules.
