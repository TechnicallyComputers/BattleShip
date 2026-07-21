# Seal latch pack + soft NearNeutral REPLACE reject → TURN_DASH (2026-07-20)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** soak1 session `582675261` seed `170377489` — Android host(client) ↔ Linux guest(host)  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** TURN_DASH@440 `did_dash` 1 vs 0 → PEER@475; diagnostics confirmed seal pack + soft_nz

## Symptom

| Signal | Detail |
|--------|--------|
| Scan | `TURN_DASH_LR_DASH_FORK` tick=440 P0 (host `did_dash=1` sx=-66 \| guest `did_dash=0` sx=11) |
| Seal dump | `SEAL_ROW` 435:(-66,-69) … 438:(-66,-69) — **435≡438** while owner first sample @435 was `(78,-6)` |
| Stomp | `SEALED_RESIM_LEDGER_SKIP` / `confirm_downgrade` @435 sealed=(-66,-69) over ledger=(78,-6) |
| Reject | `REPLACE_REJECT soft_nz=1` wire=470 keep=(63,25) reject=(4,5) `hard_zero=0 deadband=14` |
| End | PEER deepen-exhaust / session stop ~475 |

## Root cause

1. **Local-authority seal packing** — `CopyEpisodeLocalAuthoritySealFrame` preferred wire-locked / `ResolveLocalAuthority` without guarding against **live HID latch** invent on missing history ticks. At seal time for span `[435,439)`, tick 435 lacked a durable row and latched current HID (438’s smash `(-66,-69)`), duplicating the later flip onto the mismatch tick. Owner truth was still `(78,-6)`.

2. **Sealed resim kept the poison** — `SEALED_RESIM_LEDGER_SKIP` / Resolve sealed path outranked ledger even on opposite smash (dash-gate + sign disagree) → Android held `(-66,-69)` through Turn allow → `did_dash=1` while Linux had released to soft `(11,3)`.

3. **REPLACE_REJECT used NearNeutral(14)** — blocked real soft sticks `(4,5)` / `(10,5)` as if they were `(0,0)` poison (same class as soak `1981389058`).

## Fix (`port/net/sys/netinput.c`)

| Layer | Change |
|-------|--------|
| Wire / ledger reject | Smash→neutral refuse only on **hard zero** `(0,0)` (`syNetInputFrameSticksHardZero`). Soft NearNeutral sticks REPLACE/store. |
| Seal pack | Prefer gameplay when wire-locked disagrees on dash-gate / analog intent (`SEAL_PACK_PREFER_GAMEPLAY`). Never seal from live HID latch (`SEAL_PACK_SKIP_LATCH`). |
| Sealed resim | On dash-gate or opposite-intent sealed vs ledger: apply ledger (`SEAL_LEDGER_INTENT_OVERRIDE`) in Resolve + Publish. Soft same-intent mag noise still keeps seal + skip dump. |

## Acceptance (re-soak)

Dual-stick dash-dance with flips/releases both peers:

- No `SEAL_ROW` mismatch-tick duplicate of a later smash when gameplay/sample differs in sign or dash-gate
- `SEAL_PACK_PREFER_GAMEPLAY` / `SEAL_LEDGER_INTENT_OVERRIDE` when pack would have poisoned
- Zero `REPLACE_REJECT soft_nz=1` (rejects only `hard_zero=1`)
- No Turn `did_dash` asym on matched `lr_dash` after release ticks

## Related

- [`netplay_seal_local_intent_physics_fork_2026-07-20.md`](netplay_seal_local_intent_physics_fork_2026-07-20.md) — follow-up: local INTENT_OVERRIDE + history-over-tx pack (soak `1857971875`)
- [`netplay_wire_neutral_downgrade_dual_stick_onset_2026-07-20.md`](netplay_wire_neutral_downgrade_dual_stick_onset_2026-07-20.md) — original hard-zero poison class
- [`netplay_hold_last_dash_gate_send_lead_peer_2026-07-20.md`](netplay_hold_last_dash_gate_send_lead_peer_2026-07-20.md) — Turn allow / dash clamp
- [`netplay_seal_ledger_resim_stick_fork_2026-07-19.md`](netplay_seal_ledger_resim_stick_fork_2026-07-19.md) — seal vs ledger class
