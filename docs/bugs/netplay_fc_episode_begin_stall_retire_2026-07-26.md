# Retire FC episode Begin stalls — `fc_waiting_peer_episode` / `fc_symmetric_defers` (2026-07-26)

**Status:** FIX ACCEPTED (`PORT && SSB64_NETMENU`)  
**Soak (accept):** session `1065668144` seed `706657431` — 0× `fc_waiting_peer_episode` / `fc_symmetric_defers`; max span 6; soft-stable ~3027; MATCH UNSTABLE from Turn-Dash@414 / invent (not Phase 2)  
**Bucket:** `REPLAY_DETERMINISM` / protocol  
**Policy siblings:** [`netplay_ness_pk_defer_retire_input_resim_2026-07-26.md`](netplay_ness_pk_defer_retire_input_resim_2026-07-26.md), [`netplay_pause_defer_retire_input_resim_2026-07-26.md`](netplay_pause_defer_retire_input_resim_2026-07-26.md)

## Motivation

Ness/Pause TryBegin retirement: confirmed-input must Begin immediately; forever-wait + live advance grows predict. Remaining Class-B FC gates in `TryBeginDeferredStateMismatch`:

| Stage | Behavior |
|-------|----------|
| `fc_waiting_peer_episode` | Hard-fail while peer-sym pending/deferred (May 20 peer-priority forever-wait) |
| `fc_symmetric_defers` | `DeferFrameCommitForSymmetric` hold while overlapping peer-sym GGPO / in-flight peer-sym resim |

Peer priority is still correct **when peer-sym can Begin now**. Perpetual stall when it cannot is the anti-pattern.

## Fix (`PORT && SSB64_NETMENU`)

| Layer | Change |
|-------|--------|
| `fc_symmetric_defers` | `DeferFrameCommitForSymmetric` → always `FALSE` (active resim already via `fc_resim_busy`) |
| `fc_waiting_peer_episode` | Replace hard-fail with flush + `TryBeginResimFromPendingPeerSymmetric`; on success log `fc_peer_sym_prefer_began` and return; on failure fall through to FC (`fc_peer_sym_prefer_fallthrough` under defer diag) |
| Keep | `fc_resim_busy`, debounce/storm/commit, peer-sym live-cap @ mismatch−1, `EPISODE_YIELD`, pump order GGPO→flush→FC→flush→peer-sym |

Offline / non-netmenu PORT retains original wait + `DeferFrameCommitForSymmetric` body.

## Acceptance

Matched APK + Linux, normal VS (+ optional pause mash):

- No perpetual `try_begin_fail stage=fc_waiting_peer_episode` / `fc_symmetric_defers`
- Peer-sym notifies still yield `resim begin owner=peer_follower` without FC dual-span storms
- Spans short (≪ 20); lag/jitter out of scope
- If FC-vs-peer race → tighten prefer-Begin path; do **not** re-add forever wait

## Related

- [`netrollback_fc_authoritative_episode_2026-05-20.md`](netrollback_fc_authoritative_episode_2026-05-20.md) — peer priority now via immediate TryBegin, not FC forever-defer
- [`netrollback_symmetric_fc_dispatch_2026-05-19.md`](netrollback_symmetric_fc_dispatch_2026-05-19.md) — original `DeferFrameCommitForSymmetric` (superseded under netmenu)
- Phase 3: [`netplay_fc_commit_behind_frontier_deepen_2026-07-26.md`](netplay_fc_commit_behind_frontier_deepen_2026-07-26.md)
- Next: baseline echo readiness (Phase 4); Turn-Dash invent
