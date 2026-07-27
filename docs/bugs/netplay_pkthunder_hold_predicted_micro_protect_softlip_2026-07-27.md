# PK Thunder Hold predicted micro protect → SoftLip PHYSICS_FORK (2026-07-27)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** `session=213935103` `seed=3982859912` (Android client / Linux host)  
**Logs:** `/mnt/raid0/Software/BattleShip/logs/soak1-android.log` / `soak1-linux.log`  
**Bucket:** input contract / Hold aim invent  
**After:** [Hold aim protect v2](netplay_pkthunder_hold_aim_protect_universe_spike_2026-07-27.md)

## Symptom

Long soak soft-stable (`LOAD_HASH_DRIFT=0`, synctest OK, ~49 short resims) but:

- `PHYSICS_FORK_ONSET gut=3528` PKThunderHead (`hold_head→jibaku_risk`)
- `SOFTLIP_PHASE_FORK gut=3528 phase=wp_post_ceil` asymmetric `topn_x` / `vel_x`
- Later kneebend / rebirth forks; resim storm feel near end after jibaku

Scanner prefers SoftLip over FRAME_COMMIT — but peers were not on identical sticks.

## Timeline (Linux remote for P1 Hold `status=233`)

| Tick | Android (owner) | Linux (remote) |
|------|-----------------|----------------|
| 3526 | `(-15,-81)` pred=0 | `(-15,-81)` pred=0 |
| **3527** | `(-18,-80)` pred=0 | **`(-15,-81)` pred=1** `hold_last` |
| 3527 | — | `ness_jibaku_stick_protect` skip old→wire `(-15,-81)→(-18,-80)` |
| 3527 | — | `LEDGER_REFRESH_COMPLETED_SIM_CORRECT` … `skipped class=micro_stick` |
| 3528 | `(-18,-80)` | `(-18,-80)` — sticks match, **head already forked** |
| 3528 | SoftLip `wp_post_ceil` | SoftLip diverge → `PHYSICS_FORK` |

Δ was `(3,1)` — inside Hold protect `micro_db` (3).

## Root cause

v2 capped Hold protect at `micro_db` assuming Δ≤3 is noise. That is only safe on **confirmed** published rows (both peers already simmed the same stick).

**Predicted** Hold invent (`hold_last`) already steered PKThunderHead for that tick. Promote-only / protect+micro then:

1. skips short GGPO in `StickReplaceNeedsRewind`
2. LEDGER refreshes wire without rewind (`skipped class=micro_stick`)

One wrong aim frame near CLIFF SoftLip → sticky `fflags=0x8000` head fork. SoftLip is the detector, not the lever.

## Fix

`syNetplayNessStickReplaceProtectAllowsPromote(..., old_was_predicted)`:

- **Post-jibaku / bound:** unchanged uncapped Promote (predicted OK)
- **Hold Start/Hold/End + predicted:** never Protect → fall through to short GGPO
- **Hold + confirmed + Δ≤micro:** still Promote-only (noise floor)

Both StickReplace and LEDGER_REFRESH pass `old_frame->is_predicted` / `published.is_predicted`.

## Acceptance

Matched APK + Linux, long air Hold with invent then wire Δ≤`micro_db`:

- Predicted Hold invent → wire: `resim begin` / `QueueOrWiden` (not `ness_jibaku_stick_protect` + `micro_stick` Promote)
- Prefer **0×** `PHYSICS_FORK` on kind=14 from Hold invent alone
- Confirmed Hold Δ≤micro still `class=ness_jibaku_stick_protect`
- Post-jibaku uncapped Promote unchanged
- Host `pct_R` stays low (hold-neutral invent still allowed)

## Related

- [`netplay_pkthunder_hold_aim_protect_universe_spike_2026-07-27.md`](netplay_pkthunder_hold_aim_protect_universe_spike_2026-07-27.md) — Hold protect ≤micro (v2); this tightens predicted
- [`netplay_pkthunder_hold_same_intent_ggpo_2026-07-27.md`](netplay_pkthunder_hold_same_intent_ggpo_2026-07-27.md)
- [`netplay_input_contract_ledger_stickreplace_2026-07-26.md`](netplay_input_contract_ledger_stickreplace_2026-07-26.md)
- [`netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md`](netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md) — general predicted micro; Hold protect bypassed that path
