# Netplay rollback test matrix

Manual soak checklist and regression env presets for rollback (`netrollbacksnapshot.c`, `netrollback.c`, `netinput.c`).

See also: [`netplay_rollback_refactor_contracts.md`](netplay_rollback_refactor_contracts.md).

## Env presets

| Scenario | Suggested env |
|----------|----------------|
| Automatch default | Unset `SSB64_NETPLAY_AUTO_SESSION_PARAMS` (negotiates rollback + transport from RTT); verify `NetSession: apply` and `NetRollback: session_negotiated` logs |
| Baseline 1v1 | `SSB64_NETPLAY_ROLLBACK=1`, `SSB64_NETPLAY_ROLLBACK_SNAPSHOT_FRAMES=32` |
| Deep rollback | `SSB64_NETPLAY_ROLLBACK_SNAPSHOT_FRAMES=64` (or ≥ rollback span under test) |
| ~200 ms RTT netem | `tc netem delay 95ms 15ms distribution normal loss 0.3%` both peers; expect `rtt_ms≈200`, `D≈6–7`, `phase_lock≈8` (capped), `rb_snap≥48`, `fuzz≥2`, `redundancy≥2`; watch `rb=` in NetSync (prediction + resim, not lockstep-only) |
| ~84 ms RTT netem | `tc netem delay 38ms 8ms loss 0.2%` both peers; expect `rtt_ms≈80–90`, `D≈4`, `phase_lock≈6` (good band, ~1.5:1 pred:D), `redundancy=2`; host and guest `rb=` should both increment under stick churn |
| Forced resim | `SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH=1`, `SSB64_NETPLAY_ROLLBACK_INJECT_TICK=<wire_or_sim per inject docs>` |
| Load verify | `SSB64_NETPLAY_ROLLBACK_LOAD_HASH_VERIFY=1` (default); `=0` debug-only |
| Catch-up budget | `SSB64_NETPLAY_ROLLBACK_RESIM_TICKS_PER_FRAME=4` |
| Symmetric follower (auto default) | Auto session params enable **symmetric follower** resim (peer rollback notices apply on both sides). `SSB64_NETPLAY_ROLLBACK_SYMMETRIC=0` for GGPO-only independent correction |
| Legacy / force symmetric | `SSB64_NETPLAY_ROLLBACK_SYMMETRIC=1` (same follower path; explicit) |
| Symmetric diag-only | `SSB64_NETPLAY_ROLLBACK_SYMMETRIC_DIAG=1` (log peer notices without follower resim) |
| Conservative remote buttons | Default: buttons predict **0** under delay; `SSB64_NETPLAY_PREDICT_REMOTE_BUTTONS_HOLD=1` restores hold-last (shield-tap churn risk) |
| Digital tap patch (no resim) | Default on; `SSB64_NETPLAY_GGPO_DIGITAL_TAP_PATCH=0` forces rollback on 1-frame ±85 taps |
| Delay vs prediction ratio | See negotiated `NetSession: apply` line: excellent RTT → `phase_lock≤D`; good → `phase_lock≤D*1.5`; high RTT → `phase_lock` capped at 8 |
| State detail | `SSB64_NETPLAY_STATE_DETAIL_DIAG=1` (world); `=2` (+ fighter detail) |
| Resim trace | `SSB64_NETPLAY_RESIM_TICK_TRACE=1` |
| Resim RNG verify | `SSB64_NETPLAY_RESIM_RNG_VERIFY=1` |
| Peer snapshot abort | `SSB64_NETPLAY_ROLLBACK_PEER_SNAPSHOT_ABORT=1` (default); `=0` debug-only |
| Scan clean diag | `SSB64_NETPLAY_ROLLBACK_SCAN_DIAG=1` |
| Synctest (when implemented) | `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1` |
| High-latency prediction soak | `SSB64_NETPLAY_DELAY=2`, `SSB64_NETPLAY_PHASE_LOCK_PREDICTION_TICKS=3..7`, `SSB64_NETPLAY_INPUT_PREDICTION=1`, `SSB64_NETPLAY_STRICT_SLACK_FRAMES=0` |

## Cases

1. **No-items 1v1** — no `LOAD_HASH_DRIFT` after forced resim; no `ROLLBACK_IDENTITY_DRIFT`.
2. **Projectile-heavy** — Link/Samus/Pikachu; weapon snapshot + hash stable.
3. **Item-heavy** — item spawn/active hashes stable; no ghost items after rollback.
4. **4-player / high delay** — ring depth 64+; mismatch scan + paced resim.
5. **Jump / stick storm** — rapid remote stick changes at prediction windows 3, 5, and 7; no equal-gameplay rollback storm; newer corrected packets may log `REMOTE_CONFIRMED_REPLACE_NEWER`, but stale older packets should be the only `REMOTE_CONFIRMED_CONFLICT` cases.
6. **Packet loss / gap** — `tc netem` or reorder; strict `MISS (R)` should not deadlock (or session abort cleanly).
7. **Ring underrun** — deep rollback with `SNAPSHOT_FRAMES=32` and mismatch >32 ticks ago; expect load-fail log, not SIGSEGV.
8. **Cap overflow** (when instrumented) — >16 items or >32 weapons; loud failure, not silent truncation.

## Pass criteria

- Resim completes to frontier without unrecoverable load failures.
- No `LOAD_HASH_DRIFT` for covered subsystems after successful load verify.
- No `ROLLBACK_IDENTITY_DRIFT` when confirmed inputs unchanged across resim.
- No `REMOTE_CONFIRMED_CONFLICT` from synthetic gap-fill replacement.
- Gameplay continues without ghost items/weapons after rollback.
- With `SSB64_NETPLAY_ROLLBACK_SYMMETRIC=0`, peers still converge via independent GGPO correction; both sides should show `rb=` growth under stick churn.
- **Symmetric resim execution:** host and client log identical `target_tick` / `span` on each `resim begin` when symmetric follower is active; `resim baseline gate open` (or timeout) before forward sim; matching `figh`/`world`/`item`/`rng` on `resim baseline` at the same `load_tick`; no `RESIM_BASELINE_MISMATCH` during soak.

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
| `resim reconcile missing confirmed remote` | Wire hole during resim span — no predicted fallback applied |

## Out of scope (longer term)

These are **not** required for the 2026-05-18 symmetric resim execution work (wire-locked span, baseline gate, snapshot save hygiene, cosmetic RNG on load, confirmed-only remote reconcile). Revisit only if soak still shows divergence after the implemented fixes.

| Topic | Rationale |
|-------|-----------|
| **Full snapshot byte exchange** | Peers still load **local** ring slots at `load_tick`. Cross-peer `ROLLBACK_BASELINE` digest agreement at load is the chosen substitute: if `figh`/`world`/`item`/`rng` match before forward sim, identical inputs + span should converge without shipping full snapshot blobs. |
| **Pure independent GGPO (disable symmetric notify)** | Symmetric wire notify remains the **execution contract** until both peers independently detect the same mismatch tick *and* produce the same resim span without notify. `SSB64_NETPLAY_ROLLBACK_SYMMETRIC=0` is for experiments only. |
| **Anim in snapshot / hard-fail on anim-only `LOAD_HASH_DRIFT`** | Animation hash uses `syNetSyncHashFighterAnimationStateForRollback()`; figatree can advance one frame during load before verify. **Anim-only** `LOAD_HASH_DRIFT` always soft-continues resim (gameplay hashes unchanged). Do not block resim on anim-only drift unless a gameplay regression proves it necessary. |
