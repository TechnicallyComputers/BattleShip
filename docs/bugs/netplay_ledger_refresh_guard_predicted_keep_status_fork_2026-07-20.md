# Ledger refresh guard kept predicted hold_last over confirmed release → STATUS_FORK (2026-07-20)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** soak1 session `1511856153` seed `1321428975` — Android host ↔ Linux guest  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** regression from same-day guard `LEDGER_REFRESH_REJECT_NEUTRAL_DOWNGRADE`

## Symptom

| Signal | Detail |
|--------|--------|
| Scan | `STATUS_FORK_ONSET` tick=431 P0 (host status=13 dash \| guest status=10) |
| Cascade | `TURN_DASH_LR_DASH_FORK` @458 → `PEER_MAP_PHASE_FORK` @564 → PEER stop @563 |
| Storm | ~110 `RESIM_STICK_FORK` lines from 403 — ~95% prediction-window noise (see below) |
| Guard | Android `LEDGER_REFRESH_REJECT_NEUTRAL_DOWNGRADE` P0 @431 keep `(-76,-37)` reject `(0,0)` — repeated on every GGPO retry |

## Root cause

Linux P0 **really released** to `(0,0)` at 431. Android was predicting P0 via
`hold_last (-76,-37)` (published row `pred=1`). When the wire release confirm arrived, the
new-yesterday refresh guard compared incoming neutral against the *published* row without
checking `is_predicted` / `source`. The published analog was a **hold-last guess**, not a
confirm — but the guard kept it and blocked the GGPO correction on every retry
(`writer=promote_remote_authority`, `post_pre_promote`). Android drove P0 into Dash
(status 13, `did_dash`) while Linux sat at 10/12 → locomotion fork → Turn/Dash → PEER kill.

The guard was designed against *poison* zeros overwriting **confirmed** analog rows
(soak `871504438`). It over-matched onto genuine releases whenever the published side was
still a prediction.

The wire-ring guard (`REMOTE_CONFIRMED_REPLACE_REJECT_NEUTRAL_DOWNGRADE`) behaved
correctly in this soak: every `keep` value matched the sender's actual `LOCAL_PUBLISH`
(e.g. keep `(-32,3)` @501 vs Android's real `(-32,3)`), i.e. it kept blocking real stale
zero re-sends. Only the refresh-path guard regressed.

## Fix (`port/net/sys/netinput.c`)

| Layer | Change |
|-------|--------|
| `syNetInputRefreshPublishedFromAuthorityLedger` | Guard now requires the published row be strict `RemoteConfirmed` and `is_predicted == FALSE`. A hold-last prediction never outranks a confirmed wire release. |
| `syNetInputAuthorityLedgerStore` | Neutral-downgrade reject additionally skips when gameplay already equals (deadband no-op refresh; avoids spurious reject log). |
| `syNetInputMaybeLogStickSample` | `STICK_SAMPLE` now emits `pred=0/1`. `pred=1` = prediction-window row for a remote slot (or non-confirmed source / no-history fallback). |

## Scan (`scripts/netplay-scan-drift.py`)

- Parses optional `pred=` flag; a later `pred=0` sample upgrades an earlier `pred=1`
  first-write for the same tick/player.
- `RESIM_STICK_FORK` and the physics-gut `resim_stick_fork` compound skip comparisons where
  either side is `pred=1` — send-lead prediction disagree is normal rollback operation.
  Old-binary logs without the flag behave as before (`pred=0`).

## Acceptance (re-soak)

Dual-stick at Go + dash-dance with releases (flick-and-let-go both peers):

- No `LEDGER_REFRESH_REJECT_NEUTRAL_DOWNGRADE` against a `pred=1` published row
- Release-to-neutral confirms land within one GGPO correction (no repeated re-reject on the same tick)
- No STATUS 13-vs-10 fork on a release tick; no Turn/Dash cascade
- `RESIM_STICK_FORK` count collapses to true seal/ledger forks only (pred rows skipped)

## Related

- [`netplay_wire_neutral_downgrade_dual_stick_onset_2026-07-20.md`](netplay_wire_neutral_downgrade_dual_stick_onset_2026-07-20.md) — the poison class these guards target
- [`netplay_zero_onset_predict_runway_peer_2026-07-20.md`](netplay_zero_onset_predict_runway_peer_2026-07-20.md) — zero-onset predict cap (same session family)
- [`netplay_seal_ledger_resim_stick_fork_2026-07-19.md`](netplay_seal_ledger_resim_stick_fork_2026-07-19.md) — RESIM_STICK_FORK class
