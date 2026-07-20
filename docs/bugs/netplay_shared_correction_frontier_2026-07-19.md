# Shared correction frontier — behind-resolved GGPO seal hang

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-19  
**Session:** soak1 `642600542` seed `214537870` (Android client ↔ Linux host)  
**Layer cert:** `EPISODE_PROOF class=protocol agree_through_load=1` (admission / seal rendezvous)

## Symptom

| Field | Detail |
|-------|--------|
| Ep0 | GGPO P1 `btn 0→0x0008` @412 → load 411 target 420; both peers resim complete; post hashes match |
| Ep1 | Host opens `mismatch=414 load=413 target=422` (**behind** `resolved_through=420`) |
| Guest | `BASELINE_PREEMPTIVE_LIVE_CAP_SKIP stale … resolved_through=420`; no `resim begin` / no slot-1 seals |
| Kill | Host `RESIM_SEAL_ROWS_EXHAUSTED missing_slots=0x2` → hard desync → `VS session stop` (`late=414`) |

No `PEER_SNAPSHOT_DIVERGE`, FC `compared=0`. Not soft-lip / replay determinism.

## Root cause

1. **Shallow-only clamp** — `CORRECTION_CLAMP_RESOLVED` / peer soft-clamp only applied when `behind ≤ phase_lock` (4). Ep1 behind=6 skipped clamp.
2. **TryCommit episode reset** — when `mismatch < resolved` and `sim > resolved`, ordinary GGPO reset the episode anchor (meant for FC deepen) and began a behind-frontier span.
3. **Bilateral seal gate** — guest correctly refused join; host seal-wait had no “already settled” escape → fail-closed session kill.

Same family as [`netplay_ggpo_behind_resolved_through_seal_stall_2026-07-12.md`](netplay_ggpo_behind_resolved_through_seal_stall_2026-07-12.md) / [`netplay_episode_boundary_seal_hang_2026-07-12.md`](netplay_episode_boundary_seal_hang_2026-07-12.md).

## Fix (Phase 1 — Shared Correction Frontier)

| Change | Behavior |
|--------|----------|
| **Always clamp** | Local GGPO + peer SYNC soft-clamp any `mismatch < resolved_through` (remove phase_lock shallow cap). |
| **TryCommit gate** | Behind-resolved episode reset only for FC / state-hash deepen (`commit_behind_frontier` otherwise). |
| **Wire advertise** | `ROLLBACK_SYNC` +4B `resolved_through` (accept legacy/V1 sizes). |
| **Peer frontier** | `syNetRollbackNotePeerResolvedThrough` (monotonic). |
| **Seal cancel** | `FRONTIER_SEAL_CANCEL` when baseline matched, awaiting seals, and `mismatch < shared_frontier` — abandon episode, return Live (no session kill). |

## Verification

1. Rebuild desktop **and** Android (SYNC size changed).
2. Re-soak rapid button/stick GGPO shortly after a completed correction: expect `CORRECTION_CLAMP_RESOLVED` and/or no ep with `mismatch < resolved`; no `SEAL_ROWS_EXHAUSTED` → VS stop.
3. Grep: `FRONTIER_SEAL_CANCEL` only as recovery; `commit_behind_frontier` when Begin attempted behind without FC deepen.
4. Deep FC reanchor (state recovery) must still open past resolved when armed.

## Related

- [`netplay_baseline_load_clamp_frontier_2026-07-19.md`](netplay_baseline_load_clamp_frontier_2026-07-19.md) — HR clamp at frontier edge (follow-up soak `1734260705`)
- [`netplay_ggpo_behind_resolved_through_seal_stall_2026-07-12.md`](netplay_ggpo_behind_resolved_through_seal_stall_2026-07-12.md)
- [`netplay_correction_cascade_deepen_load_asym_2026-07-16.md`](netplay_correction_cascade_deepen_load_asym_2026-07-16.md)
- [`netplay_episode_proof_layer_certs_2026-07-19.md`](netplay_episode_proof_layer_certs_2026-07-19.md)
