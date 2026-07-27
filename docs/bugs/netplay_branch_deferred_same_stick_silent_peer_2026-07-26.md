# BRANCH_DEFERRED + same-stick wire → silent Turn/Dash PEER (2026-07-26)

**Status:** FIX ACCEPTED for same-stick class (`PORT && SSB64_NETMENU`); force narrowed in follow-up  
**Soak (v1):** session `743554090` seed `1165620258` — Android client (lp=1) ↔ Linux host (lp=0)  
**Soak (v2 residual):** session `1541066087` seed `3720850387` — ticket noted, 0× GGPO, soft desync until `strict remote MISS` @~608  
**Soak (v3 accept):** session `662339918` seed `4263539622` — EQ `branch_deferred_same_stick` heals on Android @400/503/530; Linux deferred Turn @686. Session later died on unequal-force storm — [`netplay_deferred_force_unequal_stick_protocol_storm_2026-07-26.md`](netplay_deferred_force_unequal_stick_protocol_storm_2026-07-26.md).  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Replay:** `20260726_131153.ssb64r` (v1), `20260726_132919.ssb64r` (v2), `20260726_134636.ssb64r` (v3)  
**Bucket:** `REPLAY_DETERMINISM` / `PEER_SNAPSHOT_DIVERGE` (`agree_through_load=1`)

## Symptom

Longer soak with heavy `class=protocol` GGPO churn. Live `figh` last matches then continuous diverge. Many deepen-exhaust PEER (map/world match, figh≠). Soft desync: PEER deepen suppressed (`resim_seal_wait` / `stick_absorb`); both peers keep moving until admission stall — not a hard lock / seal-exhaust hang.

## Timeline (v2 @575, P1 fkind=11)

| Peer | Detail |
|------|--------|
| Both @574 | P1 `status=18` Turn, sticks `(-68,-8)` |
| Android (owner) | Turn allow → **Dash `status=15`** |
| Linux (remote) | `hold_last` predicted → `BRANCH_DEFERRED turn_allow_dash` → **stay Turn `18`** |
| Linux after Advance | `REMOTE_PUBLISH … ledger_wire` same `(-68,-8)` — **no GGPO** |

Owner committed Dash; peer deferred on predict then confirmed the same sticks without rewind → permanent Turn vs Dash.

## Root cause

[`netplay_branch_sensitive_predict`](netplay_branch_sensitive_predict_2026-07-20.md) correctly suppresses predicted Turn→Dash. Acceptance requires authoritative resim when wire confirms.

**v1 gap:** ledger refresh / StickReplace treated **equal sticks** as no correction.

**v2 gap (after ticket note):** `QueueOrWidenStickCorrection` cleared the deferred ticket **before** `RequestInputCorrection`, which re-checks `StickReplaceNeedsRewind` (needs ticket for same sticks) and `PublishedSimUsedPrediction` / `ComputeInputCorrectionTuple` (fail after ledger `StoreFrame` confirms history). Result: ticket consumed, 0× `branch_deferred_same_stick` / GGPO, soft PEER deepen.

## Fix

| Layer | Change |
|-------|--------|
| `netplay_branch_predict` | On `BRANCH_DEFERRED` (`wants_branch`), note `(player,tick)` ticket |
| Ledger refresh | Equal sticks + ticket → `QueueOrWiden` (`branch_deferred_same_stick`); also when already confirmed + ticket remains; deferred arm at `GetTick() >= sim_tick` |
| StickReplaceNeedsRewind | Equal sticks but predicted + ticket → needs rewind |
| `QueueOrWiden` | **Do not** clear ticket before Request; clear when folded into open episode/absorb |
| `RequestInputCorrection` | `deferred_force` path: bypass ShouldQueue / prediction / StickReplace / ComputeTuple; arm `QueueDeferredInputCorrectionEx` at `sim_tick` and clear ticket — **only when published sticks+buttons equal remote** (v3 narrow) |

## Acceptance

Matched Android APK + Linux binary; dual-stick Turn allow under remote predict:

- `BRANCH_DEFERRED` may still fire on first-pass predict
- Same-stick wire/ledger confirm after defer → `GGPO … branch_deferred_same_stick` and/or `LEDGER_REFRESH_COMPLETED_SIM_CORRECT … branch_deferred_same_stick` → resim → `BRANCH_COMMITTED` / matching Dash
- No continuous figh diverge Turn(18) vs Dash(15) from this class
- Soft protocol GGPO OK; 0× deepen PEER from silent same-stick confirm

Rebuild desktop **and** Android APK before re-soak (AppImage + APK must both contain the new GGPO suffix).

## Related

- [`netplay_branch_sensitive_predict_2026-07-20.md`](netplay_branch_sensitive_predict_2026-07-20.md) — transactional defer framework
- [`netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md`](netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md) — prior invent/micro class
- [`netplay_deferred_force_unequal_stick_protocol_storm_2026-07-26.md`](netplay_deferred_force_unequal_stick_protocol_storm_2026-07-26.md) — v3: force must not apply to stick deltas
- [`netplay_protocol_ggpo_seal_rows_exhaust_hang_2026-07-25.md`](netplay_protocol_ggpo_seal_rows_exhaust_hang_2026-07-25.md) — distinct seal-exhaust kill
- [`netplay_input_authority_tuple_fork_fail_closed_2026-07-15.md`](netplay_input_authority_tuple_fork_fail_closed_2026-07-15.md) — ledger completed-sim correct origin
- [`netplay_episode_fsm_ggpo_drop_after_promote_2026-07-12.md`](netplay_episode_fsm_ggpo_drop_after_promote_2026-07-12.md) — related Promote-clears-prediction GGPO drop
