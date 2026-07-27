# Hold-last smash_dash_clamp during JumpAerial → SoftLip PEER (2026-07-26)

**Status:** FIX DEEPENED (`PORT && SSB64_NETMENU`, re-soak) — invent-time dash clamp **removed** (not air-gated)  
**Soak:** soak1 session `2042477761` seed `1330670901` — Android client (lp=1) ↔ Linux host (lp=0)  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `REPLAY_DETERMINISM` / `PEER_SNAPSHOT_DIVERGE` (`agree_through_load=1`)

## Symptom

| Check | Result |
|-------|--------|
| Go `ADVANCE_FORCE` | OK @390 |
| NetSync `figh` @120/240/360/400 | Match |
| Soft corrections | Cascading `EPISODE_PROOF class=protocol` from `hold_last_smash_dash_clamp` |
| Kill | `BASELINE_UNIVERSE_MISMATCH` @420 → deepen exhaust → **figh-only** PEER (`map`/`world`/`rng` match); later map also forks |
| Not | `RESIM_SEAL_ROWS_EXHAUSTED` / `baseline_matched=1` seal hang — that is [`netplay_protocol_ggpo_seal_rows_exhaust_hang_2026-07-25.md`](netplay_protocol_ggpo_seal_rows_exhaust_hang_2026-07-25.md) |

P1 Kirby in JumpAerial (`status=26`) over Dream Land floor `fline=3` with SoftLip sticky `0x8000`. Session ends ~428 after Android closes.

## Timeline

| Tick / gut | Detail |
|------------|--------|
| ~404 | P1 enters JA; protocol GGPO begins (`smash_dash_clamp` / flip) |
| **412 first-pass** | Linux P1 `STICK_SAMPLE sx=-55 pred=1` (clamp); Android owner `sx=-67 pred=0` |
| **413 SoftLip** | First TopN.x fork: Linux `0xC435AE15` vs Android `0xC435CCCD` (vel_x also splits) |
| 416–418 | Soft GGPO rematches TopN briefly |
| 419–420 | TopN re-forks; baseline `class=replay_determinism` |
| 420 | Android: deepen exhausted — peer `figh=0x37E80CF9` vs local `0xE0D46891`, map `0x72F794DE` both |

## Root cause

`syNetInputClampSmashHoldLastBelowDashGate` rewrote predicted smash hold-last `|sx|→55` so invent could not arm a false ground Dash. That invent mutation is **gameplay-coupled** (Dash consumer bandage), not input-contract:

1. Host predicts hold_last from last smash (`-67`) → clamps to `-55`.
2. Owner keeps true smash stick (`-67`) → JA air DI / SoftLip sees different stick → TopN.x / vel_x diverge.
3. Wire later promotes; `agree_through_load=1`, but SoftLip residue → PEER.

An interim `ga == Air` skip was rejected as further env coupling. Correct layer: **stop invent-time magnitude rewrite**; keep wire/ahead release & flip and stick-vs-stick dash-gate XOR only. False Turn `did_dash` under predicted smash belongs to branch-eval / consumer paths.

## Fix (`port/net/sys/netinput.c`)

| Removed | Kept |
|---------|------|
| `syNetInputClampSmashHoldLastBelowDashGate` / `hold_last_smash_dash_clamp` | Tick / ahead release & flip |
| `syNetInputPlayerGroundedForDashClamp` (air skip) | Decay by lead |
| | Dash-gate **XOR between two stick samples** (yield / seal refuse) |
| | Strict same-gate tick wire → keep hold, skip ahead |

## Acceptance

Kirby/Kirby Dream Land dual-stick after Go, JA near floor CLIFF sticky:

- No `hold_last_smash_dash_clamp` lines (feature gone)
- SoftLipPhase `topn_x` matched peers through the JA window (or only soft protocol GGPO without figh PEER)
- 0× `BASELINE_UNIVERSE` deepen-exhaust PEER from this seed shape
- Ground dash-dance: soft `did_dash` GGPO OK if wire late; no invent clamp. Prefer branch-eval if Turn forks return hard

Rebuild desktop **and** Android APK before re-soak.

**Residual soak `2141547652`:** clamp gone (0× clamp lines) but PEER remained — root was zero-onset auth_stage false `(0,0)` + delay REPLACE revive, not invent. Tracked in [`netplay_zero_onset_auth_stage_false_zero_replace_peer_2026-07-26.md`](netplay_zero_onset_auth_stage_false_zero_replace_peer_2026-07-26.md).

## Related

- [`netplay_hold_last_dash_gate_send_lead_peer_2026-07-20.md`](netplay_hold_last_dash_gate_send_lead_peer_2026-07-20.md) — original clamp (now retired from invent)
- [`netplay_hold_last_quasi_digital_smash_skip_peer_2026-07-25.md`](netplay_hold_last_quasi_digital_smash_skip_peer_2026-07-25.md) — release/flip must still run
- [`netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md`](netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md) — SoftLip sticky (consequence surface)
- [`netplay_protocol_ggpo_seal_rows_exhaust_hang_2026-07-25.md`](netplay_protocol_ggpo_seal_rows_exhaust_hang_2026-07-25.md) — different kill (seal exhaust, 0 PEER)
- [`netplay_zero_onset_auth_stage_false_zero_replace_peer_2026-07-26.md`](netplay_zero_onset_auth_stage_false_zero_replace_peer_2026-07-26.md) — post-clamp residual
