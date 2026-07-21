# Hold-last smash through Turn allow → false `did_dash` (send-lead) (2026-07-20)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** soak1 session `179193526` seed `1918325247` — Android client ↔ Linux host  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `REPLAY_DETERMINISM` (scan FAIL; both peers soft-stable)

## Symptom

| Signal | Detail |
|--------|--------|
| Scan seed | `TURN_DASH_LR_DASH_FORK@464` P1 — **matched** `lr_dash=entry=-1`, **`did_dash` 0 vs 1** |
| Kill | No PEER/FC; soft recovery healed; early `VS_SESSION_END` ~517 |
| Input | Linux `REMOTE_PUBLISH@464 source=hold_last sx=-66`; wire `+28` via ledger **after** allow |
| Gate | InvertLR dash-out `(sx * lr_turn) ≥ 56`: Android `+28` → no dash; Linux `-66` → Dash |

July 19 `lr_dash` stomp: N/A (both kept −1). July 20 tick-only smash-flip: wire not in ring yet at first-pass (`hold_last_smash_flip` count 0).

## Root cause

1. **Resolve hold-last skipped prediction decay** — `MakePredictedFrameRemoteHuman` decays by lead; authoritative Resolve hold-last kept full `last_confirmed` smash mag through send-lead.
2. **Tick-only smash-flip insufficient** — when wire for `tick` has not entered the ring, predictor invents smash through Turn allow → false `did_dash`.
3. **Seal** — Resolve `(0,0)` still sealed while provisional ring/ledger later had nonzero (`SEALED_RESIM_LOAD_NEUTRAL@493`).

## Fix (`port/net/sys/netinput.c`)

| Layer | Change |
|-------|--------|
| Hold-last decay | Resolve path applies `ApplyAnalogPredictionDecay` by lead before soft-onset/smash logic. |
| Tick wire | Non-neutral hold-last yields on release, opposite intent, or dash-gate XOR (`|sx|` crosses 56 / smash sign flip). |
| Send-lead ahead | Smash-class (`|sx|≥56`) peeks `tick+1…peek_ahead` for release → `(0,0)` or flip (no lookback). |
| Dash clamp | Smash-class unless tick row is **strict**-confirmed same dash-gate: ahead release/flip, else clamp `|sx|` to 55. Provisional same-intent smash no longer skips clamp (soak `1272919275` / `857278917`). |
| Micro skip | Never `micro_stick` skip when dash-gate disagrees. |
| Seal | After Resolve near-neutral, prefer non-neutral provisional remote ring row. |

Logs: `hold_last_smash_flip`, `hold_last_smash_release`, `hold_last_smash_release_ahead`, `hold_last_smash_flip_ahead`, `hold_last_smash_dash_clamp` (needs `SSB64_NETPLAY_ANALOG_ONSET_LOG=1`).

## Acceptance (re-soak)

Dual-stick dash-dance + buttons, `SSB64_TURN_DASH_WITNESS=1`, onset log on:

- No first-pass `did_dash` disagree at allow with matched `lr_dash`
- Fewer smash `REMOTE_PUBLISH source=hold_last` rows that immediately GGPO to release/flip
- No `SEALED_RESIM_LOAD_NEUTRAL`
- Do **not** SoftLip-harden late physics

## Related

- [`netplay_wire_neutral_downgrade_dual_stick_onset_2026-07-20.md`](netplay_wire_neutral_downgrade_dual_stick_onset_2026-07-20.md) — REPLACE_NEWER smash→neutral + strict-only clamp skip
- [`netplay_hold_last_dash_dance_smash_flip_peer_2026-07-20.md`](netplay_hold_last_dash_dance_smash_flip_peer_2026-07-20.md) — tick-only opposite-sign flip
- [`netplay_turn_lr_dash_stomp_fc_2026-07-19.md`](netplay_turn_lr_dash_stomp_fc_2026-07-19.md) — InvertLR union stomp
- [`netplay_hold_last_soft_onset_lookback_release_fc_2026-07-18.md`](netplay_hold_last_soft_onset_lookback_release_fc_2026-07-18.md) — why lookback stays off
