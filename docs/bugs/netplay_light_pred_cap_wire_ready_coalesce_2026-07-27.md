# Netplay — light PRED_CAP micro-cascade invents hold_last past wire → SoftLip fork + hang

**Date:** 2026-07-27  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** Android guest ↔ Linux host, Dream Land, seed `2609295990`  
**Follow-on to:** [pred-cap resolved floor](netplay_light_episode_resolved_floor_pred_cap_2026-07-27.md), [MERGE_DEEPEN pred hold](netplay_light_pred_span_merge_deepen_hold_last_2026-07-27.md)

## Symptom

- `SNAP_AGREE_MISMATCH` from **snap=400** (figh diverge; world/rng match).
- Linux: **248** `local_light` episodes / **494** `RESOLVED_THROUGH_PRED_CAP`; Android: 6 light (P0 only).
- FC@**961** `state_diverge=1`, `inp` agree, `class=replay_determinism`; onset=462, `last_agreed=441`, SoftLip hint.
- Hang: Linux `runway_cap` spam → `VS_SESSION_END` @906; Android `PEER_SNAPSHOT_DIVERGE` @835 + `sym_reject_cap`.

## Root cause

Android owns P1 LOCAL sticks `@400–403`: `(13,0)→(50,0)→(62,0)→(66,0)`.

Linux light ep `mismatch=400 target=404`:

| Tick | Linux resim | Android LOCAL |
|------|-------------|---------------|
| 400 | wire `(13,0)` | `(13,0)` |
| 401–403 | **hold_last `(13,0)`** | `(50)/(62)/(66)` |

`PRED_CAP` + `DEFERRED_KEEP` correctly retained the follow-up, but kept **target=405** while only wire through 401 existed → next light ep again hold_lasted ahead → **one-tick chase** (400→401→402→…) that never matched the owner's SoftLip ramp. Input history eventually agreed at FC; physics did not.

`DEFERRED_RAISE` / `MERGE_DEEPEN_PRED_HOLD` did not fire (different failure mode).

## Fix

`PORT && SSB64_NETMENU`:

1. **`syNetInputContiguousRemoteConfirmedThrough`** — highest sim tick with contiguous ledger/wire from `from_tick`.
2. **`LIGHT_WIRE_READY_{CLAMP|WIDEN}`** — align light / deferred exclusive targets to that contiguous end:
   - **Clamp** when target would invent hold_last past known wire.
   - **Widen** when more contiguous wire is already available (one ep covers the burst).
3. Call sites: `DEFERRED_KEEP/RAISE_PRED_SPAN`, correction MERGE/QUEUE, `TryBeginDeferred`, `BeginResim` (light).

Multi-slot deferred uses **MIN** across dirty remotes (union Begin needs every slot wired).

## Acceptance

- [ ] Stick onset soak: `LIGHT_WIRE_READY_*` on KEEP/Begin; light targets stay at wire exclusive end (no span-4 hold_last ahead of ledger).
- [ ] No permanent SoftLip / SNAP mismatch storm from the first stick ramp; FC `state_diverge=0`.
- [ ] Episode count may still be high under continuous stick, but each ep heals with real wire; no `runway_cap` hang from PRED micro-cascade.

## Follow-on

Dual-wire-ready soak `891751718`: Begin bounded but **Close** still raised the frontier past
the wire (watermark-only cap), and the FC diverge exit was re-suppressed forever by
seal-wait/absorb windows. Fixed in
[netplay_close_wire_cap_diverge_hang_exit_2026-07-27.md](netplay_close_wire_cap_diverge_hang_exit_2026-07-27.md).
