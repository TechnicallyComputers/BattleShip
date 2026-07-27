# Protocol GGPO storm → seal-rows exhaust hang (2026-07-25)

**Status:** OPEN (`PORT && SSB64_NETMENU`) — diagnose / fix pending  
**Soak:** soak1 session `1845693596` seed `3246477153` — Android client (lp=1) ↔ Linux host (lp=0)  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** correction / seal protocol (not `replay_determinism` / `PEER_SNAPSHOT_DIVERGE`)

## Symptom

Hold-last / zero-onset chain is clean on this soak:

| Check | Result |
|-------|--------|
| `PEER_SNAPSHOT_DIVERGE` / `BASELINE_UNIVERSE` / `class=replay_determinism` | **0** |
| Go `ADVANCE_FORCE` | OK @390 |
| NetSync `figh` @120/240/360 | Match |
| Soft corrections | ~22× `EPISODE_PROOF class=protocol` (`agree_through_load=1`) from `hold_last_smash_dash_clamp` / flip / release |

Session still **hangs then dies** near tick 465:

```text
[Linux]  EPISODE_SEAL_ROWS_WAIT load_tick=464 missing_slots=0x2   (slot 1 empty; many repeats)
[Linux]  RESIM_SEAL_ROWS_TIMEOUT load_tick=464 missing_slots=0x2
[Linux]  RESIM_BASELINE_TIMEOUT ... baseline_matched=1 seal_rows_missing=0x2 streak=3
[Linux]  RESIM_SEAL_ROWS_EXHAUSTED load_tick=464 missing_slots=0x2
         (baseline matched; no deepen — hard desync path)
[Linux]  RESIM_BASELINE_TIMEOUT streak — hard desync recovery
[Android] received VS_SESSION_END role=client tick=465
```

User-visible: brief freeze / rubber-band during late dual-stick play, then match abort. Forward drift scan can look soft-stable (no PEER) while the kill is seal-gate only.

## Timeline (late match)

| Phase | Detail |
|-------|--------|
| 411–463 | Cascading short `class=protocol` episodes (clamp/send-lead stick predict) |
| epoch20 | Android `target=465`; Linux `target=466` — tuple already skewing |
| epoch21/22 | Linux `mismatch=465 load=464 target=467`; Android `local_mismatch=466` + `PEER_SYMMETRIC_CLAMP_RESOLVED` |
| Seals | Linux `EPISODE_SEAL_ROWS_SEND` **slot=0** digest `0xAF725806`; Android `COMPATIBLE_APPLY` slot=0 digest `0x3A5DB794` (disagree) |
| Wait | Linux `WAIT_DETAIL load_tick=464 slot=1 … first_invalid_idx=0` — never receives peer slot-1 rows |
| Kill | Exhaust with **baseline matched** → hard desync (deepen suppressed by design) |

## Root cause (working)

Distinct from invent / smash hold_last PEER seeds (those are fixed on this soak).

1. **Protocol GGPO storm** — send-lead `smash_dash_clamp` / flip create many short agree-through-load corrections. Soft recovery keeps `figh` matched but hammers episode open/seal.
2. **Episode tuple / seal digest fork** — peers open overlapping epochs with different mismatch/target; `COMPATIBLE_APPLY` runs but slot-span digests diverge → exchange does not complete a consistent sealed table.
3. **Asymmetric slot wait** — host waits forever on `missing_slots=0x2` (remote P1). Prior fixes (`CORRECTION_CLAMP_RESOLVED`, `COMPATIBLE_APPLY`, `HOLD_NO_DEEPEN`) are present; self-seal / peer join still fails to fill slot 1 before timeout streak.
4. **Fail-closed** — matched baseline + seal-only miss → no deepen → `RESIM_SEAL_ROWS_EXHAUSTED` → session kill (same kill shape as older seal-exhaust bugs, new *traffic* seed).

## Not this bug

- [`netplay_hold_last_dash_clamp_jumpaerial_softlip_peer_2026-07-26.md`](netplay_hold_last_dash_clamp_jumpaerial_softlip_peer_2026-07-26.md) — soak `2042477761`: PEER / `replay_determinism` from mid-JA dash clamp (not seal exhaust)
- [`netplay_hold_last_quasi_digital_smash_skip_peer_2026-07-25.md`](netplay_hold_last_quasi_digital_smash_skip_peer_2026-07-25.md) — ACCEPTED; 0× PEER on this same soak
- [`netplay_ggpo_behind_resolved_through_seal_stall_2026-07-12.md`](netplay_ggpo_behind_resolved_through_seal_stall_2026-07-12.md) — behind-`resolved_through` reject; here clamp lines fire and `baseline_matched=1`
- [`netplay_seal_tuple_fork_asymmetric_stall_2026-07-12.md`](netplay_seal_tuple_fork_asymmetric_stall_2026-07-12.md) — introduced `COMPATIBLE_APPLY` / no-deepen-on-seal-miss; residual still dies after compatible apply + digest fork under protocol storm

## Fix direction (not implemented)

Investigate in order (NETMENU only):

1. Why slot-1 seals never land / never validate while slot-0 `COMPATIBLE_APPLY` storms (join reject? early-stash flush? authority slot mask?).
2. Whether protocol stick REPLACE should coalesce / span-extend more aggressively to cut episode rate under dual-stick clamp churn.
3. Whether digest mismatch on compatible apply should fail closed earlier (cancel episode / renegotiate) instead of wait→exhaust.
4. Self-seal fallback when `baseline_matched=1` and peer slot absent but wire-confirmed remote history covers the span (see peer-absent FC doc pattern) — only if safe under rollback contracts.

Do **not** SoftLip-harden or invent new hold_last mirrors for this kill.

## Acceptance (when fixed)

Dual-stick soak that previously stormed protocol GGPO through ~460:

- No prolonged `SEAL_ROWS_WAIT missing_slots=0x2` → `RESIM_SEAL_ROWS_EXHAUSTED` → `VS_SESSION_END`
- Soft `class=protocol` OK; 0× PEER still required
- Prefer episode coalesce / successful seal exchange over fail-closed kill when baseline matches

## Related

- [`netplay_seal_epoch_skew_identical_span_2026-07-26.md`](netplay_seal_epoch_skew_identical_span_2026-07-26.md) — same kill shape; **fixed** epoch-only skew when `(mismatch,target)` already match (soak `503281020`)
- [`netplay_hold_last_quasi_digital_smash_skip_peer_2026-07-25.md`](netplay_hold_last_quasi_digital_smash_skip_peer_2026-07-25.md) — same soak; figh PEER PASS; session still killed here
- [`netplay_seal_tuple_fork_asymmetric_stall_2026-07-12.md`](netplay_seal_tuple_fork_asymmetric_stall_2026-07-12.md) — compatible apply / HOLD_NO_DEEPEN
- [`netplay_ggpo_behind_resolved_through_seal_stall_2026-07-12.md`](netplay_ggpo_behind_resolved_through_seal_stall_2026-07-12.md) — clamp resolved
- [`netplay_fc_recovery_seal_rows_peer_absent_2026-06-11.md`](netplay_fc_recovery_seal_rows_peer_absent_2026-06-11.md) — baseline_matched=1 + missing peer slot
- [`netplay_shared_correction_frontier_2026-07-19.md`](netplay_shared_correction_frontier_2026-07-19.md) — frontier / exhaust kill shape
