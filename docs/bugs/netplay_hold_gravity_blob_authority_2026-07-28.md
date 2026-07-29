# Netplay: Ness PK Thunder Hold gravity — blob authority (retire tracking sanitize)

**Date:** 2026-07-28  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending) — **incomplete alone**  
**Soak:** Android guest ↔ Linux host, seed `1961215037`, session `313936707`  
**Follow-on root (wipe source):** [Ness/Fox SpecialHi overlay collision](netplay_ness_specialhi_fox_overlay_collision_2026-07-28.md) (seed `386129764`)  
**Protocol hang (same soak):** [healed frontier honest FC](netplay_fc_healed_frontier_honest_fc_2026-07-28.md)  
**Prior band-aid:** [hold gravity delay resurrect FC](netplay_ness_hold_gravity_delay_resurrect_fc_2026-07-19.md)

## Symptom

```text
NESS_PKTHUNDER_GATE hold_gravity_resurrect_blocked status=233
  Android @1267 live=0 expected=1 status_tics=1
  Linux   @1267 live=1 expected=2 status_tics=1
PHYSICS_FORK / FC@1276 FIELD_PEER topn_ty (inputs MATCH)
```

Peers disagreed on `pkthunder_gravity_delay` during forward Hold; fall ladder / TopN.y forked.

## Root cause

Vanilla sets `pkthunder_gravity_delay` at SpecialHi Start and **preserves** it into Hold (does not reset). The netplay gate reconstructed an “expected” countdown from out-of-blob `HoldEntryGravityDelay` / tick tracking and rewrote (or asymmetrically refused to rewrite) the live blob. That is context patching, not snapshot fidelity — peers with skewed entry tracking forked fall onset.

## Fix (architecture)

`PORT && SSB64_NETMENU`:

1. **`syNetplayNessSanitizePKThunderGravityDelay`** — clamp to `[0, FTNESS_PKTHUNDER_GRAVITY_DELAY]` only. Never resurrect or lower from tracking.
2. **Remove** `syNetplayNessExpectedGravityDelayFromTracking` (unused after clamp-only).
3. Hold-entry tracking arrays remain for jibaku delay / throw-entry rebuild until those paths are similarly retired; they no longer drive gravity.

Canonical fall / Harden paths still read the **blob** delay after clamp.

## Acceptance

- [ ] Re-soak Ness Hold: no `hold_gravity_resurrect_blocked` / `sanitize_gravity` rewrite logs; peers share gravity_delay when inputs agree.
- [ ] FC@Hold `topn_ty` physics forks from asymmetric gravity delay should disappear or become honest fail-closed after [honest FC](netplay_fc_healed_frontier_honest_fc_2026-07-28.md) resim — not hung `recovery_started=0`.
