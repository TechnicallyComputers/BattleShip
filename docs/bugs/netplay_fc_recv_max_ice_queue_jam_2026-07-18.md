# Frame-commit > `PACKET_RECV_MAX` jams ICE recv queue (2026-07-18)

**Soak:** soak1 Linux + Android after post-Go wire grace. Appear/countdown OK; hang mid first Ness JumpAerial (~tick 518) with **no** `SYNCTEST_FAIL` / hash drift / GGPO.

## Symptom

- Both peers: `wire_need next_sim=518 hr=513 wire_need=514` forever.
- `FRAME_COMMIT_DIAG sent=1 recv=0` on **both** sides (first post-Go FC, validation ~510 after Wait-seeded cadence).
- Wall frames + local INPUT send continue; remote `hr` never advances.
- Bidirectional: neither peer receives the other's INPUT or FC after the first FC is queued.

## Root cause

1. **`SYNETPEER_FRAME_COMMIT_BYTES` = 392**, but `SYNETPEER_PACKET_RECV_MAX` was max(INPUT V7, seal) ≈ **338**.
2. ICE recv ring (`MM_ICE_RECV_MAX=2048`) accepted the 392 B FC.
3. `mmIcePopReceived(buf, RECV_MAX=338)` saw `head.len > out_cap` and returned **FALSE without dequeue** → queue head permanently jammed → all later INPUT/FC invisible → `hr` freeze → `wire_need` hang.
4. Unwrap fell through to `OsRecvFrom` on Pop failure, inflating `dropped` while the real datagrams sat in the ICE ring.

## Fix

1. Fold FC into `SYNETPEER_PACKET_RECV_MAX` (`netpeer.c`) so Pop capacity ≥ wire FC.
2. `mmIcePopReceived`: discard oversized head + log + continue (never jam forever).
3. ICE-active unwrap: empty Pop → would-block; do not fall through to raw UDP recv.

## Verify

Re-soak: `FRAME_COMMIT_DIAG … recv>0`; no permanent `wire_need` at ~518; gameplay past first Ness jump. Oversized discard log should be absent once RECV_MAX includes FC.

## Related

- [`netplay_post_go_wire_need_hang_2026-07-18.md`](netplay_post_go_wire_need_hang_2026-07-18.md)
- [`netplay_intro_wait_advance_frontier_deadlock_2026-07-18.md`](netplay_intro_wait_advance_frontier_deadlock_2026-07-18.md)
