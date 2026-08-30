# Live-cap wedge: permanent load_fail_hold had no escape

**Exposed by:** the dual-Kirby cutter wedge (2026-08-30) — both peers stuck at tick 3851
in `tick_commit blocked (load_fail_hold)`, inputs flowing, frames pumping, ~15 s of dead
match until manual shutdown. `BATTLE_SIM_HOLD` never armed; nothing fired.
**Class:** pre-existing and independent of the blade bug that exposed it.

## Why nothing fired

`syNetRollbackShouldBlockLiveBattleAdvance()` blocks through two routes:

1. `sSYNetRollbackBattleSimHoldAfterLoadFail` — armed by actual snapshot load failures.
   This route HAS a proven escape: emergency restore → hold → `PumpLoadFailBattleExit`
   (unstarvable, per frame from the game loop) → 120-frame watchdog → VS teardown →
   clean battle exit / automatch abort (2026-08-21 work).
2. **The live-sim cap** (`sim_tick > cap`) — when frame-commit validation cannot pass,
   the cap never rises and this blocks forever. **No watchdog existed on this route.**
   The netinput log tag `load_fail_hold` covers both routes, which is why the wedge
   masqueraded as the already-fixed class.

## The escape (observation-only detection, escalation into the proven path)

- `netinput.c`: the commit gate's refusal branch records the refused tick
  (`syNetInputNoteLiveAdvanceBlocked`). Observation of the real gate — never probing —
  so a frozen tick in a menu/results screen, where the gate is not consulted, can never
  count as a wedge.
- `netrollback.c` (`syNetRollbackPumpLiveWedgeEscape`, run from the same unstarvable
  pump): tracks the **live frontier** — the highest tick reached *outside* resim. Resim
  episodes rewind `syNetInputGetTick()`, and the 2026-08-30 wedge churned failed
  `fc_recovery` episodes every few seconds, so a naive tick-unchanged counter would have
  reset on every episode and never fired. Frames count only while the last refused tick
  is at/past the frontier; any real forward progress resets.
- After `SSB64_NETPLAY_LIVE_WEDGE_ESCAPE_FRAMES` (default 600 ≈ 10 s; 0 disables) of
  zero live progress with the gate refusing, it logs `LIVE_WEDGE_ESCAPE` (frontier,
  refused tick, cap, cap_source) and calls `syNetRollbackArmBattleSimHoldAfterLoadFail`
  — from there the existing machinery runs: ~2 s watchdog, VS teardown (with peer
  notify), clean exit to character select. **~12 s wedge-to-menu, total.**

Guards: VS session active, rollback active, reconnect hold NOT active (a reconnect pause
legitimately freezes the sim and has its own 30 s forfeit clock), and env-disable.

## What it does not do

It does not try to *recover* the match. The wedge means frame-commit validation cannot
converge — live state has divergent history the ring never recorded. A forced rebaseline
(ring := live) could be attempted someday, but it invents a new primitive with its own
desync surface; the deliberate choice here is the clean, coordinated exit that already
exists. A match ending beats a match hanging; a match recovering is future work.

## Verification

Reproduce any wedge (dual-Kirby cutter churn did it) or force one
(`SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH` tooling): expect `LIVE_WEDGE_ESCAPE` after ~10 s,
`BATTLE_SIM_HOLD armed`, the teardown watchdog line, and both peers back at character
select. `SSB64_NETPLAY_LIVE_WEDGE_ESCAPE_FRAMES=300` shortens the wait for testing.
