# Netplay: FC deepen clears exclusive-end coverage → hard-kill after heal

**Date:** 2026-07-28  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending) — Begin refuse **superseded**  
**Seed:** `2064608876` (Dream Land Ness ditto, Android guest / Linux host)  
**Follow-on soak:** `1079687931` — over-broad Begin refuse hung FC recovery (narrowed below)  
**Superseded Begin refuse:** [healed frontier honest FC](netplay_fc_healed_frontier_honest_fc_2026-07-28.md) (soak1 `313936707`: refuse hung Hold FC; Begin refuse removed, soft-continue kept for Live SoftLip only)  
**Related:** [exclusive-end baseline diverge post heal](netplay_fc_exclusive_end_baseline_diverge_post_heal_2026-07-28.md), [figh+cam mid-resim](netplay_fc_exclusive_end_baseline_figh_cam_mid_resim_2026-07-28.md), [SoftLip understage](netplay_softlip_understage_wall_passthrough_2026-07-28.md)

## Symptom

```text
FC@849 / FC@1014 inputs_agree=1 SoftLip JumpAerial topn_tx
Commit → Live (resolved_through=1015); FIGH_STALE_AGGREGATE_OK @1002
[Android] ROLLBACK_SYNC_SEND load=1004 target=1017 resolved=0
[Linux]   PEER_SYMMETRIC_FC_JOIN load=1004 resolved_through=1015
          resim begin peer_follower → deepen exhaust
          PEER_SNAPSHOT_DIVERGE load=1004 figh+cam (world/rng/map/anim match)
          — stopping VS session (load_tick 1004)
[Android] receives VS_SESSION_END
```

Prior exclusive-end Live ignore / stale-aggregate soft-continues held earlier in the same soak (`FIGH_STALE_AGGREGATE_OK` @1002, `LIVE_CAP_SKIP` on Android).

## Root cause

1. SoftLip JumpAerial soft-floor `topn_tx` kept producing `inputs_agree=1` FC after Commit through 1015.
2. FC deepen in `TryCommitCorrectionBegin` calls `ResetCorrectionEpisode()`, which zeros `EpisodeResolvedThrough`.
3. Exclusive-end soft-continue (`StaleAggregate`, Live `ignore_stale`, preemptive LIVE_CAP skip) keyed only on live `EpisodeResolvedThrough`, so coverage vanished for the re-opened load `1004` (`load+1 <= 1015`).
4. Mid-resim baseline disagreed on figh+cam → deepen exhaust → hard kill. Android wire already advertised `resolved=0` after its Begin.

Gameplay root (SoftLip X on Dream Land soft platforms) remains; this fix stops the protocol self-disarm / hard-kill wrapper.

## Fix

`PORT && SSB64_NETMENU`:

1. **`sSYNetRollbackHealedThrough`** — raised with `EpisodeResolvedThrough` on Commit / resim-complete; **not** cleared by `ResetCorrectionEpisode` (session reset only).
2. **`ExclusiveEndCoveredThrough()`** — `max(EpisodeResolvedThrough, HealedThrough)` for Live ignore, Arm ignore, preemptive LIVE_CAP skip, and StaleAggregate exclusive-end soft-continue.
3. **`TryCommitCorrectionBegin` Begin refuse** — originally refused `mismatch < HealedThrough`. **Removed** in [honest FC](netplay_fc_healed_frontier_honest_fc_2026-07-28.md): soft-continue raised heal past real Hold forks → `recovery_started=0` hang. Exclusive-end soft-continue via `ExclusiveEndCoveredThrough` remains for Live SoftLip; inputs-agree FC recovery uses honest figh compare.

### Narrowing (soak `1079687931`) — historical

First refuse keyed on `(load+1)<=HealedThrough` blocked frontier light/FC; narrowed to onset-strict, then removed entirely (honest FC doc).

## Verification

Re-soak Dream Land Ness ditto:

- SoftLip Live after Commit: `ignore_stale` / `FIGH_STALE_AGGREGATE_OK` may soft-continue exclusive-end baselines (not during inputs-agree FC recovery)
- Inputs-agree FC with onset behind prior heal must Begin or fail-closed — never `commit_covered_healed_exclusive_end` hang
- SoftLip `topn_tx` may still diverge — session must not hard-stop solely from exclusive-end re-open after deepen cleared live `resolved_through` while Live
