# Netplay KO lifecycle diverge instrumentation (2026-05-18)

**Status:** INVESTIGATION — instrumentation landed; root-cause fix pending bisect soak.

## Symptom

After ~90s of stable netplay (`rb=0` on NetSync checkpoints), the first GGPO rollback at a KO/respawn transition exposes latent divergence:

- `rng` and `item` hashes match
- `figh`, `world`, and `anim` diverge at `load_tick`
- Client may log `RESIM_BASELINE_ECHO_SKIP reason=no_snapshot` then `PEER_SNAPSHOT_DIVERGE`

Rollback control plane remains healthy (no baseline storm, no anchor walkback).

## Instrumentation added

| Tool | Env / log tag |
|------|----------------|
| World hash partition diff | `PEER_DIVERGE_DIFF`, `SSB64_NETPLAY_PEER_DIVERGE_DETAIL=1` |
| Full hash-set abort line | `wpn`/`map`/`cam` on `PEER_SNAPSHOT_DIVERGE` |
| Desync classifier | `peer_snapshot_diverge=` in `SSB64 DESYNC REPORT` |
| Per-tick hash bisect | `SSB64_NETPLAY_SIM_STATE_TICK_INTERVAL=1`, optional `HASH_TRANSITION_LOG=1` |
| gcRunAll traversal | `SSB64_NETPLAY_GC_TRAVERSAL_DIAG=2` → non-zero `gch`, `pairs=` |
| GObj eject ring | `GOBJ_EJECT_TRACE=1`, `RING_DUMP` on diverge |
| Baseline timing | `BASELINE_LOAD_CLAMP`, `BASELINE_ECHO_RETRY_DEFER` / `BASELINE_ECHO_RETRY` |

Soak preset: `scripts/netplay-ko-lifecycle-soak.env.example`

## Bisect workflow

1. Source soak env on both peers; play to KO/respawn.
2. Diff host/client logs at the same `tick=` for `sim_state_tick` / `gc_traversal`.
3. Find the **first** partition where values split while `rng`/`item` stay matched.
4. Read `PEER_DIVERGE_DIFF` at session end for world subfield (`spawn_wait`, `falls`, `game_status`, etc.).

## Hypothesis (unconfirmed)

Non-deterministic GObj lifecycle or battle-manager KO bookkeeping causes `world`/`figh`/`anim` drift without touching RNG. `spawn_wait` jump at rollback load is a leading signal.

## Next fix (after bisect)

Target the first diverging partition only — see `docs/netplay_rollback_test_matrix.md` KO lifecycle row.
