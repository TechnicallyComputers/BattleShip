# Hold-last soft onset floor + ahead invent → PEER (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** soak1 session `985824253` seed `2921151818` — Android client (lp=1) ↔ Linux host (lp=0)  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Replay:** `20260726_125304.ssb64r`  
**Bucket:** `REPLAY_DETERMINISM` / `PEER_SNAPSHOT_DIVERGE` (`agree_through_load=1`)

## Symptom

Post [seal dash-gate decay override](netplay_seal_dash_gate_decay_intent_override_peer_2026-07-26.md): prior signals gone (0× `INTENT_OVERRIDE` / `REPLACE_REJECT_DASH_GATE` / seal exhaust). Still PEER.

| Check | Result |
|-------|--------|
| Go `ADVANCE_FORCE` | OK @390 |
| NetSync `figh` | Match through **408**; sealed resim @409 and @412 heal **bit-identical** |
| Kill | Live figh DIFF by **413**; deepen exhaust @**415**/**417** (figh-only; map/world match) |
| Android end | `VS_SESSION_END` → Linux @416 |

## Timeline

| Tick | Detail |
|------|--------|
| 409 | First-pass P1 hold_last `(59,5)` vs owner `(15,1)` — seal resim heals (`post=0x32095EC5` both) |
| 412 | P0 correction episode — seals/digests match; resim post `0x4EC5C40B` both |
| **415** | Android soft onset: peek `(-33,13)` (ahead / later ramp) → floor invent `(-33,**20**)` published as hold_last |
| | Linux owner gameplay @415 was `(-11,8)`; @416 was `(-33,13)` |
| | GGPO: published `(-33,20)` vs remote `(-11,8)` → deepen PEER (inputs later agree through load) |

## Root cause

Near-neutral hold-last **soft onset** (send-lead helper from [micro deadband / onset peek](netplay_input_contract_micro_deadband_onset_peek_2026-07-17.md)):

1. **Ahead peek** — `TryPeekRemoteAnalogForOnset(..., max_lookback=0)` still scanned `tick … tick+peek_ahead`. When tick 415 had no analog row yet, it attributed tick **416**'s `(-33,13)` onto 415.
2. **Onset floor amplify** — `ApplyAnalogOnsetStick` floored `|sy|=13` → **20** (env mag clamped 8–20), inventing mag the owner never played.

Same floor class as [soft onset lookback FC](netplay_hold_last_soft_onset_lookback_release_fc_2026-07-18.md) (lookback already retired); residual was ahead mis-tick + floor on already-analog peeks.

## Fix (`port/net/sys/netinput.c`)

| Layer | Change |
|-------|--------|
| Soft onset peek | Current-tick analog wire only (`TryPeekRemoteAnalogStickAtTick`); no ahead invent onto this tick |
| Soft onset / last_nn | Copy sticks **verbatim** — remove `ApplyAnalogOnsetStick` floor amplify |
| Dead code | Drop unused onset-mag clamp helpers / `SSB64_NETPLAY_ANALOG_ONSET_STICK_MAG` reader |

Non-neutral hold-last ahead release/flip unchanged (still ring-pure yield).

## Acceptance

Analog ramp after near-neutral confirm (send-lead later stick in ring):

- No `hold_last_soft_onset` line with `sx/sy` ≠ peek (floor amplify gone)
- Soft onset does not publish a later tick's stick onto the current sim tick
- Soft GGPO vs true onset OK; 0× deepen-exhaust PEER from this invent class
- Sealed resim heal path still bit-identical when seals agree

Rebuild desktop **and** Android APK before re-soak.

**Residual soak `740113729`:** soft-onset invent cleared; PEER@408 from predicted hold_last micro-skip + missing tick mag follow — [`netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md`](netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md).

## Related

- [`netplay_hold_last_soft_onset_lookback_release_fc_2026-07-18.md`](netplay_hold_last_soft_onset_lookback_release_fc_2026-07-18.md) — lookback + floor (lookback fixed; floor residual here)
- [`netplay_input_contract_micro_deadband_onset_peek_2026-07-17.md`](netplay_input_contract_micro_deadband_onset_peek_2026-07-17.md) — soft onset origin
- [`netplay_seal_dash_gate_decay_intent_override_peer_2026-07-26.md`](netplay_seal_dash_gate_decay_intent_override_peer_2026-07-26.md) — prior soak family (cleared)
- [`netplay_hold_last_dash_clamp_jumpaerial_softlip_peer_2026-07-26.md`](netplay_hold_last_dash_clamp_jumpaerial_softlip_peer_2026-07-26.md) — invent clamp retired; hold-last ring-pure direction
- [`netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md`](netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md) — post-fix residual
