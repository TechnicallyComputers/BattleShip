# Netplay intro baseline preemptive live drift

**Date:** 2026-06-11  
**Status:** FIX SHIPPED (re-soak pending)  
**Soak:** soak1 Dream Land intro resim @230 (Yoshi P0 AppearL offset / Kirby offset family)

## Symptom

Follower (Android) shows fighter offset at bottom/side of stage on first intro resim. Logs show:

- `RESIM_BASELINE_RECV load_tick=229` **before** `ROLLBACK_SYNC_RECV mismatch=230`
- `RESIM_BASELINE_ECHO ring-only load_tick=229 sim=231`
- `LOAD_SLOT_LIVE_DRIFT live figh=0x508CB09A slot figh=0xA0F9812F`
- `rollback_load@229` pose is correct mid-Appear on both peers (not wrong GO spawn)

## Root cause

Host sends baseline digest at `load_tick` while follower live sim has already advanced (ingress + `syNetInputAdvanceAuthoritativeSimTick` not capped until `ROLLBACK_SYNC` arms `symmetric_reject_live_cap`). Baseline echo used **ring-only** apply (no live snapshot) when `ResimPending` was still false, so presentation stayed on tick 231 live pose until `BeginResim` loaded ring@229 — visible snap/pop.

`syNetInputRollbackSimAdvanceAllowed` also ignored rollback epoch / preemptive caps (only wire pacing), so follower could run 1–2 ticks past the anchor before sync arrived.

## Fix

| Item | Change |
|------|--------|
| Preemptive cap | `OnPeerBaselineDigest`: when `load_tick < sim` and no resim pending, arm `symmetric_reject_live_cap` at `load_tick+1` + log `BASELINE_PREEMPTIVE_LIVE_CAP` |
| Live echo apply | `TryEchoBaselineResponse`: when `sim > load_tick` and not in resim-pending load path, `syNetRbSnapshotLoad` + `RefreshPresentationForLoadedTick` (`RESIM_BASELINE_ECHO live_apply`) |
| Seal wait | `ShouldDeferInterfaceDuringResimWait`: defer interface+battle when preemptive cap active and `sim > cap` |
| Sim advance | `syNetInputRollbackSimAdvanceAllowed`: honor `syNetRollbackGetLiveSimCap` (epoch / symmetric caps) |
| GO diag | `BATTLE_GO_LOG=1`: `battle_go_fighter_slot` lines; full `fighter_detail` when `STATE_DETAIL_DIAG≥1` |

## Verify

Re-run soak1 Yoshi/Kirby intro. On follower expect:

- `BASELINE_PREEMPTIVE_LIVE_CAP load_tick=229 sim=231` (or cap before sim passes 229 on faster paths)
- `RESIM_BASELINE_ECHO live_apply` instead of ring-only when sim ahead
- No `LOAD_SLOT_LIVE_DRIFT` at baseline arm (live figh matches slot after echo)
- `battle_go_fighter_slot` at GO with matching `fhash_light` cross-peer
- After `resim complete`, stale host baseline retransmits must **not** re-arm cap: grep `BASELINE_PREEMPTIVE_LIVE_CAP_SKIP` or `BASELINE_PREEMPTIVE_LIVE_CAP_CLEAR`; sim must advance past resim target (no `rollback_epoch_hold sim=232 cap=229` freeze)

## Follow-up (2026-06-11)

Stale `RESIM_BASELINE_RECV` after epoch commit re-armed preemptive cap (`sim=232 cap=229`) and froze Android. Skip preemptive arm when `load_tick+1 <= episode_resolved_through`; purge stale cap in `PeerSymmetricRejectBlocksLiveAdvance`.
