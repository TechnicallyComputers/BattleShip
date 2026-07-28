# Netplay: FC join clamped to resolved_through → seal span mismatch

**Date:** 2026-07-28  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Seed:** `3404332722` (post seal epoch-skew compatible apply)  
**Related:** [seal epoch skew mismatch fork](netplay_seal_epoch_skew_mismatch_fork_2026-07-28.md), [shared correction frontier](netplay_shared_correction_frontier_2026-07-19.md), [ggpo behind resolved](netplay_ggpo_behind_resolved_through_seal_stall_2026-07-12.md)

## Symptom

Compatible seal apply worked (`stale_episode_tuple=0`, many `EPOCH_SKEW_COMPATIBLE`), then:

```text
[Linux]  initiator FC (1205,1230) load=1204 span=25 — wait missing_slots=0x2
[Android] PEER_SYMMETRIC_CLAMP_RESOLVED 1205->1218 (resolved_through=1218)
[Android] follower (1218,1230) span=12 — SEAL_ROWS_SEND count=12
[Linux]  COMPATIBLE_APPLY slot=1 count=12 applied=12 — still missing 1205–1217
[Linux]  RESIM_SEAL_ROWS_EXHAUSTED → hard desync → Android VS_SESSION_END
```

Prior FC recoveries (756 / 1074 / 1126) completed; kill was the clamped join.

## Root cause

Shared-frontier soft-clamp raises follower `mismatch` to `resolved_through` so ordinary GGPO can join. For FC state recovery the initiator keeps the deeper onset; follower seals only the shortened span → initiator never fills peer seal rows for the exclusive prefix.

## Fix

`PORT && SSB64_NETMENU`:

1. **`SYNETROLLBACK_SYM_NOTIFY_FLAG_FC_RECOVERY` (0x02)** — set when arming symmetric notify under `FcStateRecoveryActive`.
2. **`syNetRollbackPeerSymmetricFcJoinKeepOnset`** — TRUE when behind-resolved notify is FC (wire flag, local FC arm covering span, or matching deferred FC).
3. **`Accept` / `OnPeerSymmetricRollbackNotifyEx`** — skip `resolved_through` reject and `PEER_SYMMETRIC_CLAMP_RESOLVED`; log `PEER_SYMMETRIC_FC_JOIN_KEEP_ONSET` and join at peer onset/load.

Ordinary GGPO behind-frontier clamp unchanged.

## Verification

Re-soak Dream Land Ness ditto through FC escalate past a light `resolved_through`:

- Prefer `PEER_SYMMETRIC_FC_JOIN_KEEP_ONSET` / matching `(mismatch,target)` on both peers
- No `CLAMP_RESOLVED` that shortens an FC recovery span
- No `SEAL_ROWS_EXHAUSTED missing_slots=0x2` solely from follower clamp after initiator FC begin

## Follow-on (2026-07-28)

Seed `2824158137`: keep-onset worked, but `EPISODE_YIELD` cleared the FC arm before peer TryBegin → `commit_behind_frontier` / `dup_pending`. See [fc_yield_commit_behind_frontier](netplay_fc_yield_commit_behind_frontier_2026-07-28.md).
