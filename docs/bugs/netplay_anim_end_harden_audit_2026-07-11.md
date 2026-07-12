# Anim-end harden audit (post feel-0 / fixed-point wait)

**Date:** 2026-07-11  
**Context:** After feel-0 + iterative anim-end snaps, soak `988185944` still MATCH-failed on Wait/JumpB `status_total_tics` with **identical status_id trails**. Fixed-point wait countdown + broader locomotion scope landed in the same change set. This audit tracks remaining work and what landed in the follow-up pass.

## Contract (updated)

Under `PORT && SSB64_NETMENU` live/resim:

1. Joint `anim_wait` / `anim_speed` countdowns that gate `wait > 0` continue vs end should be **grid + integer** (not raw f32 subtract) on **DObj, MObj, and CObj** play paths.
2. Figatree / matanim / camanim `anim_wait += payload` adds should be **grid-quantized** when SimQuantize is active (`syNetplayAnimWaitAdd`).
3. Any fighter `proc_update` / helper that branches on `fighter_gobj->anim_frame <= 0.0F` (including `ftAnimEndCheckSetStatus`) must be covered by BeforeSim / post-tick harden **or** inherit determinism from (1) + gobj frame latch.
4. Status loops that **re-`SetStatus` on anim end** (Wait idle, some ground loops) reset `status_total_tics` — phase skew shows up as tics drift **without** status_id fork. FC field diffs on tics alone are first-class.

## Landed (2026-07-11 follow-up)

| Area | Mechanism |
|------|-----------|
| PlayAnim wait continue/end | Shared `syNetplayAnimCountdownFixedPoint` + AfterPlayStep leftover collapse |
| MObj mat-anim / CObj cam-anim | Same fixed-point countdown + leftover collapse under NETMENU |
| Figatree payload adds | `syObjAnimWaitAdd` → `syNetplayAnimWaitAdd` on DObj/MObj/CObj parse |
| BeforeSim / post-tick scope | Wait; WalkSlow…RunBrake; KneeBend…FallAerial (**Turn/TurnRun excluded** — dash-dance SetFlag1); Squat…LandingHeavy, Ottotto, Damage…DamageFall, FallSpecial/LandingFallSpecial, Dokan*, DownBounce…Passive, LightGet…LiftTurn, LightThrow…FireFlowerShootAir, Hammer*, GuardOff, EscapeF…FuraSleep, Catch…ThrownEnd, Attack11…LandingAirEnd |
| Kirby SpecialHi | SpecialHi / Landing / AirHi / AirHiFall in SpecialHi scope helper |
| GuardOn release, Link/CopyLink SpecialN, cliff | Existing scope retained |

## Still open (same nature)

### Medium — FC recovery after FORCE_MISMATCH

Soaks still arm `FORCE_MISMATCH` @520. Seed apply at FC shows live vs blob anim/joint diffs. Matching status trails with tics skew can be amplified if recovery reseals from a soft anim fork.

**Action:** after episode seal, assert local `status_total_tics` + root `anim_frame` match peer seal rows for human slots; or disable FORCE_MISMATCH for feel/anim soaks.

### Medium — Jump aerial / Special cancel windows

`988185944` @1495: JumpB tics skew then Linux enters Kirby special while Android still JumpB. Interrupt tables often use `anim_frame` and `status_total_tics` thresholds.

**Action:** re-soak aerial → special after this pass; if still forking, harden remaining Kirby special start status ends the same way.

### Lower — diagnostics / effect-link

| Gap | Action |
|-----|--------|
| Android sparse `fighter_slot_hash` vs Linux | Ensure both peers log slot hashes each tick in soak `debug.env` |
| No anim_wait witness at Turn/Wait end | Optional `SSB64_NETPLAY_ANIM_END_WITNESS=1` |
| Yoshi egg lay effect `anim_frame <= 0` | Effect-link, not fighter — separate quantize path |
| Status trail vs hash phase | Document capture_final vs post-update hash ordering |

## Out of scope here

- Feel-0 dash-dance mush while synced (admission/FIFO/tap) — separate from anim-end.
- Offline builds — must keep vanilla f32 subtract (`#else` path / `syObjAnimWaitAdd` macro identity).

## Follow-up (2026-07-12)

Turn/TurnRun carved from BeforeSim anim-end harden after soak `902430280` showed 0× `Turn→Dash` under VS while training dash-danced. See `docs/bugs/netplay_turn_dash_allow_anim_harden_2026-07-12.md`.

## Verify checklist for each new scope

1. Status_id trails match **and** FC `status_total_tics` / `anim_hash` match.
2. No early-end gameplay (moves ending a frame early on both peers is OK if matched; one-peer early is not).
3. SYNCTEST still clean; prefer soaks with `FORCE_MISMATCH` off once for anim-only signal.
