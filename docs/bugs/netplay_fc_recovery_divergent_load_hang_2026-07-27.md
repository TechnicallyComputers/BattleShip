# Netplay — FC input-agree recovery hangs on divergent load ticks

**Date:** 2026-07-27
**Build:** netmenu (`SSB64_NETMENU=ON`)
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)
**Soak:** Android guest ↔ Linux host, Dream Land, seed `4126729879`
**Follow-on to:** [close wire-cap / diverge hang exit](netplay_close_wire_cap_diverge_hang_exit_2026-07-27.md),
[divergent load baseline stall](netplay_divergent_load_tick_baseline_stall_2026-07-12.md)

## Symptom

Light-resim storm (linux 162 / android 83 `local_light`, avg gap ~4t) from SoftLip-edge
fork @417 → permanent `SNAP_AGREE_MISMATCH` storm (matched≈27 / mismatch≈561).

FC@**1081** `state_diverge=1`, inputs agree, SoftLip onset=417. Recovery opens fat
episodes at **different loads**:

| Peer | `clamped_load` | `frontier` (logged) |
|------|----------------|---------------------|
| Linux | 1057 | 1057 |
| Android | 1047 | 1047 |

Baselines cross forever (`RESIM_BASELINE_RECV` foreign load), seals never complete
(`seal_rows_missing`), `baseline_matched=0`, `deeper_attempts=0`.
`RESIM_BASELINE_TIMEOUT` → `PEER_SYMMETRIC_CLAMP_RESOLVED 1048→1057` spam →
`VS_SESSION_END`. No `PEER_SNAPSHOT_DIVERGE` (never reached deepen exhaust).

## Root cause

1. **Asymmetric frontiers.** Light episodes independently advance each peer's
   `resolved_through` to the tip. `PickInRingFcInputAgreeLoad` used the **MAX**
   shared frontier, so when peer resolved lagged (or differed) each peer clamped to
   its **local** tip → divergent FC loads.

2. **Authoritative FC blocked negotiate.** FC recovery sets
   `AuthoritativeEpisodeActive`. `TryNegotiateResimLoadTickWithPeer` refused any
   rewrite of an authoritative load — so Linux @1057 receiving Android's @1047
   baseline never joined (`0× LOAD_TICK_NEGOTIATE`).

3. **No hang exit when seals cannot complete.** `DIVERGENT_LOAD_PROCEED` requires
   seals complete; divergent loads never seal. Timeout fell into clamp/resync spam
   instead of deepen-to-MIN or fail-closed abort.

## Fix

`PORT && SSB64_NETMENU`, `port/net/sys/netrollback.c`:

1. **`GetIntersectedCorrectionFrontier`** — MIN(local, peer) when both known.
   `PickInRingFcInputAgreeLoad` (and RING_CLAMP logs) use it so both peers prefer
   the same in-ring anchor when peer resolved is already visible. MAX shared
   frontier stays the GGPO clamp / seal-cancel authority.

2. **`LOAD_TICK_NEGOTIATE` during FC recovery** — allow negotiate when
   `FcStateRecoveryActive` even if `AuthoritativeEpisodeActive`. FC recovery load
   is locally ring-clamped, not a peer-locked tuple; MIN(local, peer) join is the
   shared contract. Live path: higher peer joins as soon as the foreign baseline
   arrives (existing call site before foreign early-return).

3. **`FC_RECOVERY_DIVERGENT_LOAD_{JOIN,WAIT,ABORT}`** — on baseline gate timeout
   during FC recovery with a foreign load and unmatched baseline:
   - Higher peer: negotiate/restart at MIN(local, foreign).
   - Lower peer (or join failed): wait one more timeout streak for the peer to join.
   - Streak ≥ 2: fail-closed session abort (`PeerSnapshotAbort`). Pure resim
     policy — no SoftLip/absorb context.

## Acceptance

- [ ] Re-soak dual builds after SoftLip-onset FC: expect matching `clamped_load` when
      peer frontiers known, or `LOAD_TICK_NEGOTIATE … fc_recovery=1` joining the lower
      load within one baseline exchange.
- [ ] No multi-hundred `PEER_SYMMETRIC_CLAMP_RESOLVED` hang after FC diverge; either
      recovery completes or `FC_RECOVERY_DIVERGENT_LOAD_ABORT` ends the session promptly.
- [ ] `WIRE_CAP` / light contract unchanged; this is FC recovery policy only.

## Follow-on

Soak `3849468025`: abort path worked, but join never ran (`deeper exhausted` + authoritative
Restart refuse) and light span-1 micro-storm remained. Fixed in
[netplay_fc_join_budget_light_wire_coalesce_2026-07-27.md](netplay_fc_join_budget_light_wire_coalesce_2026-07-27.md).
