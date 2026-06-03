# Netplay Fox Firefox (SpecialHi) launch gate — 2026-05-28

**Status:** FIX SHIPPED (soak pending)

## Symptom (phase 1 — hold)

Fox Up+B (Firefox) charge starts and pulsing ring VFX play, but Fox never launches into the flame burst. Fox remains stuck in the charge pose (`SpecialHiHold` / `SpecialAirHiHold`) after rings stop.

Observed on cross-ISA netplay (Android host / Linux guest) around tick 749 in a Mario/Fox session.

## Symptom (phase 2 — travel)

After the hold fix, Fox launches consistently but never exits Firefox flight. Deceleration runs, then velocity reverses and Fox accelerates again until a hard collision or KO. Logged as long runs at `status=232` (`SpecialAirHi`) with `SYNCTEST_FAIL` at ticks 1464, 1740, 1970 (defer ended when travel started).

## Root cause

Same `== 0` gate-miss class as rebirth `dead.wait`:

1. **Hold** — `ftFoxSpecialHiHoldProcUpdate` transitions only when `launch_delay == 0`. Restore/resim can leave `launch_delay` negative.
2. **Travel** — `ftFoxSpecialHiProcUpdate` transitions to End only when `anim_frames == 0` (30-frame travel timer). Restore/resim can leave `anim_frames` negative; `ftFoxSpecialAirHiProcPhysics` then keeps subtracting velocity along `angle` with no floor → slowdown, reversal, re-acceleration.

Synctest defer originally covered only `SpecialHiStart` … `SpecialAirHiHold`, so rollback loads during `SpecialAirHi` travel were unprotected.

## Fix

1. **Synctest defer** — `reason=fox_firefox` while any live Fox is in `SpecialHiStart` … `SpecialAirHiBound` (full Up+B sequence).
2. **Launch catch-up** — `syNetplayFoxCatchUpFirefoxLaunchIfDue` when hold + `launch_delay <= 0`.
3. **End catch-up** — `syNetplayFoxCatchUpFirefoxEndIfDue` when travel + `anim_frames <= 0` → vanilla `ftFoxSpecialHiEndSetStatus` / `ftFoxSpecialAirHiEndSetStatus`.
4. **Proc rebind** — After apply when Fox is in defer scope (includes travel/end/bound).
5. **Diagnostics** — `fighter_field_diff`: `fox_launch_delay` (hold), `fox_anim_frames` / `fox_decelerate_wait` / `fox_angle` (travel). Optional `SSB64_NETPLAY_FOX_FIREFOX_GATE_DIAG=1` logs `launch_delay_zero` and `anim_frames_zero`.

Implementation: [`port/net/sys/netplay_fox_firefox_gate.c`](../port/net/sys/netplay_fox_firefox_gate.c), wired from [`port/net/sys/netrollbacksnapshot.c`](../port/net/sys/netrollbacksnapshot.c).

## Soak pass criteria

Mario/Fox cross-ISA match; trim ticks around Firefox use (charge ~735–770, travel ~1460–1980):

- No stuck Fox at `status=212`/`218` (hold) with negative `fox_launch_delay`.
- No long runs at `status=232`/`229` (travel) with negative `fox_anim_frames`.
- Fox exits to End/fall within ~30 frames after burst; no runaway deceleration loop.
- No `SYNCTEST_FAIL` mid Firefox sequence (defer covers charge + travel + end/bound).

Pair with Fox Special-Lw soak env vars if testing both moves in one session.
