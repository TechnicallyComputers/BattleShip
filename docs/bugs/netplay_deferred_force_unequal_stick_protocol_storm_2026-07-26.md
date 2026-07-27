# deferred_force on unequal sticks → protocol storm → baseline universe kill (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** session `662339918` seed `4263539622` — Android client (lp=1) ↔ Linux host (lp=0)  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Replay:** `20260726_134636.ssb64r`  
**Bucket:** `REPLAY_DETERMINISM` / `BASELINE_UNIVERSE_MISMATCH` (`agree_through_load=1`) after heavy `class=protocol` scan churn

## Symptom

Same-stick `BRANCH_DEFERRED` heal from [`netplay_branch_deferred_same_stick_silent_peer`](netplay_branch_deferred_same_stick_silent_peer_2026-07-26.md) **worked** (Android EQ GGPO @400/503/530; Linux deferred Turn @686). Session still died ~713:

| Check | Result |
|-------|--------|
| NetSync `figh` | Last match ~694; continuous DIFF from ~702 |
| Soft corrections | ~75× `EPISODE_PROOF … source=scan class=protocol` (`agree_through_load=1`) |
| Kill | `BASELINE_UNIVERSE_MISMATCH` deepen exhaust @703 then @708 → soft PEER (`resim_seal_wait`) → `VS_SESSION_END` @713 |
| Partitions | `world` match; `figh` + `map` diverge |

Not the seal-rows exhaust hang ([protocol seal](netplay_protocol_ggpo_seal_rows_exhaust_hang_2026-07-25.md)): here baseline deepen runs (`baseline_matched=0` path).

## Mis-tagged force

Linux logged `branch_deferred_same_stick` on **unequal** sticks:

| Tick | Published (pred) | Remote wire |
|------|------------------|-------------|
| 527 | `(-77,-11)` | `(0,0)` |
| 686 | `(61,2)` | `(46,1)` |

`RequestInputCorrection` armed `deferred_force` from the ticket alone, bypassing `ShouldQueue` / StickReplace / ComputeTuple. Stick deltas that should debounce or take the normal REPLACE path instead opened forced protocol episodes.

Android's same-stick EQ cases (`80,19` / `76,35` / `31,21`) are the intended force path and stay valid.

## Kill window (P0)

Matched sticks through the late Dash hold; `figh` still matched @694. Continuous diverge @702 (both enter Wait→Turn with different SoftLip / `fhash`). Android @699 briefly `status=13` WalkFast while Linux stayed Dash; `tap_x` lagged 1 frame on Android remote predict @700–701. Residual physics fork under matched sticks may remain after this force fix — track separately if re-soak still dies here.

## Fix (`port/net/sys/netrollback.c`)

`deferred_force` only when published and remote **gameplay sticks+buttons match**. Unequal → fall through StickReplace / ShouldQueue; keep ticket until StickReplace queues or declines. Missing frames → keep ticket, do not arm with zeros.

## Acceptance

Matched APK + Linux binary; dual-stick soak:

- `GGPO … branch_deferred_same_stick` only when published sx/sy/btn == remote
- Stick-delta confirms after `BRANCH_DEFERRED` log normal GGPO (no same_stick suffix)
- Protocol scan episode rate drops vs soak `662339918` (~75 begins)
- Prefer 0× deepen-exhaust PEER from this force-storm shape

Rebuild desktop **and** Android APK (repackage AppImage if soak uses it).

## Related

- [`netplay_branch_deferred_same_stick_silent_peer_2026-07-26.md`](netplay_branch_deferred_same_stick_silent_peer_2026-07-26.md) — same-stick force (narrowed here)
- [`netplay_protocol_ggpo_seal_rows_exhaust_hang_2026-07-25.md`](netplay_protocol_ggpo_seal_rows_exhaust_hang_2026-07-25.md) — distinct seal-exhaust kill
- [`netplay_branch_sensitive_predict_2026-07-20.md`](netplay_branch_sensitive_predict_2026-07-20.md) — BRANCH_DEFERRED framework
