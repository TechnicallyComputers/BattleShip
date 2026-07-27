# Zero-onset predict runway → PEER (2026-07-20)

**Status:** FIX v10 (`PORT && SSB64_NETMENU`) — Go invent acceptance met on soak `647084351`  
**Soaks:**

| Session | Detail |
|---------|--------|
| `871504438` / `979771282` / `250667155` | Invent `(0,0)` through owner onset → Wait vs Walk/Turn → PEER |
| `1239287245` | Grace D+1 invented `(0,0)` → Wait vs Walk@391 |
| `1100933387` | v4: hard-stall all Restrict froze Go — AdvanceAllowed stalled **without ingress pump** |
| `2117016934` | v5: hang fixed; frontier cover skipped stall → Linux invent `(0,0)` @391 while Android `sx=57`@392 → PEER cascade |
| `999657306` | v6: HardStall≡Restrict + pump; still froze Go — feel-0 send-lead past auth frontier stayed **provisional** |
| `815558360` | v7: Go unblocked; hitch every 2 ticks when `frontier==sim` → asymmetric admit → Turn@413 vs Dash → PEER |
| `656287266` | v8: hitch gone; PEER `replay_determinism` @479 — Go HardStall left GetTick stuck → **double-sim tick 390** |
| `1685388441` | v9: ADVANCE_FORCE fixed double-sim; then D+1 under Restrict cover simmed 391 with invent `(0,0)` vs owner smash → Wait vs Turn |
| `647084351` | v10: Go invent PASS (P0/P1 match ~409); PEER@426 from non-smash hold_last — see [nonsmash release/flip](netplay_hold_last_nonsmash_release_flip_peer_2026-07-25.md) |
| `1907878962` | Hold-last nonsmash fix + v10: soft-stable through `VS_SESSION_END` @516; 0× PEER |

**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** input contract / `PEER_SNAPSHOT_DIVERGE`

## Root cause (evolution)

| Version | Failure |
|---------|---------|
| v1–v3 | Grace/D+1 admitted invent through owner onset |
| v4 | Hard-stall Restrict forever — no pump on AdvanceAllowed path |
| v5 | HardStall = Restrict∧(frontier&lt;sim∨dual-hot) → invent when frontier covers |
| v6 | HardStall = Restrict + pump; PublishFrame refuses Restrict invent — **idle Go hang** |
| v7 | v6 + `auth_stage` Strict LocalGameplayAuth — hang fixed; **2-tick hitch** |
| v8 | HardStall = Restrict∧(frontier&lt;sim); Restrict-with-cover = D+1; auth runway = sim+D |
| v9 | v8 + `AdvanceAfterLiveBattle` — fixed double-sim; cover admit still invented |
| v10 | HardStall≡Restrict again + keep ADVANCE_FORCE |

### v8 → v9 double-sim (`656287266`)

Go: live `gcRunAll` @390 then Advance HardStalled → GetTick stuck → second `gcRunAll` @390 → `linux[t]==android[t+1]`.

### v9 → v10 invent under cover (`1685388441`)

`ADVANCE_FORCE` advanced 390→391 correctly (P0 matched). Next live step admitted under Restrict + frontier cover (D+1): Linux FuncRead invent `(0,0) pred=1` while Android already had P1 `(-66,-12)` → **Wait vs Turn @391**, then 1-tick P1 phase skew → PEER `replay_determinism` ~437. Invent refuse only blocks History mint.

## Fix v9

| Layer | Change |
|-------|--------|
| Live Advance | `syNetInputAdvanceAuthoritativeSimTickAfterLiveBattle` — always advance after live `gcRunAll`; log `ADVANCE_FORCE` if hold would block |
| Contract | Holds gate **FuncUpdate entry only** |

## Fix v10

| Layer | Change |
|-------|--------|
| HardStall | **≡ Restrict** (no D+1 invent under frontier cover) |
| AnalogRamp / dual-hot | D+1 only (unchanged) |
| ADVANCE_FORCE | Kept (v9) — mid-pass Go HardStall must not double-sim |
| Auth runway / pump / invent refuse | Kept |

## Acceptance (re-soak)

- Past Go without permanent freeze / double-sim (`ADVANCE_FORCE` OK; no second evolved hash for same GetTick)
- No Wait/Turn vs Walk/Dash from invent `(0,0)` under Restrict (esp. @391 after Go)
- Dual-spam holds: no multi-second hang
- Soft GGPO OK; brief `zero_onset_stall` while waiting for confirmed is expected — PEER must not be seeded by invent or Go double-sim

**v10 Go invent:** PASS on `647084351`. Hold-last nonsmash residual: PASS on `1907878962`.

**Follow-up (`2141547652`):** auth_stage ahead used pre-sample latch → false Strict `(0,0)` mid-hold → REPLACE/REJECT PEER. Ahead auth now holds last gameplay — [`netplay_zero_onset_auth_stage_false_zero_replace_peer_2026-07-26.md`](netplay_zero_onset_auth_stage_false_zero_replace_peer_2026-07-26.md).

**Follow-up (`1454017460`):** v10 HardStall≡Restrict froze host predict on fresh remote idle `(0,0)` → `pct_R≈22.6%` / both-character lag. v11 hold-neutral invent when last_conf age ≤ pred_win — [`netplay_zero_onset_hold_neutral_r_stall_2026-07-27.md`](netplay_zero_onset_hold_neutral_r_stall_2026-07-27.md).

## Related

- [`netplay_hold_last_nonsmash_release_flip_peer_2026-07-25.md`](netplay_hold_last_nonsmash_release_flip_peer_2026-07-25.md) — residual after v10; accepted PASS
- [`netplay_zero_onset_auth_stage_false_zero_replace_peer_2026-07-26.md`](netplay_zero_onset_auth_stage_false_zero_replace_peer_2026-07-26.md) — auth_stage latch false zero
- [`netplay_analog_ramp_hold_last_jump_drift_2026-07-21.md`](netplay_analog_ramp_hold_last_jump_drift_2026-07-21.md)
- [`netplay_post_go_wire_need_hang_2026-07-18.md`](netplay_post_go_wire_need_hang_2026-07-18.md)
- [`netplay_feel0_send_before_sample_release_skew_2026-07-13.md`](netplay_feel0_send_before_sample_release_skew_2026-07-13.md)
- [`netplay_turn_lr_dash_stomp_fc_2026-07-19.md`](netplay_turn_lr_dash_stomp_fc_2026-07-19.md)
