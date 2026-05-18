# Netplay rollback test matrix

Manual soak checklist and regression env presets for rollback (`netrollbacksnapshot.c`, `netrollback.c`, `netinput.c`).

See also: [`netplay_rollback_refactor_contracts.md`](netplay_rollback_refactor_contracts.md).

## Env presets

| Scenario | Suggested env |
|----------|----------------|
| Baseline 1v1 | `SSB64_NETPLAY_ROLLBACK=1`, `SSB64_NETPLAY_ROLLBACK_SNAPSHOT_FRAMES=32` |
| Deep rollback | `SSB64_NETPLAY_ROLLBACK_SNAPSHOT_FRAMES=64` (or ≥ rollback span under test) |
| Forced resim | `SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH=1`, `SSB64_NETPLAY_ROLLBACK_INJECT_TICK=<wire_or_sim per inject docs>` |
| Load verify | `SSB64_NETPLAY_ROLLBACK_LOAD_HASH_VERIFY=1` (default); `=0` debug-only |
| Catch-up budget | `SSB64_NETPLAY_ROLLBACK_RESIM_TICKS_PER_FRAME=4` |
| Symmetric off (target default) | `SSB64_NETPLAY_ROLLBACK_SYMMETRIC=0` |
| State detail | `SSB64_NETPLAY_STATE_DETAIL_DIAG=1` (world); `=2` (+ fighter detail) |
| Resim trace | `SSB64_NETPLAY_RESIM_TICK_TRACE=1` |
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
- With symmetric disabled, peers converge without `peer symmetric rollback` driving gameplay.

## Fail signatures (investigate)

| Log | Action |
|-----|--------|
| `LOAD_HASH_DRIFT` | Snapshot incomplete or destructive load — stop / fix restore |
| `ROLLBACK_IDENTITY_DRIFT` | Resim not deterministic or wrong inputs during replay |
| `REMOTE_CONFIRMED_CONFLICT` | Confirmed immutability / gap-fill authority bug |
| `remote_gap_fill` | Note whether used for admission only vs rollback authority |
| `abort resim — snapshot load verify failed` | Expected if ring underrun or drift |
| `STRICT MISS (R)` sustained | Wire hole or recovery window stall |
