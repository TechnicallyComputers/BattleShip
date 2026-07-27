# Hold-last quasi-digital smash skip → PEER (2026-07-25)

**Status:** FIX ACCEPTED (`PORT && SSB64_NETMENU`) — figh PEER PASS on soak `1845693596` (session kill → [seal exhaust hang](netplay_protocol_ggpo_seal_rows_exhaust_hang_2026-07-25.md))  
**Soaks:**

| Session | Detail |
|---------|--------|
| `999197749` seed `4278811958` | Pre-fix: PEER@417–423 after full smash hold_last through owner near-neutral |
| `1845693596` seed `3246477153` | Post-fix: **PASS for figh PEER** — 0× PEER / BASELINE_UNIVERSE / `replay_determinism`; soft `class=protocol` only. Session still aborted via seal-rows exhaust — [protocol GGPO seal hang](netplay_protocol_ggpo_seal_rows_exhaust_hang_2026-07-25.md) |

**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `REPLAY_DETERMINISM` / `PEER_SNAPSHOT_DIVERGE` (pre-fix); soft `PROTOCOL` (acceptance)

## Symptom

| Signal | Detail |
|--------|--------|
| Go / `ADVANCE_FORCE` | OK @390 |
| Soft GGPO | epoch0 mismatch=405 / epoch1@410 `class=protocol` recovered |
| Kill | `BASELINE_UNIVERSE_MISMATCH` → deepen exhaust → PEER @417–423 `figh` only, `inputs agree`, `class=replay_determinism` |
| End | `VS_SESSION_END` client @433 |

First-pass sticks (P1 remote on Linux):

| Tick | Android owner | Linux hold_last |
|------|---------------|-----------------|
| 400–404 | `(67,-12)` | `(67,-12)` confirmed |
| **405** | `(-5,0)` | **`(67,-12) pred`** `REMOTE_PUBLISH source=hold_last` |
| 406 | `(-76,-8)` | `(67,-12) pred` then resim |

Same Dash→JumpAerial (15→26) path; P1 light hash already splits @405. Soft GGPO realigns through ~410, then JumpAerial TopN.x SoftLip drift (load@419 `0xC422C520` vs `0xC422CF5D`) → PEER.

No `hold_last_smash_dash_clamp` / `hold_release` lines despite `TURN_DASH_WITNESS` (onset log auto-on).

## Root cause

`syNetInputFillHoldLastSoftOnsetIfNeeded` early-returned on `FrameIsQuasiDigitalKeyboard`.

`StickEncodingLooksDigital` treats dominant cardinals as keyboard:

```c
if ((ax >= 20) && (ay <= 14)) return TRUE;
```

Gamepad smash hold `(67,-12)` matches (`ax=67`, `ay=12`) even though `StickLooksAnalog` is TRUE. Soft-onset never ran → no tick-wire release to `(-5,0)`, no ahead flip to `(-76,-8)`, **no smash_dash_clamp to 55**. Full `|sx|≥56` survived send-lead into first-pass sim.

Prior nonsmash release/flip fix ([nonsmash](netplay_hold_last_nonsmash_release_flip_peer_2026-07-25.md)) never executed on this path.

## Fix

| Layer | Change |
|-------|--------|
| Soft-onset gate | Early-out only on **strict** `FrameIsDigitalKeyboard` (±85 axes), not QuasiDigital |

Gamepad smash / walk holds keep release / flip / dash-clamp. True keyboard ±85 still skips soft-onset.

## Acceptance

Dual-stick after Go (horizontal smash hold → near-neutral → opposite smash / jump):

- Linux first-pass must yield smash hold_last via release/flip when ring has rows (invent `|sx|→55` clamp retired — see [JA SoftLip](netplay_hold_last_dash_clamp_jumpaerial_softlip_peer_2026-07-26.md))
- Soft GGPO OK; PEER must not be seeded by quasi-digital skip of smash hold_last
- Zero-onset Go invent + nonsmash release remain fixed

**`1845693596`:** PASS for this family — `ADVANCE_FORCE` @390; NetSync `figh` matched @120/240/360; `smash_flip` / `hold_release` (and then-current clamp) fired; many `EPISODE_PROOF class=protocol` soft corrections; **0** PEER. `VS_SESSION_END` @465 is seal-rows exhaust (`baseline_matched=1`, `missing_slots=0x2`), not invent/hold_last PEER — tracked in [`netplay_protocol_ggpo_seal_rows_exhaust_hang_2026-07-25.md`](netplay_protocol_ggpo_seal_rows_exhaust_hang_2026-07-25.md).

## Related

- [`netplay_protocol_ggpo_seal_rows_exhaust_hang_2026-07-25.md`](netplay_protocol_ggpo_seal_rows_exhaust_hang_2026-07-25.md) — same soak session kill (OPEN)
- [`netplay_hold_last_dash_clamp_jumpaerial_softlip_peer_2026-07-26.md`](netplay_hold_last_dash_clamp_jumpaerial_softlip_peer_2026-07-26.md) — clamp *fires* mid-JA (opposite of skip); SoftLip PEER
- [`netplay_hold_last_nonsmash_release_flip_peer_2026-07-25.md`](netplay_hold_last_nonsmash_release_flip_peer_2026-07-25.md) — release/flip for any analog hold (skipped here)
- [`netplay_hold_last_dash_gate_send_lead_peer_2026-07-20.md`](netplay_hold_last_dash_gate_send_lead_peer_2026-07-20.md) — smash_dash_clamp
- [`netplay_zero_onset_predict_runway_peer_2026-07-20.md`](netplay_zero_onset_predict_runway_peer_2026-07-20.md) — Go invent OK on this soak
