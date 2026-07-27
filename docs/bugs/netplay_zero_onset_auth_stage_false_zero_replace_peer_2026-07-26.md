# Zero-onset auth_stage false (0,0) → REPLACE revive → PEER (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** soak1 session `2141547652` seed `3897052782` — Android client (lp=1) ↔ Linux host (lp=0)  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `REPLAY_DETERMINISM` / `PEER_SNAPSHOT_DIVERGE` (`agree_through_load=1`)

## Symptom

Post invent-clamp removal ([JA SoftLip](netplay_hold_last_dash_clamp_jumpaerial_softlip_peer_2026-07-26.md)): **0×** `hold_last_smash_dash_clamp`, **0×** seal exhaust, still PEER.

| Check | Result |
|-------|--------|
| Go `ADVANCE_FORCE` | OK @390 |
| NetSync `figh` through @410 | Match |
| P1 `fhash_light` @411 | Fork — Linux `(45,-4)` vs Android owner `(0,0)` |
| SoftLip TopN | Match through gut **411**; first mismatch gut **412** |
| Kill | Deepen exhaust / `BASELINE_UNIVERSE` → PEER ~411–419; Linux `VS_SESSION_END` @421 |

Linux trail: wire 413 first confirmed `(0,0)` → `REPLACE_NEWER` to `(45,-4)` → 63× `REPLACE_REJECT_NEUTRAL_DOWNGRADE` keeping `(45,-4)` against owner `(0,0)` → invent `hold_release` @412 → SoftLip fork when owner smash-flips `(-73)`.

## Root cause

Three layers stacked:

1. **`auth_stage` false Strict zero** — During `zero_onset_stall` after resim, `StageLocalGameplayAuthForZeroOnsetStall` extended local gameplay through `sim+D` via `StoreLocalDelayFrameFromLatch`. That path read the **pre-sample HID latch** (often hard-zero before FuncRead). Android @409 minted History tick **411 = (0,0)** while P1 still held `(45,-4)` (STICK_SAMPLE@409).

2. **Bundle provisional wins** — `GatherHistoryBundle` appended send-lead **delay first**. Sample @410 filled `delay[411]=(45)`; `BundleHasWireTick` then skipped durable History `(0,0)` → later INPUT packets re-sent wire 413 as `(45,-4)`.

3. **REJECT blocked healing** — `REPLACE_REJECT_NEUTRAL_DOWNGRADE` (Go-onset smash→zero poison guard) kept `(45,-4)` forever against true owner `(0,0)` even after sim had reached that tick (`cur_tick ≥ DelaySim(wire)`).

`hold_last_hold_release` @412 was a **symptom** (invent seeing near-neutral after the poison), not the invent-layer root.

## Fix

| Layer | Change |
|-------|--------|
| Auth runway | Ahead of `GetTick()`, stage from **last gameplay hold**, not pre-sample latch |
| Egress bundle | History / wire-resend **first**; `AppendDelayed` only fills missing wire ticks |
| Wire / ledger REJECT | Hard-zero downgrade reject only while `current_tick < DelaySim(wire)` (sim still behind) |

## Acceptance

Kirby/Kirby Dream Land dual-stick after Go, mid-JA stick release → smash flip:

- No `REPLACE_NEWER` analog revive over a prior Strict hard-zero for the same wire tick from auth_stage latch
- True owner `(0,0)` release can REPLACE when sim has reached that tick (no REJECT storm)
- SoftLip / figh match through the release window (soft protocol GGPO OK)
- Go dual-stick onset: still reject smash→`(0,0)` poison while `cur_tick ≪ wire`

Rebuild desktop **and** Android APK before re-soak.

**Residual soak `68816755`:** auth_stage/REJECT family cleared (figh through 440); PEER@443 from seal INTENT_OVERRIDE on same-sign dash-gate decay — [`netplay_seal_dash_gate_decay_intent_override_peer_2026-07-26.md`](netplay_seal_dash_gate_decay_intent_override_peer_2026-07-26.md).

## Related

- [`netplay_zero_onset_predict_runway_peer_2026-07-20.md`](netplay_zero_onset_predict_runway_peer_2026-07-20.md) — auth_stage / HardStall v10
- [`netplay_wire_neutral_downgrade_dual_stick_onset_2026-07-20.md`](netplay_wire_neutral_downgrade_dual_stick_onset_2026-07-20.md) — REJECT origin (narrowed here)
- [`netplay_wire_resend_gap_restage_revision_2026-07-20.md`](netplay_wire_resend_gap_restage_revision_2026-07-20.md) — append-only wire / REPLACE_NEWER
- [`netplay_hold_last_dash_clamp_jumpaerial_softlip_peer_2026-07-26.md`](netplay_hold_last_dash_clamp_jumpaerial_softlip_peer_2026-07-26.md) — invent clamp removed; this soak was the residual
- [`netplay_feel0_send_before_sample_release_skew_2026-07-13.md`](netplay_feel0_send_before_sample_release_skew_2026-07-13.md) — history retransmission
- [`netplay_seal_dash_gate_decay_intent_override_peer_2026-07-26.md`](netplay_seal_dash_gate_decay_intent_override_peer_2026-07-26.md) — post-fix residual
