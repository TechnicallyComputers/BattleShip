# Netplay frame-commit checksum always fails (`fc_recv=0`)

**Date:** 2026-05-18  
**Status:** RESOLVED  
**Area:** `port/net/sys/netpeer.c` — `SYNETPEER_PACKET_FRAME_COMMIT` wire format

## Symptoms

- Soak with `SSB64_NETPLAY_FRAME_COMMIT_TOKEN=1`: `fc_sent` > 0, **`fc_recv=0`**, **`fc_compared=0`**
- `FRAME_COMMIT_RECV_DROP reason=checksum` on every datagram (session/header valid)
- `FRAME_COMMIT_DIAG … recv_drop_checksum=N` matches `fc_sent`
- Classifier still reports token starvation fallback (INPUT) — enforcement never engages
- RESIM_POST on the same build works (proves UDP path + generic checksum helper are fine)

## Root cause

`SYNETPEER_FRAME_COMMIT_BYTES` was **`56`** but the on-wire layout only serializes **52** bytes:

- 12-byte SSNP header (magic + wire + type + session)
- `validation_tick` + 8 token u32s (frame_id, input, slot_binding, tick_anchor, fighter, world, item, rng)
- 4-byte checksum

Send computed the checksum over `sizeof(buf) - 4` = **52 bytes before** writing the checksum field, but only **48 bytes** of payload had been written — bytes 48–51 were uninitialized stack garbage included in the sender hash.

Receive validated `size - 4` = **52 bytes** where bytes 48–51 are the wire checksum, so the recomputed hash never matched.

Same macro pattern as `SYNETPEER_RESIM_POST_BYTES (12 + 10×4) = 52`, which is why RESIM_POST checksums passed.

## Fix

1. Set `SYNETPEER_FRAME_COMMIT_BYTES` to `(12 + 4 + 4 + 4 + 4 + 4 + 4 + 4 + 4 + 4 + 4)` (= 52).
2. Send checksum over `(cursor - buf)` (payload only, before checksum write) — matches TIME_PING / UDP_SYNC pattern.

## Verification

Re-soak expecting:

- `recv_drop_checksum=0`
- `fc_recv ≈ fc_sent`
- `fc_compared > 0`
- Optional: `FRAME_COMMIT_COMPARE` lines when tokens pair
