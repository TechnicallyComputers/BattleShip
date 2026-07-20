# Ness Hold SamePass DEFER: aim collapse + Hold Y freeze (soaks 1623117354 / 1303842452 / 2082786682)

**Status:** FIX DEEPENED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-18  
**Sessions:** `1623117354`, `1303842452`, `2082786682` (Linux ↔ Android, Dream Land Ness)

## Symptom

- `PEER_SNAPSHOT_DIVERGE` @645: **figh + camera**, `fighter_slot player=1`; map/world/item/rng/anim/wpn match
- Android SamePass DEFER `via=ring_hold_aim` @573, @576, @646; Linux no DEFER in that window
- Maps matched end-to-end (map-phase class closed)

## Evidence

| Epoch | load | post figh | post cam |
|-------|------|-----------|----------|
| 1–4 | … | match | match |
| 5 | 579 | match | **fork** (first durable) |
| 6 | 639 | match `0x68B59B44` | fork |
| 7 | 642 | **fork** `0x01241E3C` vs `0x11CB5530` | fork |

- After DEFER@573/@576: Android `hold_tick` **anchor==head**; Linux distinct sticky aim. Rematched by ~579.
- `rollback_post` @643 slot=1: logged kinematics matched, **`fhash` differed** — fields in `fhash_light` not previously printed (`tap_stick_*` / `hold_stick_*` / `anim_vel`).
- DEFER@646 ran **after** epoch-7 post digest already forked.

## Root cause (two layers)

1. **Hold DEFER aim collapse:** `ring_hold_aim` load + Ness harden/reconcile still promoted live PK head → `pkthunder_pos` (same promotion class as emergency soak `1217807688`, different restore path).
2. **PEER@645:** SpecialAirHiJibaku (236) → SpecialAirHiEnd (234) resim from matched figh baseline with cam already soft-diverged @579; silent light-hash fork at post-643 then visible p1 drift by 645. Suspect stick/anim_vel latch (see [`netplay_post_resim_pl_latch_stick_range_poison_fc_2026-07-17.md`](netplay_post_resim_pl_latch_stick_range_poison_fc_2026-07-17.md)).

## Follow-up (soak `1303842452`)

Aim preserve worked (`hold_aim_preserve` rewrote collapsed aim) but **Hold Y froze** after `via=ring_hold_aim`:

| Tick | Android topn_y | Linux topn_y |
|------|----------------|--------------|
| 576–582 | 1368.083 | 1368.083 |
| 583–589 | **1368.083 (frozen)** | falling → 1354.083 |
| FC@590 | `topn_ty` only, inputs MATCH | |

`POST_RESIM_LIVE sim=576` then DEFER loaded ring[575] + `ResimHardeningAfterSnapshotLoad` / ForceRebuild — rewound past exclusive frontier and resurrected gravity-delay tracking so CanonicalHoldFall kept `vel_y=0`. FC@630 (status_tics/topn) and late PEER@3823 cascaded.

## Follow-up (soak `2082786682`)

`via=emergency_hold_aim` + aim preserve engaged; Linux still no DEFER. Aim OK; **gravity onset forked**:

| At `rollback_load@645` slot=1 | Android | Linux |
|-------------------------------|---------|-------|
| `topn_y` | `0x4505829F` (2136.16) | `0x45057A9F` (−0.5) |
| `vel_air.y` | `0` | `0xBF000000` (−0.5) |

Android `sanitize_gravity` after emergency ApplySlot: `was=3 now=5 expected=5` (stale HoldEntryTracking). Linux natural countdown hit `0` at `status_tics=5`. PEER@645 **figh-only** `fighter_slot player=1`.

**Architecture:** SamePass Finish is asymmetric (one peer DEFERs/restores; the other keeps exclusive live). Band-aids that restore then sanitize Hold fields keep sliding the failure (aim → Y → gravity).

## Fix

1. **Aim preserve** (Deepen 4): capture/re-apply sticky `pkthunder_pos` across SamePass DEFER.
2. **Hold restore order (Deepen 5):** prefer **`emergency_hold_aim`** (exclusive-frontier emergency + aim preserve — **no** `ResimHardeningAfterSnapshotLoad`). Ring is Hold fallback without Harden. Non-Hold still `via=emergency`.
3. `fighter_detail` logs `anim_vel` + `tap_stick` + `hold_stick`.
4. **Deepen 6 (soak `2082786682`):** while SamePass Hold preserve is armed, **skip Hold delay/gravity sanitize** (ApplySlot still runs sanitize — tracking sync only). Re-anchor `HoldEntry` in `EndSamePassDeferHoldAimPreserve`. No post-restore `SanitizeAllFighters` on Hold DEFER paths.
5. **Superseded by Deepen 7** ([map skew doc](netplay_post_resim_live_save_without_battle_map_skew_2026-07-16.md)): boundary Finish removes SamePass emergency/ring restore for all statuses (Hold included).

## Verify

- Mid-pass resim complete: `FINISH_DEFER_TO_FUNCUPDATE_BEGIN` (not `via=emergency_hold_aim`).
- Hold fall / aim track peer without SamePass restore surgery.
- No FC `figh` inputs=MATCH on `topn_ty` mid-Hold.
