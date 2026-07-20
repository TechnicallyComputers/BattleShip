# Post-Go `wire_need` hang after intro Wait soft-pacing (2026-07-18)

**Soak:** soak1 Linux + Android after intro Wait advance/FC-defer + resim `time_passed` fix. Countdown/Appear smooth; permanent freeze at GO.

## Symptom

- Android last `INPUT recv` ~seq 395 during late Wait; `hr` stuck at **394** through Go.
- Wait skips `wire_need`/`runway_cap` → sim still advances to ~398.
- At Go, hard wire pacing returns → `sim advance blocked (wire_need) next_sim=399 hr=394 wire_need=395`.
- Stall INPUT egress capped at **3**/sim-tick → four sends then silence while wall frames continue.
- Linux briefly unblocks on Android’s burst, then also `wire_need` when Android stops sending.
- `FRAME_COMMIT_DIAG sent=1 recv=0` both sides (first FC after Wait defer).

## Root cause

1. **Wait soft-pacing cliff** — Appear exemption lets sim run over a frozen `hr` (Android ICE recv blackhole). Re-enabling full `wire_need` at Go hangs when `hr` never moves.
2. **FC cadence not seeded during Wait defer** — `LastFrameCommitValidationTick` stayed 0 → first post-Go completed step minted an immediate catch-up FC.
3. **Stall send cap** — 3 sends/sim-tick while Advance held starves the peer after a short burst.

## Fix

1. **Post-Go wire pacing grace** (`netinput.c`) — after Wait→Go, keep Wait-style soft Advance (`hr==0` only) for `D + pred + 24` (min 32) sim ticks.
2. **FC defer seeds cadence** (`netpeer.c`) — while Wait, advance `LastFrameCommitValidationTick` without minting/sending.
3. **Stall send cap 3 → 48** — keep feeding peer `hr` during Advance holds.

## Verify

Re-soak: log `post_go_wire_pacing_grace until=…`; no permanent `wire_need` hang at first Go; first FC not immediately at Go tick; both peers keep advancing past ~400.

## Related

- [`netplay_intro_wait_advance_frontier_deadlock_2026-07-18.md`](netplay_intro_wait_advance_frontier_deadlock_2026-07-18.md)
- [`netplay_resim_time_passed_world_skew_2026-07-18.md`](netplay_resim_time_passed_world_skew_2026-07-18.md)
