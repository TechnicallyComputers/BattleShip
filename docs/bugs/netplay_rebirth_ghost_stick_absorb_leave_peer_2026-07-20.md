# Netplay — RebirthWait `is_ghost` stick absorb blocks leave GGPO → PEER figh

**Date:** 2026-07-20  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** `1174892281` (Android client + Linux host), seed `2027437158`

## Symptom

- Pair/seed OK; synctest 34 OK / 0 FAIL; `LOAD_HASH_DRIFT=0`; `ggpo real_stick=0`.
- P1 `REBIRTH_GATE` halo from ~4001; both in RebirthWait(9) through 4177 with matching light.
- **4178:** Android stick `(0,-32)` / light fork; Linux stick `(0,0)`; both still status=9; anim match.
- **4179:** Android → Fall(26); Linux stays RebirthWait(9).
- Android `PEER_SNAPSHOT_DIVERGE` @ load **4180** (`REPLAY_DETERMINISM`): figh+anim; map/rng/world match.
- Scan: `STATUS_FORK@4179` earliest; PHYSICS `topn_ty@4182` cascade; many `RESIM_STICK_FORK`.

## Root cause

1. Vanilla: `ftCommonRebirthWaitProcInterrupt` → `ftCommonGroundCheckInterrupt` (stick) → Fall + inv frames.
2. Authority (Android, local P1) publishes leave stick and drops.
3. Predictor held feel-0 `(0,0)`.
4. `syNetplayPlayerInDeadGhostStickAbsorbScope` keyed on **`is_ghost`**. RebirthWait sets `is_ghost=TRUE`, so stick-only GGPO REPLACE could be absorbed the same way as Dead* (Whispy LCG soak `1790844706`) — leave stick never heals on the predictor before PEER deepen.

July 12 FC input-skew wait ([`netplay_fc_rebirth_stick_drop_input_skew_2026-07-12.md`](netplay_fc_rebirth_stick_drop_input_skew_2026-07-12.md)) still applies; this kill was **PEER**, not FC.

## Fix

1. **Absorb scope = Dead\* only** — remove bare `is_ghost` from `syNetplayPlayerInDeadGhostStickAbsorbScope`.
2. Always-on `REBIRTH_LEAVE_STICK` on halo-timer / ground-interrupt leave (+ catch-up).
3. Always log `dead_ghost_stick` skips (no rate-limit hide).
4. Scan-drift: never demote RebirthWait↔Fall; emit `REBIRTH_LEAVE_FORK`.

## Acceptance

KO → RebirthWait → mash stick off halo (Android↔Linux):

- [ ] Predicting peer gets stick GGPO / matching leave tick (both Fall).
- [ ] No PEER figh-only from RebirthWait vs Fall stick-Y.
- [ ] Dead\* stick absorb still Promote-without-rewind (Whispy LCG class).
- [ ] SoftLip / TopN Y not “fixed” for this path.
