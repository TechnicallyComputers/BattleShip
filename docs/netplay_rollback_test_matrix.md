# Netplay rollback test matrix

Manual soak checklist and regression env presets for rollback (`netrollbacksnapshot.c`, `netrollback.c`, `netinput.c`).

See also: [`netplay_rollback_refactor_contracts.md`](netplay_rollback_refactor_contracts.md).

## Env presets

| Scenario | Suggested env |
|----------|----------------|
| Automatch default | Unset `SSB64_NETPLAY_AUTO_SESSION_PARAMS` (negotiates rollback + transport from RTT); verify `NetSession: apply` and `NetRollback: session_negotiated` logs |
| Baseline 1v1 | `SSB64_NETPLAY_ROLLBACK=1`, `SSB64_NETPLAY_ROLLBACK_SNAPSHOT_FRAMES=32` |
| Deep rollback | `SSB64_NETPLAY_ROLLBACK_SNAPSHOT_FRAMES=64` (or ≥ rollback span under test) |
| Debug snapshot ring | `SSB64_NETPLAY_ROLLBACK_SNAPSHOT_FRAMES=128` (negotiated cap 128; soak only) |
| Baseline gate soak | `SSB64_NETPLAY_RESIM_TICK_TRACE=1`, `SSB64_NETPLAY_STATE_DETAIL_DIAG=2`; expect `RESIM_BASELINE_SEND`/`RECV` then `resim baseline gate open` before `resim_tick` |
| Symmetric GGPO soak | Symmetric follower + `resim_rng_verify` on by default with rollback; **do not** set `SSB64_NETPLAY_PREDICT_NEUTRAL=1`, `SSB64_NETPLAY_PREDICTION_RECOVERY=1`, or `SSB64_NETPLAY_ROLLBACK_SYMMETRIC_DIAG=1`; expect `session_negotiated … symmetric=1 symmetric_diag_only=0 resim_rng_verify=1`, paired `GGPO deferred input correction resim`, `resim begin`/`complete`; **no** `prediction recovery armed` |
| Analog onset soak | Source `scripts/netplay-analog-onset-soak.env.example`; **unset** `SSB64_NETPLAY_ROLLBACK_SYMMETRIC_DIAG=1`; expect `symmetric_diag_only=0`, paired `resim complete`, no `PEER_BASELINE_RESYNC_STORM`, no stale `RESIM_BASELINE_ECHO` spam |
| KO lifecycle bisect soak | Source `scripts/netplay-ko-lifecycle-soak.env.example`; **no** `PREDICT_NEUTRAL=1`; `SIM_STATE_TICK_INTERVAL=1`, `GC_TRAVERSAL_DIAG=2`, `GOBJ_EJECT_TRACE=1`, `PEER_DIVERGE_DETAIL=1`; pass = first divergent partition + tick in both logs; fail = `PEER_DIVERGE_DIFF` / `peer_snapshot_diverge` at KO |
| Mixed keyboard/analog soak | Source `scripts/netplay-mixed-input-soak.env.example`; pad vs keyboard from GO and optional mid-match device switch; **no** `PREDICT_NEUTRAL=1`; pass = 5+ min, paired `resim complete`, no `PEER_SNAPSHOT_DIVERGE`; fail = `figh`/`anim` diverge with `world`/`rng` match, or `ROLLBACK_IDENTITY_DRIFT` |
| Epoch pacing + analog decay soak | Same mixed-input env; expect `rollback_epoch_hold` with `epoch=` during peer resim; **no** live `sim` ahead of peer symmetric `target+slack` while peer epoch active; no mirrored `RESIM_BASELINE_MISMATCH` at same `load_tick`; `SSB64_NETPLAY_ROLLBACK_EPOCH_CAP_SLACK=99` bisect disables cap; `SSB64_NETPLAY_ANALOG_PRED_DECAY_TICKS=0` bisect disables decay |
| Legacy gate proceed | `SSB64_NETPLAY_RESIM_BASELINE_PROCEED_ON_TIMEOUT=1` (unsafe; debug only) |
| Snapshot save assert | `SSB64_NETPLAY_SNAPSHOT_SAVE_ASSERT=1` (one-shot log if save runs during `resim_pending`) |
| ~200 ms RTT netem | `tc netem delay 95ms 15ms distribution normal loss 0.3%` both peers; expect `rtt_ms≈200`, `D≈6–7`, `phase_lock≈8` (capped), `rb_snap≥48`, `fuzz≥2`, `redundancy≥2`; watch `rb=` in NetSync (prediction + resim, not lockstep-only) |
| ~84 ms RTT netem | `tc netem delay 38ms 8ms loss 0.2%` both peers; expect `rtt_ms≈80–90`, `tier=good`, `D≈5`, `phase_lock≈6`, `redundancy=2`; host and guest `rb=` should both increment under stick churn |
| ~100 ms RTT soak | Auto-negotiate; expect `tier=good`, `D≥5`, `phase_lock≤7`; jump correction → full resim on both peers, not patch-only |
| Forced resim | `SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH=1`, `SSB64_NETPLAY_ROLLBACK_INJECT_TICK=<wire_or_sim per inject docs>` |
| Load verify | `SSB64_NETPLAY_ROLLBACK_LOAD_HASH_VERIFY=1` (default); `=0` debug-only |
| Catch-up budget | `SSB64_NETPLAY_ROLLBACK_RESIM_TICKS_PER_FRAME=4` |
| Symmetric follower (default) | On whenever rollback is active; peer notices drive matching resim. `SSB64_NETPLAY_ROLLBACK_SYMMETRIC=0` for GGPO-only independent correction |
| Symmetric diag-only | `SSB64_NETPLAY_ROLLBACK_SYMMETRIC_DIAG=1` (log peer notices without follower resim) |
| Conservative remote buttons | Default: buttons predict **0** under delay; `SSB64_NETPLAY_PREDICT_REMOTE_BUTTONS_HOLD=1` restores hold-last (shield-tap churn risk) |
| Digital tap patch (no resim) | **Off** during active rollback (force full resim). `SSB64_NETPLAY_PREDICTION_RECOVERY=1` re-enables legacy tap patch + recovery (debug) |
| Prediction recovery | **Off** by default. `SSB64_NETPLAY_PREDICTION_RECOVERY=1` debug only |
| Delay vs prediction ratio | See `NetSession: apply tier=…`: excellent → `D` 3–4, `phase_lock` ≤5; good → `D` 4–6, `phase_lock` 5–7; playable → `phase_lock` cap 6; high → `phase_lock` cap 7 |
| State detail | `SSB64_NETPLAY_STATE_DETAIL_DIAG=1` (world); `=2` (+ fighter detail) |
| NetSync log interval | `SSB64_NETPLAY_NETSYNC_LOG_INTERVAL=60` (default 120 sim ticks between NetSync checkpoints) |
| Resim defer diag | `SSB64_NETPLAY_ROLLBACK_DEFER_DIAG=1` |
| Resim trace | `SSB64_NETPLAY_RESIM_TICK_TRACE=1` |
| Resim RNG verify | Default on with rollback; `SSB64_NETPLAY_RESIM_RNG_VERIFY=0` disables |
| Peer snapshot abort | `SSB64_NETPLAY_ROLLBACK_PEER_SNAPSHOT_ABORT=1` (default); `=0` debug-only |
| Scan clean diag | `SSB64_NETPLAY_ROLLBACK_SCAN_DIAG=1` |
| Synctest (when implemented) | `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1` |
| High-latency prediction soak | `SSB64_NETPLAY_DELAY=2`, `SSB64_NETPLAY_PHASE_LOCK_PREDICTION_TICKS=3..7`, `SSB64_NETPLAY_INPUT_PREDICTION=1`, `SSB64_NETPLAY_STRICT_SLACK_FRAMES=0` |
| Neutral guard (onset window) | `SSB64_NETPLAY_NEUTRAL_GUARD_TICKS=2` (default; max 3) |
| Analog onset stick mag | `SSB64_NETPLAY_ANALOG_ONSET_STICK_MAG=12` (default; 8–20) |
| Analog onset lookback | `SSB64_NETPLAY_ANALOG_ONSET_LOOKBACK=60` (default; 8–120 sim ticks) |
| Peer diverge detail | `SSB64_NETPLAY_PEER_DIVERGE_DETAIL=1` or `STATE_DETAIL_DIAG>=1`; field-level `PEER_DIVERGE_DIFF` on `PEER_SNAPSHOT_DIVERGE` |
| Peer diverge resync window | `SSB64_NETPLAY_PEER_DIVERGE_RESYNC_TICKS=4` (default 2) before hard abort vs storm resync |
| Hash transition log | `SSB64_NETPLAY_HASH_TRANSITION_LOG=1` (local-only; pairs with `SIM_STATE_TICK_INTERVAL=1`) |
| Joint translate bisect | `SSB64_NETPLAY_JOINT_TRANSLATE_TRACE=1` (+ optional `_SLOT` / `_FKIND`); forward-sim only; `joint_translate_trigger` on first Full `figh` step |
| GC traversal diag | `SSB64_NETPLAY_GC_TRAVERSAL_DIAG=2` (`gch` + `pairs=` on NetSync lines; non-zero after Phase 3) |
| GObj eject trace | `SSB64_NETPLAY_GOBJ_EJECT_TRACE=1`, `GOBJ_EJECT_RING=32`; `RING_DUMP` on peer diverge |
| Mixed input quantize | `SSB64_NETPLAY_MIXED_INPUT_QUANTIZE=1` (default); `=0` to send raw partial keyboard sticks |
| Mixed input log | `SSB64_NETPLAY_MIXED_INPUT_LOG=1` (encoding switch lines) |
| Resim snapshot refresh | `SSB64_NETPLAY_RESIM_SNAPSHOT_REFRESH=1` (default); `=0` debug-only — disables per-tick ring refresh during resim replay |
| Rollback epoch cap slack | `SSB64_NETPLAY_ROLLBACK_EPOCH_CAP_SLACK=1` (default); `=99` disables peer-epoch live-sim cap |
| Analog prediction decay | `SSB64_NETPLAY_ANALOG_PRED_DECAY_TICKS=3` (default); `=0` off; `SSB64_NETPLAY_ANALOG_PRED_MIN_MAG=12` |
| Stick mismatch recovery | `SSB64_NETPLAY_STICK_MISMATCH_RECOVERY=1` (default); `=0` off — confirmed-only window after neutral↔analog GGPO (not `PREDICTION_RECOVERY`) |

## Cases

1. **No-items 1v1** — no `LOAD_HASH_DRIFT` after forced resim; no `ROLLBACK_IDENTITY_DRIFT`.
2. **Projectile-heavy** — Link/Samus/Pikachu; weapon snapshot + hash stable.
3. **Item-heavy** — item spawn/active hashes stable; no ghost items after rollback.
4. **4-player / high delay** — ring depth 64+; mismatch scan + paced resim.
5. **Jump / stick storm** — rapid remote stick changes at prediction windows 3, 5, and 7; no equal-gameplay rollback storm; newer corrected packets may log `REMOTE_CONFIRMED_REPLACE_NEWER`, but stale older packets should be the only `REMOTE_CONFIRMED_CONFLICT` cases.
6. **Packet loss / gap** — `tc netem` or reorder; strict `MISS (R)` should not deadlock (or session abort cleanly).
7. **Ring underrun** — deep rollback with `SNAPSHOT_FRAMES=32` and mismatch >32 ticks ago; expect load-fail log, not SIGSEGV.
8. **Cap overflow** (when instrumented) — >16 items or >32 weapons; loud failure, not silent truncation.
9. **Analog onset idle→walk** — remote idle then small stick → run; prediction uses minimal facing when `last_non_neutral` valid; GGPO still resims on confirm mismatch; symmetric follower + baseline gate per case 16 env row.
10. **Mixed keyboard vs pad** — opponents use different devices from match start; no session abort in 5+ min soak env.
11. **Mid-match device switch** — one peer switches pad↔keyboard once; at most brief rollback visible; session continues (no `PEER_SNAPSHOT_DIVERGE`).

## Pass criteria

- Resim completes to frontier without unrecoverable load failures.
- No `LOAD_HASH_DRIFT` for covered subsystems after successful load verify.
- No `ROLLBACK_IDENTITY_DRIFT` when confirmed inputs unchanged across resim.
- No `REMOTE_CONFIRMED_CONFLICT` from synthetic gap-fill replacement.
- Gameplay continues without ghost items/weapons after rollback.
- With `SSB64_NETPLAY_ROLLBACK_SYMMETRIC=0`, peers still converge via independent GGPO correction; both sides should show `rb=` growth under stick churn.
- **Symmetric resim execution:** host and client log identical `target_tick` / `span` on each `resim begin` when symmetric follower is active; initiator logs `GGPO deferred input correction` then `resim begin`; follower logs `peer symmetric rollback queued` / `peer symmetric rollback at` then `resim begin`; `ROLLBACK_SYNC_SEND`/`RECV` or symmetric fields in INPUT; `resim baseline gate open` before any `resim_tick` forward sim (no `proceeding without peer digest` unless `RESIM_BASELINE_PROCEED_ON_TIMEOUT=1`); passive peer `RESIM_BASELINE_ECHO` when only one side detected GGPO; matching `figh`/`world`/`item`/`rng` on `resim baseline` at the same `load_tick`; no `RESIM_BASELINE_MISMATCH` during soak.
- **Snapshot hygiene:** no `SNAPSHOT_SAVE_ASSERT` during resim; ring not rewritten for ticks in `(load_tick, episodeResolvedThrough]` while resim pending or episode cooldown active.
- **Conservative correction:** significant predicted-remote mismatch → `GGPO deferred input correction resim` (not live-only patch). **`prediction recovery armed` is a soak failure.**

## Fail signatures (investigate)

| Log | Action |
|-----|--------|
| `LOAD_HASH_DRIFT` | Snapshot incomplete or destructive load — stop / fix restore (anim-only: soft-continue by policy; see out-of-scope) |
| `ROLLBACK_IDENTITY_DRIFT` | Resim not deterministic or wrong inputs during replay |
| `REMOTE_CONFIRMED_CONFLICT` | Confirmed immutability / gap-fill authority bug |
| `remote_gap_fill` | Note whether used for admission only vs rollback authority |
| `abort resim — snapshot load verify failed` | Expected if ring underrun or drift |
| `STRICT MISS (R)` sustained | Wire hole or recovery window stall |
| `RESIM_BASELINE_MISMATCH` | Cross-peer snapshot diverged at load tick — deeper resync armed |
| `RESIM_BASELINE_TIMEOUT` | No peer digest in gate window — deeper load or resync (not forward sim unless proceed env) |
| `RESIM_BASELINE_SEND_FAIL` | UDP baseline send short or invalid socket |
| `LOAD_SLOT_LIVE_DRIFT` | Post-load live hash ≠ slot hash — pre-resim deeper load attempted |
| `resim reconcile missing confirmed remote` | Wire hole during resim span — no predicted fallback applied |
| `prediction recovery armed` | Patch-only path active — disable recovery; force symmetric resim |
| `GGPO input correction queued` without `deferred input correction resim` | Episode anchor / defer blocked — check `SSB64_NETPLAY_ROLLBACK_DEFER_DIAG=1` |
| `PEER_BASELINE_RESYNC_STORM` | Peer baseline resync walked past ring depth or step cap — investigate figh-only divergence + analog onset |
| `RESIM_BASELINE_ECHO` repeated for same `load_tick` after `resim complete` | Stale passive echo — should be suppressed by resolved-through / dedup |
| `PEER_DIVERGE_DIFF partition=spawn_wait` | World spawn scheduler drift during KO — see KO lifecycle bug doc |
| `BASELINE_ECHO_RETRY_DEFER` then `BASELINE_ECHO_RETRY` | One-frame snapshot race; should recover or diverge after retry, not instant abort |
| `BASELINE_LOAD_CLAMP` | Initiator advertised load_tick ahead of remote frontier — clamped send |
| `hash_transition partition=gch` before `figh` | Frame composition / GObj order fork — see `netplay_frame_composition.md` |
| `PEER_SNAPSHOT_DIVERGE` with `world`/`rng` match, `figh`/`anim` mismatch | Stale snapshot ring at `load_tick` or encoding/prediction churn — see mixed-input bug doc |
| `load post tick N failed` then no client `resim begin` | Episode anchor blocked retry — fixed: reset episode on load fail + `LOAD_TICK_ADJUST` |
| `PEER_BASELINE_ANIM_ONLY` | Gameplay partitions match, anim differs — session continues (not `PEER_SNAPSHOT_DIVERGE`) |
| `peer symmetric rollback deferred` | `ROLLBACK_SYNC` held until baseline echo/gate quiesced |
| `BASELINE_ECHO_RETRY_DEFER … snapshot_not_ready` | `load_tick >= sim_tick` or ring not committed — wait for `AfterBattleUpdate` save |
| `remote_encoding_switch` then immediate `pred=1` neutral | Quasi-digital / grace prediction regression |
| `pred sy=85` / `pred sx=85` vs remote neutral or partial analog | False digital promotion — see `netinput_false_digital_prediction_2026-05-18.md` |
| `ROLLBACK_IDENTITY_DRIFT` after keyboard onset resim | Resim replay non-determinism or wrong confirmed span |

## Integrity-first re-soak (2026-05-18)

Run **both** peers with false-digital fix + mixed-input env from [`netinput_false_digital_prediction_2026-05-18.md`](bugs/netinput_false_digital_prediction_2026-05-18.md).

| Env / log | Pass |
|-----------|------|
| No `pred sx=85` / `pred sy=85` | False-digital regression absent |
| `SSB64_NETPLAY_ITEM_HASH_TRACE=1` at resim target tick | Host/client walk order + final hash match |
| `RESIM_POST_MATCH` at each resim boundary | Cross-peer post-resim digests agree (`load=` must not be `4294967295`) |
| `POST_RECOVERY_CONVERGENCE_OK` | Two consecutive frame-commit compares after recovery (`SSB64_NETPLAY_FRAME_COMMIT_TOKEN=1`) |
| NetSync validation from ~3900+ | No sustained `figh` diff while `all` matches |
| `FRAME_COMMIT_DIAG compared > 0` | Frame-commit pairing ran (`SSB64_NETPLAY_FRAME_COMMIT_TOKEN=1`) |
| Host closes window | Peer receives `VS_SESSION_END` and stops in ≪180 render frames (not `STRICT MISS` stall) |

## Out of scope (longer term)

These are **not** required for the 2026-05-18 symmetric resim execution work (wire-locked span, baseline gate, snapshot save hygiene, cosmetic RNG on load, confirmed-only remote reconcile). Revisit only if soak still shows divergence after the implemented fixes.

| Topic | Rationale |
|-------|-----------|
| **Full snapshot byte exchange** | Peers still load **local** ring slots at `load_tick`. Cross-peer `ROLLBACK_BASELINE` digest agreement at load is the chosen substitute: if `figh`/`world`/`item`/`rng` match before forward sim, identical inputs + span should converge without shipping full snapshot blobs. |
| **Pure independent GGPO (disable symmetric notify)** | Symmetric wire notify remains the **execution contract** until both peers independently detect the same mismatch tick *and* produce the same resim span without notify. `SSB64_NETPLAY_ROLLBACK_SYMMETRIC=0` is for experiments only. |
| **Anim in snapshot / hard-fail on anim-only `LOAD_HASH_DRIFT`** | Animation hash uses `syNetSyncHashFighterAnimationStateForRollback()`; figatree can advance one frame during load before verify. **Anim-only** `LOAD_HASH_DRIFT` always soft-continues resim (gameplay hashes unchanged). Do not block resim on anim-only drift unless a gameplay regression proves it necessary. |
