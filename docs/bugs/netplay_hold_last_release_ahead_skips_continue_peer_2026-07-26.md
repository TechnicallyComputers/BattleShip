# Hold-last release_ahead skips same-intent continue → false zero → BASELINE kill (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** session `1507296706` seed `1368800091` — Android client (lp=1) ↔ Linux host (lp=0)  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `REPLAY_DETERMINISM` / `BASELINE_UNIVERSE_MISMATCH` (`agree_through_load=1`)

## Symptom

Post [deferred_force unequal narrow](netplay_deferred_force_unequal_stick_protocol_storm_2026-07-26.md): same-stick deferred heal OK; session lasts ~1665 ticks then dies.

| Check | Result |
|-------|--------|
| Pair / Go | OK; synctest 13 OK |
| Soft recovery | Many `class=protocol` scan episodes; rematch through ~1662 |
| Scan earliest | `TURN_DASH_FORK@791` (soft-healed); kill surface `STATUS_FORK@1665` P1 WalkMiddle(12) vs WalkFast(13) |
| Kill | `BASELINE_UNIVERSE_MISMATCH` load **1664** → deepen; soft PEER; `VS_SESSION_END` ~1667 |

## Root cause

Android remote P0 invent at **1664** held smash `(-84,-2)`. Ahead scan (default peek 8) walked past owner continue **`(-28,-2)` @1665** (same-intent analog) to **`(0,0)` @1666** and applied `hold_last_smash_release_ahead` → invented **zero on 1664**.

Owner still smashed on 1664 (`STICK_SAMPLE` ~`-80` / seal gap_hold `-84`). Later `LEDGER_REFRESH` `0→-84` opened GGPO load 1664, but peer universes already disagreed (`inputs agree through load` / `class=replay_determinism`). P1 WalkMiddle vs WalkFast @1665 is the visible locomotion fork after that poison.

Tick-wire path already follows same-intent mag / dash-gate XOR. Ahead path only released or opposite-flipped — it **continued** past same-intent rows to a deeper release.

## Fix (`port/net/sys/netinput.c` — `FillHoldLastSoftOnsetIfNeeded`)

| Ahead rule | Change |
|------------|--------|
| Same-intent analog on nearer future tick | **Stop** scan — keep hold (do not seek later `(0,0)`) |
| Near-neutral release_ahead | Only from **`tick+1`**; deeper release stops the scan without rewrite |

Flip_ahead (opposite intent) unchanged. Tick-wire release/follow/flip unchanged.

## Acceptance

Matched APK + Linux binary; dual-stick dash-dance soak:

- No `hold_last_smash_release_ahead` that zeros a smash tick while a nearer peek still has same-sign analog
- Immediate `tick+1` true release still yields (`smash_release_ahead` / `hold_release_ahead`)
- Soft GGPO OK; 0× `BASELINE_UNIVERSE` deepen-exhaust from false zero invent @smash-hold
- Prefer no late WalkMiddle vs WalkFast PEER from this seed

Rebuild desktop **and** Android APK before re-soak.

**Residual chain:** soft NearNeutral → [`soft_neutral`](netplay_hold_last_release_ahead_soft_neutral_peer_2026-07-26.md) (accepted on `1402597419`); hard `(0,0)` tick+1 → [`hard_zero`](netplay_hold_last_release_ahead_hard_zero_peer_2026-07-26.md) (retires all ahead release).

## Related

- [`netplay_hold_last_nonsmash_release_flip_peer_2026-07-25.md`](netplay_hold_last_nonsmash_release_flip_peer_2026-07-25.md) — release/flip for any analog hold (ahead still required; narrowed here)
- [`netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md`](netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md) — tick-wire mag follow; ahead decay must not rewrite
- [`netplay_hold_last_soft_onset_floor_ahead_peer_2026-07-26.md`](netplay_hold_last_soft_onset_floor_ahead_peer_2026-07-26.md) — ahead mis-tick on soft onset
- [`netplay_deferred_force_unequal_stick_protocol_storm_2026-07-26.md`](netplay_deferred_force_unequal_stick_protocol_storm_2026-07-26.md) — prior soak force narrow (accepted class)
- [`netplay_input_authority_tuple_fork_fail_closed_2026-07-15.md`](netplay_input_authority_tuple_fork_fail_closed_2026-07-15.md) — LEDGER_REFRESH completed-sim correct origin
