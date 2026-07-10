# Netrollback hybrid burst resim (2026-06-01)

## Symptoms

- Input corrections felt like slow-motion catch-up: forward replay spread at 4 ticks/frame over many display frames.
- Deviates from GGPO-style instant correction where typical 5–24 tick rollbacks complete in one sim burst.

## Root cause

`syNetRollbackAdvanceResimBudget()` always capped replay at `ResimTicksPerFrame` (default 4, negotiated 4–8), even when span was small enough to finish synchronously after the baseline gate opened.

## Fix

- **`syNetRollbackComputeResimTickLimit()`** — when `remaining <= MAX_BURST_TICKS` (default 24), replay the full remaining span in one call; else use per-frame budget.
- **`syNetRollbackAdvanceResimBudgetEx(max_ticks)`** — parameterized replay loop; wrapper preserves existing call sites.
- **`syNetRollbackTryOpenResimReplayGate()`** — calls `AdvanceResimBudget()` immediately when gate opens (no extra 1-frame delay before first replay tick).
- Env: **`SSB64_NETPLAY_ROLLBACK_MAX_BURST_TICKS`** (0 = disable burst).
- Raised budgeted fallback defaults: local init 12/frame; session negotiate tiers 12/16/20 (max 24).

Baseline gate, episode FSM, POST verify, and unified input reconcile unchanged.

## Verify

1. Low-RTT VS: trigger small GGPO correction (span ≤24). Log order:
   - `resim replay gate open`
   - `resim burst complete span=N ticks=N` (same frame/update)
2. Deep rollback (span >24): `resim budgeted catch-up span=... limit=.../frame`; multi-frame catch-up until target.
3. `SSB64_NETPLAY_ROLLBACK_MAX_BURST_TICKS=0`: budgeted-only behavior (no burst complete log).
4. Episode POST verify and `FinishForwardResim` still clear `ResimPending` (no zombie freeze).
