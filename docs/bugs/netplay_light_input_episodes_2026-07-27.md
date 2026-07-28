# Light input episodes — local-only GGPO corrections (2026-07-27)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak) — wire-skip follow-on  
**Soak:** Android guest ↔ Linux host, seed `644226279` — no hang/desync but ~111 episodes, every ~6.5t under stick load  
**Follow-on soak:** seed `2135217895` / session `789761450` — light fired (`local_light`×2) but `RESIM_BASELINE_SEND` still ran → guest `BASELINE_PREEMPTIVE_LIVE_CAP` / `sym_reject_cap` freeze → host `strict remote MISS stall abort` @413  
**Bucket:** resim storm / episode protocol overhead / N-peer scaling  
**After:** [multistick correction union](netplay_multistick_correction_union_2026-07-27.md), [post-episode peer_convergence hang](netplay_stick_absorb_peer_convergence_post_episode_hang_2026-07-27.md)

## Problem

Every stick GGPO correction ran the **full cross-peer lockstep episode protocol**:

1. `BeginResim` armed a symmetric notify → the peer mirrored a `peer_follower` episode (soak: 55 initiator + 56 follower on Android — ~half of all episodes were mirrors).
2. Episode entered `AwaitingBaseline`: 72-byte digest exchange + `BASELINE_STASH_COMPARE` — a full RTT gate before a 2–4 tick replay.
3. Seal-rows exchange for the span; FSM `Verify` waited on the peer's `RESIM_POST` digest.
4. `BASELINE_PREEMPTIVE_LIVE_CAP` held live sim during gates → the felt rubber-banding.

Cost per correction: 2 resims (both peers), ~1–2 RTTs, ~6+ packets. With N peers each
mispredicting every remote stick, episode load grows ~N² and every episode blocks on the
slowest peer. All recent absorb/peer_convergence/ping-pong bugs were contention between two
peers' episode protocols. Classic GGPO does **zero** per-correction network coordination.

## Fix

**Light input episodes** (`SSB64_NETPLAY_ROLLBACK_LIGHT_INPUT_EPISODES`, default ON):

Pure GGPO input corrections — local initiator, no FC state recovery, no peer-symmetric
authority — resim **locally only**:

| Step | Heavy (before / recovery paths) | Light |
|------|--------------------------------|-------|
| Symmetric notify | Armed → peer follower episode | **Skipped** — peer never mirrors |
| Baseline digest gate | RTT wait + `STASH_COMPARE` | **Skipped** — phase goes straight to ForwardResim |
| Seal rows | Exchanged + FSM sealed reconcile | **Skipped** — reconcile from wire/ledger (legacy path) |
| Verify / `RESIM_POST` handshake | Waits peer post-digest | **Skipped** — close directly |
| Replay-log per-tick hashing | CollectHashes per replayed tick | **Skipped** |
| `EPISODE_EXEC` owner | `local_initiator` / `peer_follower` | `local_light` |

Kept: snapshot load clamps, anchor probes, deeper pre-load, frontier floor, commit
bookkeeping, post-resim promote/resync, absorb arming, `EPISODE_EXEC` logging.

**Absorb becomes merge-only (2 ticks)** while light is enabled — it only folds same-burst
REPLACEs into one span. Multi-stick widening/sticky refresh disabled (long windows only
added correction latency once episodes are cheap).

**Kept heavy:** FC state recovery, `BASELINE_UNIVERSE` deepen, peer-symmetric authority
revisions, and all `peer_follower` episodes — cross-peer agreement is the point there.

### Wire-skip follow-on (soak `2135217895`)

`ArmResimBaselineAfterLoad` calls `syNetPeerTrySendRollbackBaselineDigest()`
**synchronously**. The first light patch cleared `PeerBaselineSendPending` *after* Arm,
so digests still went out; the guest armed `BASELINE_PREEMPTIVE_LIVE_CAP` /
`sym_reject_cap` and froze until the host strict-MISS aborted the session.

Fix: set `sSYNetRollbackLightEpisodeActive` **before** Arm; Arm returns after collecting
diagnostic hashes with `RESIM_BASELINE_WIRE_SKIP` (no send / no seal rows);
`TakePeerBaselineDigestForSend` also refuses while light is active.

## Determinism story

Detection does not regress: per-tick 36-byte `SNAP_AGREE` + the FC validation grid compare
state continuously. A resim-determinism bug that per-episode baseline compare used to catch
now surfaces as FC/SNAP diverge → the (still heavy) state-recovery path heals it. Resim
inputs come from the ledger-confirmed store (`RESIM_INPUT_SOURCE` provenance); the
unconfirmed tail stays predicted and may re-correct later — normal GGPO.

## Scaling

Per peer, correction cost is now O(own mispredictions) — independent of peer count. No
cross-peer episode contention for input corrections, so the absorb ↔ peer_convergence hang
class is structurally impossible for them.

## Re-soak expectations

- `EPISODE_EXEC owner=local_light` for stick corrections; `peer_follower` only after FC/BASELINE events
- `RESIM_BASELINE_WIRE_SKIP light` on light loads; **no** `RESIM_BASELINE_SEND/RECV` around light episodes
- No `BASELINE_PREEMPTIVE_LIVE_CAP` / `sym_reject_cap` freeze after first stick onset
- Episode count roughly halves (no mirrors); corrections complete same-frame (`resim burst complete`)
- Rubber-band feel gone under single-stick hold; dual-stick smoother
- Watch for new `BASELINE_UNIVERSE`/`PEER` rate — if resim determinism was being masked by
  per-episode baselines, it will show up here (then fix the determinism bug, not the protocol)
- **Both peers must run this build** (Android APK + Linux) — an old peer that still wires
  light baselines can still poison a new idle peer via preemptive live-cap

## Kill switch

`SSB64_NETPLAY_ROLLBACK_LIGHT_INPUT_EPISODES=0` restores the full symmetric protocol for
all episodes (multi-stick absorb sizing returns automatically).

## Follow-on (2026-07-27 soak)

Removing the seal-row exchange broke the implicit contract "completed episode span ==
authoritative inputs": `resolved_through` / post-match overlap-clear / timeline reconcile
still treated hold_last-guessed span ticks as resolved, turning late wire into promote-only
input loss (permanent state fork @397). Fixed with a predicted-replay watermark — see
[netplay_light_episode_resolved_floor_pred_cap_2026-07-27.md](netplay_light_episode_resolved_floor_pred_cap_2026-07-27.md).
