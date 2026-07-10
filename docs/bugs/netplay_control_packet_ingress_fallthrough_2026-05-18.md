# Netplay control packet ingress fall-through (2026-05-18)

## Symptom

`FRAME_COMMIT_DIAG` showed `fc_sent>0` and `fc_recv=0` on both peers; `RESIM_POST_*` never logged despite B2 handshake code present.

## Root cause

`syNetPeerHandlePacket()` dispatched FRAME_COMMIT / RESIM_POST / ROLLBACK_SYNC in the control switch, but unmatched small packets (valid magic, non-INPUT size) **fell through** into the INPUT size gate and were dropped as `invalid_size`.

## Fix

After the control-packet switch, return early when the header is valid SSB64 but the datagram is not an INPUT bundle size.

## Related

- Analog prediction: forward wire peek + hold-last for non-digital remotes (`netinput.c`).
- RESIM_POST: completed-epoch digest retention + flush on idle (`netrollback.c`).
