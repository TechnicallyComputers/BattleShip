# Wire REPLACE_NEWER smash→neutral downgrade at dual-stick Go (2026-07-20)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** soak1 session `857278917` seed `1190441969` — Android client ↔ Linux host  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** soft-stable MATCH; scan FAIL was mostly diagnostic + wire poison mid-GGPO

## Symptom

| Signal | Detail |
|--------|--------|
| Sync-report | `MATCH: STABLE (soft recovery)` — figh hashes aligned on 390 overlapping ticks |
| Scan FAIL | `RESIM_STICK_FORK` @392–398 — Android STICK_SAMPLE always `(0,0)` vs Linux smash |
| GGPO | Android predicted remote P0 `(0,0)` @392–394; wire smash arrived → correction storm |
| Poison | `REMOTE_CONFIRMED_REPLACE_NEWER` + `STRICT_INPUT wire_overwrite` @cur_tick=389: ticks 394–401 smash → `(0,0)` from newer `pkt_seq` |
| End | Android `VS_SESSION_END` @399 mid-recovery (not PEER / TURN_DASH) |

Dual joystick at Go felt like an instant desync; peers never hard-diverged before quit.

## Root cause

1. **First remote smash under send-lead** — Android invented predicted `(0,0)` for Linux P0; GGPO when wire landed (expected onset cost).
2. **Neutral downgrade via REPLACE_NEWER** — while sim was stuck behind the frontier, a newer packet rewrote already-strict-confirmed analog rows to near-neutral, erasing the wire GGPO was trying to apply (`deferred_queue_drop` / `CORRECTION_CLAMP_EARLY_WIRE` storm).
3. **STICK_SAMPLE blind spot** — samples read `gSYControllerDevices` only; Android devices stayed zero while `LOCAL_PUBLISH` / history had sticks → false `RESIM_STICK_FORK`.
4. **Clamp hole (related, prior soak `1272919275`)** — same-intent *provisional* smash in the ring skipped `smash_dash_clamp`, keeping `|sx|≥56` through Turn allow.

## Fix (`port/net/sys/netinput.c`)

| Layer | Change |
|-------|--------|
| Wire commit | Reject `REPLACE_NEWER` when existing is strict analog, incoming near-neutral, buttons unchanged — log `REMOTE_CONFIRMED_REPLACE_REJECT_NEUTRAL_DOWNGRADE`; still ack `pkt_seq`. |
| Hold-last smash | Clamp / ahead path unless tick row is **strict**-confirmed with same dash gate (provisional same-intent no longer skips clamp). |
| STICK_SAMPLE | Prefer published history for the completed tick, then fighter `pl`, then device. |

## Acceptance (re-soak)

Dual-stick at Go + dash-dance, `SSB64_NETPLAY_STRICT_INPUT=1`, `SSB64_STICK_SAMPLE_LOG=1`, onset log on:

- Zero `wire_overwrite` smash→neutral / zero `REPLACE_REJECT` only when poison would have fired
- Soft recovery completes without early quit; no PEER / TURN_DASH `did_dash` asym
- Android STICK_SAMPLE nonzero when history/publish has sticks (scan `RESIM_STICK_FORK` not false FAIL)
- Do **not** SoftLip-harden

## Related

- [`netplay_hold_last_dash_gate_send_lead_peer_2026-07-20.md`](netplay_hold_last_dash_gate_send_lead_peer_2026-07-20.md) — dash clamp / send-lead
- [`netplay_hold_last_zero_predict_stick_onset_fc_2026-07-20.md`](netplay_hold_last_zero_predict_stick_onset_fc_2026-07-20.md) — soft onset peek
- [`netplay_seal_ledger_resim_stick_fork_2026-07-19.md`](netplay_seal_ledger_resim_stick_fork_2026-07-19.md) — RESIM_STICK_FORK class
