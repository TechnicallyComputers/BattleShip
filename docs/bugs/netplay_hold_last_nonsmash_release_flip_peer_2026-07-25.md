# Hold-last non-smash release/flip ignored → PEER (2026-07-25)

**Status:** FIX ACCEPTED (`PORT && SSB64_NETMENU`) — PASS on soak `1907878962`  
**Soaks:**

| Session | Detail |
|---------|--------|
| `647084351` | Pre-fix: PEER@426 after zero-onset v10 Go invent PASS |
| `1907878962` | Post-fix: **PASS** — 0× PEER / BASELINE_UNIVERSE / `replay_determinism`; soft `class=protocol` only |

**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `REPLAY_DETERMINISM` / `PEER_SNAPSHOT_DIVERGE` (pre-fix); soft `PROTOCOL` (acceptance)

## Symptom

Zero-onset Go invent fixed (P0/P1 match through ~409). PEER @426 `inputs agree` / `class=replay_determinism`.

| Tick | Detail |
|------|--------|
| 413–414 | Android P1 owner release `(0,0)`; Linux hold_last `(50,4) pred=1` |
| 416 | Android P0 hold `(20,1) pred` vs Linux owner `(-82,11)`; GGPO flip late |
| 416 | Linux `zero_onset_stall` after P1 release → `last_conf` near-neutral |
| 427+ | P0/P1 light hash fork; deepen exhaust → PEER |

## Root cause

`syNetInputFillHoldLastSoftOnsetIfNeeded` only yielded to tick-wire **release** and only ran send-lead **ahead** release/flip when hold was smash-class (`|sx|≥56`).

1. Walk/dash-subthreshold hold `(50,4)` ignored owner `(0,0)` on the tick row.
2. After decay, smash often sat at `(20,1)` (below dash gate) so ahead flip to `(-82,11)` never ran.

Comment claimed “release → take wire” for all non-neutral hold-last; implementation was smash-gated.

## Fix

| Layer | Change |
|-------|--------|
| Tick wire | Near-neutral → take release for **any** non-neutral hold (`hold_release` / `smash_release`) |
| Tick wire | Opposite intent / dash-gate XOR → take for any analog hold (`hold_flip` / `smash_flip`) |
| Ahead peek | Release/flip ahead for any analog hold (`hold_*_ahead`); invent dash clamp later retired |

## Acceptance

Dual-stick after Go (walk release + smash flip):

- Linux does not keep walk hold_last through owner `(0,0)` on the same sim tick
- Decayed smash below dash gate still flips/releases from ahead wire before first-pass Turn/Dash
- Soft GGPO OK; PEER must not be seeded by non-smash hold_last ignore
- Zero-onset Go invent remains fixed (v10)

**`1907878962`:** PASS — `ADVANCE_FORCE` @390; Go sticks idle both sides; NetSync `figh` matched @120/240/360; fighter_slot_hash matched on 476/476 overlapping ticks except 4 brief soft-GGPO recoveries; `VS_SESSION_END` @516 with **0** PEER. Residual late `smash_flip` / protocol predict noise OK; invent `smash_dash_clamp` later retired ([JA SoftLip](netplay_hold_last_dash_clamp_jumpaerial_softlip_peer_2026-07-26.md)).

## Related

- [`netplay_hold_last_quasi_digital_smash_skip_peer_2026-07-25.md`](netplay_hold_last_quasi_digital_smash_skip_peer_2026-07-25.md) — soak `999197749`: QuasiDigital early-out skipped this whole path on gamepad smash
- [`netplay_zero_onset_predict_runway_peer_2026-07-20.md`](netplay_zero_onset_predict_runway_peer_2026-07-20.md) — Go invent; v10 PASS
- [`netplay_hold_last_dash_gate_send_lead_peer_2026-07-20.md`](netplay_hold_last_dash_gate_send_lead_peer_2026-07-20.md) — smash-only ahead path this extends
- [`netplay_analog_ramp_hold_last_jump_drift_2026-07-21.md`](netplay_analog_ramp_hold_last_jump_drift_2026-07-21.md) — D+1 ramp; do not hard-R dual-hot
