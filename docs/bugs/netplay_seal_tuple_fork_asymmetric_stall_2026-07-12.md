# Netplay: seal episode-tuple fork → asymmetric stall (2026-07-12)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptom (soak1 `152772770`, seed `358801690`)

After GGPO @505 and FC/map-drift resim, peers fork the episode mismatch while keeping the same epoch and target:

| Peer | Episode tuple | Outcome |
|------|---------------|---------|
| Android | `epoch=8 mismatch=502 target=509` | `SEAL_ROWS_WAIT missing_slots=0x1`, then live freeze at `completed_sim≈507` |
| Linux | `epoch=8 mismatch=504 target=509` | `SELF_SEAL_FALLBACK` → completes resim → advances to ~511, receives `VS_SESSION_END` |

Cross-seals were rejected as `stale_episode_tuple`. Baseline digests matched. Android then deepened on seal timeout (`SELF_SEAL_SKIP … not_wire_confirmed`), widening the mismatch fork.

Secondary noise: Android `SYNCTEST_FAIL` @394 (map hash never saved for 394; post-resim hashes matched). Not the hang.

## Root cause

1. **Strict episode tuple** — seal apply required exact `(epoch, mismatch, target)`. Same-epoch/same-target seals with overlapping absolute ticks were rejected when mismatch differed (deeper-load / walkback asymmetry).
2. **Seal-timeout deepen** — when baseline matched but peer seal rows were missing, timeout streak fell through to deeper-load, which moved `mismatch` further and made tuple agreement worse.
3. **Asymmetric self-seal** — the peer that held wire-confirmed remote history could `SELF_SEAL_FALLBACK` and finish; the initiator waiting on the other slot could not, and stalled while the peer resumed live.

## Fix

1. **`syNetRollbackEpisodeApplyCompatiblePeerSealRowsChunk`** — active FSM + same `epoch`+`target`, different `mismatch`: map peer rows by absolute sim tick into the local seal table (overlap only).
2. **Stale path** — compatible apply when sealed; compatible stash when not yet sealed; flush via `syNetRollbackEpisodePumpPendingPeerSealRows`.
3. **`syNetRollbackOnBaselineGateTimeout`** — after self-seal miss: pump pending → retry complete/self-seal → `HOLD_NO_DEEPEN` (retransmit) while streak allows; never deepen solely for seal-rows-missing with matched baseline; streak exhaust → hard desync (not deepen).

## Verify on re-soak

- Prefer `EPISODE_SEAL_ROWS_COMPATIBLE_APPLY` over `stale_episode_tuple` storms for same-target forks.
- Both peers advance past ~507 without one-sided live freeze.
- Android should not sit on `SEAL_ROWS_WAIT missing_slots=0x1` while Linux self-seals alone after a matched baseline.
- Logs may show `RESIM_SEAL_ROWS_HOLD_NO_DEEPEN` / `PUMP_UNBLOCK` instead of deeper-load on seal-only blocks.
