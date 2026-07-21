# Owner gap-tick restage → wire self-revision (2026-07-20)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** soak1 session `369009235` seed `894538120` — Android client ↔ Linux host  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** PEER_SNAPSHOT@457 → PHYSICS_FORK@466 (soft recovery, MATCH UNSTABLE)

## Symptom

| Signal | Detail |
|--------|--------|
| Wire | `REMOTE_CONFIRMED_REPLACE_NEWER player=0 wire=456 old_pkt_seq=478 (-5,-22) → pkt_seq=479 (77,31)` — **same wire tick sent twice with different sticks** |
| Owner | Linux `LOCAL_PUBLISH` gap 453–454 (mid-resim); first-pass `STICK_SAMPLE@454 = (-5,-22)`, post-resim `= (77,31)` (live value at 455–456) |
| Refuse | `SEAL_OVERRIDE_REFUSE_INTENT` @450/454/460 fired correctly but kept the *original* wire value while owner had self-revised |
| Result | Peers ran 454–457 on different P0 sticks → PEER@457 deepen chain → `topn_tx` PHYSICS_FORK@466 |

Mirror on Android: `SEAL_PACK player=1 tick=450 src=history (27,7)` vs originally wired `(66,2)` — published History backfilled by later HID.

## Root cause

1. **Gap-tick restage from live HID** — after a resim, FuncRead restages ticks that had no `LOCAL_PUBLISH` (publish frozen during resim). `syNetInputStoreLocalDelayFrameFromLatch` had wire-lock protection only when a Transmitted/History row existed; gap ticks fell to `BuildLocalFrameFromLatch` (wall-clock-later HID) and rewrote the gameplay ring for the past tick.
2. **NoteTransmit lock leak** — the first send of the gap tick came from the provisional delay runway; `NoteTransmittedSimFrame` early-returns when the gameplay ring lacks the row ("send-before-sample"), so the transmitted ring never locked. The next bundle re-sent the same sim tick with the restaged value → peer `REPLACE_NEWER` revision.
3. Seal pack `src=history` faithfully exported the backfilled rows, so seal, wire, and confirmed all disagreed; every episode under dual-stick load re-triggered the storm.

## Fix

| Layer | Change | File |
|-------|--------|------|
| Egress append-only | `WIRE_RESEND_MISMATCH`: if a sim tick is already transmitted-locked with different gameplay, re-send the **locked** row | `port/net/sys/netpeer.c` `syNetPeerAppendInputFrameToBundleEx` |
| Gap restage | Restage of `sample_tick <= gameplay frontier` with no wire lock keeps the first-pass gameplay row; else `GAP_RESTAGE_HOLD_LAST` from nearest earlier row (≤8 back); live latch only as logged last resort | `port/net/sys/netinput.c` `syNetInputStoreLocalDelayFrameFromLatch` |
| Witness | `SEAL_PACK_HISTORY_SUSPECT` when a history-only seal row intent-disagrees with transmitted at t−1 (should vanish with the fixes) | `syNetInputCopyEpisodeLocalAuthoritySealFrame` |
| Getter | `syNetInputTryGetTransmittedSimFrame` exported for the egress guard | `netinput.c/h` |

## Acceptance (re-soak)

Dual-stick both peers:

- Zero `REPLACE_NEWER` where `old_pkt_seq`+1 == `pkt_seq` for the same wire tick (owner self-revision)
- `WIRE_RESEND_MISMATCH` / `GAP_RESTAGE_HOLD_LAST` may appear; `GAP_RESTAGE_LATCH_FALLBACK` and `SEAL_PACK_HISTORY_SUSPECT` should be zero or near-zero
- No PEER deepen-exhaust chain rooted at a tick whose owner published gap was restaged

## Related

- [`netplay_seal_local_intent_physics_fork_2026-07-20.md`](netplay_seal_local_intent_physics_fork_2026-07-20.md) — seal apply/refuse layer (this soak validated those guards)
- [`netplay_post_resim_wirelocked_hid_restage_2026-07-13.md`](netplay_post_resim_wirelocked_hid_restage_2026-07-13.md) — restage protection for wire-locked ticks (this bug is the no-lock gap case)
