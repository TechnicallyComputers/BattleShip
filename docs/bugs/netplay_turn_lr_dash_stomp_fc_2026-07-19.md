# Turn InvertLR `lr_dash` stomp → Turn vs Dash FC (2026-07-19)

**Soak:** soak1 session `1646535146` seed `2189126453` — Android client ↔ Linux host  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `REPLAY_DETERMINISM`

## Symptom

1. `FRAME_COMMIT_STATE_DIVERGE` @680 `figh` inputs MATCH → PEER_SNAPSHOT @683.
2. First `sim_state` / `FIGHTER_LIGHT` mismatch @391 recovered by GGPO (B jump) — not the kill.
3. Hold-gravity scan hits @439/@462 were **symmetric** on both peers — red herring.
4. Real seed @639: Android P1 stays **Turn**(18); Linux → **Dash**(15). Later grab/capture cascade.

`TURN_DASH_WITNESS` (first-pass):

| Peer | @639 update `lr_dash` | @639 interrupt `did_dash` |
|------|------------------------|---------------------------|
| Android | **0** | **0** |
| Linux | **−1** | **1** |

Both: `allow=1`, `sx=-80`, `lr_turn=-1`, `tap_x=6`, `anim_frame=6`. Wire sticks matched (`ggpo_queued=0`).

## Root cause

InvertLR Turn entry sets `turn.lr_dash = ±1` (dash-out gate: `allow && lr_dash≠0 && stick×lr_turn≥56`). Center turns keep `lr_dash=0`.

Through @638 both peers had `lr_dash=-1`. Between Android interrupt@638 and update@639, **`lr_dash` stomped −1→0** (union +0xC / `attack4.unk_0xC` class — sibling of the July 12 `lr_turn` +0x10 stomp). Linux kept −1 → dashed; Android finished Turn → WalkFast → divergent Catch/Capture → FC.

`tap_x=254` on Linux after @639 is **DashSetStatus burn**, not the cause. SoftLipX / Hold gravity are unrelated.

## Fix (`PORT && SSB64_NETMENU`)

1. **`syNetplayTurnNoteEntryLrDash`** — pin InvertLR/Center `lr_dash` at `ftCommonTurnSetStatus` (and `ftCommonDashCheckTurn` smash refresh).
2. **`syNetplayHardenTurnLrDash`** — when rollback semantics active and live `lr_dash != entry` with entry≠0, restore entry. Called with `HardenTurnLrTurn` from Turn update/interrupt/SetStatus.
3. **`HardenTurnLrTurn`** — if live `lr_dash==0`, prefer entry sticky before facing fallback.
4. **Witness** — log `entry_lr_dash`, `attacks4_buf`, `phase=harden_lr_dash`.
5. **scan-drift** — `TURN_DASH_LR_DASH_FORK`, `STATUS_FORK_ONSET`; demote symmetric `HOLD_GRAVITY_RESURRECT`.

Offline / non-rollback unchanged.

## Verify

**Agent:** `cmake --build build --target ssb64 -j 4` (netmenu).

**Human re-soak:** grounded InvertLR Turn with stick held through allow frame (`SSB64_TURN_DASH_WITNESS=1`):

- Both peers `lr_dash=±1` at allow interrupt; matching `did_dash`
- Optional `harden_lr_dash` only when a stomp was repaired
- No Turn vs Dash status fork → FC `figh` inputs MATCH from this path

## Related

- [`netplay_turn_lr_turn_stomp_2026-07-12.md`](netplay_turn_lr_turn_stomp_2026-07-12.md) — `lr_turn` +0x10 harden
- [`netplay_turn_dash_allow_anim_harden_2026-07-12.md`](netplay_turn_dash_allow_anim_harden_2026-07-12.md) — SetFlag1 / allow carve
- [`netplay_hold_last_soft_onset_lookback_release_fc_2026-07-18.md`](netplay_hold_last_soft_onset_lookback_release_fc_2026-07-18.md) — similar Turn/Dash surface, input publish cause
- [`docs/refactor/ftstatusvars_overlay_map_2026-06-02.md`](../refactor/ftstatusvars_overlay_map_2026-06-02.md)
