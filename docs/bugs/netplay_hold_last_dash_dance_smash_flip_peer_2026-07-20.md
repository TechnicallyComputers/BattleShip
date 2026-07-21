# Hold-last stale smash sign → Turn/Dash PEER (dual dash-dance) (2026-07-20)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** soak1 session `1876984747` seed `1888956866` — Android client ↔ Linux host  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `REPLAY_DETERMINISM`

## Symptom

| Signal | Detail |
|--------|--------|
| Kill | Linux `PEER_SNAPSHOT_DIVERGE@6557` figh+anim; map/world/rng match |
| Named seed | `TURN_DASH_LR_DASH_FORK@6505` P1 — host `lr_dash/entry=0`, guest `=-1` |
| Storm | Dense `RESIM_STICK_FORK`; guest `SEALED_RESIM_LOAD_NEUTRAL` @6405/@6451 |
| Red herrings | `STATUS_FORK@422` recovered; symmetric `HOLD_GRAVITY_*`; late `PHYSICS_FORK` cascade |

Both sticks dash-dancing. Synctest OK, `LOAD_HASH_DRIFT=0`, `ggpo_queued=0`.

## Root cause

1. **Stale non-neutral hold-last.** Soft onset only replaced *near-neutral* hold-last. Predictor kept `last_confirmed` smash sign (`sx=+80`) while wire for the sim tick already sat in the ring as the flip (`sx=-65`). First-pass Android@6498 applied the flip; Linux hold-last stayed `+80` through Turn allow@6501.
2. **Turn entry / `lr_dash` schedule.** Mid-Turn stick asymmetry → Center vs InvertLR sticky / `DashCheckTurn` refresh during resim (`entry` 0 vs −1) → Android Dash@6507 vs Linux still Turn → PEER deepen.
3. **Seal freeze.** `CopyEpisodeRemoteAuthoritySealFrame` sealed Resolve hold-last (including invent `(0,0)`) even when ledger already had the corrected stick → in-span `SEALED_RESIM_LEDGER_SKIP` with `sealed=(0,0)`.

Not SoftLip / Hold gravity. Not a fresh InvertLR union stomp (July 19 harden): sticks disagreed, so entry pins were written under different inputs.

## Fix (`port/net/sys/netinput.c`)

| Layer | Change |
|-------|--------|
| Smash-flip | `FillHoldLastSoftOnsetIfNeeded`: when hold-last is non-neutral and wire at **this tick** has opposite analog intent (`!StickSameAnalogIntent`), replace with raw peek (no ahead, no lookback, no onset floor). Log `hold_last_smash_flip`. |
| Predict | `MakePredictedFrameRemoteHuman` calls soft-onset/smash-flip after seed/decay assembly. |
| Seal | Remote seal copy prefers authority ledger → confirmed wire history before Resolve hold-last. |
| Sealed miss | Remote resim miss under NETMENU uses hold-last + smash-flip/soft-onset instead of inventing hard `(0,0)`. |

Offline / non-rollback unchanged. Release reinflate guard (max_lookback=0) preserved.

## Acceptance (re-soak)

Dual-joystick grounded dash-dance with `SSB64_TURN_DASH_WITNESS=1` / optional `SSB64_NETPLAY_ANALOG_ONSET_LOG=1`:

- Matching `sx` through Turn allow; matching `entry_lr_dash`
- No `TURN_DASH_LR_DASH_FORK` → PEER figh-only from this path
- No `SEALED_RESIM_LOAD_NEUTRAL` under the storm
- Do **not** SoftLip-harden late `PHYSICS_FORK` / `topn_*`

## Follow-up (same day)

Soak1 `179193526`: matched `lr_dash`, false `did_dash` — wire not in ring at allow; tick-only flip insufficient. Send-lead ahead release + dash-gate clamp: [`netplay_hold_last_dash_gate_send_lead_peer_2026-07-20.md`](netplay_hold_last_dash_gate_send_lead_peer_2026-07-20.md).

## Related

- [`netplay_turn_lr_dash_stomp_fc_2026-07-19.md`](netplay_turn_lr_dash_stomp_fc_2026-07-19.md) — InvertLR union stomp (matched sticks)
- [`netplay_hold_last_zero_predict_stick_onset_fc_2026-07-20.md`](netplay_hold_last_zero_predict_stick_onset_fc_2026-07-20.md) — soft onset for hard-zero hold-last
- [`netplay_hold_last_soft_onset_lookback_release_fc_2026-07-18.md`](netplay_hold_last_soft_onset_lookback_release_fc_2026-07-18.md) — why lookback stays off
- [`netplay_sealed_resim_load_tick_neutral_invent_2026-07-19.md`](netplay_sealed_resim_load_tick_neutral_invent_2026-07-19.md) — sealed miss invent class
