# Netplay: input-authority fork → episode tuple fork → unilateral self-seal replay (fail closed)

**Date:** 2026-07-15
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)
**Soak:** session 197856492 (Linux host-client / Android guest-host), desync chain at ticks 861–868

## Symptom

Session ends in `PEER_SNAPSHOT_DIVERGE` with **no load-hash drift and no FC divergence** — just
"multiple resims, jumping around". Log signature (Android initiator):

```
resim begin epoch=3 mismatch_tick=862 load_tick=861
RESIM_BASELINE_MISMATCH universe state diverge load_tick=861 → deeper (inputs agree through load)
EPISODE_FSM reseal_deeper load=860 mismatch=861 target=864 span=3
EPISODE_SEAL_ROWS_WAIT load_tick=860 missing_slots=0x1        (hundreds of frames)
RESIM_SEAL_ROWS_TIMEOUT retry ... attempt=1..2
EPISODE_SEAL_ROWS_SELF_SEAL slot=0 filled=1 span=3
resim complete epoch=4 ...
received VS_SESSION_END tick=868
```

Linux meanwhile stays on the original tuple (`mismatch=862 span=2`), applies Android's 861 rows
as `COMPATIBLE_APPLY applied=2` (never covering tick 861 itself), and dies in
`RESIM_BASELINE_TIMEOUT load_tick=861 baseline_matched=0` → hard desync.

## Root cause — three stacked violations of "confirmed input = truth"

1. **Silent completed-sim publish rewrite (the fork seeder).**
   `syNetInputRefreshPublishedFromAuthorityLedger` stores the ledger row over published with no
   correction. When hold-last predicted a remote stick for an already-simulated tick and the
   confirmed wire row arrived one tick late (±1 delta), published was rewritten to confirmed
   truth but the state built from the guess was never rewound. Downstream, "inputs agree
   through load" passes while the universes are forked → `BASELINE_UNIVERSE_MISMATCH` classed
   as state-diverge, not input-diverge.

2. **Deeper restart widens the seal tuple backwards after seal rows were sent.**
   `syNetRollbackTryRestartResimAtDeeperLoad` moves `mismatch` to `deeper_load+1` and reseals
   (`reseal_deeper`). The peer stays on the original tuple, so the prefix row
   (`[new_mismatch, old_mismatch)`) can never arrive from the wire — the initiator waits on a
   row the peer will never send. Compatible-apply covers the overlap but not the prefix.

3. **Seal-rows timeout force-opened the gate over a live, disagreeing peer.**
   After retries the `SELF_SEAL_FALLBACK` (designed for the *peer-absent* FC-recovery case,
   `netplay_fc_recovery_seal_rows_peer_absent_2026-06-11.md`) sealed the missing row locally
   and replayed unilaterally, committing history the peer never agreed to.

## Fix (`PORT && SSB64_NETMENU`)

1. **`netinput.c` — completed-sim ledger refresh queues a correction.**
   In `syNetInputRefreshPublishedFromAuthorityLedger`, if the stored ledger row changes the
   gameplay of a valid published row for a tick the sim already consumed
   (`syNetInputGetTick() > sim_tick`, not resimulating), queue
   `syNetRollbackQueueOrWidenStickCorrection(player, sim_tick)` — regardless of deadband
   significance. Logs `LEDGER_REFRESH_COMPLETED_SIM_CORRECT` under the publish-log env.

2. **`netrollback_episode.c` — deeper reseal self-fills the prefix.**
   New `syNetRollbackEpisodeSelfSealPeerPrefixRows(from, to)`: after `reseal_deeper` widens the
   span, immediately fill peer-authority rows in `[new_mismatch, old_mismatch)` from
   wire-confirmed history (`syNetInputGetRemoteHistoryFrame`, strict/ledger only) and mark the
   peer seal ticks. Sound because the deepen precondition is "inputs agree through load".
   Missing-row masks then only cover ticks the peer will actually send. Non-resolving rows log
   `EPISODE_SEAL_ROWS_PREFIX_SKIP` and leave the timeout path to fail closed.

3. **`netrollback.c` / `netrollback_episode.c` — self-seal is peer-absent only.**
   New `syNetRollbackEpisodePeerSealActivitySeen()` (any seal chunk for the current epoch since
   `FsmBegin`). Both `SELF_SEAL_FALLBACK` sites in `syNetRollbackOnBaselineGateTimeout` now
   require it to be FALSE. If the peer is alive but sealing under a conflicting tuple, fall
   through to hold/hard-desync (`ArmPeerBaselineResync` / fail-closed) instead of force-opening
   the replay gate with a unilateral history.

## Verify

- Rebuild netmenu (`cmake --build build --target ssb64 -j 4`) — clean.
- Re-soak stick-heavy sessions (seed class 2369140563). Expect:
  - `LEDGER_REFRESH_COMPLETED_SIM_CORRECT` followed by a normal GGPO episode instead of a
    silent fork; no `BASELINE_UNIVERSE_MISMATCH ... inputs agree through load` seeded by ±1
    hold-last deltas.
  - After any `reseal_deeper`, `EPISODE_SEAL_ROWS_PREFIX_FILL` and no perpetual
    `EPISODE_SEAL_ROWS_WAIT missing_slots` on the prefix row.
  - `SELF_SEAL_FALLBACK` only when the peer sent zero seal chunks for the epoch.

## Still open (structural)

The end-state remains the strict input-authority migration
(`netplay_strict_input_authority_witness_2026-07-12.md`): confirmed-only commit with an
append-only per-remote-player ledger, retiring the replace/deadband heuristics entirely.
