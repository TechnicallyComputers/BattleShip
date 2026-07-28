# Netplay: snap_agree FC escalate minted but INPUT_SKEW_WAIT blocked recovery until ring eviction

**Date:** 2026-07-28  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Seed:** `1221028269` (post bilateral-onset soak)  
**Related:** [snap_agree FC escalate](netplay_snap_agree_mismatch_fc_escalate_2026-07-28.md), [bilateral state_agreed onset](netplay_fc_bilateral_state_agreed_onset_2026-07-28.md), [FC rebirth input skew](netplay_fc_rebirth_stick_drop_input_skew_2026-07-12.md)

## Symptom

Bilateral onset aligned (`local_agreed=peer_agreed=1571`), but session still fail-closed:

- `SNAP_AGREE_ESCALATE` from snap≈1590 with ~25 paired off-grid FC late mints through 1697
- Only one `FRAME_COMMIT_STATE_DIVERGE` — at **validation=1697**
- `FRAME_COMMIT_INPUT_AGREE_ONSET_UNRECOVERABLE` onset=1571 (`onset_load=1570 < min_load≈1572`, ring=128)
- Diag: `compared=36 state_diverge=1 recovery_started=0`

At first escalate mint (~1594) the same onset was still in-ring.

## Root cause

Escalate correctly forced FC token exchange, but `syNetPeerFrameCommitTryCompare` / `HandleFrameCommitStateMismatchCore` treat **any** input-digest disagreement as `INPUT_SKEW_WAIT` / `PENDING_GGPO` and refuse to arm FC state recovery (rebirth-stick race avoidance).

During analog play the FC authority window often disagrees cross-peer while snap_agree already shows a sustained figh fork. Light GGPO episodes (~24–32 after 1570) did not heal state. Escalate mints therefore compared, hit skew, returned — until inputs finally matched at 1697 and onset had aged out.

## Fix

When `snap_agree` mismatch streak ≥ escalate threshold (`syNetPeerSnapAgreeEscalateBypassInputSkew`):

1. **TryCompare** — log `FRAME_COMMIT_INPUT_SKEW_ESCALATE_BYPASS` and fall through to `OnPeerFrameCommitStateMismatch` instead of WAIT/PENDING return.
2. **Handle core** — same bypass at the arm gate; use bilateral `state_agreed` watermark as mismatch when digests still skew (histories may disagree); still set `FcStateRecoveryActive` / authoritative FC target.

Transient single-tick input skew without an escalate streak keeps the rebirth-stick WAIT behavior.

## Verification

Re-soak Dream Land Ness ditto:

- After `SNAP_AGREE_ESCALATE streak=8`, expect `INPUT_SKEW_ESCALATE_BYPASS` and/or `FRAME_COMMIT_STATE_DIVERGE` within a few ticks (not ~100 ticks later)
- `recovery_started=1`, onset in-ring, no `ONSET_UNRECOVERABLE` for forks born inside ring depth

## Follow-on (2026-07-28)

Seed `4025840110`: bypass worked (`recovery_started≈9–11`, 0× unrecoverable) but session died on seal `stale_episode_tuple` when Linux re-armed `(1981,2011)` vs Android follower `(2002,2011)` with epoch skew — [seal_epoch_skew_mismatch_fork](netplay_seal_epoch_skew_mismatch_fork_2026-07-28.md).
