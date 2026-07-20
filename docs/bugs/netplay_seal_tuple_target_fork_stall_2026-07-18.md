# Netplay: seal episode target-tuple fork → RESIM_BASELINE_TIMEOUT (2026-07-18)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptom (soak1 `2084493409`, seed `2997771566`)

Gameplay sim stayed matched for 4400+ ticks (`state_diverge=0`, drift scan PASS). Session aborted at tick ~4415 when Linux hit `RESIM_BASELINE_TIMEOUT` / `hard desync recovery` while Android completed epoch 20 and ran to tick 4423.

| Peer | Episode 20 tuple | Outcome |
|------|------------------|---------|
| Linux (host) | `mismatch=4415 target=4418` (after `tuple_align` widen) | `EPISODE_SEAL_ROWS_REJECT stale_episode_tuple pkt_target=4417 active_target=4418` → seal wait timeout |
| Android (guest) | `mismatch=4415 target=4417` (after `tuple_align` shrink) | `resim complete epoch=20` → live |

Trigger: overlapping GGPO corrections at the same mismatch tick — host corrected player 1 (`target=4417`, span 2), guest corrected player 0 stick release (`target=4418`, span 3). Both ran `tuple_align` from `ROLLBACK_SYNC` but converged on **different targets** because each side had already resealed locally.

Secondary wire noise: Linux kept sending `ROLLBACK_SYNC … epoch=19 target=4417` after `EPISODE_FSM begin epoch=20` / `tuple_align … target=4418` (notify armed pre-Begin with stale epoch; `ResimPending` blocked refresh).

## Root cause

1. **`syNetRollbackEpisodeApplyPeerSealRowsChunk`** only had a **mismatch-fork** compatible path (`same epoch + same target + different mismatch`). There was no **target-fork** path (`same epoch + same mismatch + different target`).
2. **`tuple_align` followed peer target blindly** — Android shrank `4418→4417` while Linux widened `4417→4418`.
3. **ROLLBACK_SYNC notify** was armed before `EpisodeBegin` (epoch N while FSM ran N+1) and could not refresh during resim (`ResimPending` early-return).

After Linux widened to `target=4418`, Android's seal rows stamped `target=4417` were rejected as `stale_episode_tuple` even though ticks `4415–4416` overlapped and baseline digests matched. Linux then rewound/deepened and stalled waiting for seal rows Android had already sent under the shorter tuple.

## Fix

### Compatible seal apply (primary recovery)

Extend `syNetRollbackEpisodeApplyCompatiblePeerSealRowsChunk` and the active-FSM stale bypass in `netrollback_episode.c`:

1. Accept compatible seals when **exactly one** of `{mismatch, target}` matches (XOR overlap).
2. For **target fork** (`same mismatch`, `pkt_target < local_target`): map peer rows by absolute sim tick into the local seal table for the shared prefix `[mismatch, pkt_target)`.
3. Log `fork=target` vs `fork=mismatch` on `COMPATIBLE_APPLY`.

### Follow-ups (prevent the fork + stale wire)

| Change | Location | Why |
|--------|----------|-----|
| **`tuple_align_keep_max_target`** — never shrink; always `max(local, peer)` | `syNetRollbackTryAlignActiveEpisodeTuple` | Stops opposite-direction reseal races |
| **Arm SYNC after `EpisodeBegin`** with live `sSYNetRollbackEpochId` | `syNetRollbackBeginResim` | SYNC epoch matches FSM epoch |
| **Allow notify refresh during resim** (drop `ResimPending` arm gate); refresh after reseal | `syNetRollbackArmSymmetricNotifyEx` + `RefreshSymmetricNotifyFromActiveEpisode` | SYNC tracks widened target / post-Begin epoch |
| **Export overlay** live FSM load/epoch/target when notify mismatch matches active episode | `ExportPeerSymmetricEpisode` / `ExportPeerSymmetricNotify` | Defense if cache lags |
| **`EPISODE_SEAL_ROWS_SHRINK_TO_PEER_PREFIX`** when peer sealed a complete shorter prefix | `syNetRollbackEpisodeTryShrinkTargetToPeerPrefix` | Unblocks seal-wait if a shorter peer tuple still arrives late |

## Verify on re-soak

- At epoch-20-style races: expect `tuple_align_keep_max_target` (no shrink) and/or `COMPATIBLE_APPLY fork=target`.
- `ROLLBACK_SYNC_SEND` epoch/target should match `EPISODE_FSM begin` / post-`tuple_align` tuple (not epoch N-1 / pre-align target).
- Both peers complete resim without `RESIM_BASELINE_TIMEOUT streak — hard desync recovery`.
- No regression on mismatch-fork soak (`COMPATIBLE_APPLY fork=mismatch` still fires for 502 vs 504 class).

## Related

- [`netplay_seal_tuple_fork_asymmetric_stall_2026-07-12.md`](netplay_seal_tuple_fork_asymmetric_stall_2026-07-12.md) — mismatch fork (same target)
- [`netplay_correction_cascade_deepen_load_asym_2026-07-16.md`](netplay_correction_cascade_deepen_load_asym_2026-07-16.md) — tuple_align deepen load
