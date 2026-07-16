# Netplay: Ness PK Thunder Hold stick-aim GGPO deferred → non-jibaku hang

**Date:** 2026-07-15
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)
**Soak:** session `887986884` seed `385025777` — STABLE / PASS (no drift), hang on 2nd PK Thunder→jibaku

## Symptom

Match stays synced (`LOAD_HASH_DRIFT=0`, synctest OK, soft recovery). ~12 short span-2 resims
from `LEDGER_REFRESH_COMPLETED_SIM_CORRECT` are barely noticeable. Second Ness PK Thunder Hold
feels hung: stick aim never brings thunder back for jibaku; session ends mid-Hold (~1968).

```
LEDGER_REFRESH_COMPLETED_SIM_CORRECT … sim_tick=1963 … old_sx=-83 … new_sx=-81
ggpo deferred lift_livecap mismatch_tick=1963 … (ness_pk_defer)
try_begin_fail stage=ness_pk_defer mismatch=1963 … sim=1964 last_committed=1791
hold_tick … status=233 … (status_tics climbs; no jibaku_collide)
received VS_SESSION_END … tick=1968
```

First Hold→jibaku @1497 (`hold_frames=50`) succeeded. Second Hold never `jibaku_trigger`.

## Root cause

1. **Completed-sim stick invent** (hold-last / provisional publish) correctly queues GGPO via
   `LEDGER_REFRESH_COMPLETED_SIM_CORRECT` (prior fix). Mid-Hold that correction is the *aim*
   channel for PK Thunder.
2. **`ness_pk_defer` blocked TryBegin through the full Hold span** (`FcResimDeferScope` includes
   Start/Hold/End). Live-cap was lifted for that same scope (2026-07-13 hang fix), so Hold kept
   simulating on the already-consumed wrong stick — thunder arc forked, jibaku never fired.
3. Policy intended “finish Hold on live, then rewind.” For stick-aim REPLACE that is wrong:
   finishing Hold *requires* the corrected aim.

FC state recovery already deferred only the **volatile** window (jibaku/bound/post-cull/launch
guard) via `FcStateRecoveryDeferScope`. Input GGPO still used the broader Hold-inclusive scope.

## Fix (`PORT && SSB64_NETMENU`)

1. **`netrollback.c` — align input GGPO defer with volatile-only scope.**
   `syNetRollbackDeferResimForNessPKThunder` now uses
   `syNetplayNessAnyLiveFighterInFcStateRecoveryDeferScope()` (same as FC recovery). Hold/Start/End
   allow TryBegin so mid-Hold stick REPLACE rewinds under existing Hold canonicalize hardening
   (`jibaku_resim_hold_drift`).
2. **Live-cap lift matches the same volatile scope.** During Hold, live-cap stays armed and
   Begin proceeds immediately (no further forked Hold ticks). Volatile jibaku/bound still lifts
   live-cap and defers Begin until safe.
3. **`netinput.c` — skip hold-last invent via promote** when `tick < sim_now` (completed-sim) or
   any fighter is in `FcResimDeferScope` (Hold span). Logs `REMOTE_PUBLISH_SKIP` reasons
   `hold_last_completed_sim` / `hold_last_ness_pk_scope`. Current-tick resolve still feeds
   ephemeral stick through `PublishFrame` for prediction.

## Verify

- Rebuild netmenu; re-soak Ness PK Thunder Hold with remote stick motion mid-Hold (2nd jibaku).
- Expect: mid-Hold `LEDGER_REFRESH_COMPLETED_SIM_CORRECT` → `resim begin` (not perpetual
  `try_begin_fail stage=ness_pk_defer`), then `jibaku_trigger` when aimed.
- Soft stick-only resims outside Hold may drop (`hold_last_completed_sim` skips).
- No new FC diverge on Hold resim (canonicalize path).

## Related

- `netplay_ness_pk_defer_ggpo_livecap_deadlock_2026-07-13.md` — live-cap × full-Hold defer freeze
- `netplay_input_authority_tuple_fork_fail_closed_2026-07-15.md` — completed-sim ledger refresh GGPO
- `netplay_ness_pkthunder_jibaku_resim_hold_drift_2026-07-10.md` — Hold resim canonicalize
