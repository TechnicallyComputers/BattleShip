# Netplay — BASELINE_LOAD_CLAMP behind shared frontier → seal hang

**Date:** 2026-07-19  
**Build:** netmenu (`SSB64_NETMENU=ON`), soak1 session `1734260705` seed `3254170758` (Android client ↔ Linux host)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)  
**Layer cert:** `EPISODE_PROOF class=protocol` (not REPLAY_DETERMINISM)

## Symptom

Near-immediate VS stop (~tick 400) after a healthy first GGPO:

| Step | Linux (initiator) | Android (follower) |
|------|-------------------|--------------------|
| Ep1 | GGPO slot1 `391→399`, complete, `resolved_through=399` | follower joins, complete |
| Ep2 arm | `GGPO … mismatch=399 target=401` then **`BASELINE_LOAD_CLAMP 398→395`** (`remote_hr=395`) | `LIVE_CAP_SKIP stale load=395 … resolved_through=399` |
| Ep2 begin | **`mismatch=396 load=395`** (behind frontier) | own episode **`mismatch=399 load=398`** |
| Seal | `missing_slots=0x2` until timeout | spam `PEER_SYMMETRIC_CLAMP_RESOLVED 396→399` |
| Kill | `RESIM_SEAL_ROWS_EXHAUSTED` → hard desync → `VS_SESSION_END` | receives end |

Drift scan **PASS** (no FC / LOAD_HASH_DRIFT). Not soft-lip / FC ring-clamp.

## Root cause

1. **HR clamp rewrote the episode behind the frontier** — `syNetRollbackClampLoadTickForPeerSend` pulled load to `remote_hr`, then BeginResim set `mismatch = load+1` (396) while `resolved_through=399`. Peer correctly clamped/skipped; initiator seal-waited on a key the peer refused.

2. **`FRONTIER_SEAL_CANCEL` never fired** — cancel used `syNetRollbackIsResimulating()`, which stays TRUE during AwaitingBaseline because `ResimDepth=1` after BeginResim. Three `RESIM_BASELINE_TIMEOUT` streaks then hard-desynced.

Same family as [`netplay_shared_correction_frontier_2026-07-19.md`](netplay_shared_correction_frontier_2026-07-19.md); this is the HR-clamp hole at the frontier edge.

## Fix

| Change | Behavior |
|--------|----------|
| **`BASELINE_LOAD_CLAMP_FRONTIER_FLOOR`** | When remote_hr would pull load below `resolved_through-1`, floor at `frontier-1` (unless FC state-recovery deepen). Keeps episode keys bilateral. |
| **`FRONTIER_BEGIN_FLOOR`** | After deeper-load walkback, bump ordinary GGPO mismatch/load up to the frontier if they landed behind. |
| **`FRONTIER_SEAL_CANCEL`** | Stop using `IsResimulating()`; treat active forward replay (gate open / FSM Replay+) as the only “too late” case. Sync legacy ticks; also cancel when load sits behind `shared-1`. |

## Verify

Re-soak rapid stick GGPO shortly after a completed correction (D=2, cross-ISA):

- Expect `BASELINE_LOAD_CLAMP_FRONTIER_FLOOR` (not load 395 with resolved=399)
- Ep2 keys match peer (`mismatch >= resolved_through`)
- No `RESIM_SEAL_ROWS_EXHAUSTED` → VS stop from this path
- If a behind-frontier episode still arms: `FRONTIER_SEAL_CANCEL` abandons Live (no hard desync)

## Related

- [`netplay_shared_correction_frontier_2026-07-19.md`](netplay_shared_correction_frontier_2026-07-19.md)
- [`netplay_correction_cascade_deepen_load_asym_2026-07-16.md`](netplay_correction_cascade_deepen_load_asym_2026-07-16.md)
- [`netplay_ggpo_behind_resolved_through_seal_stall_2026-07-12.md`](netplay_ggpo_behind_resolved_through_seal_stall_2026-07-12.md)
