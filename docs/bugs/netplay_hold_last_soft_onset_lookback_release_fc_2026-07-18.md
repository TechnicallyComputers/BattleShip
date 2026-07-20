# Hold-last soft onset lookback re-inflates release → movement tap fork FC

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-18  
**Session:** soak1 seed `1645329949`

## Symptom

- Android `PEER_SNAPSHOT_DIVERGE` @2499 — **map MATCHES**; diverge **figh + world + rng + anim + cam**
- Wire inputs agree through the load (later FC/peer paths)
- Surface look: Turn (18) vs Dash (15) with matched `sx/sy`, Android `tap_x` counting vs Linux `tap_x=254` (burned)

## Timeline

| Tick | Event |
|------|--------|
| 2313 | GGPO: Android hold_last `-64,-4` vs Linux wire `-1,0` (release). Resim span 2312→2316 OK (matched rollback_post). |
| **2316** | Post-resim live: Android `REMOTE_PUBLISH … source=hold_last sx=-64 sy=-20`; Linux local auth `STICK_SAMPLE (0,0)`. |
| 2350–51 | Linux leaves DamageN3 / AttackDash one tick early vs Android. |
| 2358 | Same stick `82,-10`; Android Turn `tap_x=6`, Linux Dash `tap_x=254` → permanent movement fork → kill @2499. |

## Root cause

[`netplay_input_contract_micro_deadband_onset_peek_2026-07-17.md`](netplay_input_contract_micro_deadband_onset_peek_2026-07-17.md) added hold-last **soft onset**: when `last_confirmed` is near-neutral, peek the remote ring and publish a clamped onset so send-lead wire does not guarantee GGPO vs `(0,0)`.

Two bugs composed:

1. **Lookback peek** — `syNetInputTryPeekRemoteAnalogForOnset` searched **backward** up to 4 ticks. After a release confirm (`-1,0`), lookback found the pre-release stick (`-64,-4`).
2. **Onset floor amplify** — `ApplyAnalogOnsetStick` with `ANALOG_ONSET_STICK_MAG` (clamped 8–**20**) floored `|sy|=4` → **20**, inventing phantom **`(-64,-20)`** that local auth never played.

Android (remote p0) sim'd that phantom at exclusive live 2316; Linux stayed neutral. Silent fighter fork → AttackDash/DamageN3 length skew → Turn vs Dash tap burn. The tap_x=254 vs counting signature is the **cascade**, not a new latch poison.

Secondary: ledger wire promote of `2313=-1,0` did not always advance `last_confirmed`, so post-GGPO hold_last could still seed `-64,-4` (`REMOTE_PUBLISH@2315`).

## Fix (`port/net/sys/netinput.c`)

| Change | Behavior |
|--------|----------|
| Soft onset `max_lookback=0` | Ahead-only (`tick+1 … tick+peek_ahead`); no backward re-inflate after release. |
| Ledger refresh → `last_confirmed` | Wire/seal ledger rows update the hold-last seed (+ `NoteRemoteNonNeutralStick`). |

Defer-GGPO onset peek (`ShouldDeferPredictedAnalogCorrection`) still may look back — that path does not invent published sticks.

## Verify

Re-soak Android↔Linux through a stick **release** GGPO (hold → near-neutral wire):

- Post-resim exclusive target: remote `REMOTE_PUBLISH` / `STICK_SAMPLE` must not invent floored pre-release sticks (`hold_last_soft_onset` only when true send-lead ahead exists)
- No AttackDash/Wait length skew → Turn vs Dash with matched wire `sx/sy` and forked `tap_x`
- `SSB64_NETPLAY_ANALOG_ONSET_LOG=1` optional: soft_onset lines only with ahead peeks

## Related

- [`netplay_input_contract_micro_deadband_onset_peek_2026-07-17.md`](netplay_input_contract_micro_deadband_onset_peek_2026-07-17.md)
- [`netplay_post_resim_pl_latch_stick_range_poison_fc_2026-07-17.md`](netplay_post_resim_pl_latch_stick_range_poison_fc_2026-07-17.md)
- [`netplay_feel0_send_before_sample_release_skew_2026-07-13.md`](netplay_feel0_send_before_sample_release_skew_2026-07-13.md)
