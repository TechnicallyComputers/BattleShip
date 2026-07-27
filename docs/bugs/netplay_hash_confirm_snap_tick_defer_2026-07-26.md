# Hash-confirm per-tick snap + soft-defer (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak); follow-on [suppress_wire_ggpo](netplay_hash_confirm_suppress_wire_ggpo_2026-07-26.md)  
**Bucket:** frame-commit / invent GGPO tax  
**After:** [snap-agree watermark](netplay_snap_agree_hash_confirm_watermark_2026-07-26.md)

## Symptom

Soak `seed=2833659564`: `snap_agree matched=501` (~80%) but `skipped_hash_confirm=0`. Remaining ≤12 ledger invent GGPOs still rewound at `sim_now≈tick+1..3`.

## Root cause

`hash_confirm` only consulted `LastFrameCommitStateAgreedTick > sim_tick`. That watermark advances when a peer snap-agree is **matched** (typically on recv). Ledger REPLACE often runs in the same / next sim step **before** the peer snap for `sim_tick` has been matched — even when the snap will match ~1 frame later. Peer-pending was also **consumed** on local complete, so REPLACE-time code could not peek peer hashes.

## Fix

1. **Peer-last ring** — every `SNAP_AGREE` recv stores non-consuming peer hashes; outcomes (match/mismatch) stamped per `snap_tick`.
2. **`syNetPeerSnapAgreeTryConfirmTick(snap_tick)`** — eager local+peer_last compare (advances watermark); used by StickReplace `hash_confirm` instead of watermark-only.
3. **Soft-defer** — on `LEDGER_REFRESH` invent REPLACE that is structurally hash_confirm-eligible but peer snap still unknown: arm `HASH_CONFIRM_DEFER` through `sim_tick + N` (default **2**, env `SSB64_NETPLAY_HASH_CONFIRM_DEFER_TICKS`) instead of immediate `QueueOrWiden`. Pump on snap recv / completed step / `syNetPeerUpdate`:
   - match → Promote-only (`skipped_hash_confirm++`)
   - mismatch or deadline → `QueueOrWiden`
4. One outstanding defer; supersede flushes prior tick to rewind.

Killswitches: existing `SSB64_NETPLAY_STICK_REPLACE_HASH_CONFIRM=0`, `SSB64_NETPLAY_SNAP_AGREE=0`; defer off with `HASH_CONFIRM_DEFER_TICKS=0`.

## Acceptance

Matched APK + Linux:

- `skipped_hash_confirm` / `class=hash_confirm` / `HASH_CONFIRM_DEFER_RESOLVE class=hash_confirm` > 0
- Remaining non-flip ≤12 ledger GGPO ↓ vs ~12–18 on short soaks
- `resim begin`/100 ↓ vs ~8.2
- No JA SoftLip PEER from hash_confirm (740113729 class)
- Snap mismatch still rewinds (no silent Promote on diverge)

## Related

- [`netplay_snap_agree_hash_confirm_watermark_2026-07-26.md`](netplay_snap_agree_hash_confirm_watermark_2026-07-26.md)
- [`netplay_hash_confirm_runway_align_2026-07-26.md`](netplay_hash_confirm_runway_align_2026-07-26.md)
- [`netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md`](netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md)
